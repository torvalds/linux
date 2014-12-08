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
#include "radeon_audio.h"
#include "r600d.h"

void dce3_2_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
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

void dce3_2_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
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

void dce3_2_afmt_write_sad_regs(struct drm_encoder *encoder,
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

void dce3_2_audio_set_dto(struct radeon_device *rdev,
	struct radeon_crtc *crtc, unsigned int clock)
{
	struct radeon_encoder *radeon_encoder;
	struct radeon_encoder_atom_dig *dig;
	unsigned int max_ratio = clock / 24000;
	u32 dto_phase;
	u32 wallclock_ratio;
	u32 dto_cntl;

	if (!crtc)
		return;

	radeon_encoder = to_radeon_encoder(crtc->encoder);
	dig = radeon_encoder->enc_priv;

	if (!dig)
		return;

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

	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	if (dig->dig_encoder == 0) {
		dto_cntl = RREG32(DCCG_AUDIO_DTO0_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
		dto_cntl |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
		WREG32(DCCG_AUDIO_DTO0_CNTL, dto_cntl);
		WREG32(DCCG_AUDIO_DTO0_PHASE, dto_phase);
		WREG32(DCCG_AUDIO_DTO0_MODULE, clock);
		WREG32(DCCG_AUDIO_DTO_SELECT, 0); /* select DTO0 */
	} else {
		dto_cntl = RREG32(DCCG_AUDIO_DTO1_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
		dto_cntl |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
		WREG32(DCCG_AUDIO_DTO1_CNTL, dto_cntl);
		WREG32(DCCG_AUDIO_DTO1_PHASE, dto_phase);
		WREG32(DCCG_AUDIO_DTO1_MODULE, clock);
		WREG32(DCCG_AUDIO_DTO_SELECT, 1); /* select DTO1 */
	}
}

void dce3_2_hdmi_update_acr(struct drm_encoder *encoder, long offset,
	const struct radeon_hdmi_acr *acr)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;

	WREG32(HDMI0_ACR_PACKET_CONTROL + offset,
		HDMI0_ACR_SOURCE |		/* select SW CTS value */
		HDMI0_ACR_AUTO_SEND);	/* allow hw to sent ACR packets when required */

	WREG32_P(HDMI0_ACR_32_0 + offset,
		HDMI0_ACR_CTS_32(acr->cts_32khz),
		~HDMI0_ACR_CTS_32_MASK);
	WREG32_P(HDMI0_ACR_32_1 + offset,
		HDMI0_ACR_N_32(acr->n_32khz),
		~HDMI0_ACR_N_32_MASK);

	WREG32_P(HDMI0_ACR_44_0 + offset,
		HDMI0_ACR_CTS_44(acr->cts_44_1khz),
		~HDMI0_ACR_CTS_44_MASK);
	WREG32_P(HDMI0_ACR_44_1 + offset,
		HDMI0_ACR_N_44(acr->n_44_1khz),
		~HDMI0_ACR_N_44_MASK);

	WREG32_P(HDMI0_ACR_48_0 + offset,
		HDMI0_ACR_CTS_48(acr->cts_48khz),
		~HDMI0_ACR_CTS_48_MASK);
	WREG32_P(HDMI0_ACR_48_1 + offset,
		HDMI0_ACR_N_48(acr->n_48khz),
		~HDMI0_ACR_N_48_MASK);
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
	dig->afmt->pin = radeon_audio_get_pin(encoder);
	radeon_audio_enable(rdev, dig->afmt->pin, 0);

	radeon_audio_set_dto(encoder, mode->clock);
	radeon_audio_set_vbi_packet(encoder);

	WREG32(HDMI0_AUDIO_CRC_CONTROL + offset, 0x1000);

	radeon_hdmi_set_color_depth(encoder);

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
		radeon_audio_write_speaker_allocation(encoder);
		radeon_audio_write_sad_regs(encoder);
	}

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

	radeon_update_avi_infoframe(encoder, buffer, sizeof(buffer));
	radeon_audio_update_acr(encoder, mode->clock);

	/* it's unknown what these bits do excatly, but it's indeed quite useful for debugging */
	WREG32(HDMI0_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(HDMI0_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(HDMI0_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(HDMI0_RAMP_CONTROL3 + offset, 0x00000001);

	r600_hdmi_audio_workaround(encoder);

	/* enable audio after to setting up hw */
	radeon_audio_enable(rdev, dig->afmt->pin, 0xf);
}
