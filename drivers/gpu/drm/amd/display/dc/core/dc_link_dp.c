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
#include "clk_mgr.h"
#include "resource.h"

#include "inc/core_types.h"
#include "link_hwss.h"
#include "dc_link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"
#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "inc/dc_link_dpia.h"
#include "inc/link_enc_cfg.h"

/*Travis*/
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_2[] = "sivarT";
/*Nutmeg*/
static const uint8_t DP_VGA_LVDS_CONVERTER_ID_3[] = "dnomlA";

#define DC_LOGGER \
	link->ctx->logger
#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */

#include "link_dpcd.h"

	/* maximum pre emphasis level allowed for each voltage swing level*/
	static const enum dc_pre_emphasis
	voltage_swing_to_pre_emphasis[] = { PRE_EMPHASIS_LEVEL3,
					    PRE_EMPHASIS_LEVEL2,
					    PRE_EMPHASIS_LEVEL1,
					    PRE_EMPHASIS_DISABLED };

enum {
	POST_LT_ADJ_REQ_LIMIT = 6,
	POST_LT_ADJ_REQ_TIMEOUT = 200
};

struct dp_lt_fallback_entry {
	enum dc_lane_count lane_count;
	enum dc_link_rate link_rate;
};

static const struct dp_lt_fallback_entry dp_lt_fallbacks[] = {
		/* This link training fallback array is ordered by
		 * link bandwidth from highest to lowest.
		 * DP specs makes it a normative policy to always
		 * choose the next highest link bandwidth during
		 * link training fallback.
		 */
		{LANE_COUNT_FOUR, LINK_RATE_UHBR20},
		{LANE_COUNT_FOUR, LINK_RATE_UHBR13_5},
		{LANE_COUNT_TWO, LINK_RATE_UHBR20},
		{LANE_COUNT_FOUR, LINK_RATE_UHBR10},
		{LANE_COUNT_TWO, LINK_RATE_UHBR13_5},
		{LANE_COUNT_FOUR, LINK_RATE_HIGH3},
		{LANE_COUNT_ONE, LINK_RATE_UHBR20},
		{LANE_COUNT_TWO, LINK_RATE_UHBR10},
		{LANE_COUNT_FOUR, LINK_RATE_HIGH2},
		{LANE_COUNT_ONE, LINK_RATE_UHBR13_5},
		{LANE_COUNT_TWO, LINK_RATE_HIGH3},
		{LANE_COUNT_ONE, LINK_RATE_UHBR10},
		{LANE_COUNT_TWO, LINK_RATE_HIGH2},
		{LANE_COUNT_FOUR, LINK_RATE_HIGH},
		{LANE_COUNT_ONE, LINK_RATE_HIGH3},
		{LANE_COUNT_FOUR, LINK_RATE_LOW},
		{LANE_COUNT_ONE, LINK_RATE_HIGH2},
		{LANE_COUNT_TWO, LINK_RATE_HIGH},
		{LANE_COUNT_TWO, LINK_RATE_LOW},
		{LANE_COUNT_ONE, LINK_RATE_HIGH},
		{LANE_COUNT_ONE, LINK_RATE_LOW},
};

static const struct dc_link_settings fail_safe_link_settings = {
		.lane_count = LANE_COUNT_ONE,
		.link_rate = LINK_RATE_LOW,
		.link_spread = LINK_SPREAD_DISABLED,
};

static bool decide_fallback_link_setting(
		struct dc_link *link,
		struct dc_link_settings initial_link_settings,
		struct dc_link_settings *current_link_setting,
		enum link_training_result training_result);
static void maximize_lane_settings(const struct link_training_settings *lt_settings,
		struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX]);
static void override_lane_settings(const struct link_training_settings *lt_settings,
		struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX]);

static uint32_t get_cr_training_aux_rd_interval(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	union training_aux_rd_interval training_rd_interval;
	uint32_t wait_in_micro_secs = 100;

	memset(&training_rd_interval, 0, sizeof(training_rd_interval));
	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING &&
			link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
		core_link_read_dpcd(
				link,
				DP_TRAINING_AUX_RD_INTERVAL,
				(uint8_t *)&training_rd_interval,
				sizeof(training_rd_interval));
		if (training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL)
			wait_in_micro_secs = training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL * 4000;
	}

	return wait_in_micro_secs;
}

static uint32_t get_eq_training_aux_rd_interval(
	struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	union training_aux_rd_interval training_rd_interval;

	memset(&training_rd_interval, 0, sizeof(training_rd_interval));
	if (dp_get_link_encoding_format(link_settings) == DP_128b_132b_ENCODING) {
		core_link_read_dpcd(
				link,
				DP_128b_132b_TRAINING_AUX_RD_INTERVAL,
				(uint8_t *)&training_rd_interval,
				sizeof(training_rd_interval));
	} else if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING &&
			link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
		core_link_read_dpcd(
				link,
				DP_TRAINING_AUX_RD_INTERVAL,
				(uint8_t *)&training_rd_interval,
				sizeof(training_rd_interval));
	}

	switch (training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL) {
	case 0: return 400;
	case 1: return 4000;
	case 2: return 8000;
	case 3: return 12000;
	case 4: return 16000;
	case 5: return 32000;
	case 6: return 64000;
	default: return 400;
	}
}

void dp_wait_for_training_aux_rd_interval(
	struct dc_link *link,
	uint32_t wait_in_micro_secs)
{
	if (wait_in_micro_secs > 1000)
		msleep(wait_in_micro_secs/1000);
	else
		udelay(wait_in_micro_secs);

	DC_LOG_HW_LINK_TRAINING("%s:\n wait = %d\n",
		__func__,
		wait_in_micro_secs);
}

enum dpcd_training_patterns
	dc_dp_training_pattern_to_dpcd_training_pattern(
	struct dc_link *link,
	enum dc_dp_training_pattern pattern)
{
	enum dpcd_training_patterns dpcd_tr_pattern =
	DPCD_TRAINING_PATTERN_VIDEOIDLE;

	switch (pattern) {
	case DP_TRAINING_PATTERN_SEQUENCE_1:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_1;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_2:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_2;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_3:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_3;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_4:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_4;
		break;
	case DP_128b_132b_TPS1:
		dpcd_tr_pattern = DPCD_128b_132b_TPS1;
		break;
	case DP_128b_132b_TPS2:
		dpcd_tr_pattern = DPCD_128b_132b_TPS2;
		break;
	case DP_128b_132b_TPS2_CDS:
		dpcd_tr_pattern = DPCD_128b_132b_TPS2_CDS;
		break;
	case DP_TRAINING_PATTERN_VIDEOIDLE:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_VIDEOIDLE;
		break;
	default:
		ASSERT(0);
		DC_LOG_HW_LINK_TRAINING("%s: Invalid HW Training pattern: %d\n",
			__func__, pattern);
		break;
	}

	return dpcd_tr_pattern;
}

static void dpcd_set_training_pattern(
	struct dc_link *link,
	enum dc_dp_training_pattern training_pattern)
{
	union dpcd_training_pattern dpcd_pattern = {0};

	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
			dc_dp_training_pattern_to_dpcd_training_pattern(
					link, training_pattern);

	core_link_write_dpcd(
		link,
		DP_TRAINING_PATTERN_SET,
		&dpcd_pattern.raw,
		1);

	DC_LOG_HW_LINK_TRAINING("%s\n %x pattern = %x\n",
		__func__,
		DP_TRAINING_PATTERN_SET,
		dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
}

static enum dc_dp_training_pattern decide_cr_training_pattern(
		const struct dc_link_settings *link_settings)
{
	switch (dp_get_link_encoding_format(link_settings)) {
	case DP_8b_10b_ENCODING:
	default:
		return DP_TRAINING_PATTERN_SEQUENCE_1;
	case DP_128b_132b_ENCODING:
		return DP_128b_132b_TPS1;
	}
}

static enum dc_dp_training_pattern decide_eq_training_pattern(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	struct link_encoder *link_enc;
	struct encoder_feature_support *enc_caps;
	struct dpcd_caps *rx_caps = &link->dpcd_caps;
	enum dc_dp_training_pattern pattern = DP_TRAINING_PATTERN_SEQUENCE_2;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);
	enc_caps = &link_enc->features;

	switch (dp_get_link_encoding_format(link_settings)) {
	case DP_8b_10b_ENCODING:
		if (enc_caps->flags.bits.IS_TPS4_CAPABLE &&
				rx_caps->max_down_spread.bits.TPS4_SUPPORTED)
			pattern = DP_TRAINING_PATTERN_SEQUENCE_4;
		else if (enc_caps->flags.bits.IS_TPS3_CAPABLE &&
				rx_caps->max_ln_count.bits.TPS3_SUPPORTED)
			pattern = DP_TRAINING_PATTERN_SEQUENCE_3;
		else
			pattern = DP_TRAINING_PATTERN_SEQUENCE_2;
		break;
	case DP_128b_132b_ENCODING:
		pattern = DP_128b_132b_TPS2;
		break;
	default:
		pattern = DP_TRAINING_PATTERN_SEQUENCE_2;
		break;
	}
	return pattern;
}

static uint8_t get_dpcd_link_rate(const struct dc_link_settings *link_settings)
{
	uint8_t link_rate = 0;
	enum dp_link_encoding encoding = dp_get_link_encoding_format(link_settings);

	if (encoding == DP_128b_132b_ENCODING)
		switch (link_settings->link_rate) {
		case LINK_RATE_UHBR10:
			link_rate = 0x1;
			break;
		case LINK_RATE_UHBR20:
			link_rate = 0x2;
			break;
		case LINK_RATE_UHBR13_5:
			link_rate = 0x4;
			break;
		default:
			link_rate = 0;
			break;
		}
	else if (encoding == DP_8b_10b_ENCODING)
		link_rate = (uint8_t) link_settings->link_rate;
	else
		link_rate = 0;

	return link_rate;
}

static void vendor_specific_lttpr_wa_one_start(struct dc_link *link)
{
	const uint8_t vendor_lttpr_write_data[4] = {0x1, 0x50, 0x63, 0xff};
	const uint8_t offset = dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	uint32_t vendor_lttpr_write_address = 0xF004F;

	if (offset != 0xFF)
		vendor_lttpr_write_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	/* W/A for certain LTTPR to reset their lane settings, part one of two */
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data[0],
			sizeof(vendor_lttpr_write_data));
}

static void vendor_specific_lttpr_wa_one_end(
	struct dc_link *link,
	uint8_t retry_count)
{
	const uint8_t vendor_lttpr_write_data[4] = {0x1, 0x50, 0x63, 0x0};
	const uint8_t offset = dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	uint32_t vendor_lttpr_write_address = 0xF004F;

	if (!retry_count) {
		if (offset != 0xFF)
			vendor_lttpr_write_address +=
					((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

		/* W/A for certain LTTPR to reset their lane settings, part two of two */
		core_link_write_dpcd(
				link,
				vendor_lttpr_write_address,
				&vendor_lttpr_write_data[0],
				sizeof(vendor_lttpr_write_data));
	}
}

static void vendor_specific_lttpr_wa_one_two(
	struct dc_link *link,
	const uint8_t rate)
{
	if (link->apply_vendor_specific_lttpr_link_rate_wa) {
		uint8_t toggle_rate = 0x0;

		if (rate == 0x6)
			toggle_rate = 0xA;
		else
			toggle_rate = 0x6;

		if (link->vendor_specific_lttpr_link_rate_wa == rate) {
			/* W/A for certain LTTPR to reset internal state for link training */
			core_link_write_dpcd(
					link,
					DP_LINK_BW_SET,
					&toggle_rate,
					1);
		}

		/* Store the last attempted link rate for this link */
		link->vendor_specific_lttpr_link_rate_wa = rate;
	}
}

static void vendor_specific_lttpr_wa_three(
	struct dc_link *link,
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX])
{
	const uint8_t vendor_lttpr_write_data_vs[3] = {0x0, 0x53, 0x63};
	const uint8_t vendor_lttpr_write_data_pe[3] = {0x0, 0x54, 0x63};
	const uint8_t offset = dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	uint32_t vendor_lttpr_write_address = 0xF004F;
	uint32_t vendor_lttpr_read_address = 0xF0053;
	uint8_t dprx_vs = 0;
	uint8_t dprx_pe = 0;
	uint8_t lane;

	if (offset != 0xFF) {
		vendor_lttpr_write_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
		vendor_lttpr_read_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
	}

	/* W/A to read lane settings requested by DPRX */
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_vs[0],
			sizeof(vendor_lttpr_write_data_vs));
	core_link_read_dpcd(
			link,
			vendor_lttpr_read_address,
			&dprx_vs,
			1);
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_pe[0],
			sizeof(vendor_lttpr_write_data_pe));
	core_link_read_dpcd(
			link,
			vendor_lttpr_read_address,
			&dprx_pe,
			1);

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		dpcd_lane_adjust[lane].bits.VOLTAGE_SWING_LANE = (dprx_vs >> (2 * lane)) & 0x3;
		dpcd_lane_adjust[lane].bits.PRE_EMPHASIS_LANE = (dprx_pe >> (2 * lane)) & 0x3;
	}
}

static void vendor_specific_lttpr_wa_three_dpcd(
	struct dc_link *link,
	union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX])
{
	union lane_adjust lane_adjust[LANE_COUNT_DP_MAX];
	uint8_t lane = 0;

	vendor_specific_lttpr_wa_three(link, lane_adjust);

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		dpcd_lane_adjust[lane].bits.VOLTAGE_SWING_SET = lane_adjust[lane].bits.VOLTAGE_SWING_LANE;
		dpcd_lane_adjust[lane].bits.PRE_EMPHASIS_SET = lane_adjust[lane].bits.PRE_EMPHASIS_LANE;
	}
}

static void vendor_specific_lttpr_wa_four(
	struct dc_link *link,
	bool apply_wa)
{
	const uint8_t vendor_lttpr_write_data_one[4] = {0x1, 0x55, 0x63, 0x8};
	const uint8_t vendor_lttpr_write_data_two[4] = {0x1, 0x55, 0x63, 0x0};
	const uint8_t offset = dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	uint32_t vendor_lttpr_write_address = 0xF004F;
	uint8_t sink_status = 0;
	uint8_t i;

	if (offset != 0xFF)
		vendor_lttpr_write_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	/* W/A to pass through DPCD write of TPS=0 to DPRX */
	if (apply_wa) {
		core_link_write_dpcd(
				link,
				vendor_lttpr_write_address,
				&vendor_lttpr_write_data_one[0],
				sizeof(vendor_lttpr_write_data_one));
	}

	/* clear training pattern set */
	dpcd_set_training_pattern(link, DP_TRAINING_PATTERN_VIDEOIDLE);

	if (apply_wa) {
		core_link_write_dpcd(
				link,
				vendor_lttpr_write_address,
				&vendor_lttpr_write_data_two[0],
				sizeof(vendor_lttpr_write_data_two));
	}

	/* poll for intra-hop disable */
	for (i = 0; i < 10; i++) {
		if ((core_link_read_dpcd(link, DP_SINK_STATUS, &sink_status, 1) == DC_OK) &&
				(sink_status & DP_INTRA_HOP_AUX_REPLY_INDICATION) == 0)
			break;
		udelay(1000);
	}
}

static void vendor_specific_lttpr_wa_five(
	struct dc_link *link,
	const union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX],
	uint8_t lane_count)
{
	const uint32_t vendor_lttpr_write_address = 0xF004F;
	const uint8_t vendor_lttpr_write_data_reset[4] = {0x1, 0x50, 0x63, 0xFF};
	uint8_t vendor_lttpr_write_data_vs[4] = {0x1, 0x51, 0x63, 0x0};
	uint8_t vendor_lttpr_write_data_pe[4] = {0x1, 0x52, 0x63, 0x0};
	uint8_t lane = 0;

	for (lane = 0; lane < lane_count; lane++) {
		vendor_lttpr_write_data_vs[3] |=
				dpcd_lane_adjust[lane].bits.VOLTAGE_SWING_SET << (2 * lane);
		vendor_lttpr_write_data_pe[3] |=
				dpcd_lane_adjust[lane].bits.PRE_EMPHASIS_SET << (2 * lane);
	}

	/* Force LTTPR to output desired VS and PE */
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_reset[0],
			sizeof(vendor_lttpr_write_data_reset));
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_vs[0],
			sizeof(vendor_lttpr_write_data_vs));
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_pe[0],
			sizeof(vendor_lttpr_write_data_pe));
}

enum dc_status dpcd_set_link_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings)
{
	uint8_t rate;
	enum dc_status status;

	union down_spread_ctrl downspread = {0};
	union lane_count_set lane_count_set = {0};

	downspread.raw = (uint8_t)
	(lt_settings->link_settings.link_spread);

	lane_count_set.bits.LANE_COUNT_SET =
	lt_settings->link_settings.lane_count;

	lane_count_set.bits.ENHANCED_FRAMING = lt_settings->enhanced_framing;
	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED = 0;


	if (link->ep_type == DISPLAY_ENDPOINT_PHY &&
			lt_settings->pattern_for_eq < DP_TRAINING_PATTERN_SEQUENCE_4) {
		lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED =
				link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED;
	}

	status = core_link_write_dpcd(link, DP_DOWNSPREAD_CTRL,
		&downspread.raw, sizeof(downspread));

	status = core_link_write_dpcd(link, DP_LANE_COUNT_SET,
		&lane_count_set.raw, 1);

	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_13 &&
			lt_settings->link_settings.use_link_rate_set == true) {
		rate = 0;
		/* WA for some MUX chips that will power down with eDP and lose supported
		 * link rate set for eDP 1.4. Source reads DPCD 0x010 again to ensure
		 * MUX chip gets link rate set back before link training.
		 */
		if (link->connector_signal == SIGNAL_TYPE_EDP) {
			uint8_t supported_link_rates[16];

			core_link_read_dpcd(link, DP_SUPPORTED_LINK_RATES,
					supported_link_rates, sizeof(supported_link_rates));
		}
		status = core_link_write_dpcd(link, DP_LINK_BW_SET, &rate, 1);
		status = core_link_write_dpcd(link, DP_LINK_RATE_SET,
				&lt_settings->link_settings.link_rate_set, 1);
	} else {
		rate = get_dpcd_link_rate(&lt_settings->link_settings);
		if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
					(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
					link->lttpr_mode == LTTPR_MODE_TRANSPARENT)
			vendor_specific_lttpr_wa_one_start(link);

		if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
					(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN))
			vendor_specific_lttpr_wa_one_two(link, rate);

		status = core_link_write_dpcd(link, DP_LINK_BW_SET, &rate, 1);
	}

	if (rate) {
		DC_LOG_HW_LINK_TRAINING("%s\n %x rate = %x\n %x lane = %x framing = %x\n %x spread = %x\n",
			__func__,
			DP_LINK_BW_SET,
			lt_settings->link_settings.link_rate,
			DP_LANE_COUNT_SET,
			lt_settings->link_settings.lane_count,
			lt_settings->enhanced_framing,
			DP_DOWNSPREAD_CTRL,
			lt_settings->link_settings.link_spread);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s\n %x rate set = %x\n %x lane = %x framing = %x\n %x spread = %x\n",
			__func__,
			DP_LINK_RATE_SET,
			lt_settings->link_settings.link_rate_set,
			DP_LANE_COUNT_SET,
			lt_settings->link_settings.lane_count,
			lt_settings->enhanced_framing,
			DP_DOWNSPREAD_CTRL,
			lt_settings->link_settings.link_spread);
	}

	return status;
}

uint8_t dc_dp_initialize_scrambling_data_symbols(
	struct dc_link *link,
	enum dc_dp_training_pattern pattern)
{
	uint8_t disable_scrabled_data_symbols = 0;

	switch (pattern) {
	case DP_TRAINING_PATTERN_SEQUENCE_1:
	case DP_TRAINING_PATTERN_SEQUENCE_2:
	case DP_TRAINING_PATTERN_SEQUENCE_3:
		disable_scrabled_data_symbols = 1;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_4:
	case DP_128b_132b_TPS1:
	case DP_128b_132b_TPS2:
		disable_scrabled_data_symbols = 0;
		break;
	default:
		ASSERT(0);
		DC_LOG_HW_LINK_TRAINING("%s: Invalid HW Training pattern: %d\n",
			__func__, pattern);
		break;
	}
	return disable_scrabled_data_symbols;
}

static inline bool is_repeater(struct dc_link *link, uint32_t offset)
{
	return (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) && (offset != 0);
}

static void dpcd_set_lt_pattern_and_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings,
	enum dc_dp_training_pattern pattern,
	uint32_t offset)
{
	uint32_t dpcd_base_lt_offset;

	uint8_t dpcd_lt_buffer[5] = {0};
	union dpcd_training_pattern dpcd_pattern = { 0 };
	uint32_t size_in_bytes;
	bool edp_workaround = false; /* TODO link_prop.INTERNAL */
	dpcd_base_lt_offset = DP_TRAINING_PATTERN_SET;

	if (is_repeater(link, offset))
		dpcd_base_lt_offset = DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
			((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	/*****************************************************************
	* DpcdAddress_TrainingPatternSet
	*****************************************************************/
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
		dc_dp_training_pattern_to_dpcd_training_pattern(link, pattern);

	dpcd_pattern.v1_4.SCRAMBLING_DISABLE =
		dc_dp_initialize_scrambling_data_symbols(link, pattern);

	dpcd_lt_buffer[DP_TRAINING_PATTERN_SET - DP_TRAINING_PATTERN_SET]
		= dpcd_pattern.raw;

	if (is_repeater(link, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s\n LTTPR Repeater ID: %d\n 0x%X pattern = %x\n",
			__func__,
			offset,
			dpcd_base_lt_offset,
			dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s\n 0x%X pattern = %x\n",
			__func__,
			dpcd_base_lt_offset,
			dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
	}

	/* concatenate everything into one buffer*/
	size_in_bytes = lt_settings->link_settings.lane_count *
			sizeof(lt_settings->dpcd_lane_settings[0]);

	 // 0x00103 - 0x00102
	memmove(
		&dpcd_lt_buffer[DP_TRAINING_LANE0_SET - DP_TRAINING_PATTERN_SET],
		lt_settings->dpcd_lane_settings,
		size_in_bytes);

	if (is_repeater(link, offset)) {
		if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
					" 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					offset,
					dpcd_base_lt_offset,
					lt_settings->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING)
		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X VS set = %x PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			offset,
			dpcd_base_lt_offset,
			lt_settings->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET,
			lt_settings->dpcd_lane_settings[0].bits.PRE_EMPHASIS_SET,
			lt_settings->dpcd_lane_settings[0].bits.MAX_SWING_REACHED,
			lt_settings->dpcd_lane_settings[0].bits.MAX_PRE_EMPHASIS_REACHED);
	} else {
		if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					dpcd_base_lt_offset,
					lt_settings->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
					__func__,
					dpcd_base_lt_offset,
					lt_settings->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET,
					lt_settings->dpcd_lane_settings[0].bits.PRE_EMPHASIS_SET,
					lt_settings->dpcd_lane_settings[0].bits.MAX_SWING_REACHED,
					lt_settings->dpcd_lane_settings[0].bits.MAX_PRE_EMPHASIS_REACHED);
	}
	if (edp_workaround) {
		/* for eDP write in 2 parts because the 5-byte burst is
		* causing issues on some eDP panels (EPR#366724)
		*/
		core_link_write_dpcd(
			link,
			DP_TRAINING_PATTERN_SET,
			&dpcd_pattern.raw,
			sizeof(dpcd_pattern.raw));

		core_link_write_dpcd(
			link,
			DP_TRAINING_LANE0_SET,
			(uint8_t *)(lt_settings->dpcd_lane_settings),
			size_in_bytes);

	} else if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
			DP_128b_132b_ENCODING) {
		core_link_write_dpcd(
				link,
				dpcd_base_lt_offset,
				dpcd_lt_buffer,
				sizeof(dpcd_lt_buffer));
	} else
		/* write it all in (1 + number-of-lanes)-byte burst*/
		core_link_write_dpcd(
				link,
				dpcd_base_lt_offset,
				dpcd_lt_buffer,
				size_in_bytes + sizeof(dpcd_pattern.raw));
}

bool dp_is_cr_done(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status)
{
	uint32_t lane;
	/*LANEx_CR_DONE bits All 1's?*/
	for (lane = 0; lane < (uint32_t)(ln_count); lane++) {
		if (!dpcd_lane_status[lane].bits.CR_DONE_0)
			return false;
	}
	return true;
}

bool dp_is_ch_eq_done(enum dc_lane_count ln_count,
		union lane_status *dpcd_lane_status)
{
	bool done = true;
	uint32_t lane;
	for (lane = 0; lane < (uint32_t)(ln_count); lane++)
		if (!dpcd_lane_status[lane].bits.CHANNEL_EQ_DONE_0)
			done = false;
	return done;
}

bool dp_is_symbol_locked(enum dc_lane_count ln_count,
		union lane_status *dpcd_lane_status)
{
	bool locked = true;
	uint32_t lane;
	for (lane = 0; lane < (uint32_t)(ln_count); lane++)
		if (!dpcd_lane_status[lane].bits.SYMBOL_LOCKED_0)
			locked = false;
	return locked;
}

bool dp_is_interlane_aligned(union lane_align_status_updated align_status)
{
	return align_status.bits.INTERLANE_ALIGN_DONE == 1;
}

void dp_hw_to_dpcd_lane_settings(
		const struct link_training_settings *lt_settings,
		const struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
		union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX])
{
	uint8_t lane = 0;

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING) {
			dpcd_lane_settings[lane].bits.VOLTAGE_SWING_SET =
					(uint8_t)(hw_lane_settings[lane].VOLTAGE_SWING);
			dpcd_lane_settings[lane].bits.PRE_EMPHASIS_SET =
					(uint8_t)(hw_lane_settings[lane].PRE_EMPHASIS);
			dpcd_lane_settings[lane].bits.MAX_SWING_REACHED =
					(hw_lane_settings[lane].VOLTAGE_SWING ==
							VOLTAGE_SWING_MAX_LEVEL ? 1 : 0);
			dpcd_lane_settings[lane].bits.MAX_PRE_EMPHASIS_REACHED =
					(hw_lane_settings[lane].PRE_EMPHASIS ==
							PRE_EMPHASIS_MAX_LEVEL ? 1 : 0);
		}
		else if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING) {
			dpcd_lane_settings[lane].tx_ffe.PRESET_VALUE =
					hw_lane_settings[lane].FFE_PRESET.settings.level;
		}
	}
}

void dp_decide_lane_settings(
		const struct link_training_settings *lt_settings,
		const union lane_adjust ln_adjust[LANE_COUNT_DP_MAX],
		struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
		union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX])
{
	uint32_t lane;

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING) {
			hw_lane_settings[lane].VOLTAGE_SWING =
					(enum dc_voltage_swing)(ln_adjust[lane].bits.
							VOLTAGE_SWING_LANE);
			hw_lane_settings[lane].PRE_EMPHASIS =
					(enum dc_pre_emphasis)(ln_adjust[lane].bits.
							PRE_EMPHASIS_LANE);
		}
		else if (dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING) {
			hw_lane_settings[lane].FFE_PRESET.raw =
					ln_adjust[lane].tx_ffe.PRESET_VALUE;
		}
	}
	dp_hw_to_dpcd_lane_settings(lt_settings, hw_lane_settings, dpcd_lane_settings);

	if (lt_settings->disallow_per_lane_settings) {
		/* we find the maximum of the requested settings across all lanes*/
		/* and set this maximum for all lanes*/
		maximize_lane_settings(lt_settings, hw_lane_settings);
		override_lane_settings(lt_settings, hw_lane_settings);

		if (lt_settings->always_match_dpcd_with_hw_lane_settings)
			dp_hw_to_dpcd_lane_settings(lt_settings, hw_lane_settings, dpcd_lane_settings);
	}

}

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

static enum dc_pre_emphasis get_max_pre_emphasis_for_voltage_swing(
	enum dc_voltage_swing voltage)
{
	enum dc_pre_emphasis pre_emphasis;
	pre_emphasis = PRE_EMPHASIS_MAX_LEVEL;

	if (voltage <= VOLTAGE_SWING_MAX_LEVEL)
		pre_emphasis = voltage_swing_to_pre_emphasis[voltage];

	return pre_emphasis;

}

static void maximize_lane_settings(const struct link_training_settings *lt_settings,
		struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	uint32_t lane;
	struct dc_lane_settings max_requested;

	max_requested.VOLTAGE_SWING = lane_settings[0].VOLTAGE_SWING;
	max_requested.PRE_EMPHASIS = lane_settings[0].PRE_EMPHASIS;
	max_requested.FFE_PRESET = lane_settings[0].FFE_PRESET;

	/* Determine what the maximum of the requested settings are*/
	for (lane = 1; lane < lt_settings->link_settings.lane_count; lane++) {
		if (lane_settings[lane].VOLTAGE_SWING > max_requested.VOLTAGE_SWING)
			max_requested.VOLTAGE_SWING = lane_settings[lane].VOLTAGE_SWING;

		if (lane_settings[lane].PRE_EMPHASIS > max_requested.PRE_EMPHASIS)
			max_requested.PRE_EMPHASIS = lane_settings[lane].PRE_EMPHASIS;
		if (lane_settings[lane].FFE_PRESET.settings.level >
				max_requested.FFE_PRESET.settings.level)
			max_requested.FFE_PRESET.settings.level =
					lane_settings[lane].FFE_PRESET.settings.level;
	}

	/* make sure the requested settings are
	 * not higher than maximum settings*/
	if (max_requested.VOLTAGE_SWING > VOLTAGE_SWING_MAX_LEVEL)
		max_requested.VOLTAGE_SWING = VOLTAGE_SWING_MAX_LEVEL;

	if (max_requested.PRE_EMPHASIS > PRE_EMPHASIS_MAX_LEVEL)
		max_requested.PRE_EMPHASIS = PRE_EMPHASIS_MAX_LEVEL;
	if (max_requested.FFE_PRESET.settings.level > DP_FFE_PRESET_MAX_LEVEL)
		max_requested.FFE_PRESET.settings.level = DP_FFE_PRESET_MAX_LEVEL;

	/* make sure the pre-emphasis matches the voltage swing*/
	if (max_requested.PRE_EMPHASIS >
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING))
		max_requested.PRE_EMPHASIS =
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING);

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		lane_settings[lane].VOLTAGE_SWING = max_requested.VOLTAGE_SWING;
		lane_settings[lane].PRE_EMPHASIS = max_requested.PRE_EMPHASIS;
		lane_settings[lane].FFE_PRESET = max_requested.FFE_PRESET;
	}
}

static void override_lane_settings(const struct link_training_settings *lt_settings,
		struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	uint32_t lane;

	if (lt_settings->voltage_swing == NULL &&
	    lt_settings->pre_emphasis == NULL &&
	    lt_settings->ffe_preset == NULL &&
	    lt_settings->post_cursor2 == NULL)

		return;

	for (lane = 1; lane < LANE_COUNT_DP_MAX; lane++) {
		if (lt_settings->voltage_swing)
			lane_settings[lane].VOLTAGE_SWING = *lt_settings->voltage_swing;
		if (lt_settings->pre_emphasis)
			lane_settings[lane].PRE_EMPHASIS = *lt_settings->pre_emphasis;
		if (lt_settings->post_cursor2)
			lane_settings[lane].POST_CURSOR2 = *lt_settings->post_cursor2;
		if (lt_settings->ffe_preset)
			lane_settings[lane].FFE_PRESET = *lt_settings->ffe_preset;
	}
}

enum dc_status dp_get_lane_status_and_lane_adjust(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	union lane_status ln_status[LANE_COUNT_DP_MAX],
	union lane_align_status_updated *ln_align,
	union lane_adjust ln_adjust[LANE_COUNT_DP_MAX],
	uint32_t offset)
{
	unsigned int lane01_status_address = DP_LANE0_1_STATUS;
	uint8_t lane_adjust_offset = 4;
	unsigned int lane01_adjust_address;
	uint8_t dpcd_buf[6] = {0};
	uint32_t lane;
	enum dc_status status;

	if (is_repeater(link, offset)) {
		lane01_status_address =
				DP_LANE0_1_STATUS_PHY_REPEATER1 +
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
		lane_adjust_offset = 3;
	}

	status = core_link_read_dpcd(
		link,
		lane01_status_address,
		(uint8_t *)(dpcd_buf),
		sizeof(dpcd_buf));

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		ln_status[lane].raw =
			get_nibble_at_index(&dpcd_buf[0], lane);
		ln_adjust[lane].raw =
			get_nibble_at_index(&dpcd_buf[lane_adjust_offset], lane);
	}

	ln_align->raw = dpcd_buf[2];

	if (is_repeater(link, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X Lane01Status = %x\n 0x%X Lane23Status = %x\n ",
			__func__,
			offset,
			lane01_status_address, dpcd_buf[0],
			lane01_status_address + 1, dpcd_buf[1]);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X Lane01Status = %x\n 0x%X Lane23Status = %x\n ",
			__func__,
			lane01_status_address, dpcd_buf[0],
			lane01_status_address + 1, dpcd_buf[1]);
	}
	lane01_adjust_address = DP_ADJUST_REQUEST_LANE0_1;

	if (is_repeater(link, offset))
		lane01_adjust_address = DP_ADJUST_REQUEST_LANE0_1_PHY_REPEATER1 +
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	if (is_repeater(link, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X Lane01AdjustRequest = %x\n 0x%X Lane23AdjustRequest = %x\n",
					__func__,
					offset,
					lane01_adjust_address,
					dpcd_buf[lane_adjust_offset],
					lane01_adjust_address + 1,
					dpcd_buf[lane_adjust_offset + 1]);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X Lane01AdjustRequest = %x\n 0x%X Lane23AdjustRequest = %x\n",
			__func__,
			lane01_adjust_address,
			dpcd_buf[lane_adjust_offset],
			lane01_adjust_address + 1,
			dpcd_buf[lane_adjust_offset + 1]);
	}

	return status;
}

enum dc_status dpcd_set_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	uint32_t offset)
{
	unsigned int lane0_set_address;
	enum dc_status status;

	lane0_set_address = DP_TRAINING_LANE0_SET;

	if (is_repeater(link, offset))
		lane0_set_address = DP_TRAINING_LANE0_SET_PHY_REPEATER1 +
		((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	status = core_link_write_dpcd(link,
		lane0_set_address,
		(uint8_t *)(link_training_setting->dpcd_lane_settings),
		link_training_setting->link_settings.lane_count);

	if (is_repeater(link, offset)) {
		if (dp_get_link_encoding_format(&link_training_setting->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
					" 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					offset,
					lane0_set_address,
					link_training_setting->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (dp_get_link_encoding_format(&link_training_setting->link_settings) ==
				DP_8b_10b_ENCODING)
		DC_LOG_HW_LINK_TRAINING("%s\n LTTPR Repeater ID: %d\n"
				" 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			offset,
			lane0_set_address,
			link_training_setting->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET,
			link_training_setting->dpcd_lane_settings[0].bits.PRE_EMPHASIS_SET,
			link_training_setting->dpcd_lane_settings[0].bits.MAX_SWING_REACHED,
			link_training_setting->dpcd_lane_settings[0].bits.MAX_PRE_EMPHASIS_REACHED);

	} else {
		if (dp_get_link_encoding_format(&link_training_setting->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					lane0_set_address,
					link_training_setting->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (dp_get_link_encoding_format(&link_training_setting->link_settings) ==
				DP_8b_10b_ENCODING)
		DC_LOG_HW_LINK_TRAINING("%s\n 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			lane0_set_address,
			link_training_setting->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET,
			link_training_setting->dpcd_lane_settings[0].bits.PRE_EMPHASIS_SET,
			link_training_setting->dpcd_lane_settings[0].bits.MAX_SWING_REACHED,
			link_training_setting->dpcd_lane_settings[0].bits.MAX_PRE_EMPHASIS_REACHED);
	}

	return status;
}

bool dp_is_max_vs_reached(
	const struct link_training_settings *lt_settings)
{
	uint32_t lane;
	for (lane = 0; lane <
		(uint32_t)(lt_settings->link_settings.lane_count);
		lane++) {
		if (lt_settings->dpcd_lane_settings[lane].bits.VOLTAGE_SWING_SET
			== VOLTAGE_SWING_MAX_LEVEL)
			return true;
	}
	return false;

}

static bool perform_post_lt_adj_req_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	enum dc_lane_count lane_count =
	lt_settings->link_settings.lane_count;

	uint32_t adj_req_count;
	uint32_t adj_req_timer;
	bool req_drv_setting_changed;
	uint32_t lane;

	req_drv_setting_changed = false;
	for (adj_req_count = 0; adj_req_count < POST_LT_ADJ_REQ_LIMIT;
	adj_req_count++) {

		req_drv_setting_changed = false;

		for (adj_req_timer = 0;
			adj_req_timer < POST_LT_ADJ_REQ_TIMEOUT;
			adj_req_timer++) {

			union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
			union lane_align_status_updated
				dpcd_lane_status_updated;
			union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = { { {0} } };

			dp_get_lane_status_and_lane_adjust(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				dpcd_lane_adjust,
				DPRX);

			if (dpcd_lane_status_updated.bits.
					POST_LT_ADJ_REQ_IN_PROGRESS == 0)
				return true;

			if (!dp_is_cr_done(lane_count, dpcd_lane_status))
				return false;

			if (!dp_is_ch_eq_done(lane_count, dpcd_lane_status) ||
					!dp_is_symbol_locked(lane_count, dpcd_lane_status) ||
					!dp_is_interlane_aligned(dpcd_lane_status_updated))
				return false;

			for (lane = 0; lane < (uint32_t)(lane_count); lane++) {

				if (lt_settings->
				dpcd_lane_settings[lane].bits.VOLTAGE_SWING_SET !=
				dpcd_lane_adjust[lane].bits.VOLTAGE_SWING_LANE ||
				lt_settings->dpcd_lane_settings[lane].bits.PRE_EMPHASIS_SET !=
				dpcd_lane_adjust[lane].bits.PRE_EMPHASIS_LANE) {

					req_drv_setting_changed = true;
					break;
				}
			}

			if (req_drv_setting_changed) {
				dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
						lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);

				dc_link_dp_set_drive_settings(link,
						link_res,
						lt_settings);
				break;
			}

			msleep(1);
		}

		if (!req_drv_setting_changed) {
			DC_LOG_WARNING("%s: Post Link Training Adjust Request Timed out\n",
				__func__);

			ASSERT(0);
			return true;
		}
	}
	DC_LOG_WARNING("%s: Post Link Training Adjust Request limit reached\n",
		__func__);

	ASSERT(0);
	return true;

}

/* Only used for channel equalization */
uint32_t dp_translate_training_aux_read_interval(uint32_t dpcd_aux_read_interval)
{
	unsigned int aux_rd_interval_us = 400;

	switch (dpcd_aux_read_interval) {
	case 0x01:
		aux_rd_interval_us = 4000;
		break;
	case 0x02:
		aux_rd_interval_us = 8000;
		break;
	case 0x03:
		aux_rd_interval_us = 12000;
		break;
	case 0x04:
		aux_rd_interval_us = 16000;
		break;
	case 0x05:
		aux_rd_interval_us = 32000;
		break;
	case 0x06:
		aux_rd_interval_us = 64000;
		break;
	default:
		break;
	}

	return aux_rd_interval_us;
}

enum link_training_result dp_get_cr_failure(enum dc_lane_count ln_count,
					union lane_status *dpcd_lane_status)
{
	enum link_training_result result = LINK_TRAINING_SUCCESS;

	if (ln_count >= LANE_COUNT_ONE && !dpcd_lane_status[0].bits.CR_DONE_0)
		result = LINK_TRAINING_CR_FAIL_LANE0;
	else if (ln_count >= LANE_COUNT_TWO && !dpcd_lane_status[1].bits.CR_DONE_0)
		result = LINK_TRAINING_CR_FAIL_LANE1;
	else if (ln_count >= LANE_COUNT_FOUR && !dpcd_lane_status[2].bits.CR_DONE_0)
		result = LINK_TRAINING_CR_FAIL_LANE23;
	else if (ln_count >= LANE_COUNT_FOUR && !dpcd_lane_status[3].bits.CR_DONE_0)
		result = LINK_TRAINING_CR_FAIL_LANE23;
	return result;
}

static enum link_training_result perform_channel_equalization_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings,
	uint32_t offset)
{
	enum dc_dp_training_pattern tr_pattern;
	uint32_t retries_ch_eq;
	uint32_t wait_time_microsec;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_align_status_updated dpcd_lane_status_updated = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

	/* Note: also check that TPS4 is a supported feature*/
	tr_pattern = lt_settings->pattern_for_eq;

	if (is_repeater(link, offset) && dp_get_link_encoding_format(&lt_settings->link_settings) == DP_8b_10b_ENCODING)
		tr_pattern = DP_TRAINING_PATTERN_SEQUENCE_4;

	dp_set_hw_training_pattern(link, link_res, tr_pattern, offset);

	for (retries_ch_eq = 0; retries_ch_eq <= LINK_TRAINING_MAX_RETRY_COUNT;
		retries_ch_eq++) {

		dp_set_hw_lane_settings(link, link_res, lt_settings, offset);

		/* 2. update DPCD*/
		if (!retries_ch_eq)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration
			 */

			dpcd_set_lt_pattern_and_lane_settings(
				link,
				lt_settings,
				tr_pattern, offset);
		else
			dpcd_set_lane_settings(link, lt_settings, offset);

		/* 3. wait for receiver to lock-on*/
		wait_time_microsec = lt_settings->eq_pattern_time;

		if (is_repeater(link, offset))
			wait_time_microsec =
					dp_translate_training_aux_read_interval(
						link->dpcd_caps.lttpr_caps.aux_rd_interval[offset - 1]);

		if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
				(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
				link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
			wait_time_microsec = 16000;
		}

		dp_wait_for_training_aux_rd_interval(
				link,
				wait_time_microsec);

		/* 4. Read lane status and requested
		 * drive settings as set by the sink*/

		dp_get_lane_status_and_lane_adjust(
			link,
			lt_settings,
			dpcd_lane_status,
			&dpcd_lane_status_updated,
			dpcd_lane_adjust,
			offset);

		/* 5. check CR done*/
		if (!dp_is_cr_done(lane_count, dpcd_lane_status))
			return LINK_TRAINING_EQ_FAIL_CR;

		/* 6. check CHEQ done*/
		if (dp_is_ch_eq_done(lane_count, dpcd_lane_status) &&
				dp_is_symbol_locked(lane_count, dpcd_lane_status) &&
				dp_is_interlane_aligned(dpcd_lane_status_updated))
			return LINK_TRAINING_SUCCESS;

		/* 7. update VS/PE/PC2 in lt_settings*/
		dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
				lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
	}

	return LINK_TRAINING_EQ_FAIL_EQ;

}

static void start_clock_recovery_pattern_early(struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings,
		uint32_t offset)
{
	DC_LOG_HW_LINK_TRAINING("%s\n GPU sends TPS1. Wait 400us.\n",
			__func__);
	dp_set_hw_training_pattern(link, link_res, lt_settings->pattern_for_cr, offset);
	dp_set_hw_lane_settings(link, link_res, lt_settings, offset);
	udelay(400);
}

static enum link_training_result perform_clock_recovery_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings,
	uint32_t offset)
{
	uint32_t retries_cr;
	uint32_t retry_count;
	uint32_t wait_time_microsec;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated dpcd_lane_status_updated;
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = { { {0} } };

	retries_cr = 0;
	retry_count = 0;

	if (!link->ctx->dc->work_arounds.lt_early_cr_pattern)
		dp_set_hw_training_pattern(link, link_res, lt_settings->pattern_for_cr, offset);

	/* najeeb - The synaptics MST hub can put the LT in
	* infinite loop by switching the VS
	*/
	/* between level 0 and level 1 continuously, here
	* we try for CR lock for LinkTrainingMaxCRRetry count*/
	while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
		(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {

		memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
		memset(&dpcd_lane_status_updated, '\0',
		sizeof(dpcd_lane_status_updated));

		/* 1. call HWSS to set lane settings*/
		dp_set_hw_lane_settings(
				link,
				link_res,
				lt_settings,
				offset);

		/* 2. update DPCD of the receiver*/
		if (!retry_count)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration.*/
			dpcd_set_lt_pattern_and_lane_settings(
					link,
					lt_settings,
					lt_settings->pattern_for_cr,
					offset);
		else
			dpcd_set_lane_settings(
					link,
					lt_settings,
					offset);

		/* 3. wait receiver to lock-on*/
		wait_time_microsec = lt_settings->cr_pattern_time;

		if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
				(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN)) {
			wait_time_microsec = 16000;
		}

		dp_wait_for_training_aux_rd_interval(
				link,
				wait_time_microsec);

		/* 4. Read lane status and requested drive
		* settings as set by the sink
		*/
		dp_get_lane_status_and_lane_adjust(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				dpcd_lane_adjust,
				offset);

		if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
				(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
				link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
			vendor_specific_lttpr_wa_one_end(link, retry_count);
			vendor_specific_lttpr_wa_three(link, dpcd_lane_adjust);
		}

		/* 5. check CR done*/
		if (dp_is_cr_done(lane_count, dpcd_lane_status))
			return LINK_TRAINING_SUCCESS;

		/* 6. max VS reached*/
		if ((dp_get_link_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING) &&
				dp_is_max_vs_reached(lt_settings))
			break;

		/* 7. same lane settings*/
		/* Note: settings are the same for all lanes,
		 * so comparing first lane is sufficient*/
		if ((dp_get_link_encoding_format(&lt_settings->link_settings) == DP_8b_10b_ENCODING) &&
				lt_settings->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET ==
						dpcd_lane_adjust[0].bits.VOLTAGE_SWING_LANE)
			retries_cr++;
		else if ((dp_get_link_encoding_format(&lt_settings->link_settings) == DP_128b_132b_ENCODING) &&
				lt_settings->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE ==
						dpcd_lane_adjust[0].tx_ffe.PRESET_VALUE)
			retries_cr++;
		else
			retries_cr = 0;

		/* 8. update VS/PE/PC2 in lt_settings*/
		dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
				lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
		retry_count++;
	}

	if (retry_count >= LINK_TRAINING_MAX_CR_RETRY) {
		ASSERT(0);
		DC_LOG_ERROR("%s: Link Training Error, could not get CR after %d tries. Possibly voltage swing issue",
			__func__,
			LINK_TRAINING_MAX_CR_RETRY);

	}

	return dp_get_cr_failure(lane_count, dpcd_lane_status);
}

static inline enum link_training_result dp_transition_to_video_idle(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings,
	enum link_training_result status)
{
	union lane_count_set lane_count_set = {0};

	/* 4. mainlink output idle pattern*/
	dp_set_hw_test_pattern(link, link_res, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

	/*
	 * 5. post training adjust if required
	 * If the upstream DPTX and downstream DPRX both support TPS4,
	 * TPS4 must be used instead of POST_LT_ADJ_REQ.
	 */
	if (link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED != 1 ||
			lt_settings->pattern_for_eq >= DP_TRAINING_PATTERN_SEQUENCE_4) {
		/* delay 5ms after Main Link output idle pattern and then check
		 * DPCD 0202h.
		 */
		if (link->connector_signal != SIGNAL_TYPE_EDP && status == LINK_TRAINING_SUCCESS) {
			msleep(5);
			status = dp_check_link_loss_status(link, lt_settings);
		}
		return status;
	}

	if (status == LINK_TRAINING_SUCCESS &&
		perform_post_lt_adj_req_sequence(link, link_res, lt_settings) == false)
		status = LINK_TRAINING_LQA_FAIL;

	lane_count_set.bits.LANE_COUNT_SET = lt_settings->link_settings.lane_count;
	lane_count_set.bits.ENHANCED_FRAMING = lt_settings->enhanced_framing;
	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED = 0;

	core_link_write_dpcd(
		link,
		DP_LANE_COUNT_SET,
		&lane_count_set.raw,
		sizeof(lane_count_set));

	return status;
}

enum link_training_result dp_check_link_loss_status(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	union lane_status lane_status;
	uint8_t dpcd_buf[6] = {0};
	uint32_t lane;

	core_link_read_dpcd(
			link,
			DP_SINK_COUNT,
			(uint8_t *)(dpcd_buf),
			sizeof(dpcd_buf));

	/*parse lane status*/
	for (lane = 0; lane < link->cur_link_settings.lane_count; lane++) {
		/*
		 * check lanes status
		 */
		lane_status.raw = get_nibble_at_index(&dpcd_buf[2], lane);

		if (!lane_status.bits.CHANNEL_EQ_DONE_0 ||
			!lane_status.bits.CR_DONE_0 ||
			!lane_status.bits.SYMBOL_LOCKED_0) {
			/* if one of the channel equalization, clock
			 * recovery or symbol lock is dropped
			 * consider it as (link has been
			 * dropped) dp sink status has changed
			 */
			status = LINK_TRAINING_LINK_LOSS;
			break;
		}
	}

	return status;
}

static inline void decide_8b_10b_training_settings(
	 struct dc_link *link,
	const struct dc_link_settings *link_setting,
	struct link_training_settings *lt_settings)
{
	memset(lt_settings, '\0', sizeof(struct link_training_settings));

	/* Initialize link settings */
	lt_settings->link_settings.use_link_rate_set = link_setting->use_link_rate_set;
	lt_settings->link_settings.link_rate_set = link_setting->link_rate_set;
	lt_settings->link_settings.link_rate = link_setting->link_rate;
	lt_settings->link_settings.lane_count = link_setting->lane_count;
	/* TODO hard coded to SS for now
	 * lt_settings.link_settings.link_spread =
	 * dal_display_path_is_ss_supported(
	 * path_mode->display_path) ?
	 * LINK_SPREAD_05_DOWNSPREAD_30KHZ :
	 * LINK_SPREAD_DISABLED;
	 */
	lt_settings->link_settings.link_spread = link->dp_ss_off ?
			LINK_SPREAD_DISABLED : LINK_SPREAD_05_DOWNSPREAD_30KHZ;
	lt_settings->lttpr_mode = link->lttpr_mode;
	lt_settings->cr_pattern_time = get_cr_training_aux_rd_interval(link, link_setting);
	lt_settings->eq_pattern_time = get_eq_training_aux_rd_interval(link, link_setting);
	lt_settings->pattern_for_cr = decide_cr_training_pattern(link_setting);
	lt_settings->pattern_for_eq = decide_eq_training_pattern(link, link_setting);
	lt_settings->enhanced_framing = 1;
	lt_settings->should_set_fec_ready = true;
	lt_settings->disallow_per_lane_settings = true;
	lt_settings->always_match_dpcd_with_hw_lane_settings = true;
	dp_hw_to_dpcd_lane_settings(lt_settings, lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
}

static inline void decide_128b_132b_training_settings(struct dc_link *link,
		const struct dc_link_settings *link_settings,
		struct link_training_settings *lt_settings)
{
	memset(lt_settings, 0, sizeof(*lt_settings));

	lt_settings->link_settings = *link_settings;
	/* TODO: should decide link spread when populating link_settings */
	lt_settings->link_settings.link_spread = link->dp_ss_off ? LINK_SPREAD_DISABLED :
			LINK_SPREAD_05_DOWNSPREAD_30KHZ;

	lt_settings->pattern_for_cr = decide_cr_training_pattern(link_settings);
	lt_settings->pattern_for_eq = decide_eq_training_pattern(link, link_settings);
	lt_settings->eq_pattern_time = 2500;
	lt_settings->eq_wait_time_limit = 400000;
	lt_settings->eq_loop_count_limit = 20;
	lt_settings->pattern_for_cds = DP_128b_132b_TPS2_CDS;
	lt_settings->cds_pattern_time = 2500;
	lt_settings->cds_wait_time_limit = (dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt) + 1) * 20000;
	lt_settings->lttpr_mode = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) ?
			LTTPR_MODE_NON_TRANSPARENT : LTTPR_MODE_TRANSPARENT;
	lt_settings->disallow_per_lane_settings = true;
	dp_hw_to_dpcd_lane_settings(lt_settings,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
}

void dp_decide_training_settings(
		struct dc_link *link,
		const struct dc_link_settings *link_settings,
		struct link_training_settings *lt_settings)
{
	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING)
		decide_8b_10b_training_settings(link, link_settings, lt_settings);
	else if (dp_get_link_encoding_format(link_settings) == DP_128b_132b_ENCODING)
		decide_128b_132b_training_settings(link, link_settings, lt_settings);
}

static void override_training_settings(
		struct dc_link *link,
		const struct dc_link_training_overrides *overrides,
		struct link_training_settings *lt_settings)
{
	uint32_t lane;

	/* Override link spread */
	if (!link->dp_ss_off && overrides->downspread != NULL)
		lt_settings->link_settings.link_spread = *overrides->downspread ?
				LINK_SPREAD_05_DOWNSPREAD_30KHZ
				: LINK_SPREAD_DISABLED;

	/* Override lane settings */
	if (overrides->voltage_swing != NULL)
		lt_settings->voltage_swing = overrides->voltage_swing;
	if (overrides->pre_emphasis != NULL)
		lt_settings->pre_emphasis = overrides->pre_emphasis;
	if (overrides->post_cursor2 != NULL)
		lt_settings->post_cursor2 = overrides->post_cursor2;
	if (overrides->ffe_preset != NULL)
		lt_settings->ffe_preset = overrides->ffe_preset;
	/* Override HW lane settings with BIOS forced values if present */
	if (link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN &&
			link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
		lt_settings->voltage_swing = &link->bios_forced_drive_settings.VOLTAGE_SWING;
		lt_settings->pre_emphasis = &link->bios_forced_drive_settings.PRE_EMPHASIS;
		lt_settings->always_match_dpcd_with_hw_lane_settings = false;
	}
	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		lt_settings->lane_settings[lane].VOLTAGE_SWING =
			lt_settings->voltage_swing != NULL ?
			*lt_settings->voltage_swing :
			VOLTAGE_SWING_LEVEL0;
		lt_settings->lane_settings[lane].PRE_EMPHASIS =
			lt_settings->pre_emphasis != NULL ?
			*lt_settings->pre_emphasis
			: PRE_EMPHASIS_DISABLED;
		lt_settings->lane_settings[lane].POST_CURSOR2 =
			lt_settings->post_cursor2 != NULL ?
			*lt_settings->post_cursor2
			: POST_CURSOR2_DISABLED;
	}

	dp_hw_to_dpcd_lane_settings(lt_settings,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);

	/* Initialize training timings */
	if (overrides->cr_pattern_time != NULL)
		lt_settings->cr_pattern_time = *overrides->cr_pattern_time;

	if (overrides->eq_pattern_time != NULL)
		lt_settings->eq_pattern_time = *overrides->eq_pattern_time;

	if (overrides->pattern_for_cr != NULL)
		lt_settings->pattern_for_cr = *overrides->pattern_for_cr;
	if (overrides->pattern_for_eq != NULL)
		lt_settings->pattern_for_eq = *overrides->pattern_for_eq;

	if (overrides->enhanced_framing != NULL)
		lt_settings->enhanced_framing = *overrides->enhanced_framing;

	if (link->preferred_training_settings.fec_enable != NULL)
		lt_settings->should_set_fec_ready = *link->preferred_training_settings.fec_enable;
}

uint8_t dp_convert_to_count(uint8_t lttpr_repeater_count)
{
	switch (lttpr_repeater_count) {
	case 0x80: // 1 lttpr repeater
		return 1;
	case 0x40: // 2 lttpr repeaters
		return 2;
	case 0x20: // 3 lttpr repeaters
		return 3;
	case 0x10: // 4 lttpr repeaters
		return 4;
	case 0x08: // 5 lttpr repeaters
		return 5;
	case 0x04: // 6 lttpr repeaters
		return 6;
	case 0x02: // 7 lttpr repeaters
		return 7;
	case 0x01: // 8 lttpr repeaters
		return 8;
	default:
		break;
	}
	return 0; // invalid value
}

static enum dc_status configure_lttpr_mode_transparent(struct dc_link *link)
{
	uint8_t repeater_mode = DP_PHY_REPEATER_MODE_TRANSPARENT;

	DC_LOG_HW_LINK_TRAINING("%s\n Set LTTPR to Transparent Mode\n", __func__);
	return core_link_write_dpcd(link,
			DP_PHY_REPEATER_MODE,
			(uint8_t *)&repeater_mode,
			sizeof(repeater_mode));
}

static enum dc_status configure_lttpr_mode_non_transparent(
		struct dc_link *link,
		const struct link_training_settings *lt_settings)
{
	/* aux timeout is already set to extended */
	/* RESET/SET lttpr mode to enable non transparent mode */
	uint8_t repeater_cnt;
	uint32_t aux_interval_address;
	uint8_t repeater_id;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	uint8_t repeater_mode = DP_PHY_REPEATER_MODE_TRANSPARENT;

	enum dp_link_encoding encoding = dp_get_link_encoding_format(&lt_settings->link_settings);

	if (encoding == DP_8b_10b_ENCODING) {
		DC_LOG_HW_LINK_TRAINING("%s\n Set LTTPR to Transparent Mode\n", __func__);
		result = core_link_write_dpcd(link,
				DP_PHY_REPEATER_MODE,
				(uint8_t *)&repeater_mode,
				sizeof(repeater_mode));

	}

	if (result == DC_OK) {
		link->dpcd_caps.lttpr_caps.mode = repeater_mode;
	}

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {

		DC_LOG_HW_LINK_TRAINING("%s\n Set LTTPR to Non Transparent Mode\n", __func__);

		repeater_mode = DP_PHY_REPEATER_MODE_NON_TRANSPARENT;
		result = core_link_write_dpcd(link,
				DP_PHY_REPEATER_MODE,
				(uint8_t *)&repeater_mode,
				sizeof(repeater_mode));

		if (result == DC_OK) {
			link->dpcd_caps.lttpr_caps.mode = repeater_mode;
		}

		if (encoding == DP_8b_10b_ENCODING) {
			repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

			/* Driver does not need to train the first hop. Skip DPCD read and clear
			 * AUX_RD_INTERVAL for DPTX-to-DPIA hop.
			 */
			if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
				link->dpcd_caps.lttpr_caps.aux_rd_interval[--repeater_cnt] = 0;

			for (repeater_id = repeater_cnt; repeater_id > 0; repeater_id--) {
				aux_interval_address = DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1 +
							((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (repeater_id - 1));
				core_link_read_dpcd(
					link,
					aux_interval_address,
					(uint8_t *)&link->dpcd_caps.lttpr_caps.aux_rd_interval[repeater_id - 1],
					sizeof(link->dpcd_caps.lttpr_caps.aux_rd_interval[repeater_id - 1]));
				link->dpcd_caps.lttpr_caps.aux_rd_interval[repeater_id - 1] &= 0x7F;
			}
		}
	}

	return result;
}

static void repeater_training_done(struct dc_link *link, uint32_t offset)
{
	union dpcd_training_pattern dpcd_pattern = {0};

	const uint32_t dpcd_base_lt_offset =
			DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
	/* Set training not in progress*/
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET = DPCD_TRAINING_PATTERN_VIDEOIDLE;

	core_link_write_dpcd(
		link,
		dpcd_base_lt_offset,
		&dpcd_pattern.raw,
		1);

	DC_LOG_HW_LINK_TRAINING("%s\n LTTPR Id: %d 0x%X pattern = %x\n",
		__func__,
		offset,
		dpcd_base_lt_offset,
		dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
}

static void print_status_message(
	struct dc_link *link,
	const struct link_training_settings *lt_settings,
	enum link_training_result status)
{
	char *link_rate = "Unknown";
	char *lt_result = "Unknown";
	char *lt_spread = "Disabled";

	switch (lt_settings->link_settings.link_rate) {
	case LINK_RATE_LOW:
		link_rate = "RBR";
		break;
	case LINK_RATE_RATE_2:
		link_rate = "R2";
		break;
	case LINK_RATE_RATE_3:
		link_rate = "R3";
		break;
	case LINK_RATE_HIGH:
		link_rate = "HBR";
		break;
	case LINK_RATE_RBR2:
		link_rate = "RBR2";
		break;
	case LINK_RATE_RATE_6:
		link_rate = "R6";
		break;
	case LINK_RATE_HIGH2:
		link_rate = "HBR2";
		break;
	case LINK_RATE_HIGH3:
		link_rate = "HBR3";
		break;
	case LINK_RATE_UHBR10:
		link_rate = "UHBR10";
		break;
	case LINK_RATE_UHBR13_5:
		link_rate = "UHBR13.5";
		break;
	case LINK_RATE_UHBR20:
		link_rate = "UHBR20";
		break;
	default:
		break;
	}

	switch (status) {
	case LINK_TRAINING_SUCCESS:
		lt_result = "pass";
		break;
	case LINK_TRAINING_CR_FAIL_LANE0:
		lt_result = "CR failed lane0";
		break;
	case LINK_TRAINING_CR_FAIL_LANE1:
		lt_result = "CR failed lane1";
		break;
	case LINK_TRAINING_CR_FAIL_LANE23:
		lt_result = "CR failed lane23";
		break;
	case LINK_TRAINING_EQ_FAIL_CR:
		lt_result = "CR failed in EQ";
		break;
	case LINK_TRAINING_EQ_FAIL_EQ:
		lt_result = "EQ failed";
		break;
	case LINK_TRAINING_LQA_FAIL:
		lt_result = "LQA failed";
		break;
	case LINK_TRAINING_LINK_LOSS:
		lt_result = "Link loss";
		break;
	case DP_128b_132b_LT_FAILED:
		lt_result = "LT_FAILED received";
		break;
	case DP_128b_132b_MAX_LOOP_COUNT_REACHED:
		lt_result = "max loop count reached";
		break;
	case DP_128b_132b_CHANNEL_EQ_DONE_TIMEOUT:
		lt_result = "channel EQ timeout";
		break;
	case DP_128b_132b_CDS_DONE_TIMEOUT:
		lt_result = "CDS timeout";
		break;
	default:
		break;
	}

	switch (lt_settings->link_settings.link_spread) {
	case LINK_SPREAD_DISABLED:
		lt_spread = "Disabled";
		break;
	case LINK_SPREAD_05_DOWNSPREAD_30KHZ:
		lt_spread = "0.5% 30KHz";
		break;
	case LINK_SPREAD_05_DOWNSPREAD_33KHZ:
		lt_spread = "0.5% 33KHz";
		break;
	default:
		break;
	}

	/* Connectivity log: link training */

	/* TODO - DP2.0 Log: add connectivity log for FFE PRESET */

	CONN_MSG_LT(link, "%sx%d %s VS=%d, PE=%d, DS=%s",
				link_rate,
				lt_settings->link_settings.lane_count,
				lt_result,
				lt_settings->lane_settings[0].VOLTAGE_SWING,
				lt_settings->lane_settings[0].PRE_EMPHASIS,
				lt_spread);
}

void dc_link_dp_set_drive_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	/* program ASIC PHY settings*/
	dp_set_hw_lane_settings(link, link_res, lt_settings, DPRX);

	dp_hw_to_dpcd_lane_settings(lt_settings,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);

	/* Notify DP sink the PHY settings from source */
	dpcd_set_lane_settings(link, lt_settings, DPRX);
}

bool dc_link_dp_perform_link_training_skip_aux(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_setting)
{
	struct link_training_settings lt_settings = {0};

	dp_decide_training_settings(
			link,
			link_setting,
			&lt_settings);
	override_training_settings(
			link,
			&link->preferred_training_settings,
			&lt_settings);

	/* 1. Perform_clock_recovery_sequence. */

	/* transmit training pattern for clock recovery */
	dp_set_hw_training_pattern(link, link_res, lt_settings.pattern_for_cr, DPRX);

	/* call HWSS to set lane settings*/
	dp_set_hw_lane_settings(link, link_res, &lt_settings, DPRX);

	/* wait receiver to lock-on*/
	dp_wait_for_training_aux_rd_interval(link, lt_settings.cr_pattern_time);

	/* 2. Perform_channel_equalization_sequence. */

	/* transmit training pattern for channel equalization. */
	dp_set_hw_training_pattern(link, link_res, lt_settings.pattern_for_eq, DPRX);

	/* call HWSS to set lane settings*/
	dp_set_hw_lane_settings(link, link_res, &lt_settings, DPRX);

	/* wait receiver to lock-on. */
	dp_wait_for_training_aux_rd_interval(link, lt_settings.eq_pattern_time);

	/* 3. Perform_link_training_int. */

	/* Mainlink output idle pattern. */
	dp_set_hw_test_pattern(link, link_res, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

	print_status_message(link, &lt_settings, LINK_TRAINING_SUCCESS);

	return true;
}

enum dc_status dpcd_configure_lttpr_mode(struct dc_link *link, struct link_training_settings *lt_settings)
{
	enum dc_status status = DC_OK;

	if (lt_settings->lttpr_mode == LTTPR_MODE_TRANSPARENT)
		status = configure_lttpr_mode_transparent(link);

	else if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		status = configure_lttpr_mode_non_transparent(link, lt_settings);

	return status;
}

static void dpcd_exit_training_mode(struct dc_link *link)
{
	uint8_t sink_status = 0;
	uint8_t i;

	/* clear training pattern set */
	dpcd_set_training_pattern(link, DP_TRAINING_PATTERN_VIDEOIDLE);

	/* poll for intra-hop disable */
	for (i = 0; i < 10; i++) {
		if ((core_link_read_dpcd(link, DP_SINK_STATUS, &sink_status, 1) == DC_OK) &&
				(sink_status & DP_INTRA_HOP_AUX_REPLY_INDICATION) == 0)
			break;
		udelay(1000);
	}
}

enum dc_status dpcd_configure_channel_coding(struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	enum dp_link_encoding encoding =
			dp_get_link_encoding_format(
					&lt_settings->link_settings);
	enum dc_status status;

	status = core_link_write_dpcd(
			link,
			DP_MAIN_LINK_CHANNEL_CODING_SET,
			(uint8_t *) &encoding,
			1);
	DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X MAIN_LINK_CHANNEL_CODING_SET = %x\n",
					__func__,
					DP_MAIN_LINK_CHANNEL_CODING_SET,
					encoding);

	return status;
}

static void dpcd_128b_132b_get_aux_rd_interval(struct dc_link *link,
		uint32_t *interval_in_us)
{
	union dp_128b_132b_training_aux_rd_interval dpcd_interval;
	uint32_t interval_unit = 0;

	dpcd_interval.raw = 0;
	core_link_read_dpcd(link, DP_128b_132b_TRAINING_AUX_RD_INTERVAL,
			&dpcd_interval.raw, sizeof(dpcd_interval.raw));
	interval_unit = dpcd_interval.bits.UNIT ? 1 : 2; /* 0b = 2 ms, 1b = 1 ms */
	/* (128b/132b_TRAINING_AUX_RD_INTERVAL value + 1) *
	 * INTERVAL_UNIT. The maximum is 256 ms
	 */
	*interval_in_us = (dpcd_interval.bits.VALUE + 1) * interval_unit * 1000;
}

static enum link_training_result dp_perform_128b_132b_channel_eq_done_sequence(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	uint8_t loop_count;
	uint32_t aux_rd_interval = 0;
	uint32_t wait_time = 0;
	union lane_align_status_updated dpcd_lane_status_updated = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

	/* Transmit 128b/132b_TPS1 over Main-Link */
	dp_set_hw_training_pattern(link, link_res, lt_settings->pattern_for_cr, DPRX);
	/* Set TRAINING_PATTERN_SET to 01h */
	dpcd_set_training_pattern(link, lt_settings->pattern_for_cr);

	/* Adjust TX_FFE_PRESET_VALUE and Transmit 128b/132b_TPS2 over Main-Link */
	dpcd_128b_132b_get_aux_rd_interval(link, &aux_rd_interval);
	dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
			&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
	dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
	dp_set_hw_lane_settings(link, link_res, lt_settings, DPRX);
	dp_set_hw_training_pattern(link, link_res, lt_settings->pattern_for_eq, DPRX);

	/* Set loop counter to start from 1 */
	loop_count = 1;

	/* Set TRAINING_PATTERN_SET to 02h and TX_FFE_PRESET_VALUE in one AUX transaction */
	dpcd_set_lt_pattern_and_lane_settings(link, lt_settings,
			lt_settings->pattern_for_eq, DPRX);

	/* poll for channel EQ done */
	while (status == LINK_TRAINING_SUCCESS) {
		dp_wait_for_training_aux_rd_interval(link, aux_rd_interval);
		wait_time += aux_rd_interval;
		dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
				&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
		dpcd_128b_132b_get_aux_rd_interval(link, &aux_rd_interval);
		if (dp_is_ch_eq_done(lt_settings->link_settings.lane_count,
				dpcd_lane_status)) {
			/* pass */
			break;
		} else if (loop_count >= lt_settings->eq_loop_count_limit) {
			status = DP_128b_132b_MAX_LOOP_COUNT_REACHED;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			status = DP_128b_132b_LT_FAILED;
		} else {
			dp_set_hw_lane_settings(link, link_res, lt_settings, DPRX);
			dpcd_set_lane_settings(link, lt_settings, DPRX);
		}
		loop_count++;
	}

	/* poll for EQ interlane align done */
	while (status == LINK_TRAINING_SUCCESS) {
		if (dpcd_lane_status_updated.bits.EQ_INTERLANE_ALIGN_DONE_128b_132b) {
			/* pass */
			break;
		} else if (wait_time >= lt_settings->eq_wait_time_limit) {
			status = DP_128b_132b_CHANNEL_EQ_DONE_TIMEOUT;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			status = DP_128b_132b_LT_FAILED;
		} else {
			dp_wait_for_training_aux_rd_interval(link,
					lt_settings->eq_pattern_time);
			wait_time += lt_settings->eq_pattern_time;
			dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
					&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		}
	}

	return status;
}

static enum link_training_result dp_perform_128b_132b_cds_done_sequence(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	/* Assumption: assume hardware has transmitted eq pattern */
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	union lane_align_status_updated dpcd_lane_status_updated = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = { { {0} } };
	uint32_t wait_time = 0;

	/* initiate CDS done sequence */
	dpcd_set_training_pattern(link, lt_settings->pattern_for_cds);

	/* poll for CDS interlane align done and symbol lock */
	while (status == LINK_TRAINING_SUCCESS) {
		dp_wait_for_training_aux_rd_interval(link,
				lt_settings->cds_pattern_time);
		wait_time += lt_settings->cds_pattern_time;
		dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
						&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		if (dp_is_symbol_locked(lt_settings->link_settings.lane_count, dpcd_lane_status) &&
				dpcd_lane_status_updated.bits.CDS_INTERLANE_ALIGN_DONE_128b_132b) {
			/* pass */
			break;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			status = DP_128b_132b_LT_FAILED;
		} else if (wait_time >= lt_settings->cds_wait_time_limit) {
			status = DP_128b_132b_CDS_DONE_TIMEOUT;
		}
	}

	return status;
}

static enum link_training_result dp_perform_8b_10b_link_training(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;

	uint8_t repeater_cnt;
	uint8_t repeater_id;
	uint8_t lane = 0;

	if (link->ctx->dc->work_arounds.lt_early_cr_pattern)
		start_clock_recovery_pattern_early(link, link_res, lt_settings, DPRX);

	/* 1. set link rate, lane count and spread. */
	dpcd_set_link_settings(link, lt_settings);

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {

		/* 2. perform link training (set link training done
		 *  to false is done as well)
		 */
		repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

		for (repeater_id = repeater_cnt; (repeater_id > 0 && status == LINK_TRAINING_SUCCESS);
				repeater_id--) {
			status = perform_clock_recovery_sequence(link, link_res, lt_settings, repeater_id);

			if (status != LINK_TRAINING_SUCCESS)
				break;

			status = perform_channel_equalization_sequence(link,
					link_res,
					lt_settings,
					repeater_id);

			if (status != LINK_TRAINING_SUCCESS)
				break;

			repeater_training_done(link, repeater_id);
		}

		for (lane = 0; lane < (uint8_t)lt_settings->link_settings.lane_count; lane++)
			lt_settings->dpcd_lane_settings[lane].raw = 0;
	}

	if (status == LINK_TRAINING_SUCCESS) {
		status = perform_clock_recovery_sequence(link, link_res, lt_settings, DPRX);
	if (status == LINK_TRAINING_SUCCESS) {
		status = perform_channel_equalization_sequence(link,
					link_res,
					lt_settings,
					DPRX);
		}
	}

	return status;
}

static enum link_training_result dp_perform_128b_132b_link_training(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	enum link_training_result result = LINK_TRAINING_SUCCESS;

	/* TODO - DP2.0 Link: remove legacy_dp2_lt logic */
	if (link->dc->debug.legacy_dp2_lt) {
		struct link_training_settings legacy_settings;

		decide_8b_10b_training_settings(link,
				&lt_settings->link_settings,
				&legacy_settings);
		return dp_perform_8b_10b_link_training(link, link_res, &legacy_settings);
	}

	dpcd_set_link_settings(link, lt_settings);

	if (result == LINK_TRAINING_SUCCESS)
		result = dp_perform_128b_132b_channel_eq_done_sequence(link, link_res, lt_settings);

	if (result == LINK_TRAINING_SUCCESS)
		result = dp_perform_128b_132b_cds_done_sequence(link, link_res, lt_settings);

	return result;
}

static enum link_training_result dc_link_dp_perform_fixed_vs_pe_training_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	const uint8_t vendor_lttpr_write_data_reset[4] = {0x1, 0x50, 0x63, 0xFF};
	const uint8_t offset = dp_convert_to_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	const uint8_t vendor_lttpr_write_data_intercept_en[4] = {0x1, 0x55, 0x63, 0x0};
	const uint8_t vendor_lttpr_write_data_intercept_dis[4] = {0x1, 0x55, 0x63, 0x68};
	uint8_t vendor_lttpr_write_data_vs[4] = {0x1, 0x51, 0x63, 0x0};
	uint8_t vendor_lttpr_write_data_pe[4] = {0x1, 0x52, 0x63, 0x0};
	uint32_t vendor_lttpr_write_address = 0xF004F;
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	uint8_t lane = 0;
	union down_spread_ctrl downspread = {0};
	union lane_count_set lane_count_set = {0};
	uint8_t toggle_rate;
	uint8_t rate;

	/* Only 8b/10b is supported */
	ASSERT(dp_get_link_encoding_format(&lt_settings->link_settings) ==
			DP_8b_10b_ENCODING);

	if (offset != 0xFF) {
		vendor_lttpr_write_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
	}

	/* Vendor specific: Reset lane settings */
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_reset[0],
			sizeof(vendor_lttpr_write_data_reset));
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_vs[0],
			sizeof(vendor_lttpr_write_data_vs));
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_pe[0],
			sizeof(vendor_lttpr_write_data_pe));

	/* Vendor specific: Enable intercept */
	core_link_write_dpcd(
			link,
			vendor_lttpr_write_address,
			&vendor_lttpr_write_data_intercept_en[0],
			sizeof(vendor_lttpr_write_data_intercept_en));

	/* 1. set link rate, lane count and spread. */

	downspread.raw = (uint8_t)(lt_settings->link_settings.link_spread);

	lane_count_set.bits.LANE_COUNT_SET =
	lt_settings->link_settings.lane_count;

	lane_count_set.bits.ENHANCED_FRAMING = lt_settings->enhanced_framing;
	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED = 0;


	if (lt_settings->pattern_for_eq < DP_TRAINING_PATTERN_SEQUENCE_4) {
		lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED =
				link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED;
	}

	core_link_write_dpcd(link, DP_DOWNSPREAD_CTRL,
		&downspread.raw, sizeof(downspread));

	core_link_write_dpcd(link, DP_LANE_COUNT_SET,
		&lane_count_set.raw, 1);

	rate = get_dpcd_link_rate(&lt_settings->link_settings);

	/* Vendor specific: Toggle link rate */
	toggle_rate = (rate == 0x6) ? 0xA : 0x6;

	if (link->vendor_specific_lttpr_link_rate_wa == rate) {
		core_link_write_dpcd(
				link,
				DP_LINK_BW_SET,
				&toggle_rate,
				1);
	}

	link->vendor_specific_lttpr_link_rate_wa = rate;

	core_link_write_dpcd(link, DP_LINK_BW_SET, &rate, 1);

	DC_LOG_HW_LINK_TRAINING("%s\n %x rate = %x\n %x lane = %x framing = %x\n %x spread = %x\n",
		__func__,
		DP_LINK_BW_SET,
		lt_settings->link_settings.link_rate,
		DP_LANE_COUNT_SET,
		lt_settings->link_settings.lane_count,
		lt_settings->enhanced_framing,
		DP_DOWNSPREAD_CTRL,
		lt_settings->link_settings.link_spread);

	/* 2. Perform link training */

	/* Perform Clock Recovery Sequence */
	if (status == LINK_TRAINING_SUCCESS) {
		uint32_t retries_cr;
		uint32_t retry_count;
		uint32_t wait_time_microsec;
		enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
		union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
		union lane_align_status_updated dpcd_lane_status_updated;
		union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

		retries_cr = 0;
		retry_count = 0;

		while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
			(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {

			memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
			memset(&dpcd_lane_status_updated, '\0',
			sizeof(dpcd_lane_status_updated));

			/* 1. call HWSS to set lane settings */
			dp_set_hw_lane_settings(
					link,
					link_res,
					lt_settings,
					0);

			/* 2. update DPCD of the receiver */
			if (!retry_count) {
				/* EPR #361076 - write as a 5-byte burst,
				 * but only for the 1-st iteration.
				 */
				dpcd_set_lt_pattern_and_lane_settings(
						link,
						lt_settings,
						lt_settings->pattern_for_cr,
						0);
				/* Vendor specific: Disable intercept */
				core_link_write_dpcd(
						link,
						vendor_lttpr_write_address,
						&vendor_lttpr_write_data_intercept_dis[0],
						sizeof(vendor_lttpr_write_data_intercept_dis));
			} else {
				vendor_lttpr_write_data_vs[3] = 0;
				vendor_lttpr_write_data_pe[3] = 0;

				for (lane = 0; lane < lane_count; lane++) {
					vendor_lttpr_write_data_vs[3] |=
							lt_settings->dpcd_lane_settings[lane].bits.VOLTAGE_SWING_SET << (2 * lane);
					vendor_lttpr_write_data_pe[3] |=
							lt_settings->dpcd_lane_settings[lane].bits.PRE_EMPHASIS_SET << (2 * lane);
				}

				/* Vendor specific: Update VS and PE to DPRX requested value */
				core_link_write_dpcd(
						link,
						vendor_lttpr_write_address,
						&vendor_lttpr_write_data_vs[0],
						sizeof(vendor_lttpr_write_data_vs));
				core_link_write_dpcd(
						link,
						vendor_lttpr_write_address,
						&vendor_lttpr_write_data_pe[0],
						sizeof(vendor_lttpr_write_data_pe));

				dpcd_set_lane_settings(
						link,
						lt_settings,
						0);
			}

			/* 3. wait receiver to lock-on*/
			wait_time_microsec = lt_settings->cr_pattern_time;

			dp_wait_for_training_aux_rd_interval(
					link,
					wait_time_microsec);

			/* 4. Read lane status and requested drive
			 * settings as set by the sink
			 */
			dp_get_lane_status_and_lane_adjust(
					link,
					lt_settings,
					dpcd_lane_status,
					&dpcd_lane_status_updated,
					dpcd_lane_adjust,
					0);

			/* 5. check CR done*/
			if (dp_is_cr_done(lane_count, dpcd_lane_status)) {
				status = LINK_TRAINING_SUCCESS;
				break;
			}

			/* 6. max VS reached*/
			if (dp_is_max_vs_reached(lt_settings))
				break;

			/* 7. same lane settings */
			/* Note: settings are the same for all lanes,
			 * so comparing first lane is sufficient
			 */
			if (lt_settings->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET ==
					dpcd_lane_adjust[0].bits.VOLTAGE_SWING_LANE)
				retries_cr++;
			else
				retries_cr = 0;

			/* 8. update VS/PE/PC2 in lt_settings*/
			dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
					lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
			retry_count++;
		}

		if (retry_count >= LINK_TRAINING_MAX_CR_RETRY) {
			ASSERT(0);
			DC_LOG_ERROR("%s: Link Training Error, could not get CR after %d tries. Possibly voltage swing issue",
				__func__,
				LINK_TRAINING_MAX_CR_RETRY);

		}

		status = dp_get_cr_failure(lane_count, dpcd_lane_status);
	}

	/* Perform Channel EQ Sequence */
	if (status == LINK_TRAINING_SUCCESS) {
		enum dc_dp_training_pattern tr_pattern;
		uint32_t retries_ch_eq;
		uint32_t wait_time_microsec;
		enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
		union lane_align_status_updated dpcd_lane_status_updated = {0};
		union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
		union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

		/* Note: also check that TPS4 is a supported feature*/
		tr_pattern = lt_settings->pattern_for_eq;

		dp_set_hw_training_pattern(link, link_res, tr_pattern, 0);

		status = LINK_TRAINING_EQ_FAIL_EQ;

		for (retries_ch_eq = 0; retries_ch_eq <= LINK_TRAINING_MAX_RETRY_COUNT;
			retries_ch_eq++) {

			dp_set_hw_lane_settings(link, link_res, lt_settings, 0);

			vendor_lttpr_write_data_vs[3] = 0;
			vendor_lttpr_write_data_pe[3] = 0;

			for (lane = 0; lane < lane_count; lane++) {
				vendor_lttpr_write_data_vs[3] |=
						lt_settings->dpcd_lane_settings[lane].bits.VOLTAGE_SWING_SET << (2 * lane);
				vendor_lttpr_write_data_pe[3] |=
						lt_settings->dpcd_lane_settings[lane].bits.PRE_EMPHASIS_SET << (2 * lane);
			}

			/* Vendor specific: Update VS and PE to DPRX requested value */
			core_link_write_dpcd(
					link,
					vendor_lttpr_write_address,
					&vendor_lttpr_write_data_vs[0],
					sizeof(vendor_lttpr_write_data_vs));
			core_link_write_dpcd(
					link,
					vendor_lttpr_write_address,
					&vendor_lttpr_write_data_pe[0],
					sizeof(vendor_lttpr_write_data_pe));

			/* 2. update DPCD*/
			if (!retries_ch_eq)
				/* EPR #361076 - write as a 5-byte burst,
				 * but only for the 1-st iteration
				 */

				dpcd_set_lt_pattern_and_lane_settings(
					link,
					lt_settings,
					tr_pattern, 0);
			else
				dpcd_set_lane_settings(link, lt_settings, 0);

			/* 3. wait for receiver to lock-on*/
			wait_time_microsec = lt_settings->eq_pattern_time;

			dp_wait_for_training_aux_rd_interval(
					link,
					wait_time_microsec);

			/* 4. Read lane status and requested
			 * drive settings as set by the sink
			 */
			dp_get_lane_status_and_lane_adjust(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				dpcd_lane_adjust,
				0);

			/* 5. check CR done*/
			if (!dp_is_cr_done(lane_count, dpcd_lane_status)) {
				status = LINK_TRAINING_EQ_FAIL_CR;
				break;
			}

			/* 6. check CHEQ done*/
			if (dp_is_ch_eq_done(lane_count, dpcd_lane_status) &&
					dp_is_symbol_locked(lane_count, dpcd_lane_status) &&
					dp_is_interlane_aligned(dpcd_lane_status_updated)) {
				status = LINK_TRAINING_SUCCESS;
				break;
			}

			/* 7. update VS/PE/PC2 in lt_settings*/
			dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
					lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
		}
	}

	return status;
}


enum link_training_result dc_link_dp_perform_link_training(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_settings,
	bool skip_video_pattern)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	struct link_training_settings lt_settings = {0};
	enum dp_link_encoding encoding =
			dp_get_link_encoding_format(link_settings);

	/* decide training settings */
	dp_decide_training_settings(
			link,
			link_settings,
			&lt_settings);
	override_training_settings(
			link,
			&link->preferred_training_settings,
			&lt_settings);

	/* reset previous training states */
	if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
			(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
		link->apply_vendor_specific_lttpr_link_rate_wa = true;
		vendor_specific_lttpr_wa_four(link, true);
	} else {
		dpcd_exit_training_mode(link);
	}

	/* configure link prior to entering training mode */
	dpcd_configure_lttpr_mode(link, &lt_settings);
	dp_set_fec_ready(link, link_res, lt_settings.should_set_fec_ready);
	dpcd_configure_channel_coding(link, &lt_settings);

	/* enter training mode:
	 * Per DP specs starting from here, DPTX device shall not issue
	 * Non-LT AUX transactions inside training mode.
	 */
	if (!link->dc->debug.apply_vendor_specific_lttpr_wa &&
			(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link->lttpr_mode == LTTPR_MODE_TRANSPARENT)
		status = dc_link_dp_perform_fixed_vs_pe_training_sequence(link, link_res, &lt_settings);
	else if (encoding == DP_8b_10b_ENCODING)
		status = dp_perform_8b_10b_link_training(link, link_res, &lt_settings);
	else if (encoding == DP_128b_132b_ENCODING)
		status = dp_perform_128b_132b_link_training(link, link_res, &lt_settings);
	else
		ASSERT(0);

	/* exit training mode */
	if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
			(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
		link->apply_vendor_specific_lttpr_link_rate_wa = false;
		vendor_specific_lttpr_wa_four(link, (status != LINK_TRAINING_SUCCESS));
	} else {
		dpcd_exit_training_mode(link);
	}

	/* switch to video idle */
	if ((status == LINK_TRAINING_SUCCESS) || !skip_video_pattern)
		status = dp_transition_to_video_idle(link,
				link_res,
				&lt_settings,
				status);

	/* dump debug data */
	print_status_message(link, &lt_settings, status);
	if (status != LINK_TRAINING_SUCCESS)
		link->ctx->dc->debug_data.ltFailCount++;
	return status;
}

bool perform_link_training_with_retries(
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern,
	int attempts,
	struct pipe_ctx *pipe_ctx,
	enum signal_type signal,
	bool do_fallback)
{
	int j;
	uint8_t delay_between_attempts = LINK_TRAINING_RETRY_DELAY;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	enum dp_panel_mode panel_mode = dp_get_panel_mode(link);
	enum link_training_result status = LINK_TRAINING_CR_FAIL_LANE0;
	struct dc_link_settings current_setting = *link_setting;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);

	if (dp_get_link_encoding_format(&current_setting) == DP_8b_10b_ENCODING)
		/* We need to do this before the link training to ensure the idle
		 * pattern in SST mode will be sent right after the link training
		 */
		link_hwss->setup_stream_encoder(pipe_ctx);

	for (j = 0; j < attempts; ++j) {

		DC_LOG_HW_LINK_TRAINING("%s: Beginning link training attempt %u of %d\n",
			__func__, (unsigned int)j + 1, attempts);

		dp_enable_link_phy(
			link,
			&pipe_ctx->link_res,
			signal,
			pipe_ctx->clock_source->id,
			&current_setting);

		if (stream->sink_patches.dppowerup_delay > 0) {
			int delay_dp_power_up_in_ms = stream->sink_patches.dppowerup_delay;

			msleep(delay_dp_power_up_in_ms);
		}

#ifdef CONFIG_DRM_AMD_DC_HDCP
		if (panel_mode == DP_PANEL_MODE_EDP) {
			struct cp_psp *cp_psp = &stream->ctx->cp_psp;

			if (cp_psp && cp_psp->funcs.enable_assr)
				/* ASSR is bound to fail with unsigned PSP
				 * verstage used during devlopment phase.
				 * Report and continue with eDP panel mode to
				 * perform eDP link training with right settings
				 */
				cp_psp->funcs.enable_assr(cp_psp->handle, link);
		}
#endif

		dp_set_panel_mode(link, panel_mode);

		if (link->aux_access_disabled) {
			dc_link_dp_perform_link_training_skip_aux(link, &pipe_ctx->link_res, &current_setting);
			return true;
		} else {
			/** @todo Consolidate USB4 DP and DPx.x training. */
			if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA) {
				status = dc_link_dpia_perform_link_training(link,
						&pipe_ctx->link_res,
						&current_setting,
						skip_video_pattern);

				/* Transmit idle pattern once training successful. */
				if (status == LINK_TRAINING_SUCCESS)
					dp_set_hw_test_pattern(link, &pipe_ctx->link_res, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);
			} else {
				status = dc_link_dp_perform_link_training(link,
						&pipe_ctx->link_res,
						&current_setting,
						skip_video_pattern);
			}

			if (status == LINK_TRAINING_SUCCESS)
				return true;
		}

		/* latest link training still fail, skip delay and keep PHY on
		 */
		if (j == (attempts - 1) && link->ep_type == DISPLAY_ENDPOINT_PHY)
			break;

		DC_LOG_WARNING("%s: Link training attempt %u of %d failed\n",
			__func__, (unsigned int)j + 1, attempts);

		dp_disable_link_phy(link, &pipe_ctx->link_res, signal);

		/* Abort link training if failure due to sink being unplugged. */
		if (status == LINK_TRAINING_ABORT) {
			enum dc_connection_type type = dc_connection_none;

			dc_link_detect_sink(link, &type);
			if (type == dc_connection_none)
				break;
		} else if (do_fallback) {
			uint32_t req_bw;
			uint32_t link_bw;

			decide_fallback_link_setting(link, *link_setting, &current_setting, status);
			/* Fail link training if reduced link bandwidth no longer meets
			 * stream requirements.
			 */
			req_bw = dc_bandwidth_in_kbps_from_timing(&stream->timing);
			link_bw = dc_link_bandwidth_kbps(link, &current_setting);
			if (req_bw > link_bw)
				break;
		}

		msleep(delay_between_attempts);

		delay_between_attempts += LINK_TRAINING_RETRY_DELAY;
	}

	return false;
}

static enum clock_source_id get_clock_source_id(struct dc_link *link)
{
	enum clock_source_id dp_cs_id = CLOCK_SOURCE_ID_UNDEFINED;
	struct clock_source *dp_cs = link->dc->res_pool->dp_clock_source;

	if (dp_cs != NULL) {
		dp_cs_id = dp_cs->id;
	} else {
		/*
		 * dp clock source is not initialized for some reason.
		 * Should not happen, CLOCK_SOURCE_ID_EXTERNAL will be used
		 */
		ASSERT(dp_cs);
	}

	return dp_cs_id;
}

static void set_dp_mst_mode(struct dc_link *link, const struct link_resource *link_res,
		bool mst_enable)
{
	if (mst_enable == false &&
		link->type == dc_connection_mst_branch) {
		/* Disable MST on link. Use only local sink. */
		dp_disable_link_phy_mst(link, link_res, link->connector_signal);

		link->type = dc_connection_single;
		link->local_sink = link->remote_sinks[0];
		link->local_sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
		dc_sink_retain(link->local_sink);
		dm_helpers_dp_mst_stop_top_mgr(link->ctx, link);
	} else if (mst_enable == true &&
			link->type == dc_connection_single &&
			link->remote_sinks[0] != NULL) {
		/* Re-enable MST on link. */
		dp_disable_link_phy(link, link_res, link->connector_signal);
		dp_enable_mst_on_sink(link, true);

		link->type = dc_connection_mst_branch;
		link->local_sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	}
}

bool dc_link_dp_sync_lt_begin(struct dc_link *link)
{
	/* Begin Sync LT. During this time,
	 * DPCD:600h must not be powered down.
	 */
	link->sync_lt_in_progress = true;

	/*Clear any existing preferred settings.*/
	memset(&link->preferred_training_settings, 0,
		sizeof(struct dc_link_training_overrides));
	memset(&link->preferred_link_setting, 0,
		sizeof(struct dc_link_settings));

	return true;
}

enum link_training_result dc_link_dp_sync_lt_attempt(
    struct dc_link *link,
    const struct link_resource *link_res,
    struct dc_link_settings *link_settings,
    struct dc_link_training_overrides *lt_overrides)
{
	struct link_training_settings lt_settings = {0};
	enum link_training_result lt_status = LINK_TRAINING_SUCCESS;
	enum dp_panel_mode panel_mode = DP_PANEL_MODE_DEFAULT;
	enum clock_source_id dp_cs_id = CLOCK_SOURCE_ID_EXTERNAL;
	bool fec_enable = false;

	dp_decide_training_settings(
			link,
			link_settings,
			&lt_settings);
	override_training_settings(
			link,
			lt_overrides,
			&lt_settings);
	/* Setup MST Mode */
	if (lt_overrides->mst_enable)
		set_dp_mst_mode(link, link_res, *lt_overrides->mst_enable);

	/* Disable link */
	dp_disable_link_phy(link, link_res, link->connector_signal);

	/* Enable link */
	dp_cs_id = get_clock_source_id(link);
	dp_enable_link_phy(link, link_res, link->connector_signal,
		dp_cs_id, link_settings);

	/* Set FEC enable */
	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING) {
		fec_enable = lt_overrides->fec_enable && *lt_overrides->fec_enable;
		dp_set_fec_ready(link, NULL, fec_enable);
	}

	if (lt_overrides->alternate_scrambler_reset) {
		if (*lt_overrides->alternate_scrambler_reset)
			panel_mode = DP_PANEL_MODE_EDP;
		else
			panel_mode = DP_PANEL_MODE_DEFAULT;
	} else
		panel_mode = dp_get_panel_mode(link);

	dp_set_panel_mode(link, panel_mode);

	/* Attempt to train with given link training settings */
	if (link->ctx->dc->work_arounds.lt_early_cr_pattern)
		start_clock_recovery_pattern_early(link, link_res, &lt_settings, DPRX);

	/* Set link rate, lane count and spread. */
	dpcd_set_link_settings(link, &lt_settings);

	/* 2. perform link training (set link training done
	 *  to false is done as well)
	 */
	lt_status = perform_clock_recovery_sequence(link, link_res, &lt_settings, DPRX);
	if (lt_status == LINK_TRAINING_SUCCESS) {
		lt_status = perform_channel_equalization_sequence(link,
						link_res,
						&lt_settings,
						DPRX);
	}

	/* 3. Sync LT must skip TRAINING_PATTERN_SET:0 (video pattern)*/
	/* 4. print status message*/
	print_status_message(link, &lt_settings, lt_status);

	return lt_status;
}

bool dc_link_dp_sync_lt_end(struct dc_link *link, bool link_down)
{
	/* If input parameter is set, shut down phy.
	 * Still shouldn't turn off dp_receiver (DPCD:600h)
	 */
	if (link_down == true) {
		struct dc_link_settings link_settings = link->cur_link_settings;
		dp_disable_link_phy(link, NULL, link->connector_signal);
		if (dp_get_link_encoding_format(&link_settings) == DP_8b_10b_ENCODING)
			dp_set_fec_ready(link, NULL, false);
	}

	link->sync_lt_in_progress = false;
	return true;
}

static enum dc_link_rate get_lttpr_max_link_rate(struct dc_link *link)
{
	enum dc_link_rate lttpr_max_link_rate = link->dpcd_caps.lttpr_caps.max_link_rate;

	if (link->dpcd_caps.lttpr_caps.supported_128b_132b_rates.bits.UHBR20)
		lttpr_max_link_rate = LINK_RATE_UHBR20;
	else if (link->dpcd_caps.lttpr_caps.supported_128b_132b_rates.bits.UHBR13_5)
		lttpr_max_link_rate = LINK_RATE_UHBR13_5;
	else if (link->dpcd_caps.lttpr_caps.supported_128b_132b_rates.bits.UHBR10)
		lttpr_max_link_rate = LINK_RATE_UHBR10;

	return lttpr_max_link_rate;
}

static enum dc_link_rate get_cable_max_link_rate(struct dc_link *link)
{
	enum dc_link_rate cable_max_link_rate = LINK_RATE_HIGH3;

	if (link->dpcd_caps.cable_attributes.bits.UHBR10_20_CAPABILITY & DP_UHBR20)
		cable_max_link_rate = LINK_RATE_UHBR20;
	else if (link->dpcd_caps.cable_attributes.bits.UHBR13_5_CAPABILITY)
		cable_max_link_rate = LINK_RATE_UHBR13_5;
	else if (link->dpcd_caps.cable_attributes.bits.UHBR10_20_CAPABILITY & DP_UHBR10)
		cable_max_link_rate = LINK_RATE_UHBR10;

	return cable_max_link_rate;
}

bool dc_link_dp_get_max_link_enc_cap(const struct dc_link *link, struct dc_link_settings *max_link_enc_cap)
{
	struct link_encoder *link_enc = NULL;

	if (!max_link_enc_cap) {
		DC_LOG_ERROR("%s: Could not return max link encoder caps", __func__);
		return false;
	}

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (link_enc && link_enc->funcs->get_max_link_cap) {
		link_enc->funcs->get_max_link_cap(link_enc, max_link_enc_cap);
		return true;
	}

	DC_LOG_ERROR("%s: Max link encoder caps unknown", __func__);
	max_link_enc_cap->lane_count = 1;
	max_link_enc_cap->link_rate = 6;
	return false;
}


struct dc_link_settings dp_get_max_link_cap(struct dc_link *link)
{
	struct dc_link_settings max_link_cap = {0};
	enum dc_link_rate lttpr_max_link_rate;
	enum dc_link_rate cable_max_link_rate;
	struct link_encoder *link_enc = NULL;


	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	/* get max link encoder capability */
	if (link_enc)
		link_enc->funcs->get_max_link_cap(link_enc, &max_link_cap);

	/* Lower link settings based on sink's link cap */
	if (link->reported_link_cap.lane_count < max_link_cap.lane_count)
		max_link_cap.lane_count =
				link->reported_link_cap.lane_count;
	if (link->reported_link_cap.link_rate < max_link_cap.link_rate)
		max_link_cap.link_rate =
				link->reported_link_cap.link_rate;
	if (link->reported_link_cap.link_spread <
			max_link_cap.link_spread)
		max_link_cap.link_spread =
				link->reported_link_cap.link_spread;

	/* Lower link settings based on cable attributes */
	cable_max_link_rate = get_cable_max_link_rate(link);

	if (!link->dc->debug.ignore_cable_id &&
			cable_max_link_rate < max_link_cap.link_rate)
		max_link_cap.link_rate = cable_max_link_rate;

	/*
	 * account for lttpr repeaters cap
	 * notes: repeaters do not snoop in the DPRX Capabilities addresses (3.6.3).
	 */
	if (link->lttpr_mode != LTTPR_MODE_NON_LTTPR) {
		if (link->dpcd_caps.lttpr_caps.max_lane_count < max_link_cap.lane_count)
			max_link_cap.lane_count = link->dpcd_caps.lttpr_caps.max_lane_count;
		lttpr_max_link_rate = get_lttpr_max_link_rate(link);

		if (lttpr_max_link_rate < max_link_cap.link_rate)
			max_link_cap.link_rate = lttpr_max_link_rate;

		DC_LOG_HW_LINK_TRAINING("%s\n Training with LTTPR,  max_lane count %d max_link rate %d \n",
						__func__,
						max_link_cap.lane_count,
						max_link_cap.link_rate);
	}

	if (dp_get_link_encoding_format(&max_link_cap) == DP_128b_132b_ENCODING &&
			link->dc->debug.disable_uhbr)
		max_link_cap.link_rate = LINK_RATE_HIGH3;

	return max_link_cap;
}

static enum dc_status read_hpd_rx_irq_data(
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

static bool dp_verify_link_cap(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int *fail_count)
{
	struct dc_link_settings cur_link_settings = {0};
	struct dc_link_settings initial_link_settings = *known_limit_link_setting;
	bool success = false;
	bool skip_video_pattern;
	enum clock_source_id dp_cs_id = get_clock_source_id(link);
	enum link_training_result status;
	union hpd_irq_data irq_data;
	struct link_resource link_res;

	memset(&irq_data, 0, sizeof(irq_data));
	cur_link_settings = initial_link_settings;

	/* Grant extended timeout request */
	if ((link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) && (link->dpcd_caps.lttpr_caps.max_ext_timeout > 0)) {
		uint8_t grant = link->dpcd_caps.lttpr_caps.max_ext_timeout & 0x80;

		core_link_write_dpcd(link, DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT, &grant, sizeof(grant));
	}

	do {
		if (!get_temp_dp_link_res(link, &link_res, &cur_link_settings))
			continue;

		skip_video_pattern = cur_link_settings.link_rate != LINK_RATE_LOW;
		dp_enable_link_phy(
				link,
				&link_res,
				link->connector_signal,
				dp_cs_id,
				&cur_link_settings);

		status = dc_link_dp_perform_link_training(
				link,
				&link_res,
				&cur_link_settings,
				skip_video_pattern);

		if (status == LINK_TRAINING_SUCCESS) {
			success = true;
			udelay(1000);
			if (read_hpd_rx_irq_data(link, &irq_data) == DC_OK &&
					hpd_rx_irq_check_link_loss_status(
							link,
							&irq_data))
				(*fail_count)++;

		} else {
			(*fail_count)++;
		}
		dp_disable_link_phy(link, &link_res, link->connector_signal);
	} while (!success && decide_fallback_link_setting(link,
			initial_link_settings, &cur_link_settings, status));

	link->verified_link_cap = success ?
			cur_link_settings : fail_safe_link_settings;
	return success;
}

static void apply_usbc_combo_phy_reset_wa(struct dc_link *link,
		struct dc_link_settings *link_settings)
{
	/* Temporary Renoir-specific workaround PHY will sometimes be in bad
	 * state on hotplugging display from certain USB-C dongle, so add extra
	 * cycle of enabling and disabling the PHY before first link training.
	 */
	struct link_resource link_res = {0};
	enum clock_source_id dp_cs_id = get_clock_source_id(link);

	dp_enable_link_phy(link, &link_res, link->connector_signal,
			dp_cs_id, link_settings);
	dp_disable_link_phy(link, &link_res, link->connector_signal);
}

bool dp_verify_link_cap_with_retries(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int attempts)
{
	int i = 0;
	bool success = false;

	if (link->link_enc && link->link_enc->features.flags.bits.DP_IS_USB_C &&
			link->dc->debug.usbc_combo_phy_reset_wa)
		apply_usbc_combo_phy_reset_wa(link, known_limit_link_setting);

	for (i = 0; i < attempts; i++) {
		int fail_count = 0;
		enum dc_connection_type type = dc_connection_none;

		memset(&link->verified_link_cap, 0,
				sizeof(struct dc_link_settings));
		if (!dc_link_detect_sink(link, &type) || type == dc_connection_none) {
			link->verified_link_cap = fail_safe_link_settings;
			break;
		} else if (dp_verify_link_cap(link, known_limit_link_setting,
				&fail_count) && fail_count == 0) {
			success = true;
			break;
		}
		msleep(10);
	}
	return success;
}

/* in DP compliance test, DPR-120 may have
 * a random value in its MAX_LINK_BW dpcd field.
 * We map it to the maximum supported link rate that
 * is smaller than MAX_LINK_BW in this case.
 */
static enum dc_link_rate get_link_rate_from_max_link_bw(
		 uint8_t max_link_bw)
{
	enum dc_link_rate link_rate;

	if (max_link_bw >= LINK_RATE_HIGH3) {
		link_rate = LINK_RATE_HIGH3;
	} else if (max_link_bw < LINK_RATE_HIGH3
			&& max_link_bw >= LINK_RATE_HIGH2) {
		link_rate = LINK_RATE_HIGH2;
	} else if (max_link_bw < LINK_RATE_HIGH2
			&& max_link_bw >= LINK_RATE_HIGH) {
		link_rate = LINK_RATE_HIGH;
	} else if (max_link_bw < LINK_RATE_HIGH
			&& max_link_bw >= LINK_RATE_LOW) {
		link_rate = LINK_RATE_LOW;
	} else {
		link_rate = LINK_RATE_UNKNOWN;
	}

	return link_rate;
}

static inline bool reached_minimum_lane_count(enum dc_lane_count lane_count)
{
	return lane_count <= LANE_COUNT_ONE;
}

static inline bool reached_minimum_link_rate(enum dc_link_rate link_rate)
{
	return link_rate <= LINK_RATE_LOW;
}

static enum dc_lane_count reduce_lane_count(enum dc_lane_count lane_count)
{
	switch (lane_count) {
	case LANE_COUNT_FOUR:
		return LANE_COUNT_TWO;
	case LANE_COUNT_TWO:
		return LANE_COUNT_ONE;
	case LANE_COUNT_ONE:
		return LANE_COUNT_UNKNOWN;
	default:
		return LANE_COUNT_UNKNOWN;
	}
}

static enum dc_link_rate reduce_link_rate(enum dc_link_rate link_rate)
{
	switch (link_rate) {
	case LINK_RATE_UHBR20:
		return LINK_RATE_UHBR13_5;
	case LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR10;
	case LINK_RATE_UHBR10:
		return LINK_RATE_HIGH3;
	case LINK_RATE_HIGH3:
		return LINK_RATE_HIGH2;
	case LINK_RATE_HIGH2:
		return LINK_RATE_HIGH;
	case LINK_RATE_HIGH:
		return LINK_RATE_LOW;
	case LINK_RATE_LOW:
		return LINK_RATE_UNKNOWN;
	default:
		return LINK_RATE_UNKNOWN;
	}
}

static enum dc_lane_count increase_lane_count(enum dc_lane_count lane_count)
{
	switch (lane_count) {
	case LANE_COUNT_ONE:
		return LANE_COUNT_TWO;
	case LANE_COUNT_TWO:
		return LANE_COUNT_FOUR;
	default:
		return LANE_COUNT_UNKNOWN;
	}
}

static enum dc_link_rate increase_link_rate(enum dc_link_rate link_rate)
{
	switch (link_rate) {
	case LINK_RATE_LOW:
		return LINK_RATE_HIGH;
	case LINK_RATE_HIGH:
		return LINK_RATE_HIGH2;
	case LINK_RATE_HIGH2:
		return LINK_RATE_HIGH3;
	case LINK_RATE_HIGH3:
		return LINK_RATE_UHBR10;
	case LINK_RATE_UHBR10:
		return LINK_RATE_UHBR13_5;
	case LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR20;
	default:
		return LINK_RATE_UNKNOWN;
	}
}

static bool decide_fallback_link_setting_max_bw_policy(
		const struct dc_link_settings *max,
		struct dc_link_settings *cur)
{
	uint8_t cur_idx = 0, next_idx;
	bool found = false;

	while (cur_idx < ARRAY_SIZE(dp_lt_fallbacks))
		/* find current index */
		if (dp_lt_fallbacks[cur_idx].lane_count == cur->lane_count &&
				dp_lt_fallbacks[cur_idx].link_rate == cur->link_rate)
			break;
		else
			cur_idx++;

	next_idx = cur_idx + 1;

	while (next_idx < ARRAY_SIZE(dp_lt_fallbacks))
		/* find next index */
		if (dp_lt_fallbacks[next_idx].lane_count <= max->lane_count &&
				dp_lt_fallbacks[next_idx].link_rate <= max->link_rate)
			break;
		else
			next_idx++;

	if (next_idx < ARRAY_SIZE(dp_lt_fallbacks)) {
		cur->lane_count = dp_lt_fallbacks[next_idx].lane_count;
		cur->link_rate = dp_lt_fallbacks[next_idx].link_rate;
		found = true;
	}

	return found;
}

/*
 * function: set link rate and lane count fallback based
 * on current link setting and last link training result
 * return value:
 *			true - link setting could be set
 *			false - has reached minimum setting
 *					and no further fallback could be done
 */
static bool decide_fallback_link_setting(
		struct dc_link *link,
		struct dc_link_settings initial_link_settings,
		struct dc_link_settings *current_link_setting,
		enum link_training_result training_result)
{
	if (!current_link_setting)
		return false;
	if (dp_get_link_encoding_format(&initial_link_settings) == DP_128b_132b_ENCODING ||
			link->dc->debug.force_dp2_lt_fallback_method)
		return decide_fallback_link_setting_max_bw_policy(&initial_link_settings,
				current_link_setting);

	switch (training_result) {
	case LINK_TRAINING_CR_FAIL_LANE0:
	case LINK_TRAINING_CR_FAIL_LANE1:
	case LINK_TRAINING_CR_FAIL_LANE23:
	case LINK_TRAINING_LQA_FAIL:
	{
		if (!reached_minimum_link_rate
				(current_link_setting->link_rate)) {
			current_link_setting->link_rate =
				reduce_link_rate(
					current_link_setting->link_rate);
		} else if (!reached_minimum_lane_count
				(current_link_setting->lane_count)) {
			current_link_setting->link_rate =
				initial_link_settings.link_rate;
			if (training_result == LINK_TRAINING_CR_FAIL_LANE0)
				return false;
			else if (training_result == LINK_TRAINING_CR_FAIL_LANE1)
				current_link_setting->lane_count =
						LANE_COUNT_ONE;
			else if (training_result ==
					LINK_TRAINING_CR_FAIL_LANE23)
				current_link_setting->lane_count =
						LANE_COUNT_TWO;
			else
				current_link_setting->lane_count =
					reduce_lane_count(
					current_link_setting->lane_count);
		} else {
			return false;
		}
		break;
	}
	case LINK_TRAINING_EQ_FAIL_EQ:
	{
		if (!reached_minimum_lane_count
				(current_link_setting->lane_count)) {
			current_link_setting->lane_count =
				reduce_lane_count(
					current_link_setting->lane_count);
		} else if (!reached_minimum_link_rate
				(current_link_setting->link_rate)) {
			current_link_setting->link_rate =
				reduce_link_rate(
					current_link_setting->link_rate);
		} else {
			return false;
		}
		break;
	}
	case LINK_TRAINING_EQ_FAIL_CR:
	{
		if (!reached_minimum_link_rate
				(current_link_setting->link_rate)) {
			current_link_setting->link_rate =
				reduce_link_rate(
					current_link_setting->link_rate);
		} else {
			return false;
		}
		break;
	}
	default:
		return false;
	}
	return true;
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

static bool decide_dp_link_settings(struct dc_link *link, struct dc_link_settings *link_setting, uint32_t req_bw)
{
	struct dc_link_settings initial_link_setting = {
		LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED, false, 0};
	struct dc_link_settings current_link_setting =
			initial_link_setting;
	uint32_t link_bw;

	if (req_bw > dc_link_bandwidth_kbps(link, &link->verified_link_cap))
		return false;

	/* search for the minimum link setting that:
	 * 1. is supported according to the link training result
	 * 2. could support the b/w requested by the timing
	 */
	while (current_link_setting.link_rate <=
			link->verified_link_cap.link_rate) {
		link_bw = dc_link_bandwidth_kbps(
				link,
				&current_link_setting);
		if (req_bw <= link_bw) {
			*link_setting = current_link_setting;
			return true;
		}

		if (current_link_setting.lane_count <
				link->verified_link_cap.lane_count) {
			current_link_setting.lane_count =
					increase_lane_count(
							current_link_setting.lane_count);
		} else {
			current_link_setting.link_rate =
					increase_link_rate(
							current_link_setting.link_rate);
			current_link_setting.lane_count =
					initial_link_setting.lane_count;
		}
	}

	return false;
}

bool decide_edp_link_settings(struct dc_link *link, struct dc_link_settings *link_setting, uint32_t req_bw)
{
	struct dc_link_settings initial_link_setting;
	struct dc_link_settings current_link_setting;
	uint32_t link_bw;

	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if (link->dpcd_caps.dpcd_rev.raw < DPCD_REV_13 ||
			link->dpcd_caps.edp_supported_link_rates_count == 0) {
		*link_setting = link->verified_link_cap;
		return true;
	}

	memset(&initial_link_setting, 0, sizeof(initial_link_setting));
	initial_link_setting.lane_count = LANE_COUNT_ONE;
	initial_link_setting.link_rate = link->dpcd_caps.edp_supported_link_rates[0];
	initial_link_setting.link_spread = LINK_SPREAD_DISABLED;
	initial_link_setting.use_link_rate_set = true;
	initial_link_setting.link_rate_set = 0;
	current_link_setting = initial_link_setting;

	/* search for the minimum link setting that:
	 * 1. is supported according to the link training result
	 * 2. could support the b/w requested by the timing
	 */
	while (current_link_setting.link_rate <=
			link->verified_link_cap.link_rate) {
		link_bw = dc_link_bandwidth_kbps(
				link,
				&current_link_setting);
		if (req_bw <= link_bw) {
			*link_setting = current_link_setting;
			return true;
		}

		if (current_link_setting.lane_count <
				link->verified_link_cap.lane_count) {
			current_link_setting.lane_count =
					increase_lane_count(
							current_link_setting.lane_count);
		} else {
			if (current_link_setting.link_rate_set < link->dpcd_caps.edp_supported_link_rates_count) {
				current_link_setting.link_rate_set++;
				current_link_setting.link_rate =
					link->dpcd_caps.edp_supported_link_rates[current_link_setting.link_rate_set];
				current_link_setting.lane_count =
									initial_link_setting.lane_count;
			} else
				break;
		}
	}
	return false;
}

static bool decide_edp_link_settings_with_dsc(struct dc_link *link,
		struct dc_link_settings *link_setting,
		uint32_t req_bw,
		enum dc_link_rate max_link_rate)
{
	struct dc_link_settings initial_link_setting;
	struct dc_link_settings current_link_setting;
	uint32_t link_bw;

	unsigned int policy = 0;

	policy = link->ctx->dc->debug.force_dsc_edp_policy;
	if (max_link_rate == LINK_RATE_UNKNOWN)
		max_link_rate = link->verified_link_cap.link_rate;
	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if ((link->dpcd_caps.dpcd_rev.raw < DPCD_REV_13 ||
			link->dpcd_caps.edp_supported_link_rates_count == 0)) {
		/* for DSC enabled case, we search for minimum lane count */
		memset(&initial_link_setting, 0, sizeof(initial_link_setting));
		initial_link_setting.lane_count = LANE_COUNT_ONE;
		initial_link_setting.link_rate = LINK_RATE_LOW;
		initial_link_setting.link_spread = LINK_SPREAD_DISABLED;
		initial_link_setting.use_link_rate_set = false;
		initial_link_setting.link_rate_set = 0;
		current_link_setting = initial_link_setting;
		if (req_bw > dc_link_bandwidth_kbps(link, &link->verified_link_cap))
			return false;

		/* search for the minimum link setting that:
		 * 1. is supported according to the link training result
		 * 2. could support the b/w requested by the timing
		 */
		while (current_link_setting.link_rate <=
				max_link_rate) {
			link_bw = dc_link_bandwidth_kbps(
					link,
					&current_link_setting);
			if (req_bw <= link_bw) {
				*link_setting = current_link_setting;
				return true;
			}
			if (policy) {
				/* minimize lane */
				if (current_link_setting.link_rate < max_link_rate) {
					current_link_setting.link_rate =
							increase_link_rate(
									current_link_setting.link_rate);
				} else {
					if (current_link_setting.lane_count <
									link->verified_link_cap.lane_count) {
						current_link_setting.lane_count =
								increase_lane_count(
										current_link_setting.lane_count);
						current_link_setting.link_rate = initial_link_setting.link_rate;
					} else
						break;
				}
			} else {
				/* minimize link rate */
				if (current_link_setting.lane_count <
						link->verified_link_cap.lane_count) {
					current_link_setting.lane_count =
							increase_lane_count(
									current_link_setting.lane_count);
				} else {
					current_link_setting.link_rate =
							increase_link_rate(
									current_link_setting.link_rate);
					current_link_setting.lane_count =
							initial_link_setting.lane_count;
				}
			}
		}
		return false;
	}

	/* if optimize edp link is supported */
	memset(&initial_link_setting, 0, sizeof(initial_link_setting));
	initial_link_setting.lane_count = LANE_COUNT_ONE;
	initial_link_setting.link_rate = link->dpcd_caps.edp_supported_link_rates[0];
	initial_link_setting.link_spread = LINK_SPREAD_DISABLED;
	initial_link_setting.use_link_rate_set = true;
	initial_link_setting.link_rate_set = 0;
	current_link_setting = initial_link_setting;

	/* search for the minimum link setting that:
	 * 1. is supported according to the link training result
	 * 2. could support the b/w requested by the timing
	 */
	while (current_link_setting.link_rate <=
			max_link_rate) {
		link_bw = dc_link_bandwidth_kbps(
				link,
				&current_link_setting);
		if (req_bw <= link_bw) {
			*link_setting = current_link_setting;
			return true;
		}
		if (policy) {
			/* minimize lane */
			if (current_link_setting.link_rate_set <
					link->dpcd_caps.edp_supported_link_rates_count
					&& current_link_setting.link_rate < max_link_rate) {
				current_link_setting.link_rate_set++;
				current_link_setting.link_rate =
					link->dpcd_caps.edp_supported_link_rates[current_link_setting.link_rate_set];
			} else {
				if (current_link_setting.lane_count < link->verified_link_cap.lane_count) {
					current_link_setting.lane_count =
							increase_lane_count(
									current_link_setting.lane_count);
					current_link_setting.link_rate_set = initial_link_setting.link_rate_set;
					current_link_setting.link_rate =
						link->dpcd_caps.edp_supported_link_rates[current_link_setting.link_rate_set];
				} else
					break;
			}
		} else {
			/* minimize link rate */
			if (current_link_setting.lane_count <
					link->verified_link_cap.lane_count) {
				current_link_setting.lane_count =
						increase_lane_count(
								current_link_setting.lane_count);
			} else {
				if (current_link_setting.link_rate_set < link->dpcd_caps.edp_supported_link_rates_count) {
					current_link_setting.link_rate_set++;
					current_link_setting.link_rate =
						link->dpcd_caps.edp_supported_link_rates[current_link_setting.link_rate_set];
					current_link_setting.lane_count =
						initial_link_setting.lane_count;
				} else
					break;
			}
		}
	}
	return false;
}

static bool decide_mst_link_settings(const struct dc_link *link, struct dc_link_settings *link_setting)
{
	*link_setting = link->verified_link_cap;
	return true;
}

void decide_link_settings(struct dc_stream_state *stream,
	struct dc_link_settings *link_setting)
{
	struct dc_link *link;
	uint32_t req_bw;

	req_bw = dc_bandwidth_in_kbps_from_timing(&stream->timing);

	link = stream->link;

	/* if preferred is specified through AMDDP, use it, if it's enough
	 * to drive the mode
	 */
	if (link->preferred_link_setting.lane_count !=
			LANE_COUNT_UNKNOWN &&
			link->preferred_link_setting.link_rate !=
					LINK_RATE_UNKNOWN) {
		*link_setting =  link->preferred_link_setting;
		return;
	}

	/* MST doesn't perform link training for now
	 * TODO: add MST specific link training routine
	 */
	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		if (decide_mst_link_settings(link, link_setting))
			return;
	} else if (link->connector_signal == SIGNAL_TYPE_EDP) {
		/* enable edp link optimization for DSC eDP case */
		if (stream->timing.flags.DSC) {
			enum dc_link_rate max_link_rate = LINK_RATE_UNKNOWN;

			if (link->ctx->dc->debug.force_dsc_edp_policy) {
				/* calculate link max link rate cap*/
				struct dc_link_settings tmp_link_setting;
				struct dc_crtc_timing tmp_timing = stream->timing;
				uint32_t orig_req_bw;

				tmp_link_setting.link_rate = LINK_RATE_UNKNOWN;
				tmp_timing.flags.DSC = 0;
				orig_req_bw = dc_bandwidth_in_kbps_from_timing(&tmp_timing);
				decide_edp_link_settings(link, &tmp_link_setting, orig_req_bw);
				max_link_rate = tmp_link_setting.link_rate;
			}
			if (decide_edp_link_settings_with_dsc(link, link_setting, req_bw, max_link_rate))
				return;
		} else if (decide_edp_link_settings(link, link_setting, req_bw))
			return;
	} else if (decide_dp_link_settings(link, link_setting, req_bw))
		return;

	BREAK_TO_DEBUGGER();
	ASSERT(link->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN);

	*link_setting = link->verified_link_cap;
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

static void dp_test_send_link_training(struct dc_link *link)
{
	struct dc_link_settings link_settings = {0};

	core_link_read_dpcd(
			link,
			DP_TEST_LANE_COUNT,
			(unsigned char *)(&link_settings.lane_count),
			1);
	core_link_read_dpcd(
			link,
			DP_TEST_LINK_RATE,
			(unsigned char *)(&link_settings.link_rate),
			1);

	/* Set preferred link settings */
	link->verified_link_cap.lane_count = link_settings.lane_count;
	link->verified_link_cap.link_rate = link_settings.link_rate;

	dp_retrain_link_dp_test(link, &link_settings, false);
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

	if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
			(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link->lttpr_mode == LTTPR_MODE_TRANSPARENT)
		vendor_specific_lttpr_wa_three_dpcd(
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
	case PHY_TEST_PATTERN_SQUARE_PULSE:
		test_pattern = DP_TEST_PATTERN_SQUARE_PULSE;
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

	if (test_pattern == DP_TEST_PATTERN_SQUARE_PULSE) {
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

	/* prepare link training settings */
	link_training_settings.link_settings = link->cur_link_settings;

	for (lane = 0; lane <
		(unsigned int)(link->cur_link_settings.lane_count);
		lane++) {
		dpcd_lane_adjust.raw =
			get_nibble_at_index(&dpcd_lane_adjustment[0].raw, lane);
		if (dp_get_link_encoding_format(&link->cur_link_settings) ==
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
		} else if (dp_get_link_encoding_format(&link->cur_link_settings) ==
				DP_128b_132b_ENCODING) {
			link_training_settings.hw_lane_settings[lane].FFE_PRESET.raw =
					dpcd_lane_adjust.tx_ffe.PRESET_VALUE;
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

	channel_count = dpcd_test_mode.bits.channel_count + 1;

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
				pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe) {
			core_link_disable_stream(pipe_ctx);
		}
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe_ctx && pipe_ctx->stream && !pipe_ctx->stream->dpms_off &&
				pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe) {
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

/*query dpcd for version and mst cap addresses*/
bool is_mst_supported(struct dc_link *link)
{
	bool mst          = false;
	enum dc_status st = DC_OK;
	union dpcd_rev rev;
	union mstm_cap cap;

	if (link->preferred_training_settings.mst_enable &&
		*link->preferred_training_settings.mst_enable == false) {
		return false;
	}

	rev.raw  = 0;
	cap.raw  = 0;

	st = core_link_read_dpcd(link, DP_DPCD_REV, &rev.raw,
			sizeof(rev));

	if (st == DC_OK && rev.raw >= DPCD_REV_12) {

		st = core_link_read_dpcd(link, DP_MSTM_CAP,
				&cap.raw, sizeof(cap));
		if (st == DC_OK && cap.bits.MST_CAP == 1)
			mst = true;
	}
	return mst;

}

bool is_dp_active_dongle(const struct dc_link *link)
{
	return (link->dpcd_caps.dongle_type >= DISPLAY_DONGLE_DP_VGA_CONVERTER) &&
				(link->dpcd_caps.dongle_type <= DISPLAY_DONGLE_DP_HDMI_CONVERTER);
}

bool is_dp_branch_device(const struct dc_link *link)
{
	return link->dpcd_caps.is_branch_dev;
}

static int translate_dpcd_max_bpc(enum dpcd_downstream_port_max_bpc bpc)
{
	switch (bpc) {
	case DOWN_STREAM_MAX_8BPC:
		return 8;
	case DOWN_STREAM_MAX_10BPC:
		return 10;
	case DOWN_STREAM_MAX_12BPC:
		return 12;
	case DOWN_STREAM_MAX_16BPC:
		return 16;
	default:
		break;
	}

	return -1;
}

#if defined(CONFIG_DRM_AMD_DC_DCN)
uint32_t dc_link_bw_kbps_from_raw_frl_link_rate_data(uint8_t bw)
{
	switch (bw) {
	case 0b001:
		return 9000000;
	case 0b010:
		return 18000000;
	case 0b011:
		return 24000000;
	case 0b100:
		return 32000000;
	case 0b101:
		return 40000000;
	case 0b110:
		return 48000000;
	}

	return 0;
}

/*
 * Return PCON's post FRL link training supported BW if its non-zero, otherwise return max_supported_frl_bw.
 */
static uint32_t intersect_frl_link_bw_support(
	const uint32_t max_supported_frl_bw_in_kbps,
	const union hdmi_encoded_link_bw hdmi_encoded_link_bw)
{
	uint32_t supported_bw_in_kbps = max_supported_frl_bw_in_kbps;

	// HDMI_ENCODED_LINK_BW bits are only valid if HDMI Link Configuration bit is 1 (FRL mode)
	if (hdmi_encoded_link_bw.bits.FRL_MODE) {
		if (hdmi_encoded_link_bw.bits.BW_48Gbps)
			supported_bw_in_kbps = 48000000;
		else if (hdmi_encoded_link_bw.bits.BW_40Gbps)
			supported_bw_in_kbps = 40000000;
		else if (hdmi_encoded_link_bw.bits.BW_32Gbps)
			supported_bw_in_kbps = 32000000;
		else if (hdmi_encoded_link_bw.bits.BW_24Gbps)
			supported_bw_in_kbps = 24000000;
		else if (hdmi_encoded_link_bw.bits.BW_18Gbps)
			supported_bw_in_kbps = 18000000;
		else if (hdmi_encoded_link_bw.bits.BW_9Gbps)
			supported_bw_in_kbps = 9000000;
	}

	return supported_bw_in_kbps;
}
#endif

static void read_dp_device_vendor_id(struct dc_link *link)
{
	struct dp_device_vendor_id dp_id;

	/* read IEEE branch device id */
	core_link_read_dpcd(
		link,
		DP_BRANCH_OUI,
		(uint8_t *)&dp_id,
		sizeof(dp_id));

	link->dpcd_caps.branch_dev_id =
		(dp_id.ieee_oui[0] << 16) +
		(dp_id.ieee_oui[1] << 8) +
		dp_id.ieee_oui[2];

	memmove(
		link->dpcd_caps.branch_dev_name,
		dp_id.ieee_device_id,
		sizeof(dp_id.ieee_device_id));
}



static void get_active_converter_info(
	uint8_t data, struct dc_link *link)
{
	union dp_downstream_port_present ds_port = { .byte = data };
	memset(&link->dpcd_caps.dongle_caps, 0, sizeof(link->dpcd_caps.dongle_caps));

	/* decode converter info*/
	if (!ds_port.fields.PORT_PRESENT) {
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
		ddc_service_set_dongle_type(link->ddc,
				link->dpcd_caps.dongle_type);
		link->dpcd_caps.is_branch_dev = false;
		return;
	}

	/* DPCD 0x5 bit 0 = 1, it indicate it's branch device */
	link->dpcd_caps.is_branch_dev = ds_port.fields.PORT_PRESENT;

	switch (ds_port.fields.PORT_TYPE) {
	case DOWNSTREAM_VGA:
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_VGA_CONVERTER;
		break;
	case DOWNSTREAM_DVI_HDMI_DP_PLUS_PLUS:
		/* At this point we don't know is it DVI or HDMI or DP++,
		 * assume DVI.*/
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_DVI_CONVERTER;
		break;
	default:
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
		break;
	}

	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_11) {
		uint8_t det_caps[16]; /* CTS 4.2.2.7 expects source to read Detailed Capabilities Info : 00080h-0008F.*/
		union dwnstream_port_caps_byte0 *port_caps =
			(union dwnstream_port_caps_byte0 *)det_caps;
		if (core_link_read_dpcd(link, DP_DOWNSTREAM_PORT_0,
				det_caps, sizeof(det_caps)) == DC_OK) {

			switch (port_caps->bits.DWN_STRM_PORTX_TYPE) {
			/*Handle DP case as DONGLE_NONE*/
			case DOWN_STREAM_DETAILED_DP:
				link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
				break;
			case DOWN_STREAM_DETAILED_VGA:
				link->dpcd_caps.dongle_type =
					DISPLAY_DONGLE_DP_VGA_CONVERTER;
				break;
			case DOWN_STREAM_DETAILED_DVI:
				link->dpcd_caps.dongle_type =
					DISPLAY_DONGLE_DP_DVI_CONVERTER;
				break;
			case DOWN_STREAM_DETAILED_HDMI:
			case DOWN_STREAM_DETAILED_DP_PLUS_PLUS:
				/*Handle DP++ active converter case, process DP++ case as HDMI case according DP1.4 spec*/
				link->dpcd_caps.dongle_type =
					DISPLAY_DONGLE_DP_HDMI_CONVERTER;

				link->dpcd_caps.dongle_caps.dongle_type = link->dpcd_caps.dongle_type;
				if (ds_port.fields.DETAILED_CAPS) {

					union dwnstream_port_caps_byte3_hdmi
						hdmi_caps = {.raw = det_caps[3] };
					union dwnstream_port_caps_byte2
						hdmi_color_caps = {.raw = det_caps[2] };
					link->dpcd_caps.dongle_caps.dp_hdmi_max_pixel_clk_in_khz =
						det_caps[1] * 2500;

					link->dpcd_caps.dongle_caps.is_dp_hdmi_s3d_converter =
						hdmi_caps.bits.FRAME_SEQ_TO_FRAME_PACK;
					/*YCBCR capability only for HDMI case*/
					if (port_caps->bits.DWN_STRM_PORTX_TYPE
							== DOWN_STREAM_DETAILED_HDMI) {
						link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr422_pass_through =
								hdmi_caps.bits.YCrCr422_PASS_THROUGH;
						link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr420_pass_through =
								hdmi_caps.bits.YCrCr420_PASS_THROUGH;
						link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr422_converter =
								hdmi_caps.bits.YCrCr422_CONVERSION;
						link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr420_converter =
								hdmi_caps.bits.YCrCr420_CONVERSION;
					}

					link->dpcd_caps.dongle_caps.dp_hdmi_max_bpc =
						translate_dpcd_max_bpc(
							hdmi_color_caps.bits.MAX_BITS_PER_COLOR_COMPONENT);

#if defined(CONFIG_DRM_AMD_DC_DCN)
					if (link->dc->caps.hdmi_frl_pcon_support) {
						union hdmi_encoded_link_bw hdmi_encoded_link_bw;

						link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps =
								dc_link_bw_kbps_from_raw_frl_link_rate_data(
										hdmi_color_caps.bits.MAX_ENCODED_LINK_BW_SUPPORT);

						// Intersect reported max link bw support with the supported link rate post FRL link training
						if (core_link_read_dpcd(link, DP_PCON_HDMI_POST_FRL_STATUS,
								&hdmi_encoded_link_bw.raw, sizeof(hdmi_encoded_link_bw)) == DC_OK) {
							link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps = intersect_frl_link_bw_support(
									link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps,
									hdmi_encoded_link_bw);
						}

						if (link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps > 0)
							link->dpcd_caps.dongle_caps.extendedCapValid = true;
					}
#endif

					if (link->dpcd_caps.dongle_caps.dp_hdmi_max_pixel_clk_in_khz != 0)
						link->dpcd_caps.dongle_caps.extendedCapValid = true;
				}

				break;
			}
		}
	}

	ddc_service_set_dongle_type(link->ddc, link->dpcd_caps.dongle_type);

	{
		struct dp_sink_hw_fw_revision dp_hw_fw_revision;

		core_link_read_dpcd(
			link,
			DP_BRANCH_REVISION_START,
			(uint8_t *)&dp_hw_fw_revision,
			sizeof(dp_hw_fw_revision));

		link->dpcd_caps.branch_hw_revision =
			dp_hw_fw_revision.ieee_hw_rev;

		memmove(
			link->dpcd_caps.branch_fw_revision,
			dp_hw_fw_revision.ieee_fw_rev,
			sizeof(dp_hw_fw_revision.ieee_fw_rev));
	}
	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_14 &&
			link->dpcd_caps.dongle_type != DISPLAY_DONGLE_NONE) {
		union dp_dfp_cap_ext dfp_cap_ext;
		memset(&dfp_cap_ext, '\0', sizeof (dfp_cap_ext));
		core_link_read_dpcd(
				link,
				DP_DFP_CAPABILITY_EXTENSION_SUPPORT,
				dfp_cap_ext.raw,
				sizeof(dfp_cap_ext.raw));
		link->dpcd_caps.dongle_caps.dfp_cap_ext.supported = dfp_cap_ext.fields.supported;
		link->dpcd_caps.dongle_caps.dfp_cap_ext.max_pixel_rate_in_mps =
				dfp_cap_ext.fields.max_pixel_rate_in_mps[0] +
				(dfp_cap_ext.fields.max_pixel_rate_in_mps[1] << 8);
		link->dpcd_caps.dongle_caps.dfp_cap_ext.max_video_h_active_width =
				dfp_cap_ext.fields.max_video_h_active_width[0] +
				(dfp_cap_ext.fields.max_video_h_active_width[1] << 8);
		link->dpcd_caps.dongle_caps.dfp_cap_ext.max_video_v_active_height =
				dfp_cap_ext.fields.max_video_v_active_height[0] +
				(dfp_cap_ext.fields.max_video_v_active_height[1] << 8);
		link->dpcd_caps.dongle_caps.dfp_cap_ext.encoding_format_caps =
				dfp_cap_ext.fields.encoding_format_caps;
		link->dpcd_caps.dongle_caps.dfp_cap_ext.rgb_color_depth_caps =
				dfp_cap_ext.fields.rgb_color_depth_caps;
		link->dpcd_caps.dongle_caps.dfp_cap_ext.ycbcr444_color_depth_caps =
				dfp_cap_ext.fields.ycbcr444_color_depth_caps;
		link->dpcd_caps.dongle_caps.dfp_cap_ext.ycbcr422_color_depth_caps =
				dfp_cap_ext.fields.ycbcr422_color_depth_caps;
		link->dpcd_caps.dongle_caps.dfp_cap_ext.ycbcr420_color_depth_caps =
				dfp_cap_ext.fields.ycbcr420_color_depth_caps;
		DC_LOG_DP2("DFP capability extension is read at link %d", link->link_index);
		DC_LOG_DP2("\tdfp_cap_ext.supported = %s", link->dpcd_caps.dongle_caps.dfp_cap_ext.supported ? "true" : "false");
		DC_LOG_DP2("\tdfp_cap_ext.max_pixel_rate_in_mps = %d", link->dpcd_caps.dongle_caps.dfp_cap_ext.max_pixel_rate_in_mps);
		DC_LOG_DP2("\tdfp_cap_ext.max_video_h_active_width = %d", link->dpcd_caps.dongle_caps.dfp_cap_ext.max_video_h_active_width);
		DC_LOG_DP2("\tdfp_cap_ext.max_video_v_active_height = %d", link->dpcd_caps.dongle_caps.dfp_cap_ext.max_video_v_active_height);
	}
}

static void dp_wa_power_up_0010FA(struct dc_link *link, uint8_t *dpcd_data,
		int length)
{
	int retry = 0;

	if (!link->dpcd_caps.dpcd_rev.raw) {
		do {
			dp_receiver_power_ctrl(link, true);
			core_link_read_dpcd(link, DP_DPCD_REV,
							dpcd_data, length);
			link->dpcd_caps.dpcd_rev.raw = dpcd_data[
				DP_DPCD_REV -
				DP_DPCD_REV];
		} while (retry++ < 4 && !link->dpcd_caps.dpcd_rev.raw);
	}

	if (link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER) {
		switch (link->dpcd_caps.branch_dev_id) {
		/* 0010FA active dongles (DP-VGA, DP-DLDVI converters) power down
		 * all internal circuits including AUX communication preventing
		 * reading DPCD table and EDID (spec violation).
		 * Encoder will skip DP RX power down on disable_output to
		 * keep receiver powered all the time.*/
		case DP_BRANCH_DEVICE_ID_0010FA:
		case DP_BRANCH_DEVICE_ID_0080E1:
		case DP_BRANCH_DEVICE_ID_00E04C:
			link->wa_flags.dp_keep_receiver_powered = true;
			break;

		/* TODO: May need work around for other dongles. */
		default:
			link->wa_flags.dp_keep_receiver_powered = false;
			break;
		}
	} else
		link->wa_flags.dp_keep_receiver_powered = false;
}

/* Read additional sink caps defined in source specific DPCD area
 * This function currently only reads from SinkCapability address (DP_SOURCE_SINK_CAP)
 */
static bool dpcd_read_sink_ext_caps(struct dc_link *link)
{
	uint8_t dpcd_data;

	if (!link)
		return false;

	if (core_link_read_dpcd(link, DP_SOURCE_SINK_CAP, &dpcd_data, 1) != DC_OK)
		return false;

	link->dpcd_sink_ext_caps.raw = dpcd_data;
	return true;
}

bool dp_retrieve_lttpr_cap(struct dc_link *link)
{
	uint8_t lttpr_dpcd_data[8];
	bool allow_lttpr_non_transparent_mode = 0;
	bool vbios_lttpr_enable = link->dc->caps.vbios_lttpr_enable;
	bool vbios_lttpr_interop = link->dc->caps.vbios_lttpr_aware;
	enum dc_status status = DC_ERROR_UNEXPECTED;
	bool is_lttpr_present = false;

	memset(lttpr_dpcd_data, '\0', sizeof(lttpr_dpcd_data));

	if ((link->dc->config.allow_lttpr_non_transparent_mode.bits.DP2_0 &&
			link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED)) {
		allow_lttpr_non_transparent_mode = 1;
	} else if (link->dc->config.allow_lttpr_non_transparent_mode.bits.DP1_4A &&
			!link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED) {
		allow_lttpr_non_transparent_mode = 1;
	}

	/*
	 * Logic to determine LTTPR mode
	 */
	link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
	if (vbios_lttpr_enable && vbios_lttpr_interop)
		link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
	else if (!vbios_lttpr_enable && vbios_lttpr_interop) {
		if (allow_lttpr_non_transparent_mode)
			link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
		else
			link->lttpr_mode = LTTPR_MODE_TRANSPARENT;
	} else if (!vbios_lttpr_enable && !vbios_lttpr_interop) {
		if (!allow_lttpr_non_transparent_mode || !link->dc->caps.extended_aux_timeout_support)
			link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
		else
			link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
	}
#if defined(CONFIG_DRM_AMD_DC_DCN)
	/* Check DP tunnel LTTPR mode debug option. */
	if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA &&
	    link->dc->debug.dpia_debug.bits.force_non_lttpr)
		link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
#endif

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT || link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
		/* By reading LTTPR capability, RX assumes that we will enable
		 * LTTPR extended aux timeout if LTTPR is present.
		 */
		status = core_link_read_dpcd(
				link,
				DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV,
				lttpr_dpcd_data,
				sizeof(lttpr_dpcd_data));
		if (status != DC_OK) {
			DC_LOG_DP2("%s: Read LTTPR caps data failed.\n", __func__);
			return false;
		}

		link->dpcd_caps.lttpr_caps.revision.raw =
				lttpr_dpcd_data[DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.max_link_rate =
				lttpr_dpcd_data[DP_MAX_LINK_RATE_PHY_REPEATER -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.phy_repeater_cnt =
				lttpr_dpcd_data[DP_PHY_REPEATER_CNT -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.max_lane_count =
				lttpr_dpcd_data[DP_MAX_LANE_COUNT_PHY_REPEATER -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.mode =
				lttpr_dpcd_data[DP_PHY_REPEATER_MODE -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.max_ext_timeout =
				lttpr_dpcd_data[DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.main_link_channel_coding.raw =
				lttpr_dpcd_data[DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		link->dpcd_caps.lttpr_caps.supported_128b_132b_rates.raw =
				lttpr_dpcd_data[DP_PHY_REPEATER_128b_132b_RATES -
								DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

		/* Attempt to train in LTTPR transparent mode if repeater count exceeds 8. */
		is_lttpr_present = (link->dpcd_caps.lttpr_caps.max_lane_count > 0 &&
				link->dpcd_caps.lttpr_caps.phy_repeater_cnt < 0xff &&
				link->dpcd_caps.lttpr_caps.max_lane_count <= 4 &&
				link->dpcd_caps.lttpr_caps.revision.raw >= 0x14);
		if (is_lttpr_present) {
			CONN_DATA_DETECT(link, lttpr_dpcd_data, sizeof(lttpr_dpcd_data), "LTTPR Caps: ");
			configure_lttpr_mode_transparent(link);
		} else
			link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
	}
	return is_lttpr_present;
}


static bool is_usbc_connector(struct dc_link *link)
{
	return link->link_enc &&
			link->link_enc->features.flags.bits.DP_IS_USB_C;
}

static bool retrieve_link_cap(struct dc_link *link)
{
	/* DP_ADAPTER_CAP - DP_DPCD_REV + 1 == 16 and also DP_DSC_BITS_PER_PIXEL_INC - DP_DSC_SUPPORT + 1 == 16,
	 * which means size 16 will be good for both of those DPCD register block reads
	 */
	uint8_t dpcd_data[16];
	/*Only need to read 1 byte starting from DP_DPRX_FEATURE_ENUMERATION_LIST.
	 */
	uint8_t dpcd_dprx_data = '\0';
	uint8_t dpcd_power_state = '\0';

	struct dp_device_vendor_id sink_id;
	union down_stream_port_count down_strm_port_count;
	union edp_configuration_cap edp_config_cap;
	union dp_downstream_port_present ds_port = { 0 };
	enum dc_status status = DC_ERROR_UNEXPECTED;
	uint32_t read_dpcd_retry_cnt = 3;
	int i;
	struct dp_sink_hw_fw_revision dp_hw_fw_revision;
	const uint32_t post_oui_delay = 30; // 30ms
	bool is_lttpr_present = false;

	memset(dpcd_data, '\0', sizeof(dpcd_data));
	memset(&down_strm_port_count,
		'\0', sizeof(union down_stream_port_count));
	memset(&edp_config_cap, '\0',
		sizeof(union edp_configuration_cap));

	/* if extended timeout is supported in hardware,
	 * default to LTTPR timeout (3.2ms) first as a W/A for DP link layer
	 * CTS 4.2.1.1 regression introduced by CTS specs requirement update.
	 */
	dc_link_aux_try_to_configure_timeout(link->ddc,
			LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD);

	is_lttpr_present = dp_retrieve_lttpr_cap(link);
	/* Read DP tunneling information. */
	status = dpcd_get_tunneling_device_data(link);

	status = core_link_read_dpcd(link, DP_SET_POWER,
			&dpcd_power_state, sizeof(dpcd_power_state));

	/* Delay 1 ms if AUX CH is in power down state. Based on spec
	 * section 2.3.1.2, if AUX CH may be powered down due to
	 * write to DPCD 600h = 2. Sink AUX CH is monitoring differential
	 * signal and may need up to 1 ms before being able to reply.
	 */
	if (status != DC_OK || dpcd_power_state == DP_SET_POWER_D3)
		udelay(1000);

	dpcd_set_source_specific_data(link);
	/* Sink may need to configure internals based on vendor, so allow some
	 * time before proceeding with possibly vendor specific transactions
	 */
	msleep(post_oui_delay);

	/* Read cable ID and update receiver */
	dpcd_update_cable_id(link);

	for (i = 0; i < read_dpcd_retry_cnt; i++) {
		status = core_link_read_dpcd(
				link,
				DP_DPCD_REV,
				dpcd_data,
				sizeof(dpcd_data));
		if (status == DC_OK)
			break;
	}

	if (status != DC_OK) {
		dm_error("%s: Read receiver caps dpcd data failed.\n", __func__);
		return false;
	}

	if (!is_lttpr_present)
		dc_link_aux_try_to_configure_timeout(link->ddc, LINK_AUX_DEFAULT_TIMEOUT_PERIOD);

	{
		union training_aux_rd_interval aux_rd_interval;

		aux_rd_interval.raw =
			dpcd_data[DP_TRAINING_AUX_RD_INTERVAL];

		link->dpcd_caps.ext_receiver_cap_field_present =
				aux_rd_interval.bits.EXT_RECEIVER_CAP_FIELD_PRESENT == 1;

		if (aux_rd_interval.bits.EXT_RECEIVER_CAP_FIELD_PRESENT == 1) {
			uint8_t ext_cap_data[16];

			memset(ext_cap_data, '\0', sizeof(ext_cap_data));
			for (i = 0; i < read_dpcd_retry_cnt; i++) {
				status = core_link_read_dpcd(
				link,
				DP_DP13_DPCD_REV,
				ext_cap_data,
				sizeof(ext_cap_data));
				if (status == DC_OK) {
					memcpy(dpcd_data, ext_cap_data, sizeof(dpcd_data));
					break;
				}
			}
			if (status != DC_OK)
				dm_error("%s: Read extend caps data failed, use cap from dpcd 0.\n", __func__);
		}
	}

	link->dpcd_caps.dpcd_rev.raw =
			dpcd_data[DP_DPCD_REV - DP_DPCD_REV];

	if (link->dpcd_caps.ext_receiver_cap_field_present) {
		for (i = 0; i < read_dpcd_retry_cnt; i++) {
			status = core_link_read_dpcd(
					link,
					DP_DPRX_FEATURE_ENUMERATION_LIST,
					&dpcd_dprx_data,
					sizeof(dpcd_dprx_data));
			if (status == DC_OK)
				break;
		}

		link->dpcd_caps.dprx_feature.raw = dpcd_dprx_data;

		if (status != DC_OK)
			dm_error("%s: Read DPRX caps data failed.\n", __func__);
	}

	else {
		link->dpcd_caps.dprx_feature.raw = 0;
	}


	/* Error condition checking...
	 * It is impossible for Sink to report Max Lane Count = 0.
	 * It is possible for Sink to report Max Link Rate = 0, if it is
	 * an eDP device that is reporting specialized link rates in the
	 * SUPPORTED_LINK_RATE table.
	 */
	if (dpcd_data[DP_MAX_LANE_COUNT - DP_DPCD_REV] == 0)
		return false;

	ds_port.byte = dpcd_data[DP_DOWNSTREAMPORT_PRESENT -
				 DP_DPCD_REV];

	read_dp_device_vendor_id(link);

	/* TODO - decouple raw mst capability from policy decision */
	link->dpcd_caps.is_mst_capable = is_mst_supported(link);

	get_active_converter_info(ds_port.byte, link);

	dp_wa_power_up_0010FA(link, dpcd_data, sizeof(dpcd_data));

	down_strm_port_count.raw = dpcd_data[DP_DOWN_STREAM_PORT_COUNT -
				 DP_DPCD_REV];

	link->dpcd_caps.allow_invalid_MSA_timing_param =
		down_strm_port_count.bits.IGNORE_MSA_TIMING_PARAM;

	link->dpcd_caps.max_ln_count.raw = dpcd_data[
		DP_MAX_LANE_COUNT - DP_DPCD_REV];

	link->dpcd_caps.max_down_spread.raw = dpcd_data[
		DP_MAX_DOWNSPREAD - DP_DPCD_REV];

	link->reported_link_cap.lane_count =
		link->dpcd_caps.max_ln_count.bits.MAX_LANE_COUNT;
	link->reported_link_cap.link_rate = get_link_rate_from_max_link_bw(
			dpcd_data[DP_MAX_LINK_RATE - DP_DPCD_REV]);
	link->reported_link_cap.link_spread =
		link->dpcd_caps.max_down_spread.bits.MAX_DOWN_SPREAD ?
		LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	edp_config_cap.raw = dpcd_data[
		DP_EDP_CONFIGURATION_CAP - DP_DPCD_REV];
	link->dpcd_caps.panel_mode_edp =
		edp_config_cap.bits.ALT_SCRAMBLER_RESET;
	link->dpcd_caps.dpcd_display_control_capable =
		edp_config_cap.bits.DPCD_DISPLAY_CONTROL_CAPABLE;

	link->test_pattern_enabled = false;
	link->compliance_test_state.raw = 0;

	/* read sink count */
	core_link_read_dpcd(link,
			DP_SINK_COUNT,
			&link->dpcd_caps.sink_count.raw,
			sizeof(link->dpcd_caps.sink_count.raw));

	/* read sink ieee oui */
	core_link_read_dpcd(link,
			DP_SINK_OUI,
			(uint8_t *)(&sink_id),
			sizeof(sink_id));

	link->dpcd_caps.sink_dev_id =
			(sink_id.ieee_oui[0] << 16) +
			(sink_id.ieee_oui[1] << 8) +
			(sink_id.ieee_oui[2]);

	memmove(
		link->dpcd_caps.sink_dev_id_str,
		sink_id.ieee_device_id,
		sizeof(sink_id.ieee_device_id));

	/* Quirk Apple MBP 2017 15" Retina panel: Wrong DP_MAX_LINK_RATE */
	{
		uint8_t str_mbp_2017[] = { 101, 68, 21, 101, 98, 97 };

		if ((link->dpcd_caps.sink_dev_id == 0x0010fa) &&
		    !memcmp(link->dpcd_caps.sink_dev_id_str, str_mbp_2017,
			    sizeof(str_mbp_2017))) {
			link->reported_link_cap.link_rate = 0x0c;
		}
	}

	core_link_read_dpcd(
		link,
		DP_SINK_HW_REVISION_START,
		(uint8_t *)&dp_hw_fw_revision,
		sizeof(dp_hw_fw_revision));

	link->dpcd_caps.sink_hw_revision =
		dp_hw_fw_revision.ieee_hw_rev;

	memmove(
		link->dpcd_caps.sink_fw_revision,
		dp_hw_fw_revision.ieee_fw_rev,
		sizeof(dp_hw_fw_revision.ieee_fw_rev));

	memset(&link->dpcd_caps.dsc_caps, '\0',
			sizeof(link->dpcd_caps.dsc_caps));
	memset(&link->dpcd_caps.fec_cap, '\0', sizeof(link->dpcd_caps.fec_cap));
	/* Read DSC and FEC sink capabilities if DP revision is 1.4 and up */
	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_14) {
		status = core_link_read_dpcd(
				link,
				DP_FEC_CAPABILITY,
				&link->dpcd_caps.fec_cap.raw,
				sizeof(link->dpcd_caps.fec_cap.raw));
		status = core_link_read_dpcd(
				link,
				DP_DSC_SUPPORT,
				link->dpcd_caps.dsc_caps.dsc_basic_caps.raw,
				sizeof(link->dpcd_caps.dsc_caps.dsc_basic_caps.raw));
		if (link->dpcd_caps.dongle_type != DISPLAY_DONGLE_NONE) {
			status = core_link_read_dpcd(
					link,
					DP_DSC_BRANCH_OVERALL_THROUGHPUT_0,
					link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
					sizeof(link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw));
			DC_LOG_DSC("DSC branch decoder capability is read at link %d", link->link_index);
			DC_LOG_DSC("\tBRANCH_OVERALL_THROUGHPUT_0 = 0x%02x",
					link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.fields.BRANCH_OVERALL_THROUGHPUT_0);
			DC_LOG_DSC("\tBRANCH_OVERALL_THROUGHPUT_1 = 0x%02x",
					link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.fields.BRANCH_OVERALL_THROUGHPUT_1);
			DC_LOG_DSC("\tBRANCH_MAX_LINE_WIDTH 0x%02x",
					link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.fields.BRANCH_MAX_LINE_WIDTH);
		}

		/* Apply work around to disable FEC and DSC for USB4 tunneling in TBT3 compatibility mode
		 * only if required.
		 */
		if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA &&
#if defined(CONFIG_DRM_AMD_DC_DCN)
				!link->dc->debug.dpia_debug.bits.disable_force_tbt3_work_around &&
#endif
				link->dpcd_caps.is_branch_dev &&
				link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_90CC24 &&
				link->dpcd_caps.branch_hw_revision == DP_BRANCH_HW_REV_10 &&
				(link->dpcd_caps.fec_cap.bits.FEC_CAPABLE ||
				link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT)) {
			/* A TBT3 device is expected to report no support for FEC or DSC to a USB4 DPIA.
			 * Clear FEC and DSC capabilities as a work around if that is not the case.
			 */
			link->wa_flags.dpia_forced_tbt3_mode = true;
			memset(&link->dpcd_caps.dsc_caps, '\0', sizeof(link->dpcd_caps.dsc_caps));
			memset(&link->dpcd_caps.fec_cap, '\0', sizeof(link->dpcd_caps.fec_cap));
			DC_LOG_DSC("Clear DSC SUPPORT for USB4 link(%d) in TBT3 compatibility mode", link->link_index);
		} else
			link->wa_flags.dpia_forced_tbt3_mode = false;
	}

	if (!dpcd_read_sink_ext_caps(link))
		link->dpcd_sink_ext_caps.raw = 0;

	link->dpcd_caps.channel_coding_cap.raw = dpcd_data[DP_MAIN_LINK_CHANNEL_CODING_CAP - DP_DPCD_REV];

	if (link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED) {
		DC_LOG_DP2("128b/132b encoding is supported at link %d", link->link_index);

		core_link_read_dpcd(link,
				DP_128b_132b_SUPPORTED_LINK_RATES,
				&link->dpcd_caps.dp_128b_132b_supported_link_rates.raw,
				sizeof(link->dpcd_caps.dp_128b_132b_supported_link_rates.raw));
		if (link->dpcd_caps.dp_128b_132b_supported_link_rates.bits.UHBR20)
			link->reported_link_cap.link_rate = LINK_RATE_UHBR20;
		else if (link->dpcd_caps.dp_128b_132b_supported_link_rates.bits.UHBR13_5)
			link->reported_link_cap.link_rate = LINK_RATE_UHBR13_5;
		else if (link->dpcd_caps.dp_128b_132b_supported_link_rates.bits.UHBR10)
			link->reported_link_cap.link_rate = LINK_RATE_UHBR10;
		else
			dm_error("%s: Invalid RX 128b_132b_supported_link_rates\n", __func__);
		DC_LOG_DP2("128b/132b supported link rates is read at link %d", link->link_index);
		DC_LOG_DP2("\tmax 128b/132b link rate support is %d.%d GHz",
				link->reported_link_cap.link_rate / 100,
				link->reported_link_cap.link_rate % 100);

		core_link_read_dpcd(link,
				DP_SINK_VIDEO_FALLBACK_FORMATS,
				&link->dpcd_caps.fallback_formats.raw,
				sizeof(link->dpcd_caps.fallback_formats.raw));
		DC_LOG_DP2("sink video fallback format is read at link %d", link->link_index);
		if (link->dpcd_caps.fallback_formats.bits.dp_1920x1080_60Hz_24bpp_support)
			DC_LOG_DP2("\t1920x1080@60Hz 24bpp fallback format supported");
		if (link->dpcd_caps.fallback_formats.bits.dp_1280x720_60Hz_24bpp_support)
			DC_LOG_DP2("\t1280x720@60Hz 24bpp fallback format supported");
		if (link->dpcd_caps.fallback_formats.bits.dp_1024x768_60Hz_24bpp_support)
			DC_LOG_DP2("\t1024x768@60Hz 24bpp fallback format supported");
		if (link->dpcd_caps.fallback_formats.raw == 0) {
			DC_LOG_DP2("\tno supported fallback formats, assume 1920x1080@60Hz 24bpp is supported");
			link->dpcd_caps.fallback_formats.bits.dp_1920x1080_60Hz_24bpp_support = 1;
		}

		core_link_read_dpcd(link,
				DP_FEC_CAPABILITY_1,
				&link->dpcd_caps.fec_cap1.raw,
				sizeof(link->dpcd_caps.fec_cap1.raw));
		DC_LOG_DP2("FEC CAPABILITY 1 is read at link %d", link->link_index);
		if (link->dpcd_caps.fec_cap1.bits.AGGREGATED_ERROR_COUNTERS_CAPABLE)
			DC_LOG_DP2("\tFEC aggregated error counters are supported");
	}

	/* Connectivity log: detection */
	CONN_DATA_DETECT(link, dpcd_data, sizeof(dpcd_data), "Rx Caps: ");

	return true;
}

bool dp_overwrite_extended_receiver_cap(struct dc_link *link)
{
	uint8_t dpcd_data[16];
	uint32_t read_dpcd_retry_cnt = 3;
	enum dc_status status = DC_ERROR_UNEXPECTED;
	union dp_downstream_port_present ds_port = { 0 };
	union down_stream_port_count down_strm_port_count;
	union edp_configuration_cap edp_config_cap;

	int i;

	for (i = 0; i < read_dpcd_retry_cnt; i++) {
		status = core_link_read_dpcd(
				link,
				DP_DPCD_REV,
				dpcd_data,
				sizeof(dpcd_data));
		if (status == DC_OK)
			break;
	}

	link->dpcd_caps.dpcd_rev.raw =
		dpcd_data[DP_DPCD_REV - DP_DPCD_REV];

	if (dpcd_data[DP_MAX_LANE_COUNT - DP_DPCD_REV] == 0)
		return false;

	ds_port.byte = dpcd_data[DP_DOWNSTREAMPORT_PRESENT -
			DP_DPCD_REV];

	get_active_converter_info(ds_port.byte, link);

	down_strm_port_count.raw = dpcd_data[DP_DOWN_STREAM_PORT_COUNT -
			DP_DPCD_REV];

	link->dpcd_caps.allow_invalid_MSA_timing_param =
		down_strm_port_count.bits.IGNORE_MSA_TIMING_PARAM;

	link->dpcd_caps.max_ln_count.raw = dpcd_data[
		DP_MAX_LANE_COUNT - DP_DPCD_REV];

	link->dpcd_caps.max_down_spread.raw = dpcd_data[
		DP_MAX_DOWNSPREAD - DP_DPCD_REV];

	link->reported_link_cap.lane_count =
		link->dpcd_caps.max_ln_count.bits.MAX_LANE_COUNT;
	link->reported_link_cap.link_rate = dpcd_data[
		DP_MAX_LINK_RATE - DP_DPCD_REV];
	link->reported_link_cap.link_spread =
		link->dpcd_caps.max_down_spread.bits.MAX_DOWN_SPREAD ?
		LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	edp_config_cap.raw = dpcd_data[
		DP_EDP_CONFIGURATION_CAP - DP_DPCD_REV];
	link->dpcd_caps.panel_mode_edp =
		edp_config_cap.bits.ALT_SCRAMBLER_RESET;
	link->dpcd_caps.dpcd_display_control_capable =
		edp_config_cap.bits.DPCD_DISPLAY_CONTROL_CAPABLE;

	return true;
}

bool detect_dp_sink_caps(struct dc_link *link)
{
	return retrieve_link_cap(link);
}

static enum dc_link_rate linkRateInKHzToLinkRateMultiplier(uint32_t link_rate_in_khz)
{
	enum dc_link_rate link_rate;
	// LinkRate is normally stored as a multiplier of 0.27 Gbps per lane. Do the translation.
	switch (link_rate_in_khz) {
	case 1620000:
		link_rate = LINK_RATE_LOW;		// Rate_1 (RBR)		- 1.62 Gbps/Lane
		break;
	case 2160000:
		link_rate = LINK_RATE_RATE_2;	// Rate_2			- 2.16 Gbps/Lane
		break;
	case 2430000:
		link_rate = LINK_RATE_RATE_3;	// Rate_3			- 2.43 Gbps/Lane
		break;
	case 2700000:
		link_rate = LINK_RATE_HIGH;		// Rate_4 (HBR)		- 2.70 Gbps/Lane
		break;
	case 3240000:
		link_rate = LINK_RATE_RBR2;		// Rate_5 (RBR2)	- 3.24 Gbps/Lane
		break;
	case 4320000:
		link_rate = LINK_RATE_RATE_6;	// Rate_6			- 4.32 Gbps/Lane
		break;
	case 5400000:
		link_rate = LINK_RATE_HIGH2;	// Rate_7 (HBR2)	- 5.40 Gbps/Lane
		break;
	case 8100000:
		link_rate = LINK_RATE_HIGH3;	// Rate_8 (HBR3)	- 8.10 Gbps/Lane
		break;
	default:
		link_rate = LINK_RATE_UNKNOWN;
		break;
	}
	return link_rate;
}

void detect_edp_sink_caps(struct dc_link *link)
{
	uint8_t supported_link_rates[16];
	uint32_t entry;
	uint32_t link_rate_in_khz;
	enum dc_link_rate link_rate = LINK_RATE_UNKNOWN;
	uint8_t backlight_adj_cap;

	retrieve_link_cap(link);
	link->dpcd_caps.edp_supported_link_rates_count = 0;
	memset(supported_link_rates, 0, sizeof(supported_link_rates));

	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_13 &&
			(link->dc->debug.optimize_edp_link_rate ||
			link->reported_link_cap.link_rate == LINK_RATE_UNKNOWN)) {
		// Read DPCD 00010h - 0001Fh 16 bytes at one shot
		core_link_read_dpcd(link, DP_SUPPORTED_LINK_RATES,
							supported_link_rates, sizeof(supported_link_rates));

		for (entry = 0; entry < 16; entry += 2) {
			// DPCD register reports per-lane link rate = 16-bit link rate capability
			// value X 200 kHz. Need multiplier to find link rate in kHz.
			link_rate_in_khz = (supported_link_rates[entry+1] * 0x100 +
										supported_link_rates[entry]) * 200;

			if (link_rate_in_khz != 0) {
				link_rate = linkRateInKHzToLinkRateMultiplier(link_rate_in_khz);
				link->dpcd_caps.edp_supported_link_rates[link->dpcd_caps.edp_supported_link_rates_count] = link_rate;
				link->dpcd_caps.edp_supported_link_rates_count++;

				if (link->reported_link_cap.link_rate < link_rate)
					link->reported_link_cap.link_rate = link_rate;
			}
		}
	}
	core_link_read_dpcd(link, DP_EDP_BACKLIGHT_ADJUSTMENT_CAP,
						&backlight_adj_cap, sizeof(backlight_adj_cap));

	link->dpcd_caps.dynamic_backlight_capable_edp =
				(backlight_adj_cap & DP_EDP_DYNAMIC_BACKLIGHT_CAP) ? true:false;

	dc_link_set_default_brightness_aux(link);
}

void dc_link_dp_enable_hpd(const struct dc_link *link)
{
	struct link_encoder *encoder = link->link_enc;

	if (encoder != NULL && encoder->funcs->enable_hpd != NULL)
		encoder->funcs->enable_hpd(encoder);
}

void dc_link_dp_disable_hpd(const struct dc_link *link)
{
	struct link_encoder *encoder = link->link_enc;

	if (encoder != NULL && encoder->funcs->enable_hpd != NULL)
		encoder->funcs->disable_hpd(encoder);
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
			if (link->dc->debug.apply_vendor_specific_lttpr_wa &&
					(link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
					link->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
				dpcd_set_lane_settings(link, p_link_settings, DPRX);
				vendor_specific_lttpr_wa_five(
						link,
						p_link_settings->dpcd_lane_settings,
						p_link_settings->link_settings.lane_count);
			} else {
				dp_set_hw_lane_settings(link, &pipe_ctx->link_res, p_link_settings, DPRX);
				dpcd_set_lane_settings(link, p_link_settings, DPRX);
			}
		}

		/* Blank stream if running test pattern */
		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/*TODO:
			 * m_pHwss->
			 * MuteAudioEndpoint(pPathMode->pDisplayPath, true);
			 */
			/* Blank stream */
			pipes->stream_res.stream_enc->funcs->dp_blank(link, pipe_ctx->stream_res.stream_enc);
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
		case DP_TEST_PATTERN_SQUARE_PULSE:
			pattern = PHY_TEST_PATTERN_SQUARE_PULSE;
			break;
		default:
			return false;
		}

		if (test_pattern == DP_TEST_PATTERN_VIDEO_MODE
		/*TODO:&& !pPathMode->pDisplayPath->IsTargetPoweredOn()*/)
			return false;

		if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
#if defined(CONFIG_DRM_AMD_DC_DCN)
			if (test_pattern == DP_TEST_PATTERN_SQUARE_PULSE)
				core_link_write_dpcd(link,
						DP_LINK_SQUARE_PATTERN,
						p_custom_pattern,
						1);

#endif
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

void dpcd_set_source_specific_data(struct dc_link *link)
{
	if (!link->dc->vendor_signature.is_valid) {
		enum dc_status __maybe_unused result_write_min_hblank = DC_NOT_SUPPORTED;
		struct dpcd_amd_signature amd_signature = {0};
		struct dpcd_amd_device_id amd_device_id = {0};

		amd_device_id.device_id_byte1 =
				(uint8_t)(link->ctx->asic_id.chip_id);
		amd_device_id.device_id_byte2 =
				(uint8_t)(link->ctx->asic_id.chip_id >> 8);
		amd_device_id.dce_version =
				(uint8_t)(link->ctx->dce_version);
		amd_device_id.dal_version_byte1 = 0x0; // needed? where to get?
		amd_device_id.dal_version_byte2 = 0x0; // needed? where to get?

		core_link_read_dpcd(link, DP_SOURCE_OUI,
				(uint8_t *)(&amd_signature),
				sizeof(amd_signature));

		if (!((amd_signature.AMD_IEEE_TxSignature_byte1 == 0x0) &&
			(amd_signature.AMD_IEEE_TxSignature_byte2 == 0x0) &&
			(amd_signature.AMD_IEEE_TxSignature_byte3 == 0x1A))) {

			amd_signature.AMD_IEEE_TxSignature_byte1 = 0x0;
			amd_signature.AMD_IEEE_TxSignature_byte2 = 0x0;
			amd_signature.AMD_IEEE_TxSignature_byte3 = 0x1A;

			core_link_write_dpcd(link, DP_SOURCE_OUI,
				(uint8_t *)(&amd_signature),
				sizeof(amd_signature));
		}

		core_link_write_dpcd(link, DP_SOURCE_OUI+0x03,
				(uint8_t *)(&amd_device_id),
				sizeof(amd_device_id));

		if (link->ctx->dce_version >= DCN_VERSION_2_0 &&
			link->dc->caps.min_horizontal_blanking_period != 0) {

			uint8_t hblank_size = (uint8_t)link->dc->caps.min_horizontal_blanking_period;

			if (link->preferred_link_setting.dpcd_source_device_specific_field_support) {
				result_write_min_hblank = core_link_write_dpcd(link,
					DP_SOURCE_MINIMUM_HBLANK_SUPPORTED, (uint8_t *)(&hblank_size),
					sizeof(hblank_size));

				if (result_write_min_hblank == DC_ERROR_UNEXPECTED)
					link->preferred_link_setting.dpcd_source_device_specific_field_support = false;
			} else {
				DC_LOG_DC("Sink device does not support 00340h DPCD write. Skipping on purpose.\n");
			}
		}

		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_INFORMATION,
							WPP_BIT_FLAG_DC_DETECTION_DP_CAPS,
							"result=%u link_index=%u enum dce_version=%d DPCD=0x%04X min_hblank=%u branch_dev_id=0x%x branch_dev_name='%c%c%c%c%c%c'",
							result_write_min_hblank,
							link->link_index,
							link->ctx->dce_version,
							DP_SOURCE_MINIMUM_HBLANK_SUPPORTED,
							link->dc->caps.min_horizontal_blanking_period,
							link->dpcd_caps.branch_dev_id,
							link->dpcd_caps.branch_dev_name[0],
							link->dpcd_caps.branch_dev_name[1],
							link->dpcd_caps.branch_dev_name[2],
							link->dpcd_caps.branch_dev_name[3],
							link->dpcd_caps.branch_dev_name[4],
							link->dpcd_caps.branch_dev_name[5]);
	} else {
		core_link_write_dpcd(link, DP_SOURCE_OUI,
				link->dc->vendor_signature.data.raw,
				sizeof(link->dc->vendor_signature.data.raw));
	}
}

void dpcd_update_cable_id(struct dc_link *link)
{
	struct link_encoder *link_enc = NULL;

	link_enc = link_enc_cfg_get_link_enc(link);

	if (!link_enc ||
			!link_enc->features.flags.bits.IS_UHBR10_CAPABLE ||
			link->dprx_status.cable_id_updated)
		return;

	/* Retrieve cable attributes */
	if (!is_usbc_connector(link))
		core_link_read_dpcd(link, DP_CABLE_ATTRIBUTES_UPDATED_BY_DPRX,
				&link->dpcd_caps.cable_attributes.raw,
				sizeof(uint8_t));

	/* Update receiver with cable attributes */
	core_link_write_dpcd(link, DP_CABLE_ATTRIBUTES_UPDATED_BY_DPTX,
			&link->dpcd_caps.cable_attributes.raw,
			sizeof(link->dpcd_caps.cable_attributes.raw));

	link->dprx_status.cable_id_updated = 1;
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

	if (core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_CURRENT_PEAK,
			dpcd_backlight_get.raw,
			sizeof(union dpcd_source_backlight_get)) != DC_OK)
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

	if (core_link_read_dpcd(link, DP_SOURCE_BACKLIGHT_LEVEL,
		(uint8_t *) backlight_millinits,
		sizeof(uint32_t)) != DC_OK)
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
			!link->dc->debug.optimize_edp_link_rate)
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
		decide_edp_link_settings(link, &link_setting, req_bw);
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

enum dp_link_encoding dp_get_link_encoding_format(const struct dc_link_settings *link_settings)
{
	if ((link_settings->link_rate >= LINK_RATE_LOW) &&
			(link_settings->link_rate <= LINK_RATE_HIGH3))
		return DP_8b_10b_ENCODING;
	else if ((link_settings->link_rate >= LINK_RATE_UHBR10) &&
			(link_settings->link_rate <= LINK_RATE_UHBR20))
		return DP_128b_132b_ENCODING;
	return DP_UNKNOWN_ENCODING;
}

enum dp_link_encoding dc_link_dp_mst_decide_link_encoding_format(const struct dc_link *link)
{
	struct dc_link_settings link_settings = {0};

	if (!dc_is_dp_signal(link->connector_signal))
		return DP_UNKNOWN_ENCODING;

	if (link->preferred_link_setting.lane_count !=
			LANE_COUNT_UNKNOWN &&
			link->preferred_link_setting.link_rate !=
					LINK_RATE_UNKNOWN) {
		link_settings = link->preferred_link_setting;
	} else {
		decide_mst_link_settings(link, &link_settings);
	}

	return dp_get_link_encoding_format(&link_settings);
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
	} else {
		/// Leave req_slot_count = 0 if allocate is false.
	}

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

	ASSERT(req_slot_count <= MAX_MTP_SLOT_COUNT); /// Validation should filter out modes that exceed link BW
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

	proposed_table->stream_count = 1; /// Always 1 stream for SST
	proposed_table->stream_allocations[0].slot_count = req_slot_count;
	proposed_table->stream_allocations[0].vcp_id = vc_id;

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

bool is_dp_128b_132b_signal(struct pipe_ctx *pipe_ctx)
{
	/* If this assert is hit then we have a link encoder dynamic management issue */
	ASSERT(pipe_ctx->stream_res.hpo_dp_stream_enc ? pipe_ctx->link_res.hpo_dp_link_enc != NULL : true);
	return (pipe_ctx->stream_res.hpo_dp_stream_enc &&
			pipe_ctx->link_res.hpo_dp_link_enc &&
			dc_is_dp_signal(pipe_ctx->stream->signal));
}

void edp_panel_backlight_power_on(struct dc_link *link)
{
	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return;

	link->dc->hwss.edp_power_control(link, true);
	link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	if (link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_backlight_control(link, true);
}

void dc_link_dp_clear_rx_status(struct dc_link *link)
{
	memset(&link->dprx_status, 0, sizeof(link->dprx_status));
}

void dp_receiver_power_ctrl(struct dc_link *link, bool on)
{
	uint8_t state;

	state = on ? DP_POWER_STATE_D0 : DP_POWER_STATE_D3;

	if (link->sync_lt_in_progress)
		return;

	core_link_write_dpcd(link, DP_SET_POWER, &state,
						 sizeof(state));

}

void dp_source_sequence_trace(struct dc_link *link, uint8_t dp_test_mode)
{
	if (link != NULL && link->dc->debug.enable_driver_sequence_debug)
		core_link_write_dpcd(link, DP_SOURCE_SEQUENCE,
					&dp_test_mode, sizeof(dp_test_mode));
}


static uint8_t convert_to_count(uint8_t lttpr_repeater_count)
{
	switch (lttpr_repeater_count) {
	case 0x80: // 1 lttpr repeater
		return 1;
	case 0x40: // 2 lttpr repeaters
		return 2;
	case 0x20: // 3 lttpr repeaters
		return 3;
	case 0x10: // 4 lttpr repeaters
		return 4;
	case 0x08: // 5 lttpr repeaters
		return 5;
	case 0x04: // 6 lttpr repeaters
		return 6;
	case 0x02: // 7 lttpr repeaters
		return 7;
	case 0x01: // 8 lttpr repeaters
		return 8;
	default:
		break;
	}
	return 0; // invalid value
}

static inline bool is_immediate_downstream(struct dc_link *link, uint32_t offset)
{
	return (convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) == offset);
}

void dp_enable_link_phy(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum signal_type signal,
	enum clock_source_id clock_source,
	const struct dc_link_settings *link_settings)
{
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	struct pipe_ctx *pipes =
			link->dc->current_state->res_ctx.pipe_ctx;
	struct clock_source *dp_cs =
			link->dc->res_pool->dp_clock_source;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	unsigned int i;

	if (link->connector_signal == SIGNAL_TYPE_EDP) {
		link->dc->hwss.edp_power_control(link, true);
		link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	}

	/* If the current pixel clock source is not DTO(happens after
	 * switching from HDMI passive dongle to DP on the same connector),
	 * switch the pixel clock source to DTO.
	 */
	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream != NULL &&
			pipes[i].stream->link == link) {
			if (pipes[i].clock_source != NULL &&
					pipes[i].clock_source->id != CLOCK_SOURCE_ID_DP_DTO) {
				pipes[i].clock_source = dp_cs;
				pipes[i].stream_res.pix_clk_params.requested_pix_clk_100hz =
						pipes[i].stream->timing.pix_clk_100hz;
				pipes[i].clock_source->funcs->program_pix_clk(
							pipes[i].clock_source,
							&pipes[i].stream_res.pix_clk_params,
							&pipes[i].pll_settings);
			}
		}
	}

	link->cur_link_settings = *link_settings;

	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING) {
		if (dc->clk_mgr->funcs->notify_link_rate_change)
			dc->clk_mgr->funcs->notify_link_rate_change(dc->clk_mgr, link);
	}

	if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	if (link_hwss->ext.enable_dp_link_output)
		link_hwss->ext.enable_dp_link_output(link, link_res, signal,
				clock_source, link_settings);

	if (dmcu != NULL && dmcu->funcs->unlock_phy)
		dmcu->funcs->unlock_phy(dmcu);

	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_LINK_PHY);
	dp_receiver_power_ctrl(link, true);
}

void edp_add_delay_for_T9(struct dc_link *link)
{
	if (link->local_sink &&
			link->local_sink->edid_caps.panel_patch.extra_delay_backlight_off > 0)
		udelay(link->local_sink->edid_caps.panel_patch.extra_delay_backlight_off * 1000);
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

	if (link->local_sink &&
			link->local_sink->edid_caps.panel_patch.extra_t7_ms > 0)
		udelay(link->local_sink->edid_caps.panel_patch.extra_t7_ms * 1000);

	return result;
}

void dp_disable_link_phy(struct dc_link *link, const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc  *dc = link->ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);

	if (!link->wa_flags.dp_keep_receiver_powered)
		dp_receiver_power_ctrl(link, false);

	if (signal == SIGNAL_TYPE_EDP) {
		if (link->dc->hwss.edp_backlight_control)
			link->dc->hwss.edp_backlight_control(link, false);
		if (link_hwss->ext.disable_dp_link_output)
			link_hwss->ext.disable_dp_link_output(link, link_res, signal);
		link->dc->hwss.edp_power_control(link, false);
	} else {
		if (dmcu != NULL && dmcu->funcs->lock_phy)
			dmcu->funcs->lock_phy(dmcu);
		if (link_hwss->ext.disable_dp_link_output)
			link_hwss->ext.disable_dp_link_output(link, link_res, signal);
		if (dmcu != NULL && dmcu->funcs->unlock_phy)
			dmcu->funcs->unlock_phy(dmcu);
	}

	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);

	/* Clear current link setting.*/
	memset(&link->cur_link_settings, 0,
			sizeof(link->cur_link_settings));

	if (dc->clk_mgr->funcs->notify_link_rate_change)
		dc->clk_mgr->funcs->notify_link_rate_change(dc->clk_mgr, link);
}

void dp_disable_link_phy_mst(struct dc_link *link, const struct link_resource *link_res,
		enum signal_type signal)
{
	/* MST disable link only when no stream use the link */
	if (link->mst_stream_alloc_table.stream_count > 0)
		return;

	dp_disable_link_phy(link, link_res, signal);

	/* set the sink to SST mode after disabling the link */
	dp_enable_mst_on_sink(link, false);
}

bool dp_set_hw_training_pattern(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum dc_dp_training_pattern pattern,
	uint32_t offset)
{
	enum dp_test_pattern test_pattern = DP_TEST_PATTERN_UNSUPPORTED;

	switch (pattern) {
	case DP_TRAINING_PATTERN_SEQUENCE_1:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN1;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_2:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN2;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_3:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN3;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_4:
		test_pattern = DP_TEST_PATTERN_TRAINING_PATTERN4;
		break;
	case DP_128b_132b_TPS1:
		test_pattern = DP_TEST_PATTERN_128b_132b_TPS1_TRAINING_MODE;
		break;
	case DP_128b_132b_TPS2:
		test_pattern = DP_TEST_PATTERN_128b_132b_TPS2_TRAINING_MODE;
		break;
	default:
		break;
	}

	dp_set_hw_test_pattern(link, link_res, test_pattern, NULL, 0);

	return true;
}

void dp_set_hw_lane_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct link_training_settings *link_settings,
	uint32_t offset)
{
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);

	if ((link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) && !is_immediate_downstream(link, offset))
		return;

	if (link_hwss->ext.set_dp_lane_settings)
		link_hwss->ext.set_dp_lane_settings(link, link_res,
				&link_settings->link_settings,
				link_settings->hw_lane_settings);

	memmove(link->cur_lane_setting,
			link_settings->hw_lane_settings,
			sizeof(link->cur_lane_setting));
}

void dp_set_hw_test_pattern(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum dp_test_pattern test_pattern,
	uint8_t *custom_pattern,
	uint32_t custom_pattern_size)
{
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	struct encoder_set_dp_phy_pattern_param pattern_param = {0};

	pattern_param.dp_phy_pattern = test_pattern;
	pattern_param.custom_pattern = custom_pattern;
	pattern_param.custom_pattern_size = custom_pattern_size;
	pattern_param.dp_panel_mode = dp_get_panel_mode(link);

	if (link_hwss->ext.set_dp_link_test_pattern)
		link_hwss->ext.set_dp_link_test_pattern(link, link_res, &pattern_param);
}

void dp_retrain_link_dp_test(struct dc_link *link,
			struct dc_link_settings *link_setting,
			bool skip_video_pattern)
{
	struct pipe_ctx *pipes =
			&link->dc->current_state->res_ctx.pipe_ctx[0];
	unsigned int i;


	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream != NULL &&
			!pipes[i].top_pipe && !pipes[i].prev_odm_pipe &&
			pipes[i].stream->link != NULL &&
			pipes[i].stream_res.stream_enc != NULL &&
			pipes[i].stream->link == link) {
			udelay(100);

			pipes[i].stream_res.stream_enc->funcs->dp_blank(link,
					pipes[i].stream_res.stream_enc);

			/* disable any test pattern that might be active */
			dp_set_hw_test_pattern(link, &pipes[i].link_res,
					DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

			dp_receiver_power_ctrl(link, false);

			link->dc->hwss.disable_stream(&pipes[i]);
			if ((&pipes[i])->stream_res.audio && !link->dc->debug.az_endpoint_mute_only)
				(&pipes[i])->stream_res.audio->funcs->az_disable((&pipes[i])->stream_res.audio);

			if (link->link_enc)
				link->link_enc->funcs->disable_output(
						link->link_enc,
						SIGNAL_TYPE_DISPLAY_PORT);

			/* Clear current link setting. */
			memset(&link->cur_link_settings, 0,
				sizeof(link->cur_link_settings));

			perform_link_training_with_retries(
					link_setting,
					skip_video_pattern,
					LINK_TRAINING_ATTEMPTS,
					&pipes[i],
					SIGNAL_TYPE_DISPLAY_PORT,
					false);

			link->dc->hwss.enable_stream(&pipes[i]);

			link->dc->hwss.unblank_stream(&pipes[i],
					link_setting);

			if (pipes[i].stream_res.audio) {
				/* notify audio driver for
				 * audio modes of monitor */
				pipes[i].stream_res.audio->funcs->az_enable(
						pipes[i].stream_res.audio);

				/* un-mute audio */
				/* TODO: audio should be per stream rather than
				 * per link */
				pipes[i].stream_res.stream_enc->funcs->
				audio_mute_control(
					pipes[i].stream_res.stream_enc, false);
			}
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
				&& !is_dp_128b_132b_signal(pipe_ctx)) {
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
			if (is_dp_128b_132b_signal(pipe_ctx))
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

		DC_LOG_DSC(" ");
		dsc->funcs->dsc_get_packed_pps(dsc, &dsc_cfg, &dsc_packed_pps[0]);
		if (dc_is_dp_signal(stream->signal)) {
			DC_LOG_DSC("Setting stream encoder DSC PPS SDP for engine %d\n", (int)pipe_ctx->stream_res.stream_enc->id);
			if (is_dp_128b_132b_signal(pipe_ctx))
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
		if (dc_is_dp_signal(stream->signal)) {
			if (is_dp_128b_132b_signal(pipe_ctx))
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
