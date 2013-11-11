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
#include <linux/hdmi.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "r600d.h"
#include "atom.h"

/*
 * HDMI color format
 */
enum r600_hdmi_color_format {
	RGB = 0,
	YCC_422 = 1,
	YCC_444 = 2
};

/*
 * IEC60958 status bits
 */
enum r600_hdmi_iec_status_bits {
	AUDIO_STATUS_DIG_ENABLE   = 0x01,
	AUDIO_STATUS_V            = 0x02,
	AUDIO_STATUS_VCFG         = 0x04,
	AUDIO_STATUS_EMPHASIS     = 0x08,
	AUDIO_STATUS_COPYRIGHT    = 0x10,
	AUDIO_STATUS_NONAUDIO     = 0x20,
	AUDIO_STATUS_PROFESSIONAL = 0x40,
	AUDIO_STATUS_LEVEL        = 0x80
};

static const struct radeon_hdmi_acr r600_hdmi_predefined_acr[] = {
    /*	     32kHz	  44.1kHz	48kHz    */
    /* Clock      N     CTS      N     CTS      N     CTS */
    {  25174,  4576,  28125,  7007,  31250,  6864,  28125 }, /*  25,20/1.001 MHz */
    {  25200,  4096,  25200,  6272,  28000,  6144,  25200 }, /*  25.20       MHz */
    {  27000,  4096,  27000,  6272,  30000,  6144,  27000 }, /*  27.00       MHz */
    {  27027,  4096,  27027,  6272,  30030,  6144,  27027 }, /*  27.00*1.001 MHz */
    {  54000,  4096,  54000,  6272,  60000,  6144,  54000 }, /*  54.00       MHz */
    {  54054,  4096,  54054,  6272,  60060,  6144,  54054 }, /*  54.00*1.001 MHz */
    {  74175, 11648, 210937, 17836, 234375, 11648, 140625 }, /*  74.25/1.001 MHz */
    {  74250,  4096,  74250,  6272,  82500,  6144,  74250 }, /*  74.25       MHz */
    { 148351, 11648, 421875,  8918, 234375,  5824, 140625 }, /* 148.50/1.001 MHz */
    { 148500,  4096, 148500,  6272, 165000,  6144, 148500 }, /* 148.50       MHz */
    {      0,  4096,      0,  6272,      0,  6144,      0 }  /* Other */
};

/*
 * calculate CTS value if it's not found in the table
 */
static void r600_hdmi_calc_cts(uint32_t clock, int *CTS, int N, int freq)
{
	if (*CTS == 0)
		*CTS = clock * N / (128 * freq) * 1000;
	DRM_DEBUG("Using ACR timing N=%d CTS=%d for frequency %d\n",
		  N, *CTS, freq);
}

struct radeon_hdmi_acr r600_hdmi_acr(uint32_t clock)
{
	struct radeon_hdmi_acr res;
	u8 i;

	for (i = 0; r600_hdmi_predefined_acr[i].clock != clock &&
	     r600_hdmi_predefined_acr[i].clock != 0; i++)
		;
	res = r600_hdmi_predefined_acr[i];

	/* In case some CTS are missing */
	r600_hdmi_calc_cts(clock, &res.cts_32khz, res.n_32khz, 32000);
	r600_hdmi_calc_cts(clock, &res.cts_44_1khz, res.n_44_1khz, 44100);
	r600_hdmi_calc_cts(clock, &res.cts_48khz, res.n_48khz, 48000);

	return res;
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void r600_hdmi_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_hdmi_acr acr = r600_hdmi_acr(clock);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	WREG32(HDMI0_ACR_32_0 + offset, HDMI0_ACR_CTS_32(acr.cts_32khz));
	WREG32(HDMI0_ACR_32_1 + offset, acr.n_32khz);

	WREG32(HDMI0_ACR_44_0 + offset, HDMI0_ACR_CTS_44(acr.cts_44_1khz));
	WREG32(HDMI0_ACR_44_1 + offset, acr.n_44_1khz);

	WREG32(HDMI0_ACR_48_0 + offset, HDMI0_ACR_CTS_48(acr.cts_48khz));
	WREG32(HDMI0_ACR_48_1 + offset, acr.n_48khz);
}

/*
 * build a HDMI Video Info Frame
 */
static void r600_hdmi_update_avi_infoframe(struct drm_encoder *encoder,
					   void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	uint8_t *frame = buffer + 3;
	uint8_t *header = buffer;

	WREG32(HDMI0_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(HDMI0_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(HDMI0_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(HDMI0_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8) | (header[1] << 24));
}

/*
 * build a Audio Info Frame
 */
static void r600_hdmi_update_audio_infoframe(struct drm_encoder *encoder,
					     const void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	const u8 *frame = buffer + 3;

	WREG32(HDMI0_AUDIO_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(HDMI0_AUDIO_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x8] << 24));
}

/*
 * test if audio buffer is filled enough to start playing
 */
static bool r600_hdmi_is_audio_buffer_filled(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	return (RREG32(HDMI0_STATUS + offset) & 0x10) != 0;
}

/*
 * have buffer status changed since last call?
 */
int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	int status, result;

	if (!dig->afmt || !dig->afmt->enabled)
		return 0;

	status = r600_hdmi_is_audio_buffer_filled(encoder);
	result = dig->afmt->last_buffer_filled_status != status;
	dig->afmt->last_buffer_filled_status = status;

	return result;
}

/*
 * write the audio workaround status to the hardware
 */
static void r600_hdmi_audio_workaround(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	bool hdmi_audio_workaround = false; /* FIXME */
	u32 value;

	if (!hdmi_audio_workaround ||
	    r600_hdmi_is_audio_buffer_filled(encoder))
		value = 0; /* disable workaround */
	else
		value = HDMI0_AUDIO_TEST_EN; /* enable workaround */
	WREG32_P(HDMI0_AUDIO_PACKET_CONTROL + offset,
		 value, ~HDMI0_AUDIO_TEST_EN);
}

void r600_audio_set_dto(struct drm_encoder *encoder, u32 clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 base_rate = 24000;

	if (!dig || !dig->afmt)
		return;

	/* there are two DTOs selected by DCCG_AUDIO_DTO_SELECT.
	 * doesn't matter which one you use.  Just use the first one.
	 */
	/* XXX two dtos; generally use dto0 for hdmi */
	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	if (ASIC_IS_DCE32(rdev)) {
		if (dig->dig_encoder == 0) {
			WREG32(DCCG_AUDIO_DTO0_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO0_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 0); /* select DTO0 */
		} else {
			WREG32(DCCG_AUDIO_DTO1_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO1_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 1); /* select DTO1 */
		}
	} else if (ASIC_IS_DCE3(rdev)) {
		/* according to the reg specs, this should DCE3.2 only, but in
		 * practice it seems to cover DCE3.0/3.1 as well.
		 */
		if (dig->dig_encoder == 0) {
			WREG32(DCCG_AUDIO_DTO0_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO0_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 0); /* select DTO0 */
		} else {
			WREG32(DCCG_AUDIO_DTO1_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO1_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 1); /* select DTO1 */
		}
	} else {
		/* according to the reg specs, this should be DCE2.0 and DCE3.0/3.1 */
		WREG32(AUDIO_DTO, AUDIO_DTO_PHASE(base_rate / 10) |
		       AUDIO_DTO_MODULE(clock / 10));
	}
}

/*
 * update the info frames with the data from the current display mode
 */
void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
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

	WREG32(HDMI0_ACR_PACKET_CONTROL + offset,
	       HDMI0_ACR_AUTO_SEND | /* allow hw to sent ACR packets when required */
	       HDMI0_ACR_SOURCE); /* select SW CTS value */

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
}

/*
 * update settings with current parameters from audio engine
 */
void r600_hdmi_update_audio_settings(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct r600_audio audio = r600_audio_status(rdev);
	uint8_t buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AUDIO_INFOFRAME_SIZE];
	struct hdmi_audio_infoframe frame;
	uint32_t offset;
	uint32_t iec;
	ssize_t err;

	if (!dig->afmt || !dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	DRM_DEBUG("%s with %d channels, %d Hz sampling rate, %d bits per sample,\n",
		 r600_hdmi_is_audio_buffer_filled(encoder) ? "playing" : "stopped",
		  audio.channels, audio.rate, audio.bits_per_sample);
	DRM_DEBUG("0x%02X IEC60958 status bits and 0x%02X category code\n",
		  (int)audio.status_bits, (int)audio.category_code);

	iec = 0;
	if (audio.status_bits & AUDIO_STATUS_PROFESSIONAL)
		iec |= 1 << 0;
	if (audio.status_bits & AUDIO_STATUS_NONAUDIO)
		iec |= 1 << 1;
	if (audio.status_bits & AUDIO_STATUS_COPYRIGHT)
		iec |= 1 << 2;
	if (audio.status_bits & AUDIO_STATUS_EMPHASIS)
		iec |= 1 << 3;

	iec |= HDMI0_60958_CS_CATEGORY_CODE(audio.category_code);

	switch (audio.rate) {
	case 32000:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0x3);
		break;
	case 44100:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0x0);
		break;
	case 48000:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0x2);
		break;
	case 88200:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0x8);
		break;
	case 96000:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0xa);
		break;
	case 176400:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0xc);
		break;
	case 192000:
		iec |= HDMI0_60958_CS_SAMPLING_FREQUENCY(0xe);
		break;
	}

	WREG32(HDMI0_60958_0 + offset, iec);

	iec = 0;
	switch (audio.bits_per_sample) {
	case 16:
		iec |= HDMI0_60958_CS_WORD_LENGTH(0x2);
		break;
	case 20:
		iec |= HDMI0_60958_CS_WORD_LENGTH(0x3);
		break;
	case 24:
		iec |= HDMI0_60958_CS_WORD_LENGTH(0xb);
		break;
	}
	if (audio.status_bits & AUDIO_STATUS_V)
		iec |= 0x5 << 16;
	WREG32_P(HDMI0_60958_1 + offset, iec, ~0x5000f);

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		DRM_ERROR("failed to setup audio infoframe\n");
		return;
	}

	frame.channels = audio.channels;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack audio infoframe\n");
		return;
	}

	r600_hdmi_update_audio_infoframe(encoder, buffer, sizeof(buffer));
	r600_hdmi_audio_workaround(encoder);
}

/*
 * enable the HDMI engine
 */
void r600_hdmi_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 hdmi = HDMI0_ERROR_ACK;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (enable && dig->afmt->enabled)
		return;
	if (!enable && !dig->afmt->enabled)
		return;

	/* Older chipsets require setting HDMI and routing manually */
	if (!ASIC_IS_DCE3(rdev)) {
		if (enable)
			hdmi |= HDMI0_ENABLE;
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			if (enable) {
				WREG32_OR(AVIVO_TMDSA_CNTL, AVIVO_TMDSA_CNTL_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_TMDSA);
			} else {
				WREG32_AND(AVIVO_TMDSA_CNTL, ~AVIVO_TMDSA_CNTL_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			if (enable) {
				WREG32_OR(AVIVO_LVTMA_CNTL, AVIVO_LVTMA_CNTL_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_LVTMA);
			} else {
				WREG32_AND(AVIVO_LVTMA_CNTL, ~AVIVO_LVTMA_CNTL_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_DDI:
			if (enable) {
				WREG32_OR(DDIA_CNTL, DDIA_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_DDIA);
			} else {
				WREG32_AND(DDIA_CNTL, ~DDIA_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
			if (enable)
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_DVOA);
			break;
		default:
			dev_err(rdev->dev, "Invalid encoder for HDMI: 0x%X\n",
				radeon_encoder->encoder_id);
			break;
		}
		WREG32(HDMI0_CONTROL + dig->afmt->offset, hdmi);
	}

	if (rdev->irq.installed) {
		/* if irq is available use it */
		/* XXX: shouldn't need this on any asics.  Double check DCE2/3 */
		if (enable)
			radeon_irq_kms_enable_afmt(rdev, dig->afmt->id);
		else
			radeon_irq_kms_disable_afmt(rdev, dig->afmt->id);
	}

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, radeon_encoder->encoder_id);
}

