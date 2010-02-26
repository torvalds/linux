/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Christian König.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */
#include "drmP.h"
#include "radeon.h"
#include "radeon_reg.h"
#include "atom.h"

#define AUDIO_TIMER_INTERVALL 100 /* 1/10 sekund should be enough */

/*
 * check if the chipset is supported
 */
static int r600_audio_chipset_supported(struct radeon_device *rdev)
{
	return rdev->family >= CHIP_R600
		|| rdev->family == CHIP_RS600
		|| rdev->family == CHIP_RS690
		|| rdev->family == CHIP_RS740;
}

/*
 * current number of channels
 */
static int r600_audio_channels(struct radeon_device *rdev)
{
	return (RREG32(R600_AUDIO_RATE_BPS_CHANNEL) & 0x7) + 1;
}

/*
 * current bits per sample
 */
static int r600_audio_bits_per_sample(struct radeon_device *rdev)
{
	uint32_t value = (RREG32(R600_AUDIO_RATE_BPS_CHANNEL) & 0xF0) >> 4;
	switch (value) {
	case 0x0: return  8;
	case 0x1: return 16;
	case 0x2: return 20;
	case 0x3: return 24;
	case 0x4: return 32;
	}

	DRM_ERROR("Unknown bits per sample 0x%x using 16 instead.\n", (int)value);

	return 16;
}

/*
 * current sampling rate in HZ
 */
static int r600_audio_rate(struct radeon_device *rdev)
{
	uint32_t value = RREG32(R600_AUDIO_RATE_BPS_CHANNEL);
	uint32_t result;

	if (value & 0x4000)
		result = 44100;
	else
		result = 48000;

	result *= ((value >> 11) & 0x7) + 1;
	result /= ((value >> 8) & 0x7) + 1;

	return result;
}

/*
 * iec 60958 status bits
 */
static uint8_t r600_audio_status_bits(struct radeon_device *rdev)
{
	return RREG32(R600_AUDIO_STATUS_BITS) & 0xff;
}

/*
 * iec 60958 category code
 */
static uint8_t r600_audio_category_code(struct radeon_device *rdev)
{
	return (RREG32(R600_AUDIO_STATUS_BITS) >> 8) & 0xff;
}

/*
 * update all hdmi interfaces with current audio parameters
 */
static void r600_audio_update_hdmi(unsigned long param)
{
	struct radeon_device *rdev = (struct radeon_device *)param;
	struct drm_device *dev = rdev->ddev;

	int channels = r600_audio_channels(rdev);
	int rate = r600_audio_rate(rdev);
	int bps = r600_audio_bits_per_sample(rdev);
	uint8_t status_bits = r600_audio_status_bits(rdev);
	uint8_t category_code = r600_audio_category_code(rdev);

	struct drm_encoder *encoder;
	int changes = 0;

	changes |= channels != rdev->audio_channels;
	changes |= rate != rdev->audio_rate;
	changes |= bps != rdev->audio_bits_per_sample;
	changes |= status_bits != rdev->audio_status_bits;
	changes |= category_code != rdev->audio_category_code;

	if (changes) {
		rdev->audio_channels = channels;
		rdev->audio_rate = rate;
		rdev->audio_bits_per_sample = bps;
		rdev->audio_status_bits = status_bits;
		rdev->audio_category_code = category_code;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (changes || r600_hdmi_buffer_status_changed(encoder))
			r600_hdmi_update_audio_settings(
				encoder, channels,
				rate, bps, status_bits,
				category_code);
	}

	mod_timer(&rdev->audio_timer,
		jiffies + msecs_to_jiffies(AUDIO_TIMER_INTERVALL));
}

/*
 * turn on/off audio engine
 */
static void r600_audio_engine_enable(struct radeon_device *rdev, bool enable)
{
	DRM_INFO("%s audio support", enable ? "Enabling" : "Disabling");
	WREG32_P(R600_AUDIO_ENABLE, enable ? 0x81000000 : 0x0, ~0x81000000);
}

/*
 * initialize the audio vars and register the update timer
 */
int r600_audio_init(struct radeon_device *rdev)
{
	if (!radeon_audio || !r600_audio_chipset_supported(rdev))
		return 0;

	r600_audio_engine_enable(rdev, true);

	rdev->audio_channels = -1;
	rdev->audio_rate = -1;
	rdev->audio_bits_per_sample = -1;
	rdev->audio_status_bits = 0;
	rdev->audio_category_code = 0;

	setup_timer(
		&rdev->audio_timer,
		r600_audio_update_hdmi,
		(unsigned long)rdev);

	mod_timer(&rdev->audio_timer, jiffies + 1);

	return 0;
}

/*
 * determin how the encoders and audio interface is wired together
 */
int r600_audio_tmds_index(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder *other;

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		return 0;

	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		/* special case check if an TMDS1 is present */
		list_for_each_entry(other, &dev->mode_config.encoder_list, head) {
			if (to_radeon_encoder(other)->encoder_id ==
				ENCODER_OBJECT_ID_INTERNAL_TMDS1)
				return 1;
		}
		return 0;

	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		return 1;

	default:
		DRM_ERROR("Unsupported encoder type 0x%02X\n",
			  radeon_encoder->encoder_id);
		return -1;
	}
}

/*
 * atach the audio codec to the clock source of the encoder
 */
void r600_audio_set_clock(struct drm_encoder *encoder, int clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	int base_rate = 48000;

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		WREG32_P(R600_AUDIO_TIMING, 0, ~0x301);
		break;

	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
		WREG32_P(R600_AUDIO_TIMING, 0x100, ~0x301);
		break;

	default:
		DRM_ERROR("Unsupported encoder type 0x%02X\n",
			  radeon_encoder->encoder_id);
		return;
	}

	switch (r600_audio_tmds_index(encoder)) {
	case 0:
		WREG32(R600_AUDIO_PLL1_MUL, base_rate*50);
		WREG32(R600_AUDIO_PLL1_DIV, clock*100);
		WREG32(R600_AUDIO_CLK_SRCSEL, 0);
		break;

	case 1:
		WREG32(R600_AUDIO_PLL2_MUL, base_rate*50);
		WREG32(R600_AUDIO_PLL2_DIV, clock*100);
		WREG32(R600_AUDIO_CLK_SRCSEL, 1);
		break;
	}
}

/*
 * release the audio timer
 * TODO: How to do this correctly on SMP systems?
 */
void r600_audio_fini(struct radeon_device *rdev)
{
	if (!radeon_audio || !r600_audio_chipset_supported(rdev))
		return;

	del_timer(&rdev->audio_timer);
	WREG32_P(R600_AUDIO_ENABLE, 0x0, ~0x81000000);

	r600_audio_engine_enable(rdev, false);
}
