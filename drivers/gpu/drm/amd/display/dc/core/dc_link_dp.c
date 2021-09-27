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
#include "dc_link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"
#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
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

static bool decide_fallback_link_setting(
		struct dc_link_settings initial_link_settings,
		struct dc_link_settings *current_link_setting,
		enum link_training_result training_result);
static struct dc_link_settings get_common_supported_link_settings(
		struct dc_link_settings link_setting_a,
		struct dc_link_settings link_setting_b);

static uint32_t get_cr_training_aux_rd_interval(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	union training_aux_rd_interval training_rd_interval;
	uint32_t wait_in_micro_secs = 100;

	memset(&training_rd_interval, 0, sizeof(training_rd_interval));
	core_link_read_dpcd(
			link,
			DP_TRAINING_AUX_RD_INTERVAL,
			(uint8_t *)&training_rd_interval,
			sizeof(training_rd_interval));
	if (training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL)
		wait_in_micro_secs = training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL * 4000;
	return wait_in_micro_secs;
}

static uint32_t get_eq_training_aux_rd_interval(
	struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	union training_aux_rd_interval training_rd_interval;
	uint32_t wait_in_micro_secs = 400;

	memset(&training_rd_interval, 0, sizeof(training_rd_interval));
	/* overwrite the delay if rev > 1.1*/
	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
		/* DP 1.2 or later - retrieve delay through
		 * "DPCD_ADDR_TRAINING_AUX_RD_INTERVAL" register */
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

void dp_wait_for_training_aux_rd_interval(
	struct dc_link *link,
	uint32_t wait_in_micro_secs)
{
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
	union dpcd_training_pattern dpcd_pattern = { {0} };

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
	return DP_TRAINING_PATTERN_SEQUENCE_1;
}

static enum dc_dp_training_pattern decide_eq_training_pattern(struct dc_link *link,
		const struct dc_link_settings *link_settings)
{
	struct link_encoder *link_enc;
	enum dc_dp_training_pattern highest_tp = DP_TRAINING_PATTERN_SEQUENCE_2;
	struct encoder_feature_support *features;
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;

	/* Access link encoder capability based on whether it is statically
	 * or dynamically assigned to a link.
	 */
	if (link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign)
		link_enc = link_enc_cfg_get_link_enc_used_by_link(link->dc->current_state, link);
	else
		link_enc = link->link_enc;
	ASSERT(link_enc);
	features = &link_enc->features;

	if (features->flags.bits.IS_TPS3_CAPABLE)
		highest_tp = DP_TRAINING_PATTERN_SEQUENCE_3;

	if (features->flags.bits.IS_TPS4_CAPABLE)
		highest_tp = DP_TRAINING_PATTERN_SEQUENCE_4;

	if (dpcd_caps->max_down_spread.bits.TPS4_SUPPORTED &&
		highest_tp >= DP_TRAINING_PATTERN_SEQUENCE_4)
		return DP_TRAINING_PATTERN_SEQUENCE_4;

	if (dpcd_caps->max_ln_count.bits.TPS3_SUPPORTED &&
		highest_tp >= DP_TRAINING_PATTERN_SEQUENCE_3)
		return DP_TRAINING_PATTERN_SEQUENCE_3;

	return DP_TRAINING_PATTERN_SEQUENCE_2;
}

enum dc_status dpcd_set_link_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings)
{
	uint8_t rate;
	enum dc_status status;

	union down_spread_ctrl downspread = { {0} };
	union lane_count_set lane_count_set = { {0} };

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

	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_14 &&
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
		rate = (uint8_t) (lt_settings->link_settings.link_rate);
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
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX] = { { {0} } };

	uint32_t dpcd_base_lt_offset;

	uint8_t dpcd_lt_buffer[5] = {0};
	union dpcd_training_pattern dpcd_pattern = { {0} };
	uint32_t lane;
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
	/*****************************************************************
	* DpcdAddress_Lane0Set -> DpcdAddress_Lane3Set
	*****************************************************************/
	for (lane = 0; lane <
		(uint32_t)(lt_settings->link_settings.lane_count); lane++) {

		dpcd_lane[lane].bits.VOLTAGE_SWING_SET =
		(uint8_t)(lt_settings->lane_settings[lane].VOLTAGE_SWING);
		dpcd_lane[lane].bits.PRE_EMPHASIS_SET =
		(uint8_t)(lt_settings->lane_settings[lane].PRE_EMPHASIS);

		dpcd_lane[lane].bits.MAX_SWING_REACHED =
		(lt_settings->lane_settings[lane].VOLTAGE_SWING ==
		VOLTAGE_SWING_MAX_LEVEL ? 1 : 0);
		dpcd_lane[lane].bits.MAX_PRE_EMPHASIS_REACHED =
		(lt_settings->lane_settings[lane].PRE_EMPHASIS ==
		PRE_EMPHASIS_MAX_LEVEL ? 1 : 0);
	}

	/* concatenate everything into one buffer*/

	size_in_bytes = lt_settings->link_settings.lane_count * sizeof(dpcd_lane[0]);

	 // 0x00103 - 0x00102
	memmove(
		&dpcd_lt_buffer[DP_TRAINING_LANE0_SET - DP_TRAINING_PATTERN_SET],
		dpcd_lane,
		size_in_bytes);

	if (is_repeater(link, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s:\n LTTPR Repeater ID: %d\n"
				" 0x%X VS set = %x PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			offset,
			dpcd_base_lt_offset,
			dpcd_lane[0].bits.VOLTAGE_SWING_SET,
			dpcd_lane[0].bits.PRE_EMPHASIS_SET,
			dpcd_lane[0].bits.MAX_SWING_REACHED,
			dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s:\n 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			dpcd_base_lt_offset,
			dpcd_lane[0].bits.VOLTAGE_SWING_SET,
			dpcd_lane[0].bits.PRE_EMPHASIS_SET,
			dpcd_lane[0].bits.MAX_SWING_REACHED,
			dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);
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
			(uint8_t *)(dpcd_lane),
			size_in_bytes);

		} else
		/* write it all in (1 + number-of-lanes)-byte burst*/
			core_link_write_dpcd(
				link,
				dpcd_base_lt_offset,
				dpcd_lt_buffer,
				size_in_bytes + sizeof(dpcd_pattern.raw));

	link->cur_lane_setting = lt_settings->lane_settings[0];
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

void dp_update_drive_settings(
		struct link_training_settings *dest,
		struct link_training_settings src)
{
	uint32_t lane;
	for (lane = 0; lane < src.link_settings.lane_count; lane++) {
		if (dest->voltage_swing == NULL)
			dest->lane_settings[lane].VOLTAGE_SWING = src.lane_settings[lane].VOLTAGE_SWING;
		else
			dest->lane_settings[lane].VOLTAGE_SWING = *dest->voltage_swing;

		if (dest->pre_emphasis == NULL)
			dest->lane_settings[lane].PRE_EMPHASIS = src.lane_settings[lane].PRE_EMPHASIS;
		else
			dest->lane_settings[lane].PRE_EMPHASIS = *dest->pre_emphasis;

		if (dest->post_cursor2 == NULL)
			dest->lane_settings[lane].POST_CURSOR2 = src.lane_settings[lane].POST_CURSOR2;
		else
			dest->lane_settings[lane].POST_CURSOR2 = *dest->post_cursor2;
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

static void find_max_drive_settings(
	const struct link_training_settings *link_training_setting,
	struct link_training_settings *max_lt_setting)
{
	uint32_t lane;
	struct dc_lane_settings max_requested;

	max_requested.VOLTAGE_SWING =
		link_training_setting->
		lane_settings[0].VOLTAGE_SWING;
	max_requested.PRE_EMPHASIS =
		link_training_setting->
		lane_settings[0].PRE_EMPHASIS;
	/*max_requested.postCursor2 =
	 * link_training_setting->laneSettings[0].postCursor2;*/

	/* Determine what the maximum of the requested settings are*/
	for (lane = 1; lane < link_training_setting->link_settings.lane_count;
			lane++) {
		if (link_training_setting->lane_settings[lane].VOLTAGE_SWING >
			max_requested.VOLTAGE_SWING)

			max_requested.VOLTAGE_SWING =
			link_training_setting->
			lane_settings[lane].VOLTAGE_SWING;

		if (link_training_setting->lane_settings[lane].PRE_EMPHASIS >
				max_requested.PRE_EMPHASIS)
			max_requested.PRE_EMPHASIS =
			link_training_setting->
			lane_settings[lane].PRE_EMPHASIS;

		/*
		if (link_training_setting->laneSettings[lane].postCursor2 >
		 max_requested.postCursor2)
		{
		max_requested.postCursor2 =
		link_training_setting->laneSettings[lane].postCursor2;
		}
		*/
	}

	/* make sure the requested settings are
	 * not higher than maximum settings*/
	if (max_requested.VOLTAGE_SWING > VOLTAGE_SWING_MAX_LEVEL)
		max_requested.VOLTAGE_SWING = VOLTAGE_SWING_MAX_LEVEL;

	if (max_requested.PRE_EMPHASIS > PRE_EMPHASIS_MAX_LEVEL)
		max_requested.PRE_EMPHASIS = PRE_EMPHASIS_MAX_LEVEL;
	/*
	if (max_requested.postCursor2 > PostCursor2_MaxLevel)
	max_requested.postCursor2 = PostCursor2_MaxLevel;
	*/

	/* make sure the pre-emphasis matches the voltage swing*/
	if (max_requested.PRE_EMPHASIS >
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING))
		max_requested.PRE_EMPHASIS =
		get_max_pre_emphasis_for_voltage_swing(
			max_requested.VOLTAGE_SWING);

	/*
	 * Post Cursor2 levels are completely independent from
	 * pre-emphasis (Post Cursor1) levels. But Post Cursor2 levels
	 * can only be applied to each allowable combination of voltage
	 * swing and pre-emphasis levels */
	 /* if ( max_requested.postCursor2 >
	  *  getMaxPostCursor2ForVoltageSwing(max_requested.voltageSwing))
	  *  max_requested.postCursor2 =
	  *  getMaxPostCursor2ForVoltageSwing(max_requested.voltageSwing);
	  */

	max_lt_setting->link_settings.link_rate =
		link_training_setting->link_settings.link_rate;
	max_lt_setting->link_settings.lane_count =
	link_training_setting->link_settings.lane_count;
	max_lt_setting->link_settings.link_spread =
		link_training_setting->link_settings.link_spread;

	for (lane = 0; lane <
		link_training_setting->link_settings.lane_count;
		lane++) {
		max_lt_setting->lane_settings[lane].VOLTAGE_SWING =
			max_requested.VOLTAGE_SWING;
		max_lt_setting->lane_settings[lane].PRE_EMPHASIS =
			max_requested.PRE_EMPHASIS;
		/*max_lt_setting->laneSettings[lane].postCursor2 =
		 * max_requested.postCursor2;
		 */
	}

}

enum dc_status dp_get_lane_status_and_drive_settings(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	union lane_status *ln_status,
	union lane_align_status_updated *ln_status_updated,
	struct link_training_settings *req_settings,
	uint32_t offset)
{
	unsigned int lane01_status_address = DP_LANE0_1_STATUS;
	uint8_t lane_adjust_offset = 4;
	unsigned int lane01_adjust_address;
	uint8_t dpcd_buf[6] = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = { { {0} } };
	struct link_training_settings request_settings = { {0} };
	uint32_t lane;
	enum dc_status status;

	memset(req_settings, '\0', sizeof(struct link_training_settings));

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
		dpcd_lane_adjust[lane].raw =
			get_nibble_at_index(&dpcd_buf[lane_adjust_offset], lane);
	}

	ln_status_updated->raw = dpcd_buf[2];

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

	/*copy to req_settings*/
	request_settings.link_settings.lane_count =
		link_training_setting->link_settings.lane_count;
	request_settings.link_settings.link_rate =
		link_training_setting->link_settings.link_rate;
	request_settings.link_settings.link_spread =
		link_training_setting->link_settings.link_spread;

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		request_settings.lane_settings[lane].VOLTAGE_SWING =
			(enum dc_voltage_swing)(dpcd_lane_adjust[lane].bits.
				VOLTAGE_SWING_LANE);
		request_settings.lane_settings[lane].PRE_EMPHASIS =
			(enum dc_pre_emphasis)(dpcd_lane_adjust[lane].bits.
				PRE_EMPHASIS_LANE);
	}

	/*Note: for postcursor2, read adjusted
	 * postcursor2 settings from*/
	/*DpcdAddress_AdjustRequestPostCursor2 =
	 *0x020C (not implemented yet)*/

	/* we find the maximum of the requested settings across all lanes*/
	/* and set this maximum for all lanes*/
	find_max_drive_settings(&request_settings, req_settings);

	/* if post cursor 2 is needed in the future,
	 * read DpcdAddress_AdjustRequestPostCursor2 = 0x020C
	 */

	return status;
}

enum dc_status dpcd_set_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	uint32_t offset)
{
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX] = {{{0}}};
	uint32_t lane;
	unsigned int lane0_set_address;
	enum dc_status status;

	lane0_set_address = DP_TRAINING_LANE0_SET;

	if (is_repeater(link, offset))
		lane0_set_address = DP_TRAINING_LANE0_SET_PHY_REPEATER1 +
		((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->
		link_settings.lane_count);
		lane++) {
		dpcd_lane[lane].bits.VOLTAGE_SWING_SET =
			(uint8_t)(link_training_setting->
			lane_settings[lane].VOLTAGE_SWING);
		dpcd_lane[lane].bits.PRE_EMPHASIS_SET =
			(uint8_t)(link_training_setting->
			lane_settings[lane].PRE_EMPHASIS);
		dpcd_lane[lane].bits.MAX_SWING_REACHED =
			(link_training_setting->
			lane_settings[lane].VOLTAGE_SWING ==
			VOLTAGE_SWING_MAX_LEVEL ? 1 : 0);
		dpcd_lane[lane].bits.MAX_PRE_EMPHASIS_REACHED =
			(link_training_setting->
			lane_settings[lane].PRE_EMPHASIS ==
			PRE_EMPHASIS_MAX_LEVEL ? 1 : 0);
	}

	status = core_link_write_dpcd(link,
		lane0_set_address,
		(uint8_t *)(dpcd_lane),
		link_training_setting->link_settings.lane_count);

	/*
	if (LTSettings.link.rate == LinkRate_High2)
	{
		DpcdTrainingLaneSet2 dpcd_lane2[lane_count_DPMax] = {0};
		for ( uint32_t lane = 0;
		lane < lane_count_DPMax; lane++)
		{
			dpcd_lane2[lane].bits.post_cursor2_set =
			static_cast<unsigned char>(
			LTSettings.laneSettings[lane].postCursor2);
			dpcd_lane2[lane].bits.max_post_cursor2_reached = 0;
		}
		m_pDpcdAccessSrv->WriteDpcdData(
		DpcdAddress_Lane0Set2,
		reinterpret_cast<unsigned char*>(dpcd_lane2),
		LTSettings.link.lanes);
	}
	*/

	if (is_repeater(link, offset)) {
		DC_LOG_HW_LINK_TRAINING("%s\n LTTPR Repeater ID: %d\n"
				" 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			offset,
			lane0_set_address,
			dpcd_lane[0].bits.VOLTAGE_SWING_SET,
			dpcd_lane[0].bits.PRE_EMPHASIS_SET,
			dpcd_lane[0].bits.MAX_SWING_REACHED,
			dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);

	} else {
		DC_LOG_HW_LINK_TRAINING("%s\n 0x%X VS set = %x  PE set = %x max VS Reached = %x  max PE Reached = %x\n",
			__func__,
			lane0_set_address,
			dpcd_lane[0].bits.VOLTAGE_SWING_SET,
			dpcd_lane[0].bits.PRE_EMPHASIS_SET,
			dpcd_lane[0].bits.MAX_SWING_REACHED,
			dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);
	}
	link->cur_lane_setting = link_training_setting->lane_settings[0];

	return status;
}

bool dp_is_max_vs_reached(
	const struct link_training_settings *lt_settings)
{
	uint32_t lane;
	for (lane = 0; lane <
		(uint32_t)(lt_settings->link_settings.lane_count);
		lane++) {
		if (lt_settings->lane_settings[lane].VOLTAGE_SWING
			== VOLTAGE_SWING_MAX_LEVEL)
			return true;
	}
	return false;

}

static bool perform_post_lt_adj_req_sequence(
	struct dc_link *link,
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

			struct link_training_settings req_settings;
			union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
			union lane_align_status_updated
				dpcd_lane_status_updated;

			dp_get_lane_status_and_drive_settings(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
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
				lane_settings[lane].VOLTAGE_SWING !=
				req_settings.lane_settings[lane].
				VOLTAGE_SWING ||
				lt_settings->lane_settings[lane].PRE_EMPHASIS !=
				req_settings.lane_settings[lane].PRE_EMPHASIS) {

					req_drv_setting_changed = true;
					break;
				}
			}

			if (req_drv_setting_changed) {
				dp_update_drive_settings(
					lt_settings, req_settings);

				dc_link_dp_set_drive_settings(link,
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
	struct link_training_settings *lt_settings,
	uint32_t offset)
{
	struct link_training_settings req_settings;
	enum dc_dp_training_pattern tr_pattern;
	uint32_t retries_ch_eq;
	uint32_t wait_time_microsec;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_align_status_updated dpcd_lane_status_updated = { {0} };
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = { { {0} } };

	/* Note: also check that TPS4 is a supported feature*/

	tr_pattern = lt_settings->pattern_for_eq;

	if (is_repeater(link, offset))
		tr_pattern = DP_TRAINING_PATTERN_SEQUENCE_4;

	dp_set_hw_training_pattern(link, tr_pattern, offset);

	for (retries_ch_eq = 0; retries_ch_eq <= LINK_TRAINING_MAX_RETRY_COUNT;
		retries_ch_eq++) {

		dp_set_hw_lane_settings(link, lt_settings, offset);

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

		dp_wait_for_training_aux_rd_interval(
				link,
				wait_time_microsec);

		/* 4. Read lane status and requested
		 * drive settings as set by the sink*/

		dp_get_lane_status_and_drive_settings(
			link,
			lt_settings,
			dpcd_lane_status,
			&dpcd_lane_status_updated,
			&req_settings,
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
		dp_update_drive_settings(lt_settings, req_settings);
	}

	return LINK_TRAINING_EQ_FAIL_EQ;

}

static void start_clock_recovery_pattern_early(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t offset)
{
	DC_LOG_HW_LINK_TRAINING("%s\n GPU sends TPS1. Wait 400us.\n",
			__func__);
	dp_set_hw_training_pattern(link, lt_settings->pattern_for_cr, offset);
	dp_set_hw_lane_settings(link, lt_settings, offset);
	udelay(400);
}

static enum link_training_result perform_clock_recovery_sequence(
	struct dc_link *link,
	struct link_training_settings *lt_settings,
	uint32_t offset)
{
	uint32_t retries_cr;
	uint32_t retry_count;
	uint32_t wait_time_microsec;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated dpcd_lane_status_updated;

	retries_cr = 0;
	retry_count = 0;

	if (!link->ctx->dc->work_arounds.lt_early_cr_pattern)
		dp_set_hw_training_pattern(link, lt_settings->pattern_for_cr, offset);

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

		if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
			wait_time_microsec = TRAINING_AUX_RD_INTERVAL;

		dp_wait_for_training_aux_rd_interval(
				link,
				wait_time_microsec);

		/* 4. Read lane status and requested drive
		* settings as set by the sink
		*/
		dp_get_lane_status_and_drive_settings(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
				offset);

		/* 5. check CR done*/
		if (dp_is_cr_done(lane_count, dpcd_lane_status))
			return LINK_TRAINING_SUCCESS;

		/* 6. max VS reached*/
		if (dp_is_max_vs_reached(lt_settings))
			break;

		/* 7. same lane settings*/
		/* Note: settings are the same for all lanes,
		 * so comparing first lane is sufficient*/
		if ((lt_settings->lane_settings[0].VOLTAGE_SWING ==
			req_settings.lane_settings[0].VOLTAGE_SWING)
			&& (lt_settings->lane_settings[0].PRE_EMPHASIS ==
				req_settings.lane_settings[0].PRE_EMPHASIS))
			retries_cr++;
		else
			retries_cr = 0;

		/* 8. update VS/PE/PC2 in lt_settings*/
		dp_update_drive_settings(lt_settings, req_settings);

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
	struct link_training_settings *lt_settings,
	enum link_training_result status)
{
	union lane_count_set lane_count_set = { {0} };

	/* 4. mainlink output idle pattern*/
	dp_set_hw_test_pattern(link, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

	/*
	 * 5. post training adjust if required
	 * If the upstream DPTX and downstream DPRX both support TPS4,
	 * TPS4 must be used instead of POST_LT_ADJ_REQ.
	 */
	if (link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED != 1 ||
			lt_settings->pattern_for_eq == DP_TRAINING_PATTERN_SEQUENCE_4) {
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
		perform_post_lt_adj_req_sequence(link, lt_settings) == false)
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
}

void dp_decide_training_settings(
		struct dc_link *link,
		const struct dc_link_settings *link_settings,
		struct link_training_settings *lt_settings)
{
	if (dp_get_link_encoding_format(link_settings) == DP_8b_10b_ENCODING)
		decide_8b_10b_training_settings(link, link_settings, lt_settings);
}

static void override_training_settings(
		struct dc_link *link,
		const struct dc_link_training_overrides *overrides,
		struct link_training_settings *lt_settings)
{
	uint32_t lane;

	/* Override link settings */
	if (link->preferred_link_setting.link_rate != LINK_RATE_UNKNOWN)
		lt_settings->link_settings.link_rate = link->preferred_link_setting.link_rate;
	if (link->preferred_link_setting.lane_count != LANE_COUNT_UNKNOWN)
		lt_settings->link_settings.lane_count = link->preferred_link_setting.lane_count;

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

enum dc_status configure_lttpr_mode_transparent(struct dc_link *link)
{
	uint8_t repeater_mode = DP_PHY_REPEATER_MODE_TRANSPARENT;

	DC_LOG_HW_LINK_TRAINING("%s\n Set LTTPR to Transparent Mode\n", __func__);
	return core_link_write_dpcd(link,
			DP_PHY_REPEATER_MODE,
			(uint8_t *)&repeater_mode,
			sizeof(repeater_mode));
}

enum dc_status configure_lttpr_mode_non_transparent(
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
	union dpcd_training_pattern dpcd_pattern = { {0} };

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
	struct link_training_settings *lt_settings)
{
	/* program ASIC PHY settings*/
	dp_set_hw_lane_settings(link, lt_settings, DPRX);

	/* Notify DP sink the PHY settings from source */
	dpcd_set_lane_settings(link, lt_settings, DPRX);
}

bool dc_link_dp_perform_link_training_skip_aux(
	struct dc_link *link,
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
	dp_set_hw_training_pattern(link, lt_settings.pattern_for_cr, DPRX);

	/* call HWSS to set lane settings*/
	dp_set_hw_lane_settings(link, &lt_settings, DPRX);

	/* wait receiver to lock-on*/
	dp_wait_for_training_aux_rd_interval(link, lt_settings.cr_pattern_time);

	/* 2. Perform_channel_equalization_sequence. */

	/* transmit training pattern for channel equalization. */
	dp_set_hw_training_pattern(link, lt_settings.pattern_for_eq, DPRX);

	/* call HWSS to set lane settings*/
	dp_set_hw_lane_settings(link, &lt_settings, DPRX);

	/* wait receiver to lock-on. */
	dp_wait_for_training_aux_rd_interval(link, lt_settings.eq_pattern_time);

	/* 3. Perform_link_training_int. */

	/* Mainlink output idle pattern. */
	dp_set_hw_test_pattern(link, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

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

	/* clear training pattern set */
	dpcd_set_training_pattern(link, DP_TRAINING_PATTERN_VIDEOIDLE);
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

static enum link_training_result dp_perform_8b_10b_link_training(
		struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	enum link_training_result status = LINK_TRAINING_SUCCESS;

	uint8_t repeater_cnt;
	uint8_t repeater_id;
	uint8_t lane = 0;

	if (link->ctx->dc->work_arounds.lt_early_cr_pattern)
		start_clock_recovery_pattern_early(link, lt_settings, DPRX);

	/* 1. set link rate, lane count and spread. */
	dpcd_set_link_settings(link, lt_settings);

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {

		/* 2. perform link training (set link training done
		 *  to false is done as well)
		 */
		repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

		for (repeater_id = repeater_cnt; (repeater_id > 0 && status == LINK_TRAINING_SUCCESS);
				repeater_id--) {
			status = perform_clock_recovery_sequence(link, lt_settings, repeater_id);

			if (status != LINK_TRAINING_SUCCESS)
				break;

			status = perform_channel_equalization_sequence(link,
					lt_settings,
					repeater_id);

			if (status != LINK_TRAINING_SUCCESS)
				break;

			repeater_training_done(link, repeater_id);
		}

		for (lane = 0; lane < (uint8_t)lt_settings->link_settings.lane_count; lane++)
			lt_settings->lane_settings[lane].VOLTAGE_SWING = VOLTAGE_SWING_LEVEL0;
	}

	if (status == LINK_TRAINING_SUCCESS) {
		status = perform_clock_recovery_sequence(link, lt_settings, DPRX);
	if (status == LINK_TRAINING_SUCCESS) {
		status = perform_channel_equalization_sequence(link,
					lt_settings,
					DPRX);
		}
	}

	return status;
}

enum link_training_result dc_link_dp_perform_link_training(
	struct dc_link *link,
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
	dpcd_exit_training_mode(link);

	/* configure link prior to entering training mode */
	dpcd_configure_lttpr_mode(link, &lt_settings);
	dp_set_fec_ready(link, lt_settings.should_set_fec_ready);
	dpcd_configure_channel_coding(link, &lt_settings);

	/* enter training mode:
	 * Per DP specs starting from here, DPTX device shall not issue
	 * Non-LT AUX transactions inside training mode.
	 */
	if (encoding == DP_8b_10b_ENCODING)
		status = dp_perform_8b_10b_link_training(link, &lt_settings);
	else
		ASSERT(0);

	/* exit training mode and switch to video idle */
	dpcd_exit_training_mode(link);
	if ((status == LINK_TRAINING_SUCCESS) || !skip_video_pattern)
		status = dp_transition_to_video_idle(link,
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
	struct link_encoder *link_enc;
	enum link_training_result status = LINK_TRAINING_CR_FAIL_LANE0;
	struct dc_link_settings current_setting = *link_setting;

	/* Dynamically assigned link encoders associated with stream rather than
	 * link.
	 */
	if (link->dc->res_pool->funcs->link_encs_assign)
		link_enc = stream->link_enc;
	else
		link_enc = link->link_enc;

	/* We need to do this before the link training to ensure the idle pattern in SST
	 * mode will be sent right after the link training
	 */
	link_enc->funcs->connect_dig_be_to_fe(link_enc,
							pipe_ctx->stream_res.stream_enc->id, true);

	for (j = 0; j < attempts; ++j) {

		DC_LOG_HW_LINK_TRAINING("%s: Beginning link training attempt %u of %d\n",
			__func__, (unsigned int)j + 1, attempts);

		dp_enable_link_phy(
			link,
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

			if (cp_psp && cp_psp->funcs.enable_assr) {
				if (!cp_psp->funcs.enable_assr(cp_psp->handle, link)) {
					/* since eDP implies ASSR on, change panel
					 * mode to disable ASSR
					 */
					panel_mode = DP_PANEL_MODE_DEFAULT;
				}
			}
		}
#endif

		dp_set_panel_mode(link, panel_mode);

		if (link->aux_access_disabled) {
			dc_link_dp_perform_link_training_skip_aux(link, &current_setting);
			return true;
		} else {
				status = dc_link_dp_perform_link_training(
										link,
										&current_setting,
										skip_video_pattern);
			if (status == LINK_TRAINING_SUCCESS)
				return true;
		}

		/* latest link training still fail, skip delay and keep PHY on
		 */
		if (j == (attempts - 1) && link->ep_type == DISPLAY_ENDPOINT_PHY)
			break;

		DC_LOG_WARNING("%s: Link training attempt %u of %d failed\n",
			__func__, (unsigned int)j + 1, attempts);

		dp_disable_link_phy(link, signal);

		/* Abort link training if failure due to sink being unplugged. */
		if (status == LINK_TRAINING_ABORT) {
			enum dc_connection_type type = dc_connection_none;

			dc_link_detect_sink(link, &type);
			if (type == dc_connection_none)
				break;
		} else if (do_fallback) {
			decide_fallback_link_setting(*link_setting, &current_setting, status);
			/* Fail link training if reduced link bandwidth no longer meets
			 * stream requirements.
			 */
			if (dc_bandwidth_in_kbps_from_timing(&stream->timing) <
					dc_link_bandwidth_kbps(link, &current_setting))
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

static void set_dp_mst_mode(struct dc_link *link, bool mst_enable)
{
	if (mst_enable == false &&
		link->type == dc_connection_mst_branch) {
		/* Disable MST on link. Use only local sink. */
		dp_disable_link_phy_mst(link, link->connector_signal);

		link->type = dc_connection_single;
		link->local_sink = link->remote_sinks[0];
		link->local_sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
		dc_sink_retain(link->local_sink);
		dm_helpers_dp_mst_stop_top_mgr(link->ctx, link);
	} else if (mst_enable == true &&
			link->type == dc_connection_single &&
			link->remote_sinks[0] != NULL) {
		/* Re-enable MST on link. */
		dp_disable_link_phy(link, link->connector_signal);
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
		set_dp_mst_mode(link, *lt_overrides->mst_enable);

	/* Disable link */
	dp_disable_link_phy(link, link->connector_signal);

	/* Enable link */
	dp_cs_id = get_clock_source_id(link);
	dp_enable_link_phy(link, link->connector_signal,
		dp_cs_id, link_settings);

	/* Set FEC enable */
	fec_enable = lt_overrides->fec_enable && *lt_overrides->fec_enable;
	dp_set_fec_ready(link, fec_enable);

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
		start_clock_recovery_pattern_early(link, &lt_settings, DPRX);

	/* Set link rate, lane count and spread. */
	dpcd_set_link_settings(link, &lt_settings);

	/* 2. perform link training (set link training done
	 *  to false is done as well)
	 */
	lt_status = perform_clock_recovery_sequence(link, &lt_settings, DPRX);
	if (lt_status == LINK_TRAINING_SUCCESS) {
		lt_status = perform_channel_equalization_sequence(link,
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
		dp_disable_link_phy(link, link->connector_signal);
		dp_set_fec_ready(link, false);
	}

	link->sync_lt_in_progress = false;
	return true;
}

bool dc_link_dp_get_max_link_enc_cap(const struct dc_link *link, struct dc_link_settings *max_link_enc_cap)
{
	if (!max_link_enc_cap) {
		DC_LOG_ERROR("%s: Could not return max link encoder caps", __func__);
		return false;
	}

	if (link->link_enc->funcs->get_max_link_cap) {
		link->link_enc->funcs->get_max_link_cap(link->link_enc, max_link_enc_cap);
		return true;
	}

	DC_LOG_ERROR("%s: Max link encoder caps unknown", __func__);
	max_link_enc_cap->lane_count = 1;
	max_link_enc_cap->link_rate = 6;
	return false;
}

static struct dc_link_settings get_max_link_cap(struct dc_link *link)
{
	struct dc_link_settings max_link_cap = {0};

	/* get max link encoder capability */
	link->link_enc->funcs->get_max_link_cap(link->link_enc, &max_link_cap);

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
	/*
	 * account for lttpr repeaters cap
	 * notes: repeaters do not snoop in the DPRX Capabilities addresses (3.6.3).
	 */
	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {
		if (link->dpcd_caps.lttpr_caps.max_lane_count < max_link_cap.lane_count)
			max_link_cap.lane_count = link->dpcd_caps.lttpr_caps.max_lane_count;

		if (link->dpcd_caps.lttpr_caps.max_link_rate < max_link_cap.link_rate)
			max_link_cap.link_rate = link->dpcd_caps.lttpr_caps.max_link_rate;

		DC_LOG_HW_LINK_TRAINING("%s\n Training with LTTPR,  max_lane count %d max_link rate %d \n",
						__func__,
						max_link_cap.lane_count,
						max_link_cap.link_rate);
	}
	return max_link_cap;
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

bool dp_verify_link_cap(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int *fail_count)
{
	struct dc_link_settings max_link_cap = {0};
	struct dc_link_settings cur_link_setting = {0};
	struct dc_link_settings *cur = &cur_link_setting;
	struct dc_link_settings initial_link_settings = {0};
	bool success;
	bool skip_link_training;
	bool skip_video_pattern;
	enum clock_source_id dp_cs_id = CLOCK_SOURCE_ID_EXTERNAL;
	enum link_training_result status;
	union hpd_irq_data irq_data;

	if (link->dc->debug.skip_detection_link_training) {
		link->verified_link_cap = *known_limit_link_setting;
		return true;
	}

	memset(&irq_data, 0, sizeof(irq_data));
	success = false;
	skip_link_training = false;

	max_link_cap = get_max_link_cap(link);

	/* Grant extended timeout request */
	if ((link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) && (link->dpcd_caps.lttpr_caps.max_ext_timeout > 0)) {
		uint8_t grant = link->dpcd_caps.lttpr_caps.max_ext_timeout & 0x80;

		core_link_write_dpcd(link, DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT, &grant, sizeof(grant));
	}

	/* TODO implement override and monitor patch later */

	/* try to train the link from high to low to
	 * find the physical link capability
	 */
	/* disable PHY done possible by BIOS, will be done by driver itself */
	dp_disable_link_phy(link, link->connector_signal);

	dp_cs_id = get_clock_source_id(link);

	/* link training starts with the maximum common settings
	 * supported by both sink and ASIC.
	 */
	initial_link_settings = get_common_supported_link_settings(
			*known_limit_link_setting,
			max_link_cap);
	cur_link_setting = initial_link_settings;

	/* Temporary Renoir-specific workaround for SWDEV-215184;
	 * PHY will sometimes be in bad state on hotplugging display from certain USB-C dongle,
	 * so add extra cycle of enabling and disabling the PHY before first link training.
	 */
	if (link->link_enc->features.flags.bits.DP_IS_USB_C &&
			link->dc->debug.usbc_combo_phy_reset_wa) {
		dp_enable_link_phy(link, link->connector_signal, dp_cs_id, cur);
		dp_disable_link_phy(link, link->connector_signal);
	}

	do {
		skip_video_pattern = true;

		if (cur->link_rate == LINK_RATE_LOW)
			skip_video_pattern = false;

		dp_enable_link_phy(
				link,
				link->connector_signal,
				dp_cs_id,
				cur);


		if (skip_link_training)
			success = true;
		else {
			status = dc_link_dp_perform_link_training(
							link,
							cur,
							skip_video_pattern);
			if (status == LINK_TRAINING_SUCCESS)
				success = true;
			else
				(*fail_count)++;
		}

		if (success) {
			link->verified_link_cap = *cur;
			udelay(1000);
			if (read_hpd_rx_irq_data(link, &irq_data) == DC_OK)
				if (hpd_rx_irq_check_link_loss_status(
						link,
						&irq_data))
					(*fail_count)++;
		}
		/* always disable the link before trying another
		 * setting or before returning we'll enable it later
		 * based on the actual mode we're driving
		 */
		dp_disable_link_phy(link, link->connector_signal);
	} while (!success && decide_fallback_link_setting(
			initial_link_settings, cur, status));

	/* Link Training failed for all Link Settings
	 *  (Lane Count is still unknown)
	 */
	if (!success) {
		/* If all LT fails for all settings,
		 * set verified = failed safe (1 lane low)
		 */
		link->verified_link_cap.lane_count = LANE_COUNT_ONE;
		link->verified_link_cap.link_rate = LINK_RATE_LOW;

		link->verified_link_cap.link_spread =
		LINK_SPREAD_DISABLED;
	}


	return success;
}

bool dp_verify_link_cap_with_retries(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int attempts)
{
	int i = 0;
	bool success = false;

	for (i = 0; i < attempts; i++) {
		int fail_count = 0;
		enum dc_connection_type type = dc_connection_none;

		memset(&link->verified_link_cap, 0,
				sizeof(struct dc_link_settings));
		if (!dc_link_detect_sink(link, &type) || type == dc_connection_none) {
			link->verified_link_cap.lane_count = LANE_COUNT_ONE;
			link->verified_link_cap.link_rate = LINK_RATE_LOW;
			link->verified_link_cap.link_spread = LINK_SPREAD_DISABLED;
			break;
		} else if (dp_verify_link_cap(link,
				&link->reported_link_cap,
				&fail_count) && fail_count == 0) {
			success = true;
			break;
		}
		msleep(10);
	}
	return success;
}

bool dp_verify_mst_link_cap(
	struct dc_link *link)
{
	struct dc_link_settings max_link_cap = {0};

	max_link_cap = get_max_link_cap(link);
	link->verified_link_cap = get_common_supported_link_settings(
		link->reported_link_cap,
		max_link_cap);

	return true;
}

static struct dc_link_settings get_common_supported_link_settings(
		struct dc_link_settings link_setting_a,
		struct dc_link_settings link_setting_b)
{
	struct dc_link_settings link_settings = {0};

	link_settings.lane_count =
		(link_setting_a.lane_count <=
			link_setting_b.lane_count) ?
			link_setting_a.lane_count :
			link_setting_b.lane_count;
	link_settings.link_rate =
		(link_setting_a.link_rate <=
			link_setting_b.link_rate) ?
			link_setting_a.link_rate :
			link_setting_b.link_rate;
	link_settings.link_spread = LINK_SPREAD_DISABLED;

	/* in DP compliance test, DPR-120 may have
	 * a random value in its MAX_LINK_BW dpcd field.
	 * We map it to the maximum supported link rate that
	 * is smaller than MAX_LINK_BW in this case.
	 */
	if (link_settings.link_rate > LINK_RATE_HIGH3) {
		link_settings.link_rate = LINK_RATE_HIGH3;
	} else if (link_settings.link_rate < LINK_RATE_HIGH3
			&& link_settings.link_rate > LINK_RATE_HIGH2) {
		link_settings.link_rate = LINK_RATE_HIGH2;
	} else if (link_settings.link_rate < LINK_RATE_HIGH2
			&& link_settings.link_rate > LINK_RATE_HIGH) {
		link_settings.link_rate = LINK_RATE_HIGH;
	} else if (link_settings.link_rate < LINK_RATE_HIGH
			&& link_settings.link_rate > LINK_RATE_LOW) {
		link_settings.link_rate = LINK_RATE_LOW;
	} else if (link_settings.link_rate < LINK_RATE_LOW) {
		link_settings.link_rate = LINK_RATE_UNKNOWN;
	}

	return link_settings;
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
	default:
		return LINK_RATE_UNKNOWN;
	}
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
		struct dc_link_settings initial_link_settings,
		struct dc_link_settings *current_link_setting,
		enum link_training_result training_result)
{
	if (!current_link_setting)
		return false;

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
		if (decide_edp_link_settings(link, link_setting, req_bw))
			return;
	} else if (decide_dp_link_settings(link, link_setting, req_bw))
		return;

	BREAK_TO_DEBUGGER();
	ASSERT(link->verified_link_cap.lane_count != LANE_COUNT_UNKNOWN);

	*link_setting = link->verified_link_cap;
}

/*************************Short Pulse IRQ***************************/
static bool allow_hpd_rx_irq(const struct dc_link *link)
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
			/* Acknowledge and clear error bits */
			dm_helpers_dp_write_dpcd(
				link->ctx,
				link,
				8198,/*DpcdAddress_PSR_Error_Status*/
				&psr_error_status.raw,
				sizeof(psr_error_status.raw));

			/* PSR error, disable and re-enable PSR */
			dc_link_set_psr_allow_active(link, false, true, false);
			dc_link_set_psr_allow_active(link, true, true, false);

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
			(DP_TEST_80BIT_CUSTOM_PATTERN_79_72 -
			DP_TEST_80BIT_CUSTOM_PATTERN_7_0)+1] = {0};
	unsigned int test_pattern_size = 0;
	enum dp_test_pattern test_pattern;
	struct dc_link_training_settings link_settings;
	union lane_adjust dpcd_lane_adjust;
	unsigned int lane;
	struct link_training_settings link_training_settings;
	int i = 0;

	dpcd_test_pattern.raw = 0;
	memset(dpcd_lane_adjustment, 0, sizeof(dpcd_lane_adjustment));
	memset(&link_settings, 0, sizeof(link_settings));

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

	/* prepare link training settings */
	link_settings.link = link->cur_link_settings;

	for (lane = 0; lane <
		(unsigned int)(link->cur_link_settings.lane_count);
		lane++) {
		dpcd_lane_adjust.raw =
			get_nibble_at_index(&dpcd_lane_adjustment[0].raw, lane);
		link_settings.lane_settings[lane].VOLTAGE_SWING =
			(enum dc_voltage_swing)
			(dpcd_lane_adjust.bits.VOLTAGE_SWING_LANE);
		link_settings.lane_settings[lane].PRE_EMPHASIS =
			(enum dc_pre_emphasis)
			(dpcd_lane_adjust.bits.PRE_EMPHASIS_LANE);
		link_settings.lane_settings[lane].POST_CURSOR2 =
			(enum dc_post_cursor2)
			((dpcd_post_cursor_2_adjustment >> (lane * 2)) & 0x03);
	}

	for (i = 0; i < 4; i++)
		link_training_settings.lane_settings[i] =
				link_settings.lane_settings[i];
	link_training_settings.link_settings = link_settings.link;
	link_training_settings.allow_invalid_msa_timing_param = false;
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

static void handle_automated_test(struct dc_link *link)
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

bool dc_link_handle_hpd_rx_irq(struct dc_link *link, union hpd_irq_data *out_hpd_irq_dpcd_data, bool *out_link_loss)
{
	union hpd_irq_data hpd_irq_dpcd_data = { { { {0} } } };
	union device_service_irq device_service_clear = { { 0 } };
	enum dc_status result;
	bool status = false;
	struct pipe_ctx *pipe_ctx;
	int i;

	if (out_link_loss)
		*out_link_loss = false;
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
		handle_automated_test(link);
		return false;
	}

	if (!allow_hpd_rx_irq(link)) {
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

	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY)
		return true;

	/* check if we have MST msg and return since we poll for it */
	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY)
		return false;

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

		for (i = 0; i < MAX_PIPES; i++) {
			pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream && pipe_ctx->stream->link == link)
				break;
		}

		if (pipe_ctx == NULL || pipe_ctx->stream == NULL)
			return false;


		for (i = 0; i < MAX_PIPES; i++) {
			pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream && !pipe_ctx->stream->dpms_off &&
					pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe)
				core_link_disable_stream(pipe_ctx);
		}

		for (i = 0; i < MAX_PIPES; i++) {
			pipe_ctx = &link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream && !pipe_ctx->stream->dpms_off &&
					pipe_ctx->stream->link == link && !pipe_ctx->prev_odm_pipe)
				core_link_enable_stream(link->dc->current_state, pipe_ctx);
		}

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
	uint8_t lttpr_dpcd_data[6];
	bool vbios_lttpr_enable = link->dc->caps.vbios_lttpr_enable;
	bool vbios_lttpr_interop = link->dc->caps.vbios_lttpr_aware;
	enum dc_status status = DC_ERROR_UNEXPECTED;
	bool is_lttpr_present = false;

	memset(lttpr_dpcd_data, '\0', sizeof(lttpr_dpcd_data));

	/*
	 * Logic to determine LTTPR mode
	 */
	link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
	if (vbios_lttpr_enable && vbios_lttpr_interop)
		link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
	else if (!vbios_lttpr_enable && vbios_lttpr_interop) {
		if (link->dc->config.allow_lttpr_non_transparent_mode)
			link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
		else
			link->lttpr_mode = LTTPR_MODE_TRANSPARENT;
	} else if (!vbios_lttpr_enable && !vbios_lttpr_interop) {
		if (!link->dc->config.allow_lttpr_non_transparent_mode
			|| !link->dc->caps.extended_aux_timeout_support)
			link->lttpr_mode = LTTPR_MODE_NON_LTTPR;
		else
			link->lttpr_mode = LTTPR_MODE_NON_TRANSPARENT;
	}

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
			dm_error("%s: Read LTTPR caps data failed.\n", __func__);
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

		/* Attempt to train in LTTPR transparent mode if repeater count exceeds 8. */
		is_lttpr_present = (dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) != 0 &&
				link->dpcd_caps.lttpr_caps.max_lane_count > 0 &&
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
		status = core_link_read_dpcd(
				link,
				DP_DSC_BRANCH_OVERALL_THROUGHPUT_0,
				link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw,
				sizeof(link->dpcd_caps.dsc_caps.dsc_branch_decoder_caps.raw));
	}

	if (!dpcd_read_sink_ext_caps(link))
		link->dpcd_sink_ext_caps.raw = 0;

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

	/* dc init_hw has power encoder using default
	 * signal for connector. For native DP, no
	 * need to power up encoder again. If not native
	 * DP, hw_init may need check signal or power up
	 * encoder here.
	 */
	/* TODO save sink caps in link->sink */
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
	link->verified_link_cap = link->reported_link_cap;

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
			int dpg_width = width;

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
		dp_set_hw_test_pattern(link, test_pattern,
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
			dp_set_hw_lane_settings(link, p_link_settings, DPRX);
			dpcd_set_lane_settings(link, p_link_settings, DPRX);
		}

		/* Blank stream if running test pattern */
		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/*TODO:
			 * m_pHwss->
			 * MuteAudioEndpoint(pPathMode->pDisplayPath, true);
			 */
			/* Blank stream */
			pipes->stream_res.stream_enc->funcs->dp_blank(pipe_ctx->stream_res.stream_enc);
		}

		dp_set_hw_test_pattern(link, test_pattern,
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
		default:
			return false;
		}

		if (test_pattern == DP_TEST_PATTERN_VIDEO_MODE
		/*TODO:&& !pPathMode->pDisplayPath->IsTargetPoweredOn()*/)
			return false;

		if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
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

enum dc_status dp_set_fec_ready(struct dc_link *link, bool ready)
{
	/* FEC has to be "set ready" before the link training.
	 * The policy is to always train with FEC
	 * if the sink supports it and leave it enabled on link.
	 * If FEC is not supported, disable it.
	 */
	struct link_encoder *link_enc = NULL;
	enum dc_status status = DC_OK;
	uint8_t fec_config = 0;

	/* Access link encoder based on whether it is statically
	 * or dynamically assigned to a link.
	 */
	if (link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign)
		link_enc = link_enc_cfg_get_link_enc_used_by_link(link->dc->current_state, link);
	else
		link_enc = link->link_enc;
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
				link_enc->funcs->fec_set_ready(link->link_enc, false);
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

	/* Access link encoder based on whether it is statically
	 * or dynamically assigned to a link.
	 */
	if (link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign)
		link_enc = link_enc_cfg_get_link_enc_used_by_link(
				link->dc->current_state, link);
	else
		link_enc = link->link_enc;
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
	union lane_count_set lane_count_set = { {0} };

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

	decide_edp_link_settings(link, &link_setting, req_bw);

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
	return DP_UNKNOWN_ENCODING;
}

