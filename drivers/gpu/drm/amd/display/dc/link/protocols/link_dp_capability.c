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
 * This file implements dp specific link capability retrieval sequence. It is
 * responsible for retrieving, parsing, overriding, deciding capability obtained
 * from dp link. Link capability consists of encoders, DPRXs, cables, retimers,
 * usb and all other possible backend capabilities. Other components should
 * include this header file in order to access link capability. Accessing link
 * capability by dereferencing dc_link outside dp_link_capability is not a
 * recommended method as it makes the component dependent on the underlying data
 * structure used to represent link capability instead of function interfaces.
 */

#include "link_dp_capability.h"
#include "link_ddc.h"
#include "link_dpcd.h"
#include "link_dp_dpia.h"
#include "link_dp_phy.h"
#include "link_edp_panel_control.h"
#include "link_dp_irq_handler.h"
#include "link/accessories/link_dp_trace.h"
#include "link/link_detection.h"
#include "link/link_validation.h"
#include "link_dp_training.h"
#include "atomfirmware.h"
#include "resource.h"
#include "link_enc_cfg.h"
#include "dc_dmub_srv.h"
#include "gpio_service_interface.h"

#define DC_LOGGER \
	link->ctx->logger
#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */

#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif
#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

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

uint8_t dp_parse_lttpr_repeater_count(uint8_t lttpr_repeater_count)
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

uint32_t link_bw_kbps_from_raw_frl_link_rate_data(uint8_t bw)
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

static enum dc_link_rate linkRateInKHzToLinkRateMultiplier(uint32_t link_rate_in_khz)
{
	enum dc_link_rate link_rate;
	// LinkRate is normally stored as a multiplier of 0.27 Gbps per lane. Do the translation.
	switch (link_rate_in_khz) {
	case 1620000:
		link_rate = LINK_RATE_LOW;	// Rate_1 (RBR)	- 1.62 Gbps/Lane
		break;
	case 2160000:
		link_rate = LINK_RATE_RATE_2;	// Rate_2	- 2.16 Gbps/Lane
		break;
	case 2430000:
		link_rate = LINK_RATE_RATE_3;	// Rate_3	- 2.43 Gbps/Lane
		break;
	case 2700000:
		link_rate = LINK_RATE_HIGH;	// Rate_4 (HBR)	- 2.70 Gbps/Lane
		break;
	case 3240000:
		link_rate = LINK_RATE_RBR2;	// Rate_5 (RBR2)- 3.24 Gbps/Lane
		break;
	case 4320000:
		link_rate = LINK_RATE_RATE_6;	// Rate_6	- 4.32 Gbps/Lane
		break;
	case 5400000:
		link_rate = LINK_RATE_HIGH2;	// Rate_7 (HBR2)- 5.40 Gbps/Lane
		break;
	case 6750000:
		link_rate = LINK_RATE_RATE_8;	// Rate_8	- 6.75 Gbps/Lane
		break;
	case 8100000:
		link_rate = LINK_RATE_HIGH3;	// Rate_9 (HBR3)- 8.10 Gbps/Lane
		break;
	default:
		link_rate = LINK_RATE_UNKNOWN;
		break;
	}
	return link_rate;
}

static union dp_cable_id intersect_cable_id(
		union dp_cable_id *a, union dp_cable_id *b)
{
	union dp_cable_id out;

	out.bits.UHBR10_20_CAPABILITY = MIN(a->bits.UHBR10_20_CAPABILITY,
			b->bits.UHBR10_20_CAPABILITY);
	out.bits.UHBR13_5_CAPABILITY = MIN(a->bits.UHBR13_5_CAPABILITY,
			b->bits.UHBR13_5_CAPABILITY);
	out.bits.CABLE_TYPE = MAX(a->bits.CABLE_TYPE, b->bits.CABLE_TYPE);

	return out;
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

static void dp_wa_power_up_0010FA(struct dc_link *link, uint8_t *dpcd_data,
		int length)
{
	int retry = 0;

	if (!link->dpcd_caps.dpcd_rev.raw) {
		do {
			dpcd_write_rx_power_ctrl(link, true);
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

bool dp_is_fec_supported(const struct dc_link *link)
{
	/* TODO - use asic cap instead of link_enc->features
	 * we no longer know which link enc to use for this link before commit
	 */
	struct link_encoder *link_enc = NULL;

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	return (dc_is_dp_signal(link->connector_signal) && link_enc &&
			link_enc->features.fec_supported &&
			link->dpcd_caps.fec_cap.bits.FEC_CAPABLE);
}

bool dp_should_enable_fec(const struct dc_link *link)
{
	bool force_disable = false;

	if (link->fec_state == dc_link_fec_enabled)
		force_disable = false;
	else if (link->connector_signal != SIGNAL_TYPE_DISPLAY_PORT_MST &&
			link->local_sink &&
			link->local_sink->edid_caps.panel_patch.disable_fec)
		force_disable = true;
	else if (link->connector_signal == SIGNAL_TYPE_EDP
			&& (link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.
			 dsc_support.DSC_SUPPORT == false
				|| link->panel_config.dsc.disable_dsc_edp
				|| !link->dc->caps.edp_dsc_support))
		force_disable = true;

	return !force_disable && dp_is_fec_supported(link);
}

bool dp_is_128b_132b_signal(struct pipe_ctx *pipe_ctx)
{
	/* If this assert is hit then we have a link encoder dynamic management issue */
	ASSERT(pipe_ctx->stream_res.hpo_dp_stream_enc ? pipe_ctx->link_res.hpo_dp_link_enc != NULL : true);
	return (pipe_ctx->stream_res.hpo_dp_stream_enc &&
			pipe_ctx->link_res.hpo_dp_link_enc &&
			dc_is_dp_signal(pipe_ctx->stream->signal));
}

bool dp_is_lttpr_present(struct dc_link *link)
{
	return (dp_parse_lttpr_repeater_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) != 0 &&
			link->dpcd_caps.lttpr_caps.max_lane_count > 0 &&
			link->dpcd_caps.lttpr_caps.max_lane_count <= 4 &&
			link->dpcd_caps.lttpr_caps.revision.raw >= 0x14);
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
	enum dc_link_rate cable_max_link_rate = LINK_RATE_UNKNOWN;

	if (link->dpcd_caps.cable_id.bits.UHBR10_20_CAPABILITY & DP_UHBR20) {
		cable_max_link_rate = LINK_RATE_UHBR20;
	} else if (link->dpcd_caps.cable_id.bits.UHBR13_5_CAPABILITY) {
		cable_max_link_rate = LINK_RATE_UHBR13_5;
	} else if (link->dpcd_caps.cable_id.bits.UHBR10_20_CAPABILITY & DP_UHBR10) {
		// allow DP40 cables to do UHBR13.5 for passive or unknown cable type
		if (link->dpcd_caps.cable_id.bits.CABLE_TYPE < 2) {
			cable_max_link_rate = LINK_RATE_UHBR13_5;
		} else {
			cable_max_link_rate = LINK_RATE_UHBR10;
		}
	}

	return cable_max_link_rate;
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

static enum dc_link_rate reduce_link_rate(const struct dc_link *link, enum dc_link_rate link_rate)
{
	// NEEDSWORK: provide some details about why this function never returns some of the
	// obscure link rates such as 4.32 Gbps or 3.24 Gbps and if such behavior is intended.
	//

	switch (link_rate) {
	case LINK_RATE_UHBR20:
		return LINK_RATE_UHBR13_5;
	case LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR10;
	case LINK_RATE_UHBR10:
		return LINK_RATE_HIGH3;
	case LINK_RATE_HIGH3:
		if (link->connector_signal == SIGNAL_TYPE_EDP && link->dc->debug.support_eDP1_5)
			return LINK_RATE_RATE_8;
		return LINK_RATE_HIGH2;
	case LINK_RATE_RATE_8:
		return LINK_RATE_HIGH2;
	case LINK_RATE_HIGH2:
		return LINK_RATE_HIGH;
	case LINK_RATE_RATE_6:
	case LINK_RATE_RBR2:
		return LINK_RATE_HIGH;
	case LINK_RATE_HIGH:
		return LINK_RATE_LOW;
	case LINK_RATE_RATE_3:
	case LINK_RATE_RATE_2:
		return LINK_RATE_LOW;
	case LINK_RATE_LOW:
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

static enum dc_link_rate increase_link_rate(struct dc_link *link,
		enum dc_link_rate link_rate)
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
		/* upto DP2.x specs UHBR13.5 is the only link rate that could be
		 * not supported by DPRX when higher link rate is supported.
		 * so we treat it as a special case for code simplicity. When we
		 * have new specs with more link rates like this, we should
		 * consider a more generic solution to handle discrete link
		 * rate capabilities.
		 */
		return link->dpcd_caps.dp_128b_132b_supported_link_rates.bits.UHBR13_5 ?
				LINK_RATE_UHBR13_5 : LINK_RATE_UHBR20;
	case LINK_RATE_UHBR13_5:
		return LINK_RATE_UHBR20;
	default:
		return LINK_RATE_UNKNOWN;
	}
}

static bool decide_fallback_link_setting_max_bw_policy(
		struct dc_link *link,
		const struct dc_link_settings *max,
		struct dc_link_settings *cur,
		enum link_training_result training_result)
{
	uint8_t cur_idx = 0, next_idx;
	bool found = false;

	if (training_result == LINK_TRAINING_ABORT)
		return false;

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
		if (dp_lt_fallbacks[next_idx].lane_count > max->lane_count ||
				dp_lt_fallbacks[next_idx].link_rate > max->link_rate)
			next_idx++;
		else if (dp_lt_fallbacks[next_idx].link_rate == LINK_RATE_UHBR13_5 &&
				link->dpcd_caps.dp_128b_132b_supported_link_rates.bits.UHBR13_5 == 0)
			/* upto DP2.x specs UHBR13.5 is the only link rate that
			 * could be not supported by DPRX when higher link rate
			 * is supported. so we treat it as a special case for
			 * code simplicity. When we have new specs with more
			 * link rates like this, we should consider a more
			 * generic solution to handle discrete link rate
			 * capabilities.
			 */
			next_idx++;
		else
			break;

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
bool decide_fallback_link_setting(
		struct dc_link *link,
		struct dc_link_settings *max,
		struct dc_link_settings *cur,
		enum link_training_result training_result)
{
	if (link_dp_get_encoding_format(max) == DP_128b_132b_ENCODING ||
			link->dc->debug.force_dp2_lt_fallback_method)
		return decide_fallback_link_setting_max_bw_policy(link, max,
				cur, training_result);

	switch (training_result) {
	case LINK_TRAINING_CR_FAIL_LANE0:
	case LINK_TRAINING_CR_FAIL_LANE1:
	case LINK_TRAINING_CR_FAIL_LANE23:
	case LINK_TRAINING_LQA_FAIL:
	{
		if (!reached_minimum_link_rate(cur->link_rate)) {
			cur->link_rate = reduce_link_rate(link, cur->link_rate);
		} else if (!reached_minimum_lane_count(cur->lane_count)) {
			cur->link_rate = max->link_rate;
			if (training_result == LINK_TRAINING_CR_FAIL_LANE0)
				return false;
			else if (training_result == LINK_TRAINING_CR_FAIL_LANE1)
				cur->lane_count = LANE_COUNT_ONE;
			else if (training_result == LINK_TRAINING_CR_FAIL_LANE23)
				cur->lane_count = LANE_COUNT_TWO;
			else
				cur->lane_count = reduce_lane_count(cur->lane_count);
		} else {
			return false;
		}
		break;
	}
	case LINK_TRAINING_EQ_FAIL_EQ:
	case LINK_TRAINING_EQ_FAIL_CR_PARTIAL:
	{
		if (!reached_minimum_lane_count(cur->lane_count)) {
			cur->lane_count = reduce_lane_count(cur->lane_count);
		} else if (!reached_minimum_link_rate(cur->link_rate)) {
			cur->link_rate = reduce_link_rate(link, cur->link_rate);
			/* Reduce max link rate to avoid potential infinite loop.
			 * Needed so that any subsequent CR_FAIL fallback can't
			 * re-set the link rate higher than the link rate from
			 * the latest EQ_FAIL fallback.
			 */
			max->link_rate = cur->link_rate;
			cur->lane_count = max->lane_count;
		} else {
			return false;
		}
		break;
	}
	case LINK_TRAINING_EQ_FAIL_CR:
	{
		if (!reached_minimum_link_rate(cur->link_rate)) {
			cur->link_rate = reduce_link_rate(link, cur->link_rate);
			/* Reduce max link rate to avoid potential infinite loop.
			 * Needed so that any subsequent CR_FAIL fallback can't
			 * re-set the link rate higher than the link rate from
			 * the latest EQ_FAIL fallback.
			 */
			max->link_rate = cur->link_rate;
			cur->lane_count = max->lane_count;
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
static bool decide_dp_link_settings(struct dc_link *link, struct dc_link_settings *link_setting, uint32_t req_bw)
{
	struct dc_link_settings initial_link_setting = {
		LANE_COUNT_ONE, LINK_RATE_LOW, LINK_SPREAD_DISABLED, false, 0};
	struct dc_link_settings current_link_setting =
			initial_link_setting;
	uint32_t link_bw;

	if (req_bw > dp_link_bandwidth_kbps(link, &link->verified_link_cap))
		return false;

	/* search for the minimum link setting that:
	 * 1. is supported according to the link training result
	 * 2. could support the b/w requested by the timing
	 */
	while (current_link_setting.link_rate <=
			link->verified_link_cap.link_rate) {
		link_bw = dp_link_bandwidth_kbps(
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
					increase_link_rate(link,
							current_link_setting.link_rate);
			current_link_setting.lane_count =
					initial_link_setting.lane_count;
		}
	}

	return false;
}

bool edp_decide_link_settings(struct dc_link *link,
		struct dc_link_settings *link_setting, uint32_t req_bw)
{
	struct dc_link_settings initial_link_setting;
	struct dc_link_settings current_link_setting;
	uint32_t link_bw;

	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if (!edp_is_ilr_optimization_enabled(link)) {
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
		link_bw = dp_link_bandwidth_kbps(
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

bool decide_edp_link_settings_with_dsc(struct dc_link *link,
		struct dc_link_settings *link_setting,
		uint32_t req_bw,
		enum dc_link_rate max_link_rate)
{
	struct dc_link_settings initial_link_setting;
	struct dc_link_settings current_link_setting;
	uint32_t link_bw;

	unsigned int policy = 0;

	policy = link->panel_config.dsc.force_dsc_edp_policy;
	if (max_link_rate == LINK_RATE_UNKNOWN)
		max_link_rate = link->verified_link_cap.link_rate;
	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if (!edp_is_ilr_optimization_enabled(link)) {
		/* for DSC enabled case, we search for minimum lane count */
		memset(&initial_link_setting, 0, sizeof(initial_link_setting));
		initial_link_setting.lane_count = LANE_COUNT_ONE;
		initial_link_setting.link_rate = LINK_RATE_LOW;
		initial_link_setting.link_spread = LINK_SPREAD_DISABLED;
		initial_link_setting.use_link_rate_set = false;
		initial_link_setting.link_rate_set = 0;
		current_link_setting = initial_link_setting;
		if (req_bw > dp_link_bandwidth_kbps(link, &link->verified_link_cap))
			return false;

		/* search for the minimum link setting that:
		 * 1. is supported according to the link training result
		 * 2. could support the b/w requested by the timing
		 */
		while (current_link_setting.link_rate <=
				max_link_rate) {
			link_bw = dp_link_bandwidth_kbps(
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
							increase_link_rate(link,
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
							increase_link_rate(link,
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
		link_bw = dp_link_bandwidth_kbps(
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

bool link_decide_link_settings(struct dc_stream_state *stream,
	struct dc_link_settings *link_setting)
{
	struct dc_link *link = stream->link;
	uint32_t req_bw = dc_bandwidth_in_kbps_from_timing(&stream->timing, dc_link_get_highest_encoding_format(link));

	memset(link_setting, 0, sizeof(*link_setting));

	/* if preferred is specified through AMDDP, use it, if it's enough
	 * to drive the mode
	 */
	if (link->preferred_link_setting.lane_count !=
			LANE_COUNT_UNKNOWN &&
			link->preferred_link_setting.link_rate !=
					LINK_RATE_UNKNOWN) {
		*link_setting = link->preferred_link_setting;
		return true;
	}

	/* MST doesn't perform link training for now
	 * TODO: add MST specific link training routine
	 */
	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		decide_mst_link_settings(link, link_setting);
	} else if (link->connector_signal == SIGNAL_TYPE_EDP) {
		/* enable edp link optimization for DSC eDP case */
		if (stream->timing.flags.DSC) {
			enum dc_link_rate max_link_rate = LINK_RATE_UNKNOWN;

			if (link->panel_config.dsc.force_dsc_edp_policy) {
				/* calculate link max link rate cap*/
				struct dc_link_settings tmp_link_setting;
				struct dc_crtc_timing tmp_timing = stream->timing;
				uint32_t orig_req_bw;

				tmp_link_setting.link_rate = LINK_RATE_UNKNOWN;
				tmp_timing.flags.DSC = 0;
				orig_req_bw = dc_bandwidth_in_kbps_from_timing(&tmp_timing,
						dc_link_get_highest_encoding_format(link));
				edp_decide_link_settings(link, &tmp_link_setting, orig_req_bw);
				max_link_rate = tmp_link_setting.link_rate;
			}
			decide_edp_link_settings_with_dsc(link, link_setting, req_bw, max_link_rate);
		} else {
			edp_decide_link_settings(link, link_setting, req_bw);
		}
	} else {
		decide_dp_link_settings(link, link_setting, req_bw);
	}

	return link_setting->lane_count != LANE_COUNT_UNKNOWN &&
			link_setting->link_rate != LINK_RATE_UNKNOWN;
}

enum dp_link_encoding link_dp_get_encoding_format(const struct dc_link_settings *link_settings)
{
	if ((link_settings->link_rate >= LINK_RATE_LOW) &&
			(link_settings->link_rate <= LINK_RATE_HIGH3))
		return DP_8b_10b_ENCODING;
	else if ((link_settings->link_rate >= LINK_RATE_UHBR10) &&
			(link_settings->link_rate <= LINK_RATE_UHBR20))
		return DP_128b_132b_ENCODING;
	return DP_UNKNOWN_ENCODING;
}

enum dp_link_encoding mst_decide_link_encoding_format(const struct dc_link *link)
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

	return link_dp_get_encoding_format(&link_settings);
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

static enum dc_status wake_up_aux_channel(struct dc_link *link)
{
	enum dc_status status = DC_ERROR_UNEXPECTED;
	uint32_t aux_channel_retry_cnt = 0;
	uint8_t dpcd_power_state = '\0';

	while (status != DC_OK && aux_channel_retry_cnt < 10) {
		status = core_link_read_dpcd(link, DP_SET_POWER,
				&dpcd_power_state, sizeof(dpcd_power_state));

		/* Delay 1 ms if AUX CH is in power down state. Based on spec
		 * section 2.3.1.2, if AUX CH may be powered down due to
		 * write to DPCD 600h = 2. Sink AUX CH is monitoring differential
		 * signal and may need up to 1 ms before being able to reply.
		 */
		if (status != DC_OK || dpcd_power_state == DP_SET_POWER_D3) {
			fsleep(1000);
			aux_channel_retry_cnt++;
		}
	}

	if (status != DC_OK) {
		dpcd_power_state = DP_SET_POWER_D0;
		status = core_link_write_dpcd(
				link,
				DP_SET_POWER,
				&dpcd_power_state,
				sizeof(dpcd_power_state));

		dpcd_power_state = DP_SET_POWER_D3;
		status = core_link_write_dpcd(
				link,
				DP_SET_POWER,
				&dpcd_power_state,
				sizeof(dpcd_power_state));
		DC_LOG_DC("%s: Failed to power up sink\n", __func__);
		return DC_ERROR_UNEXPECTED;
	}

	return DC_OK;
}

static void get_active_converter_info(
	uint8_t data, struct dc_link *link)
{
	union dp_downstream_port_present ds_port = { .byte = data };
	memset(&link->dpcd_caps.dongle_caps, 0, sizeof(link->dpcd_caps.dongle_caps));

	/* decode converter info*/
	if (!ds_port.fields.PORT_PRESENT) {
		link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
		set_dongle_type(link->ddc,
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

					if (link->dc->caps.dp_hdmi21_pcon_support) {
						union hdmi_encoded_link_bw hdmi_encoded_link_bw;

						link->dpcd_caps.dongle_caps.dp_hdmi_frl_max_link_bw_in_kbps =
								link_bw_kbps_from_raw_frl_link_rate_data(
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

					if (link->dpcd_caps.dongle_caps.dp_hdmi_max_pixel_clk_in_khz != 0)
						link->dpcd_caps.dongle_caps.extendedCapValid = true;
				}

				break;
			}
		}
	}

	set_dongle_type(link->ddc, link->dpcd_caps.dongle_type);

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

			result_write_min_hblank = core_link_write_dpcd(link,
				DP_SOURCE_MINIMUM_HBLANK_SUPPORTED, (uint8_t *)(&hblank_size),
				sizeof(hblank_size));
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

void dpcd_write_cable_id_to_dprx(struct dc_link *link)
{
	if (!link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED ||
			link->dpcd_caps.cable_id.raw == 0 ||
			link->dprx_states.cable_id_written)
		return;

	core_link_write_dpcd(link, DP_CABLE_ATTRIBUTES_UPDATED_BY_DPTX,
			&link->dpcd_caps.cable_id.raw,
			sizeof(link->dpcd_caps.cable_id.raw));

	link->dprx_states.cable_id_written = 1;
}

static bool get_usbc_cable_id(struct dc_link *link, union dp_cable_id *cable_id)
{
	union dmub_rb_cmd cmd;

	if (!link->ctx->dmub_srv ||
			link->ep_type != DISPLAY_ENDPOINT_PHY ||
			link->link_enc->features.flags.bits.DP_IS_USB_C == 0)
		return false;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cable_id.header.type = DMUB_CMD_GET_USBC_CABLE_ID;
	cmd.cable_id.header.payload_bytes = sizeof(cmd.cable_id.data);
	cmd.cable_id.data.input.phy_inst = resource_transmitter_to_phy_idx(
			link->dc, link->link_enc->transmitter);
	if (dc_wake_and_execute_dmub_cmd(link->dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY) &&
			cmd.cable_id.header.ret_status == 1) {
		cable_id->raw = cmd.cable_id.data.output_raw;
		DC_LOG_DC("usbc_cable_id = %d.\n", cable_id->raw);
	}
	return cmd.cable_id.header.ret_status == 1;
}

static void retrieve_cable_id(struct dc_link *link)
{
	union dp_cable_id usbc_cable_id;

	link->dpcd_caps.cable_id.raw = 0;
	core_link_read_dpcd(link, DP_CABLE_ATTRIBUTES_UPDATED_BY_DPRX,
			&link->dpcd_caps.cable_id.raw, sizeof(uint8_t));

	if (get_usbc_cable_id(link, &usbc_cable_id))
		link->dpcd_caps.cable_id = intersect_cable_id(
				&link->dpcd_caps.cable_id, &usbc_cable_id);
}

bool read_is_mst_supported(struct dc_link *link)
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

/* Read additional sink caps defined in source specific DPCD area
 * This function currently only reads from SinkCapability address (DP_SOURCE_SINK_CAP)
 * TODO: Add FS caps and read from DP_SOURCE_SINK_FS_CAP as well
 */
static bool dpcd_read_sink_ext_caps(struct dc_link *link)
{
	uint8_t dpcd_data = 0;
	uint8_t edp_general_cap2 = 0;

	if (!link)
		return false;

	if (core_link_read_dpcd(link, DP_SOURCE_SINK_CAP, &dpcd_data, 1) != DC_OK)
		return false;

	link->dpcd_sink_ext_caps.raw = dpcd_data;

	if (core_link_read_dpcd(link, DP_EDP_GENERAL_CAP_2, &edp_general_cap2, 1) != DC_OK)
		return false;

	link->dpcd_caps.panel_luminance_control = (edp_general_cap2 & DP_EDP_PANEL_LUMINANCE_CONTROL_CAPABLE) != 0;

	return true;
}

enum dc_status dp_retrieve_lttpr_cap(struct dc_link *link)
{
	uint8_t lttpr_dpcd_data[8];
	enum dc_status status;
	bool is_lttpr_present;

	/* Logic to determine LTTPR support*/
	bool vbios_lttpr_interop = link->dc->caps.vbios_lttpr_aware;

	if (!vbios_lttpr_interop || !link->dc->caps.extended_aux_timeout_support)
		return DC_NOT_SUPPORTED;

	/* By reading LTTPR capability, RX assumes that we will enable
	 * LTTPR extended aux timeout if LTTPR is present.
	 */
	status = core_link_read_dpcd(
			link,
			DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV,
			lttpr_dpcd_data,
			sizeof(lttpr_dpcd_data));

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
			lttpr_dpcd_data[DP_PHY_REPEATER_128B132B_RATES -
							DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];

	/* If this chip cap is set, at least one retimer must exist in the chain
	 * Override count to 1 if we receive a known bad count (0 or an invalid value) */
	if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			(dp_parse_lttpr_repeater_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) == 0)) {
		ASSERT(0);
		link->dpcd_caps.lttpr_caps.phy_repeater_cnt = 0x80;
		DC_LOG_DC("lttpr_caps forced phy_repeater_cnt = %d\n", link->dpcd_caps.lttpr_caps.phy_repeater_cnt);
	}

	/* Attempt to train in LTTPR transparent mode if repeater count exceeds 8. */
	is_lttpr_present = dp_is_lttpr_present(link);

	if (is_lttpr_present)
		CONN_DATA_DETECT(link, lttpr_dpcd_data, sizeof(lttpr_dpcd_data), "LTTPR Caps: ");

	DC_LOG_DC("is_lttpr_present = %d\n", is_lttpr_present);
	return status;
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

	struct dp_device_vendor_id sink_id;
	union down_stream_port_count down_strm_port_count;
	union edp_configuration_cap edp_config_cap;
	union dp_downstream_port_present ds_port = { 0 };
	enum dc_status status = DC_ERROR_UNEXPECTED;
	uint32_t read_dpcd_retry_cnt = 3;
	int i;
	struct dp_sink_hw_fw_revision dp_hw_fw_revision;
	const uint32_t post_oui_delay = 30; // 30ms
	bool is_fec_supported = false;
	bool is_dsc_basic_supported = false;
	bool is_dsc_passthrough_supported = false;

	memset(dpcd_data, '\0', sizeof(dpcd_data));
	memset(&down_strm_port_count,
		'\0', sizeof(union down_stream_port_count));
	memset(&edp_config_cap, '\0',
		sizeof(union edp_configuration_cap));

	/* if extended timeout is supported in hardware,
	 * default to LTTPR timeout (3.2ms) first as a W/A for DP link layer
	 * CTS 4.2.1.1 regression introduced by CTS specs requirement update.
	 */
	try_to_configure_aux_timeout(link->ddc,
			LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD);

	status = dp_retrieve_lttpr_cap(link);

	if (status != DC_OK) {
		status = wake_up_aux_channel(link);
		if (status == DC_OK)
			dp_retrieve_lttpr_cap(link);
		else
			return false;
	}

	if (dp_is_lttpr_present(link))
		configure_lttpr_mode_transparent(link);

	/* Read DP tunneling information. */
	status = dpcd_get_tunneling_device_data(link);

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

	if (!dp_is_lttpr_present(link))
		try_to_configure_aux_timeout(link->ddc, LINK_AUX_DEFAULT_TIMEOUT_PERIOD);


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

		/* AdaptiveSyncCapability  */
		dpcd_dprx_data = 0;
		for (i = 0; i < read_dpcd_retry_cnt; i++) {
			status = core_link_read_dpcd(
					link, DP_DPRX_FEATURE_ENUMERATION_LIST_CONT_1,
					&dpcd_dprx_data, sizeof(dpcd_dprx_data));
			if (status == DC_OK)
				break;
		}

		link->dpcd_caps.adaptive_sync_caps.dp_adap_sync_caps.raw = dpcd_dprx_data;

		if (status != DC_OK)
			dm_error("%s: Read DPRX caps data failed. Addr:%#x\n",
					__func__, DP_DPRX_FEATURE_ENUMERATION_LIST_CONT_1);
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
	link->dpcd_caps.is_mst_capable = read_is_mst_supported(link);
	DC_LOG_DC("%s: MST_Support: %s\n", __func__, str_yes_no(link->dpcd_caps.is_mst_capable));

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
	link->dpcd_caps.channel_coding_cap.raw =
			dpcd_data[DP_MAIN_LINK_CHANNEL_CODING - DP_DPCD_REV];
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

	/* Quirk for Retina panels: wrong DP_MAX_LINK_RATE */
	{
		uint8_t str_mbp_2018[] = { 101, 68, 21, 103, 98, 97 };
		uint8_t fwrev_mbp_2018[] = { 7, 4 };
		uint8_t fwrev_mbp_2018_vega[] = { 8, 4 };

		/* We also check for the firmware revision as 16,1 models have an
		 * identical device id and are incorrectly quirked otherwise.
		 */
		if ((link->dpcd_caps.sink_dev_id == 0x0010fa) &&
		    !memcmp(link->dpcd_caps.sink_dev_id_str, str_mbp_2018,
			     sizeof(str_mbp_2018)) &&
		    (!memcmp(link->dpcd_caps.sink_fw_revision, fwrev_mbp_2018,
			     sizeof(fwrev_mbp_2018)) ||
		    !memcmp(link->dpcd_caps.sink_fw_revision, fwrev_mbp_2018_vega,
			     sizeof(fwrev_mbp_2018_vega)))) {
			link->reported_link_cap.link_rate = LINK_RATE_RBR2;
		}
	}

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
		if (status == DC_OK) {
			is_fec_supported = link->dpcd_caps.fec_cap.bits.FEC_CAPABLE;
			is_dsc_basic_supported = link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT;
			is_dsc_passthrough_supported = link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_PASSTHROUGH_SUPPORT;
			DC_LOG_DC("%s: FEC_Sink_Support: %s\n", __func__,
				  str_yes_no(is_fec_supported));
			DC_LOG_DC("%s: DSC_Basic_Sink_Support: %s\n", __func__,
				  str_yes_no(is_dsc_basic_supported));
			DC_LOG_DC("%s: DSC_Passthrough_Sink_Support: %s\n", __func__,
				  str_yes_no(is_dsc_passthrough_supported));
		}
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
				link->dc->debug.dpia_debug.bits.enable_force_tbt3_work_around &&
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

	if (link->dpcd_caps.channel_coding_cap.bits.DP_128b_132b_SUPPORTED) {
		DC_LOG_DP2("128b/132b encoding is supported at link %d", link->link_index);

		core_link_read_dpcd(link,
				DP_128B132B_SUPPORTED_LINK_RATES,
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

	retrieve_cable_id(link);
	dpcd_write_cable_id_to_dprx(link);

	/* Connectivity log: detection */
	CONN_DATA_DETECT(link, dpcd_data, sizeof(dpcd_data), "Rx Caps: ");

	return true;
}

bool detect_dp_sink_caps(struct dc_link *link)
{
	return retrieve_link_cap(link);
}

void detect_edp_sink_caps(struct dc_link *link)
{
	uint8_t supported_link_rates[16];
	uint32_t entry;
	uint32_t link_rate_in_khz;
	enum dc_link_rate link_rate = LINK_RATE_UNKNOWN;
	uint8_t backlight_adj_cap;
	uint8_t general_edp_cap;

	retrieve_link_cap(link);
	link->dpcd_caps.edp_supported_link_rates_count = 0;
	memset(supported_link_rates, 0, sizeof(supported_link_rates));

	/*
	 * edp_supported_link_rates_count is only valid for eDP v1.4 or higher.
	 * Per VESA eDP spec, "The DPCD revision for eDP v1.4 is 13h"
	 */
	if (link->dpcd_caps.dpcd_rev.raw >= DPCD_REV_13) {
		// Read DPCD 00010h - 0001Fh 16 bytes at one shot
		core_link_read_dpcd(link, DP_SUPPORTED_LINK_RATES,
							supported_link_rates, sizeof(supported_link_rates));

		for (entry = 0; entry < 16; entry += 2) {
			// DPCD register reports per-lane link rate = 16-bit link rate capability
			// value X 200 kHz. Need multiplier to find link rate in kHz.
			link_rate_in_khz = (supported_link_rates[entry+1] * 0x100 +
										supported_link_rates[entry]) * 200;

			DC_LOG_DC("%s: eDP v1.4 supported sink rates: [%d] %d kHz\n", __func__,
				  entry / 2, link_rate_in_khz);

			if (link_rate_in_khz != 0) {
				link_rate = linkRateInKHzToLinkRateMultiplier(link_rate_in_khz);
				link->dpcd_caps.edp_supported_link_rates[link->dpcd_caps.edp_supported_link_rates_count] = link_rate;
				link->dpcd_caps.edp_supported_link_rates_count++;
			}
		}
	}

	core_link_read_dpcd(link, DP_EDP_BACKLIGHT_ADJUSTMENT_CAP,
						&backlight_adj_cap, sizeof(backlight_adj_cap));

	link->dpcd_caps.dynamic_backlight_capable_edp =
				(backlight_adj_cap & DP_EDP_DYNAMIC_BACKLIGHT_CAP) ? true:false;

	core_link_read_dpcd(link, DP_EDP_GENERAL_CAP_1,
						&general_edp_cap, sizeof(general_edp_cap));

	link->dpcd_caps.set_power_state_capable_edp =
				(general_edp_cap & DP_EDP_SET_POWER_CAP) ? true:false;

	set_default_brightness_aux(link);

	core_link_read_dpcd(link, DP_EDP_DPCD_REV,
		&link->dpcd_caps.edp_rev,
		sizeof(link->dpcd_caps.edp_rev));
	/*
	 * PSR is only valid for eDP v1.3 or higher.
	 */
	if (link->dpcd_caps.edp_rev >= DP_EDP_13) {
		core_link_read_dpcd(link, DP_PSR_SUPPORT,
			&link->dpcd_caps.psr_info.psr_version,
			sizeof(link->dpcd_caps.psr_info.psr_version));
		if (link->dpcd_caps.sink_dev_id == DP_BRANCH_DEVICE_ID_001CF8)
			core_link_read_dpcd(link, DP_FORCE_PSRSU_CAPABILITY,
						&link->dpcd_caps.psr_info.force_psrsu_cap,
						sizeof(link->dpcd_caps.psr_info.force_psrsu_cap));
		core_link_read_dpcd(link, DP_PSR_CAPS,
			&link->dpcd_caps.psr_info.psr_dpcd_caps.raw,
			sizeof(link->dpcd_caps.psr_info.psr_dpcd_caps.raw));
		if (link->dpcd_caps.psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED) {
			core_link_read_dpcd(link, DP_PSR2_SU_Y_GRANULARITY,
				&link->dpcd_caps.psr_info.psr2_su_y_granularity_cap,
				sizeof(link->dpcd_caps.psr_info.psr2_su_y_granularity_cap));
		}
	}

	/*
	 * ALPM is only valid for eDP v1.4 or higher.
	 */
	if (link->dpcd_caps.dpcd_rev.raw >= DP_EDP_14)
		core_link_read_dpcd(link, DP_RECEIVER_ALPM_CAP,
			&link->dpcd_caps.alpm_caps.raw,
			sizeof(link->dpcd_caps.alpm_caps.raw));

	/*
	 * Read REPLAY info
	 */
	core_link_read_dpcd(link, DP_SINK_PR_PIXEL_DEVIATION_PER_LINE,
			&link->dpcd_caps.pr_info.pixel_deviation_per_line,
			sizeof(link->dpcd_caps.pr_info.pixel_deviation_per_line));
	core_link_read_dpcd(link, DP_SINK_PR_MAX_NUMBER_OF_DEVIATION_LINE,
			&link->dpcd_caps.pr_info.max_deviation_line,
			sizeof(link->dpcd_caps.pr_info.max_deviation_line));
}

bool dp_get_max_link_enc_cap(const struct dc_link *link, struct dc_link_settings *max_link_enc_cap)
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

const struct dc_link_settings *dp_get_verified_link_cap(
		const struct dc_link *link)
{
	if (link->preferred_link_setting.lane_count != LANE_COUNT_UNKNOWN &&
			link->preferred_link_setting.link_rate != LINK_RATE_UNKNOWN)
		return &link->preferred_link_setting;
	return &link->verified_link_cap;
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

	/* Lower link settings based on cable attributes
	 * Cable ID is a DP2 feature to identify max certified link rate that
	 * a cable can carry. The cable identification method requires both
	 * cable and display hardware support. Since the specs comes late, it is
	 * anticipated that the first round of DP2 cables and displays may not
	 * be fully compatible to reliably return cable ID data. Therefore the
	 * decision of our cable id policy is that if the cable can return non
	 * zero cable id data, we will take cable's link rate capability into
	 * account. However if we get zero data, the cable link rate capability
	 * is considered inconclusive. In this case, we will not take cable's
	 * capability into account to avoid of over limiting hardware capability
	 * from users. The max overall link rate capability is still determined
	 * after actual dp pre-training. Cable id is considered as an auxiliary
	 * method of determining max link bandwidth capability.
	 */
	cable_max_link_rate = get_cable_max_link_rate(link);

	if (!link->dc->debug.ignore_cable_id &&
			cable_max_link_rate != LINK_RATE_UNKNOWN &&
			cable_max_link_rate < max_link_cap.link_rate)
		max_link_cap.link_rate = cable_max_link_rate;

	/* account for lttpr repeaters cap
	 * notes: repeaters do not snoop in the DPRX Capabilities addresses (3.6.3).
	 */
	if (dp_is_lttpr_present(link)) {
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

	if (link_dp_get_encoding_format(&max_link_cap) == DP_128b_132b_ENCODING &&
			link->dc->debug.disable_uhbr)
		max_link_cap.link_rate = LINK_RATE_HIGH3;

	return max_link_cap;
}

static bool dp_verify_link_cap(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int *fail_count)
{
	struct dc_link_settings cur_link_settings = {0};
	struct dc_link_settings max_link_settings = *known_limit_link_setting;
	bool success = false;
	bool skip_video_pattern;
	enum clock_source_id dp_cs_id = get_clock_source_id(link);
	enum link_training_result status = LINK_TRAINING_SUCCESS;
	union hpd_irq_data irq_data;
	struct link_resource link_res;

	memset(&irq_data, 0, sizeof(irq_data));
	cur_link_settings = max_link_settings;

	/* Grant extended timeout request */
	if (dp_is_lttpr_present(link) && link->dpcd_caps.lttpr_caps.max_ext_timeout > 0) {
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

		status = dp_perform_link_training(
				link,
				&link_res,
				&cur_link_settings,
				skip_video_pattern);

		if (status == LINK_TRAINING_SUCCESS) {
			success = true;
			fsleep(1000);
			if (dp_read_hpd_rx_irq_data(link, &irq_data) == DC_OK &&
					dp_parse_link_loss_status(
							link,
							&irq_data))
				(*fail_count)++;
		} else if (status == LINK_TRAINING_LINK_LOSS) {
			success = true;
			(*fail_count)++;
		} else {
			(*fail_count)++;
		}
		dp_trace_lt_total_count_increment(link, true);
		dp_trace_lt_result_update(link, status, true);
		dp_disable_link_phy(link, &link_res, link->connector_signal);
	} while (!success && decide_fallback_link_setting(link,
			&max_link_settings, &cur_link_settings, status));

	link->verified_link_cap = success ?
			cur_link_settings : fail_safe_link_settings;
	return success;
}

bool dp_verify_link_cap_with_retries(
	struct dc_link *link,
	struct dc_link_settings *known_limit_link_setting,
	int attempts)
{
	int i = 0;
	bool success = false;
	int fail_count = 0;
	struct dc_link_settings last_verified_link_cap = fail_safe_link_settings;

	dp_trace_detect_lt_init(link);

	if (link->link_enc && link->link_enc->features.flags.bits.DP_IS_USB_C &&
			link->dc->debug.usbc_combo_phy_reset_wa)
		apply_usbc_combo_phy_reset_wa(link, known_limit_link_setting);

	dp_trace_set_lt_start_timestamp(link, false);
	for (i = 0; i < attempts; i++) {
		enum dc_connection_type type = dc_connection_none;

		memset(&link->verified_link_cap, 0,
				sizeof(struct dc_link_settings));
		if (!link_detect_connection_type(link, &type) || type == dc_connection_none) {
			link->verified_link_cap = fail_safe_link_settings;
			break;
		} else if (dp_verify_link_cap(link, known_limit_link_setting, &fail_count)) {
			last_verified_link_cap = link->verified_link_cap;
			if (fail_count == 0) {
				success = true;
				break;
			}
		} else {
			link->verified_link_cap = last_verified_link_cap;
		}
		fsleep(10 * 1000);
	}

	dp_trace_lt_fail_count_update(link, fail_count, true);
	dp_trace_set_lt_end_timestamp(link, true);

	return success;
}

/*
 * Check if there is a native DP or passive DP-HDMI dongle connected
 */
bool dp_is_sink_present(struct dc_link *link)
{
	enum gpio_result gpio_result;
	uint32_t clock_pin = 0;
	uint8_t retry = 0;
	struct ddc *ddc;

	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(link->link_id);

	bool present =
		((connector_id == CONNECTOR_ID_DISPLAY_PORT) ||
		(connector_id == CONNECTOR_ID_EDP) ||
		(connector_id == CONNECTOR_ID_USBC));

	ddc = get_ddc_pin(link->ddc);

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return present;
	}

	/* Open GPIO and set it to I2C mode */
	/* Note: this GpioMode_Input will be converted
	 * to GpioConfigType_I2cAuxDualMode in GPIO component,
	 * which indicates we need additional delay
	 */

	if (dal_ddc_open(ddc, GPIO_MODE_INPUT,
			 GPIO_DDC_CONFIG_TYPE_MODE_I2C) != GPIO_RESULT_OK) {
		dal_ddc_close(ddc);

		return present;
	}

	/*
	 * Read GPIO: DP sink is present if both clock and data pins are zero
	 *
	 * [W/A] plug-unplug DP cable, sometimes customer board has
	 * one short pulse on clk_pin(1V, < 1ms). DP will be config to HDMI/DVI
	 * then monitor can't br light up. Add retry 3 times
	 * But in real passive dongle, it need additional 3ms to detect
	 */
	do {
		gpio_result = dal_gpio_get_value(ddc->pin_clock, &clock_pin);
		ASSERT(gpio_result == GPIO_RESULT_OK);
		if (clock_pin)
			fsleep(1000);
		else
			break;
	} while (retry++ < 3);

	present = (gpio_result == GPIO_RESULT_OK) && !clock_pin;

	dal_ddc_close(ddc);

	return present;
}
