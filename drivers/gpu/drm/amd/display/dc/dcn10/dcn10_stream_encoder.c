/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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
 * Authors: AMD
 *
 */


#include "dc_bios_types.h"
#include "dcn10_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"

#define DC_LOGGER \
		enc1->base.ctx->logger


#define REG(reg)\
	(enc1->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc1->se_shift->field_name, enc1->se_mask->field_name

#define VBI_LINE_0 0
#define DP_BLANK_MAX_RETRY 20
#define HDMI_CLOCK_CHANNEL_RATE_MORE_340M 340000


enum {
	DP_MST_UPDATE_MAX_RETRY = 50
};

#define CTX \
	enc1->base.ctx

void enc1_update_generic_info_packet(
	struct dcn10_stream_encoder *enc1,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet)
{
	uint32_t regval;
	/* TODOFPGA Figure out a proper number for max_retries polling for lock
	 * use 50 for now.
	 */
	uint32_t max_retries = 50;

	/*we need turn on clock before programming AFMT block*/
	REG_UPDATE(AFMT_CNTL, AFMT_AUDIO_CLOCK_EN, 1);

	if (packet_index >= 8)
		ASSERT(0);

	/* poll dig_update_lock is not locked -> asic internal signal
	 * assume otg master lock will unlock it
	 */
/*		REG_WAIT(AFMT_VBI_PACKET_CONTROL, AFMT_GENERIC_LOCK_STATUS,
			0, 10, max_retries);*/

	/* check if HW reading GSP memory */
	REG_WAIT(AFMT_VBI_PACKET_CONTROL, AFMT_GENERIC_CONFLICT,
			0, 10, max_retries);

	/* HW does is not reading GSP memory not reading too long ->
	 * something wrong. clear GPS memory access and notify?
	 * hw SW is writing to GSP memory
	 */
	REG_UPDATE(AFMT_VBI_PACKET_CONTROL, AFMT_GENERIC_CONFLICT_CLR, 1);

	/* choose which generic packet to use */
	regval = REG_READ(AFMT_VBI_PACKET_CONTROL);
	REG_UPDATE(AFMT_VBI_PACKET_CONTROL,
			AFMT_GENERIC_INDEX, packet_index);

	/* write generic packet header
	 * (4th byte is for GENERIC0 only)
	 */
	REG_SET_4(AFMT_GENERIC_HDR, 0,
			AFMT_GENERIC_HB0, info_packet->hb0,
			AFMT_GENERIC_HB1, info_packet->hb1,
			AFMT_GENERIC_HB2, info_packet->hb2,
			AFMT_GENERIC_HB3, info_packet->hb3);

	/* write generic packet contents
	 * (we never use last 4 bytes)
	 * there are 8 (0-7) mmDIG0_AFMT_GENERIC0_x registers
	 */
	{
		const uint32_t *content =
			(const uint32_t *) &info_packet->sb[0];

		REG_WRITE(AFMT_GENERIC_0, *content++);
		REG_WRITE(AFMT_GENERIC_1, *content++);
		REG_WRITE(AFMT_GENERIC_2, *content++);
		REG_WRITE(AFMT_GENERIC_3, *content++);
		REG_WRITE(AFMT_GENERIC_4, *content++);
		REG_WRITE(AFMT_GENERIC_5, *content++);
		REG_WRITE(AFMT_GENERIC_6, *content++);
		REG_WRITE(AFMT_GENERIC_7, *content);
	}

	switch (packet_index) {
	case 0:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC0_FRAME_UPDATE, 1);
		break;
	case 1:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC1_FRAME_UPDATE, 1);
		break;
	case 2:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC2_FRAME_UPDATE, 1);
		break;
	case 3:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC3_FRAME_UPDATE, 1);
		break;
	case 4:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC4_FRAME_UPDATE, 1);
		break;
	case 5:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC5_FRAME_UPDATE, 1);
		break;
	case 6:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC6_FRAME_UPDATE, 1);
		break;
	case 7:
		REG_UPDATE(AFMT_VBI_PACKET_CONTROL1,
				AFMT_GENERIC7_FRAME_UPDATE, 1);
		break;
	default:
		break;
	}
}

static void enc1_update_hdmi_info_packet(
	struct dcn10_stream_encoder *enc1,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet)
{
	uint32_t cont, send, line;

	if (info_packet->valid) {
		enc1_update_generic_info_packet(
			enc1,
			packet_index,
			info_packet);

		/* enable transmission of packet(s) -
		 * packet transmission begins on the next frame
		 */
		cont = 1;
		/* send packet(s) every frame */
		send = 1;
		/* select line number to send packets on */
		line = 2;
	} else {
		cont = 0;
		send = 0;
		line = 0;
	}

	/* choose which generic packet control to use */
	switch (packet_index) {
	case 0:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send,
				HDMI_GENERIC0_LINE, line);
		break;
	case 1:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send,
				HDMI_GENERIC1_LINE, line);
		break;
	case 2:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send,
				HDMI_GENERIC0_LINE, line);
		break;
	case 3:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send,
				HDMI_GENERIC1_LINE, line);
		break;
	case 4:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL2,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send,
				HDMI_GENERIC0_LINE, line);
		break;
	case 5:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL2,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send,
				HDMI_GENERIC1_LINE, line);
		break;
	case 6:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL3,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send,
				HDMI_GENERIC0_LINE, line);
		break;
	case 7:
		REG_UPDATE_3(HDMI_GENERIC_PACKET_CONTROL3,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send,
				HDMI_GENERIC1_LINE, line);
		break;
	default:
		/* invalid HW packet index */
		DC_LOG_WARNING(
			"Invalid HW packet index: %s()\n",
			__func__);
		return;
	}
}

/* setup stream encoder in dp mode */
void enc1_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum dc_color_space output_color_space)
{
	uint32_t h_active_start;
	uint32_t v_active_start;
	uint32_t misc0 = 0;
	uint32_t misc1 = 0;
	uint32_t h_blank;
	uint32_t h_back_porch;
	uint8_t synchronous_clock = 0; /* asynchronous mode */
	uint8_t colorimetry_bpc;
	uint8_t dynamic_range_rgb = 0; /*full range*/
	uint8_t dynamic_range_ycbcr = 1; /*bt709*/
	uint8_t dp_pixel_encoding = 0;
	uint8_t dp_component_depth = 0;

	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* set pixel encoding */
	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_YCBCR422;
		break;
	case PIXEL_ENCODING_YCBCR444:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_YCBCR444;

		if (crtc_timing->flags.Y_ONLY)
			if (crtc_timing->display_color_depth != COLOR_DEPTH_666)
				/* HW testing only, no use case yet.
				 * Color depth of Y-only could be
				 * 8, 10, 12, 16 bits
				 */
				dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_Y_ONLY;

		/* Note: DP_MSA_MISC1 bit 7 is the indicator
		 * of Y-only mode.
		 * This bit is set in HW if register
		 * DP_PIXEL_ENCODING is programmed to 0x4
		 */
		break;
	case PIXEL_ENCODING_YCBCR420:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_YCBCR420;
		REG_UPDATE(DP_VID_TIMING, DP_VID_N_MUL, 1);
		break;
	default:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_RGB444;
		break;
	}

	misc1 = REG_READ(DP_MSA_MISC);
	/* For YCbCr420 and BT2020 Colorimetry Formats, VSC SDP shall be used.
	 * When MISC1, bit 6, is Set to 1, a Source device uses a VSC SDP to indicate the
	 * Pixel Encoding/Colorimetry Format and that a Sink device shall ignore MISC1, bit 7,
	 * and MISC0, bits 7:1 (MISC1, bit 7, and MISC0, bits 7:1, become "don't care").
	 */
	if ((crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) ||
			(output_color_space == COLOR_SPACE_2020_YCBCR) ||
			(output_color_space == COLOR_SPACE_2020_RGB_FULLRANGE) ||
			(output_color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE))
		misc1 = misc1 | 0x40;
	else
		misc1 = misc1 & ~0x40;

	/* set color depth */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_6BPC;
		break;
	case COLOR_DEPTH_888:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_8BPC;
		break;
	case COLOR_DEPTH_101010:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_10BPC;
		break;
	case COLOR_DEPTH_121212:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_12BPC;
		break;
	case COLOR_DEPTH_161616:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_16BPC;
		break;
	default:
		dp_component_depth = DP_COMPONENT_PIXEL_DEPTH_6BPC;
		break;
	}

	/* Set DP pixel encoding and component depth */
	REG_UPDATE_2(DP_PIXEL_FORMAT,
			DP_PIXEL_ENCODING, dp_pixel_encoding,
			DP_COMPONENT_DEPTH, dp_component_depth);

	/* set dynamic range and YCbCr range */

	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
		colorimetry_bpc = 0;
		break;
	case COLOR_DEPTH_888:
		colorimetry_bpc = 1;
		break;
	case COLOR_DEPTH_101010:
		colorimetry_bpc = 2;
		break;
	case COLOR_DEPTH_121212:
		colorimetry_bpc = 3;
		break;
	default:
		colorimetry_bpc = 0;
		break;
	}

	misc0 = misc0 | synchronous_clock;
	misc0 = colorimetry_bpc << 5;

	switch (output_color_space) {
	case COLOR_SPACE_SRGB:
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		dynamic_range_rgb = 0; /*full range*/
		break;
	case COLOR_SPACE_SRGB_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		dynamic_range_rgb = 1; /*limited range*/
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR601_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1, bit4=0 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		dynamic_range_ycbcr = 0; /*bt601*/
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR709_LIMITED:
		misc0 = misc0 | 0x18; /* bit3=1, bit4=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		dynamic_range_ycbcr = 1; /*bt709*/
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
		dynamic_range_rgb = 1; /*limited range*/
		break;
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_YCBCR:
	case COLOR_SPACE_XR_RGB:
	case COLOR_SPACE_MSREF_SCRGB:
	case COLOR_SPACE_ADOBERGB:
	case COLOR_SPACE_DCIP3:
	case COLOR_SPACE_XV_YCC_709:
	case COLOR_SPACE_XV_YCC_601:
	case COLOR_SPACE_DISPLAYNATIVE:
	case COLOR_SPACE_DOLBYVISION:
	case COLOR_SPACE_APPCTRL:
	case COLOR_SPACE_CUSTOMPOINTS:
	case COLOR_SPACE_UNKNOWN:
		/* do nothing */
		break;
	}

	REG_SET(DP_MSA_COLORIMETRY, 0, DP_MSA_MISC0, misc0);
	REG_WRITE(DP_MSA_MISC, misc1);   /* MSA_MISC1 */

	/* dcn new register
	 * dc_crtc_timing is vesa dmt struct. data from edid
	 */
	REG_SET_2(DP_MSA_TIMING_PARAM1, 0,
			DP_MSA_HTOTAL, crtc_timing->h_total,
			DP_MSA_VTOTAL, crtc_timing->v_total);

	/* calculate from vesa timing parameters
	 * h_active_start related to leading edge of sync
	 */

	h_blank = crtc_timing->h_total - crtc_timing->h_border_left -
			crtc_timing->h_addressable - crtc_timing->h_border_right;

	h_back_porch = h_blank - crtc_timing->h_front_porch -
			crtc_timing->h_sync_width;

	/* start at beginning of left border */
	h_active_start = crtc_timing->h_sync_width + h_back_porch;


	v_active_start = crtc_timing->v_total - crtc_timing->v_border_top -
			crtc_timing->v_addressable - crtc_timing->v_border_bottom -
			crtc_timing->v_front_porch;


	/* start at beginning of left border */
	REG_SET_2(DP_MSA_TIMING_PARAM2, 0,
		DP_MSA_HSTART, h_active_start,
		DP_MSA_VSTART, v_active_start);

	REG_SET_4(DP_MSA_TIMING_PARAM3, 0,
			DP_MSA_HSYNCWIDTH,
			crtc_timing->h_sync_width,
			DP_MSA_HSYNCPOLARITY,
			!crtc_timing->flags.HSYNC_POSITIVE_POLARITY,
			DP_MSA_VSYNCWIDTH,
			crtc_timing->v_sync_width,
			DP_MSA_VSYNCPOLARITY,
			!crtc_timing->flags.VSYNC_POSITIVE_POLARITY);

	/* HWDITH include border or overscan */
	REG_SET_2(DP_MSA_TIMING_PARAM4, 0,
		DP_MSA_HWIDTH, crtc_timing->h_border_left +
		crtc_timing->h_addressable + crtc_timing->h_border_right,
		DP_MSA_VHEIGHT, crtc_timing->v_border_top +
		crtc_timing->v_addressable + crtc_timing->v_border_bottom);
}

static void enc1_stream_encoder_set_stream_attribute_helper(
		struct dcn10_stream_encoder *enc1,
		struct dc_crtc_timing *crtc_timing)
{
	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE(DIG_FE_CNTL, TMDS_PIXEL_ENCODING, 1);
		break;
	default:
		REG_UPDATE(DIG_FE_CNTL, TMDS_PIXEL_ENCODING, 0);
		break;
	}
	REG_UPDATE(DIG_FE_CNTL, TMDS_COLOR_FORMAT, 0);
}

/* setup stream encoder in hdmi mode */
void enc1_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	int actual_pix_clk_khz,
	bool enable_audio)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	struct bp_encoder_control cntl = {0};

	cntl.action = ENCODER_CONTROL_SETUP;
	cntl.engine_id = enc1->base.id;
	cntl.signal = SIGNAL_TYPE_HDMI_TYPE_A;
	cntl.enable_dp_audio = enable_audio;
	cntl.pixel_clock = actual_pix_clk_khz;
	cntl.lanes_number = LANE_COUNT_FOUR;

	if (enc1->base.bp->funcs->encoder_control(
			enc1->base.bp, &cntl) != BP_RESULT_OK)
		return;

	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);

	/* setup HDMI engine */
	REG_UPDATE_5(HDMI_CONTROL,
		HDMI_PACKET_GEN_VERSION, 1,
		HDMI_KEEPOUT_MODE, 1,
		HDMI_DEEP_COLOR_ENABLE, 0,
		HDMI_DATA_SCRAMBLE_EN, 0,
		HDMI_CLOCK_CHANNEL_RATE, 0);


	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_888:
		REG_UPDATE(HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH, 0);
		break;
	case COLOR_DEPTH_101010:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 1);
			}
		break;
	case COLOR_DEPTH_121212:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 1);
			}
		break;
	case COLOR_DEPTH_161616:
		REG_UPDATE_2(HDMI_CONTROL,
				HDMI_DEEP_COLOR_DEPTH, 3,
				HDMI_DEEP_COLOR_ENABLE, 1);
		break;
	default:
		break;
	}

	if (actual_pix_clk_khz >= HDMI_CLOCK_CHANNEL_RATE_MORE_340M) {
		/* enable HDMI data scrambler
		 * HDMI_CLOCK_CHANNEL_RATE_MORE_340M
		 * Clock channel frequency is 1/4 of character rate.
		 */
		REG_UPDATE_2(HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN, 1,
			HDMI_CLOCK_CHANNEL_RATE, 1);
	} else if (crtc_timing->flags.LTE_340MCSC_SCRAMBLE) {

		/* TODO: New feature for DCE11, still need to implement */

		/* enable HDMI data scrambler
		 * HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE
		 * Clock channel frequency is the same
		 * as character rate
		 */
		REG_UPDATE_2(HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN, 1,
			HDMI_CLOCK_CHANNEL_RATE, 0);
	}


	REG_UPDATE_3(HDMI_VBI_PACKET_CONTROL,
		HDMI_GC_CONT, 1,
		HDMI_GC_SEND, 1,
		HDMI_NULL_SEND, 1);

	/* following belongs to audio */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_SEND, 1);

	REG_UPDATE(AFMT_INFOFRAME_CONTROL0, AFMT_AUDIO_INFO_UPDATE, 1);

	REG_UPDATE(HDMI_INFOFRAME_CONTROL1, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	REG_UPDATE(HDMI_GC, HDMI_GC_AVMUTE, 0);
}

/* setup stream encoder in dvi mode */
void enc1_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool is_dual_link)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	struct bp_encoder_control cntl = {0};

	cntl.action = ENCODER_CONTROL_SETUP;
	cntl.engine_id = enc1->base.id;
	cntl.signal = is_dual_link ?
			SIGNAL_TYPE_DVI_DUAL_LINK : SIGNAL_TYPE_DVI_SINGLE_LINK;
	cntl.enable_dp_audio = false;
	cntl.pixel_clock = crtc_timing->pix_clk_khz;
	cntl.lanes_number = (is_dual_link) ? LANE_COUNT_EIGHT : LANE_COUNT_FOUR;

	if (enc1->base.bp->funcs->encoder_control(
			enc1->base.bp, &cntl) != BP_RESULT_OK)
		return;

	ASSERT(crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB);
	ASSERT(crtc_timing->display_color_depth == COLOR_DEPTH_888);
	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);
}

void enc1_stream_encoder_set_mst_bandwidth(
	struct stream_encoder *enc,
	struct fixed31_32 avg_time_slots_per_mtp)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t x = dc_fixpt_floor(
		avg_time_slots_per_mtp);
	uint32_t y = dc_fixpt_ceil(
		dc_fixpt_shl(
			dc_fixpt_sub_int(
				avg_time_slots_per_mtp,
				x),
			26));

	REG_SET_2(DP_MSE_RATE_CNTL, 0,
		DP_MSE_RATE_X, x,
		DP_MSE_RATE_Y, y);

	/* wait for update to be completed on the link */
	/* i.e. DP_MSE_RATE_UPDATE_PENDING field (read only) */
	/* is reset to 0 (not pending) */
	REG_WAIT(DP_MSE_RATE_UPDATE, DP_MSE_RATE_UPDATE_PENDING,
			0,
			10, DP_MST_UPDATE_MAX_RETRY);
}

static void enc1_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* for bring up, disable dp double  TODO */
	REG_UPDATE(HDMI_DB_CONTROL, HDMI_DB_DISABLE, 1);

	enc1_update_hdmi_info_packet(enc1, 0, &info_frame->avi);
	enc1_update_hdmi_info_packet(enc1, 1, &info_frame->vendor);
	enc1_update_hdmi_info_packet(enc1, 2, &info_frame->gamut);
	enc1_update_hdmi_info_packet(enc1, 3, &info_frame->spd);
	enc1_update_hdmi_info_packet(enc1, 4, &info_frame->hdrsmd);
}

static void enc1_stream_encoder_stop_hdmi_info_packets(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* stop generic packets 0 & 1 on HDMI */
	REG_SET_6(HDMI_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_LINE, 0,
		HDMI_GENERIC1_SEND, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC0_SEND, 0);

	/* stop generic packets 2 & 3 on HDMI */
	REG_SET_6(HDMI_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC0_SEND, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_LINE, 0,
		HDMI_GENERIC1_SEND, 0);

	/* stop generic packets 2 & 3 on HDMI */
	REG_SET_6(HDMI_GENERIC_PACKET_CONTROL2, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC0_SEND, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_LINE, 0,
		HDMI_GENERIC1_SEND, 0);

	REG_SET_6(HDMI_GENERIC_PACKET_CONTROL3, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC0_SEND, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_LINE, 0,
		HDMI_GENERIC1_SEND, 0);
}

void enc1_stream_encoder_update_dp_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;

	if (info_frame->vsc.valid)
		enc1_update_generic_info_packet(
					enc1,
					0,  /* packetIndex */
					&info_frame->vsc);

	if (info_frame->spd.valid)
		enc1_update_generic_info_packet(
				enc1,
				2,  /* packetIndex */
				&info_frame->spd);

	if (info_frame->hdrsmd.valid)
		enc1_update_generic_info_packet(
				enc1,
				3,  /* packetIndex */
				&info_frame->hdrsmd);

	/* enable/disable transmission of packet(s).
	 * If enabled, packet transmission begins on the next frame
	 */
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP0_ENABLE, info_frame->vsc.valid);
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP2_ENABLE, info_frame->spd.valid);
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP3_ENABLE, info_frame->hdrsmd.valid);


	/* This bit is the master enable bit.
	 * When enabling secondary stream engine,
	 * this master bit must also be set.
	 * This register shared with audio info frame.
	 * Therefore we need to enable master bit
	 * if at least on of the fields is not 0
	 */
	value = REG_READ(DP_SEC_CNTL);
	if (value)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);
}

void enc1_stream_encoder_stop_dp_info_packets(
	struct stream_encoder *enc)
{
	/* stop generic packets on DP */
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;

	REG_SET_10(DP_SEC_CNTL, 0,
		DP_SEC_GSP0_ENABLE, 0,
		DP_SEC_GSP1_ENABLE, 0,
		DP_SEC_GSP2_ENABLE, 0,
		DP_SEC_GSP3_ENABLE, 0,
		DP_SEC_GSP4_ENABLE, 0,
		DP_SEC_GSP5_ENABLE, 0,
		DP_SEC_GSP6_ENABLE, 0,
		DP_SEC_GSP7_ENABLE, 0,
		DP_SEC_MPG_ENABLE, 0,
		DP_SEC_STREAM_ENABLE, 0);

	/* this register shared with audio info frame.
	 * therefore we need to keep master enabled
	 * if at least one of the fields is not 0 */
	value = REG_READ(DP_SEC_CNTL);
	if (value)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);

}

void enc1_stream_encoder_dp_blank(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t  reg1 = 0;
	uint32_t max_retries = DP_BLANK_MAX_RETRY * 10;

	/* Note: For CZ, we are changing driver default to disable
	 * stream deferred to next VBLANK. If results are positive, we
	 * will make the same change to all DCE versions. There are a
	 * handful of panels that cannot handle disable stream at
	 * HBLANK and will result in a white line flash across the
	 * screen on stream disable.
	 */
	REG_GET(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, &reg1);
	if ((reg1 & 0x1) == 0)
		/*stream not enabled*/
		return;
	/* Specify the video stream disable point
	 * (2 = start of the next vertical blank)
	 */
	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_DIS_DEFER, 2);
	/* Larger delay to wait until VBLANK - use max retry of
	 * 10us*3000=30ms. This covers 16.6ms of typical 60 Hz mode +
	 * a little more because we may not trust delay accuracy.
	 */
	max_retries = DP_BLANK_MAX_RETRY * 150;

	/* disable DP stream */
	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, 0);

	/* the encoder stops sending the video stream
	 * at the start of the vertical blanking.
	 * Poll for DP_VID_STREAM_STATUS == 0
	 */

	REG_WAIT(DP_VID_STREAM_CNTL, DP_VID_STREAM_STATUS,
			0,
			10, max_retries);

	/* Tell the DP encoder to ignore timing from CRTC, must be done after
	 * the polling. If we set DP_STEER_FIFO_RESET before DP stream blank is
	 * complete, stream status will be stuck in video stream enabled state,
	 * i.e. DP_VID_STREAM_STATUS stuck at 1.
	 */

	REG_UPDATE(DP_STEER_FIFO, DP_STEER_FIFO_RESET, true);
}

/* output video stream to link encoder */
void enc1_stream_encoder_dp_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;

		/* M / N = Fstream / Flink
		 * m_vid / n_vid = pixel rate / link rate
		 */

		uint64_t m_vid_l = n_vid;

		m_vid_l *= param->pixel_clk_khz;
		m_vid_l = div_u64(m_vid_l,
			param->link_settings.link_rate
				* LINK_RATE_REF_FREQ_IN_KHZ);

		m_vid = (uint32_t) m_vid_l;

		/* enable auto measurement */

		REG_UPDATE(DP_VID_TIMING, DP_VID_M_N_GEN_EN, 0);

		/* auto measurement need 1 full 0x8000 symbol cycle to kick in,
		 * therefore program initial value for Mvid and Nvid
		 */

		REG_UPDATE(DP_VID_N, DP_VID_N, n_vid);

		REG_UPDATE(DP_VID_M, DP_VID_M, m_vid);

		REG_UPDATE(DP_VID_TIMING, DP_VID_M_N_GEN_EN, 1);
	}

	/* set DIG_START to 0x1 to resync FIFO */

	REG_UPDATE(DIG_FE_CNTL, DIG_START, 1);

	/* switch DP encoder to CRTC data */

	REG_UPDATE(DP_STEER_FIFO, DP_STEER_FIFO_RESET, 0);

	/* wait 100us for DIG/DP logic to prime
	 * (i.e. a few video lines)
	 */
	udelay(100);

	/* the hardware would start sending video at the start of the next DP
	 * frame (i.e. rising edge of the vblank).
	 * NOTE: We used to program DP_VID_STREAM_DIS_DEFER = 2 here, but this
	 * register has no effect on enable transition! HW always guarantees
	 * VID_STREAM enable at start of next frame, and this is not
	 * programmable
	 */

	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, true);
}

void enc1_stream_encoder_set_avmute(
	struct stream_encoder *enc,
	bool enable)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	unsigned int value = enable ? 1 : 0;

	REG_UPDATE(HDMI_GC, HDMI_GC_AVMUTE, value);
}


#define DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT 0x8000
#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC 1

#include "include/audio_types.h"

/**
* speakersToChannels
*
* @brief
*  translate speakers to channels
*
*  FL  - Front Left
*  FR  - Front Right
*  RL  - Rear Left
*  RR  - Rear Right
*  RC  - Rear Center
*  FC  - Front Center
*  FLC - Front Left Center
*  FRC - Front Right Center
*  RLC - Rear Left Center
*  RRC - Rear Right Center
*  LFE - Low Freq Effect
*
*               FC
*          FLC      FRC
*    FL                    FR
*
*                    LFE
*              ()
*
*
*    RL                    RR
*          RLC      RRC
*               RC
*
*             ch  8   7   6   5   4   3   2   1
* 0b00000011      -   -   -   -   -   -   FR  FL
* 0b00000111      -   -   -   -   -   LFE FR  FL
* 0b00001011      -   -   -   -   FC  -   FR  FL
* 0b00001111      -   -   -   -   FC  LFE FR  FL
* 0b00010011      -   -   -   RC  -   -   FR  FL
* 0b00010111      -   -   -   RC  -   LFE FR  FL
* 0b00011011      -   -   -   RC  FC  -   FR  FL
* 0b00011111      -   -   -   RC  FC  LFE FR  FL
* 0b00110011      -   -   RR  RL  -   -   FR  FL
* 0b00110111      -   -   RR  RL  -   LFE FR  FL
* 0b00111011      -   -   RR  RL  FC  -   FR  FL
* 0b00111111      -   -   RR  RL  FC  LFE FR  FL
* 0b01110011      -   RC  RR  RL  -   -   FR  FL
* 0b01110111      -   RC  RR  RL  -   LFE FR  FL
* 0b01111011      -   RC  RR  RL  FC  -   FR  FL
* 0b01111111      -   RC  RR  RL  FC  LFE FR  FL
* 0b11110011      RRC RLC RR  RL  -   -   FR  FL
* 0b11110111      RRC RLC RR  RL  -   LFE FR  FL
* 0b11111011      RRC RLC RR  RL  FC  -   FR  FL
* 0b11111111      RRC RLC RR  RL  FC  LFE FR  FL
* 0b11000011      FRC FLC -   -   -   -   FR  FL
* 0b11000111      FRC FLC -   -   -   LFE FR  FL
* 0b11001011      FRC FLC -   -   FC  -   FR  FL
* 0b11001111      FRC FLC -   -   FC  LFE FR  FL
* 0b11010011      FRC FLC -   RC  -   -   FR  FL
* 0b11010111      FRC FLC -   RC  -   LFE FR  FL
* 0b11011011      FRC FLC -   RC  FC  -   FR  FL
* 0b11011111      FRC FLC -   RC  FC  LFE FR  FL
* 0b11110011      FRC FLC RR  RL  -   -   FR  FL
* 0b11110111      FRC FLC RR  RL  -   LFE FR  FL
* 0b11111011      FRC FLC RR  RL  FC  -   FR  FL
* 0b11111111      FRC FLC RR  RL  FC  LFE FR  FL
*
* @param
*  speakers - speaker information as it comes from CEA audio block
*/
/* translate speakers to channels */

union audio_cea_channels {
	uint8_t all;
	struct audio_cea_channels_bits {
		uint32_t FL:1;
		uint32_t FR:1;
		uint32_t LFE:1;
		uint32_t FC:1;
		uint32_t RL_RC:1;
		uint32_t RR:1;
		uint32_t RC_RLC_FLC:1;
		uint32_t RRC_FRC:1;
	} channels;
};

struct audio_clock_info {
	/* pixel clock frequency*/
	uint32_t pixel_clock_in_10khz;
	/* N - 32KHz audio */
	uint32_t n_32khz;
	/* CTS - 32KHz audio*/
	uint32_t cts_32khz;
	uint32_t n_44khz;
	uint32_t cts_44khz;
	uint32_t n_48khz;
	uint32_t cts_48khz;
};

/* 25.2MHz/1.001*/
/* 25.2MHz/1.001*/
/* 25.2MHz*/
/* 27MHz */
/* 27MHz*1.001*/
/* 27MHz*1.001*/
/* 54MHz*/
/* 54MHz*1.001*/
/* 74.25MHz/1.001*/
/* 74.25MHz*/
/* 148.5MHz/1.001*/
/* 148.5MHz*/

static const struct audio_clock_info audio_clock_info_table[16] = {
	{2517, 4576, 28125, 7007, 31250, 6864, 28125},
	{2518, 4576, 28125, 7007, 31250, 6864, 28125},
	{2520, 4096, 25200, 6272, 28000, 6144, 25200},
	{2700, 4096, 27000, 6272, 30000, 6144, 27000},
	{2702, 4096, 27027, 6272, 30030, 6144, 27027},
	{2703, 4096, 27027, 6272, 30030, 6144, 27027},
	{5400, 4096, 54000, 6272, 60000, 6144, 54000},
	{5405, 4096, 54054, 6272, 60060, 6144, 54054},
	{7417, 11648, 210937, 17836, 234375, 11648, 140625},
	{7425, 4096, 74250, 6272, 82500, 6144, 74250},
	{14835, 11648, 421875, 8918, 234375, 5824, 140625},
	{14850, 4096, 148500, 6272, 165000, 6144, 148500},
	{29670, 5824, 421875, 4459, 234375, 5824, 281250},
	{29700, 3072, 222750, 4704, 247500, 5120, 247500},
	{59340, 5824, 843750, 8918, 937500, 5824, 562500},
	{59400, 3072, 445500, 9408, 990000, 6144, 594000}
};

static const struct audio_clock_info audio_clock_info_table_36bpc[14] = {
	{2517,  9152,  84375,  7007,  48875,  9152,  56250},
	{2518,  9152,  84375,  7007,  48875,  9152,  56250},
	{2520,  4096,  37800,  6272,  42000,  6144,  37800},
	{2700,  4096,  40500,  6272,  45000,  6144,  40500},
	{2702,  8192,  81081,  6272,  45045,  8192,  54054},
	{2703,  8192,  81081,  6272,  45045,  8192,  54054},
	{5400,  4096,  81000,  6272,  90000,  6144,  81000},
	{5405,  4096,  81081,  6272,  90090,  6144,  81081},
	{7417, 11648, 316406, 17836, 351562, 11648, 210937},
	{7425, 4096, 111375,  6272, 123750,  6144, 111375},
	{14835, 11648, 632812, 17836, 703125, 11648, 421875},
	{14850, 4096, 222750,  6272, 247500,  6144, 222750},
	{29670, 5824, 632812,  8918, 703125,  5824, 421875},
	{29700, 4096, 445500,  4704, 371250,  5120, 371250}
};

static const struct audio_clock_info audio_clock_info_table_48bpc[14] = {
	{2517,  4576,  56250,  7007,  62500,  6864,  56250},
	{2518,  4576,  56250,  7007,  62500,  6864,  56250},
	{2520,  4096,  50400,  6272,  56000,  6144,  50400},
	{2700,  4096,  54000,  6272,  60000,  6144,  54000},
	{2702,  4096,  54054,  6267,  60060,  8192,  54054},
	{2703,  4096,  54054,  6272,  60060,  8192,  54054},
	{5400,  4096, 108000,  6272, 120000,  6144, 108000},
	{5405,  4096, 108108,  6272, 120120,  6144, 108108},
	{7417, 11648, 421875, 17836, 468750, 11648, 281250},
	{7425,  4096, 148500,  6272, 165000,  6144, 148500},
	{14835, 11648, 843750,  8918, 468750, 11648, 281250},
	{14850, 4096, 297000,  6272, 330000,  6144, 297000},
	{29670, 5824, 843750,  4459, 468750,  5824, 562500},
	{29700, 3072, 445500,  4704, 495000,  5120, 495000}


};

static union audio_cea_channels speakers_to_channels(
	struct audio_speaker_flags speaker_flags)
{
	union audio_cea_channels cea_channels = {0};

	/* these are one to one */
	cea_channels.channels.FL = speaker_flags.FL_FR;
	cea_channels.channels.FR = speaker_flags.FL_FR;
	cea_channels.channels.LFE = speaker_flags.LFE;
	cea_channels.channels.FC = speaker_flags.FC;

	/* if Rear Left and Right exist move RC speaker to channel 7
	 * otherwise to channel 5
	 */
	if (speaker_flags.RL_RR) {
		cea_channels.channels.RL_RC = speaker_flags.RL_RR;
		cea_channels.channels.RR = speaker_flags.RL_RR;
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RC;
	} else {
		cea_channels.channels.RL_RC = speaker_flags.RC;
	}

	/* FRONT Left Right Center and REAR Left Right Center are exclusive */
	if (speaker_flags.FLC_FRC) {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.FLC_FRC;
		cea_channels.channels.RRC_FRC = speaker_flags.FLC_FRC;
	} else {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RLC_RRC;
		cea_channels.channels.RRC_FRC = speaker_flags.RLC_RRC;
	}

	return cea_channels;
}

static void get_audio_clock_info(
	enum dc_color_depth color_depth,
	uint32_t crtc_pixel_clock_in_khz,
	uint32_t actual_pixel_clock_in_khz,
	struct audio_clock_info *audio_clock_info)
{
	const struct audio_clock_info *clock_info;
	uint32_t index;
	uint32_t crtc_pixel_clock_in_10khz = crtc_pixel_clock_in_khz / 10;
	uint32_t audio_array_size;

	switch (color_depth) {
	case COLOR_DEPTH_161616:
		clock_info = audio_clock_info_table_48bpc;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table_48bpc);
		break;
	case COLOR_DEPTH_121212:
		clock_info = audio_clock_info_table_36bpc;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table_36bpc);
		break;
	default:
		clock_info = audio_clock_info_table;
		audio_array_size = ARRAY_SIZE(
				audio_clock_info_table);
		break;
	}

	if (clock_info != NULL) {
		/* search for exact pixel clock in table */
		for (index = 0; index < audio_array_size; index++) {
			if (clock_info[index].pixel_clock_in_10khz >
				crtc_pixel_clock_in_10khz)
				break;  /* not match */
			else if (clock_info[index].pixel_clock_in_10khz ==
					crtc_pixel_clock_in_10khz) {
				/* match found */
				*audio_clock_info = clock_info[index];
				return;
			}
		}
	}

	/* not found */
	if (actual_pixel_clock_in_khz == 0)
		actual_pixel_clock_in_khz = crtc_pixel_clock_in_khz;

	/* See HDMI spec  the table entry under
	 *  pixel clock of "Other". */
	audio_clock_info->pixel_clock_in_10khz =
			actual_pixel_clock_in_khz / 10;
	audio_clock_info->cts_32khz = actual_pixel_clock_in_khz;
	audio_clock_info->cts_44khz = actual_pixel_clock_in_khz;
	audio_clock_info->cts_48khz = actual_pixel_clock_in_khz;

	audio_clock_info->n_32khz = 4096;
	audio_clock_info->n_44khz = 6272;
	audio_clock_info->n_48khz = 6144;
}

static void enc1_se_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *audio_info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	uint32_t speakers = 0;
	uint32_t channels = 0;

	ASSERT(audio_info);
	if (audio_info == NULL)
		/* This should not happen.it does so we don't get BSOD*/
		return;

	speakers = audio_info->flags.info.ALLSPEAKERS;
	channels = speakers_to_channels(audio_info->flags.speaker_flags).all;

	/* setup the audio stream source select (audio -> dig mapping) */
	REG_SET(AFMT_AUDIO_SRC_CONTROL, 0, AFMT_AUDIO_SRC_SELECT, az_inst);

	/* Channel allocation */
	REG_UPDATE(AFMT_AUDIO_PACKET_CONTROL2, AFMT_AUDIO_CHANNEL_ENABLE, channels);
}

static void enc1_se_setup_hdmi_audio(
	struct stream_encoder *enc,
	const struct audio_crtc_info *crtc_info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	struct audio_clock_info audio_clock_info = {0};

	/* HDMI_AUDIO_PACKET_CONTROL */
	REG_UPDATE(HDMI_AUDIO_PACKET_CONTROL,
			HDMI_AUDIO_DELAY_EN, 1);

	/* AFMT_AUDIO_PACKET_CONTROL */
	REG_UPDATE(AFMT_AUDIO_PACKET_CONTROL, AFMT_60958_CS_UPDATE, 1);

	/* AFMT_AUDIO_PACKET_CONTROL2 */
	REG_UPDATE_2(AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_AUDIO_LAYOUT_OVRD, 0,
			AFMT_60958_OSF_OVRD, 0);

	/* HDMI_ACR_PACKET_CONTROL */
	REG_UPDATE_3(HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND, 1,
			HDMI_ACR_SOURCE, 0,
			HDMI_ACR_AUDIO_PRIORITY, 0);

	/* Program audio clock sample/regeneration parameters */
	get_audio_clock_info(crtc_info->color_depth,
			     crtc_info->requested_pixel_clock,
			     crtc_info->calculated_pixel_clock,
			     &audio_clock_info);
	DC_LOG_HW_AUDIO(
			"\n%s:Input::requested_pixel_clock = %d"	\
			"calculated_pixel_clock = %d \n", __func__,	\
			crtc_info->requested_pixel_clock,		\
			crtc_info->calculated_pixel_clock);

	/* HDMI_ACR_32_0__HDMI_ACR_CTS_32_MASK */
	REG_UPDATE(HDMI_ACR_32_0, HDMI_ACR_CTS_32, audio_clock_info.cts_32khz);

	/* HDMI_ACR_32_1__HDMI_ACR_N_32_MASK */
	REG_UPDATE(HDMI_ACR_32_1, HDMI_ACR_N_32, audio_clock_info.n_32khz);

	/* HDMI_ACR_44_0__HDMI_ACR_CTS_44_MASK */
	REG_UPDATE(HDMI_ACR_44_0, HDMI_ACR_CTS_44, audio_clock_info.cts_44khz);

	/* HDMI_ACR_44_1__HDMI_ACR_N_44_MASK */
	REG_UPDATE(HDMI_ACR_44_1, HDMI_ACR_N_44, audio_clock_info.n_44khz);

	/* HDMI_ACR_48_0__HDMI_ACR_CTS_48_MASK */
	REG_UPDATE(HDMI_ACR_48_0, HDMI_ACR_CTS_48, audio_clock_info.cts_48khz);

	/* HDMI_ACR_48_1__HDMI_ACR_N_48_MASK */
	REG_UPDATE(HDMI_ACR_48_1, HDMI_ACR_N_48, audio_clock_info.n_48khz);

	/* Video driver cannot know in advance which sample rate will
	 * be used by HD Audio driver
	 * HDMI_ACR_PACKET_CONTROL__HDMI_ACR_N_MULTIPLE field is
	 * programmed below in interruppt callback
	 */

	/* AFMT_60958_0__AFMT_60958_CS_CHANNEL_NUMBER_L_MASK &
	 * AFMT_60958_0__AFMT_60958_CS_CLOCK_ACCURACY_MASK
	 */
	REG_UPDATE_2(AFMT_60958_0,
			AFMT_60958_CS_CHANNEL_NUMBER_L, 1,
			AFMT_60958_CS_CLOCK_ACCURACY, 0);

	/* AFMT_60958_1 AFMT_60958_CS_CHALNNEL_NUMBER_R */
	REG_UPDATE(AFMT_60958_1, AFMT_60958_CS_CHANNEL_NUMBER_R, 2);

	/* AFMT_60958_2 now keep this settings until
	 * Programming guide comes out
	 */
	REG_UPDATE_6(AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_2, 3,
			AFMT_60958_CS_CHANNEL_NUMBER_3, 4,
			AFMT_60958_CS_CHANNEL_NUMBER_4, 5,
			AFMT_60958_CS_CHANNEL_NUMBER_5, 6,
			AFMT_60958_CS_CHANNEL_NUMBER_6, 7,
			AFMT_60958_CS_CHANNEL_NUMBER_7, 8);
}

static void enc1_se_setup_dp_audio(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* --- DP Audio packet configurations --- */

	/* ATP Configuration */
	REG_SET(DP_SEC_AUD_N, 0,
			DP_SEC_AUD_N, DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT);

	/* Async/auto-calc timestamp mode */
	REG_SET(DP_SEC_TIMESTAMP, 0, DP_SEC_TIMESTAMP_MODE,
			DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC);

	/* --- The following are the registers
	 *  copied from the SetupHDMI ---
	 */

	/* AFMT_AUDIO_PACKET_CONTROL */
	REG_UPDATE(AFMT_AUDIO_PACKET_CONTROL, AFMT_60958_CS_UPDATE, 1);

	/* AFMT_AUDIO_PACKET_CONTROL2 */
	/* Program the ATP and AIP next */
	REG_UPDATE_2(AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_AUDIO_LAYOUT_OVRD, 0,
			AFMT_60958_OSF_OVRD, 0);

	/* AFMT_INFOFRAME_CONTROL0 */
	REG_UPDATE(AFMT_INFOFRAME_CONTROL0, AFMT_AUDIO_INFO_UPDATE, 1);

	/* AFMT_60958_0__AFMT_60958_CS_CLOCK_ACCURACY_MASK */
	REG_UPDATE(AFMT_60958_0, AFMT_60958_CS_CLOCK_ACCURACY, 0);
}

static void enc1_se_enable_audio_clock(
	struct stream_encoder *enc,
	bool enable)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (REG(AFMT_CNTL) == 0)
		return;   /* DCE8/10 does not have this register */

	REG_UPDATE(AFMT_CNTL, AFMT_AUDIO_CLOCK_EN, !!enable);

	/* wait for AFMT clock to turn on,
	 * expectation: this should complete in 1-2 reads
	 *
	 * REG_WAIT(AFMT_CNTL, AFMT_AUDIO_CLOCK_ON, !!enable, 1, 10);
	 *
	 * TODO: wait for clock_on does not work well. May need HW
	 * program sequence. But audio seems work normally even without wait
	 * for clock_on status change
	 */
}

static void enc1_se_enable_dp_audio(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* Enable Audio packets */
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_ASP_ENABLE, 1);

	/* Program the ATP and AIP next */
	REG_UPDATE_2(DP_SEC_CNTL,
			DP_SEC_ATP_ENABLE, 1,
			DP_SEC_AIP_ENABLE, 1);

	/* Program STREAM_ENABLE after all the other enables. */
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);
}

static void enc1_se_disable_dp_audio(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;

	/* Disable Audio packets */
	REG_UPDATE_5(DP_SEC_CNTL,
			DP_SEC_ASP_ENABLE, 0,
			DP_SEC_ATP_ENABLE, 0,
			DP_SEC_AIP_ENABLE, 0,
			DP_SEC_ACM_ENABLE, 0,
			DP_SEC_STREAM_ENABLE, 0);

	/* This register shared with encoder info frame. Therefore we need to
	 * keep master enabled if at least on of the fields is not 0
	 */
	value = REG_READ(DP_SEC_CNTL);
	if (value != 0)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);

}

void enc1_se_audio_mute_control(
	struct stream_encoder *enc,
	bool mute)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(AFMT_AUDIO_PACKET_CONTROL, AFMT_AUDIO_SAMPLE_SEND, !mute);
}

void enc1_se_dp_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info)
{
	enc1_se_audio_setup(enc, az_inst, info);
}

void enc1_se_dp_audio_enable(
	struct stream_encoder *enc)
{
	enc1_se_enable_audio_clock(enc, true);
	enc1_se_setup_dp_audio(enc);
	enc1_se_enable_dp_audio(enc);
}

void enc1_se_dp_audio_disable(
	struct stream_encoder *enc)
{
	enc1_se_disable_dp_audio(enc);
	enc1_se_enable_audio_clock(enc, false);
}

void enc1_se_hdmi_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	enc1_se_enable_audio_clock(enc, true);
	enc1_se_setup_hdmi_audio(enc, audio_crtc_info);
	enc1_se_audio_setup(enc, az_inst, info);
}

void enc1_se_hdmi_audio_disable(
	struct stream_encoder *enc)
{
	enc1_se_enable_audio_clock(enc, false);
}


void enc1_setup_stereo_sync(
	struct stream_encoder *enc,
	int tg_inst, bool enable)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	REG_UPDATE(DIG_FE_CNTL, DIG_STEREOSYNC_SELECT, tg_inst);
	REG_UPDATE(DIG_FE_CNTL, DIG_STEREOSYNC_GATE_EN, !enable);
}


static const struct stream_encoder_funcs dcn10_str_enc_funcs = {
	.dp_set_stream_attribute =
		enc1_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		enc1_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		enc1_stream_encoder_dvi_set_stream_attribute,
	.set_mst_bandwidth =
		enc1_stream_encoder_set_mst_bandwidth,
	.update_hdmi_info_packets =
		enc1_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		enc1_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets =
		enc1_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		enc1_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		enc1_stream_encoder_dp_blank,
	.dp_unblank =
		enc1_stream_encoder_dp_unblank,
	.audio_mute_control = enc1_se_audio_mute_control,

	.dp_audio_setup = enc1_se_dp_audio_setup,
	.dp_audio_enable = enc1_se_dp_audio_enable,
	.dp_audio_disable = enc1_se_dp_audio_disable,

	.hdmi_audio_setup = enc1_se_hdmi_audio_setup,
	.hdmi_audio_disable = enc1_se_hdmi_audio_disable,
	.setup_stereo_sync  = enc1_setup_stereo_sync,
	.set_avmute = enc1_stream_encoder_set_avmute,
};

void dcn10_stream_encoder_construct(
	struct dcn10_stream_encoder *enc1,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	const struct dcn10_stream_enc_registers *regs,
	const struct dcn10_stream_encoder_shift *se_shift,
	const struct dcn10_stream_encoder_mask *se_mask)
{
	enc1->base.funcs = &dcn10_str_enc_funcs;
	enc1->base.ctx = ctx;
	enc1->base.id = eng_id;
	enc1->base.bp = bp;
	enc1->regs = regs;
	enc1->se_shift = se_shift;
	enc1->se_mask = se_mask;
}

