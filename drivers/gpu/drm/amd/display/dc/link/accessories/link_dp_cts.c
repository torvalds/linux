/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 *
 */
#include "link_dp_cts.h"
#include "link/link_resource.h"
#include "link/protocols/link_dpcd.h"
#include "link/protocols/link_dp_training.h"
#include "link/protocols/link_dp_phy.h"
#include "link/protocols/link_dp_training_fixed_vs_pe_retimer.h"
#include "link/protocols/link_dp_capability.h"
#include "link/link_dpms.h"
#include "resource.h"
#include "dm_helpers.h"
#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"

#define DC_LOGGER \
	link->ctx->logger

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
	case DP_TEST_LINK_RATE_UHBR13_5_LEGACY:
	case DP_TEST_LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR13_5;
	default:
		return LINK_RATE_UNKNOWN;
	}
}

static void dp_retrain_link_dp_test(struct dc_link *link,
			struct dc_link_settings *link_setting,
			bool skip_video_pattern)
{
	struct pipe_ctx *pipes[MAX_PIPES];
	struct dc_state *state = link->dc->current_state;
	uint8_t count;
	int i;

	udelay(100);

	link_get_master_pipes_with_dpms_on(link, state, &count, pipes);

	for (i = 0; i < count; i++) {
		link_set_dpms_off(pipes[i]);
		pipes[i]->link_config.dp_link_settings = *link_setting;
		update_dp_encoder_resources_for_test_harness(
				link->dc,
				state,
				pipes[i]);
	}

	for (i = count-1; i >= 0; i--)
		link_set_dpms_on(state, pipes[i]);
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

	if (link_settings.link_rate == LINK_RATE_UNKNOWN) {
		DC_LOG_ERROR("%s: Invalid test link rate.", __func__);
		ASSERT(0);
	}

	/* Set preferred link settings */
	link->verified_link_cap.lane_count = link_settings.lane_count;
	link->verified_link_cap.link_rate = link_settings.link_rate;

	dp_retrain_link_dp_test(link, &link_settings, false);
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

	link_training_settings.lttpr_mode = dp_decide_lttpr_mode(link, &link->cur_link_settings);

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

	if (IS_DP_PHY_SQUARE_PATTERN(test_pattern)) {
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
	dp_set_test_pattern(
		link,
		test_pattern,
		DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED,
		&link_training_settings,
		test_pattern_buffer,
		test_pattern_size);
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
	struct pipe_ctx *odm_pipe;
	struct test_pattern_params *tp_params;

	memset(&params, 0, sizeof(params));

	resource_build_test_pattern_params(&link->dc->current_state->res_ctx,
			pipe_ctx);
	controller_test_pattern = pipe_ctx->stream_res.test_pattern_params.test_pattern;

	switch (test_pattern) {
	case DP_TEST_PATTERN_COLOR_SQUARES:
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
	case DP_TEST_PATTERN_VERTICAL_BARS:
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
	case DP_TEST_PATTERN_COLOR_RAMP:
	{
		/* disable bit depth reduction */
		pipe_ctx->stream->bit_depth_params = params;
		if (pipe_ctx->stream_res.tg->funcs->set_test_pattern) {
			opp->funcs->opp_program_bit_depth_reduction(opp, &params);
			pipe_ctx->stream_res.tg->funcs->set_test_pattern(pipe_ctx->stream_res.tg,
				controller_test_pattern, color_depth);
		} else if (link->dc->hwss.set_disp_pattern_generator) {
			enum controller_dp_color_space controller_color_space;
			struct output_pixel_processor *odm_opp;

			controller_color_space = pipe_ctx->stream_res.test_pattern_params.color_space;

			if (controller_color_space == CONTROLLER_DP_COLOR_SPACE_UDEFINED) {
				DC_LOG_ERROR("%s: Color space must be defined for test pattern", __func__);
				ASSERT(0);
			}

			odm_pipe = pipe_ctx;
			while (odm_pipe) {
				tp_params = &odm_pipe->stream_res.test_pattern_params;
				odm_opp = odm_pipe->stream_res.opp;
				odm_opp->funcs->opp_program_bit_depth_reduction(odm_opp, &params);
				link->dc->hwss.set_disp_pattern_generator(link->dc,
						odm_pipe,
						tp_params->test_pattern,
						tp_params->color_space,
						tp_params->color_depth,
						NULL,
						tp_params->width,
						tp_params->height,
						tp_params->offset);
				odm_pipe = odm_pipe->next_odm_pipe;
			}
		}
	}
	break;
	case DP_TEST_PATTERN_VIDEO_MODE:
	{
		/* restore bitdepth reduction */
		resource_build_bit_depth_reduction_params(pipe_ctx->stream, &params);
		pipe_ctx->stream->bit_depth_params = params;
		if (pipe_ctx->stream_res.tg->funcs->set_test_pattern) {
			opp->funcs->opp_program_bit_depth_reduction(opp, &params);
			pipe_ctx->stream_res.tg->funcs->set_test_pattern(pipe_ctx->stream_res.tg,
					CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
					color_depth);
		} else if (link->dc->hwss.set_disp_pattern_generator) {
			struct output_pixel_processor *odm_opp;

			odm_pipe = pipe_ctx;
			while (odm_pipe) {
				tp_params = &odm_pipe->stream_res.test_pattern_params;
				odm_opp = odm_pipe->stream_res.opp;
				odm_opp->funcs->opp_program_bit_depth_reduction(odm_opp, &params);
				link->dc->hwss.set_disp_pattern_generator(link->dc,
						odm_pipe,
						tp_params->test_pattern,
						tp_params->color_space,
						tp_params->color_depth,
						NULL,
						tp_params->width,
						tp_params->height,
						tp_params->offset);
				odm_pipe = odm_pipe->next_odm_pipe;
			}
		}
	}
	break;

	default:
	break;
	}
}

void dp_handle_automated_test(struct dc_link *link)
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

bool dp_set_test_pattern(
	struct dc_link *link,
	enum dp_test_pattern test_pattern,
	enum dp_test_pattern_color_space test_pattern_color_space,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size)
{
	const struct link_hwss *link_hwss;
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

		if (resource_is_pipe_type(&pipes[i], OTG_MASTER) &&
				pipes[i].stream->link == link) {
			pipe_ctx = &pipes[i];
			break;
		}
	}

	if (pipe_ctx == NULL)
		return false;

	link->pending_test_pattern = test_pattern;

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
		link->current_test_pattern = test_pattern;
		link->pending_test_pattern = DP_TEST_PATTERN_UNSUPPORTED;

		return true;
	}

	/* Check for PHY Test Patterns */
	if (IS_DP_PHY_PATTERN(test_pattern)) {
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
			link->current_test_pattern = test_pattern;
			link->pending_test_pattern = DP_TEST_PATTERN_UNSUPPORTED;
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
			if (IS_DP_PHY_SQUARE_PATTERN(test_pattern))
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
		link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
		pipe_ctx->stream->output_color_space = color_space;
		link_hwss->setup_stream_attribute(pipe_ctx);

		if (pipe_ctx->stream->use_vsc_sdp_for_colorimetry) {
			if (test_pattern == DP_TEST_PATTERN_COLOR_SQUARES_CEA)
				pipe_ctx->stream->vsc_infopacket.sb[17] |= (1 << 7); // sb17 bit 7 Dynamic Range: 0 = VESA range, 1 = CTA range
			else
				pipe_ctx->stream->vsc_infopacket.sb[17] &= ~(1 << 7);

			if (color_space == COLOR_SPACE_YCBCR601_LIMITED)
				pipe_ctx->stream->vsc_infopacket.sb[16] &= 0xf0;
			else if (color_space == COLOR_SPACE_YCBCR709_LIMITED)
				pipe_ctx->stream->vsc_infopacket.sb[16] |= 1;

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
		link->current_test_pattern = test_pattern;
		link->pending_test_pattern = DP_TEST_PATTERN_UNSUPPORTED;
	}

	return true;
}

void dp_set_preferred_link_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link *link)
{
	int i;
	struct pipe_ctx *pipe;
	struct dc_stream_state *link_stream;
	struct dc_link_settings store_settings = *link_setting;

	link->preferred_link_setting = store_settings;

	/* Retrain with preferred link settings only relevant for
	 * DP signal type
	 * Check for non-DP signal or if passive dongle present
	 */
	if (!dc_is_dp_signal(link->connector_signal) ||
		link->dongle_max_pix_clk > 0)
		return;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream && pipe->stream->link) {
			if (pipe->stream->link == link) {
				link_stream = pipe->stream;
				break;
			}
		}
	}

	/* Stream not found */
	if (i == MAX_PIPES)
		return;

	/* Cannot retrain link if backend is off */
	if (link_stream->dpms_off)
		return;

	if (link_decide_link_settings(link_stream, &store_settings))
		dp_retrain_link_dp_test(link, &store_settings, false);
}

void dp_set_preferred_training_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link_training_overrides *lt_overrides,
		struct dc_link *link,
		bool skip_immediate_retrain)
{
	if (lt_overrides != NULL)
		link->preferred_training_settings = *lt_overrides;
	else
		memset(&link->preferred_training_settings, 0, sizeof(link->preferred_training_settings));

	if (link_setting != NULL) {
		link->preferred_link_setting = *link_setting;
	} else {
		link->preferred_link_setting.lane_count = LANE_COUNT_UNKNOWN;
		link->preferred_link_setting.link_rate = LINK_RATE_UNKNOWN;
	}

	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT &&
			link->type == dc_connection_mst_branch)
		dm_helpers_dp_mst_update_branch_bandwidth(dc->ctx, link);

	/* Retrain now, or wait until next stream update to apply */
	if (skip_immediate_retrain == false)
		dp_set_preferred_link_settings(dc, &link->preferred_link_setting, link);
}
