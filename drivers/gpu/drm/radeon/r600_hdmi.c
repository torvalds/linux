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

struct {
	uint32_t Clock;

	int N_32kHz;
	int CTS_32kHz;

	int N_44_1kHz;
	int CTS_44_1kHz;

	int N_48kHz;
	int CTS_48kHz;

} r600_hdmi_ACR[] = {
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
static void r600_hdmi_calc_CTS(uint32_t clock, int *CTS, int N, int freq)
{
	if (*CTS == 0)
		*CTS = clock * N / (128 * freq) * 1000;
	DRM_DEBUG("Using ACR timing N=%d CTS=%d for frequency %d\n",
		  N, *CTS, freq);
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void r600_hdmi_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;
	int CTS;
	int N;
	int i;

	for (i = 0; r600_hdmi_ACR[i].Clock != clock && r600_hdmi_ACR[i].Clock != 0; i++);

	CTS = r600_hdmi_ACR[i].CTS_32kHz;
	N = r600_hdmi_ACR[i].N_32kHz;
	r600_hdmi_calc_CTS(clock, &CTS, N, 32000);
	WREG32(offset+R600_HDMI_32kHz_CTS, CTS << 12);
	WREG32(offset+R600_HDMI_32kHz_N, N);

	CTS = r600_hdmi_ACR[i].CTS_44_1kHz;
	N = r600_hdmi_ACR[i].N_44_1kHz;
	r600_hdmi_calc_CTS(clock, &CTS, N, 44100);
	WREG32(offset+R600_HDMI_44_1kHz_CTS, CTS << 12);
	WREG32(offset+R600_HDMI_44_1kHz_N, N);

	CTS = r600_hdmi_ACR[i].CTS_48kHz;
	N = r600_hdmi_ACR[i].N_48kHz;
	r600_hdmi_calc_CTS(clock, &CTS, N, 48000);
	WREG32(offset+R600_HDMI_48kHz_CTS, CTS << 12);
	WREG32(offset+R600_HDMI_48kHz_N, N);
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
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;

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

	WREG32(offset+R600_HDMI_VIDEOINFOFRAME_0,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(offset+R600_HDMI_VIDEOINFOFRAME_1,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(offset+R600_HDMI_VIDEOINFOFRAME_2,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(offset+R600_HDMI_VIDEOINFOFRAME_3,
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
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;

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

	WREG32(offset+R600_HDMI_AUDIOINFOFRAME_0,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(offset+R600_HDMI_AUDIOINFOFRAME_1,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x8] << 24));
}

/*
 * test if audio buffer is filled enough to start playing
 */
static int r600_hdmi_is_audio_buffer_filled(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;

	return (RREG32(offset+R600_HDMI_STATUS) & 0x10) != 0;
}

/*
 * have buffer status changed since last call?
 */
int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	int status, result;

	if (!radeon_encoder->hdmi_offset)
		return 0;

	status = r600_hdmi_is_audio_buffer_filled(encoder);
	result = radeon_encoder->hdmi_buffer_status != status;
	radeon_encoder->hdmi_buffer_status = status;

	return result;
}

/*
 * write the audio workaround status to the hardware
 */
void r600_hdmi_audio_workaround(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t offset = radeon_encoder->hdmi_offset;

	if (!offset)
		return;

	if (r600_hdmi_is_audio_buffer_filled(encoder)) {
		/* disable audio workaround and start delivering of audio frames */
		WREG32_P(offset+R600_HDMI_CNTL, 0x00000001, ~0x00001001);

	} else if (radeon_encoder->hdmi_audio_workaround) {
		/* enable audio workaround and start delivering of audio frames */
		WREG32_P(offset+R600_HDMI_CNTL, 0x00001001, ~0x00001001);

	} else {
		/* disable audio workaround and stop delivering of audio frames */
		WREG32_P(offset+R600_HDMI_CNTL, 0x00000000, ~0x00001001);
	}
}


/*
 * update the info frames with the data from the current display mode
 */
void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;

	if (ASIC_IS_DCE4(rdev))
		return;

	if (!offset)
		return;

	r600_audio_set_clock(encoder, mode->clock);

	WREG32(offset+R600_HDMI_UNKNOWN_0, 0x1000);
	WREG32(offset+R600_HDMI_UNKNOWN_1, 0x0);
	WREG32(offset+R600_HDMI_UNKNOWN_2, 0x1000);

	r600_hdmi_update_ACR(encoder, mode->clock);

	WREG32(offset+R600_HDMI_VIDEOCNTL, 0x13);

	WREG32(offset+R600_HDMI_VERSION, 0x202);

	r600_hdmi_videoinfoframe(encoder, RGB, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	/* it's unknown what these bits do excatly, but it's indeed quite usefull for debugging */
	WREG32(offset+R600_HDMI_AUDIO_DEBUG_0, 0x00FFFFFF);
	WREG32(offset+R600_HDMI_AUDIO_DEBUG_1, 0x007FFFFF);
	WREG32(offset+R600_HDMI_AUDIO_DEBUG_2, 0x00000001);
	WREG32(offset+R600_HDMI_AUDIO_DEBUG_3, 0x00000001);

	r600_hdmi_audio_workaround(encoder);

	/* audio packets per line, does anyone know how to calc this ? */
	WREG32_P(offset+R600_HDMI_CNTL, 0x00040000, ~0x001F0000);

	/* update? reset? don't realy know */
	WREG32_P(offset+R600_HDMI_CNTL, 0x14000000, ~0x14000000);
}

/*
 * update settings with current parameters from audio engine
 */
void r600_hdmi_update_audio_settings(struct drm_encoder *encoder,
				     int channels,
				     int rate,
				     int bps,
				     uint8_t status_bits,
				     uint8_t category_code)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t offset = to_radeon_encoder(encoder)->hdmi_offset;

	uint32_t iec;

	if (!offset)
		return;

	DRM_DEBUG("%s with %d channels, %d Hz sampling rate, %d bits per sample,\n",
		 r600_hdmi_is_audio_buffer_filled(encoder) ? "playing" : "stopped",
		channels, rate, bps);
	DRM_DEBUG("0x%02X IEC60958 status bits and 0x%02X category code\n",
		  (int)status_bits, (int)category_code);

	iec = 0;
	if (status_bits & AUDIO_STATUS_PROFESSIONAL)
		iec |= 1 << 0;
	if (status_bits & AUDIO_STATUS_NONAUDIO)
		iec |= 1 << 1;
	if (status_bits & AUDIO_STATUS_COPYRIGHT)
		iec |= 1 << 2;
	if (status_bits & AUDIO_STATUS_EMPHASIS)
		iec |= 1 << 3;

	iec |= category_code << 8;

	switch (rate) {
	case  32000: iec |= 0x3 << 24; break;
	case  44100: iec |= 0x0 << 24; break;
	case  88200: iec |= 0x8 << 24; break;
	case 176400: iec |= 0xc << 24; break;
	case  48000: iec |= 0x2 << 24; break;
	case  96000: iec |= 0xa << 24; break;
	case 192000: iec |= 0xe << 24; break;
	}

	WREG32(offset+R600_HDMI_IEC60958_1, iec);

	iec = 0;
	switch (bps) {
	case 16: iec |= 0x2; break;
	case 20: iec |= 0x3; break;
	case 24: iec |= 0xb; break;
	}
	if (status_bits & AUDIO_STATUS_V)
		iec |= 0x5 << 16;

	WREG32_P(offset+R600_HDMI_IEC60958_2, iec, ~0x5000f);

	/* 0x021 or 0x031 sets the audio frame length */
	WREG32(offset+R600_HDMI_AUDIOCNTL, 0x31);
	r600_hdmi_audioinfoframe(encoder, channels-1, 0, 0, 0, 0, 0, 0, 0);

	r600_hdmi_audio_workaround(encoder);

	/* update? reset? don't realy know */
	WREG32_P(offset+R600_HDMI_CNTL, 0x04000000, ~0x04000000);
}

static int r600_hdmi_find_free_block(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	bool free_blocks[3] = { true, true, true };

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		switch (radeon_encoder->hdmi_offset) {
		case R600_HDMI_BLOCK1:
			free_blocks[0] = false;
			break;
		case R600_HDMI_BLOCK2:
			free_blocks[1] = false;
			break;
		case R600_HDMI_BLOCK3:
			free_blocks[2] = false;
			break;
		}
	}

	if (rdev->family == CHIP_RS600 || rdev->family == CHIP_RS690) {
		return free_blocks[0] ? R600_HDMI_BLOCK1 : 0;
	} else if (rdev->family >= CHIP_R600) {
		if (free_blocks[0])
			return R600_HDMI_BLOCK1;
		else if (free_blocks[1])
			return R600_HDMI_BLOCK2;
	}
	return 0;
}

static void r600_hdmi_assign_block(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig) {
		dev_err(rdev->dev, "Enabling HDMI on non-dig encoder\n");
		return;
	}

	if (ASIC_IS_DCE4(rdev)) {
		/* TODO */
	} else if (ASIC_IS_DCE3(rdev)) {
		radeon_encoder->hdmi_offset = dig->dig_encoder ?
			R600_HDMI_BLOCK3 : R600_HDMI_BLOCK1;
		if (ASIC_IS_DCE32(rdev))
			radeon_encoder->hdmi_config_offset = dig->dig_encoder ?
				R600_HDMI_CONFIG2 : R600_HDMI_CONFIG1;
	} else if (rdev->family >= CHIP_R600) {
		radeon_encoder->hdmi_offset = r600_hdmi_find_free_block(dev);
	}
}

/*
 * enable the HDMI engine
 */
void r600_hdmi_enable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (ASIC_IS_DCE4(rdev))
		return;

	if (!radeon_encoder->hdmi_offset) {
		r600_hdmi_assign_block(encoder);
		if (!radeon_encoder->hdmi_offset) {
			dev_warn(rdev->dev, "Could not find HDMI block for "
				"0x%x encoder\n", radeon_encoder->encoder_id);
			return;
		}
	}

	if (ASIC_IS_DCE32(rdev) && !ASIC_IS_DCE4(rdev)) {
		WREG32_P(radeon_encoder->hdmi_config_offset + 0x4, 0x1, ~0x1);
	} else if (rdev->family >= CHIP_R600 && !ASIC_IS_DCE3(rdev)) {
		int offset = radeon_encoder->hdmi_offset;
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			WREG32_P(AVIVO_TMDSA_CNTL, 0x4, ~0x4);
			WREG32(offset + R600_HDMI_ENABLE, 0x101);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			WREG32_P(AVIVO_LVTMA_CNTL, 0x4, ~0x4);
			WREG32(offset + R600_HDMI_ENABLE, 0x105);
			break;
		default:
			dev_err(rdev->dev, "Unknown HDMI output type\n");
			break;
		}
	}

	DRM_DEBUG("Enabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		radeon_encoder->hdmi_offset, radeon_encoder->encoder_id);
}

/*
 * disable the HDMI engine
 */
void r600_hdmi_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (ASIC_IS_DCE4(rdev))
		return;

	if (!radeon_encoder->hdmi_offset) {
		dev_err(rdev->dev, "Disabling not enabled HDMI\n");
		return;
	}

	DRM_DEBUG("Disabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		radeon_encoder->hdmi_offset, radeon_encoder->encoder_id);

	if (ASIC_IS_DCE32(rdev) && !ASIC_IS_DCE4(rdev)) {
		WREG32_P(radeon_encoder->hdmi_config_offset + 0x4, 0, ~0x1);
	} else if (rdev->family >= CHIP_R600 && !ASIC_IS_DCE3(rdev)) {
		int offset = radeon_encoder->hdmi_offset;
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			WREG32_P(AVIVO_TMDSA_CNTL, 0, ~0x4);
			WREG32(offset + R600_HDMI_ENABLE, 0);
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			WREG32_P(AVIVO_LVTMA_CNTL, 0, ~0x4);
			WREG32(offset + R600_HDMI_ENABLE, 0);
			break;
		default:
			dev_err(rdev->dev, "Unknown HDMI output type\n");
			break;
		}
	}

	radeon_encoder->hdmi_offset = 0;
	radeon_encoder->hdmi_config_offset = 0;
}
