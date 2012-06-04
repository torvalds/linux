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
#include "radeon_drm.h"
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

struct radeon_hdmi_acr r600_hdmi_predefined_acr[] = {
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
 * calculate the crc for a given info frame
 */
static void r600_hdmi_infoframe_checksum(uint8_t packetType,
					 uint8_t versionNumber,
					 uint8_t length,
					 uint8_t *frame)
{
	int i;
	frame[0] = packetType + versionNumber + length;
	for (i = 1; i <= length; i++)
		frame[0] += frame[i];
	frame[0] = 0x100 - frame[0];
}

/*
 * build a HDMI Video Info Frame
 */
static void r600_hdmi_videoinfoframe(
	struct drm_encoder *encoder,
	enum r600_hdmi_color_format color_format,
	int active_information_present,
	uint8_t active_format_aspect_ratio,
	uint8_t scan_information,
	uint8_t colorimetry,
	uint8_t ex_colorimetry,
	uint8_t quantization,
	int ITC,
	uint8_t picture_aspect_ratio,
	uint8_t video_format_identification,
	uint8_t pixel_repetition,
	uint8_t non_uniform_picture_scaling,
	uint8_t bar_info_data_valid,
	uint16_t top_bar,
	uint16_t bottom_bar,
	uint16_t left_bar,
	uint16_t right_bar
)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	uint8_t frame[14];

	frame[0x0] = 0;
	frame[0x1] =
		(scan_information & 0x3) |
		((bar_info_data_valid & 0x3) << 2) |
		((active_information_present & 0x1) << 4) |
		((color_format & 0x3) << 5);
	frame[0x2] =
		(active_format_aspect_ratio & 0xF) |
		((picture_aspect_ratio & 0x3) << 4) |
		((colorimetry & 0x3) << 6);
	frame[0x3] =
		(non_uniform_picture_scaling & 0x3) |
		((quantization & 0x3) << 2) |
		((ex_colorimetry & 0x7) << 4) |
		((ITC & 0x1) << 7);
	frame[0x4] = (video_format_identification & 0x7F);
	frame[0x5] = (pixel_repetition & 0xF);
	frame[0x6] = (top_bar & 0xFF);
	frame[0x7] = (top_bar >> 8);
	frame[0x8] = (bottom_bar & 0xFF);
	frame[0x9] = (bottom_bar >> 8);
	frame[0xA] = (left_bar & 0xFF);
	frame[0xB] = (left_bar >> 8);
	frame[0xC] = (right_bar & 0xFF);
	frame[0xD] = (right_bar >> 8);

	r600_hdmi_infoframe_checksum(0x82, 0x02, 0x0D, frame);
	/* Our header values (type, version, length) should be alright, Intel
	 * is using the same. Checksum function also seems to be OK, it works
	 * fine for audio infoframe. However calculated value is always lower
	 * by 2 in comparison to fglrx. It breaks displaying anything in case
	 * of TVs that strictly check the checksum. Hack it manually here to
	 * workaround this issue. */
	frame[0x0] += 2;

	WREG32(HDMI0_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(HDMI0_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(HDMI0_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(HDMI0_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8));
}

/*
 * build a Audio Info Frame
 */
static void r600_hdmi_audioinfoframe(
	struct drm_encoder *encoder,
	uint8_t channel_count,
	uint8_t coding_type,
	uint8_t sample_size,
	uint8_t sample_frequency,
	uint8_t format,
	uint8_t channel_allocation,
	uint8_t level_shift,
	int downmix_inhibit
)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	uint8_t frame[11];

	frame[0x0] = 0;
	frame[0x1] = (channel_count & 0x7) | ((coding_type & 0xF) << 4);
	frame[0x2] = (sample_size & 0x3) | ((sample_frequency & 0x7) << 2);
	frame[0x3] = format;
	frame[0x4] = channel_allocation;
	frame[0x5] = ((level_shift & 0xF) << 3) | ((downmix_inhibit & 0x1) << 7);
	frame[0x6] = 0;
	frame[0x7] = 0;
	frame[0x8] = 0;
	frame[0x9] = 0;
	frame[0xA] = 0;

	r600_hdmi_infoframe_checksum(0x84, 0x01, 0x0A, frame);

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


/*
 * update the info frames with the data from the current display mode
 */
void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset;

	if (ASIC_IS_DCE5(rdev))
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	r600_audio_set_clock(encoder, mode->clock);

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
		       HDMI0_AUDIO_SEND_MAX_PACKETS | /* send NULL packets if no audio is available */
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

	r600_hdmi_videoinfoframe(encoder, RGB, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

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
	uint32_t offset;
	uint32_t iec;

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

	r600_hdmi_audioinfoframe(encoder, audio.channels - 1, 0, 0, 0, 0, 0, 0,
				 0);

	r600_hdmi_audio_workaround(encoder);
}

/*
 * enable the HDMI engine
 */
void r600_hdmi_enable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset;
	u32 hdmi;

	if (ASIC_IS_DCE5(rdev))
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	/* Older chipsets require setting HDMI and routing manually */
	if (rdev->family >= CHIP_R600 && !ASIC_IS_DCE3(rdev)) {
		hdmi = HDMI0_ERROR_ACK | HDMI0_ENABLE;
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			WREG32_P(AVIVO_TMDSA_CNTL, AVIVO_TMDSA_CNTL_HDMI_EN,
				 ~AVIVO_TMDSA_CNTL_HDMI_EN);
			hdmi |= HDMI0_STREAM(HDMI0_STREAM_TMDSA);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			WREG32_P(AVIVO_LVTMA_CNTL, AVIVO_LVTMA_CNTL_HDMI_EN,
				 ~AVIVO_LVTMA_CNTL_HDMI_EN);
			hdmi |= HDMI0_STREAM(HDMI0_STREAM_LVTMA);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_DDI:
			WREG32_P(DDIA_CNTL, DDIA_HDMI_EN, ~DDIA_HDMI_EN);
			hdmi |= HDMI0_STREAM(HDMI0_STREAM_DDIA);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
			hdmi |= HDMI0_STREAM(HDMI0_STREAM_DVOA);
			break;
		default:
			dev_err(rdev->dev, "Invalid encoder for HDMI: 0x%X\n",
				radeon_encoder->encoder_id);
			break;
		}
		WREG32(HDMI0_CONTROL + offset, hdmi);
	}

	if (rdev->irq.installed) {
		/* if irq is available use it */
		rdev->irq.afmt[dig->afmt->id] = true;
		radeon_irq_set(rdev);
	}

	dig->afmt->enabled = true;

	DRM_DEBUG("Enabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  offset, radeon_encoder->encoder_id);
}

/*
 * disable the HDMI engine
 */
void r600_hdmi_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset;

	if (ASIC_IS_DCE5(rdev))
		return;

	/* Called for ATOM_ENCODER_MODE_HDMI only */
	if (!dig || !dig->afmt) {
		WARN_ON(1);
		return;
	}
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	DRM_DEBUG("Disabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  offset, radeon_encoder->encoder_id);

	/* disable irq */
	rdev->irq.afmt[dig->afmt->id] = false;
	radeon_irq_set(rdev);

	/* Older chipsets not handled by AtomBIOS */
	if (rdev->family >= CHIP_R600 && !ASIC_IS_DCE3(rdev)) {
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			WREG32_P(AVIVO_TMDSA_CNTL, 0,
				 ~AVIVO_TMDSA_CNTL_HDMI_EN);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			WREG32_P(AVIVO_LVTMA_CNTL, 0,
				 ~AVIVO_LVTMA_CNTL_HDMI_EN);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_DDI:
			WREG32_P(DDIA_CNTL, 0, ~DDIA_HDMI_EN);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
			break;
		default:
			dev_err(rdev->dev, "Invalid encoder for HDMI: 0x%X\n",
				radeon_encoder->encoder_id);
			break;
		}
		WREG32(HDMI0_CONTROL + offset, HDMI0_ERROR_ACK);
	}

	dig->afmt->enabled = false;
}
