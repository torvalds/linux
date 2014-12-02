/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * Authors: Slava Grigorev <slava.grigorev@amd.com>
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include "radeon.h"
#include "atom.h"
#include "radeon_audio.h"

void r600_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		u8 enable_mask);
void dce6_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		u8 enable_mask);
u32 dce6_endpoint_rreg(struct radeon_device *rdev, u32 offset, u32 reg);
void dce6_endpoint_wreg(struct radeon_device *rdev,
		u32 offset, u32 reg, u32 v);
void dce3_2_afmt_write_sad_regs(struct drm_encoder *encoder,
		struct cea_sad *sads, int sad_count);
void evergreen_hdmi_write_sad_regs(struct drm_encoder *encoder,
		struct cea_sad *sads, int sad_count);
void dce6_afmt_write_sad_regs(struct drm_encoder *encoder,
		struct cea_sad *sads, int sad_count);
void dce3_2_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce3_2_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce4_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce4_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce6_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce6_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
		u8 *sadb, int sad_count);
void dce4_afmt_write_latency_fields(struct drm_encoder *encoder,
		struct drm_connector *connector, struct drm_display_mode *mode);
void dce6_afmt_write_latency_fields(struct drm_encoder *encoder,
		struct drm_connector *connector, struct drm_display_mode *mode);

static const u32 pin_offsets[7] =
{
	(0x5e00 - 0x5e00),
	(0x5e18 - 0x5e00),
	(0x5e30 - 0x5e00),
	(0x5e48 - 0x5e00),
	(0x5e60 - 0x5e00),
	(0x5e78 - 0x5e00),
	(0x5e90 - 0x5e00),
};

static u32 radeon_audio_rreg(struct radeon_device *rdev, u32 offset, u32 reg)
{
	return RREG32(reg);
}

static void radeon_audio_wreg(struct radeon_device *rdev, u32 offset,
		u32 reg, u32 v)
{
	WREG32(reg, v);
}

static struct radeon_audio_basic_funcs dce32_funcs = {
	.endpoint_rreg = radeon_audio_rreg,
	.endpoint_wreg = radeon_audio_wreg,
};

static struct radeon_audio_basic_funcs dce4_funcs = {
	.endpoint_rreg = radeon_audio_rreg,
	.endpoint_wreg = radeon_audio_wreg,
};

static struct radeon_audio_basic_funcs dce6_funcs = {
	.endpoint_rreg = dce6_endpoint_rreg,
	.endpoint_wreg = dce6_endpoint_wreg,
};

static struct radeon_audio_funcs dce32_hdmi_funcs = {
	.write_sad_regs = dce3_2_afmt_write_sad_regs,
	.write_speaker_allocation = dce3_2_afmt_hdmi_write_speaker_allocation,
};

static struct radeon_audio_funcs dce32_dp_funcs = {
	.write_sad_regs = dce3_2_afmt_write_sad_regs,
	.write_speaker_allocation = dce3_2_afmt_dp_write_speaker_allocation,
};

static struct radeon_audio_funcs dce4_hdmi_funcs = {
	.write_sad_regs = evergreen_hdmi_write_sad_regs,
	.write_speaker_allocation = dce4_afmt_hdmi_write_speaker_allocation,
	.write_latency_fields = dce4_afmt_write_latency_fields,
};

static struct radeon_audio_funcs dce4_dp_funcs = {
	.write_sad_regs = evergreen_hdmi_write_sad_regs,
	.write_speaker_allocation = dce4_afmt_dp_write_speaker_allocation,
	.write_latency_fields = dce4_afmt_write_latency_fields,
};

static struct radeon_audio_funcs dce6_hdmi_funcs = {
	.write_sad_regs = dce6_afmt_write_sad_regs,
	.write_speaker_allocation = dce6_afmt_hdmi_write_speaker_allocation,
	.write_latency_fields = dce6_afmt_write_latency_fields,
};

static struct radeon_audio_funcs dce6_dp_funcs = {
	.write_sad_regs = dce6_afmt_write_sad_regs,
	.write_speaker_allocation = dce6_afmt_dp_write_speaker_allocation,
	.write_latency_fields = dce6_afmt_write_latency_fields,
};

static void radeon_audio_interface_init(struct radeon_device *rdev)
{
	if (ASIC_IS_DCE6(rdev)) {
		rdev->audio.funcs = &dce6_funcs;
		rdev->audio.hdmi_funcs = &dce6_hdmi_funcs;
		rdev->audio.dp_funcs = &dce6_dp_funcs;
	} else if (ASIC_IS_DCE4(rdev)) {
		rdev->audio.funcs = &dce4_funcs;
		rdev->audio.hdmi_funcs = &dce4_hdmi_funcs;
		rdev->audio.dp_funcs = &dce4_dp_funcs;
	} else {
		rdev->audio.funcs = &dce32_funcs;
		rdev->audio.hdmi_funcs = &dce32_hdmi_funcs;
		rdev->audio.dp_funcs = &dce32_dp_funcs;
	}
}

static int radeon_audio_chipset_supported(struct radeon_device *rdev)
{
	return ASIC_IS_DCE2(rdev) && !ASIC_IS_NODCE(rdev);
}

int radeon_audio_init(struct radeon_device *rdev)
{
	int i;

	if (!radeon_audio || !radeon_audio_chipset_supported(rdev))
		return 0;

	rdev->audio.enabled = true;

	if (ASIC_IS_DCE83(rdev))		/* KB: 2 streams, 3 endpoints */
		rdev->audio.num_pins = 3;
	else if (ASIC_IS_DCE81(rdev))	/* KV: 4 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE8(rdev))	/* BN/HW: 6 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE64(rdev))	/* OL: 2 streams, 2 endpoints */
		rdev->audio.num_pins = 2;
	else if (ASIC_IS_DCE61(rdev))	/* TN: 4 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else if (ASIC_IS_DCE6(rdev))	/* SI: 6 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else
		rdev->audio.num_pins = 1;

	for (i = 0; i < rdev->audio.num_pins; i++) {
		rdev->audio.pin[i].channels = -1;
		rdev->audio.pin[i].rate = -1;
		rdev->audio.pin[i].bits_per_sample = -1;
		rdev->audio.pin[i].status_bits = 0;
		rdev->audio.pin[i].category_code = 0;
		rdev->audio.pin[i].connected = false;
		rdev->audio.pin[i].offset = pin_offsets[i];
		rdev->audio.pin[i].id = i;
	}

	radeon_audio_interface_init(rdev);

	/* disable audio.  it will be set up later */
	for (i = 0; i < rdev->audio.num_pins; i++)
		if (ASIC_IS_DCE6(rdev))
			dce6_audio_enable(rdev, &rdev->audio.pin[i], false);
		else
			r600_audio_enable(rdev, &rdev->audio.pin[i], false);

	return 0;
}

void radeon_audio_detect(struct drm_connector *connector,
	enum drm_connector_status status)
{
	if (!connector || !connector->encoder)
		return;

	if (status == connector_status_connected) {
		int sink_type;
		struct radeon_device *rdev = connector->encoder->dev->dev_private;
		struct radeon_connector *radeon_connector;
		struct radeon_encoder *radeon_encoder =
			to_radeon_encoder(connector->encoder);

		if (!drm_detect_monitor_audio(radeon_connector_edid(connector))) {
			radeon_encoder->audio = 0;
			return;
		}

		radeon_connector = to_radeon_connector(connector);
		sink_type = radeon_dp_getsinktype(radeon_connector);

		if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort &&
			sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT)
			radeon_encoder->audio = rdev->audio.dp_funcs;
		else
			radeon_encoder->audio = rdev->audio.hdmi_funcs;
		/* TODO: set up the sads, etc. and set the audio enable_mask */
	} else {
		/* TODO: reset the audio enable_mask */
	}
}

u32 radeon_audio_endpoint_rreg(struct radeon_device *rdev, u32 offset, u32 reg)
{
	if (rdev->audio.funcs->endpoint_rreg)
		return rdev->audio.funcs->endpoint_rreg(rdev, offset, reg);

	return 0;
}

void radeon_audio_endpoint_wreg(struct radeon_device *rdev, u32 offset,
	u32 reg, u32 v)
{
	if (rdev->audio.funcs->endpoint_wreg)
		rdev->audio.funcs->endpoint_wreg(rdev, offset, reg, v);
}

void radeon_audio_write_sad_regs(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	struct cea_sad *sads;
	int sad_count;

	list_for_each_entry(connector,
		&encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			radeon_connector = to_radeon_connector(connector);
			break;
		}
	}

	if (!radeon_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_sad(radeon_connector_edid(connector), &sads);
	if (sad_count <= 0) {
		DRM_ERROR("Couldn't read SADs: %d\n", sad_count);
		return;
	}
	BUG_ON(!sads);

	radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->write_sad_regs)
		radeon_encoder->audio->write_sad_regs(encoder, sads, sad_count);

	kfree(sads);
}

void radeon_audio_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
    struct drm_connector *connector;
    struct radeon_connector *radeon_connector = NULL;
    u8 *sadb = NULL;
    int sad_count;

    list_for_each_entry(connector,
		&encoder->dev->mode_config.connector_list, head) {
        if (connector->encoder == encoder) {
            radeon_connector = to_radeon_connector(connector);
            break;
        }
    }

    if (!radeon_connector) {
        DRM_ERROR("Couldn't find encoder's connector\n");
        return;
    }

    sad_count = drm_edid_to_speaker_allocation(
		radeon_connector_edid(connector), &sadb);
    if (sad_count < 0) {
        DRM_DEBUG("Couldn't read Speaker Allocation Data Block: %d\n",
			sad_count);
        sad_count = 0;
    }

	if (radeon_encoder->audio && radeon_encoder->audio->write_speaker_allocation)
		radeon_encoder->audio->write_speaker_allocation(encoder, sadb, sad_count);

    kfree(sadb);
}

void radeon_audio_write_latency_fields(struct drm_encoder *encoder,
	struct drm_display_mode *mode)
{
	struct radeon_encoder *radeon_encoder;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = 0;

	list_for_each_entry(connector,
		&encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			radeon_connector = to_radeon_connector(connector);
			break;
		}
	}

	if (!radeon_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->write_latency_fields)
		radeon_encoder->audio->write_latency_fields(encoder, connector, mode);
}
