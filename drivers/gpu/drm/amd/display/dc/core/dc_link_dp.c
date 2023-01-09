/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 */
#include "dm_services.h"
#include "dc.h"
#include "dc_link_dp.h"
#include "dm_helpers.h"
#include "opp.h"
#include "dsc.h"
#include "resource.h"

#include "inc/core_types.h"
#include "link_hwss.h"
#include "link/protocols/link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"

#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "link/protocols/link_dp_dpia.h"
#include "inc/link_enc_cfg.h"
#include "clk_mgr.h"
#include "link/accessories/link_dp_trace.h"
#include "link/protocols/link_dp_training.h"
#include "link/protocols/link_dp_training_fixed_vs_pe_retimer.h"
#include "link/protocols/link_dp_training_dpia.h"
#include "link/protocols/link_dp_training_auxless.h"
#include "link/protocols/link_dp_phy.h"
#include "link/protocols/link_dp_capability.h"
#define DC_LOGGER \
	link->ctx->logger

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */
#include "link/protocols/link_dpcd.h"

bool dp_validate_mode_timing(
	struct dc_link *link,
	const struct dc_crtc_timing *timing)
{
	uint32_t req_bw;
	uint32_t max_bw;

	const struct dc_link_settings *link_setting;

	/* According to spec, VSC SDP should be used if pixel format is YCbCr420 */
	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 &&
			!link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED &&
			dal_graphics_object_id_get_connector_id(link->link_id) != CONNECTOR_ID_VIRTUAL)
		return false;

	/*always DP fail safe mode*/
	if ((timing->pix_clk_100hz / 10) == (uint32_t) 25175 &&
		timing->h_addressable == (uint32_t) 640 &&
		timing->v_addressable == (uint32_t) 480)
		return true;

	link_setting = dc_link_get_link_cap(link);

	/* TODO: DYNAMIC_VALIDATION needs to be implemented */
	/*if (flags.DYNAMIC_VALIDATION == 1 &&
		link->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN)
		link_setting = &link->verified_link_cap;
	*/

	req_bw = dc_bandwidth_in_kbps_from_timing(timing);
	max_bw = dc_link_bandwidth_kbps(link, link_setting);

	if (req_bw <= max_bw) {
		/* remember the biggest mode here, during
		 * initial link training (to get
		 * verified_link_cap), LS sends event about
		 * cannot train at reported cap to upper
		 * layer and upper layer will re-enumerate modes.
		 * this is not necessary if the lower
		 * verified_link_cap is enough to drive
		 * all the modes */

		/* TODO: DYNAMIC_VALIDATION needs to be implemented */
		/* if (flags.DYNAMIC_VALIDATION == 1)
			dpsst->max_req_bw_for_verified_linkcap = dal_max(
				dpsst->max_req_bw_for_verified_linkcap, req_bw); */
		return true;
	} else
		return false;
}

static enum dc_link_rate get_link_rate_from_test_link_rate(uint8_t test_rate)
{
	switch (test_rate) {
	case DP_TEST_LINK_RATE_RBR:
		return LINK_RATE_LOW;
	case DP_TEST_LINK_RATE_HBR:
		return LINK_RATE_HIGH;
	case DP_TEST_LINK_RATE_HBR2:
		return LINK_RATE_HIGH2;
	case DP_TEST_LINK_RATE_HBR3:
		return LINK_RATE_HIGH3;
	case DP_TEST_LINK_RATE_UHBR10:
		return LINK_RATE_UHBR10;
	case DP_TEST_LINK_RATE_UHBR20:
		return LINK_RATE_UHBR20;
	case DP_TEST_LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR13_5;
	default:
		return LINK_RATE_UNKNOWN;
	}
}

static void dp_test_send_link_training(struct dc_link *link)
{
	struct dc_link_settings link_settings = {0};
	uint8_t test_rate = 0;

	core_link_read_dpcd(
			link,
			DP_TEST_LANE_COUNT,
			(unsigned char *)(&link_settings.lane_count),
			1);
	core_link_read_dpcd(
			link,
			DP_TEST_LINK_RATE,
			&test_rate,
			1);
	link_settings.link_rate = get_link_rate_from_test_link_rate(test_rate);

	/* Set preferred link settings */
	link->verified_link_cap.lane_count = link_settings.lane_count;
	link->verified_link_cap.link_rate = link_settings.link_rate;

	dp_retrain_link_dp_test(link, &link_settings, false);
}

static bool is_dp_phy_sqaure_pattern(enum dp_test_pattern test_pattern)
{
	return (DP_TEST_PATTERN_SQUARE_BEGIN <= test_pattern &&
			test_pattern <= DP_TEST_PATTERN_SQUARE_END);
}

/* TODO Raven hbr2 compliance eye output is unstable
 * (toggling on and off) with debugger break
 * This caueses intermittent PHY automation failure
 * Need to look into the root cause */
static void dp_test_send_phy_test_pattern(struct dc_link *link)
{
	union phy_test_pattern dpcd_test_pattern;
	union lane_adjust dpcd_lane_adjustment[2];
	unsigned char dpcd_post_cursor_2_adjustment = 0;
	unsigned char test_pattern_buffer[
			(DP_TEST_264BIT_CUSTOM_PATTERN_263_256 -
			DP_TEST_264BIT_CUSTOM_PATTERN_7_0)+1] = {0};
	unsigned int test_pattern_size = 0;
	enum dp_test_pattern test_pattern;
	union lane_adjust dpcd_lane_adjust;
	unsigned int lane;
	struct link_training_settings link_training_settings;
	unsigned char no_preshoot = 0;
	unsigned char no_deemphasis = 0;

	dpcd_test_pattern.raw = 0;
	memset(dpcd_lane_adjustment, 0, sizeof(dpcd_lane_adjustment));
	memset(&link_training_settings, 0, sizeof(link_training_settings));

	/* get phy test pattern and pattern parameters from DP receiver */
	core_link_read_dpcd(
			link,
			DP_PHY_TEST_PATTERN,
			&dpcd_test_pattern.raw,
			sizeof(dpcd_test_pattern));
	core_link_read_dpcd(
			link,
			DP_ADJUST_REQUEST_LANE0_1,
			&dpcd_lane_adjustment[0].raw,
			sizeof(dpcd_lane_adjustment));

	/* prepare link training settings */
	link_training_settings.link_settings = link->cur_link_settings;

	link_training_settings.lttpr_mode = dc_link_decide_lttpr_mode(link, &link->cur_link_settings);

	if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link_training_settings.lttpr_mode == LTTPR_MODE_TRANSPARENT)
		dp_fixed_vs_pe_read_lane_adjust(
				link,
				link_training_settings.dpcd_lane_settings);

	/*get post cursor 2 parameters
	 * For DP 1.1a or eariler, this DPCD register's value is 0
	 * For DP 1.2 or later:
	 * Bits 1:0 = POST_CURSOR2_LANE0; Bits 3:2 = POST_CURSOR2_LANE1
	 * Bits 5:4 = POST_CURSOR2_LANE2; Bits 7:6 = POST_CURSOR2_LANE3
	 */
	core_link_read_dpcd(
			link,
			DP_ADJUST_REQUEST_POST_CURSOR2,
			&dpcd_post_cursor_2_adjustment,
			sizeof(dpcd_post_cursor_2_adjustment));

	/* translate request */
	switch (dpcd_test_pattern.bits.PATTERN) {
	case PHY_TEST_PATTERN_D10_2:
		test_pattern = DP_TEST_PATTERN_D102;
		break;
	case PHY_TEST_PATTERN_SYMBOL_ERROR:
		test_pattern = DP_TEST_PATTERN_SYMBOL_ERROR;
		break;
	case PHY_TEST_PATTERN_PRBS7:
		test_pattern = DP_TEST_PATTERN_PRBS7;
		break;
	case PHY_TEST_PATTERN_80BIT_CUSTOM:
		test_pattern = DP_TEST_PATTERN_80BIT_CUSTOM;
		break;
	case PHY_TEST_PATTERN_CP2520_1:
		/* CP2520 pattern is unstable, temporarily use TPS4 instead */
		test_pattern = (link->dc->caps.force_dp_tps4_for_cp2520 == 1) ?
				DP_TEST_PATTERN_TRAINING_PATTERN4 :
				DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE;
		break;
	case PHY_TEST_PATTERN_CP2520_2:
		/* CP2520 pattern is unstable, temporarily use TPS4 instead */
		test_pattern = (link->dc->caps.force_dp_tps4_for_cp2520 == 1) ?
				DP_TEST_PATTERN_TRAINING_PATTERN4 :
				DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE;
		break;
	case PHY_TEST_PATTERN_CP2520_3:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN4;
		break;
	case PHY_TEST_PATTERN_128b_132b_TPS1:
		test_pattern = DP_TEST_PATTERN_128b_132b_TPS1;
		break;
	case PHY_TEST_PATTERN_128b_132b_TPS2:
		test_pattern = DP_TEST_PATTERN_128b_132b_TPS2;
		break;
	case PHY_TEST_PATTERN_PRBS9:
		test_pattern = DP_TEST_PATTERN_PRBS9;
		break;
	case PHY_TEST_PATTERN_PRBS11:
		test_pattern = DP_TEST_PATTERN_PRBS11;
		break;
	case PHY_TEST_PATTERN_PRBS15:
		test_pattern = DP_TEST_PATTERN_PRBS15;
		break;
	case PHY_TEST_PATTERN_PRBS23:
		test_pattern = DP_TEST_PATTERN_PRBS23;
		break;
	case PHY_TEST_PATTERN_PRBS31:
		test_pattern = DP_TEST_PATTERN_PRBS31;
		break;
	case PHY_TEST_PATTERN_264BIT_CUSTOM:
		test_pattern = DP_TEST_PATTERN_264BIT_CUSTOM;
		break;
	case PHY_TEST_PATTERN_SQUARE:
		test_pattern = DP_TEST_PATTERN_SQUARE;
		break;
	case PHY_TEST_PATTERN_SQUARE_PRESHOOT_DISABLED:
		test_pattern = DP_TEST_PATTERN_SQUARE_PRESHOOT_DISABLED;
		no_preshoot = 1;
		break;
	case PHY_TEST_PATTERN_SQUARE_DEEMPHASIS_DISABLED:
		test_pattern = DP_TEST_PATTERN_SQUARE_DEEMPHASIS_DISABLED;
		no_deemphasis = 1;
		break;
	case PHY_TEST_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED:
		test_pattern = DP_TEST_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED;
		no_preshoot = 1;
		no_deemphasis = 1;
		break;
	default:
		test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
	break;
	}

	if (test_pattern == DP_TEST_PATTERN_80BIT_CUSTOM) {
		test_pattern_size = (DP_TEST_80BIT_CUSTOM_PATTERN_79_72 -
				DP_TEST_80BIT_CUSTOM_PATTERN_7_0) + 1;
		core_link_read_dpcd(
				link,
				DP_TEST_80BIT_CUSTOM_PATTERN_7_0,
				test_pattern_buffer,
				test_pattern_size);
	}

	if (is_dp_phy_sqaure_pattern(test_pattern)) {
		test_pattern_size = 1; // Square pattern data is 1 byte (DP spec)
		core_link_read_dpcd(
				link,
				DP_PHY_SQUARE_PATTERN,
				test_pattern_buffer,
				test_pattern_size);
	}

	if (test_pattern == DP_TEST_PATTERN_264BIT_CUSTOM) {
		test_pattern_size = (DP_TEST_264BIT_CUSTOM_PATTERN_263_256-
				DP_TEST_264BIT_CUSTOM_PATTERN_7_0) + 1;
		core_link_read_dpcd(
				link,
				DP_TEST_264BIT_CUSTOM_PATTERN_7_0,
				test_pattern_buffer,
				test_pattern_size);
	}

	for (lane = 0; lane <
		(unsigned int)(link->cur_link_settings.lane_count);
		lane++) {
		dpcd_lane_adjust.raw =
			dp_get_nibble_at_index(&dpcd_lane_adjustment[0].raw, lane);
		if (link_dp_get_encoding_format(&link->cur_link_settings) ==
				DP_8b_10b_ENCODING) {
			link_training_settings.hw_lane_settings[lane].VOLTAGE_SWING =
				(enum dc_voltage_swing)
				(dpcd_lane_adjust.bits.VOLTAGE_SWING_LANE);
			link_training_settings.hw_lane_settings[lane].PRE_EMPHASIS =
				(enum dc_pre_emphasis)
				(dpcd_lane_adjust.bits.PRE_EMPHASIS_LANE);
			link_training_settings.hw_lane_settings[lane].POST_CURSOR2 =
				(enum dc_post_cursor2)
				((dpcd_post_cursor_2_adjustment >> (lane * 2)) & 0x03);
		} else if (link_dp_get_encoding_format(&link->cur_link_settings) ==
				DP_128b_132b_ENCODING) {
			link_training_settings.hw_lane_settings[lane].FFE_PRESET.settings.level =
					dpcd_lane_adjust.tx_ffe.PRESET_VALUE;
			link_training_settings.hw_lane_settings[lane].FFE_PRESET.settings.no_preshoot = no_preshoot;
			link_training_settings.hw_lane_settings[lane].FFE_PRESET.settings.no_deemphasis = no_deemphasis;
		}
	}

	dp_hw_to_dpcd_lane_settings(&link_training_settings,
			link_training_settings.hw_lane_settings,
			link_training_settings.dpcd_lane_settings);
	/*Usage: Measure DP physical lane signal
	 * by DP SI test equipment automatically.
	 * PHY test pattern request is generated by equipment via HPD interrupt.
	 * HPD needs to be active all the time. HPD should be active
	 * all the time. Do not touch it.
	 * forward request to DS
	 */
	dc_link_dp_set_test_pattern(
		link,
		test_pattern,
		DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED,
		&link_training_settings,
		test_pattern_buffer,
		test_pattern_size);
}

static void dp_test_get_audio_test_data(struct dc_link *link, bool disable_video)
{
	union audio_test_mode            dpcd_test_mode = {0};
	struct audio_test_pattern_type   dpcd_pattern_type = {0};
	union audio_test_pattern_period  dpcd_pattern_period[AUDIO_CHANNELS_COUNT] = {0};
	enum dp_test_pattern test_pattern = DP_TEST_PATTERN_AUDIO_OPERATOR_DEFINED;

	struct pipe_ctx *pipes = link->dc->current_state->res_ctx.pipe_ctx;
	struct pipe_ctx *pipe_ctx = &pipes[0];
	unsigned int channel_count;
	unsigned int channel = 0;
	unsigned int modes = 0;
	unsigned int sampling_rate_in_hz = 0;

	// get audio test mode and test pattern parameters
	core_link_read_dpcd(
		link,
		DP_TEST_AUDIO_MODE,
		&dpcd_test_mode.raw,
		sizeof(dpcd_test_mode));

	core_link_read_dpcd(
		link,
		DP_TEST_AUDIO_PATTERN_TYPE,
		&dpcd_pattern_type.value,
		sizeof(dpcd_pattern_type));

	channel_count = min(dpcd_test_mode.bits.channel_count + 1, AUDIO_CHANNELS_COUNT);

	// read pattern periods for requested channels when sawTooth pattern is requested
	if (dpcd_pattern_type.value == AUDIO_TEST_PATTERN_SAWTOOTH ||
			dpcd_pattern_type.value == AUDIO_TEST_PATTERN_OPERATOR_DEFINED) {

		test_pattern = (dpcd_pattern_type.value == AUDIO_TEST_PATTERN_SAWTOOTH) ?
				DP_TEST_PATTERN_AUDIO_SAWTOOTH : DP_TEST_PATTERN_AUDIO_OPERATOR_DEFINED;
		// read period for each channel
		for (channel = 0; channel < channel_count; channel++) {
			core_link_read_dpcd(
							link,
							DP_TEST_AUDIO_PERIOD_CH1 + channel,
							&dpcd_pattern_period[channel].raw,
							sizeof(dpcd_pattern_period[channel]));
		}
	}

	// translate sampling rate
	switch (dpcd_test_mode.bits.sampling_rate) {
	case AUDIO_SAMPLING_RATE_32KHZ:
		sampling_rate_in_hz = 32000;
		break;
	case AUDIO_SAMPLING_RATE_44_1KHZ:
		sampling_rate_in_hz = 44100;
		break;
	case AUDIO_SAMPLING_RATE_48KHZ:
		sampling_rate_in_hz = 48000;
		break;
	case AUDIO_SAMPLING_RATE_88_2KHZ:
		sampling_rate_in_hz = 88200;
		break;
	case AUDIO_SAMPLING_RATE_96KHZ:
		sampling_rate_in_hz = 96000;
		break;
	case AUDIO_SAMPLING_RATE_176_4KHZ:
		sampling_rate_in_hz = 176400;
		break;
	case AUDIO_SAMPLING_RATE_192KHZ:
		sampling_rate_in_hz = 192000;
		break;
	default:
		sampling_rate_in_hz = 0;
		break;
	}

	link->audio_test_data.flags.test_requested = 1;
	link->audio_test_data.flags.disable_video = disable_video;
	link->audio_test_data.sampling_rate = sampling_rate_in_hz;
	link->audio_test_data.channel_count = channel_count;
	link->audio_test_data.pattern_type = test_pattern;

	if (test_pattern == DP_TEST_PATTERN_AUDIO_SAWTOOTH) {
		for (modes = 0; modes < pipe_ctx->stream->audio_info.mode_count; modes++) {
			link->audio_test_data.pattern_period[modes] = dpcd_pattern_period[modes].bits.pattern_period;
		}
	}
}

void dc_link_dp_handle_automated_test(struct dc_link *link)
{
	union test_request test_request;
	union test_response test_response;

	memset(&test_request, 0, sizeof(test_request));
	memset(&test_response, 0, sizeof(test_response));

	core_link_read_dpcd(
		link,
		DP_TEST_REQUEST,
		&test_request.raw,
		sizeof(union test_request));
	if (test_request.bits.LINK_TRAINING) {
		/* ACK first to let DP RX test box monitor LT sequence */
		test_response.bits.ACK = 1;
		core_link_write_dpcd(
			link,
			DP_TEST_RESPONSE,
			&test_response.raw,
			sizeof(test_response));
		dp_test_send_link_training(link);
		/* no acknowledge request is needed again */
		test_response.bits.ACK = 0;
	}
	if (test_request.bits.LINK_TEST_PATTRN) {
		union test_misc dpcd_test_params;
		union link_test_pattern dpcd_test_pattern;

		memset(&dpcd_test_pattern, 0, sizeof(dpcd_test_pattern));
		memset(&dpcd_test_params, 0, sizeof(dpcd_test_params));

		/* get link test pattern and pattern parameters */
		core_link_read_dpcd(
				link,
				DP_TEST_PATTERN,
				&dpcd_test_pattern.raw,
				sizeof(dpcd_test_pattern));
		core_link_read_dpcd(
				link,
				DP_TEST_MISC0,
				&dpcd_test_params.raw,
				sizeof(dpcd_test_params));
		test_response.bits.ACK = dm_helpers_dp_handle_test_pattern_request(link->ctx, link,
				dpcd_test_pattern, dpcd_test_params) ? 1 : 0;
	}

	if (test_request.bits.AUDIO_TEST_PATTERN) {
		dp_test_get_audio_test_data(link, test_request.bits.TEST_AUDIO_DISABLED_VIDEO);
		test_response.bits.ACK = 1;
	}

	if (test_request.bits.PHY_TEST_PATTERN) {
		dp_test_send_phy_test_pattern(link);
		test_response.bits.ACK = 1;
	}

	/* send request acknowledgment */
	if (test_response.bits.ACK)
		core_link_write_dpcd(
			link,
			DP_TEST_RESPONSE,
			&test_response.raw,
			sizeof(test_response));
}

static bool is_dp_phy_pattern(enum dp_test_pattern test_pattern)
{
	if ((DP_TEST_PATTERN_PHY_PATTERN_BEGIN <= test_pattern &&
			test_pattern <= DP_TEST_PATTERN_PHY_PATTERN_END) ||
			test_pattern == DP_TEST_PATTERN_VIDEO_MODE)
		return true;
	else
		return false;
}

static void set_crtc_test_pattern(struct dc_link *link,
				struct pipe_ctx *pipe_ctx,
				enum dp_test_pattern test_pattern,
				enum dp_test_pattern_color_space test_pattern_color_space)
{
	enum controller_dp_test_pattern controller_test_pattern;
	enum dc_color_depth color_depth = pipe_ctx->
		stream->timing.display_color_depth;
	struct bit_depth_reduction_params params;
	struct output_pixel_processor *opp = pipe_ctx->stream_res.opp;
	int width = pipe_ctx->stream->timing.h_addressable +
		pipe_ctx->stream->timing.h_border_left +
		pipe_ctx->stream->timing.h_border_right;
	int height = pipe_ctx->stream->timing.v_addressable +
		pipe_ctx->stream->timing.v_border_bottom +
		pipe_ctx->stream->timing.v_border_top;

	memset(&params, 0, sizeof(params));

	switch (test_pattern) {
	case DP_TEST_PATTERN_COLOR_SQUARES:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES;
	break;
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA;
	break;
	case DP_TEST_PATTERN_VERTICAL_BARS:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_VERTICALBARS;
	break;
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS;
	break;
	case DP_TEST_PATTERN_COLOR_RAMP:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORRAMP;
	break;
	default:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_VIDEOMODE;
	break;
	}

	switch (test_pattern) {
	case DP_TEST_PATTERN_COLOR_SQUARES:
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
	case DP_TEST_PATTERN_VERTICAL_BARS:
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
	case DP_TEST_PATTERN_COLOR_RAMP:
	{
		/* disable bit depth reduction */
		pipe_ctx->stream->bit_depth_params = params;
		opp->funcs->opp_program_bit_depth_reduction(opp, &params);
		if (pipe_ctx->stream_res.tg->funcs->set_test_pattern)
			pipe_ctx->stream_res.tg->funcs->set_test_pattern(pipe_ctx->stream_res.tg,
				controller_test_pattern, color_depth);
		else if (link->dc->hwss.set_disp_pattern_generator) {
			struct pipe_ctx *odm_pipe;
			enum controller_dp_color_space controller_color_space;
			int opp_cnt = 1;
			int offset = 0;
			int dpg_width = width;

			switch (test_pattern_color_space) {
			case DP_TEST_PATTERN_COLOR_SPACE_RGB:
				controller_color_space = CONTROLLER_DP_COLOR_SPACE_RGB;
				break;
			case DP_TEST_PATTERN_COLOR_SPACE_YCBCR601:
				controller_color_space = CONTROLLER_DP_COLOR_SPACE_YCBCR601;
				break;
			case DP_TEST_PATTERN_COLOR_SPACE_YCBCR709:
				controller_color_space = CONTROLLER_DP_COLOR_SPACE_YCBCR709;
				break;
			case DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED:
			default:
				controller_color_space = CONTROLLER_DP_COLOR_SPACE_UDEFINED;
				DC_LOG_ERROR("%s: Color space must be defined for test pattern", __func__);
				ASSERT(0);
				break;
			}

			for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
				opp_cnt++;
			dpg_width = width / opp_cnt;
			offset = dpg_width;

			link->dc->hwss.set_disp_pattern_generator(link->dc,
					pipe_ctx,
					controller_test_pattern,
					controller_color_space,
					color_depth,
					NULL,
					dpg_width,
					height,
					0);

			for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
				struct output_pixel_processor *odm_opp = odm_pipe->stream_res.opp;

				odm_opp->funcs->opp_program_bit_depth_reduction(odm_opp, &params);
				link->dc->hwss.set_disp_pattern_generator(link->dc,
						odm_pipe,
						controller_test_pattern,
						controller_color_space,
						color_depth,
						NULL,
						dpg_width,
						height,
						offset);
				offset += offset;
			}
		}
	}
	break;
	case DP_TEST_PATTERN_VIDEO_MODE:
	{
		/* restore bitdepth reduction */
		resource_build_bit_depth_reduction_params(pipe_ctx->stream, &params);
		pipe_ctx->stream->bit_depth_params = params;
		opp->funcs->opp_program_bit_depth_reduction(opp, &params);
		if (pipe_ctx->stream_res.tg->funcs->set_test_pattern)
			pipe_ctx->stream_res.tg->funcs->set_test_pattern(pipe_ctx->stream_res.tg,
				CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
				color_depth);
		else if (link->dc->hwss.set_disp_pattern_generator) {
			struct pipe_ctx *odm_pipe;
			int opp_cnt = 1;
			int dpg_width;

			for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
				opp_cnt++;

			dpg_width = width / opp_cnt;
			for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
				struct output_pixel_processor *odm_opp = odm_pipe->stream_res.opp;

				odm_opp->funcs->opp_program_bit_depth_reduction(odm_opp, &params);
				link->dc->hwss.set_disp_pattern_generator(link->dc,
						odm_pipe,
						CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
						CONTROLLER_DP_COLOR_SPACE_UDEFINED,
						color_depth,
						NULL,
						dpg_width,
						height,
						0);
			}
			link->dc->hwss.set_disp_pattern_generator(link->dc,
					pipe_ctx,
					CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
					CONTROLLER_DP_COLOR_SPACE_UDEFINED,
					color_depth,
					NULL,
					dpg_width,
					height,
					0);
		}
	}
	break;

	default:
	break;
	}
}

bool dc_link_dp_set_test_pattern(
	struct dc_link *link,
	enum dp_test_pattern test_pattern,
	enum dp_test_pattern_color_space test_pattern_color_space,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size)
{
	struct pipe_ctx *pipes = link->dc->current_state->res_ctx.pipe_ctx;
	struct pipe_ctx *pipe_ctx = NULL;
	unsigned int lane;
	unsigned int i;
	unsigned char link_qual_pattern[LANE_COUNT_DP_MAX] = {0};
	union dpcd_training_pattern training_pattern;
	enum dpcd_phy_test_patterns pattern;

	memset(&training_pattern, 0, sizeof(training_pattern));

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream == NULL)
			continue;

		if (pipes[i].stream->link == link && !pipes[i].top_pipe && !pipes[i].prev_odm_pipe) {
			pipe_ctx = &pipes[i];
			break;
		}
	}

	if (pipe_ctx == NULL)
		return false;

	/* Reset CRTC Test Pattern if it is currently running and request is VideoMode */
	if (link->test_pattern_enabled && test_pattern ==
			DP_TEST_PATTERN_VIDEO_MODE) {
		/* Set CRTC Test Pattern */
		set_crtc_test_pattern(link, pipe_ctx, test_pattern, test_pattern_color_space);
		dp_set_hw_test_pattern(link, &pipe_ctx->link_res, test_pattern,
				(uint8_t *)p_custom_pattern,
				(uint32_t)cust_pattern_size);

		/* Unblank Stream */
		link->dc->hwss.unblank_stream(
			pipe_ctx,
			&link->verified_link_cap);
		/* TODO:m_pHwss->MuteAudioEndpoint
		 * (pPathMode->pDisplayPath, false);
		 */

		/* Reset Test Pattern state */
		link->test_pattern_enabled = false;

		return true;
	}

	/* Check for PHY Test Patterns */
	if (is_dp_phy_pattern(test_pattern)) {
		/* Set DPCD Lane Settings before running test pattern */
		if (p_link_settings != NULL) {
			if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
					p_link_settings->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
				dp_fixed_vs_pe_set_retimer_lane_settings(
						link,
						p_link_settings->dpcd_lane_settings,
						p_link_settings->link_settings.lane_count);
			} else {
				dp_set_hw_lane_settings(link, &pipe_ctx->link_res, p_link_settings, DPRX);
			}
			dpcd_set_lane_settings(link, p_link_settings, DPRX);
		}

		/* Blank stream if running test pattern */
		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/*TODO:
			 * m_pHwss->
			 * MuteAudioEndpoint(pPathMode->pDisplayPath, true);
			 */
			/* Blank stream */
			link->dc->hwss.blank_stream(pipe_ctx);
		}

		dp_set_hw_test_pattern(link, &pipe_ctx->link_res, test_pattern,
				(uint8_t *)p_custom_pattern,
				(uint32_t)cust_pattern_size);

		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/* Set Test Pattern state */
			link->test_pattern_enabled = true;
			if (p_link_settings != NULL)
				dpcd_set_link_settings(link,
						p_link_settings);
		}

		switch (test_pattern) {
		case DP_TEST_PATTERN_VIDEO_MODE:
			pattern = PHY_TEST_PATTERN_NONE;
			break;
		case DP_TEST_PATTERN_D102:
			pattern = PHY_TEST_PATTERN_D10_2;
			break;
		case DP_TEST_PATTERN_SYMBOL_ERROR:
			pattern = PHY_TEST_PATTERN_SYMBOL_ERROR;
			break;
		case DP_TEST_PATTERN_PRBS7:
			pattern = PHY_TEST_PATTERN_PRBS7;
			break;
		case DP_TEST_PATTERN_80BIT_CUSTOM:
			pattern = PHY_TEST_PATTERN_80BIT_CUSTOM;
			break;
		case DP_TEST_PATTERN_CP2520_1:
			pattern = PHY_TEST_PATTERN_CP2520_1;
			break;
		case DP_TEST_PATTERN_CP2520_2:
			pattern = PHY_TEST_PATTERN_CP2520_2;
			break;
		case DP_TEST_PATTERN_CP2520_3:
			pattern = PHY_TEST_PATTERN_CP2520_3;
			break;
		case DP_TEST_PATTERN_128b_132b_TPS1:
			pattern = PHY_TEST_PATTERN_128b_132b_TPS1;
			break;
		case DP_TEST_PATTERN_128b_132b_TPS2:
			pattern = PHY_TEST_PATTERN_128b_132b_TPS2;
			break;
		case DP_TEST_PATTERN_PRBS9:
			pattern = PHY_TEST_PATTERN_PRBS9;
			break;
		case DP_TEST_PATTERN_PRBS11:
			pattern = PHY_TEST_PATTERN_PRBS11;
			break;
		case DP_TEST_PATTERN_PRBS15:
			pattern = PHY_TEST_PATTERN_PRBS15;
			break;
		case DP_TEST_PATTERN_PRBS23:
			pattern = PHY_TEST_PATTERN_PRBS23;
			break;
		case DP_TEST_PATTERN_PRBS31:
			pattern = PHY_TEST_PATTERN_PRBS31;
			break;
		case DP_TEST_PATTERN_264BIT_CUSTOM:
			pattern = PHY_TEST_PATTERN_264BIT_CUSTOM;
			break;
		case DP_TEST_PATTERN_SQUARE:
			pattern = PHY_TEST_PATTERN_SQUARE;
			break;
		case DP_TEST_PATTERN_SQUARE_PRESHOOT_DISABLED:
			pattern = PHY_TEST_PATTERN_SQUARE_PRESHOOT_DISABLED;
			break;
		case DP_TEST_PATTERN_SQUARE_DEEMPHASIS_DISABLED:
			pattern = PHY_TEST_PATTERN_SQUARE_DEEMPHASIS_DISABLED;
			break;
		case DP_TEST_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED:
			pattern = PHY_TEST_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED;
			break;
		default:
			return false;
		}

		if (test_pattern == DP_TEST_PATTERN_VIDEO_MODE
		/*TODO:&& !pPathMode->pDisplayPath->IsTargetPoweredOn()*/)
			return false;

		if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
			if (is_dp_phy_sqaure_pattern(test_pattern))
				core_link_write_dpcd(link,
						DP_LINK_SQUARE_PATTERN,
						p_custom_pattern,
						1);

			/* tell receiver that we are sending qualification
			 * pattern DP 1.2 or later - DP receiver's link quality
			 * pattern is set using DPCD LINK_QUAL_LANEx_SET
			 * register (0x10B~0x10E)\
			 */
			for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++)
				link_qual_pattern[lane] =
						(unsigned char)(pattern);

			core_link_write_dpcd(link,
					DP_LINK_QUAL_LANE0_SET,
					link_qual_pattern,
					sizeof(link_qual_pattern));
		} else if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_10 ||
			   link->dpcd_caps.dpcd_rev.raw == 0) {
			/* tell receiver that we are sending qualification
			 * pattern DP 1.1a or earlier - DP receiver's link
			 * quality pattern is set using
			 * DPCD TRAINING_PATTERN_SET -> LINK_QUAL_PATTERN_SET
			 * register (0x102). We will use v_1.3 when we are
			 * setting test pattern for DP 1.1.
			 */
			core_link_read_dpcd(link, DP_TRAINING_PATTERN_SET,
					    &training_pattern.raw,
					    sizeof(training_pattern));
			training_pattern.v1_3.LINK_QUAL_PATTERN_SET = pattern;
			core_link_write_dpcd(link, DP_TRAINING_PATTERN_SET,
					     &training_pattern.raw,
					     sizeof(training_pattern));
		}
	} else {
		enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;

		switch (test_pattern_color_space) {
		case DP_TEST_PATTERN_COLOR_SPACE_RGB:
			color_space = COLOR_SPACE_SRGB;
			if (test_pattern == DP_TEST_PATTERN_COLOR_SQUARES_CEA)
				color_space = COLOR_SPACE_SRGB_LIMITED;
			break;

		case DP_TEST_PATTERN_COLOR_SPACE_YCBCR601:
			color_space = COLOR_SPACE_YCBCR601;
			if (test_pattern == DP_TEST_PATTERN_COLOR_SQUARES_CEA)
				color_space = COLOR_SPACE_YCBCR601_LIMITED;
			break;
		case DP_TEST_PATTERN_COLOR_SPACE_YCBCR709:
			color_space = COLOR_SPACE_YCBCR709;
			if (test_pattern == DP_TEST_PATTERN_COLOR_SQUARES_CEA)
				color_space = COLOR_SPACE_YCBCR709_LIMITED;
			break;
		default:
			break;
		}

		if (pipe_ctx->stream_res.tg->funcs->lock_doublebuffer_enable) {
			if (pipe_ctx->stream && should_use_dmub_lock(pipe_ctx->stream->link)) {
				union dmub_hw_lock_flags hw_locks = { 0 };
				struct dmub_hw_lock_inst_flags inst_flags = { 0 };

				hw_locks.bits.lock_dig = 1;
				inst_flags.dig_inst = pipe_ctx->stream_res.tg->inst;

				dmub_hw_lock_mgr_cmd(link->ctx->dmub_srv,
							true,
							&hw_locks,
							&inst_flags);
			} else
				pipe_ctx->stream_res.tg->funcs->lock_doublebuffer_enable(
						pipe_ctx->stream_res.tg);
		}

		pipe_ctx->stream_res.tg->funcs->lock(pipe_ctx->stream_res.tg);
		/* update MSA to requested color space */
		pipe_ctx->stream_res.stream_enc->funcs->dp_set_stream_attribute(pipe_ctx->stream_res.stream_enc,
				&pipe_ctx->stream->timing,
				color_space,
				pipe_ctx->stream->use_vsc_sdp_for_colorimetry,
				link->dpcd_caps.dprx_feature.bits.SST_SPLIT_SDP_CAP);

		if (pipe_ctx->stream->use_vsc_sdp_for_colorimetry) {
			if (test_pattern == DP_TEST_PATTERN_COLOR_SQUARES_CEA)
				pipe_ctx->stream->vsc_infopacket.sb[17] |= (1 << 7); // sb17 bit 7 Dynamic Range: 0 = VESA range, 1 = CTA range
			else
				pipe_ctx->stream->vsc_infopacket.sb[17] &= ~(1 << 7);
			resource_build_info_frame(pipe_ctx);
			link->dc->hwss.update_info_frame(pipe_ctx);
		}

		/* CRTC Patterns */
		set_crtc_test_pattern(link, pipe_ctx, test_pattern, test_pattern_color_space);
		pipe_ctx->stream_res.tg->funcs->unlock(pipe_ctx->stream_res.tg);
		pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg,
				CRTC_STATE_VACTIVE);
		pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg,
				CRTC_STATE_VBLANK);
		pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg,
				CRTC_STATE_VACTIVE);

		if (pipe_ctx->stream_res.tg->funcs->lock_doublebuffer_disable) {
			if (pipe_ctx->stream && should_use_dmub_lock(pipe_ctx->stream->link)) {
				union dmub_hw_lock_flags hw_locks = { 0 };
				struct dmub_hw_lock_inst_flags inst_flags = { 0 };

				hw_locks.bits.lock_dig = 1;
				inst_flags.dig_inst = pipe_ctx->stream_res.tg->inst;

				dmub_hw_lock_mgr_cmd(link->ctx->dmub_srv,
							false,
							&hw_locks,
							&inst_flags);
			} else
				pipe_ctx->stream_res.tg->funcs->lock_doublebuffer_disable(
						pipe_ctx->stream_res.tg);
		}

		/* Set Test Pattern state */
		link->test_pattern_enabled = true;
	}

	return true;
}

void dp_enable_mst_on_sink(struct dc_link *link, bool enable)
{
	unsigned char mstmCntl;

	core_link_read_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
	if (enable)
		mstmCntl |= DP_MST_EN;
	else
		mstmCntl &= (~DP_MST_EN);

	core_link_write_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
}

enum dc_status dp_set_fec_ready(struct dc_link *link, const struct link_resource *link_res, bool ready)
{
	/* FEC has to be "set ready" before the link training.
	 * The policy is to always train with FEC
	 * if the sink supports it and leave it enabled on link.
	 * If FEC is not supported, disable it.
	 */
	struct link_encoder *link_enc = NULL;
	enum dc_status status = DC_OK;
	uint8_t fec_config = 0;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (!dc_link_should_enable_fec(link))
		return status;

	if (link_enc->funcs->fec_set_ready &&
			link->dpcd_caps.fec_cap.bits.FEC_CAPABLE) {
		if (ready) {
			fec_config = 1;
			status = core_link_write_dpcd(link,
					DP_FEC_CONFIGURATION,
					&fec_config,
					sizeof(fec_config));
			if (status == DC_OK) {
				link_enc->funcs->fec_set_ready(link_enc, true);
				link->fec_state = dc_link_fec_ready;
			} else {
				link_enc->funcs->fec_set_ready(link_enc, false);
				link->fec_state = dc_link_fec_not_ready;
				dm_error("dpcd write failed to set fec_ready");
			}
		} else if (link->fec_state == dc_link_fec_ready) {
			fec_config = 0;
			status = core_link_write_dpcd(link,
					DP_FEC_CONFIGURATION,
					&fec_config,
					sizeof(fec_config));
			link_enc->funcs->fec_set_ready(link_enc, false);
			link->fec_state = dc_link_fec_not_ready;
		}
	}

	return status;
}

void dp_set_fec_enable(struct dc_link *link, bool enable)
{
	struct link_encoder *link_enc = NULL;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (!dc_link_should_enable_fec(link))
		return;

	if (link_enc->funcs->fec_set_enable &&
			link->dpcd_caps.fec_cap.bits.FEC_CAPABLE) {
		if (link->fec_state == dc_link_fec_ready && enable) {
			/* Accord to DP spec, FEC enable sequence can first
			 * be transmitted anytime after 1000 LL codes have
			 * been transmitted on the link after link training
			 * completion. Using 1 lane RBR should have the maximum
			 * time for transmitting 1000 LL codes which is 6.173 us.
			 * So use 7 microseconds delay instead.
			 */
			udelay(7);
			link_enc->funcs->fec_set_enable(link_enc, true);
			link->fec_state = dc_link_fec_enabled;
		} else if (link->fec_state == dc_link_fec_enabled && !enable) {
			link_enc->funcs->fec_set_enable(link_enc, false);
			link->fec_state = dc_link_fec_ready;
		}
	}
}

// TODO - DP2.0 Link: Fix get_lane_status to handle LTTPR offset (SST and MST)
static void get_lane_status(
	struct dc_link *link,
	uint32_t lane_count,
	union lane_status *status,
	union lane_align_status_updated *status_updated)
{
	unsigned int lane;
	uint8_t dpcd_buf[3] = {0};

	if (status == NULL || status_updated == NULL) {
		return;
	}

	core_link_read_dpcd(
			link,
			DP_LANE0_1_STATUS,
			dpcd_buf,
			sizeof(dpcd_buf));

	for (lane = 0; lane < lane_count; lane++) {
		status[lane].raw = dp_get_nibble_at_index(&dpcd_buf[0], lane);
	}

	status_updated->raw = dpcd_buf[2];
}

bool dpcd_write_128b_132b_sst_payload_allocation_table(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		struct link_mst_stream_allocation_table *proposed_table,
		bool allocate)
{
	const uint8_t vc_id = 1; /// VC ID always 1 for SST
	const uint8_t start_time_slot = 0; /// Always start at time slot 0 for SST
	bool result = false;
	uint8_t req_slot_count = 0;
	struct fixed31_32 avg_time_slots_per_mtp = { 0 };
	union payload_table_update_status update_status = { 0 };
	const uint32_t max_retries = 30;
	uint32_t retries = 0;

	if (allocate)	{
		avg_time_slots_per_mtp = calculate_sst_avg_time_slots_per_mtp(stream, link);
		req_slot_count = dc_fixpt_ceil(avg_time_slots_per_mtp);
		/// Validation should filter out modes that exceed link BW
		ASSERT(req_slot_count <= MAX_MTP_SLOT_COUNT);
		if (req_slot_count > MAX_MTP_SLOT_COUNT)
			return false;
	} else {
		/// Leave req_slot_count = 0 if allocate is false.
	}

	proposed_table->stream_count = 1; /// Always 1 stream for SST
	proposed_table->stream_allocations[0].slot_count = req_slot_count;
	proposed_table->stream_allocations[0].vcp_id = vc_id;

	if (link->aux_access_disabled)
		return true;

	/// Write DPCD 2C0 = 1 to start updating
	update_status.bits.VC_PAYLOAD_TABLE_UPDATED = 1;
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_TABLE_UPDATE_STATUS,
			&update_status.raw,
			1);

	/// Program the changes in DPCD 1C0 - 1C2
	ASSERT(vc_id == 1);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_SET,
			&vc_id,
			1);

	ASSERT(start_time_slot == 0);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_START_TIME_SLOT,
			&start_time_slot,
			1);

	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_TIME_SLOT_COUNT,
			&req_slot_count,
			1);

	/// Poll till DPCD 2C0 read 1
	/// Try for at least 150ms (30 retries, with 5ms delay after each attempt)

	while (retries < max_retries) {
		if (core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1) == DC_OK) {
			if (update_status.bits.VC_PAYLOAD_TABLE_UPDATED == 1) {
				DC_LOG_DP2("SST Update Payload: downstream payload table updated.");
				result = true;
				break;
			}
		} else {
			union dpcd_rev dpcdRev;

			if (core_link_read_dpcd(
					link,
					DP_DPCD_REV,
					&dpcdRev.raw,
					1) != DC_OK) {
				DC_LOG_ERROR("SST Update Payload: Unable to read DPCD revision "
						"of sink while polling payload table "
						"updated status bit.");
				break;
			}
		}
		retries++;
		msleep(5);
	}

	if (!result && retries == max_retries) {
		DC_LOG_ERROR("SST Update Payload: Payload table not updated after retries, "
				"continue on. Something is wrong with the branch.");
		// TODO - DP2.0 Payload: Read and log the payload table from downstream branch
	}

	return result;
}

bool dpcd_poll_for_allocation_change_trigger(struct dc_link *link)
{
	/*
	 * wait for ACT handled
	 */
	int i;
	const int act_retries = 30;
	enum act_return_status result = ACT_FAILED;
	union payload_table_update_status update_status = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated lane_status_updated;

	if (link->aux_access_disabled)
		return true;
	for (i = 0; i < act_retries; i++) {
		get_lane_status(link, link->cur_link_settings.lane_count, dpcd_lane_status, &lane_status_updated);

		if (!dp_is_cr_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_ch_eq_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_symbol_locked(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_interlane_aligned(lane_status_updated)) {
			DC_LOG_ERROR("SST Update Payload: Link loss occurred while "
					"polling for ACT handled.");
			result = ACT_LINK_LOST;
			break;
		}
		core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1);

		if (update_status.bits.ACT_HANDLED == 1) {
			DC_LOG_DP2("SST Update Payload: ACT handled by downstream.");
			result = ACT_SUCCESS;
			break;
		}

		msleep(5);
	}

	if (result == ACT_FAILED) {
		DC_LOG_ERROR("SST Update Payload: ACT still not handled after retries, "
				"continue on. Something is wrong with the branch.");
	}

	return (result == ACT_SUCCESS);
}

struct fixed31_32 calculate_sst_avg_time_slots_per_mtp(
		const struct dc_stream_state *stream,
		const struct dc_link *link)
{
	struct fixed31_32 link_bw_effective =
			dc_fixpt_from_int(
					dc_link_bandwidth_kbps(link, &link->cur_link_settings));
	struct fixed31_32 timeslot_bw_effective =
			dc_fixpt_div_int(link_bw_effective, MAX_MTP_SLOT_COUNT);
	struct fixed31_32 timing_bw =
			dc_fixpt_from_int(
					dc_bandwidth_in_kbps_from_timing(&stream->timing));
	struct fixed31_32 avg_time_slots_per_mtp =
			dc_fixpt_div(timing_bw, timeslot_bw_effective);

	return avg_time_slots_per_mtp;
}

void dc_link_clear_dprx_states(struct dc_link *link)
{
	memset(&link->dprx_states, 0, sizeof(link->dprx_states));
}

void dp_source_sequence_trace(struct dc_link *link, uint8_t dp_test_mode)
{
	if (link != NULL && link->dc->debug.enable_driver_sequence_debug)
		core_link_write_dpcd(link, DP_SOURCE_SEQUENCE,
					&dp_test_mode, sizeof(dp_test_mode));
}

void dp_retrain_link_dp_test(struct dc_link *link,
			struct dc_link_settings *link_setting,
			bool skip_video_pattern)
{
	struct pipe_ctx *pipe;
	unsigned int i;

	udelay(100);

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream != NULL &&
				pipe->stream->link == link &&
				!pipe->stream->dpms_off &&
				!pipe->top_pipe && !pipe->prev_odm_pipe) {
			core_link_disable_stream(pipe);
			pipe->link_config.dp_link_settings = *link_setting;
			update_dp_encoder_resources_for_test_harness(
					link->dc,
					pipe->stream->ctx->dc->current_state,
					pipe);
		}
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream != NULL &&
				pipe->stream->link == link &&
				!pipe->stream->dpms_off &&
				!pipe->top_pipe && !pipe->prev_odm_pipe) {
			core_link_enable_stream(
					pipe->stream->ctx->dc->current_state,
					pipe);
		}
	}
}

#undef DC_LOGGER
#define DC_LOGGER \
	dsc->ctx->logger
static void dsc_optc_config_log(struct display_stream_compressor *dsc,
		struct dsc_optc_config *config)
{
	uint32_t precision = 1 << 28;
	uint32_t bytes_per_pixel_int = config->bytes_per_pixel / precision;
	uint32_t bytes_per_pixel_mod = config->bytes_per_pixel % precision;
	uint64_t ll_bytes_per_pix_fraq = bytes_per_pixel_mod;

	/* 7 fractional digits decimal precision for bytes per pixel is enough because DSC
	 * bits per pixel precision is 1/16th of a pixel, which means bytes per pixel precision is
	 * 1/16/8 = 1/128 of a byte, or 0.0078125 decimal
	 */
	ll_bytes_per_pix_fraq *= 10000000;
	ll_bytes_per_pix_fraq /= precision;

	DC_LOG_DSC("\tbytes_per_pixel 0x%08x (%d.%07d)",
			config->bytes_per_pixel, bytes_per_pixel_int, (uint32_t)ll_bytes_per_pix_fraq);
	DC_LOG_DSC("\tis_pixel_format_444 %d", config->is_pixel_format_444);
	DC_LOG_DSC("\tslice_width %d", config->slice_width);
}

bool dp_set_dsc_on_rx(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	bool result = false;

	if (dc_is_virtual_signal(stream->signal) || IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		result = true;
	else
		result = dm_helpers_dp_write_dsc_enable(dc->ctx, stream, enable);
	return result;
}

/* The stream with these settings can be sent (unblanked) only after DSC was enabled on RX first,
 * i.e. after dp_enable_dsc_on_rx() had been called
 */
void dp_set_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg;
		enum optc_dsc_mode optc_dsc_mode;

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in encoder */
		if (dc_is_dp_signal(stream->signal) && !IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)
				&& !link_is_dp_128b_132b_signal(pipe_ctx)) {
			DC_LOG_DSC("Setting stream encoder DSC config for engine %d:", (int)pipe_ctx->stream_res.stream_enc->id);
			dsc_optc_config_log(dsc, &dsc_optc_cfg);
			pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(pipe_ctx->stream_res.stream_enc,
									optc_dsc_mode,
									dsc_optc_cfg.bytes_per_pixel,
									dsc_optc_cfg.slice_width);

			/* PPS SDP is set elsewhere because it has to be done after DIG FE is connected to DIG BE */
		}

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		dsc_optc_config_log(dsc, &dsc_optc_cfg);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC in stream encoder */
		if (dc_is_dp_signal(stream->signal)) {
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(
						pipe_ctx->stream_res.stream_enc,
						OPTC_DSC_DISABLED, 0, 0);
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
							pipe_ctx->stream_res.stream_enc, false, NULL, true);
			}
		}

		/* disable DSC block */
		pipe_ctx->stream_res.dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
	}
}

bool dp_set_dsc_enable(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	bool result = false;

	if (!pipe_ctx->stream->timing.flags.DSC)
		goto out;
	if (!dsc)
		goto out;

	if (enable) {
		{
			dp_set_dsc_on_stream(pipe_ctx, true);
			result = true;
		}
	} else {
		dp_set_dsc_on_rx(pipe_ctx, false);
		dp_set_dsc_on_stream(pipe_ctx, false);
		result = true;
	}
out:
	return result;
}

/*
 * For dynamic bpp change case, dsc is programmed with MASTER_UPDATE_LOCK enabled;
 * hence PPS info packet update need to use frame update instead of immediate update.
 * Added parameter immediate_update for this purpose.
 * The decision to use frame update is hard-coded in function dp_update_dsc_config(),
 * which is the only place where a "false" would be passed in for param immediate_update.
 *
 * immediate_update is only applicable when DSC is enabled.
 */
bool dp_set_dsc_pps_sdp(struct pipe_ctx *pipe_ctx, bool enable, bool immediate_update)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (!pipe_ctx->stream->timing.flags.DSC || !dsc)
		return false;

	if (enable) {
		struct dsc_config dsc_cfg;
		uint8_t dsc_packed_pps[128];

		memset(&dsc_cfg, 0, sizeof(dsc_cfg));
		memset(dsc_packed_pps, 0, 128);

		/* Enable DSC hw block */
		dsc_cfg.pic_width = stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;

		dsc->funcs->dsc_get_packed_pps(dsc, &dsc_cfg, &dsc_packed_pps[0]);
		memcpy(&stream->dsc_packed_pps[0], &dsc_packed_pps[0], sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			DC_LOG_DSC("Setting stream encoder DSC PPS SDP for engine %d\n", (int)pipe_ctx->stream_res.stream_enc->id);
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										true,
										&dsc_packed_pps[0],
										immediate_update);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc,
						true,
						&dsc_packed_pps[0],
						immediate_update);
		}
	} else {
		/* disable DSC PPS in stream encoder */
		memset(&stream->dsc_packed_pps[0], 0, sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			if (link_is_dp_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc, false, NULL, true);
		}
	}

	return true;
}


bool dp_update_dsc_config(struct pipe_ctx *pipe_ctx)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;

	if (!pipe_ctx->stream->timing.flags.DSC)
		return false;
	if (!dsc)
		return false;

	dp_set_dsc_on_stream(pipe_ctx, true);
	dp_set_dsc_pps_sdp(pipe_ctx, true, false);
	return true;
}

#undef DC_LOGGER
#define DC_LOGGER \
	link->ctx->logger
