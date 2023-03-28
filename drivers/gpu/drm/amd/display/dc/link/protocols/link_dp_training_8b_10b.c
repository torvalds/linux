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
 * This file implements dp 8b/10b link training software policies and
 * sequences.
 */
#include "link_dp_training_8b_10b.h"
#include "link_dpcd.h"
#include "link_dp_phy.h"
#include "link_dp_capability.h"

#define DC_LOGGER \
	link->ctx->logger

static int32_t get_cr_training_aux_rd_interval(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	union training_aux_rd_interval training_rd_interval;
	uint32_t wait_in_micro_secs = 100;

	memset(&training_rd_interval, 0, sizeof(training_rd_interval));
	if (link_dp_get_encoding_format(link_settings) == DP_8b_10b_ENCODING &&
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
	if (link_dp_get_encoding_format(link_settings) == DP_128b_132b_ENCODING) {
		core_link_read_dpcd(
				link,
				DP_128B132B_TRAINING_AUX_RD_INTERVAL,
				(uint8_t *)&training_rd_interval,
				sizeof(training_rd_interval));
	} else if (link_dp_get_encoding_format(link_settings) == DP_8b_10b_ENCODING &&
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

void decide_8b_10b_training_settings(
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
	lt_settings->cr_pattern_time = get_cr_training_aux_rd_interval(link, link_setting);
	lt_settings->eq_pattern_time = get_eq_training_aux_rd_interval(link, link_setting);
	lt_settings->pattern_for_cr = decide_cr_training_pattern(link_setting);
	lt_settings->pattern_for_eq = decide_eq_training_pattern(link, link_setting);
	lt_settings->enhanced_framing = 1;
	lt_settings->should_set_fec_ready = true;
	lt_settings->disallow_per_lane_settings = true;
	lt_settings->always_match_dpcd_with_hw_lane_settings = true;
	lt_settings->lttpr_mode = dp_decide_8b_10b_lttpr_mode(link);
	dp_hw_to_dpcd_lane_settings(lt_settings, lt_settings->hw_lane_settings, lt_settings->dpcd_lane_settings);
}

enum lttpr_mode dp_decide_8b_10b_lttpr_mode(struct dc_link *link)
{
	bool is_lttpr_present = dp_is_lttpr_present(link);
	bool vbios_lttpr_force_non_transparent = link->dc->caps.vbios_lttpr_enable;
	bool vbios_lttpr_aware = link->dc->caps.vbios_lttpr_aware;

	if (!is_lttpr_present)
		return LTTPR_MODE_NON_LTTPR;

	if (vbios_lttpr_aware) {
		if (vbios_lttpr_force_non_transparent) {
			DC_LOG_DC("chose LTTPR_MODE_NON_TRANSPARENT due to VBIOS DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE set to 1.\n");
			return LTTPR_MODE_NON_TRANSPARENT;
		} else {
			DC_LOG_DC("chose LTTPR_MODE_NON_TRANSPARENT by default due to VBIOS not set DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE set to 1.\n");
			return LTTPR_MODE_TRANSPARENT;
		}
	}

	if (link->dc->config.allow_lttpr_non_transparent_mode.bits.DP1_4A &&
			link->dc->caps.extended_aux_timeout_support) {
		DC_LOG_DC("chose LTTPR_MODE_NON_TRANSPARENT by default and dc->config.allow_lttpr_non_transparent_mode.bits.DP1_4A set to 1.\n");
		return LTTPR_MODE_NON_TRANSPARENT;
	}

	DC_LOG_DC("chose LTTPR_MODE_NON_LTTPR.\n");
	return LTTPR_MODE_NON_LTTPR;
}

enum link_training_result perform_8b_10b_clock_recovery_sequence(
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
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};

	retries_cr = 0;
	retry_count = 0;

	memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
	memset(&dpcd_lane_status_updated, '\0',
	sizeof(dpcd_lane_status_updated));

	if (!link->ctx->dc->work_arounds.lt_early_cr_pattern)
		dp_set_hw_training_pattern(link, link_res, lt_settings->pattern_for_cr, offset);

	/* najeeb - The synaptics MST hub can put the LT in
	* infinite loop by switching the VS
	*/
	/* between level 0 and level 1 continuously, here
	* we try for CR lock for LinkTrainingMaxCRRetry count*/
	while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
		(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {


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

		/* 5. check CR done*/
		if (dp_is_cr_done(lane_count, dpcd_lane_status))
			return LINK_TRAINING_SUCCESS;

		/* 6. max VS reached*/
		if ((link_dp_get_encoding_format(&lt_settings->link_settings) ==
				DP_8b_10b_ENCODING) &&
				dp_is_max_vs_reached(lt_settings))
			break;

		/* 7. same lane settings*/
		/* Note: settings are the same for all lanes,
		 * so comparing first lane is sufficient*/
		if ((link_dp_get_encoding_format(&lt_settings->link_settings) == DP_8b_10b_ENCODING) &&
				lt_settings->dpcd_lane_settings[0].bits.VOLTAGE_SWING_SET ==
						dpcd_lane_adjust[0].bits.VOLTAGE_SWING_LANE)
			retries_cr++;
		else if ((link_dp_get_encoding_format(&lt_settings->link_settings) == DP_128b_132b_ENCODING) &&
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

enum link_training_result perform_8b_10b_channel_equalization_sequence(
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

	if (is_repeater(lt_settings, offset) && link_dp_get_encoding_format(&lt_settings->link_settings) == DP_8b_10b_ENCODING)
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

		if (is_repeater(lt_settings, offset))
			wait_time_microsec =
					dp_translate_training_aux_read_interval(
						link->dpcd_caps.lttpr_caps.aux_rd_interval[offset - 1]);

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
			return dpcd_lane_status[0].bits.CR_DONE_0 ?
					LINK_TRAINING_EQ_FAIL_CR_PARTIAL :
					LINK_TRAINING_EQ_FAIL_CR;

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

enum link_training_result dp_perform_8b_10b_link_training(
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

	if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {

		/* 2. perform link training (set link training done
		 *  to false is done as well)
		 */
		repeater_cnt = dp_parse_lttpr_repeater_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

		for (repeater_id = repeater_cnt; (repeater_id > 0 && status == LINK_TRAINING_SUCCESS);
				repeater_id--) {
			status = perform_8b_10b_clock_recovery_sequence(link, link_res, lt_settings, repeater_id);

			if (status != LINK_TRAINING_SUCCESS) {
				repeater_training_done(link, repeater_id);
				break;
			}

			status = perform_8b_10b_channel_equalization_sequence(link,
					link_res,
					lt_settings,
					repeater_id);

			repeater_training_done(link, repeater_id);

			if (status != LINK_TRAINING_SUCCESS)
				break;

			for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
				lt_settings->dpcd_lane_settings[lane].raw = 0;
				lt_settings->hw_lane_settings[lane].VOLTAGE_SWING = 0;
				lt_settings->hw_lane_settings[lane].PRE_EMPHASIS = 0;
			}
		}
	}

	if (status == LINK_TRAINING_SUCCESS) {
		status = perform_8b_10b_clock_recovery_sequence(link, link_res, lt_settings, DPRX);
		if (status == LINK_TRAINING_SUCCESS) {
			status = perform_8b_10b_channel_equalization_sequence(link,
					link_res,
					lt_settings,
					DPRX);
		}
	}

	return status;
}
