/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * Copyright 2014 Rafał Miłecki
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
 */
#include <linux/hdmi.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "r600d.h"

static void dce3_2_afmt_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	u32 tmp;
	u8 *sadb = NULL;
	int sad_count;

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

	sad_count = drm_edid_to_speaker_allocation(radeon_connector->edid, &sadb);
	if (sad_count < 0) {
		DRM_DEBUG("Couldn't read Speaker Allocation Data Block: %d\n", sad_count);
		sad_count = 0;
	}

	/* program the speaker allocation */
	tmp = RREG32(AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER);
	tmp &= ~(DP_CONNECTION | SPEAKER_ALLOCATION_MASK);
	/* set HDMI mode */
	tmp |= HDMI_CONNECTION;
	if (sad_count)
		tmp |= SPEAKER_ALLOCATION(sadb[0]);
	else
		tmp |= SPEAKER_ALLOCATION(5); /* stereo */
	WREG32(AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER, tmp);

	kfree(sadb);
}

static void dce3_2_afmt_write_sad_regs(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	struct cea_sad *sads;
	int i, sad_count;

	static const u16 eld_reg_to_type[][2] = {
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR0, HDMI_AUDIO_CODING_TYPE_PCM },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR1, HDMI_AUDIO_CODING_TYPE_AC3 },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR2, HDMI_AUDIO_CODING_TYPE_MPEG1 },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR3, HDMI_AUDIO_CODING_TYPE_MP3 },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR4, HDMI_AUDIO_CODING_TYPE_MPEG2 },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR5, HDMI_AUDIO_CODING_TYPE_AAC_LC },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR6, HDMI_AUDIO_CODING_TYPE_DTS },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR7, HDMI_AUDIO_CODING_TYPE_ATRAC },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR9, HDMI_AUDIO_CODING_TYPE_EAC3 },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR10, HDMI_AUDIO_CODING_TYPE_DTS_HD },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR11, HDMI_AUDIO_CODING_TYPE_MLP },
		{ AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR13, HDMI_AUDIO_CODING_TYPE_WMA_PRO },
	};

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

		WREG32(eld_reg_to_type[i][0], value);
	}

	kfree(sads);
}

/*
 * update the info frames with the data from the current display mode
 */
void dce3_1_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	uint32_t offset;
	ssize_t err;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	/* disable audio prior to setting up hw */
	dig->afmt->pin = r600_audio_get_pin(rdev);
	r600_audio_enable(rdev, dig->afmt->pin, 0);

	r600_audio_set_dto(encoder, mode->clock);

	WREG32(HDMI0_VBI_PACKET_CONTROL + offset,
	       HDMI0_NULL_SEND); /* send null packets when required */

	WREG32(HDMI0_AUDIO_CRC_CONTROL + offset, 0x1000);

	if (ASIC_IS_DCE32(rdev)) {
		WREG32(HDMI0_AUDIO_PACKET_CONTROL + offset,
		       HDMI0_AUDIO_DELAY_EN(1) | /* default audio delay */
		       HDMI0_AUDIO_PACKETS_PER_LINE(3)); /* should be suffient for all audio modes and small enough for all hblanks */
		WREG32(AFMT_AUDIO_PACKET_CONTROL + offset,
		       AFMT_AUDIO_SAMPLE_SEND | /* send audio packets */
		       AFMT_60958_CS_UPDATE); /* allow 60958 channel status fields to be updated */
	} else {
		WREG32(HDMI0_AUDIO_PACKET_CONTROL + offset,
		       HDMI0_AUDIO_SAMPLE_SEND | /* send audio packets */
		       HDMI0_AUDIO_DELAY_EN(1) | /* default audio delay */
		       HDMI0_AUDIO_PACKETS_PER_LINE(3) | /* should be suffient for all audio modes and small enough for all hblanks */
		       HDMI0_60958_CS_UPDATE); /* allow 60958 channel status fields to be updated */
	}

	if (ASIC_IS_DCE32(rdev)) {
		dce3_2_afmt_write_speaker_allocation(encoder);
		dce3_2_afmt_write_sad_regs(encoder);
	}

	WREG32(HDMI0_ACR_PACKET_CONTROL + offset,
	       HDMI0_ACR_SOURCE | /* select SW CTS value - XXX verify that hw CTS works on all families */
	       HDMI0_ACR_AUTO_SEND); /* allow hw to sent ACR packets when required */

	WREG32(HDMI0_VBI_PACKET_CONTROL + offset,
	       HDMI0_NULL_SEND | /* send null packets when required */
	       HDMI0_GC_SEND | /* send general control packets */
	       HDMI0_GC_CONT); /* send general control packets every frame */

	/* TODO: HDMI0_AUDIO_INFO_UPDATE */
	WREG32(HDMI0_INFOFRAME_CONTROL0 + offset,
	       HDMI0_AVI_INFO_SEND | /* enable AVI info frames */
	       HDMI0_AVI_INFO_CONT | /* send AVI info frames every frame/field */
	       HDMI0_AUDIO_INFO_SEND | /* enable audio info frames (frames won't be set until audio is enabled) */
	       HDMI0_AUDIO_INFO_CONT); /* send audio info frames every frame/field */

	WREG32(HDMI0_INFOFRAME_CONTROL1 + offset,
	       HDMI0_AVI_INFO_LINE(2) | /* anything other than 0 */
	       HDMI0_AUDIO_INFO_LINE(2)); /* anything other than 0 */

	WREG32(HDMI0_GC + offset, 0); /* unset HDMI0_GC_AVMUTE */

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (err < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %zd\n", err);
		return;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %zd\n", err);
		return;
	}

	r600_hdmi_update_avi_infoframe(encoder, buffer, sizeof(buffer));
	r600_hdmi_update_ACR(encoder, mode->clock);

	/* it's unknown what these bits do excatly, but it's indeed quite useful for debugging */
	WREG32(HDMI0_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(HDMI0_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(HDMI0_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(HDMI0_RAMP_CONTROL3 + offset, 0x00000001);

	r600_hdmi_audio_workaround(encoder);

	/* enable audio after to setting up hw */
	r600_audio_enable(rdev, dig->afmt->pin, 0xf);
}
