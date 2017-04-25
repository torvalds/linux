/* Copyright 2015 Advanced Micro Devices, Inc. */
#include "dm_services.h"
#include "dc.h"
#include "dc_link_dp.h"
#include "dm_helpers.h"

#include "inc/core_types.h"
#include "link_hwss.h"
#include "dc_link_ddc.h"
#include "core_status.h"
#include "dpcd_defs.h"

#include "core_dc.h"
#include "resource.h"

/* maximum pre emphasis level allowed for each voltage swing level*/
static const enum dc_pre_emphasis voltage_swing_to_pre_emphasis[] = {
		PRE_EMPHASIS_LEVEL3,
		PRE_EMPHASIS_LEVEL2,
		PRE_EMPHASIS_LEVEL1,
		PRE_EMPHASIS_DISABLED };

enum {
	POST_LT_ADJ_REQ_LIMIT = 6,
	POST_LT_ADJ_REQ_TIMEOUT = 200
};

enum {
	LINK_TRAINING_MAX_RETRY_COUNT = 5,
	/* to avoid infinite loop where-in the receiver
	 * switches between different VS
	 */
	LINK_TRAINING_MAX_CR_RETRY = 100
};

static const struct dc_link_settings link_training_fallback_table[] = {
/* 4320 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH3, LINK_SPREAD_DISABLED },
/* 2160 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 648 Mbytes/sec*/
{ LANE_COUNT_FOUR, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 2160 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH3, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 324 Mbytes/sec*/
{ LANE_COUNT_TWO, LINK_RATE_LOW, LINK_SPREAD_DISABLED },
/* 1080 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH3, LINK_SPREAD_DISABLED },
/* 540 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH2, LINK_SPREAD_DISABLED },
/* 270 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_HIGH, LINK_SPREAD_DISABLED },
/* 162 Mbytes/sec*/
{ LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED } };

static void wait_for_training_aux_rd_interval(
	struct core_link* link,
	uint32_t default_wait_in_micro_secs)
{
	union training_aux_rd_interval training_rd_interval;

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
			default_wait_in_micro_secs =
				training_rd_interval.bits.TRAINIG_AUX_RD_INTERVAL * 4000;
	}

	udelay(default_wait_in_micro_secs);

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s:\n wait = %d\n",
		__func__,
		default_wait_in_micro_secs);
}

static void dpcd_set_training_pattern(
	struct core_link* link,
	union dpcd_training_pattern dpcd_pattern)
{
	core_link_write_dpcd(
		link,
		DP_TRAINING_PATTERN_SET,
		&dpcd_pattern.raw,
		1);

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s\n %x pattern = %x\n",
		__func__,
		DP_TRAINING_PATTERN_SET,
		dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
}

static void dpcd_set_link_settings(
	struct core_link* link,
	const struct link_training_settings *lt_settings)
{
	uint8_t rate = (uint8_t)
	(lt_settings->link_settings.link_rate);

	union down_spread_ctrl downspread = {{0}};
	union lane_count_set lane_count_set = {{0}};
	uint8_t link_set_buffer[2];

	downspread.raw = (uint8_t)
	(lt_settings->link_settings.link_spread);

	lane_count_set.bits.LANE_COUNT_SET =
	lt_settings->link_settings.lane_count;

	lane_count_set.bits.ENHANCED_FRAMING = 1;

	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED =
		link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED;

	link_set_buffer[0] = rate;
	link_set_buffer[1] = lane_count_set.raw;

	core_link_write_dpcd(link, DP_LINK_BW_SET,
	link_set_buffer, 2);
	core_link_write_dpcd(link, DP_DOWNSPREAD_CTRL,
	&downspread.raw, sizeof(downspread));

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s\n %x rate = %x\n %x lane = %x\n %x spread = %x\n",
		__func__,
		DP_LINK_BW_SET,
		lt_settings->link_settings.link_rate,
		DP_LANE_COUNT_SET,
		lt_settings->link_settings.lane_count,
		DP_DOWNSPREAD_CTRL,
		lt_settings->link_settings.link_spread);

}

static enum dpcd_training_patterns
	hw_training_pattern_to_dpcd_training_pattern(
	struct core_link* link,
	enum hw_dp_training_pattern pattern)
{
	enum dpcd_training_patterns dpcd_tr_pattern =
	DPCD_TRAINING_PATTERN_VIDEOIDLE;

	switch (pattern) {
	case HW_DP_TRAINING_PATTERN_1:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_1;
		break;
	case HW_DP_TRAINING_PATTERN_2:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_2;
		break;
	case HW_DP_TRAINING_PATTERN_3:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_3;
		break;
	case HW_DP_TRAINING_PATTERN_4:
		dpcd_tr_pattern = DPCD_TRAINING_PATTERN_4;
		break;
	default:
		ASSERT(0);
		dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
			"%s: Invalid HW Training pattern: %d\n",
			__func__, pattern);
		break;
	}

	return dpcd_tr_pattern;

}

static void dpcd_set_lt_pattern_and_lane_settings(
	struct core_link* link,
	const struct link_training_settings *lt_settings,
	enum hw_dp_training_pattern pattern)
{
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX] = {{{0}}};
	const uint32_t dpcd_base_lt_offset =
	DP_TRAINING_PATTERN_SET;
	uint8_t dpcd_lt_buffer[5] = {0};
	union dpcd_training_pattern dpcd_pattern = {{0}};
	uint32_t lane;
	uint32_t size_in_bytes;
	bool edp_workaround = false; /* TODO link_prop.INTERNAL */

	/*****************************************************************
	* DpcdAddress_TrainingPatternSet
	*****************************************************************/
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
		hw_training_pattern_to_dpcd_training_pattern(link, pattern);

	dpcd_lt_buffer[DP_TRAINING_PATTERN_SET - dpcd_base_lt_offset]
		= dpcd_pattern.raw;

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s\n %x pattern = %x\n",
		__func__,
		DP_TRAINING_PATTERN_SET,
		dpcd_pattern.v1_4.TRAINING_PATTERN_SET);

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

	/* concatinate everything into one buffer*/

	size_in_bytes = lt_settings->link_settings.lane_count * sizeof(dpcd_lane[0]);

	 // 0x00103 - 0x00102
	memmove(
		&dpcd_lt_buffer[DP_TRAINING_LANE0_SET - dpcd_base_lt_offset],
		dpcd_lane,
		size_in_bytes);

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s:\n %x VS set = %x  PE set = %x \
		max VS Reached = %x  max PE Reached = %x\n",
		__func__,
		DP_TRAINING_LANE0_SET,
		dpcd_lane[0].bits.VOLTAGE_SWING_SET,
		dpcd_lane[0].bits.PRE_EMPHASIS_SET,
		dpcd_lane[0].bits.MAX_SWING_REACHED,
		dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);

	if (edp_workaround) {
		/* for eDP write in 2 parts because the 5-byte burst is
		* causing issues on some eDP panels (EPR#366724)
		*/
		core_link_write_dpcd(
			link,
			DP_TRAINING_PATTERN_SET,
			&dpcd_pattern.raw,
			sizeof(dpcd_pattern.raw) );

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
				size_in_bytes + sizeof(dpcd_pattern.raw) );

	link->public.cur_lane_setting = lt_settings->lane_settings[0];
}

static bool is_cr_done(enum dc_lane_count ln_count,
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

static bool is_ch_eq_done(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status,
	union lane_align_status_updated *lane_status_updated)
{
	bool done = true;
	uint32_t lane;
	if (!lane_status_updated->bits.INTERLANE_ALIGN_DONE)
		done = false;
	else {
		for (lane = 0; lane < (uint32_t)(ln_count); lane++) {
			if (!dpcd_lane_status[lane].bits.SYMBOL_LOCKED_0 ||
				!dpcd_lane_status[lane].bits.CHANNEL_EQ_DONE_0)
				done = false;
		}
	}
	return done;

}

static void update_drive_settings(
		struct link_training_settings *dest,
		struct link_training_settings src)
{
	uint32_t lane;
	for (lane = 0; lane < src.link_settings.lane_count; lane++) {
		dest->lane_settings[lane].VOLTAGE_SWING =
			src.lane_settings[lane].VOLTAGE_SWING;
		dest->lane_settings[lane].PRE_EMPHASIS =
			src.lane_settings[lane].PRE_EMPHASIS;
		dest->lane_settings[lane].POST_CURSOR2 =
			src.lane_settings[lane].POST_CURSOR2;
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

static void get_lane_status_and_drive_settings(
	struct core_link* link,
	const struct link_training_settings *link_training_setting,
	union lane_status *ln_status,
	union lane_align_status_updated *ln_status_updated,
	struct link_training_settings *req_settings)
{
	uint8_t dpcd_buf[6] = {0};
	union lane_adjust dpcd_lane_adjust[LANE_COUNT_DP_MAX] = {{{0}}};
	struct link_training_settings request_settings = {{0}};
	uint32_t lane;

	memset(req_settings, '\0', sizeof(struct link_training_settings));

	core_link_read_dpcd(
		link,
		DP_LANE0_1_STATUS,
		(uint8_t *)(dpcd_buf),
		sizeof(dpcd_buf));

	for (lane = 0; lane <
		(uint32_t)(link_training_setting->link_settings.lane_count);
		lane++) {

		ln_status[lane].raw =
			get_nibble_at_index(&dpcd_buf[0], lane);
		dpcd_lane_adjust[lane].raw =
			get_nibble_at_index(&dpcd_buf[4], lane);
	}

	ln_status_updated->raw = dpcd_buf[2];

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s:\n%x Lane01Status = %x\n %x Lane23Status = %x\n ",
		__func__,
		DP_LANE0_1_STATUS, dpcd_buf[0],
		DP_LANE2_3_STATUS, dpcd_buf[1]);

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s:\n %x Lane01AdjustRequest = %x\n %x Lane23AdjustRequest = %x\n",
		__func__,
		DP_ADJUST_REQUEST_LANE0_1,
		dpcd_buf[4],
		DP_ADJUST_REQUEST_LANE2_3,
		dpcd_buf[5]);

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

}

static void dpcd_set_lane_settings(
	struct core_link* link,
	const struct link_training_settings *link_training_setting)
{
	union dpcd_training_lane dpcd_lane[LANE_COUNT_DP_MAX] = {{{0}}};
	uint32_t lane;

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

	core_link_write_dpcd(link,
		DP_TRAINING_LANE0_SET,
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

	dm_logger_write(link->ctx->logger, LOG_HW_LINK_TRAINING,
		"%s\n %x VS set = %x  PE set = %x \
		max VS Reached = %x  max PE Reached = %x\n",
		__func__,
		DP_TRAINING_LANE0_SET,
		dpcd_lane[0].bits.VOLTAGE_SWING_SET,
		dpcd_lane[0].bits.PRE_EMPHASIS_SET,
		dpcd_lane[0].bits.MAX_SWING_REACHED,
		dpcd_lane[0].bits.MAX_PRE_EMPHASIS_REACHED);

	link->public.cur_lane_setting = link_training_setting->lane_settings[0];

}

static bool is_max_vs_reached(
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

void dc_link_dp_set_drive_settings(
	const struct dc_link *link,
	struct link_training_settings *lt_settings)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);
	/* program ASIC PHY settings*/
	dp_set_hw_lane_settings(core_link, lt_settings);

	/* Notify DP sink the PHY settings from source */
	dpcd_set_lane_settings(core_link, lt_settings);
}

static bool perform_post_lt_adj_req_sequence(
	struct core_link *link,
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

			get_lane_status_and_drive_settings(
			link,
			lt_settings,
			dpcd_lane_status,
			&dpcd_lane_status_updated,
			&req_settings);

			if (dpcd_lane_status_updated.bits.
					POST_LT_ADJ_REQ_IN_PROGRESS == 0)
				return true;

			if (!is_cr_done(lane_count, dpcd_lane_status))
				return false;

			if (!is_ch_eq_done(
				lane_count,
				dpcd_lane_status,
				&dpcd_lane_status_updated))
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
				update_drive_settings(
					lt_settings,req_settings);

				dc_link_dp_set_drive_settings(&link->public,
						lt_settings);
				break;
			}

			msleep(1);
		}

		if (!req_drv_setting_changed) {
			dm_logger_write(link->ctx->logger, LOG_WARNING,
				"%s: Post Link Training Adjust Request Timed out\n",
				__func__);

			ASSERT(0);
			return true;
		}
	}
	dm_logger_write(link->ctx->logger, LOG_WARNING,
		"%s: Post Link Training Adjust Request limit reached\n",
		__func__);

	ASSERT(0);
	return true;

}

static enum hw_dp_training_pattern get_supported_tp(struct core_link *link)
{
	enum hw_dp_training_pattern highest_tp = HW_DP_TRAINING_PATTERN_2;
	struct encoder_feature_support *features = &link->link_enc->features;
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;

	if (features->flags.bits.IS_TPS3_CAPABLE)
		highest_tp = HW_DP_TRAINING_PATTERN_3;

	if (features->flags.bits.IS_TPS4_CAPABLE)
		highest_tp = HW_DP_TRAINING_PATTERN_4;

	if (dpcd_caps->max_down_spread.bits.TPS4_SUPPORTED &&
		highest_tp >= HW_DP_TRAINING_PATTERN_4)
		return HW_DP_TRAINING_PATTERN_4;

	if (dpcd_caps->max_ln_count.bits.TPS3_SUPPORTED &&
		highest_tp >= HW_DP_TRAINING_PATTERN_3)
		return HW_DP_TRAINING_PATTERN_3;

	return HW_DP_TRAINING_PATTERN_2;
}

static bool perform_channel_equalization_sequence(
	struct core_link *link,
	struct link_training_settings *lt_settings)
{
	struct link_training_settings req_settings;
	enum hw_dp_training_pattern hw_tr_pattern;
	uint32_t retries_ch_eq;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_align_status_updated dpcd_lane_status_updated = {{0}};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = {{{0}}};;

	hw_tr_pattern = get_supported_tp(link);

	dp_set_hw_training_pattern(link, hw_tr_pattern);

	for (retries_ch_eq = 0; retries_ch_eq <= LINK_TRAINING_MAX_RETRY_COUNT;
		retries_ch_eq++) {

		dp_set_hw_lane_settings(link, lt_settings);

		/* 2. update DPCD*/
		if (!retries_ch_eq)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration*/
			dpcd_set_lt_pattern_and_lane_settings(
				link,
				lt_settings,
				hw_tr_pattern);
		else
			dpcd_set_lane_settings(link, lt_settings);

		/* 3. wait for receiver to lock-on*/
		wait_for_training_aux_rd_interval(link, 400);

		/* 4. Read lane status and requested
		 * drive settings as set by the sink*/

		get_lane_status_and_drive_settings(
			link,
			lt_settings,
			dpcd_lane_status,
			&dpcd_lane_status_updated,
			&req_settings);

		/* 5. check CR done*/
		if (!is_cr_done(lane_count, dpcd_lane_status))
			return false;

		/* 6. check CHEQ done*/
		if (is_ch_eq_done(lane_count,
			dpcd_lane_status,
			&dpcd_lane_status_updated))
			return true;

		/* 7. update VS/PE/PC2 in lt_settings*/
		update_drive_settings(lt_settings, req_settings);
	}

	return false;

}

static bool perform_clock_recovery_sequence(
	struct core_link *link,
	struct link_training_settings *lt_settings)
{
	uint32_t retries_cr;
	uint32_t retry_count;
	uint32_t lane;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count =
	lt_settings->link_settings.lane_count;
	enum hw_dp_training_pattern hw_tr_pattern = HW_DP_TRAINING_PATTERN_1;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated dpcd_lane_status_updated;

	retries_cr = 0;
	retry_count = 0;
	/* initial drive setting (VS/PE/PC2)*/
	for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++) {
		lt_settings->lane_settings[lane].VOLTAGE_SWING =
		VOLTAGE_SWING_LEVEL0;
		lt_settings->lane_settings[lane].PRE_EMPHASIS =
		PRE_EMPHASIS_DISABLED;
		lt_settings->lane_settings[lane].POST_CURSOR2 =
		POST_CURSOR2_DISABLED;
	}

	dp_set_hw_training_pattern(link, hw_tr_pattern);

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
				lt_settings);

		/* 2. update DPCD of the receiver*/
		if (!retries_cr)
			/* EPR #361076 - write as a 5-byte burst,
			 * but only for the 1-st iteration.*/
			dpcd_set_lt_pattern_and_lane_settings(
					link,
					lt_settings,
					hw_tr_pattern);
		else
			dpcd_set_lane_settings(
					link,
					lt_settings);

		/* 3. wait receiver to lock-on*/
		wait_for_training_aux_rd_interval(
				link,
				100);

		/* 4. Read lane status and requested drive
		* settings as set by the sink
		*/
		get_lane_status_and_drive_settings(
				link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings);

		/* 5. check CR done*/
		if (is_cr_done(lane_count, dpcd_lane_status))
			return true;

		/* 6. max VS reached*/
		if (is_max_vs_reached(lt_settings))
			return false;

		/* 7. same voltage*/
		/* Note: VS same for all lanes,
		* so comparing first lane is sufficient*/
		if (lt_settings->lane_settings[0].VOLTAGE_SWING ==
			req_settings.lane_settings[0].VOLTAGE_SWING)
			retries_cr++;
		else
			retries_cr = 0;

		/* 8. update VS/PE/PC2 in lt_settings*/
		update_drive_settings(lt_settings, req_settings);

		retry_count++;
	}

	if (retry_count >= LINK_TRAINING_MAX_CR_RETRY) {
		ASSERT(0);
		dm_logger_write(link->ctx->logger, LOG_ERROR,
			"%s: Link Training Error, could not \
			 get CR after %d tries. \
			Possibly voltage swing issue", __func__,
			LINK_TRAINING_MAX_CR_RETRY);

	}

	return false;
}

static inline bool perform_link_training_int(
	struct core_link *link,
	struct link_training_settings *lt_settings,
	bool status)
{
	union lane_count_set lane_count_set = { {0} };
	union dpcd_training_pattern dpcd_pattern = { {0} };

	/* 3. set training not in progress*/
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET = DPCD_TRAINING_PATTERN_VIDEOIDLE;
	dpcd_set_training_pattern(link, dpcd_pattern);

	/* 4. mainlink output idle pattern*/
	dp_set_hw_test_pattern(link, DP_TEST_PATTERN_VIDEO_MODE, NULL, 0);

	/*
	 * 5. post training adjust if required
	 * If the upstream DPTX and downstream DPRX both support TPS4,
	 * TPS4 must be used instead of POST_LT_ADJ_REQ.
	 */
	if (link->dpcd_caps.max_ln_count.bits.POST_LT_ADJ_REQ_SUPPORTED != 1 ||
			get_supported_tp(link) == HW_DP_TRAINING_PATTERN_4)
		return status;

	if (status &&
		perform_post_lt_adj_req_sequence(link, lt_settings) == false)
		status = false;

	lane_count_set.bits.LANE_COUNT_SET = lt_settings->link_settings.lane_count;
	lane_count_set.bits.ENHANCED_FRAMING = 1;
	lane_count_set.bits.POST_LT_ADJ_REQ_GRANTED = 0;

	core_link_write_dpcd(
		link,
		DP_LANE_COUNT_SET,
		&lane_count_set.raw,
		sizeof(lane_count_set));

	return status;
}

bool dc_link_dp_perform_link_training(
	struct dc_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);
	bool status;

	char *link_rate = "Unknown";
	struct link_training_settings lt_settings;

	status = false;
	memset(&lt_settings, '\0', sizeof(lt_settings));

	lt_settings.link_settings.link_rate = link_setting->link_rate;
	lt_settings.link_settings.lane_count = link_setting->lane_count;

	/*@todo[vdevulap] move SS to LS, should not be handled by displaypath*/

	/* TODO hard coded to SS for now
	 * lt_settings.link_settings.link_spread =
	 * dal_display_path_is_ss_supported(
	 * path_mode->display_path) ?
	 * LINK_SPREAD_05_DOWNSPREAD_30KHZ :
	 * LINK_SPREAD_DISABLED;
	 */
	lt_settings.link_settings.link_spread = LINK_SPREAD_05_DOWNSPREAD_30KHZ;

	/* 1. set link rate, lane count and spread*/
	dpcd_set_link_settings(core_link, &lt_settings);

	/* 2. perform link training (set link training done
	 *  to false is done as well)*/
	if (perform_clock_recovery_sequence(core_link, &lt_settings)) {

		if (perform_channel_equalization_sequence(core_link,
				&lt_settings))
			status = true;
	}

	if (status || !skip_video_pattern)
		status = perform_link_training_int(core_link,
				&lt_settings, status);

	/* 6. print status message*/
	switch (lt_settings.link_settings.link_rate) {

	case LINK_RATE_LOW:
		link_rate = "RBR";
		break;
	case LINK_RATE_HIGH:
		link_rate = "HBR";
		break;
	case LINK_RATE_HIGH2:
		link_rate = "HBR2";
		break;
	case LINK_RATE_RBR2:
		link_rate = "RBR2";
		break;
	case LINK_RATE_HIGH3:
		link_rate = "HBR3";
		break;
	default:
		break;
	}

	/* Connectivity log: link training */
	CONN_MSG_LT(core_link, "%sx%d %s VS=%d, PE=%d",
			link_rate,
			lt_settings.link_settings.lane_count,
			status ? "pass" : "fail",
			lt_settings.lane_settings[0].VOLTAGE_SWING,
			lt_settings.lane_settings[0].PRE_EMPHASIS);

	return status;
}


bool perform_link_training_with_retries(
	struct core_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern,
	int attempts)
{
	uint8_t j;
	uint8_t delay_between_attempts = LINK_TRAINING_RETRY_DELAY;

	for (j = 0; j < attempts; ++j) {

		if (dc_link_dp_perform_link_training(
				&link->public,
				link_setting,
				skip_video_pattern))
			return true;

		msleep(delay_between_attempts);
		delay_between_attempts += LINK_TRAINING_RETRY_DELAY;
	}

	return false;
}

/*TODO add more check to see if link support request link configuration */
static bool is_link_setting_supported(
	const struct dc_link_settings *link_setting,
	const struct dc_link_settings *max_link_setting)
{
	if (link_setting->lane_count > max_link_setting->lane_count ||
		link_setting->link_rate > max_link_setting->link_rate)
		return false;
	return true;
}

static const uint32_t get_link_training_fallback_table_len(
	struct core_link *link)
{
	return ARRAY_SIZE(link_training_fallback_table);
}

static const struct dc_link_settings *get_link_training_fallback_table(
	struct core_link *link, uint32_t i)
{
	return &link_training_fallback_table[i];
}

static bool exceeded_limit_link_setting(
	const struct dc_link_settings *link_setting,
	const struct dc_link_settings *limit_link_setting)
{
	return (link_setting->lane_count * link_setting->link_rate
		 > limit_link_setting->lane_count * limit_link_setting->link_rate ?
				 true : false);
}

static struct dc_link_settings get_max_link_cap(struct core_link *link)
{
	/* Set Default link settings */
	struct dc_link_settings max_link_cap = {LANE_COUNT_FOUR, LINK_RATE_HIGH,
			LINK_SPREAD_05_DOWNSPREAD_30KHZ};

	/* Higher link settings based on feature supported */
	if (link->link_enc->features.flags.bits.IS_HBR2_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_HIGH2;

	if (link->link_enc->features.flags.bits.IS_HBR3_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_HIGH3;

	/* Lower link settings based on sink's link cap */
	if (link->public.reported_link_cap.lane_count < max_link_cap.lane_count)
		max_link_cap.lane_count =
				link->public.reported_link_cap.lane_count;
	if (link->public.reported_link_cap.link_rate < max_link_cap.link_rate)
		max_link_cap.link_rate =
				link->public.reported_link_cap.link_rate;
	if (link->public.reported_link_cap.link_spread <
			max_link_cap.link_spread)
		max_link_cap.link_spread =
				link->public.reported_link_cap.link_spread;
	return max_link_cap;
}

bool dp_hbr_verify_link_cap(
	struct core_link *link,
	struct dc_link_settings *known_limit_link_setting)
{
	struct dc_link_settings max_link_cap = {0};
	bool success;
	bool skip_link_training;
	const struct dc_link_settings *cur;
	bool skip_video_pattern;
	uint32_t i;
	struct clock_source *dp_cs;
	enum clock_source_id dp_cs_id = CLOCK_SOURCE_ID_EXTERNAL;

	success = false;
	skip_link_training = false;

	max_link_cap = get_max_link_cap(link);

	/* TODO implement override and monitor patch later */

	/* try to train the link from high to low to
	 * find the physical link capability
	 */
	/* disable PHY done possible by BIOS, will be done by driver itself */
	dp_disable_link_phy(link, link->public.connector_signal);

	dp_cs = link->dc->res_pool->dp_clock_source;

	if (dp_cs)
		dp_cs_id = dp_cs->id;
	else {
		/*
		 * dp clock source is not initialized for some reason.
		 * Should not happen, CLOCK_SOURCE_ID_EXTERNAL will be used
		 */
		ASSERT(dp_cs);
	}

	for (i = 0; i < get_link_training_fallback_table_len(link) &&
		!success; i++) {
		cur = get_link_training_fallback_table(link, i);

		if (known_limit_link_setting->lane_count != LANE_COUNT_UNKNOWN &&
			exceeded_limit_link_setting(cur,
					known_limit_link_setting))
			continue;

		if (!is_link_setting_supported(cur, &max_link_cap))
			continue;

		skip_video_pattern = true;
		if (cur->link_rate == LINK_RATE_LOW)
			skip_video_pattern = false;

		dp_enable_link_phy(
				link,
				link->public.connector_signal,
				dp_cs_id,
				cur);

		if (skip_link_training)
			success = true;
		else {
			success = dc_link_dp_perform_link_training(
							&link->public,
							cur,
							skip_video_pattern);
		}

		if (success)
			link->public.verified_link_cap = *cur;

		/* always disable the link before trying another
		 * setting or before returning we'll enable it later
		 * based on the actual mode we're driving
		 */
		dp_disable_link_phy(link, link->public.connector_signal);
	}

	/* Link Training failed for all Link Settings
	 *  (Lane Count is still unknown)
	 */
	if (!success) {
		/* If all LT fails for all settings,
		 * set verified = failed safe (1 lane low)
		 */
		link->public.verified_link_cap.lane_count = LANE_COUNT_ONE;
		link->public.verified_link_cap.link_rate = LINK_RATE_LOW;

		link->public.verified_link_cap.link_spread =
		LINK_SPREAD_DISABLED;
	}

	link->public.max_link_setting = link->public.verified_link_cap;

	return success;
}

static uint32_t bandwidth_in_kbps_from_timing(
	const struct dc_crtc_timing *timing)
{
	uint32_t bits_per_channel = 0;
	uint32_t kbps;
	switch (timing->display_color_depth) {

	case COLOR_DEPTH_666:
		bits_per_channel = 6;
		break;
	case COLOR_DEPTH_888:
		bits_per_channel = 8;
		break;
	case COLOR_DEPTH_101010:
		bits_per_channel = 10;
		break;
	case COLOR_DEPTH_121212:
		bits_per_channel = 12;
		break;
	case COLOR_DEPTH_141414:
		bits_per_channel = 14;
		break;
	case COLOR_DEPTH_161616:
		bits_per_channel = 16;
		break;
	default:
		break;
	}
	ASSERT(bits_per_channel != 0);

	kbps = timing->pix_clk_khz;
	kbps *= bits_per_channel;

	if (timing->flags.Y_ONLY != 1)
		/*Only YOnly make reduce bandwidth by 1/3 compares to RGB*/
		kbps *= 3;

	return kbps;

}

static uint32_t bandwidth_in_kbps_from_link_settings(
	const struct dc_link_settings *link_setting)
{
	uint32_t link_rate_in_kbps = link_setting->link_rate *
		LINK_RATE_REF_FREQ_IN_KHZ;

	uint32_t lane_count  = link_setting->lane_count;
	uint32_t kbps = link_rate_in_kbps;
	kbps *= lane_count;
	kbps *= 8;   /* 8 bits per byte*/

	return kbps;

}

bool dp_validate_mode_timing(
	struct core_link *link,
	const struct dc_crtc_timing *timing)
{
	uint32_t req_bw;
	uint32_t max_bw;

	const struct dc_link_settings *link_setting;

	/*always DP fail safe mode*/
	if (timing->pix_clk_khz == (uint32_t)25175 &&
		timing->h_addressable == (uint32_t)640 &&
		timing->v_addressable == (uint32_t)480)
		return true;

	/* We always use verified link settings */
	link_setting = &link->public.verified_link_cap;

	/* TODO: DYNAMIC_VALIDATION needs to be implemented */
	/*if (flags.DYNAMIC_VALIDATION == 1 &&
		link->public.verified_link_cap.lane_count != LANE_COUNT_UNKNOWN)
		link_setting = &link->public.verified_link_cap;
	*/

	req_bw = bandwidth_in_kbps_from_timing(timing);
	max_bw = bandwidth_in_kbps_from_link_settings(link_setting);

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

void decide_link_settings(struct core_stream *stream,
	struct dc_link_settings *link_setting)
{

	const struct dc_link_settings *cur_ls;
	struct core_link* link;
	uint32_t req_bw;
	uint32_t link_bw;
	uint32_t i;

	req_bw = bandwidth_in_kbps_from_timing(
			&stream->public.timing);

	/* if preferred is specified through AMDDP, use it, if it's enough
	 * to drive the mode
	 */
	link = stream->sink->link;

	if ((link->public.reported_link_cap.lane_count != LANE_COUNT_UNKNOWN) &&
		(link->public.reported_link_cap.link_rate <=
				link->public.verified_link_cap.link_rate)) {

		link_bw = bandwidth_in_kbps_from_link_settings(
				&link->public.reported_link_cap);

		if (req_bw < link_bw) {
			*link_setting = link->public.reported_link_cap;
			return;
		}
	}

	/* search for first suitable setting for the requested
	 * bandwidth
	 */
	for (i = 0; i < get_link_training_fallback_table_len(link); i++) {

		cur_ls = get_link_training_fallback_table(link, i);

		link_bw =
				bandwidth_in_kbps_from_link_settings(
				cur_ls);

		if (req_bw < link_bw) {
			if (is_link_setting_supported(
				cur_ls,
				&link->public.max_link_setting)) {
				*link_setting = *cur_ls;
				return;
			}
		}
	}

	BREAK_TO_DEBUGGER();
	ASSERT(link->public.verified_link_cap.lane_count !=
		LANE_COUNT_UNKNOWN);

	*link_setting = link->public.verified_link_cap;
}

/*************************Short Pulse IRQ***************************/

static bool hpd_rx_irq_check_link_loss_status(
	struct core_link *link,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	uint8_t irq_reg_rx_power_state;
	enum dc_status dpcd_result = DC_ERROR_UNEXPECTED;
	union lane_status lane_status;
	uint32_t lane;
	bool sink_status_changed;
	bool return_code;

	sink_status_changed = false;
	return_code = false;

	if (link->public.cur_link_settings.lane_count == 0)
		return return_code;
	/*1. Check that we can handle interrupt: Not in FS DOS,
	 *  Not in "Display Timeout" state, Link is trained.
	 */

	dpcd_result = core_link_read_dpcd(link,
		DP_SET_POWER,
		&irq_reg_rx_power_state,
		sizeof(irq_reg_rx_power_state));

	if (dpcd_result != DC_OK) {
		irq_reg_rx_power_state = DP_SET_POWER_D0;
		dm_logger_write(link->ctx->logger, LOG_HW_HPD_IRQ,
			"%s: DPCD read failed to obtain power state.\n",
			__func__);
	}

	if (irq_reg_rx_power_state == DP_SET_POWER_D0) {

		/*2. Check that Link Status changed, before re-training.*/

		/*parse lane status*/
		for (lane = 0;
			lane < link->public.cur_link_settings.lane_count;
			lane++) {

			/* check status of lanes 0,1
			 * changed DpcdAddress_Lane01Status (0x202)*/
			lane_status.raw = get_nibble_at_index(
				&hpd_irq_dpcd_data->bytes.lane01_status.raw,
				lane);

			if (!lane_status.bits.CHANNEL_EQ_DONE_0 ||
				!lane_status.bits.CR_DONE_0 ||
				!lane_status.bits.SYMBOL_LOCKED_0) {
				/* if one of the channel equalization, clock
				 * recovery or symbol lock is dropped
				 * consider it as (link has been
				 * dropped) dp sink status has changed*/
				sink_status_changed = true;
				break;
			}

		}

		/* Check interlane align.*/
		if (sink_status_changed ||
			!hpd_irq_dpcd_data->bytes.lane_status_updated.bits.
			INTERLANE_ALIGN_DONE) {

			dm_logger_write(link->ctx->logger, LOG_HW_HPD_IRQ,
				"%s: Link Status changed.\n",
				__func__);

			return_code = true;
		}
	}

	return return_code;
}

static enum dc_status read_hpd_rx_irq_data(
	struct core_link *link,
	union hpd_irq_data *irq_data)
{
	/* The HW reads 16 bytes from 200h on HPD,
	 * but if we get an AUX_DEFER, the HW cannot retry
	 * and this causes the CTS tests 4.3.2.1 - 3.2.4 to
	 * fail, so we now explicitly read 6 bytes which is
	 * the req from the above mentioned test cases.
	 */
	return core_link_read_dpcd(
	link,
	DP_SINK_COUNT,
	irq_data->raw,
	sizeof(union hpd_irq_data));
}

static bool allow_hpd_rx_irq(const struct core_link *link)
{
	/*
	 * Don't handle RX IRQ unless one of following is met:
	 * 1) The link is established (cur_link_settings != unknown)
	 * 2) We kicked off MST detection
	 * 3) We know we're dealing with an active dongle
	 */

	if ((link->public.cur_link_settings.lane_count != LANE_COUNT_UNKNOWN) ||
		(link->public.type == dc_connection_mst_branch) ||
		is_dp_active_dongle(link))
		return true;

	return false;
}

static bool handle_hpd_irq_psr_sink(const struct core_link *link)
{
	union dpcd_psr_configuration psr_configuration;

	if (!link->psr_enabled)
		return false;

	dm_helpers_dp_read_dpcd(
		link->ctx,
		&link->public,
		368,/*DpcdAddress_PSR_Enable_Cfg*/
		&psr_configuration.raw,
		sizeof(psr_configuration.raw));


	if (psr_configuration.bits.ENABLE) {
		unsigned char dpcdbuf[3] = {0};
		union psr_error_status psr_error_status;
		union psr_sink_psr_status psr_sink_psr_status;

		dm_helpers_dp_read_dpcd(
			link->ctx,
			&link->public,
			0x2006, /*DpcdAddress_PSR_Error_Status*/
			(unsigned char *) dpcdbuf,
			sizeof(dpcdbuf));

		/*DPCD 2006h   ERROR STATUS*/
		psr_error_status.raw = dpcdbuf[0];
		/*DPCD 2008h   SINK PANEL SELF REFRESH STATUS*/
		psr_sink_psr_status.raw = dpcdbuf[2];

		if (psr_error_status.bits.LINK_CRC_ERROR ||
				psr_error_status.bits.RFB_STORAGE_ERROR) {
			/* Acknowledge and clear error bits */
			dm_helpers_dp_write_dpcd(
				link->ctx,
				&link->public,
				8198,/*DpcdAddress_PSR_Error_Status*/
				&psr_error_status.raw,
				sizeof(psr_error_status.raw));

			/* PSR error, disable and re-enable PSR */
			dc_link_set_psr_enable(&link->public, false);
			dc_link_set_psr_enable(&link->public, true);

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

static void dp_test_send_link_training(struct core_link *link)
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
	link->public.verified_link_cap.lane_count = link_settings.lane_count;
	link->public.verified_link_cap.link_rate = link_settings.link_rate;

	dp_retrain_link_dp_test(link, &link_settings, false);
}

static void dp_test_send_phy_test_pattern(struct core_link *link)
{
	union phy_test_pattern dpcd_test_pattern;
	union lane_adjust dpcd_lane_adjustment[2];
	unsigned char dpcd_post_cursor_2_adjustment = 0;
	unsigned char test_80_bit_pattern[
			(DP_TEST_80BIT_CUSTOM_PATTERN_79_72 -
			DP_TEST_80BIT_CUSTOM_PATTERN_7_0)+1] = {0};
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
			DP_TEST_PHY_PATTERN,
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
		test_pattern = DP_TEST_PATTERN_CP2520_1;
		break;
	case PHY_TEST_PATTERN_CP2520_2:
		test_pattern = DP_TEST_PATTERN_CP2520_2;
		break;
	case PHY_TEST_PATTERN_CP2520_3:
		test_pattern = DP_TEST_PATTERN_CP2520_3;
		break;
	default:
		test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
	break;
	}

	if (test_pattern == DP_TEST_PATTERN_80BIT_CUSTOM)
		core_link_read_dpcd(
				link,
				DP_TEST_80BIT_CUSTOM_PATTERN_7_0,
				test_80_bit_pattern,
				sizeof(test_80_bit_pattern));

	/* prepare link training settings */
	link_settings.link = link->public.cur_link_settings;

	for (lane = 0; lane <
		(unsigned int)(link->public.cur_link_settings.lane_count);
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
		&link->public,
		test_pattern,
		&link_training_settings,
		test_80_bit_pattern,
		(DP_TEST_80BIT_CUSTOM_PATTERN_79_72 -
		DP_TEST_80BIT_CUSTOM_PATTERN_7_0)+1);
}

static void dp_test_send_link_test_pattern(struct core_link *link)
{
	union link_test_pattern dpcd_test_pattern;
	union test_misc dpcd_test_params;
	enum dp_test_pattern test_pattern;

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

	dc_link_dp_set_test_pattern(
			&link->public,
			test_pattern,
			NULL,
			NULL,
			0);
}

static void handle_automated_test(struct core_link *link)
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
	if (test_request.bits.PHY_TEST_PATTERN) {
		dp_test_send_phy_test_pattern(link);
		test_response.bits.ACK = 1;
	}
	if (!test_request.raw)
		/* no requests, revert all test signals
		 * TODO: revert all test signals
		 */
		test_response.bits.ACK = 1;
	/* send request acknowledgment */
	if (test_response.bits.ACK)
		core_link_write_dpcd(
			link,
			DP_TEST_RESPONSE,
			&test_response.raw,
			sizeof(test_response));
}

bool dc_link_handle_hpd_rx_irq(const struct dc_link *dc_link)
{
	struct core_link *link = DC_LINK_TO_LINK(dc_link);
	union hpd_irq_data hpd_irq_dpcd_data = {{{{0}}}};
	union device_service_irq device_service_clear = { { 0 } };
	enum dc_status result = DDC_RESULT_UNKNOWN;
	bool status = false;
	/* For use cases related to down stream connection status change,
	 * PSR and device auto test, refer to function handle_sst_hpd_irq
	 * in DAL2.1*/

	dm_logger_write(link->ctx->logger, LOG_HW_HPD_IRQ,
		"%s: Got short pulse HPD on link %d\n",
		__func__, link->public.link_index);

	 /* All the "handle_hpd_irq_xxx()" methods
		 * should be called only after
		 * dal_dpsst_ls_read_hpd_irq_data
		 * Order of calls is important too
		 */
	result = read_hpd_rx_irq_data(link, &hpd_irq_dpcd_data);

	if (result != DC_OK) {
		dm_logger_write(link->ctx->logger, LOG_HW_HPD_IRQ,
			"%s: DPCD read failed to obtain irq data\n",
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
		dm_logger_write(link->ctx->logger, LOG_HW_HPD_IRQ,
			"%s: skipping HPD handling on %d\n",
			__func__, link->public.link_index);
		return false;
	}

	if (handle_hpd_irq_psr_sink(link))
		/* PSR-related error was detected and handled */
		return true;

	/* If PSR-related error handled, Main link may be off,
	 * so do not handle as a normal sink status change interrupt.
	 */

	/* check if we have MST msg and return since we poll for it */
	if (hpd_irq_dpcd_data.bytes.device_service_irq.
			bits.DOWN_REP_MSG_RDY ||
		hpd_irq_dpcd_data.bytes.device_service_irq.
			bits.UP_REQ_MSG_RDY)
		return false;

	/* For now we only handle 'Downstream port status' case.
	 * If we got sink count changed it means
	 * Downstream port status changed,
	 * then DM should call DC to do the detection. */
	if (hpd_rx_irq_check_link_loss_status(
		link,
		&hpd_irq_dpcd_data)) {
		/* Connectivity log: link loss */
		CONN_DATA_LINK_LOSS(link,
					hpd_irq_dpcd_data.raw,
					sizeof(hpd_irq_dpcd_data),
					"Status: ");

		perform_link_training_with_retries(link,
			&link->public.cur_link_settings,
			true, LINK_TRAINING_ATTEMPTS);

		status = false;
	}

	if (link->public.type == dc_connection_active_dongle &&
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
bool is_mst_supported(struct core_link *link)
{
	bool mst          = false;
	enum dc_status st = DC_OK;
	union dpcd_rev rev;
	union mstm_cap cap;

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

bool is_dp_active_dongle(const struct core_link *link)
{
	enum display_dongle_type dongle_type = link->dpcd_caps.dongle_type;

	return (dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER) ||
			(dongle_type == DISPLAY_DONGLE_DP_DVI_CONVERTER) ||
			(dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER);
}

static void get_active_converter_info(
	uint8_t data, struct core_link *link)
{
	union dp_downstream_port_present ds_port = { .byte = data };

	/* decode converter info*/
	if (!ds_port.fields.PORT_PRESENT) {
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
		ddc_service_set_dongle_type(link->ddc,
				link->dpcd_caps.dongle_type);
		return;
	}

	switch (ds_port.fields.PORT_TYPE) {
	case DOWNSTREAM_VGA:
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_VGA_CONVERTER;
		break;
	case DOWNSTREAM_DVI_HDMI:
		/* At this point we don't know is it DVI or HDMI,
		 * assume DVI.*/
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_DVI_CONVERTER;
		break;
	default:
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
		break;
	}

	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_11) {
		uint8_t det_caps[4];
		union dwnstream_port_caps_byte0 *port_caps =
			(union dwnstream_port_caps_byte0 *)det_caps;
		core_link_read_dpcd(link, DP_DOWNSTREAM_PORT_0,
				det_caps, sizeof(det_caps));

		switch (port_caps->bits.DWN_STRM_PORTX_TYPE) {
		case DOWN_STREAM_DETAILED_VGA:
			link->dpcd_caps.dongle_type =
				DISPLAY_DONGLE_DP_VGA_CONVERTER;
			break;
		case DOWN_STREAM_DETAILED_DVI:
			link->dpcd_caps.dongle_type =
				DISPLAY_DONGLE_DP_DVI_CONVERTER;
			break;
		case DOWN_STREAM_DETAILED_HDMI:
			link->dpcd_caps.dongle_type =
				DISPLAY_DONGLE_DP_HDMI_CONVERTER;

			link->dpcd_caps.dongle_caps.dongle_type = link->dpcd_caps.dongle_type;
			if (ds_port.fields.DETAILED_CAPS) {

				union dwnstream_port_caps_byte3_hdmi
					hdmi_caps = {.raw = det_caps[3] };
				union dwnstream_port_caps_byte1
					hdmi_color_caps = {.raw = det_caps[2] };
				link->dpcd_caps.dongle_caps.dp_hdmi_max_pixel_clk =
					det_caps[1] * 25000;

				link->dpcd_caps.dongle_caps.is_dp_hdmi_s3d_converter =
					hdmi_caps.bits.FRAME_SEQ_TO_FRAME_PACK;
				link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr422_pass_through =
					hdmi_caps.bits.YCrCr422_PASS_THROUGH;
				link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr420_pass_through =
					hdmi_caps.bits.YCrCr420_PASS_THROUGH;
				link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr422_converter =
					hdmi_caps.bits.YCrCr422_CONVERSION;
				link->dpcd_caps.dongle_caps.is_dp_hdmi_ycbcr420_converter =
					hdmi_caps.bits.YCrCr420_CONVERSION;

				link->dpcd_caps.dongle_caps.dp_hdmi_max_bpc =
					hdmi_color_caps.bits.MAX_BITS_PER_COLOR_COMPONENT;

				link->dpcd_caps.dongle_caps.extendedCapValid = true;
			}

			break;
		}
	}

	ddc_service_set_dongle_type(link->ddc, link->dpcd_caps.dongle_type);

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

	{
		struct dp_sink_hw_fw_revision dp_hw_fw_revision;

		core_link_read_dpcd(
			link,
			DP_BRANCH_REVISION_START,
			(uint8_t *)&dp_hw_fw_revision,
			sizeof(dp_hw_fw_revision));

		link->dpcd_caps.branch_hw_revision =
			dp_hw_fw_revision.ieee_hw_rev;
	}
}

static void dp_wa_power_up_0010FA(struct core_link *link, uint8_t *dpcd_data,
		int length)
{
	int retry = 0;
	union dp_downstream_port_present ds_port = { 0 };

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

	ds_port.byte = dpcd_data[DP_DOWNSTREAMPORT_PRESENT -
				 DP_DPCD_REV];

	if (link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER) {
		switch (link->dpcd_caps.branch_dev_id) {
		/* Some active dongles (DP-VGA, DP-DLDVI converters) power down
		 * all internal circuits including AUX communication preventing
		 * reading DPCD table and EDID (spec violation).
		 * Encoder will skip DP RX power down on disable_output to
		 * keep receiver powered all the time.*/
		case DP_BRANCH_DEVICE_ID_1:
		case DP_BRANCH_DEVICE_ID_4:
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

static void retrieve_link_cap(struct core_link *link)
{
	uint8_t dpcd_data[DP_TRAINING_AUX_RD_INTERVAL - DP_DPCD_REV + 1];

	union down_stream_port_count down_strm_port_count;
	union edp_configuration_cap edp_config_cap;
	union dp_downstream_port_present ds_port = { 0 };

	memset(dpcd_data, '\0', sizeof(dpcd_data));
	memset(&down_strm_port_count,
		'\0', sizeof(union down_stream_port_count));
	memset(&edp_config_cap, '\0',
		sizeof(union edp_configuration_cap));

	core_link_read_dpcd(
		link,
		DP_DPCD_REV,
		dpcd_data,
		sizeof(dpcd_data));

	{
		union training_aux_rd_interval aux_rd_interval;

		aux_rd_interval.raw =
			dpcd_data[DP_TRAINING_AUX_RD_INTERVAL];

		if (aux_rd_interval.bits.EXT_RECIEVER_CAP_FIELD_PRESENT == 1) {
			core_link_read_dpcd(
				link,
				DP_DP13_DPCD_REV,
				dpcd_data,
				sizeof(dpcd_data));
		}
	}

	link->dpcd_caps.dpcd_rev.raw =
		dpcd_data[DP_DPCD_REV - DP_DPCD_REV];

	ds_port.byte = dpcd_data[DP_DOWNSTREAMPORT_PRESENT -
				 DP_DPCD_REV];

	get_active_converter_info(ds_port.byte, link);

	dp_wa_power_up_0010FA(link, dpcd_data, sizeof(dpcd_data));

	link->dpcd_caps.allow_invalid_MSA_timing_param =
		down_strm_port_count.bits.IGNORE_MSA_TIMING_PARAM;

	link->dpcd_caps.max_ln_count.raw = dpcd_data[
		DP_MAX_LANE_COUNT - DP_DPCD_REV];

	link->dpcd_caps.max_down_spread.raw = dpcd_data[
		DP_MAX_DOWNSPREAD - DP_DPCD_REV];

	link->public.reported_link_cap.lane_count =
		link->dpcd_caps.max_ln_count.bits.MAX_LANE_COUNT;
	link->public.reported_link_cap.link_rate = dpcd_data[
		DP_MAX_LINK_RATE - DP_DPCD_REV];
	link->public.reported_link_cap.link_spread =
		link->dpcd_caps.max_down_spread.bits.MAX_DOWN_SPREAD ?
		LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	edp_config_cap.raw = dpcd_data[
		DP_EDP_CONFIGURATION_CAP - DP_DPCD_REV];
	link->dpcd_caps.panel_mode_edp =
		edp_config_cap.bits.ALT_SCRAMBLER_RESET;

	link->public.test_pattern_enabled = false;
	link->public.compliance_test_state.raw = 0;

	/* read sink count */
	core_link_read_dpcd(link,
			DP_SINK_COUNT,
			&link->dpcd_caps.sink_count.raw,
			sizeof(link->dpcd_caps.sink_count.raw));

	/* Connectivity log: detection */
	CONN_DATA_DETECT(link, dpcd_data, sizeof(dpcd_data), "Rx Caps: ");
}

void detect_dp_sink_caps(struct core_link *link)
{
	retrieve_link_cap(link);

	/* dc init_hw has power encoder using default
	 * signal for connector. For native DP, no
	 * need to power up encoder again. If not native
	 * DP, hw_init may need check signal or power up
	 * encoder here.
	 */

	if (is_mst_supported(link)) {
		link->public.verified_link_cap = link->public.reported_link_cap;
	} else {
		dp_hbr_verify_link_cap(link,
			&link->public.reported_link_cap);
	}
	/* TODO save sink caps in link->sink */
}

void dc_link_dp_enable_hpd(const struct dc_link *link)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);
	struct link_encoder *encoder = core_link->link_enc;

	if (encoder != NULL && encoder->funcs->enable_hpd != NULL)
		encoder->funcs->enable_hpd(encoder);
}

void dc_link_dp_disable_hpd(const struct dc_link *link)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);
	struct link_encoder *encoder = core_link->link_enc;

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

static void set_crtc_test_pattern(struct core_link *link,
				struct pipe_ctx *pipe_ctx,
				enum dp_test_pattern test_pattern)
{
	enum controller_dp_test_pattern controller_test_pattern;
	enum dc_color_depth color_depth = pipe_ctx->
		stream->public.timing.display_color_depth;
	struct bit_depth_reduction_params params;

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
		pipe_ctx->opp->funcs->
			opp_program_bit_depth_reduction(pipe_ctx->opp, &params);

		pipe_ctx->tg->funcs->set_test_pattern(pipe_ctx->tg,
				controller_test_pattern, color_depth);
	}
	break;
	case DP_TEST_PATTERN_VIDEO_MODE:
	{
		/* restore bitdepth reduction */
		resource_build_bit_depth_reduction_params(pipe_ctx->stream,
					&params);
		pipe_ctx->stream->bit_depth_params = params;
		pipe_ctx->opp->funcs->
			opp_program_bit_depth_reduction(pipe_ctx->opp, &params);

		pipe_ctx->tg->funcs->set_test_pattern(pipe_ctx->tg,
				CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
				color_depth);
	}
	break;

	default:
	break;
	}
}

bool dc_link_dp_set_test_pattern(
	const struct dc_link *link,
	enum dp_test_pattern test_pattern,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size)
{
	struct core_link *core_link = DC_LINK_TO_CORE(link);
	struct pipe_ctx *pipes =
			core_link->dc->current_context->res_ctx.pipe_ctx;
	struct pipe_ctx pipe_ctx = pipes[0];
	unsigned int lane;
	unsigned int i;
	unsigned char link_qual_pattern[LANE_COUNT_DP_MAX] = {0};
	union dpcd_training_pattern training_pattern;
	enum dpcd_phy_test_patterns pattern;

	memset(&training_pattern, 0, sizeof(training_pattern));

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream->sink->link == core_link) {
			pipe_ctx = pipes[i];
			break;
		}
	}

	/* Reset CRTC Test Pattern if it is currently running and request
	 * is VideoMode Reset DP Phy Test Pattern if it is currently running
	 * and request is VideoMode
	 */
	if (core_link->public.test_pattern_enabled && test_pattern ==
			DP_TEST_PATTERN_VIDEO_MODE) {
		/* Set CRTC Test Pattern */
		set_crtc_test_pattern(core_link, &pipe_ctx, test_pattern);
		dp_set_hw_test_pattern(core_link, test_pattern,
				(uint8_t *)p_custom_pattern,
				(uint32_t)cust_pattern_size);

		/* Unblank Stream */
		core_link->dc->hwss.unblank_stream(
			&pipe_ctx,
			&core_link->public.verified_link_cap);
		/* TODO:m_pHwss->MuteAudioEndpoint
		 * (pPathMode->pDisplayPath, false);
		 */

		/* Reset Test Pattern state */
		core_link->public.test_pattern_enabled = false;

		return true;
	}

	/* Check for PHY Test Patterns */
	if (is_dp_phy_pattern(test_pattern)) {
		/* Set DPCD Lane Settings before running test pattern */
		if (p_link_settings != NULL) {
			dp_set_hw_lane_settings(core_link, p_link_settings);
			dpcd_set_lane_settings(core_link, p_link_settings);
		}

		/* Blank stream if running test pattern */
		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/*TODO:
			 * m_pHwss->
			 * MuteAudioEndpoint(pPathMode->pDisplayPath, true);
			 */
			/* Blank stream */
			pipes->stream_enc->funcs->dp_blank(pipe_ctx.stream_enc);
		}

		dp_set_hw_test_pattern(core_link, test_pattern,
				(uint8_t *)p_custom_pattern,
				(uint32_t)cust_pattern_size);

		if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
			/* Set Test Pattern state */
			core_link->public.test_pattern_enabled = true;
			if (p_link_settings != NULL)
				dpcd_set_link_settings(core_link,
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

		if (core_link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_12) {
			/* tell receiver that we are sending qualification
			 * pattern DP 1.2 or later - DP receiver's link quality
			 * pattern is set using DPCD LINK_QUAL_LANEx_SET
			 * register (0x10B~0x10E)\
			 */
			for (lane = 0; lane < LANE_COUNT_DP_MAX; lane++)
				link_qual_pattern[lane] =
						(unsigned char)(pattern);

			core_link_write_dpcd(core_link,
					DP_LINK_QUAL_LANE0_SET,
					link_qual_pattern,
					sizeof(link_qual_pattern));
		} else if (core_link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_10 ||
				core_link->dpcd_caps.dpcd_rev.raw == 0) {
			/* tell receiver that we are sending qualification
			 * pattern DP 1.1a or earlier - DP receiver's link
			 * quality pattern is set using
			 * DPCD TRAINING_PATTERN_SET -> LINK_QUAL_PATTERN_SET
			 * register (0x102). We will use v_1.3 when we are
			 * setting test pattern for DP 1.1.
			 */
			core_link_read_dpcd(core_link,
					DP_TRAINING_PATTERN_SET,
					&training_pattern.raw,
					sizeof(training_pattern));
			training_pattern.v1_3.LINK_QUAL_PATTERN_SET = pattern;
			core_link_write_dpcd(core_link,
					DP_TRAINING_PATTERN_SET,
					&training_pattern.raw,
					sizeof(training_pattern));
		}
	} else {
	/* CRTC Patterns */
		set_crtc_test_pattern(core_link, &pipe_ctx, test_pattern);
		/* Set Test Pattern state */
		core_link->public.test_pattern_enabled = true;
	}

	return true;
}

void dp_enable_mst_on_sink(struct core_link *link, bool enable)
{
	unsigned char mstmCntl;

	core_link_read_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
	if (enable)
		mstmCntl |= DP_MST_EN;
	else
		mstmCntl &= (~DP_MST_EN);

	core_link_write_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
}
