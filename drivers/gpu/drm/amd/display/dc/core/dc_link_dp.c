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
#include "link/link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"

#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "link/link_dp_dpia.h"
#include "inc/link_enc_cfg.h"
#include "clk_mgr.h"
#include "link/link_dp_trace.h"
#include "link/link_dp_training.h"
#include "link/link_dp_training_fixed_vs_pe_retimer.h"
#include "link/link_dp_training_dpia.h"
#include "link/link_dp_training_auxless.h"
#include "link/link_dp_phy.h"
#include "link/link_dp_capability.h"
#define DC_LOGGER \
	link->ctx->logger

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */
#include "link/link_dpcd.h"

static uint8_t get_nibble_at_index(const uint8_t *buf,
	uint32_t index)
{
	uint8_t nibble;
	nibble = buf[index / 2];

	if (index % 2)
		nibble >>= 4;
	else
		nibble &= 0x0F;

	return nibble;
}

enum dc_status read_hpd_rx_irq_data(
	struct dc_link *link,
	union hpd_irq_data *irq_data)
{
	static enum dc_status retval;

	/* The HW reads 16 bytes from 200h on HPD,
	 * but if we get an AUX_DEFER, the HW cannot retry
	 * and this causes the CTS tests 4.3.2.1 - 3.2.4 to
	 * fail, so we now explicitly read 6 bytes which is
	 * the req from the above mentioned test cases.
	 *
	 * For DP 1.4 we need to read those from 2002h range.
	 */
	if (link->dpcd_caps.dpcd_rev.raw < DPCD_REV_14)
		retval = core_link_read_dpcd(
			link,
			DP_SINK_COUNT,
			irq_data->raw,
			sizeof(union hpd_irq_data));
	else {
		/* Read 14 bytes in a single read and then copy only the required fields.
		 * This is more efficient than doing it in two separate AUX reads. */

		uint8_t tmp[DP_SINK_STATUS_ESI - DP_SINK_COUNT_ESI + 1];

		retval = core_link_read_dpcd(
			link,
			DP_SINK_COUNT_ESI,
			tmp,
			sizeof(tmp));

		if (retval != DC_OK)
			return retval;

		irq_data->bytes.sink_cnt.raw = tmp[DP_SINK_COUNT_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.device_service_irq.raw = tmp[DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0 - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane01_status.raw = tmp[DP_LANE0_1_STATUS_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane23_status.raw = tmp[DP_LANE2_3_STATUS_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane_status_updated.raw = tmp[DP_LANE_ALIGN_STATUS_UPDATED_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.sink_status.raw = tmp[DP_SINK_STATUS_ESI - DP_SINK_COUNT_ESI];
	}

	return retval;
}

bool hpd_rx_irq_check_link_loss_status(
	struct dc_link *link,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	uint8_t irq_reg_rx_power_state = 0;
	enum dc_status dpcd_result = DC_ERROR_UNEXPECTED;
	union lane_status lane_status;
	uint32_t lane;
	bool sink_status_changed;
	bool return_code;

	sink_status_changed = false;
	return_code = false;

	if (link->cur_link_settings.lane_count == 0)
		return return_code;

	/*1. Check that Link Status changed, before re-training.*/

	/*parse lane status*/
	for (lane = 0; lane < link->cur_link_settings.lane_count; lane++) {
		/* check status of lanes 0,1
		 * changed DpcdAddress_Lane01Status (0x202)
		 */
		lane_status.raw = get_nibble_at_index(
			&hpd_irq_dpcd_data->bytes.lane01_status.raw,
			lane);

		if (!lane_status.bits.CHANNEL_EQ_DONE_0 ||
			!lane_status.bits.CR_DONE_0 ||
			!lane_status.bits.SYMBOL_LOCKED_0) {
			/* if one of the channel equalization, clock
			 * recovery or symbol lock is dropped
			 * consider it as (link has been
			 * dropped) dp sink status has changed
			 */
			sink_status_changed = true;
			break;
		}
	}

	/* Check interlane align.*/
	if (sink_status_changed ||
		!hpd_irq_dpcd_data->bytes.lane_status_updated.bits.INTERLANE_ALIGN_DONE) {

		DC_LOG_HW_HPD_IRQ("%s: Link Status changed.\n", __func__);

		return_code = true;

		/*2. Check that we can handle interrupt: Not in FS DOS,
		 *  Not in "Display Timeout" state, Link is trained.
		 */
		dpcd_result = core_link_read_dpcd(link,
			DP_SET_POWER,
			&irq_reg_rx_power_state,
			sizeof(irq_reg_rx_power_state));

		if (dpcd_result != DC_OK) {
			DC_LOG_HW_HPD_IRQ("%s: DPCD read failed to obtain power state.\n",
				__func__);
		} else {
			if (irq_reg_rx_power_state != DP_SET_POWER_D0)
				return_code = false;
		}
	}

	return return_code;
}

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

/*************************Short Pulse IRQ***************************/
bool dc_link_dp_allow_hpd_rx_irq(const struct dc_link *link)
{
	/*
	 * Don't handle RX IRQ unless one of following is met:
	 * 1) The link is established (cur_link_settings != unknown)
	 * 2) We know we're dealing with a branch device, SST or MST
	 */

	if ((link->cur_link_settings.lane_count != LANE_COUNT_UNKNOWN) ||
		is_dp_branch_device(link))
		return true;

	return false;
}

static bool handle_hpd_irq_psr_sink(struct dc_link *link)
{
	union dpcd_psr_configuration psr_configuration;

	if (!link->psr_settings.psr_feature_enabled)
		return false;

	dm_helpers_dp_read_dpcd(
		link->ctx,
		link,
		368,/*DpcdAddress_PSR_Enable_Cfg*/
		&psr_configuration.raw,
		sizeof(psr_configuration.raw));

	if (psr_configuration.bits.ENABLE) {
		unsigned char dpcdbuf[3] = {0};
		union psr_error_status psr_error_status;
		union psr_sink_psr_status psr_sink_psr_status;

		dm_helpers_dp_read_dpcd(
			link->ctx,
			link,
			0x2006, /*DpcdAddress_PSR_Error_Status*/
			(unsigned char *) dpcdbuf,
			sizeof(dpcdbuf));

		/*DPCD 2006h   ERROR STATUS*/
		psr_error_status.raw = dpcdbuf[0];
		/*DPCD 2008h   SINK PANEL SELF REFRESH STATUS*/
		psr_sink_psr_status.raw = dpcdbuf[2];

		if (psr_error_status.bits.LINK_CRC_ERROR ||
				psr_error_status.bits.RFB_STORAGE_ERROR ||
				psr_error_status.bits.VSC_SDP_ERROR) {
			bool allow_active;

			/* Acknowledge and clear error bits */
			dm_helpers_dp_write_dpcd(
				link->ctx,
				link,
				8198,/*DpcdAddress_PSR_Error_Status*/
				&psr_error_status.raw,
				sizeof(psr_error_status.raw));

			/* PSR error, disable and re-enable PSR */
			if (link->psr_settings.psr_allow_active) {
				allow_active = false;
				dc_link_set_psr_allow_active(link, &allow_active, true, false, NULL);
				allow_active = true;
				dc_link_set_psr_allow_active(link, &allow_active, true, false, NULL);
			}

			return true;
		} else if (psr_sink_psr_status.bits.SINK_SELF_REFRESH_STATUS ==
				PSR_SINK_STATE_ACTIVE_DISPLAY_FROM_SINK_RFB){
			/* No error is detect, PSR is active.
			 * We should return with IRQ_HPD handled without
			 * checking for loss of sync since PSR would have
			 * powered down main link.
			 */
			return true;
		}
	}
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
			get_nibble_at_index(&dpcd_lane_adjustment[0].raw, lane);
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

static void dp_test_send_link_test_pattern(struct dc_link *link)
{
	union link_test_pattern dpcd_test_pattern;
	union test_misc dpcd_test_params;
	enum dp_test_pattern test_pattern;
	enum dp_test_pattern_color_space test_pattern_color_space =
			DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED;
	enum dc_color_depth requestColorDepth = COLOR_DEPTH_UNDEFINED;
	struct pipe_ctx *pipes = link->dc->current_state->res_ctx.pipe_ctx;
	struct pipe_ctx *pipe_ctx = NULL;
	int i;

	memset(&dpcd_test_pattern, 0, sizeof(dpcd_test_pattern));
	memset(&dpcd_test_params, 0, sizeof(dpcd_test_params));

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream == NULL)
			continue;

		if (pipes[i].stream->link == link && !pipes[i].top_pipe && !pipes[i].prev_odm_pipe) {
			pipe_ctx = &pipes[i];
			break;
		}
	}

	if (pipe_ctx == NULL)
		return;

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

	switch (dpcd_test_pattern.bits.PATTERN) {
	case LINK_TEST_PATTERN_COLOR_RAMP:
		test_pattern = DP_TEST_PATTERN_COLOR_RAMP;
	break;
	case LINK_TEST_PATTERN_VERTICAL_BARS:
		test_pattern = DP_TEST_PATTERN_VERTICAL_BARS;
	break; /* black and white */
	case LINK_TEST_PATTERN_COLOR_SQUARES:
		test_pattern = (dpcd_test_params.bits.DYN_RANGE ==
				TEST_DYN_RANGE_VESA ?
				DP_TEST_PATTERN_COLOR_SQUARES :
				DP_TEST_PATTERN_COLOR_SQUARES_CEA);
	break;
	default:
		test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
	break;
	}

	if (dpcd_test_params.bits.CLR_FORMAT == 0)
		test_pattern_color_space = DP_TEST_PATTERN_COLOR_SPACE_RGB;
	else
		test_pattern_color_space = dpcd_test_params.bits.YCBCR_COEFS ?
				DP_TEST_PATTERN_COLOR_SPACE_YCBCR709 :
				DP_TEST_PATTERN_COLOR_SPACE_YCBCR601;

	switch (dpcd_test_params.bits.BPC) {
	case 0: // 6 bits
		requestColorDepth = COLOR_DEPTH_666;
		break;
	case 1: // 8 bits
		requestColorDepth = COLOR_DEPTH_888;
		break;
	case 2: // 10 bits
		requestColorDepth = COLOR_DEPTH_101010;
		break;
	case 3: // 12 bits
		requestColorDepth = COLOR_DEPTH_121212;
		break;
	default:
		break;
	}

	switch (dpcd_test_params.bits.CLR_FORMAT) {
	case 0:
		pipe_ctx->stream->timing.pixel_encoding = PIXEL_ENCODING_RGB;
		break;
	case 1:
		pipe_ctx->stream->timing.pixel_encoding = PIXEL_ENCODING_YCBCR422;
		break;
	case 2:
		pipe_ctx->stream->timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
		break;
	default:
		pipe_ctx->stream->timing.pixel_encoding = PIXEL_ENCODING_RGB;
		break;
	}


	if (requestColorDepth != COLOR_DEPTH_UNDEFINED
			&& pipe_ctx->stream->timing.display_color_depth != requestColorDepth) {
		DC_LOG_DEBUG("%s: original bpc %d, changing to %d\n",
				__func__,
				pipe_ctx->stream->timing.display_color_depth,
				requestColorDepth);
		pipe_ctx->stream->timing.display_color_depth = requestColorDepth;
	}

	dp_update_dsc_config(pipe_ctx);

	dc_link_dp_set_test_pattern(
			link,
			test_pattern,
			test_pattern_color_space,
			NULL,
			NULL,
			0);
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
		dp_test_send_link_test_pattern(link);
		test_response.bits.ACK = 1;
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

void dc_link_dp_handle_link_loss(struct dc_link *link)
{
	int i;
	struct pipe_ctx *pipe_ctx;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx && pipe_ctx->stream && pipe_ctx->stream->link == link)
			break;
	}

	if (pipe_ctx == NULL || pipe_ctx->stream == NULL)
		return;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx && pipe_ctx->stream && !pipe_ctx->stream->dpms_off &&
				pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe)
			core_link_disable_stream(pipe_ctx);
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx && pipe_ctx->stream && !pipe_ctx->stream->dpms_off
				&& pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe) {
			// Always use max settings here for DP 1.4a LL Compliance CTS
			if (link->is_automated) {
				pipe_ctx->link_config.dp_link_settings.lane_count =
						link->verified_link_cap.lane_count;
				pipe_ctx->link_config.dp_link_settings.link_rate =
						link->verified_link_cap.link_rate;
				pipe_ctx->link_config.dp_link_settings.link_spread =
						link->verified_link_cap.link_spread;
			}
			core_link_enable_stream(link->dc->current_state, pipe_ctx);
		}
	}
}

bool dc_link_handle_hpd_rx_irq(struct dc_link *link, union hpd_irq_data *out_hpd_irq_dpcd_data, bool *out_link_loss,
							bool defer_handling, bool *has_left_work)
{
	union hpd_irq_data hpd_irq_dpcd_data = {0};
	union device_service_irq device_service_clear = {0};
	enum dc_status result;
	bool status = false;

	if (out_link_loss)
		*out_link_loss = false;

	if (has_left_work)
		*has_left_work = false;
	/* For use cases related to down stream connection status change,
	 * PSR and device auto test, refer to function handle_sst_hpd_irq
	 * in DAL2.1*/

	DC_LOG_HW_HPD_IRQ("%s: Got short pulse HPD on link %d\n",
		__func__, link->link_index);


	 /* All the "handle_hpd_irq_xxx()" methods
		 * should be called only after
		 * dal_dpsst_ls_read_hpd_irq_data
		 * Order of calls is important too
		 */
	result = read_hpd_rx_irq_data(link, &hpd_irq_dpcd_data);
	if (out_hpd_irq_dpcd_data)
		*out_hpd_irq_dpcd_data = hpd_irq_dpcd_data;

	if (result != DC_OK) {
		DC_LOG_HW_HPD_IRQ("%s: DPCD read failed to obtain irq data\n",
			__func__);
		return false;
	}

	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		// Workaround for DP 1.4a LL Compliance CTS as USB4 has to share encoders unlike DP and USBC
		link->is_automated = true;
		device_service_clear.bits.AUTOMATED_TEST = 1;
		core_link_write_dpcd(
			link,
			DP_DEVICE_SERVICE_IRQ_VECTOR,
			&device_service_clear.raw,
			sizeof(device_service_clear.raw));
		device_service_clear.raw = 0;
		if (defer_handling && has_left_work)
			*has_left_work = true;
		else
			dc_link_dp_handle_automated_test(link);
		return false;
	}

	if (!dc_link_dp_allow_hpd_rx_irq(link)) {
		DC_LOG_HW_HPD_IRQ("%s: skipping HPD handling on %d\n",
			__func__, link->link_index);
		return false;
	}

	if (handle_hpd_irq_psr_sink(link))
		/* PSR-related error was detected and handled */
		return true;

	/* If PSR-related error handled, Main link may be off,
	 * so do not handle as a normal sink status change interrupt.
	 */

	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY) {
		if (defer_handling && has_left_work)
			*has_left_work = true;
		return true;
	}

	/* check if we have MST msg and return since we poll for it */
	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
		if (defer_handling && has_left_work)
			*has_left_work = true;
		return false;
	}

	/* For now we only handle 'Downstream port status' case.
	 * If we got sink count changed it means
	 * Downstream port status changed,
	 * then DM should call DC to do the detection.
	 * NOTE: Do not handle link loss on eDP since it is internal link*/
	if ((link->connector_signal != SIGNAL_TYPE_EDP) &&
		hpd_rx_irq_check_link_loss_status(
			link,
			&hpd_irq_dpcd_data)) {
		/* Connectivity log: link loss */
		CONN_DATA_LINK_LOSS(link,
					hpd_irq_dpcd_data.raw,
					sizeof(hpd_irq_dpcd_data),
					"Status: ");

		if (defer_handling && has_left_work)
			*has_left_work = true;
		else
			dc_link_dp_handle_link_loss(link);

		status = false;
		if (out_link_loss)
			*out_link_loss = true;

		dp_trace_link_loss_increment(link);
	}

	if (link->type == dc_connection_sst_branch &&
		hpd_irq_dpcd_data.bytes.sink_cnt.bits.SINK_COUNT
			!= link->dpcd_sink_count)
		status = true;

	/* reasons for HPD RX:
	 * 1. Link Loss - ie Re-train the Link
	 * 2. MST sideband message
	 * 3. Automated Test - ie. Internal Commit
	 * 4. CP (copy protection) - (not interesting for DM???)
	 * 5. DRR
	 * 6. Downstream Port status changed
	 * -ie. Detect - this the only one
	 * which is interesting for DM because
	 * it must call dc_link_detect.
	 */
	return status;
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

void dp_set_panel_mode(struct dc_link *link, enum dp_panel_mode panel_mode)
{
	union dpcd_edp_config edp_config_set;
	bool panel_mode_edp = false;

	memset(&edp_config_set, '\0', sizeof(union dpcd_edp_config));

	if (panel_mode != DP_PANEL_MODE_DEFAULT) {

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
		case DP_PANEL_MODE_SPECIAL:
			panel_mode_edp = true;
			break;

		default:
				break;
		}

		/*set edp panel mode in receiver*/
		core_link_read_dpcd(
			link,
			DP_EDP_CONFIGURATION_SET,
			&edp_config_set.raw,
			sizeof(edp_config_set.raw));

		if (edp_config_set.bits.PANEL_MODE_EDP
			!= panel_mode_edp) {
			enum dc_status result;

			edp_config_set.bits.PANEL_MODE_EDP =
			panel_mode_edp;
			result = core_link_write_dpcd(
				link,
				DP_EDP_CONFIGURATION_SET,
				&edp_config_set.raw,
				sizeof(edp_config_set.raw));

			ASSERT(result == DC_OK);
		}
	}
	DC_LOG_DETECTION_DP_CAPS("Link: %d eDP panel mode supported: %d "
		 "eDP panel mode enabled: %d \n",
		 link->link_index,
		 link->dpcd_caps.panel_mode_edp,
		 panel_mode_edp);
}

enum dp_panel_mode dp_get_panel_mode(struct dc_link *link)
{
	/* We need to explicitly check that connector
	 * is not DP. Some Travis_VGA get reported
	 * by video bios as DP.
	 */
	if (link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT) {

		switch (link->dpcd_caps.branch_dev_id) {
		case DP_BRANCH_DEVICE_ID_0022B9:
			/* alternate scrambler reset is required for Travis
			 * for the case when external chip does not
			 * provide sink device id, alternate scrambler
			 * scheme will  be overriden later by querying
			 * Encoder features
			 */
			if (strncmp(
				link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_2,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
					return DP_PANEL_MODE_SPECIAL;
			}
			break;
		case DP_BRANCH_DEVICE_ID_00001A:
			/* alternate scrambler reset is required for Travis
			 * for the case when external chip does not provide
			 * sink device id, alternate scrambler scheme will
			 * be overriden later by querying Encoder feature
			 */
			if (strncmp(link->dpcd_caps.branch_dev_name,
				DP_VGA_LVDS_CONVERTER_ID_3,
				sizeof(
				link->dpcd_caps.
				branch_dev_name)) == 0) {
					return DP_PANEL_MODE_SPECIAL;
			}
			break;
		default:
			break;
		}
	}

	if (link->dpcd_caps.panel_mode_edp &&
		(link->connector_signal == SIGNAL_TYPE_EDP ||
		 (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT &&
		  link->is_internal_display))) {
		return DP_PANEL_MODE_EDP;
	}

	return DP_PANEL_MODE_DEFAULT;
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

bool dc_link_set_backlight_level_nits(struct dc_link *link,
		bool isHDR,
		uint32_t backlight_millinits,
		uint32_t transition_time_in_ms)
{
	struct dpcd_source_backlight_set dpcd_backlight_set;
	uint8_t backlight_control = isHDR ? 1 : 0;

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
			link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	// OLEDs have no PWM, they can only use AUX
	if (link->dpcd_sink_ext_caps.bits.oled == 1)
		backlight_control = 1;

	*(uint32_t *)&dpcd_backlight_set.backlight_level_millinits = backlight_millinits;
	*(uint16_t *)&dpcd_backlight_set.backlight_transition_time_ms = (uint16_t)transition_time_in_ms;


	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_LEVEL,
			(uint8_t *)(&dpcd_backlight_set),
			sizeof(dpcd_backlight_set)) != DC_OK)
		return false;

	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_CONTROL,
			&backlight_control, 1) != DC_OK)
		return false;

	return true;
}

bool dc_link_get_backlight_level_nits(struct dc_link *link,
		uint32_t *backlight_millinits_avg,
		uint32_t *backlight_millinits_peak)
{
	union dpcd_source_backlight_get dpcd_backlight_get;

	memset(&dpcd_backlight_get, 0, sizeof(union dpcd_source_backlight_get));

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
			link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (!core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_CURRENT_PEAK,
			dpcd_backlight_get.raw,
			sizeof(union dpcd_source_backlight_get)))
		return false;

	*backlight_millinits_avg =
		dpcd_backlight_get.bytes.backlight_millinits_avg;
	*backlight_millinits_peak =
		dpcd_backlight_get.bytes.backlight_millinits_peak;

	/* On non-supported panels dpcd_read usually succeeds with 0 returned */
	if (*backlight_millinits_avg == 0 ||
			*backlight_millinits_avg > *backlight_millinits_peak)
		return false;

	return true;
}

bool dc_link_backlight_enable_aux(struct dc_link *link, bool enable)
{
	uint8_t backlight_enable = enable ? 1 : 0;

	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
		link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (core_link_write_dpcd(link, DP_SOURCE_BACKLIGHT_ENABLE,
		&backlight_enable, 1) != DC_OK)
		return false;

	return true;
}

// we read default from 0x320 because we expect BIOS wrote it there
// regular get_backlight_nit reads from panel set at 0x326
bool dc_link_read_default_bl_aux(struct dc_link *link, uint32_t *backlight_millinits)
{
	if (!link || (link->connector_signal != SIGNAL_TYPE_EDP &&
		link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT))
		return false;

	if (!core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_LEVEL,
		(uint8_t *) backlight_millinits,
		sizeof(uint32_t)))
		return false;

	return true;
}

bool dc_link_set_default_brightness_aux(struct dc_link *link)
{
	uint32_t default_backlight;

	if (link && link->dpcd_sink_ext_caps.bits.oled == 1) {
		if (!dc_link_read_default_bl_aux(link, &default_backlight))
			default_backlight = 150000;
		// if < 5 nits or > 5000, it might be wrong readback
		if (default_backlight < 5000 || default_backlight > 5000000)
			default_backlight = 150000; //

		return dc_link_set_backlight_level_nits(link, true,
				default_backlight, 0);
	}
	return false;
}

bool is_edp_ilr_optimization_required(struct dc_link *link, struct dc_crtc_timing *crtc_timing)
{
	struct dc_link_settings link_setting;
	uint8_t link_bw_set;
	uint8_t link_rate_set;
	uint32_t req_bw;
	union lane_count_set lane_count_set = {0};

	ASSERT(link || crtc_timing); // invalid input

	if (link->dpcd_caps.edp_supported_link_rates_count == 0 ||
			!link->panel_config.ilr.optimize_edp_link_rate)
		return false;


	// Read DPCD 00100h to find if standard link rates are set
	core_link_read_dpcd(link, DP_LINK_BW_SET,
				&link_bw_set, sizeof(link_bw_set));

	if (link_bw_set) {
		DC_LOG_EVENT_LINK_TRAINING("eDP ILR: Optimization required, VBIOS used link_bw_set\n");
		return true;
	}

	// Read DPCD 00115h to find the edp link rate set used
	core_link_read_dpcd(link, DP_LINK_RATE_SET,
			    &link_rate_set, sizeof(link_rate_set));

	// Read DPCD 00101h to find out the number of lanes currently set
	core_link_read_dpcd(link, DP_LANE_COUNT_SET,
				&lane_count_set.raw, sizeof(lane_count_set));

	req_bw = dc_bandwidth_in_kbps_from_timing(crtc_timing);

	if (!crtc_timing->flags.DSC)
		dc_link_decide_edp_link_settings(link, &link_setting, req_bw);
	else
		decide_edp_link_settings_with_dsc(link, &link_setting, req_bw, LINK_RATE_UNKNOWN);

	if (link->dpcd_caps.edp_supported_link_rates[link_rate_set] != link_setting.link_rate ||
			lane_count_set.bits.LANE_COUNT_SET != link_setting.lane_count) {
		DC_LOG_EVENT_LINK_TRAINING("eDP ILR: Optimization required, VBIOS link_rate_set not optimal\n");
		return true;
	}

	DC_LOG_EVENT_LINK_TRAINING("eDP ILR: No optimization required, VBIOS set optimal link_rate_set\n");
	return false;
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
		status[lane].raw = get_nibble_at_index(&dpcd_buf[0], lane);
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

void edp_panel_backlight_power_on(struct dc_link *link, bool wait_for_hpd)
{
	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	link->dc->hwss.edp_power_control(link, true);
	if (wait_for_hpd)
		link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	if (link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_backlight_control(link, true);
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

void edp_add_delay_for_T9(struct dc_link *link)
{
	if (link && link->panel_config.pps.extra_delay_backlight_off > 0)
		udelay(link->panel_config.pps.extra_delay_backlight_off * 1000);
}

bool edp_receiver_ready_T9(struct dc_link *link)
{
	unsigned int tries = 0;
	unsigned char sinkstatus = 0;
	unsigned char edpRev = 0;
	enum dc_status result = DC_OK;

	result = core_link_read_dpcd(link, DP_EDP_DPCD_REV, &edpRev, sizeof(edpRev));

	/* start from eDP version 1.2, SINK_STAUS indicate the sink is ready.*/
	if (result == DC_OK && edpRev >= DP_EDP_12) {
		do {
			sinkstatus = 1;
			result = core_link_read_dpcd(link, DP_SINK_STATUS, &sinkstatus, sizeof(sinkstatus));
			if (sinkstatus == 0)
				break;
			if (result != DC_OK)
				break;
			udelay(100); //MAx T9
		} while (++tries < 50);
	}

	return result;
}
bool edp_receiver_ready_T7(struct dc_link *link)
{
	unsigned char sinkstatus = 0;
	unsigned char edpRev = 0;
	enum dc_status result = DC_OK;

	/* use absolute time stamp to constrain max T7*/
	unsigned long long enter_timestamp = 0;
	unsigned long long finish_timestamp = 0;
	unsigned long long time_taken_in_ns = 0;

	result = core_link_read_dpcd(link, DP_EDP_DPCD_REV, &edpRev, sizeof(edpRev));

	if (result == DC_OK && edpRev >= DP_EDP_12) {
		/* start from eDP version 1.2, SINK_STAUS indicate the sink is ready.*/
		enter_timestamp = dm_get_timestamp(link->ctx);
		do {
			sinkstatus = 0;
			result = core_link_read_dpcd(link, DP_SINK_STATUS, &sinkstatus, sizeof(sinkstatus));
			if (sinkstatus == 1)
				break;
			if (result != DC_OK)
				break;
			udelay(25);
			finish_timestamp = dm_get_timestamp(link->ctx);
			time_taken_in_ns = dm_get_elapse_time_in_ns(link->ctx, finish_timestamp, enter_timestamp);
		} while (time_taken_in_ns < 50 * 1000000); //MAx T7 is 50ms
	}

	if (link && link->panel_config.pps.extra_t7_ms > 0)
		udelay(link->panel_config.pps.extra_t7_ms * 1000);

	return result;
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
