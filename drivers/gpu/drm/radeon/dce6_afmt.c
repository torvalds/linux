/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 */
#include <linux/hdmi.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "sid.h"

static u32 dce6_endpoint_rreg(struct radeon_device *rdev,
			      u32 block_offset, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&rdev->end_idx_lock, flags);
	WREG32(AZ_F0_CODEC_ENDPOINT_INDEX + block_offset, reg);
	r = RREG32(AZ_F0_CODEC_ENDPOINT_DATA + block_offset);
	spin_unlock_irqrestore(&rdev->end_idx_lock, flags);

	return r;
}

static void dce6_endpoint_wreg(struct radeon_device *rdev,
			       u32 block_offset, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->end_idx_lock, flags);
	if (ASIC_IS_DCE8(rdev))
		WREG32(AZ_F0_CODEC_ENDPOINT_INDEX + block_offset, reg);
	else
		WREG32(AZ_F0_CODEC_ENDPOINT_INDEX + block_offset,
		       AZ_ENDPOINT_REG_WRITE_EN | AZ_ENDPOINT_REG_INDEX(reg));
	WREG32(AZ_F0_CODEC_ENDPOINT_DATA + block_offset, v);
	spin_unlock_irqrestore(&rdev->end_idx_lock, flags);
}

#define RREG32_ENDPOINT(block, reg) dce6_endpoint_rreg(rdev, (block), (reg))
#define WREG32_ENDPOINT(block, reg, v) dce6_endpoint_wreg(rdev, (block), (reg), (v))


static void dce6_afmt_get_connected_pins(struct radeon_device *rdev)
{
	int i;
	u32 offset, tmp;

	for (i = 0; i < rdev->audio.num_pins; i++) {
		offset = rdev->audio.pin[i].offset;
		tmp = RREG32_ENDPOINT(offset,
				      AZ_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT);
		if (((tmp & PORT_CONNECTIVITY_MASK) >> PORT_CONNECTIVITY_SHIFT) == 1)
			rdev->audio.pin[i].connected = false;
		else
			rdev->audio.pin[i].connected = true;
	}
}

struct r600_audio_pin *dce6_audio_get_pin(struct radeon_device *rdev)
{
	int i;

	dce6_afmt_get_connected_pins(rdev);

	for (i = 0; i < rdev->audio.num_pins; i++) {
		if (rdev->audio.pin[i].connected)
			return &rdev->audio.pin[i];
	}
	DRM_ERROR("No connected audio pins found!\n");
	return NULL;
}

void dce6_afmt_select_pin(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 offset;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->offset;

	WREG32(AFMT_AUDIO_SRC_CONTROL + offset,
	       AFMT_AUDIO_SRC_SELECT(dig->afmt->pin->id));
}

void dce6_afmt_write_latency_fields(struct drm_encoder *encoder,
				    struct drm_display_mode *mode)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	u32 tmp = 0, offset;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			radeon_connector = to_radeon_connector(connector);
			break;
		}
	}

	if (!radeon_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (connector->latency_present[1])
			tmp = VIDEO_LIPSYNC(connector->video_latency[1]) |
				AUDIO_LIPSYNC(connector->audio_latency[1]);
		else
			tmp = VIDEO_LIPSYNC(255) | AUDIO_LIPSYNC(255);
	} else {
		if (connector->latency_present[0])
			tmp = VIDEO_LIPSYNC(connector->video_latency[0]) |
				AUDIO_LIPSYNC(connector->audio_latency[0]);
		else
			tmp = VIDEO_LIPSYNC(255) | AUDIO_LIPSYNC(255);
	}
	WREG32_ENDPOINT(offset, AZ_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC, tmp);
}

void dce6_afmt_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	u32 offset, tmp;
	u8 *sadb;
	int sad_count;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder)
			radeon_connector = to_radeon_connector(connector);
	}

	if (!radeon_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_speaker_allocation(radeon_connector->edid, &sadb);
	if (sad_count <= 0) {
		DRM_ERROR("Couldn't read Speaker Allocation Data Block: %d\n", sad_count);
		return;
	}

	/* program the speaker allocation */
	tmp = RREG32_ENDPOINT(offset, AZ_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER);
	tmp &= ~(DP_CONNECTION | SPEAKER_ALLOCATION_MASK);
	/* set HDMI mode */
	tmp |= HDMI_CONNECTION;
	if (sad_count)
		tmp |= SPEAKER_ALLOCATION(sadb[0]);
	else
		tmp |= SPEAKER_ALLOCATION(5); /* stereo */
	WREG32_ENDPOINT(offset, AZ_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER, tmp);

	kfree(sadb);
}

void dce6_afmt_write_sad_regs(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 offset;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	struct cea_sad *sads;
	int i, sad_count;

	static const u16 eld_reg_to_type[][2] = {
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0, HDMI_AUDIO_CODING_TYPE_PCM },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR1, HDMI_AUDIO_CODING_TYPE_AC3 },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR2, HDMI_AUDIO_CODING_TYPE_MPEG1 },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR3, HDMI_AUDIO_CODING_TYPE_MP3 },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR4, HDMI_AUDIO_CODING_TYPE_MPEG2 },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR5, HDMI_AUDIO_CODING_TYPE_AAC_LC },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR6, HDMI_AUDIO_CODING_TYPE_DTS },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR7, HDMI_AUDIO_CODING_TYPE_ATRAC },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR9, HDMI_AUDIO_CODING_TYPE_EAC3 },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR10, HDMI_AUDIO_CODING_TYPE_DTS_HD },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR11, HDMI_AUDIO_CODING_TYPE_MLP },
		{ AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR13, HDMI_AUDIO_CODING_TYPE_WMA_PRO },
	};

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder)
			radeon_connector = to_radeon_connector(connector);
	}

	if (!radeon_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_sad(radeon_connector->edid, &sads);
	if (sad_count <= 0) {
		DRM_ERROR("Couldn't read SADs: %d\n", sad_count);
		return;
	}
	BUG_ON(!sads);

	for (i = 0; i < ARRAY_SIZE(eld_reg_to_type); i++) {
		u32 value = 0;
		u8 stereo_freqs = 0;
		int max_channels = -1;
		int j;

		for (j = 0; j < sad_count; j++) {
			struct cea_sad *sad = &sads[j];

			if (sad->format == eld_reg_to_type[i][1]) {
				if (sad->channels > max_channels) {
					value = MAX_CHANNELS(sad->channels) |
						DESCRIPTOR_BYTE_2(sad->byte2) |
						SUPPORTED_FREQUENCIES(sad->freq);
					max_channels = sad->channels;
				}

				if (sad->format == HDMI_AUDIO_CODING_TYPE_PCM)
					stereo_freqs |= sad->freq;
				else
					break;
			}
		}

		value |= SUPPORTED_FREQUENCIES_STEREO(stereo_freqs);

		WREG32_ENDPOINT(offset, eld_reg_to_type[i][0], value);
	}

	kfree(sads);
}

static int dce6_audio_chipset_supported(struct radeon_device *rdev)
{
	return !ASIC_IS_NODCE(rdev);
}

static void dce6_audio_enable(struct radeon_device *rdev,
			      struct r600_audio_pin *pin,
			      bool enable)
{
	WREG32_ENDPOINT(pin->offset, AZ_F0_CODEC_PIN_CONTROL_HOTPLUG_CONTROL,
			enable ? AUDIO_ENABLED : 0);
	DRM_INFO("%s audio %d support\n", enable ? "Enabling" : "Disabling", pin->id);
}

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

int dce6_audio_init(struct radeon_device *rdev)
{
	int i;

	if (!radeon_audio || !dce6_audio_chipset_supported(rdev))
		return 0;

	rdev->audio.enabled = true;

	if (ASIC_IS_DCE8(rdev))
		rdev->audio.num_pins = 6;
	else if (ASIC_IS_DCE61(rdev))
		rdev->audio.num_pins = 4;
	else
		rdev->audio.num_pins = 6;

	for (i = 0; i < rdev->audio.num_pins; i++) {
		rdev->audio.pin[i].channels = -1;
		rdev->audio.pin[i].rate = -1;
		rdev->audio.pin[i].bits_per_sample = -1;
		rdev->audio.pin[i].status_bits = 0;
		rdev->audio.pin[i].category_code = 0;
		rdev->audio.pin[i].connected = false;
		rdev->audio.pin[i].offset = pin_offsets[i];
		rdev->audio.pin[i].id = i;
		dce6_audio_enable(rdev, &rdev->audio.pin[i], true);
	}

	return 0;
}

void dce6_audio_fini(struct radeon_device *rdev)
{
	int i;

	if (!rdev->audio.enabled)
		return;

	for (i = 0; i < rdev->audio.num_pins; i++)
		dce6_audio_enable(rdev, &rdev->audio.pin[i], false);

	rdev->audio.enabled = false;
}
