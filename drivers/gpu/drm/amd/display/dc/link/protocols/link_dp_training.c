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

/* FILE POLICY AND INTENDED USAGE:
 * This file implements all generic dp link training helper functions and top
 * level generic training sequence. All variations of dp link training sequence
 * should be called inside the top level training functions in this file to
 * ensure the integrity of our overall training procedure across different types
 * of link encoding and back end hardware.
 */
#include "link_dp_training.h"
#include "link_dp_training_8b_10b.h"
#include "link_dp_training_128b_132b.h"
#include "link_dp_training_auxless.h"
#include "link_dp_training_dpia.h"
#include "link_dp_training_fixed_vs_pe_retimer.h"
#include "link_dpcd.h"
#include "link/accessories/link_dp_trace.h"
#include "link_dp_phy.h"
#include "link_dp_capability.h"
#include "link_edp_panel_control.h"
#include "atomfirmware.h"
#include "link_enc_cfg.h"
#include "resource.h"
#include "dm_helpers.h"

#define DC_LOGGER \
	link->ctx->logger

#define POST_LT_ADJ_REQ_LIMIT 6
#define POST_LT_ADJ_REQ_TIMEOUT 200
#define LINK_TRAINING_RETRY_DELAY 50 /* ms */

void dp_log_training_result(
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
	case LINK_TRAINING_EQ_FAIL_CR_PARTIAL:
		lt_result = "CR failed in EQ partially";
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
				lt_settings->hw_lane_settings[0].VOLTAGE_SWING,
				lt_settings->hw_lane_settings[0].PRE_EMPHASIS,
				lt_spread);
}

uint8_t dp_initialize_scrambling_data_symbols(
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

enum dpcd_training_patterns
	dp_training_pattern_to_dpcd_training_pattern(
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

uint8_t dp_get_nibble_at_index(const uint8_t *buf,
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

/* maximum pre emphasis level allowed for each voltage swing level*/
static const enum dc_pre_emphasis voltage_swing_to_pre_emphasis[] = {
		PRE_EMPHASIS_LEVEL3,
		PRE_EMPHASIS_LEVEL2,
		PRE_EMPHASIS_LEVEL1,
		PRE_EMPHASIS_DISABLED };

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

void dp_hw_to_dpcd_lane_settings(
		const struct link_training_settings *lt_settings,
		const struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
		union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX])
{
	uint8_t lane = 0;

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
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
		} else if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING) {
			dpcd_lane_settings[lane].tx_ffe.PRESET_VALUE =
					hw_lane_settings[lane].FFE_PRESET.settings.level;
		}
	}
}

uint8_t get_dpcd_link_rate(const struct dc_link_settings *link_settings)
{
	uint8_t link_rate = 0;
	enum dp_link_encoding encoding = link_dp_get_encoding_format(link_settings);

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

bool is_repeater(const struct link_training_settings *lt_settings, uint32_t offset)
{
	return (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) && (offset != 0);
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

bool dp_is_cr_done(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status)
{
	bool done = true;
	uint32_t lane;
	/*LANEx_CR_DONE bits All 1's?*/
	for (lane = 0; lane < (uint32_t)(ln_count); lane++) {
		if (!dpcd_lane_status[lane].bits.CR_DONE_0)
			done = false;
	}
	return done;

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
		lane_status.raw = dp_get_nibble_at_index(&dpcd_buf[2], lane);

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

	if (is_repeater(link_training_setting, offset)) {
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

	if (status != DC_OK) {
		DC_LOG_HW_LINK_TRAINING("%s:\n Failed to read from address 0x%X,"
			" keep current lane status and lane adjust unchanged",
			__func__,
			lane01_status_address);
		return status;
	}

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		ln_status[lane].raw =
			dp_get_nibble_at_index(&dpcd_buf[0], lane);
		ln_adjust[lane].raw =
			dp_get_nibble_at_index(&dpcd_buf[lane_adjust_offset], lane);
	}

	ln_align->raw = dpcd_buf[2];

	if (is_repeater(link_training_setting, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X Lane01Status = %x\n 0x%X Lane23Status = %x\n ",
			__func__,
			offset,
			lane01_status_address, dpcd_buf[0],
			lane01_status_address + 1, dpcd_buf[1]);

		lane01_adjust_address = DP_ADJUST_REQUEST_LANE0_1_PHY_REPEATER1 +
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X Lane01AdjustRequest = %x\n 0x%X Lane23AdjustRequest = %x\n",
					__func__,
					offset,
					lane01_adjust_address,
					dpcd_buf[lane_adjust_offset],
					lane01_adjust_address + 1,
					dpcd_buf[lane_adjust_offset + 1]);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X Lane01Status = %x\n 0x%X Lane23Status = %x\n ",
			__func__,
			lane01_status_address, dpcd_buf[0],
			lane01_status_address + 1, dpcd_buf[1]);

		lane01_adjust_address = DP_ADJUST_REQUEST_LANE0_1;

		DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X Lane01AdjustRequest = %x\n 0x%X Lane23AdjustRequest = %x\n",
			__func__,
			lane01_adjust_address,
			dpcd_buf[lane_adjust_offset],
			lane01_adjust_address + 1,
			dpcd_buf[lane_adjust_offset + 1]);
	}

	return status;
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

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
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

void dp_get_lttpr_mode_override(struct dc_link *link, enum lttpr_mode *override)
{
	if (!dp_is_lttpr_present(link))
		return;

	if (link->dc->debug.lttpr_mode_override == LTTPR_MODE_TRANSPARENT) {
		*override = LTTPR_MODE_TRANSPARENT;
	} else if (link->dc->debug.lttpr_mode_override == LTTPR_MODE_NON_TRANSPARENT) {
		*override = LTTPR_MODE_NON_TRANSPARENT;
	} else if (link->dc->debug.lttpr_mode_override == LTTPR_MODE_NON_LTTPR) {
		*override = LTTPR_MODE_NON_LTTPR;
	}
	DC_LOG_DC("lttpr_mode_override chose LTTPR_MODE = %d\n", (uint8_t)(*override));
}

void override_training_settings(
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
	if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			lt_settings->lttpr_mode == LTTPR_MODE_TRANSPARENT) {
		lt_settings->voltage_swing = &link->bios_forced_drive_settings.VOLTAGE_SWING;
		lt_settings->pre_emphasis = &link->bios_forced_drive_settings.PRE_EMPHASIS;
		lt_settings->always_match_dpcd_with_hw_lane_settings = false;
	}
	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		lt_settings->hw_lane_settings[lane].VOLTAGE_SWING =
			lt_settings->voltage_swing != NULL ?
			*lt_settings->voltage_swing :
			VOLTAGE_SWING_LEVEL0;
		lt_settings->hw_lane_settings[lane].PRE_EMPHASIS =
			lt_settings->pre_emphasis != NULL ?
			*lt_settings->pre_emphasis
			: PRE_EMPHASIS_DISABLED;
		lt_settings->hw_lane_settings[lane].POST_CURSOR2 =
			lt_settings->post_cursor2 != NULL ?
			*lt_settings->post_cursor2
			: POST_CURSOR2_DISABLED;
	}

	if (lt_settings->always_match_dpcd_with_hw_lane_settings)
		dp_hw_to_dpcd_lane_settings(lt_settings,
				lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);

	/* Override training timings */
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
	/* Check DP tunnel LTTPR mode debug option. */
	if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA && link->dc->debug.dpia_debug.bits.force_non_lttpr)
		lt_settings->lttpr_mode = LTTPR_MODE_NON_LTTPR;

#endif
	dp_get_lttpr_mode_override(link, &lt_settings->lttpr_mode);

}

enum dc_dp_training_pattern decide_cr_training_pattern(
		const struct dc_link_settings *link_settings)
{
	switch (link_dp_get_encoding_format(link_settings)) {
	case DP_8b_10b_ENCODING:
	default:
		return DP_TRAINING_PATTERN_SEQUENCE_1;
	case DP_128b_132b_ENCODING:
		return DP_128b_132b_TPS1;
	}
}

enum dc_dp_training_pattern decide_eq_training_pattern(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	struct link_encoder *link_enc;
	struct encoder_feature_support *enc_caps;
	struct dpcd_caps *rx_caps = &link->dpcd_caps;
	enum dc_dp_training_pattern pattern = DP_TRAINING_PATTERN_SEQUENCE_2;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);
	enc_caps = &link_enc->features;

	switch (link_dp_get_encoding_format(link_settings)) {
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

enum lttpr_mode dc_link_decide_lttpr_mode(struct dc_link *link,
		struct dc_link_settings *link_setting)
{
	enum dp_link_encoding encoding = link_dp_get_encoding_format(link_setting);

	if (encoding == DP_8b_10b_ENCODING)
		return dp_decide_8b_10b_lttpr_mode(link);
	else if (encoding == DP_128b_132b_ENCODING)
		return dp_decide_128b_132b_lttpr_mode(link);

	ASSERT(0);
	return LTTPR_MODE_NON_LTTPR;
}

void dp_decide_lane_settings(
		const struct link_training_settings *lt_settings,
		const union lane_adjust ln_adjust[LANE_COUNT_DP_MAX],
		struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
		union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX])
{
	uint32_t lane;

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING) {
			hw_lane_settings[lane].VOLTAGE_SWING =
					(enum dc_voltage_swing)(ln_adjust[lane].bits.
							VOLTAGE_SWING_LANE);
			hw_lane_settings[lane].PRE_EMPHASIS =
					(enum dc_pre_emphasis)(ln_adjust[lane].bits.
							PRE_EMPHASIS_LANE);
		} else if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
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

void dp_decide_training_settings(
		struct dc_link *link,
		const struct dc_link_settings *link_settings,
		struct link_training_settings *lt_settings)
{
	if (link_dp_get_encoding_format(link_settings) == DP_8b_10b_ENCODING)
		decide_8b_10b_training_settings(link, link_settings, lt_settings);
	else if (link_dp_get_encoding_format(link_settings) == DP_128b_132b_ENCODING)
		decide_128b_132b_training_settings(link, link_settings, lt_settings);
}


enum dc_status configure_lttpr_mode_transparent(struct dc_link *link)
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

	enum dp_link_encoding encoding = link_dp_get_encoding_format(&lt_settings->link_settings);

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

	if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {

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
			repeater_cnt = dp_parse_lttpr_repeater_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

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

enum dc_status dpcd_configure_lttpr_mode(struct dc_link *link, struct link_training_settings *lt_settings)
{
	enum dc_status status = DC_OK;

	if (lt_settings->lttpr_mode == LTTPR_MODE_TRANSPARENT)
		status = configure_lttpr_mode_transparent(link);

	else if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		status = configure_lttpr_mode_non_transparent(link, lt_settings);

	return status;
}

void repeater_training_done(struct dc_link *link, uint32_t offset)
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

static void dpcd_exit_training_mode(struct dc_link *link, enum dp_link_encoding encoding)
{
	uint8_t sink_status = 0;
	uint8_t i;

	/* clear training pattern set */
	dpcd_set_training_pattern(link, DP_TRAINING_PATTERN_VIDEOIDLE);

	if (encoding == DP_128b_132b_ENCODING) {
		/* poll for intra-hop disable */
		for (i = 0; i < 10; i++) {
			if ((core_link_read_dpcd(link, DP_SINK_STATUS, &sink_status, 1) == DC_OK) &&
					(sink_status & DP_INTRA_HOP_AUX_REPLY_INDICATION) == 0)
				break;
			udelay(1000);
		}
	}
}

enum dc_status dpcd_configure_channel_coding(struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	enum dp_link_encoding encoding =
			link_dp_get_encoding_format(
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

void dpcd_set_training_pattern(
	struct dc_link *link,
	enum dc_dp_training_pattern training_pattern)
{
	union dpcd_training_pattern dpcd_pattern = {0};

	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
			dp_training_pattern_to_dpcd_training_pattern(
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

enum dc_status dpcd_set_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	uint32_t offset)
{
	unsigned int lane0_set_address;
	enum dc_status status;
	lane0_set_address = DP_TRAINING_LANE0_SET;

	if (is_repeater(link_training_setting, offset))
		lane0_set_address = DP_TRAINING_LANE0_SET_PHY_REPEATER1 +
		((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	status = core_link_write_dpcd(link,
		lane0_set_address,
		(uint8_t *)(link_training_setting->dpcd_lane_settings),
		link_training_setting->link_settings.lane_count);

	if (is_repeater(link_training_setting, offset)) {
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

void dpcd_set_lt_pattern_and_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings,
	enum dc_dp_training_pattern pattern,
	uint32_t offset)
{
	uint32_t dpcd_base_lt_offset;
	uint8_t dpcd_lt_buffer[5] = {0};
	union dpcd_training_pattern dpcd_pattern = {0};
	uint32_t size_in_bytes;
	bool edp_workaround = false; /* TODO link_prop.INTERNAL */
	dpcd_base_lt_offset = DP_TRAINING_PATTERN_SET;

	if (is_repeater(lt_settings, offset))
		dpcd_base_lt_offset = DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
			((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	/*****************************************************************
	* DpcdAddress_TrainingPatternSet
	*****************************************************************/
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
		dp_training_pattern_to_dpcd_training_pattern(link, pattern);

	dpcd_pattern.v1_4.SCRAMBLING_DISABLE =
		dp_initialize_scrambling_data_symbols(link, pattern);

	dpcd_lt_buffer[DP_TRAINING_PATTERN_SET - DP_TRAINING_PATTERN_SET]
		= dpcd_pattern.raw;

	if (is_repeater(lt_settings, offset)) {
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

	if (is_repeater(lt_settings, offset)) {
		if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
					" 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					offset,
					dpcd_base_lt_offset,
					lt_settings->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
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
		if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
				DP_128b_132b_ENCODING)
			DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X TX_FFE_PRESET_VALUE = %x\n",
					__func__,
					dpcd_base_lt_offset,
					lt_settings->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
		else if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
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

	} else if (link_dp_get_encoding_format(&lt_settings->link_settings) ==
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

void start_clock_recovery_pattern_early(struct dc_link *link,
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
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
	union lane_align_status_updated dpcd_lane_status_updated = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

	req_drv_setting_changed = false;
	for (adj_req_count = 0; adj_req_count < POST_LT_ADJ_REQ_LIMIT;
	adj_req_count++) {

		req_drv_setting_changed = false;

		for (adj_req_timer = 0;
			adj_req_timer < POST_LT_ADJ_REQ_TIMEOUT;
			adj_req_timer++) {

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

				dp_set_drive_settings(link,
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

static enum link_training_result dp_transition_to_video_idle(
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

enum link_training_result dp_perform_link_training(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_settings,
	bool skip_video_pattern)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	struct link_training_settings lt_settings = {0};
	enum dp_link_encoding encoding =
			link_dp_get_encoding_format(link_settings);

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
	dpcd_exit_training_mode(link, encoding);

	/* configure link prior to entering training mode */
	dpcd_configure_lttpr_mode(link, &lt_settings);
	dp_set_fec_ready(link, link_res, lt_settings.should_set_fec_ready);
	dpcd_configure_channel_coding(link, &lt_settings);

	/* enter training mode:
	 * Per DP specs starting from here, DPTX device shall not issue
	 * Non-LT AUX transactions inside training mode.
	 */
	if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) && encoding == DP_8b_10b_ENCODING)
		status = dp_perform_fixed_vs_pe_training_sequence(link, link_res, &lt_settings);
	else if (encoding == DP_8b_10b_ENCODING)
		status = dp_perform_8b_10b_link_training(link, link_res, &lt_settings);
	else if (encoding == DP_128b_132b_ENCODING)
		status = dp_perform_128b_132b_link_training(link, link_res, &lt_settings);
	else
		ASSERT(0);

	/* exit training mode */
	dpcd_exit_training_mode(link, encoding);

	/* switch to video idle */
	if ((status == LINK_TRAINING_SUCCESS) || !skip_video_pattern)
		status = dp_transition_to_video_idle(link,
				link_res,
				&lt_settings,
				status);

	/* dump debug data */
	dp_log_training_result(link, &lt_settings, status);
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
	struct dc_link_settings cur_link_settings = *link_setting;
	struct dc_link_settings max_link_settings = *link_setting;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	int fail_count = 0;
	bool is_link_bw_low = false; /* link bandwidth < stream bandwidth */
	bool is_link_bw_min = /* RBR x 1 */
		(cur_link_settings.link_rate <= LINK_RATE_LOW) &&
		(cur_link_settings.lane_count <= LANE_COUNT_ONE);

	dp_trace_commit_lt_init(link);


	if (link_dp_get_encoding_format(&cur_link_settings) == DP_8b_10b_ENCODING)
		/* We need to do this before the link training to ensure the idle
		 * pattern in SST mode will be sent right after the link training
		 */
		link_hwss->setup_stream_encoder(pipe_ctx);

	dp_trace_set_lt_start_timestamp(link, false);
	j = 0;
	while (j < attempts && fail_count < (attempts * 10)) {

		DC_LOG_HW_LINK_TRAINING("%s: Beginning link(%d) training attempt %u of %d @ rate(%d) x lane(%d)\n",
			__func__, link->link_index, (unsigned int)j + 1, attempts, cur_link_settings.link_rate,
			cur_link_settings.lane_count);

		dp_enable_link_phy(
			link,
			&pipe_ctx->link_res,
			signal,
			pipe_ctx->clock_source->id,
			&cur_link_settings);

		if (stream->sink_patches.dppowerup_delay > 0) {
			int delay_dp_power_up_in_ms = stream->sink_patches.dppowerup_delay;

			msleep(delay_dp_power_up_in_ms);
		}

#ifdef CONFIG_DRM_AMD_DC_HDCP
		if (panel_mode == DP_PANEL_MODE_EDP) {
			struct cp_psp *cp_psp = &stream->ctx->cp_psp;

			if (cp_psp && cp_psp->funcs.enable_assr) {
				/* ASSR is bound to fail with unsigned PSP
				 * verstage used during devlopment phase.
				 * Report and continue with eDP panel mode to
				 * perform eDP link training with right settings
				 */
				bool result;
				result = cp_psp->funcs.enable_assr(cp_psp->handle, link);
			}
		}
#endif

		dp_set_panel_mode(link, panel_mode);

		if (link->aux_access_disabled) {
			dc_link_dp_perform_link_training_skip_aux(link, &pipe_ctx->link_res, &cur_link_settings);
			return true;
		} else {
			/** @todo Consolidate USB4 DP and DPx.x training. */
			if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA) {
				status = dc_link_dpia_perform_link_training(
						link,
						&pipe_ctx->link_res,
						&cur_link_settings,
						skip_video_pattern);

				/* Transmit idle pattern once training successful. */
				if (status == LINK_TRAINING_SUCCESS && !is_link_bw_low) {
					dp_set_hw_test_pattern(link, &pipe_ctx->link_res, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);
					// Update verified link settings to current one
					// Because DPIA LT might fallback to lower link setting.
					if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
						link->verified_link_cap.link_rate = link->cur_link_settings.link_rate;
						link->verified_link_cap.lane_count = link->cur_link_settings.lane_count;
						dm_helpers_dp_mst_update_branch_bandwidth(link->ctx, link);
					}
				}
			} else {
				status = dp_perform_link_training(
						link,
						&pipe_ctx->link_res,
						&cur_link_settings,
						skip_video_pattern);
			}

			dp_trace_lt_total_count_increment(link, false);
			dp_trace_lt_result_update(link, status, false);
			dp_trace_set_lt_end_timestamp(link, false);
			if (status == LINK_TRAINING_SUCCESS && !is_link_bw_low)
				return true;
		}

		fail_count++;
		dp_trace_lt_fail_count_update(link, fail_count, false);
		if (link->ep_type == DISPLAY_ENDPOINT_PHY) {
			/* latest link training still fail or link training is aborted
			 * skip delay and keep PHY on
			 */
			if (j == (attempts - 1) || (status == LINK_TRAINING_ABORT))
				break;
		}

		DC_LOG_WARNING("%s: Link(%d) training attempt %u of %d failed @ rate(%d) x lane(%d) : fail reason:(%d)\n",
			__func__, link->link_index, (unsigned int)j + 1, attempts, cur_link_settings.link_rate,
			cur_link_settings.lane_count, status);

		dp_disable_link_phy(link, &pipe_ctx->link_res, signal);

		/* Abort link training if failure due to sink being unplugged. */
		if (status == LINK_TRAINING_ABORT) {
			enum dc_connection_type type = dc_connection_none;

			dc_link_detect_connection_type(link, &type);
			if (type == dc_connection_none) {
				DC_LOG_HW_LINK_TRAINING("%s: Aborting training because sink unplugged\n", __func__);
				break;
			}
		}

		/* Try to train again at original settings if:
		 * - not falling back between training attempts;
		 * - aborted previous attempt due to reasons other than sink unplug;
		 * - successfully trained but at a link rate lower than that required by stream;
		 * - reached minimum link bandwidth.
		 */
		if (!do_fallback || (status == LINK_TRAINING_ABORT) ||
				(status == LINK_TRAINING_SUCCESS && is_link_bw_low) ||
				is_link_bw_min) {
			j++;
			cur_link_settings = *link_setting;
			delay_between_attempts += LINK_TRAINING_RETRY_DELAY;
			is_link_bw_low = false;
			is_link_bw_min = (cur_link_settings.link_rate <= LINK_RATE_LOW) &&
				(cur_link_settings.lane_count <= LANE_COUNT_ONE);

		} else if (do_fallback) { /* Try training at lower link bandwidth if doing fallback. */
			uint32_t req_bw;
			uint32_t link_bw;

			decide_fallback_link_setting(link, &max_link_settings,
					&cur_link_settings, status);
			/* Flag if reduced link bandwidth no longer meets stream requirements or fallen back to
			 * minimum link bandwidth.
			 */
			req_bw = dc_bandwidth_in_kbps_from_timing(&stream->timing);
			link_bw = dc_link_bandwidth_kbps(link, &cur_link_settings);
			is_link_bw_low = (req_bw > link_bw);
			is_link_bw_min = ((cur_link_settings.link_rate <= LINK_RATE_LOW) &&
				(cur_link_settings.lane_count <= LANE_COUNT_ONE));

			if (is_link_bw_low)
				DC_LOG_WARNING(
					"%s: Link(%d) bandwidth too low after fallback req_bw(%d) > link_bw(%d)\n",
					__func__, link->link_index, req_bw, link_bw);
		}

		msleep(delay_between_attempts);
	}

	return false;
}

