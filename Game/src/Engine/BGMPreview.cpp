#include "BGMPreview.hpp"
#include "../Data/Chart.hpp"
#include "../EnvironmentSetup.hpp"
#include "Configuration.h"
#include "GameAudioSampleCache.hpp"
#include <Logs.h>
#include <cmath>
#include <future>

#include "../Resources/GameDatabase.h"

BGMPreview::~BGMPreview()
{
    if (m_mutex) {
        std::lock_guard<std::mutex> lock(*m_mutex);
    }

    m_callback = [&](bool) {};
    delete m_mutex;
}

void BGMPreview::Load()
{
    OnStarted = false;
    OnPause = false;
    m_rate = 1;

    if (m_mutex == nullptr) {
        m_mutex = new std::mutex();
    }

    if (m_autoSamples.size() == 0) {
        m_autoSamples = std::vector<AutoSample>(10);
        m_autoSamples.reserve(50);
    }

    std::string Key = EnvironmentSetup::Get("Key");

    auto tr = std::thread([&] {
        int state = ++m_currentState;

        std::lock_guard<std::mutex> lock(*m_mutex);
        Ready = false;

        int          index = EnvironmentSetup::GetInt("Key");
        DB_MusicItem item = GameDatabase::GetInstance()->Find(index);

        std::filesystem::path file = GameDatabase::GetInstance()->GetPath();
        file /= "o2ma" + std::to_string(index) + ".ojn";

        if (file.string() != m_currentFilePath || GameAudioSampleCache::SetRate() != 1.0 || GameAudioSampleCache::IsEmpty()) {
            try {
                O2::OJN o2jamFile;
                o2jamFile.Load(file);

                if (!o2jamFile.IsValid()) {
                    return;
                }

                if (m_currentChart != nullptr && file.string() != m_currentFilePath) {
                    delete m_currentChart;
                }

                m_currentFilePath = file.string();
                m_currentChart = new Chart(o2jamFile, 2);
            } catch (std::exception &e) {
                Logs::Puts("[BGMPreview] Failed to load the audio chart: %s", e.what());
                return;
            }

            m_autoSamples.clear();

            for (auto &sample : m_currentChart->m_autoSamples) {
                m_autoSamples.push_back(sample);
            }

            for (auto &note : m_currentChart->m_notes) {
                if (note.Keysound != -1) {
                    AutoSample sm = {};
                    sm.StartTime = note.StartTime;
                    sm.Index = note.Keysound;
                    sm.Volume = note.Volume;
                    sm.Pan = note.Pan;

                    m_autoSamples.push_back(sm);
                }
            }

            std::sort(m_autoSamples.begin(), m_autoSamples.end(), [](const AutoSample &a, const AutoSample &b) {
                return a.StartTime < b.StartTime;
            });

            m_length = m_currentChart->GetLength();
            m_startOffset = m_autoSamples.size() ? m_autoSamples.front().StartTime : 0.0;

            GameAudioSampleCache::SetRate(1.0);
            GameAudioSampleCache::Load(m_currentChart, Configuration::Load("Game", "AudioPitch") == "1");
        }

        if (m_callback && m_currentState == state) {
            m_callback(true);
        }

        Ready = true;
    });

    tr.detach();
}

void BGMPreview::Update(double delta)
{
    if (OnPause || !OnStarted || !Ready)
        return;

    m_currentAudioPosition += (delta * m_rate) * 1000;

    for (int i = m_currentSampleIndex; i < m_autoSamples.size(); i++) {
        auto &sample = m_autoSamples[i];
        if (m_currentAudioPosition >= sample.StartTime) {
            if (sample.StartTime - m_currentAudioPosition < 5) {
                GameAudioSampleCache::Play(sample.Index, (int)::round(sample.Volume * 50.0f), (int)::round(sample.Pan * 100.0f));
            }

            m_currentSampleIndex++;
        } else {
            break;
        }
    }

    if (m_currentAudioPosition > m_length + 200) {
        Stop();
    }
}

void BGMPreview::Play()
{
    m_currentAudioPosition = m_startOffset;
    m_currentSampleIndex = 0;

    OnStarted = true;
}

void BGMPreview::Stop()
{
    m_currentState = -1;

    if (!IsPlaying())
        return;
    OnStarted = false;

    m_callback(false);
    GameAudioSampleCache::StopAll();
}

void BGMPreview::Reload()
{
    OnPause = true;

    GameAudioSampleCache::Load(m_currentChart, Configuration::Load("Game", "AudioPitch") == "1", true);

    OnPause = false;
}

bool BGMPreview::IsPlaying()
{
    return !(OnPause || !OnStarted || !Ready); // TODO fix
}

bool BGMPreview::IsReady()
{
    return Ready;
}

void BGMPreview::OnReady(std::function<void(bool)> callback)
{
    m_callback = callback;
}