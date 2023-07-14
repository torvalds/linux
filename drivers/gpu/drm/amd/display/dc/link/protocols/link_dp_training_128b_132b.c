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
 * This file implements dp 128b/132b link training software policies and
 * sequences.
 */
#include "link_dp_training_128b_132b.h"
#include "link_dp_training_8b_10b.h"
#include "link_dpcd.h"
#include "link_dp_phy.h"
#include "link_dp_capability.h"

#define DC_LOGGER \
	link->ctx->logger

static enum dc_status dpcd_128b_132b_set_lane_settings(
		struct dc_link *link,
		const struct link_training_settings *link_training_setting)
{
	enum dc_status status = core_link_write_dpcd(link,
			DP_TRAINING_LANE0_SET,
			(uint8_t *)(link_training_setting->dpcd_lane_settings),
			sizeof(link_training_setting->dpcd_lane_settings));

	DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X TX_FFE_PRESET_VALUE = %x\n",
			__func__,
			DP_TRAINING_LANE0_SET,
			link_training_setting->dpcd_lane_settings[0].tx_ffe.PRESET_VALUE);
	return status;
}

static void dpcd_128b_132b_get_aux_rd_interval(struct dc_link *link,
		uint32_t *interval_in_us)
{
	union dp_128b_132b_training_aux_rd_interval dpcd_interval;
	uint32_t interval_unit = 0;

	dpcd_interval.raw = 0;
	core_link_read_dpcd(link, DP_128B132B_TRAINING_AUX_RD_INTERVAL,
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
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};
	enum dc_status status = DC_OK;
	enum link_training_result result = LINK_TRAINING_SUCCESS;

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
	while (result == LINK_TRAINING_SUCCESS) {
		dp_wait_for_training_aux_rd_interval(link, aux_rd_interval);
		wait_time += aux_rd_interval;
		status = dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
				&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		dp_decide_lane_settings(lt_settings, dpcd_lane_adjust,
				lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
		dpcd_128b_132b_get_aux_rd_interval(link, &aux_rd_interval);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
		} else if (dp_is_ch_eq_done(lt_settings->link_settings.lane_count,
				dpcd_lane_status)) {
			/* pass */
			break;
		} else if (loop_count >= lt_settings->eq_loop_count_limit) {
			result = DP_128b_132b_MAX_LOOP_COUNT_REACHED;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			result = DP_128b_132b_LT_FAILED;
		} else {
			dp_set_hw_lane_settings(link, link_res, lt_settings, DPRX);
			dpcd_128b_132b_set_lane_settings(link, lt_settings);
		}
		loop_count++;
	}

	/* poll for EQ interlane align done */
	while (result == LINK_TRAINING_SUCCESS) {
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
		} else if (dpcd_lane_status_updated.bits.EQ_INTERLANE_ALIGN_DONE_128b_132b) {
			/* pass */
			break;
		} else if (wait_time >= lt_settings->eq_wait_time_limit) {
			result = DP_128b_132b_CHANNEL_EQ_DONE_TIMEOUT;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			result = DP_128b_132b_LT_FAILED;
		} else {
			dp_wait_for_training_aux_rd_interval(link,
					lt_settings->eq_pattern_time);
			wait_time += lt_settings->eq_pattern_time;
			status = dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
					&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		}
	}

	return result;
}

static enum link_training_result dp_perform_128b_132b_cds_done_sequence(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	/* Assumption: assume hardware has transmitted eq pattern */
	enum dc_status status = DC_OK;
	enum link_training_result result = LINK_TRAINING_SUCCESS;
	union lane_align_status_updated dpcd_lane_status_updated = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};
	uint32_t wait_time = 0;

	/* initiate CDS done sequence */
	dpcd_set_training_pattern(link, lt_settings->pattern_for_cds);

	/* poll for CDS interlane align done and symbol lock */
	while (result == LINK_TRAINING_SUCCESS) {
		dp_wait_for_training_aux_rd_interval(link,
				lt_settings->cds_pattern_time);
		wait_time += lt_settings->cds_pattern_time;
		status = dp_get_lane_status_and_lane_adjust(link, lt_settings, dpcd_lane_status,
						&dpcd_lane_status_updated, dpcd_lane_adjust, DPRX);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
		} else if (dp_is_symbol_locked(lt_settings->link_settings.lane_count, dpcd_lane_status) &&
				dpcd_lane_status_updated.bits.CDS_INTERLANE_ALIGN_DONE_128b_132b) {
			/* pass */
			break;
		} else if (dpcd_lane_status_updated.bits.LT_FAILED_128b_132b) {
			result = DP_128b_132b_LT_FAILED;
		} else if (wait_time >= lt_settings->cds_wait_time_limit) {
			result = DP_128b_132b_CDS_DONE_TIMEOUT;
		}
	}

	return result;
}

enum link_training_result dp_perform_128b_132b_link_training(
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

	if (result == LINK_TRAINING_SUCCESS) {
		result = dp_perform_128b_132b_channel_eq_done_sequence(link, link_res, lt_settings);
		if (result == LINK_TRAINING_SUCCESS)
			DC_LOG_HW_LINK_TRAINING("%s: Channel EQ done.\n", __func__);
	}

	if (result == LINK_TRAINING_SUCCESS) {
		result = dp_perform_128b_132b_cds_done_sequence(link, link_res, lt_settings);
		if (result == LINK_TRAINING_SUCCESS)
			DC_LOG_HW_LINK_TRAINING("%s: CDS done.\n", __func__);
	}

	return result;
}

void decide_128b_132b_training_settings(struct dc_link *link,
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
	lt_settings->cds_wait_time_limit = (dp_parse_lttpr_repeater_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt) + 1) * 20000;
	lt_settings->disallow_per_lane_settings = true;
	lt_settings->lttpr_mode = dp_decide_128b_132b_lttpr_mode(link);
	dp_hw_to_dpcd_lane_settings(lt_settings,
			lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
}

enum lttpr_mode dp_decide_128b_132b_lttpr_mode(struct dc_link *link)
{
	enum lttpr_mode mode = LTTPR_MODE_NON_LTTPR;

	if (dp_is_lttpr_present(link))
		mode = LTTPR_MODE_NON_TRANSPARENT;

	DC_LOG_DC("128b_132b chose LTTPR_MODE %d.\n", mode);
	return mode;
}

