/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn32/dcn32_dio_stream_encoder.h"
#include "dcn35/dcn35_dio_stream_encoder.h"

#include "dcn401_dio_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"
#include "link.h"
#include "dpcd_defs.h"

#define DC_LOGGER \
		enc1->base.ctx->logger

#define REG(reg)\
	(enc1->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc1->se_shift->field_name, enc1->se_mask->field_name

#define VBI_LINE_0 0
#define HDMI_CLOCK_CHANNEL_RATE_MORE_340M 340000

#define CTX \
	enc1->base.ctx



static void enc401_dp_set_odm_combine(
	struct stream_encoder *enc,
	bool odm_combine)
{
}

/* setup stream encoder in dvi mode */
static void enc401_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool is_dual_link)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
		struct bp_encoder_control cntl = {0};

		cntl.action = ENCODER_CONTROL_SETUP;
		cntl.engine_id = enc1->base.id;
		cntl.signal = is_dual_link ?
			SIGNAL_TYPE_DVI_DUAL_LINK : SIGNAL_TYPE_DVI_SINGLE_LINK;
		cntl.enable_dp_audio = false;
		cntl.pixel_clock = crtc_timing->pix_clk_100hz / 10;
		cntl.lanes_number = (is_dual_link) ? LANE_COUNT_EIGHT : LANE_COUNT_FOUR;

		if (enc1->base.bp->funcs->encoder_control(
				enc1->base.bp, &cntl) != BP_RESULT_OK)
			return;

	} else {

		//Set pattern for clock channel, default vlue 0x63 does not work
		REG_UPDATE(DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, 0x1F);

		//DIG_BE_TMDS_DVI_MODE : TMDS-DVI mode is already set in link_encoder_setup

		//DIG_SOURCE_SELECT is already set in dig_connect_to_otg

		/* DIG_START is removed from the register spec */
	}

	ASSERT(crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB);
	ASSERT(crtc_timing->display_color_depth == COLOR_DEPTH_888);
	enc401_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);
}

/* setup stream encoder in hdmi mode */
static void enc401_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	int actual_pix_clk_khz,
	bool enable_audio)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
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

	} else {

		//Set pattern for clock channel, default vlue 0x63 does not work
		REG_UPDATE(DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, 0x1F);

		//DIG_BE_TMDS_HDMI_MODE : TMDS-HDMI mode is already set in link_encoder_setup

		//DIG_SOURCE_SELECT is already set in dig_connect_to_otg

		/* DIG_START is removed from the register spec */
	}

	/* Configure pixel encoding */
	enc401_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);

	/* setup HDMI engine */
	REG_UPDATE_6(HDMI_CONTROL,
		HDMI_PACKET_GEN_VERSION, 1,
		HDMI_KEEPOUT_MODE, 1,
		HDMI_DEEP_COLOR_ENABLE, 0,
		HDMI_DATA_SCRAMBLE_EN, 0,
		HDMI_NO_EXTRA_NULL_PACKET_FILLED, 1,
		HDMI_CLOCK_CHANNEL_RATE, 0);

	/* Configure color depth */
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


	/* Enable transmission of General Control packet on every frame */
	REG_UPDATE_3(HDMI_VBI_PACKET_CONTROL,
		HDMI_GC_CONT, 1,
		HDMI_GC_SEND, 1,
		HDMI_NULL_SEND, 1);

	/* Disable Audio Content Protection packet transmission */
	REG_UPDATE(HDMI_VBI_PACKET_CONTROL, HDMI_ACP_SEND, 0);
	/* following belongs to audio */
	/* Enable Audio InfoFrame packet transmission. */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_SEND, 1);

	/* update double-buffered AUDIO_INFO registers immediately */
	ASSERT(enc->afmt);
	enc->afmt->funcs->audio_info_immediate_update(enc->afmt);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL1, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_GC, HDMI_GC_AVMUTE, 0);
}

static void enc401_set_dig_input_mode(struct stream_encoder *enc, unsigned int pix_per_container)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	// The naming of this field is confusing, what it means is the output mode of otg, which
	// is the input mode of the dig
	switch (pix_per_container)	{
	case 2:
		REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_OUTPUT_PIXEL_PER_CYCLE, 0x1);
		break;
	case 4:
		REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_OUTPUT_PIXEL_PER_CYCLE, 0x2);
		break;
	case 8:
		REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_OUTPUT_PIXEL_PER_CYCLE, 0x3);
		break;
	default:
		REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_OUTPUT_PIXEL_PER_CYCLE, 0x0);
		break;
	}
}

static bool is_two_pixels_per_containter(const struct dc_crtc_timing *timing)
{
	bool two_pix = timing->pixel_encoding == PIXEL_ENCODING_YCBCR420;

	two_pix = two_pix || (timing->flags.DSC && timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
			&& !timing->dsc_cfg.ycbcr422_simple);
	return two_pix;
}

static void enc401_stream_encoder_dp_unblank(
		struct dc_link *link,
		struct stream_encoder *enc,
		const struct encoder_unblank_param *param)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;
		uint32_t pix_per_container = 1;
		uint64_t m_vid_l = n_vid;

		/* YCbCr 4:2:0 or YCbCr4:2:2 simple + DSC: Computed VID_M will be 2X the input rate */
		if (is_two_pixels_per_containter(&param->timing)) {
			pix_per_container = 2;
		}

		/* M / N = Fstream / Flink
		 * m_vid / n_vid = pixel rate / link rate
		 */
		m_vid_l *= param->timing.pix_clk_100hz / pix_per_container / 10;
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

		/* reduce jitter based on read rate */
		switch (param->pix_per_cycle)	{
		case 2:
			REG_UPDATE(DP_VID_TIMING, DP_VID_N_INTERVAL, 0x1);
			break;
		case 4:
			REG_UPDATE(DP_VID_TIMING, DP_VID_N_INTERVAL, 0x2);
			break;
		case 8:
			REG_UPDATE(DP_VID_TIMING, DP_VID_N_INTERVAL, 0x3);
			break;
		default:
			REG_UPDATE(DP_VID_TIMING, DP_VID_N_INTERVAL, 0x0);
			break;
		}

		REG_UPDATE(DP_VID_TIMING, DP_VID_M_N_GEN_EN, 1);
	}

	/* make sure stream is disabled before resetting steer fifo */
	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, false);
	REG_WAIT(DP_VID_STREAM_CNTL, DP_VID_STREAM_STATUS, 0, 10, 5000);

	/* DIG_START is removed from the register spec */

	/* switch DP encoder to CRTC data, but reset it the fifo first. It may happen
	 * that it overflows during mode transition, and sometimes doesn't recover.
	 */
	REG_UPDATE(DP_STEER_FIFO, DP_STEER_FIFO_RESET, 1);
	udelay(10);

	REG_UPDATE(DP_STEER_FIFO, DP_STEER_FIFO_RESET, 0);

	REG_UPDATE(DP_STEER_FIFO, DP_STEER_FIFO_ENABLE, 1);

	REG_UPDATE_2(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, 1, DP_VID_STREAM_DIS_DEFER, 2);
	udelay(200);

	/* DIG Resync FIFO now needs to be explicitly enabled
	 */
	/* read start level = 0 will bring underflow / overflow and DIG_FIFO_ERROR = 1
	 * so set it to 1/2 full = 7 before reset as suggested by hardware team.
	 */
	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_READ_START_LEVEL, 0x7);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_RESET, 1);

	REG_WAIT(DIG_FIFO_CTRL0, DIG_FIFO_RESET_DONE, 1, 10, 5000);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_RESET, 0);

	REG_WAIT(DIG_FIFO_CTRL0, DIG_FIFO_RESET_DONE, 0, 10, 5000);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_ENABLE, 1);

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

	link->dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_DP_VID_STREAM);
}

/* this function read dsc related register fields to be logged later in dcn10_log_hw_state
 * into a dcn_dsc_state struct.
 */
static void enc401_read_state(struct stream_encoder *enc, struct enc_state *s)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	//if dsc is enabled, continue to read
	REG_GET(DP_PIXEL_FORMAT, PIXEL_ENCODING_TYPE, &s->dsc_mode);

	if (s->dsc_mode) {
		REG_GET(DP_GSP11_CNTL, DP_SEC_GSP11_LINE_NUM, &s->sec_gsp_pps_line_num);

		REG_GET(DP_MSA_VBID_MISC, DP_VBID6_LINE_REFERENCE, &s->vbid6_line_reference);
		REG_GET(DP_MSA_VBID_MISC, DP_VBID6_LINE_NUM, &s->vbid6_line_num);

		REG_GET(DP_GSP11_CNTL, DP_SEC_GSP11_ENABLE, &s->sec_gsp_pps_enable);
		REG_GET(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, &s->sec_stream_enable);
	}
}

static void enc401_stream_encoder_enable(
	struct stream_encoder *enc,
	enum signal_type signal,
	bool enable)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	if (enable) {
		switch (signal) {
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
			/* TMDS-DVI */
			REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_MODE, 2);
			break;
		case SIGNAL_TYPE_HDMI_TYPE_A:
			/* TMDS-HDMI */
			REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_MODE, 3);
			break;
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
			/* DP MST */
			REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_MODE, 5);
			break;
		case SIGNAL_TYPE_EDP:
		case SIGNAL_TYPE_DISPLAY_PORT:
		case SIGNAL_TYPE_VIRTUAL:
			/* DP SST */
			REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_MODE, 0);
			break;
		default:
			/* invalid mode ! */
			ASSERT_CRITICAL(false);
		}

		REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_CLK_EN, 1);
		REG_UPDATE(DIG_FE_EN_CNTL, DIG_FE_ENABLE, 1);
	} else {
		REG_UPDATE(DIG_FE_EN_CNTL, DIG_FE_ENABLE, 0);
		REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_CLK_EN, 0);
	}
}

void enc401_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum dc_color_space output_color_space,
	bool use_vsc_sdp_for_colorimetry,
	uint32_t enable_sdp_splitting)
{
	uint32_t h_active_start;
	uint32_t v_active_start;
	uint32_t misc0 = 0;
	uint32_t misc1 = 0;
	uint32_t h_blank;
	uint32_t h_back_porch;
	uint8_t synchronous_clock = 0; /* asynchronous mode */
	uint8_t colorimetry_bpc;
	uint8_t dp_pixel_encoding = 0;
	uint8_t dp_component_depth = 0;
	uint8_t dp_translate_pixel_enc = 0;
	// Fix set but not used warnings
	//uint8_t dp_pixel_encoding_type = 0;
	uint8_t dp_compressed_pixel_format = 0;

	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	struct dc_crtc_timing hw_crtc_timing = *crtc_timing;

	if (hw_crtc_timing.flags.INTERLACE) {
		/*the input timing is in VESA spec format with Interlace flag =1*/
		hw_crtc_timing.v_total /= 2;
		hw_crtc_timing.v_border_top /= 2;
		hw_crtc_timing.v_addressable /= 2;
		hw_crtc_timing.v_border_bottom /= 2;
		hw_crtc_timing.v_front_porch /= 2;
		hw_crtc_timing.v_sync_width /= 2;
	}


	/* set pixel encoding */
	switch (hw_crtc_timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_YCBCR422;
		break;
	case PIXEL_ENCODING_YCBCR444:
		dp_pixel_encoding = DP_PIXEL_ENCODING_TYPE_YCBCR444;

		if (hw_crtc_timing.flags.Y_ONLY)
			if (hw_crtc_timing.display_color_depth != COLOR_DEPTH_666)
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
	if (use_vsc_sdp_for_colorimetry)
		misc1 = misc1 | 0x40;
	else
		misc1 = misc1 & ~0x40;

	/* set color depth */
	switch (hw_crtc_timing.display_color_depth) {
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

	if (hw_crtc_timing.flags.DSC) {
		// Fix set but not used error
		//dp_pixel_encoding_type = 1;
		switch (hw_crtc_timing.pixel_encoding) {
		case PIXEL_ENCODING_YCBCR444:
			dp_compressed_pixel_format = 0;
			break;
		case PIXEL_ENCODING_YCBCR422:
			dp_compressed_pixel_format = 1;
			if (hw_crtc_timing.dsc_cfg.ycbcr422_simple)
				dp_compressed_pixel_format = 0;
			break;
		case PIXEL_ENCODING_YCBCR420:
			dp_compressed_pixel_format = 1;
			break;
		default:
			dp_compressed_pixel_format = 0;
			break;
		}
	} else {
		// Fix set but not used error
		//dp_pixel_encoding_type = 0;
		switch (dp_pixel_encoding) {
		case DP_PIXEL_ENCODING_TYPE_RGB444:
			dp_translate_pixel_enc = 0;
			break;
		case DP_PIXEL_ENCODING_TYPE_YCBCR422:
			dp_translate_pixel_enc = 1;
			break;
		case DP_PIXEL_ENCODING_TYPE_YCBCR444:
			dp_translate_pixel_enc = 0;
			break;
		case DP_PIXEL_ENCODING_TYPE_Y_ONLY:
			dp_translate_pixel_enc = 3;
			break;
		case DP_PIXEL_ENCODING_TYPE_YCBCR420:
			dp_translate_pixel_enc = 2;
			break;
		default:
			ASSERT(0);
			break;
		}
	}
	/* Set DP pixel encoding and component depth */
	REG_UPDATE_4(DP_PIXEL_FORMAT,
			PIXEL_ENCODING_TYPE, hw_crtc_timing.flags.DSC ? 1 : 0,
			UNCOMPRESSED_PIXEL_FORMAT, dp_translate_pixel_enc,
			UNCOMPRESSED_COMPONENT_DEPTH, dp_component_depth,
			COMPRESSED_PIXEL_FORMAT, dp_compressed_pixel_format);

	/* set dynamic range and YCbCr range */

	switch (hw_crtc_timing.display_color_depth) {
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
		break;
	case COLOR_SPACE_SRGB_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR601_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1, bit4=0 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR709_LIMITED:
		misc0 = misc0 | 0x18; /* bit3=1, bit4=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
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
	case COLOR_SPACE_YCBCR709_BLACK:
		/* do nothing */
		break;
	}

	REG_SET(DP_MSA_COLORIMETRY, 0, DP_MSA_MISC0, misc0);
	REG_WRITE(DP_MSA_MISC, misc1);   /* MSA_MISC1 */

	/* dcn new register
	 * dc_crtc_timing is vesa dmt struct. data from edid
	 */
	REG_SET_2(DP_MSA_TIMING_PARAM1, 0,
			DP_MSA_HTOTAL, hw_crtc_timing.h_total,
			DP_MSA_VTOTAL, hw_crtc_timing.v_total);

	/* calculate from vesa timing parameters
	 * h_active_start related to leading edge of sync
	 */

	h_blank = hw_crtc_timing.h_total - hw_crtc_timing.h_border_left -
			hw_crtc_timing.h_addressable - hw_crtc_timing.h_border_right;

	h_back_porch = h_blank - hw_crtc_timing.h_front_porch -
			hw_crtc_timing.h_sync_width;

	/* start at beginning of left border */
	h_active_start = hw_crtc_timing.h_sync_width + h_back_porch;


	v_active_start = hw_crtc_timing.v_total - hw_crtc_timing.v_border_top -
			hw_crtc_timing.v_addressable - hw_crtc_timing.v_border_bottom -
			hw_crtc_timing.v_front_porch;


	/* start at beginning of left border */
	REG_SET_2(DP_MSA_TIMING_PARAM2, 0,
		DP_MSA_HSTART, h_active_start,
		DP_MSA_VSTART, v_active_start);

	REG_SET_4(DP_MSA_TIMING_PARAM3, 0,
			DP_MSA_HSYNCWIDTH,
			hw_crtc_timing.h_sync_width,
			DP_MSA_HSYNCPOLARITY,
			!hw_crtc_timing.flags.HSYNC_POSITIVE_POLARITY,
			DP_MSA_VSYNCWIDTH,
			hw_crtc_timing.v_sync_width,
			DP_MSA_VSYNCPOLARITY,
			!hw_crtc_timing.flags.VSYNC_POSITIVE_POLARITY);

	/* HWDITH include border or overscan */
	REG_SET_2(DP_MSA_TIMING_PARAM4, 0,
		DP_MSA_HWIDTH, hw_crtc_timing.h_border_left +
		hw_crtc_timing.h_addressable + hw_crtc_timing.h_border_right,
		DP_MSA_VHEIGHT, hw_crtc_timing.v_border_top +
		hw_crtc_timing.v_addressable + hw_crtc_timing.v_border_bottom);

	REG_UPDATE(DP_SEC_FRAMING4,
		DP_SST_SDP_SPLITTING, enable_sdp_splitting);
}

static void enc401_stream_encoder_map_to_link(
		struct stream_encoder *enc,
		uint32_t stream_enc_inst,
		uint32_t link_enc_inst)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(STREAM_MAPPER_CONTROL,
				DIG_STREAM_LINK_TARGET, link_enc_inst);
}

static const struct stream_encoder_funcs dcn401_str_enc_funcs = {
	.dp_set_odm_combine =
		enc401_dp_set_odm_combine,
	.dp_set_stream_attribute =
		enc401_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		enc401_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		enc401_stream_encoder_dvi_set_stream_attribute,
	.set_throttled_vcp_size =
		enc1_stream_encoder_set_throttled_vcp_size,
	.update_hdmi_info_packets =
		enc3_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		enc3_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets_sdp_line_num =
		enc3_stream_encoder_update_dp_info_packets_sdp_line_num,
	.update_dp_info_packets =
		enc3_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		enc1_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		enc1_stream_encoder_dp_blank,
	.dp_unblank =
		enc401_stream_encoder_dp_unblank,
	.audio_mute_control = enc3_audio_mute_control,

	.dp_audio_setup = enc3_se_dp_audio_setup,
	.dp_audio_enable = enc3_se_dp_audio_enable,
	.dp_audio_disable = enc1_se_dp_audio_disable,

	.hdmi_audio_setup = enc3_se_hdmi_audio_setup,
	.hdmi_audio_disable = enc1_se_hdmi_audio_disable,
	.setup_stereo_sync  = enc1_setup_stereo_sync,
	.set_avmute = enc1_stream_encoder_set_avmute,
	.dig_connect_to_otg = enc1_dig_connect_to_otg,
	.dig_source_otg = enc1_dig_source_otg,

	.dp_get_pixel_format  = enc1_stream_encoder_dp_get_pixel_format,

	.enc_read_state = enc401_read_state,
	.dp_set_dsc_config = NULL,
	.dp_set_dsc_pps_info_packet = enc3_dp_set_dsc_pps_info_packet,
	.set_dynamic_metadata = enc401_set_dynamic_metadata,
	.hdmi_reset_stream_attribute = enc1_reset_hdmi_stream_attribute,
	.enable_stream = enc401_stream_encoder_enable,

	.set_input_mode = enc401_set_dig_input_mode,
	.enable_fifo = enc35_enable_fifo,
	.disable_fifo = enc35_disable_fifo,
	.map_stream_to_link = enc401_stream_encoder_map_to_link,
};

void dcn401_dio_stream_encoder_construct(
	struct dcn10_stream_encoder *enc1,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct afmt *afmt,
	const struct dcn10_stream_enc_registers *regs,
	const struct dcn10_stream_encoder_shift *se_shift,
	const struct dcn10_stream_encoder_mask *se_mask)
{
	enc1->base.funcs = &dcn401_str_enc_funcs;
	enc1->base.ctx = ctx;
	enc1->base.id = eng_id;
	enc1->base.bp = bp;
	enc1->base.vpg = vpg;
	enc1->base.afmt = afmt;
	enc1->regs = regs;
	enc1->se_shift = se_shift;
	enc1->se_mask = se_mask;
	enc1->base.stream_enc_inst = vpg->inst;
}

void enc401_set_dynamic_metadata(struct stream_encoder *enc,
		bool enable_dme,
		uint32_t hubp_requestor_id,
		enum dynamic_metadata_mode dmdata_mode)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (enable_dme) {
		REG_UPDATE_2(DME_CONTROL,
				METADATA_HUBP_REQUESTOR_ID, hubp_requestor_id,
				METADATA_STREAM_TYPE, (dmdata_mode == dmdata_dolby_vision) ? 1 : 0);

		/* Use default line reference DP_SOF for bringup.
		 * Should use OTG_SOF for DRR cases
		 */
		if (dmdata_mode == dmdata_dp)
			REG_UPDATE_3(DP_SEC_METADATA_TRANSMISSION,
					DP_SEC_METADATA_PACKET_ENABLE, 1,
					DP_SEC_METADATA_PACKET_LINE_REFERENCE, 0,
					DP_SEC_METADATA_PACKET_LINE, 20);
		else {
			REG_UPDATE_3(HDMI_METADATA_PACKET_CONTROL,
					HDMI_METADATA_PACKET_ENABLE, 1,
					HDMI_METADATA_PACKET_LINE_REFERENCE, 0,
					HDMI_METADATA_PACKET_LINE, 2);

			if (dmdata_mode == dmdata_dolby_vision)
				REG_UPDATE(HDMI_CONTROL,
						DOLBY_VISION_EN, 1);
		}

		REG_UPDATE(DME_CONTROL,
				METADATA_ENGINE_EN, 1);
	} else {
		REG_UPDATE(DME_CONTROL,
				METADATA_ENGINE_EN, 0);

		if (dmdata_mode == dmdata_dp)
			REG_UPDATE(DP_SEC_METADATA_TRANSMISSION,
					DP_SEC_METADATA_PACKET_ENABLE, 0);
		else {
			REG_UPDATE(HDMI_METADATA_PACKET_CONTROL,
					HDMI_METADATA_PACKET_ENABLE, 0);
			REG_UPDATE(HDMI_CONTROL,
					DOLBY_VISION_EN, 0);
		}
	}
}
void enc401_stream_encoder_set_stream_attribute_helper(
		struct dcn10_stream_encoder *enc1,
		struct dc_crtc_timing *crtc_timing)
{
	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE(HDMI_CONTROL, TMDS_PIXEL_ENCODING, 1);
		break;
	default:
		REG_UPDATE(HDMI_CONTROL, TMDS_PIXEL_ENCODING, 0);
		break;
	}
	REG_UPDATE(HDMI_CONTROL, TMDS_COLOR_FORMAT, 0);
}
