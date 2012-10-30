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
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "evergreend.h"
#include "atom.h"

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

/*
 * calculate the crc for a given info frame
 */
static void evergreen_hdmi_infoframe_checksum(uint8_t packetType,
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
static void evergreen_hdmi_videoinfoframe(
	struct drm_encoder *encoder,
	uint8_t color_format,
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

	evergreen_hdmi_infoframe_checksum(0x82, 0x02, 0x0D, frame);
	/* Our header values (type, version, length) should be alright, Intel
	 * is using the same. Checksum function also seems to be OK, it works
	 * fine for audio infoframe. However calculated value is always lower
	 * by 2 in comparison to fglrx. It breaks displaying anything in case
	 * of TVs that strictly check the checksum. Hack it manually here to
	 * workaround this issue. */
	frame[0x0] += 2;

	WREG32(AFMT_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(AFMT_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(AFMT_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(AFMT_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8));
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
	uint32_t offset;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	r600_audio_set_clock(encoder, mode->clock);

	WREG32(HDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_NULL_SEND); /* send null packets when required */

	WREG32(AFMT_AUDIO_CRC_CONTROL + offset, 0x1000);

	WREG32(HDMI_AUDIO_PACKET_CONTROL + offset,
	       HDMI_AUDIO_DELAY_EN(1) | /* set the default audio delay */
	       HDMI_AUDIO_PACKETS_PER_LINE(3)); /* should be suffient for all audio modes and small enough for all hblanks */

	WREG32(AFMT_AUDIO_PACKET_CONTROL + offset,
	       AFMT_AUDIO_SAMPLE_SEND | /* send audio packets */
	       AFMT_60958_CS_UPDATE); /* allow 60958 channel status fields to be updated */

	WREG32(HDMI_ACR_PACKET_CONTROL + offset,
	       HDMI_ACR_AUTO_SEND | /* allow hw to sent ACR packets when required */
	       HDMI_ACR_SOURCE); /* select SW CTS value */

	WREG32(HDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_NULL_SEND | /* send null packets when required */
	       HDMI_GC_SEND | /* send general control packets */
	       HDMI_GC_CONT); /* send general control packets every frame */

	WREG32(HDMI_INFOFRAME_CONTROL0 + offset,
	       HDMI_AVI_INFO_SEND | /* enable AVI info frames */
	       HDMI_AVI_INFO_CONT | /* send AVI info frames every frame/field */
	       HDMI_AUDIO_INFO_SEND | /* enable audio info frames (frames won't be set until audio is enabled) */
	       HDMI_AUDIO_INFO_CONT); /* required for audio info values to be updated */

	WREG32(AFMT_INFOFRAME_CONTROL0 + offset,
	       AFMT_AUDIO_INFO_UPDATE); /* required for audio info values to be updated */

	WREG32(HDMI_INFOFRAME_CONTROL1 + offset,
	       HDMI_AVI_INFO_LINE(2) | /* anything other than 0 */
	       HDMI_AUDIO_INFO_LINE(2)); /* anything other than 0 */

	WREG32(HDMI_GC + offset, 0); /* unset HDMI_GC_AVMUTE */

	evergreen_hdmi_videoinfoframe(encoder, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				      0, 0, 0, 0, 0, 0);

	evergreen_hdmi_update_ACR(encoder, mode->clock);

	/* it's unknown what these bits do excatly, but it's indeed quite useful for debugging */
	WREG32(AFMT_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(AFMT_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(AFMT_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(AFMT_RAMP_CONTROL3 + offset, 0x00000001);
}
