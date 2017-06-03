// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cubeb/cubeb.h>

#include "AudioCommon/CubebStream.h"
#include "AudioCommon/CubebUtils.h"
#include "AudioCommon/DPL2Decoder.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/Thread.h"
#include "Core/ConfigManager.h"

// ~10 ms - needs to be at least 240 for surround
constexpr u32 BUFFER_SAMPLES = 512;

long CubebStream::DataCallback(cubeb_stream* stream, void* user_data, const void* /*input_buffer*/,
                               void* output_buffer, long num_frames)
{
  auto* self = static_cast<CubebStream*>(user_data);

  if (self->m_stereo)
  {
    self->m_mixer->Mix(static_cast<short*>(output_buffer), num_frames);
  }
  else
  {
    size_t required_capacity = num_frames * 2;
    if (required_capacity > self->m_short_buffer.capacity() ||
        required_capacity > self->m_floatstereo_buffer.capacity())
    {
      INFO_LOG(AUDIO, "Expanding conversion buffers size: %li frames", num_frames);
      self->m_short_buffer.reserve(required_capacity);
      self->m_floatstereo_buffer.reserve(required_capacity);
    }

    self->m_mixer->Mix(self->m_short_buffer.data(), num_frames);

    // s16 to float
    for (size_t i = 0; i < static_cast<size_t>(num_frames) * 2; ++i)
      self->m_floatstereo_buffer[i] = self->m_short_buffer[i] / static_cast<float>(1 << 15);

    // DPL2Decode output: LEFTFRONT, RIGHTFRONT, CENTREFRONT, (sub), LEFTREAR, RIGHTREAR
    DPL2Decode(self->m_floatstereo_buffer.data(), num_frames, static_cast<float*>(output_buffer));
  }

  return num_frames;
}

void CubebStream::StateCallback(cubeb_stream* stream, void* user_data, cubeb_state state)
{
}

bool CubebStream::Start()
{
  m_ctx = CubebUtils::GetContext();
  if (!m_ctx)
    return false;

  m_stereo = !SConfig::GetInstance().bDPL2Decoder;

  cubeb_stream_params params;
  params.rate = m_mixer->GetSampleRate();
  if (m_stereo)
  {
    params.channels = 2;
    params.format = CUBEB_SAMPLE_S16NE;
    params.layout = CUBEB_LAYOUT_STEREO;
  }
  else
  {
    params.channels = 6;
    params.format = CUBEB_SAMPLE_FLOAT32NE;
    params.layout = CUBEB_LAYOUT_3F2_LFE;
  }

  u32 minimum_latency = 0;
  if (cubeb_get_min_latency(m_ctx.get(), params, &minimum_latency) != CUBEB_OK)
    ERROR_LOG(AUDIO, "Error getting minimum latency");
  INFO_LOG(AUDIO, "Minimum latency: %i frames", minimum_latency);

  if (cubeb_stream_init(m_ctx.get(), &m_stream, "Dolphin Audio Output", nullptr, nullptr, nullptr,
                        &params, std::max(BUFFER_SAMPLES, minimum_latency), DataCallback,
                        StateCallback, this) != CUBEB_OK)
  {
    ERROR_LOG(AUDIO, "Error initializing cubeb stream");
    return false;
  }

  if (cubeb_stream_start(m_stream) != CUBEB_OK)
  {
    ERROR_LOG(AUDIO, "Error starting cubeb stream");
    return false;
  }
  return true;
}

void CubebStream::Stop()
{
  if (cubeb_stream_stop(m_stream) != CUBEB_OK)
  {
    ERROR_LOG(AUDIO, "Error stopping cubeb stream");
  }
  cubeb_stream_destroy(m_stream);
  m_ctx.reset();
}

void CubebStream::SetVolume(int volume)
{
  cubeb_stream_set_volume(m_stream, volume / 100.0f);
}