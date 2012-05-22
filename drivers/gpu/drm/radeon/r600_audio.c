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
#include "radeon_asic.h"
#include "atom.h"

/*
 * check if enc_priv stores radeon_encoder_atom_dig
 */
static bool radeon_dig_encoder(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_LVDS:
	case ENCODER_OBJECT_ID_INTERNAL_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
	case ENCODER_OBJECT_ID_INTERNAL_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		return true;
	}
	return false;
}

/*
 * check if the chipset is supported
 */
static int r600_audio_chipset_supported(struct radeon_device *rdev)
{
	return (rdev->family >= CHIP_R600 && !ASIC_IS_DCE5(rdev))
		|| rdev->family == CHIP_RS600
		|| rdev->family == CHIP_RS690
		|| rdev->family == CHIP_RS740;
}

/*
 * current number of channels
 */
int r600_audio_channels(struct radeon_device *rdev)
{
	return (RREG32(R600_AUDIO_RATE_BPS_CHANNEL) & 0x7) + 1;
}

/*
 * current bits per sample
 */
int r600_audio_bits_per_sample(struct radeon_device *rdev)
{
	uint32_t value = (RREG32(R600_AUDIO_RATE_BPS_CHANNEL) & 0xF0) >> 4;
	switch (value) {
	case 0x0: return  8;
	case 0x1: return 16;
	case 0x2: return 20;
	case 0x3: return 24;
	case 0x4: return 32;
	}

	dev_err(rdev->dev, "Unknown bits per sample 0x%x using 16 instead\n",
		(int)value);

	return 16;
}

/*
 * current sampling rate in HZ
 */
int r600_audio_rate(struct radeon_device *rdev)
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
uint8_t r600_audio_status_bits(struct radeon_device *rdev)
{
	return RREG32(R600_AUDIO_STATUS_BITS) & 0xff;
}

/*
 * iec 60958 category code
 */
uint8_t r600_audio_category_code(struct radeon_device *rdev)
{
	return (RREG32(R600_AUDIO_STATUS_BITS) >> 8) & 0xff;
}

/*
 * update all hdmi interfaces with current audio parameters
 */
void r600_audio_update_hdmi(struct work_struct *work)
{
	struct radeon_device *rdev = container_of(work, struct radeon_device,
						  audio_work);
	struct drm_device *dev = rdev->ddev;

	int channels = r600_audio_channels(rdev);
	int rate = r600_audio_rate(rdev);
	int bps = r600_audio_bits_per_sample(rdev);
	uint8_t status_bits = r600_audio_status_bits(rdev);
	uint8_t category_code = r600_audio_category_code(rdev);
	struct drm_encoder *encoder;
	int changes = 0;

	changes |= channels != rdev->audio.channels;
	changes |= rate != rdev->audio.rate;
	changes |= bps != rdev->audio.bits_per_sample;
	changes |= status_bits != rdev->audio.status_bits;
	changes |= category_code != rdev->audio.category_code;

	if (changes) {
		rdev->audio.channels = channels;
		rdev->audio.rate = rate;
		rdev->audio.bits_per_sample = bps;
		rdev->audio.status_bits = status_bits;
		rdev->audio.category_code = category_code;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (!radeon_dig_encoder(encoder))
			continue;
		if (changes || r600_hdmi_buffer_status_changed(encoder))
			r600_hdmi_update_audio_settings(encoder);
	}
}

/*
 * turn on/off audio engine
 */
static void r600_audio_engine_enable(struct radeon_device *rdev, bool enable)
{
	u32 value = 0;
	DRM_INFO("%s audio support\n", enable ? "Enabling" : "Disabling");
	if (ASIC_IS_DCE4(rdev)) {
		if (enable) {
			value |= 0x81000000; /* Required to enable audio */
			value |= 0x0e1000f0; /* fglrx sets that too */
		}
		WREG32(EVERGREEN_AUDIO_ENABLE, value);
	} else {
		WREG32_P(R600_AUDIO_ENABLE,
			 enable ? 0x81000000 : 0x0, ~0x81000000);
	}
	rdev->audio.enabled = enable;
}

/*
 * initialize the audio vars
 */
int r600_audio_init(struct radeon_device *rdev)
{
	if (!radeon_audio || !r600_audio_chipset_supported(rdev))
		return 0;

	r600_audio_engine_enable(rdev, true);

	rdev->audio.channels = -1;
	rdev->audio.rate = -1;
	rdev->audio.bits_per_sample = -1;
	rdev->audio.status_bits = 0;
	rdev->audio.category_code = 0;

	return 0;
}

/*
 * atach the audio codec to the clock source of the encoder
 */
void r600_audio_set_clock(struct drm_encoder *encoder, int clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
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
		dev_err(rdev->dev, "Unsupported encoder type 0x%02X\n",
			  radeon_encoder->encoder_id);
		return;
	}

	if (ASIC_IS_DCE4(rdev)) {
		/* TODO: other PLLs? */
		WREG32(EVERGREEN_AUDIO_PLL1_MUL, base_rate * 10);
		WREG32(EVERGREEN_AUDIO_PLL1_DIV, clock * 10);
		WREG32(EVERGREEN_AUDIO_PLL1_UNK, 0x00000071);

		/* Some magic trigger or src sel? */
		WREG32_P(0x5ac, 0x01, ~0x77);
	} else {
		switch (dig->dig_encoder) {
		case 0:
			WREG32(R600_AUDIO_PLL1_MUL, base_rate * 50);
			WREG32(R600_AUDIO_PLL1_DIV, clock * 100);
			WREG32(R600_AUDIO_CLK_SRCSEL, 0);
			break;

		case 1:
			WREG32(R600_AUDIO_PLL2_MUL, base_rate * 50);
			WREG32(R600_AUDIO_PLL2_DIV, clock * 100);
			WREG32(R600_AUDIO_CLK_SRCSEL, 1);
			break;
		default:
			dev_err(rdev->dev,
				"Unsupported DIG on encoder 0x%02X\n",
				radeon_encoder->encoder_id);
			return;
		}
	}
}

/*
 * release the audio timer
 * TODO: How to do this correctly on SMP systems?
 */
void r600_audio_fini(struct radeon_device *rdev)
{
	if (!rdev->audio.enabled)
		return;

	r600_audio_engine_enable(rdev, false);
}
