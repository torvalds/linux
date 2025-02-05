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

#include <drm/drm_edid.h>
#include <drm/radeon_drm.h>
#include "evergreen_hdmi.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include "evergreend.h"
#include "atom.h"

/* enable the audio stream */
void dce4_audio_enable(struct radeon_device *rdev,
			      struct r600_audio_pin *pin,
			      u8 enable_mask)
{
	u32 tmp = RREG32(AZ_HOT_PLUG_CONTROL);

	if (!pin)
		return;

	if (enable_mask) {
		tmp |= AUDIO_ENABLED;
		if (enable_mask & 1)
			tmp |= PIN0_AUDIO_ENABLED;
		if (enable_mask & 2)
			tmp |= PIN1_AUDIO_ENABLED;
		if (enable_mask & 4)
			tmp |= PIN2_AUDIO_ENABLED;
		if (enable_mask & 8)
			tmp |= PIN3_AUDIO_ENABLED;
	} else {
		tmp &= ~(AUDIO_ENABLED |
			 PIN0_AUDIO_ENABLED |
			 PIN1_AUDIO_ENABLED |
			 PIN2_AUDIO_ENABLED |
			 PIN3_AUDIO_ENABLED);
	}

	WREG32(AZ_HOT_PLUG_CONTROL, tmp);
}

void evergreen_hdmi_update_acr(struct drm_encoder *encoder, long offset,
	const struct radeon_hdmi_acr *acr)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	int bpc = 8;

	if (encoder->crtc) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
		bpc = radeon_crtc->bpc;
	}

	if (bpc > 8)
		WREG32(HDMI_ACR_PACKET_CONTROL + offset,
			HDMI_ACR_AUTO_SEND);	/* allow hw to sent ACR packets when required */
	else
		WREG32(HDMI_ACR_PACKET_CONTROL + offset,
			HDMI_ACR_SOURCE |		/* select SW CTS value */
			HDMI_ACR_AUTO_SEND);	/* allow hw to sent ACR packets when required */

	WREG32(HDMI_ACR_32_0 + offset, HDMI_ACR_CTS_32(acr->cts_32khz));
	WREG32(HDMI_ACR_32_1 + offset, acr->n_32khz);

	WREG32(HDMI_ACR_44_0 + offset, HDMI_ACR_CTS_44(acr->cts_44_1khz));
	WREG32(HDMI_ACR_44_1 + offset, acr->n_44_1khz);

	WREG32(HDMI_ACR_48_0 + offset, HDMI_ACR_CTS_48(acr->cts_48khz));
	WREG32(HDMI_ACR_48_1 + offset, acr->n_48khz);
}

void dce4_afmt_write_latency_fields(struct drm_encoder *encoder,
		struct drm_connector *connector, struct drm_display_mode *mode)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	u32 tmp = 0;

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
	WREG32_ENDPOINT(0, AZ_F0_CODEC_PIN0_CONTROL_RESPONSE_LIPSYNC, tmp);
}

void dce4_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
	u8 *sadb, int sad_count)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	u32 tmp;

	/* program the speaker allocation */
	tmp = RREG32_ENDPOINT(0, AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER);
	tmp &= ~(DP_CONNECTION | SPEAKER_ALLOCATION_MASK);
	/* set HDMI mode */
	tmp |= HDMI_CONNECTION;
	if (sad_count)
		tmp |= SPEAKER_ALLOCATION(sadb[0]);
	else
		tmp |= SPEAKER_ALLOCATION(5); /* stereo */
	WREG32_ENDPOINT(0, AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER, tmp);
}

void dce4_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
	u8 *sadb, int sad_count)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	u32 tmp;

	/* program the speaker allocation */
	tmp = RREG32_ENDPOINT(0, AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER);
	tmp &= ~(HDMI_CONNECTION | SPEAKER_ALLOCATION_MASK);
	/* set DP mode */
	tmp |= DP_CONNECTION;
	if (sad_count)
		tmp |= SPEAKER_ALLOCATION(sadb[0]);
	else
		tmp |= SPEAKER_ALLOCATION(5); /* stereo */
	WREG32_ENDPOINT(0, AZ_F0_CODEC_PIN0_CONTROL_CHANNEL_SPEAKER, tmp);
}

void evergreen_hdmi_write_sad_regs(struct drm_encoder *encoder,
	struct cea_sad *sads, int sad_count)
{
	int i;
	struct radeon_device *rdev = encoder->dev->dev_private;
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

		WREG32_ENDPOINT(0, eld_reg_to_type[i][0], value);
	}
}

/*
 * build a AVI Info Frame
 */
void evergreen_set_avi_packet(struct radeon_device *rdev, u32 offset,
			      unsigned char *buffer, size_t size)
{
	uint8_t *frame = buffer + 3;

	WREG32(AFMT_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(AFMT_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(AFMT_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(AFMT_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8) | (buffer[1] << 24));

	WREG32_P(HDMI_INFOFRAME_CONTROL1 + offset,
		 HDMI_AVI_INFO_LINE(2),	/* anything other than 0 */
		 ~HDMI_AVI_INFO_LINE_MASK);
}

void dce4_hdmi_audio_set_dto(struct radeon_device *rdev,
	struct radeon_crtc *crtc, unsigned int clock)
{
	unsigned int max_ratio = clock / 24000;
	u32 dto_phase;
	u32 wallclock_ratio;
	u32 value;

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

	value = RREG32(DCCG_AUDIO_DTO0_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
	value |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
	value &= ~DCCG_AUDIO_DTO1_USE_512FBR_DTO;
	WREG32(DCCG_AUDIO_DTO0_CNTL, value);

	/* Two dtos; generally use dto0 for HDMI */
	value = 0;

	if (crtc)
		value |= DCCG_AUDIO_DTO0_SOURCE_SEL(crtc->crtc_id);

	WREG32(DCCG_AUDIO_DTO_SOURCE, value);

	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	WREG32(DCCG_AUDIO_DTO0_PHASE, dto_phase);
	WREG32(DCCG_AUDIO_DTO0_MODULE, clock);
}

void dce4_dp_audio_set_dto(struct radeon_device *rdev,
			   struct radeon_crtc *crtc, unsigned int clock)
{
	u32 value;

	value = RREG32(DCCG_AUDIO_DTO1_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
	value |= DCCG_AUDIO_DTO1_USE_512FBR_DTO;
	WREG32(DCCG_AUDIO_DTO1_CNTL, value);

	/* Two dtos; generally use dto1 for DP */
	value = 0;
	value |= DCCG_AUDIO_DTO_SEL;

	if (crtc)
		value |= DCCG_AUDIO_DTO0_SOURCE_SEL(crtc->crtc_id);

	WREG32(DCCG_AUDIO_DTO_SOURCE, value);

	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	if (ASIC_IS_DCE41(rdev)) {
		unsigned int div = (RREG32(DCE41_DENTIST_DISPCLK_CNTL) &
			DENTIST_DPREFCLK_WDIVIDER_MASK) >>
			DENTIST_DPREFCLK_WDIVIDER_SHIFT;
		div = radeon_audio_decode_dfs_div(div);

		if (div)
			clock = 100 * clock / div;
	}

	WREG32(DCCG_AUDIO_DTO1_PHASE, 24000);
	WREG32(DCCG_AUDIO_DTO1_MODULE, clock);
}

void dce4_set_vbi_packet(struct drm_encoder *encoder, u32 offset)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;

	WREG32(HDMI_VBI_PACKET_CONTROL + offset,
		HDMI_NULL_SEND |	/* send null packets when required */
		HDMI_GC_SEND |		/* send general control packets */
		HDMI_GC_CONT);		/* send general control packets every frame */
}

void dce4_hdmi_set_color_depth(struct drm_encoder *encoder, u32 offset, int bpc)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	uint32_t val;

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
}

void dce4_set_audio_packet(struct drm_encoder *encoder, u32 offset)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;

	WREG32(AFMT_INFOFRAME_CONTROL0 + offset,
		AFMT_AUDIO_INFO_UPDATE); /* required for audio info values to be updated */

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

	WREG32(AFMT_AUDIO_PACKET_CONTROL2 + offset,
		AFMT_AUDIO_CHANNEL_ENABLE(0xff));

	WREG32(HDMI_AUDIO_PACKET_CONTROL + offset,
	       HDMI_AUDIO_DELAY_EN(1) | /* set the default audio delay */
	       HDMI_AUDIO_PACKETS_PER_LINE(3)); /* should be suffient for all audio modes and small enough for all hblanks */

	/* allow 60958 channel status and send audio packets fields to be updated */
	WREG32_OR(AFMT_AUDIO_PACKET_CONTROL + offset,
		  AFMT_RESET_FIFO_WHEN_AUDIO_DIS | AFMT_60958_CS_UPDATE);
}


void dce4_set_mute(struct drm_encoder *encoder, u32 offset, bool mute)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (mute)
		WREG32_OR(HDMI_GC + offset, HDMI_GC_AVMUTE);
	else
		WREG32_AND(HDMI_GC + offset, ~HDMI_GC_AVMUTE);
}

void evergreen_hdmi_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (enable) {
		struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

		if (connector && connector->display_info.has_audio) {
			WREG32(HDMI_INFOFRAME_CONTROL0 + dig->afmt->offset,
			       HDMI_AVI_INFO_SEND | /* enable AVI info frames */
			       HDMI_AVI_INFO_CONT | /* required for audio info values to be updated */
			       HDMI_AUDIO_INFO_SEND | /* enable audio info frames (frames won't be set until audio is enabled) */
			       HDMI_AUDIO_INFO_CONT); /* required for audio info values to be updated */
			WREG32_OR(AFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset,
				  AFMT_AUDIO_SAMPLE_SEND);
		} else {
			WREG32(HDMI_INFOFRAME_CONTROL0 + dig->afmt->offset,
			       HDMI_AVI_INFO_SEND | /* enable AVI info frames */
			       HDMI_AVI_INFO_CONT); /* required for audio info values to be updated */
			WREG32_AND(AFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset,
				   ~AFMT_AUDIO_SAMPLE_SEND);
		}
	} else {
		WREG32_AND(AFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset,
			   ~AFMT_AUDIO_SAMPLE_SEND);
		WREG32(HDMI_INFOFRAME_CONTROL0 + dig->afmt->offset, 0);
	}

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, radeon_encoder->encoder_id);
}

void evergreen_dp_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

	if (!dig || !dig->afmt)
		return;

	if (enable && connector && connector->display_info.has_audio) {
		struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		struct radeon_connector_atom_dig *dig_connector;
		uint32_t val;

		WREG32_OR(AFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset,
			  AFMT_AUDIO_SAMPLE_SEND);

		WREG32(EVERGREEN_DP_SEC_TIMESTAMP + dig->afmt->offset,
		       EVERGREEN_DP_SEC_TIMESTAMP_MODE(1));

		if (!ASIC_IS_DCE6(rdev) && radeon_connector->con_priv) {
			dig_connector = radeon_connector->con_priv;
			val = RREG32(EVERGREEN_DP_SEC_AUD_N + dig->afmt->offset);
			val &= ~EVERGREEN_DP_SEC_N_BASE_MULTIPLE(0xf);

			if (dig_connector->dp_clock == 162000)
				val |= EVERGREEN_DP_SEC_N_BASE_MULTIPLE(3);
			else
				val |= EVERGREEN_DP_SEC_N_BASE_MULTIPLE(5);

			WREG32(EVERGREEN_DP_SEC_AUD_N + dig->afmt->offset, val);
		}

		WREG32(EVERGREEN_DP_SEC_CNTL + dig->afmt->offset,
			EVERGREEN_DP_SEC_ASP_ENABLE |		/* Audio packet transmission */
			EVERGREEN_DP_SEC_ATP_ENABLE |		/* Audio timestamp packet transmission */
			EVERGREEN_DP_SEC_AIP_ENABLE |		/* Audio infoframe packet transmission */
			EVERGREEN_DP_SEC_STREAM_ENABLE);	/* Master enable for secondary stream engine */
	} else {
		WREG32(EVERGREEN_DP_SEC_CNTL + dig->afmt->offset, 0);
		WREG32_AND(AFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset,
			   ~AFMT_AUDIO_SAMPLE_SEND);
	}

	dig->afmt->enabled = enable;
}
