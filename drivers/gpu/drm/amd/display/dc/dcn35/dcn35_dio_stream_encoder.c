/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 */


#include "dc_bios_types.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn314/dcn314_dio_stream_encoder.h"
#include "dcn32/dcn32_dio_stream_encoder.h"
#include "dcn35_dio_stream_encoder.h"
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
/* setup stream encoder in dvi mode */
static void enc35_stream_encoder_dvi_set_stream_attribute(
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
	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);
}
/* setup stream encoder in hdmi mode */
static void enc35_stream_encoder_hdmi_set_stream_attribute(
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
		enc314_enable_fifo(enc);
	}

	/* Configure pixel encoding */
	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);

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



static void enc35_stream_encoder_enable(
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
			/* DP SST */
			REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_MODE, 0);
			break;
		default:
			/* invalid mode ! */
			ASSERT_CRITICAL(false);
		}
	}
}

static bool is_two_pixels_per_containter(const struct dc_crtc_timing *timing)
{
	bool two_pix = timing->pixel_encoding == PIXEL_ENCODING_YCBCR420;

	two_pix = two_pix || (timing->flags.DSC && timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
			&& !timing->dsc_cfg.ycbcr422_simple);
	return two_pix;
}

static bool is_h_timing_divisible_by_2(const struct dc_crtc_timing *timing)
{
	/* math borrowed from function of same name in inc/resource
	 * checks if h_timing is divisible by 2
	 */

	bool divisible = false;
	uint16_t h_blank_start = 0;
	uint16_t h_blank_end = 0;

	if (timing) {
		h_blank_start = timing->h_total - timing->h_front_porch;
		h_blank_end = h_blank_start - timing->h_addressable;

		/* HTOTAL, Hblank start/end, and Hsync start/end all must be
		 * divisible by 2 in order for the horizontal timing params
		 * to be considered divisible by 2. Hsync start is always 0.
		 */
		divisible = (timing->h_total % 2 == 0) &&
				(h_blank_start % 2 == 0) &&
				(h_blank_end % 2 == 0) &&
				(timing->h_sync_width % 2 == 0);
	}
	return divisible;
}

static bool is_dp_dig_pixel_rate_div_policy(struct dc *dc, const struct dc_crtc_timing *timing)
{
	/* should be functionally the same as dcn32_is_dp_dig_pixel_rate_div_policy for DP encoders*/
	return is_h_timing_divisible_by_2(timing) &&
		dc->debug.enable_dp_dig_pixel_rate_div_policy;
}

static void enc35_stream_encoder_dp_unblank(
		struct dc_link *link,
		struct stream_encoder *enc,
		const struct encoder_unblank_param *param)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	struct dc *dc = enc->ctx->dc;

	if (param->link_settings.link_rate != LINK_RATE_UNKNOWN) {
		uint32_t n_vid = 0x8000;
		uint32_t m_vid;
		uint32_t n_multiply = 0;
		uint32_t pix_per_cycle = 0;
		uint64_t m_vid_l = n_vid;

		/* YCbCr 4:2:0 : Computed VID_M will be 2X the input rate */
		if (is_two_pixels_per_containter(&param->timing) || param->opp_cnt > 1
			|| is_dp_dig_pixel_rate_div_policy(dc, &param->timing)) {
			/*this logic should be the same in get_pixel_clock_parameters() */
			n_multiply = 1;
			pix_per_cycle = 1;
		}
		/* M / N = Fstream / Flink
		 * m_vid / n_vid = pixel rate / link rate
		 */

		m_vid_l *= param->timing.pix_clk_100hz / 10;
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

		REG_UPDATE_2(DP_VID_TIMING,
				DP_VID_M_N_GEN_EN, 1,
				DP_VID_N_MUL, n_multiply);

		REG_UPDATE(DP_PIXEL_FORMAT,
				DP_PIXEL_PER_CYCLE_PROCESSING_MODE,
				pix_per_cycle);
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

	/* wait 100us for DIG/DP logic to prime
	 * (i.e. a few video lines)
	 */
	udelay(100);

	/* the hardware would start sending video at the start of the next DP
	 * frame (i.e. rising edge of the vblank).
	 * NOTE: We used to program DP_VID_STREAM_DIS_DEFER = 2 here, but this
	 * register has no effect on enable transition! HW always makes sure
	 * VID_STREAM enable at start of next frame, and this is not
	 * programmable
	 */

	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, true);

	/*
	 * DIG Resync FIFO now needs to be explicitly enabled.
	 * This should come after DP_VID_STREAM_ENABLE per HW docs.
	 */
	enc314_enable_fifo(enc);

	link->dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_DP_VID_STREAM);
}

static void enc35_stream_encoder_map_to_link(
		struct stream_encoder *enc,
		uint32_t stream_enc_inst,
		uint32_t link_enc_inst)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	ASSERT(stream_enc_inst < 5 && link_enc_inst < 5);
	REG_UPDATE(STREAM_MAPPER_CONTROL,
				DIG_STREAM_LINK_TARGET, link_enc_inst);
}

static void enc35_reset_fifo(struct stream_encoder *enc, bool reset)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t reset_val = reset ? 1 : 0;
	uint32_t is_symclk_on;

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_RESET, reset_val);
	REG_GET(DIG_FE_CLK_CNTL, DIG_FE_SYMCLK_FE_G_CLOCK_ON, &is_symclk_on);

	if (is_symclk_on)
		REG_WAIT(DIG_FIFO_CTRL0, DIG_FIFO_RESET_DONE, reset_val, 10, 5000);
	else
		udelay(10);
}

static void enc35_disable_fifo(struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_ENABLE, 0);
	REG_UPDATE(DIG_FE_EN_CNTL, DIG_FE_ENABLE, 0);
	REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_CLK_EN, 0);
}

static void enc35_enable_fifo(struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_READ_START_LEVEL, 0x7);
	REG_UPDATE(DIG_FE_CLK_CNTL, DIG_FE_CLK_EN, 1);
	REG_UPDATE(DIG_FE_EN_CNTL, DIG_FE_ENABLE, 1);

	enc35_reset_fifo(enc, true);
	enc35_reset_fifo(enc, false);

	REG_UPDATE(DIG_FIFO_CTRL0, DIG_FIFO_ENABLE, 1);
}

static const struct stream_encoder_funcs dcn35_str_enc_funcs = {
	.dp_set_odm_combine =
		enc314_dp_set_odm_combine,
	.dp_set_stream_attribute =
		enc2_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		enc35_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		enc35_stream_encoder_dvi_set_stream_attribute,
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
		enc314_stream_encoder_dp_blank,
	.dp_unblank =
		enc35_stream_encoder_dp_unblank,
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

	.enc_read_state = enc314_read_state,
	.dp_set_dsc_config = enc314_dp_set_dsc_config,
	.dp_set_dsc_pps_info_packet = enc3_dp_set_dsc_pps_info_packet,
	.set_dynamic_metadata = enc2_set_dynamic_metadata,
	.hdmi_reset_stream_attribute = enc1_reset_hdmi_stream_attribute,
	.dig_stream_enable = enc35_stream_encoder_enable,

	.set_input_mode = enc314_set_dig_input_mode,
	.enable_fifo = enc35_enable_fifo,
	.disable_fifo = enc35_disable_fifo,
	.map_stream_to_link = enc35_stream_encoder_map_to_link,
};

void dcn35_dio_stream_encoder_construct(
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
	enc1->base.funcs = &dcn35_str_enc_funcs;
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

