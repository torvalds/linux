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
 * This file implements 8b/10b link training specially modified to support an
 * embedded retimer chip. This retimer chip is referred as fixed vs pe retimer.
 * Unlike native dp connection this chip requires a modified link training
 * protocol based on 8b/10b link training. Since this is a non standard sequence
 * and we must support this hardware, we decided to isolate it in its own
 * training sequence inside its own file.
 */
#include "link_dp_training_fixed_vs_pe_retimer.h"
#include "link_dp_training_8b_10b.h"
#include "link_dpcd.h"
#include "link_dp_phy.h"
#include "link_dp_capability.h"
#include "link_ddc.h"

#define DC_LOGGER \
	link->ctx->logger

void dp_fixed_vs_pe_read_lane_adjust(
	struct dc_link *link,
	union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX])
{
	const uint8_t vendor_lttpr_write_data_vs[3] = {0x0, 0x53, 0x63};
	const uint8_t vendor_lttpr_write_data_pe[3] = {0x0, 0x54, 0x63};
	uint8_t dprx_vs = 0;
	uint8_t dprx_pe = 0;
	uint8_t lane;

	/* W/A to read lane settings requested by DPRX */
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));

	link_query_fixed_vs_pe_retimer(link->ddc, &dprx_vs, 1);

	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

	link_query_fixed_vs_pe_retimer(link->ddc, &dprx_pe, 1);

	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		dpcd_lane_adjust[lane].bits.VOLTAGE_SWING_SET  = (dprx_vs >> (2 * lane)) & 0x3;
		dpcd_lane_adjust[lane].bits.PRE_EMPHASIS_SET = (dprx_pe >> (2 * lane)) & 0x3;
	}
}


void dp_fixed_vs_pe_set_retimer_lane_settings(
	struct dc_link *link,
	const union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX],
	uint8_t lane_count)
{
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
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_reset[0], sizeof(vendor_lttpr_write_data_reset));

	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));

	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));
}

static enum link_training_result perform_fixed_vs_pe_nontransparent_training_sequence(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	uint8_t lane = 0;
	uint8_t toggle_rate = 0x6;
	uint8_t target_rate = 0x6;
	bool apply_toggle_rate_wa = false;
	uint8_t repeater_cnt;
	uint8_t repeater_id;

	/* Fixed VS/PE specific: Force CR AUX RD Interval to at least 16ms */
	if (lt_settings->cr_pattern_time < 16000)
		lt_settings->cr_pattern_time = 16000;

	/* Fixed VS/PE specific: Toggle link rate */
	apply_toggle_rate_wa = (link->vendor_specific_lttpr_link_rate_wa == target_rate);
	target_rate = get_dpcd_link_rate(&lt_settings->link_settings);
	toggle_rate = (target_rate == 0x6) ? 0xA : 0x6;

	if (apply_toggle_rate_wa)
		lt_settings->link_settings.link_rate = toggle_rate;

	if (link->ctx->dc->work_arounds.lt_early_cr_pattern)
		start_clock_recovery_pattern_early(link, link_res, lt_settings, DPRX);

	/* 1. set link rate, lane count and spread. */
	dpcd_set_link_settings(link, lt_settings);

	/* Fixed VS/PE specific: Toggle link rate back*/
	if (apply_toggle_rate_wa) {
		core_link_write_dpcd(
				link,
				DP_LINK_BW_SET,
				&target_rate,
				1);
	}

	link->vendor_specific_lttpr_link_rate_wa = target_rate;

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


enum link_training_result dp_perform_fixed_vs_pe_training_sequence_legacy(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	const uint8_t vendor_lttpr_write_data_reset[4] = {0x1, 0x50, 0x63, 0xFF};
	const uint8_t offset = dp_parse_lttpr_repeater_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	const uint8_t vendor_lttpr_write_data_intercept_en[4] = {0x1, 0x55, 0x63, 0x0};
	const uint8_t vendor_lttpr_write_data_intercept_dis[4] = {0x1, 0x55, 0x63, 0x68};
	uint32_t pre_disable_intercept_delay_ms = 0;
	uint8_t vendor_lttpr_write_data_vs[4] = {0x1, 0x51, 0x63, 0x0};
	uint8_t vendor_lttpr_write_data_pe[4] = {0x1, 0x52, 0x63, 0x0};
	const uint8_t vendor_lttpr_write_data_4lane_1[4] = {0x1, 0x6E, 0xF2, 0x19};
	const uint8_t vendor_lttpr_write_data_4lane_2[4] = {0x1, 0x6B, 0xF2, 0x01};
	const uint8_t vendor_lttpr_write_data_4lane_3[4] = {0x1, 0x6D, 0xF2, 0x18};
	const uint8_t vendor_lttpr_write_data_4lane_4[4] = {0x1, 0x6C, 0xF2, 0x03};
	const uint8_t vendor_lttpr_write_data_4lane_5[4] = {0x1, 0x03, 0xF3, 0x06};
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	uint8_t lane = 0;
	union down_spread_ctrl downspread = {0};
	union lane_count_set lane_count_set = {0};
	uint8_t toggle_rate;
	uint8_t rate;

	/* Only 8b/10b is supported */
	ASSERT(link_dp_get_encoding_format(&lt_settings->link_settings) ==
			DP_8b_10b_ENCODING);

	if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {
		status = perform_fixed_vs_pe_nontransparent_training_sequence(link, link_res, lt_settings);
		return status;
	}

	if (offset != 0xFF) {
		if (offset == 2) {
			pre_disable_intercept_delay_ms = link->dc->debug.fixed_vs_aux_delay_config_wa;

		/* Certain display and cable configuration require extra delay */
		} else if (offset > 2) {
			pre_disable_intercept_delay_ms = link->dc->debug.fixed_vs_aux_delay_config_wa * 2;
		}
	}

	/* Vendor specific: Reset lane settings */
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_reset[0], sizeof(vendor_lttpr_write_data_reset));
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

	/* Vendor specific: Enable intercept */
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_intercept_en[0], sizeof(vendor_lttpr_write_data_intercept_en));


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

	if (lt_settings->link_settings.lane_count == LANE_COUNT_FOUR) {
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_1[0], sizeof(vendor_lttpr_write_data_4lane_1));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_2[0], sizeof(vendor_lttpr_write_data_4lane_2));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_3[0], sizeof(vendor_lttpr_write_data_4lane_3));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_4[0], sizeof(vendor_lttpr_write_data_4lane_4));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_5[0], sizeof(vendor_lttpr_write_data_4lane_5));
	}

	/* 2. Perform link training */

	/* Perform Clock Recovery Sequence */
	if (status == LINK_TRAINING_SUCCESS) {
		const uint8_t max_vendor_dpcd_retries = 10;
		uint32_t retries_cr;
		uint32_t retry_count;
		uint32_t wait_time_microsec;
		enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
		union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
		union lane_align_status_updated dpcd_lane_status_updated;
		union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};
		uint8_t i = 0;

		retries_cr = 0;
		retry_count = 0;

		memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
		memset(&dpcd_lane_status_updated, '\0',
		sizeof(dpcd_lane_status_updated));

		while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
			(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {


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
				for (i = 0; i < max_vendor_dpcd_retries; i++) {
					if (pre_disable_intercept_delay_ms != 0)
						msleep(pre_disable_intercept_delay_ms);
					if (link_configure_fixed_vs_pe_retimer(link->ddc,
							&vendor_lttpr_write_data_intercept_dis[0],
							sizeof(vendor_lttpr_write_data_intercept_dis)))
						break;

					link_configure_fixed_vs_pe_retimer(link->ddc,
							&vendor_lttpr_write_data_intercept_en[0],
							sizeof(vendor_lttpr_write_data_intercept_en));
				}
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
				link_configure_fixed_vs_pe_retimer(link->ddc,
						&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
				link_configure_fixed_vs_pe_retimer(link->ddc,
						&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

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
			link_configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
			link_configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

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

enum link_training_result dp_perform_fixed_vs_pe_training_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	const uint8_t vendor_lttpr_write_data_reset[4] = {0x1, 0x50, 0x63, 0xFF};
	const uint8_t offset = dp_parse_lttpr_repeater_count(
			link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	const uint8_t vendor_lttpr_write_data_intercept_en[4] = {0x1, 0x55, 0x63, 0x0};
	const uint8_t vendor_lttpr_write_data_intercept_dis[4] = {0x1, 0x55, 0x63, 0x6E};
	const uint8_t vendor_lttpr_write_data_adicora_eq1[4] = {0x1, 0x55, 0x63, 0x2E};
	const uint8_t vendor_lttpr_write_data_adicora_eq2[4] = {0x1, 0x55, 0x63, 0x01};
	const uint8_t vendor_lttpr_write_data_adicora_eq3[4] = {0x1, 0x55, 0x63, 0x68};
	uint32_t pre_disable_intercept_delay_ms = 0;
	uint8_t vendor_lttpr_write_data_vs[4] = {0x1, 0x51, 0x63, 0x0};
	uint8_t vendor_lttpr_write_data_pe[4] = {0x1, 0x52, 0x63, 0x0};
	const uint8_t vendor_lttpr_write_data_4lane_1[4] = {0x1, 0x6E, 0xF2, 0x19};
	const uint8_t vendor_lttpr_write_data_4lane_2[4] = {0x1, 0x6B, 0xF2, 0x01};
	const uint8_t vendor_lttpr_write_data_4lane_3[4] = {0x1, 0x6D, 0xF2, 0x18};
	const uint8_t vendor_lttpr_write_data_4lane_4[4] = {0x1, 0x6C, 0xF2, 0x03};
	const uint8_t vendor_lttpr_write_data_4lane_5[4] = {0x1, 0x03, 0xF3, 0x06};
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	uint8_t lane = 0;
	union down_spread_ctrl downspread = {0};
	union lane_count_set lane_count_set = {0};
	uint8_t toggle_rate;
	uint8_t rate;

	/* Only 8b/10b is supported */
	ASSERT(link_dp_get_encoding_format(&lt_settings->link_settings) ==
			DP_8b_10b_ENCODING);

	if (lt_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {
		status = perform_fixed_vs_pe_nontransparent_training_sequence(link, link_res, lt_settings);
		return status;
	}

	if (offset != 0xFF) {
		if (offset == 2) {
			pre_disable_intercept_delay_ms = link->dc->debug.fixed_vs_aux_delay_config_wa;

		/* Certain display and cable configuration require extra delay */
		} else if (offset > 2) {
			pre_disable_intercept_delay_ms = link->dc->debug.fixed_vs_aux_delay_config_wa * 2;
		}
	}

	/* Vendor specific: Reset lane settings */
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_reset[0], sizeof(vendor_lttpr_write_data_reset));
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

	/* Vendor specific: Enable intercept */
	link_configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_intercept_en[0], sizeof(vendor_lttpr_write_data_intercept_en));

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

	if (lt_settings->link_settings.lane_count == LANE_COUNT_FOUR) {
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_1[0], sizeof(vendor_lttpr_write_data_4lane_1));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_2[0], sizeof(vendor_lttpr_write_data_4lane_2));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_3[0], sizeof(vendor_lttpr_write_data_4lane_3));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_4[0], sizeof(vendor_lttpr_write_data_4lane_4));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_4lane_5[0], sizeof(vendor_lttpr_write_data_4lane_5));
	}

	/* 2. Perform link training */

	/* Perform Clock Recovery Sequence */
	if (status == LINK_TRAINING_SUCCESS) {
		const uint8_t max_vendor_dpcd_retries = 10;
		uint32_t retries_cr;
		uint32_t retry_count;
		uint32_t wait_time_microsec;
		enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
		union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
		union lane_align_status_updated dpcd_lane_status_updated;
		union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {0};
		uint8_t i = 0;

		retries_cr = 0;
		retry_count = 0;

		memset(&dpcd_lane_status, '\0', sizeof(dpcd_lane_status));
		memset(&dpcd_lane_status_updated, '\0',
		sizeof(dpcd_lane_status_updated));

		while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
			(retry_count < LINK_TRAINING_MAX_CR_RETRY)) {


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
				for (i = 0; i < max_vendor_dpcd_retries; i++) {
					if (pre_disable_intercept_delay_ms != 0)
						msleep(pre_disable_intercept_delay_ms);
					if (link_configure_fixed_vs_pe_retimer(link->ddc,
							&vendor_lttpr_write_data_intercept_dis[0],
							sizeof(vendor_lttpr_write_data_intercept_dis)))
						break;

					link_configure_fixed_vs_pe_retimer(link->ddc,
							&vendor_lttpr_write_data_intercept_en[0],
							sizeof(vendor_lttpr_write_data_intercept_en));
				}
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
				link_configure_fixed_vs_pe_retimer(link->ddc,
						&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
				link_configure_fixed_vs_pe_retimer(link->ddc,
						&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

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

		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_adicora_eq1[0],
				sizeof(vendor_lttpr_write_data_adicora_eq1));
		link_configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_adicora_eq2[0],
				sizeof(vendor_lttpr_write_data_adicora_eq2));


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
			link_configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_write_data_vs[0], sizeof(vendor_lttpr_write_data_vs));
			link_configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_write_data_pe[0], sizeof(vendor_lttpr_write_data_pe));

			/* 2. update DPCD*/
			if (!retries_ch_eq) {
				/* EPR #361076 - write as a 5-byte burst,
				 * but only for the 1-st iteration
				 */

				dpcd_set_lt_pattern_and_lane_settings(
					link,
					lt_settings,
					tr_pattern, 0);

				link_configure_fixed_vs_pe_retimer(link->ddc,
						&vendor_lttpr_write_data_adicora_eq3[0],
						sizeof(vendor_lttpr_write_data_adicora_eq3));

			} else
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
