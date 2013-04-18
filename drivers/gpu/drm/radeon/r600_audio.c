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
#include <drm/drmP.h>
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
	return ASIC_IS_DCE2(rdev) && !ASIC_IS_DCE6(rdev);
}

struct r600_audio r600_audio_status(struct radeon_device *rdev)
{
	struct r600_audio status;
	uint32_t value;

	value = RREG32(R600_AUDIO_RATE_BPS_CHANNEL);

	/* number of channels */
	status.channels = (value & 0x7) + 1;

	/* bits per sample */
	switch ((value & 0xF0) >> 4) {
	case 0x0:
		status.bits_per_sample = 8;
		break;
	case 0x1:
		status.bits_per_sample = 16;
		break;
	case 0x2:
		status.bits_per_sample = 20;
		break;
	case 0x3:
		status.bits_per_sample = 24;
		break;
	case 0x4:
		status.bits_per_sample = 32;
		break;
	default:
		dev_err(rdev->dev, "Unknown bits per sample 0x%x, using 16\n",
			(int)value);
		status.bits_per_sample = 16;
	}

	/* current sampling rate in HZ */
	if (value & 0x4000)
		status.rate = 44100;
	else
		status.rate = 48000;
	status.rate *= ((value >> 11) & 0x7) + 1;
	status.rate /= ((value >> 8) & 0x7) + 1;

	value = RREG32(R600_AUDIO_STATUS_BITS);

	/* iec 60958 status bits */
	status.status_bits = value & 0xff;

	/* iec 60958 category code */
	status.category_code = (value >> 8) & 0xff;

	return status;
}

/*
 * update all hdmi interfaces with current audio parameters
 */
void r600_audio_update_hdmi(struct work_struct *work)
{
	struct radeon_device *rdev = container_of(work, struct radeon_device,
						  audio_work);
	struct drm_device *dev = rdev->ddev;
	struct r600_audio audio_status = r600_audio_status(rdev);
	struct drm_encoder *encoder;
	bool changed = false;

	if (rdev->audio_status.channels != audio_status.channels ||
	    rdev->audio_status.rate != audio_status.rate ||
	    rdev->audio_status.bits_per_sample != audio_status.bits_per_sample ||
	    rdev->audio_status.status_bits != audio_status.status_bits ||
	    rdev->audio_status.category_code != audio_status.category_code) {
		rdev->audio_status = audio_status;
		changed = true;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (!radeon_dig_encoder(encoder))
			continue;
		if (changed || r600_hdmi_buffer_status_changed(encoder))
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
	rdev->audio_enabled = enable;
}

/*
 * initialize the audio vars
 */
int r600_audio_init(struct radeon_device *rdev)
{
	if (!radeon_audio || !r600_audio_chipset_supported(rdev))
		return 0;

	r600_audio_engine_enable(rdev, true);

	rdev->audio_status.channels = -1;
	rdev->audio_status.rate = -1;
	rdev->audio_status.bits_per_sample = -1;
	rdev->audio_status.status_bits = 0;
	rdev->audio_status.category_code = 0;

	return 0;
}

/*
 * release the audio timer
 * TODO: How to do this correctly on SMP systems?
 */
void r600_audio_fini(struct radeon_device *rdev)
{
	if (!rdev->audio_enabled)
		return;

	r600_audio_engine_enable(rdev, false);
}
