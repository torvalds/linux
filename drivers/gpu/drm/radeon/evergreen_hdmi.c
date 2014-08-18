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
 *          Rafał Miłecki
 */
#include <linux/hdmi.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "evergreend.h"
#include "atom.h"

extern void dce6_afmt_write_speaker_allocation(struct drm_encoder *encoder);
extern void dce6_afmt_write_sad_regs(struct drm_encoder *encoder);
extern void dce6_afmt_select_pin(struct drm_encoder *encoder);
extern void dce6_afmt_write_latency_fields(struct drm_encoder *encoder,
					   struct drm_display_mode *mode);

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void evergreen_hdmi_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_hdmi_acr acr = r600_hdmi_acr(clock);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	WREG32(HDMI_ACR_32_0 + offset, HDMI_ACR_CTS_32(acr.cts_32khz));
	WREG32(HDMI_ACR_32_1 + offset, acr.n_32khz);

	WREG32(HDMI_ACR_44_0 + offset, HDMI_ACR_CTS_44(acr.cts_44_1khz));
	WREG32(HDMI_ACR_44_1 + offset, acr.n_44_1khz);

	WREG32(HDMI_ACR_48_0 + offset, HDMI_ACR_CTS_48(acr.cts_48khz));
	WREG32(HDMI_ACR_48_1 + offset, acr.n_48khz);
}

static void dce4_afmt_write_latency_fields(struct drm_encoder *encoder,
					   struct drm_display_mode *mode)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	u32 tmp = 0;

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
	WREG32(AZ_F0_CODEC_PIN0_CONTROL_RESPONSE_LIPSYNC, tmp);
}

static void dce4_afmt_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector = NULL;
	u32 tmp;
	u8 *sadb;
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

	sad_count = drm_edid_to_speaker_allocation(radeon_connector_edid(connector), &sadb);
	if (sad_count <= 0) {
		DRM_ERROR("Couldn't read Speaker Allocation Data Block: %d\n", sad_count);
		return;
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

static void evergreen_hdmi_write_sad_regs(struct drm_encoder *encoder)
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

	sad_count = drm_edid_to_sad(radeon_connector_edid(connector), &sads);
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
 * build a HDMI Video Info Frame
 */
static void evergreen_hdmi_update_avi_infoframe(struct drm_encoder *encoder,
						void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	uint8_t *frame = buffer + 3;
	uint8_t *header = buffer;

	WREG32(AFMT_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(AFMT_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(AFMT_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(AFMT_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8) | (header[1] << 24));
}

static void evergreen_audio_set_dto(struct drm_encoder *encoder, u32 clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
	u32 base_rate = 24000;
	u32 max_ratio = clock / base_rate;
	u32 dto_phase;
	u32 dto_modulo = clock;
	u32 wallclock_ratio;
	u32 dto_cntl;

	if (!dig || !dig->afmt)
		return;

	if (ASIC_IS_DCE6(rdev)) {
		dto_phase = 24 * 1000;
	} else {
		if (max_ratio >= 8) {
			dto_phase = 192 * 1000;
			wallclock_ratio = 3;
		} else if (max_ratio >= 4) {
			dto_phase = 96 * 1000;
			wallclock_ratio = 2;
		} else if (max_ratio >= 2) {
			dto_phase = 48 * 1000;
			wallclock_ratio = 1;
		} else {
			dto_phase = 24 * 1000;
			wallclock_ratio = 0;
		}
		dto_cntl = RREG32(DCCG_AUDIO_DTO0_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
		dto_cntl |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
		WREG32(DCCG_AUDIO_DTO0_CNTL, dto_cntl);
	}

	/* XXX two dtos; generally use dto0 for hdmi */
	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	WREG32(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_SOURCE_SEL(radeon_crtc->crtc_id));
	WREG32(DCCG_AUDIO_DTO0_PHASE, dto_phase);
	WREG32(DCCG_AUDIO_DTO0_MODULE, dto_modulo);
}


/*
 * update the info frames with the data from the current display mode
 */
void evergreen_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	uint32_t offset;
	ssize_t err;
	uint32_t val;
	int bpc = 8;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	/* hdmi deep color mode general control packets setup, if bpc > 8 */
	if (encoder->crtc) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
		bpc = radeon_crtc->bpc;
	}

	/* disable audio prior to setting up hw */
	if (ASIC_IS_DCE6(rdev)) {
		dig->afmt->pin = dce6_audio_get_pin(rdev);
		dce6_audio_enable(rdev, dig->afmt->pin, false);
	} else {
		dig->afmt->pin = r600_audio_get_pin(rdev);
		r600_audio_enable(rdev, dig->afmt->pin, false);
	}

	evergreen_audio_set_dto(encoder, mode->clock);

	WREG32(HDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_NULL_SEND); /* send null packets when required */

	WREG32(AFMT_AUDIO_CRC_CONTROL + offset, 0x1000);

	val = RREG32(HDMI_CONTROL + offset);
	val &= ~HDMI_DEEP_COLOR_ENABLE;
	val &= ~HDMI_DEEP_COLOR_DEPTH_MASK;

	switch (bpc) {
		case 0:
		case 6:
		case 8:
		case 16:
		default:
			DRM_DEBUG("%s: Disabling hdmi deep color for %d bpc.\n",
					 connector->name, bpc);
			break;
		case 10:
			val |= HDMI_DEEP_COLOR_ENABLE;
			val |= HDMI_DEEP_COLOR_DEPTH(HDMI_30BIT_DEEP_COLOR);
			DRM_DEBUG("%s: Enabling hdmi deep color 30 for 10 bpc.\n",
					 connector->name);
			break;
		case 12:
			val |= HDMI_DEEP_COLOR_ENABLE;
			val |= HDMI_DEEP_COLOR_DEPTH(HDMI_36BIT_DEEP_COLOR);
			DRM_DEBUG("%s: Enabling hdmi deep color 36 for 12 bpc.\n",
					 connector->name);
			break;
	}

	WREG32(HDMI_CONTROL + offset, val);

	WREG32(HDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_NULL_SEND | /* send null packets when required */
	       HDMI_GC_SEND | /* send general control packets */
	       HDMI_GC_CONT); /* send general control packets every frame */

	WREG32(HDMI_INFOFRAME_CONTROL0 + offset,
	       HDMI_AUDIO_INFO_SEND | /* enable audio info frames (frames won't be set until audio is enabled) */
	       HDMI_AUDIO_INFO_CONT); /* required for audio info values to be updated */

	WREG32(AFMT_INFOFRAME_CONTROL0 + offset,
	       AFMT_AUDIO_INFO_UPDATE); /* required for audio info values to be updated */

	WREG32(HDMI_INFOFRAME_CONTROL1 + offset,
	       HDMI_AUDIO_INFO_LINE(2)); /* anything other than 0 */

	WREG32(HDMI_GC + offset, 0); /* unset HDMI_GC_AVMUTE */

	WREG32(HDMI_AUDIO_PACKET_CONTROL + offset,
	       HDMI_AUDIO_DELAY_EN(1) | /* set the default audio delay */
	       HDMI_AUDIO_PACKETS_PER_LINE(3)); /* should be suffient for all audio modes and small enough for all hblanks */

	WREG32(AFMT_AUDIO_PACKET_CONTROL + offset,
	       AFMT_60958_CS_UPDATE); /* allow 60958 channel status fields to be updated */

	/* fglrx clears sth in AFMT_AUDIO_PACKET_CONTROL2 here */

	if (bpc > 8)
		WREG32(HDMI_ACR_PACKET_CONTROL + offset,
		       HDMI_ACR_AUTO_SEND); /* allow hw to sent ACR packets when required */
	else
		WREG32(HDMI_ACR_PACKET_CONTROL + offset,
		       HDMI_ACR_SOURCE | /* select SW CTS value */
		       HDMI_ACR_AUTO_SEND); /* allow hw to sent ACR packets when required */

	evergreen_hdmi_update_ACR(encoder, mode->clock);

	WREG32(AFMT_60958_0 + offset,
	       AFMT_60958_CS_CHANNEL_NUMBER_L(1));

	WREG32(AFMT_60958_1 + offset,
	       AFMT_60958_CS_CHANNEL_NUMBER_R(2));

	WREG32(AFMT_60958_2 + offset,
	       AFMT_60958_CS_CHANNEL_NUMBER_2(3) |
	       AFMT_60958_CS_CHANNEL_NUMBER_3(4) |
	       AFMT_60958_CS_CHANNEL_NUMBER_4(5) |
	       AFMT_60958_CS_CHANNEL_NUMBER_5(6) |
	       AFMT_60958_CS_CHANNEL_NUMBER_6(7) |
	       AFMT_60958_CS_CHANNEL_NUMBER_7(8));

	if (ASIC_IS_DCE6(rdev)) {
		dce6_afmt_write_speaker_allocation(encoder);
	} else {
		dce4_afmt_write_speaker_allocation(encoder);
	}

	WREG32(AFMT_AUDIO_PACKET_CONTROL2 + offset,
	       AFMT_AUDIO_CHANNEL_ENABLE(0xff));

	/* fglrx sets 0x40 in 0x5f80 here */

	if (ASIC_IS_DCE6(rdev)) {
		dce6_afmt_select_pin(encoder);
		dce6_afmt_write_sad_regs(encoder);
		dce6_afmt_write_latency_fields(encoder, mode);
	} else {
		evergreen_hdmi_write_sad_regs(encoder);
		dce4_afmt_write_latency_fields(encoder, mode);
	}

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

	evergreen_hdmi_update_avi_infoframe(encoder, buffer, sizeof(buffer));

	WREG32_OR(HDMI_INFOFRAME_CONTROL0 + offset,
		  HDMI_AVI_INFO_SEND | /* enable AVI info frames */
		  HDMI_AVI_INFO_CONT); /* required for audio info values to be updated */

	WREG32_P(HDMI_INFOFRAME_CONTROL1 + offset,
		 HDMI_AVI_INFO_LINE(2), /* anything other than 0 */
		 ~HDMI_AVI_INFO_LINE_MASK);

	WREG32_OR(AFMT_AUDIO_PACKET_CONTROL + offset,
		  AFMT_AUDIO_SAMPLE_SEND); /* send audio packets */

	/* it's unknown what these bits do excatly, but it's indeed quite useful for debugging */
	WREG32(AFMT_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(AFMT_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(AFMT_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(AFMT_RAMP_CONTROL3 + offset, 0x00000001);

	/* enable audio after to setting up hw */
	if (ASIC_IS_DCE6(rdev))
		dce6_audio_enable(rdev, dig->afmt->pin, true);
	else
		r600_audio_enable(rdev, dig->afmt->pin, true);
}

void evergreen_hdmi_enable(struct drm_encoder *encoder, bool enable)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (enable && dig->afmt->enabled)
		return;
	if (!enable && !dig->afmt->enabled)
		return;

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, radeon_encoder->encoder_id);
}
