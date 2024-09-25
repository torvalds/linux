/*
 * Copyright © 2008-2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/debugfs.h>

#include <drm/display/drm_dp_helper.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_encoder.h"
#include "intel_hotplug.h"
#include "intel_panel.h"

#define LT_MSG_PREFIX			"[CONNECTOR:%d:%s][ENCODER:%d:%s][%s] "
#define LT_MSG_ARGS(_intel_dp, _dp_phy)	(_intel_dp)->attached_connector->base.base.id, \
					(_intel_dp)->attached_connector->base.name, \
					dp_to_dig_port(_intel_dp)->base.base.base.id, \
					dp_to_dig_port(_intel_dp)->base.base.name, \
					drm_dp_phy_name(_dp_phy)

#define lt_dbg(_intel_dp, _dp_phy, _format, ...) \
	drm_dbg_kms(to_intel_display(_intel_dp)->drm, \
		    LT_MSG_PREFIX _format, \
		    LT_MSG_ARGS(_intel_dp, _dp_phy), ## __VA_ARGS__)

#define lt_err(_intel_dp, _dp_phy, _format, ...) do { \
	if (intel_digital_port_connected(&dp_to_dig_port(_intel_dp)->base)) \
		drm_err(to_intel_display(_intel_dp)->drm, \
			LT_MSG_PREFIX _format, \
			LT_MSG_ARGS(_intel_dp, _dp_phy), ## __VA_ARGS__); \
	else \
		lt_dbg(_intel_dp, _dp_phy, "Sink disconnected: " _format, ## __VA_ARGS__); \
} while (0)

static void intel_dp_reset_lttpr_common_caps(struct intel_dp *intel_dp)
{
	memset(intel_dp->lttpr_common_caps, 0, sizeof(intel_dp->lttpr_common_caps));
}

static void intel_dp_reset_lttpr_count(struct intel_dp *intel_dp)
{
	intel_dp->lttpr_common_caps[DP_PHY_REPEATER_CNT -
				    DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV] = 0;
}

static u8 *intel_dp_lttpr_phy_caps(struct intel_dp *intel_dp,
				   enum drm_dp_phy dp_phy)
{
	return intel_dp->lttpr_phy_caps[dp_phy - DP_PHY_LTTPR1];
}

static void intel_dp_read_lttpr_phy_caps(struct intel_dp *intel_dp,
					 const u8 dpcd[DP_RECEIVER_CAP_SIZE],
					 enum drm_dp_phy dp_phy)
{
	u8 *phy_caps = intel_dp_lttpr_phy_caps(intel_dp, dp_phy);

	if (drm_dp_read_lttpr_phy_caps(&intel_dp->aux, dpcd, dp_phy, phy_caps) < 0) {
		lt_dbg(intel_dp, dp_phy, "failed to read the PHY caps\n");
		return;
	}

	lt_dbg(intel_dp, dp_phy, "PHY capabilities: %*ph\n",
	       (int)sizeof(intel_dp->lttpr_phy_caps[0]),
	       phy_caps);
}

static bool intel_dp_read_lttpr_common_caps(struct intel_dp *intel_dp,
					    const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int ret;

	ret = drm_dp_read_lttpr_common_caps(&intel_dp->aux, dpcd,
					    intel_dp->lttpr_common_caps);
	if (ret < 0)
		goto reset_caps;

	lt_dbg(intel_dp, DP_PHY_DPRX, "LTTPR common capabilities: %*ph\n",
	       (int)sizeof(intel_dp->lttpr_common_caps),
	       intel_dp->lttpr_common_caps);

	/* The minimum value of LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV is 1.4 */
	if (intel_dp->lttpr_common_caps[0] < 0x14)
		goto reset_caps;

	return true;

reset_caps:
	intel_dp_reset_lttpr_common_caps(intel_dp);
	return false;
}

static bool
intel_dp_set_lttpr_transparent_mode(struct intel_dp *intel_dp, bool enable)
{
	u8 val = enable ? DP_PHY_REPEATER_MODE_TRANSPARENT :
			  DP_PHY_REPEATER_MODE_NON_TRANSPARENT;

	if (drm_dp_dpcd_write(&intel_dp->aux, DP_PHY_REPEATER_MODE, &val, 1) != 1)
		return false;

	intel_dp->lttpr_common_caps[DP_PHY_REPEATER_MODE -
				    DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV] = val;

	return true;
}

static bool intel_dp_lttpr_transparent_mode_enabled(struct intel_dp *intel_dp)
{
	return intel_dp->lttpr_common_caps[DP_PHY_REPEATER_MODE -
					   DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV] ==
		DP_PHY_REPEATER_MODE_TRANSPARENT;
}

/*
 * Read the LTTPR common capabilities and switch the LTTPR PHYs to
 * non-transparent mode if this is supported. Preserve the
 * transparent/non-transparent mode on an active link.
 *
 * Return the number of detected LTTPRs in non-transparent mode or 0 if the
 * LTTPRs are in transparent mode or the detection failed.
 */
static int intel_dp_init_lttpr_phys(struct intel_dp *intel_dp, const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int lttpr_count;

	if (!intel_dp_read_lttpr_common_caps(intel_dp, dpcd))
		return 0;

	lttpr_count = drm_dp_lttpr_count(intel_dp->lttpr_common_caps);
	/*
	 * Prevent setting LTTPR transparent mode explicitly if no LTTPRs are
	 * detected as this breaks link training at least on the Dell WD19TB
	 * dock.
	 */
	if (lttpr_count == 0)
		return 0;

	/*
	 * Don't change the mode on an active link, to prevent a loss of link
	 * synchronization. See DP Standard v2.0 3.6.7. about the LTTPR
	 * resetting its internal state when the mode is changed from
	 * non-transparent to transparent.
	 */
	if (intel_dp->link_trained) {
		if (lttpr_count < 0 || intel_dp_lttpr_transparent_mode_enabled(intel_dp))
			goto out_reset_lttpr_count;

		return lttpr_count;
	}

	/*
	 * See DP Standard v2.0 3.6.6.1. about the explicit disabling of
	 * non-transparent mode and the disable->enable non-transparent mode
	 * sequence.
	 */
	intel_dp_set_lttpr_transparent_mode(intel_dp, true);

	/*
	 * In case of unsupported number of LTTPRs or failing to switch to
	 * non-transparent mode fall-back to transparent link training mode,
	 * still taking into account any LTTPR common lane- rate/count limits.
	 */
	if (lttpr_count < 0)
		goto out_reset_lttpr_count;

	if (!intel_dp_set_lttpr_transparent_mode(intel_dp, false)) {
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Switching to LTTPR non-transparent LT mode failed, fall-back to transparent mode\n");

		intel_dp_set_lttpr_transparent_mode(intel_dp, true);

		goto out_reset_lttpr_count;
	}

	return lttpr_count;

out_reset_lttpr_count:
	intel_dp_reset_lttpr_count(intel_dp);

	return 0;
}

static int intel_dp_init_lttpr(struct intel_dp *intel_dp, const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int lttpr_count;
	int i;

	lttpr_count = intel_dp_init_lttpr_phys(intel_dp, dpcd);

	for (i = 0; i < lttpr_count; i++) {
		intel_dp_read_lttpr_phy_caps(intel_dp, dpcd, DP_PHY_LTTPR(i));
		drm_dp_dump_lttpr_desc(&intel_dp->aux, DP_PHY_LTTPR(i));
	}

	return lttpr_count;
}

int intel_dp_read_dprx_caps(struct intel_dp *intel_dp, u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (intel_dp_is_edp(intel_dp))
		return 0;

	/*
	 * Detecting LTTPRs must be avoided on platforms with an AUX timeout
	 * period < 3.2ms. (see DP Standard v2.0, 2.11.2, 3.6.6.1).
	 */
	if (DISPLAY_VER(display) >= 10 && !IS_GEMINILAKE(i915))
		if (drm_dp_dpcd_probe(&intel_dp->aux,
				      DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV))
			return -EIO;

	if (drm_dp_read_dpcd_caps(&intel_dp->aux, dpcd))
		return -EIO;

	return 0;
}

/**
 * intel_dp_init_lttpr_and_dprx_caps - detect LTTPR and DPRX caps, init the LTTPR link training mode
 * @intel_dp: Intel DP struct
 *
 * Read the LTTPR common and DPRX capabilities and switch to non-transparent
 * link training mode if any is detected and read the PHY capabilities for all
 * detected LTTPRs. In case of an LTTPR detection error or if the number of
 * LTTPRs is more than is supported (8), fall back to the no-LTTPR,
 * transparent mode link training mode.
 *
 * Returns:
 *   >0  if LTTPRs were detected and the non-transparent LT mode was set. The
 *       DPRX capabilities are read out.
 *    0  if no LTTPRs or more than 8 LTTPRs were detected or in case of a
 *       detection failure and the transparent LT mode was set. The DPRX
 *       capabilities are read out.
 *   <0  Reading out the DPRX capabilities failed.
 */
int intel_dp_init_lttpr_and_dprx_caps(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);
	int lttpr_count = 0;

	/*
	 * Detecting LTTPRs must be avoided on platforms with an AUX timeout
	 * period < 3.2ms. (see DP Standard v2.0, 2.11.2, 3.6.6.1).
	 */
	if (!intel_dp_is_edp(intel_dp) &&
	    (DISPLAY_VER(display) >= 10 && !IS_GEMINILAKE(i915))) {
		u8 dpcd[DP_RECEIVER_CAP_SIZE];
		int err = intel_dp_read_dprx_caps(intel_dp, dpcd);

		if (err != 0)
			return err;

		lttpr_count = intel_dp_init_lttpr(intel_dp, dpcd);
	}

	/*
	 * The DPTX shall read the DPRX caps after LTTPR detection, so re-read
	 * it here.
	 */
	if (drm_dp_read_dpcd_caps(&intel_dp->aux, intel_dp->dpcd)) {
		intel_dp_reset_lttpr_common_caps(intel_dp);
		return -EIO;
	}

	return lttpr_count;
}

static u8 dp_voltage_max(u8 preemph)
{
	switch (preemph & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_1;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
	default:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
	}
}

static u8 intel_dp_lttpr_voltage_max(struct intel_dp *intel_dp,
				     enum drm_dp_phy dp_phy)
{
	const u8 *phy_caps = intel_dp_lttpr_phy_caps(intel_dp, dp_phy);

	if (drm_dp_lttpr_voltage_swing_level_3_supported(phy_caps))
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
}

static u8 intel_dp_lttpr_preemph_max(struct intel_dp *intel_dp,
				     enum drm_dp_phy dp_phy)
{
	const u8 *phy_caps = intel_dp_lttpr_phy_caps(intel_dp, dp_phy);

	if (drm_dp_lttpr_pre_emphasis_level_3_supported(phy_caps))
		return DP_TRAIN_PRE_EMPH_LEVEL_3;
	else
		return DP_TRAIN_PRE_EMPH_LEVEL_2;
}

static bool
intel_dp_phy_is_downstream_of_source(struct intel_dp *intel_dp,
				     enum drm_dp_phy dp_phy)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int lttpr_count = drm_dp_lttpr_count(intel_dp->lttpr_common_caps);

	drm_WARN_ON_ONCE(display->drm,
			 lttpr_count <= 0 && dp_phy != DP_PHY_DPRX);

	return lttpr_count <= 0 || dp_phy == DP_PHY_LTTPR(lttpr_count - 1);
}

static u8 intel_dp_phy_voltage_max(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state,
				   enum drm_dp_phy dp_phy)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u8 voltage_max;

	/*
	 * Get voltage_max from the DPTX_PHY (source or LTTPR) upstream from
	 * the DPRX_PHY we train.
	 */
	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		voltage_max = intel_dp->voltage_max(intel_dp, crtc_state);
	else
		voltage_max = intel_dp_lttpr_voltage_max(intel_dp, dp_phy + 1);

	drm_WARN_ON_ONCE(display->drm,
			 voltage_max != DP_TRAIN_VOLTAGE_SWING_LEVEL_2 &&
			 voltage_max != DP_TRAIN_VOLTAGE_SWING_LEVEL_3);

	return voltage_max;
}

static u8 intel_dp_phy_preemph_max(struct intel_dp *intel_dp,
				   enum drm_dp_phy dp_phy)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u8 preemph_max;

	/*
	 * Get preemph_max from the DPTX_PHY (source or LTTPR) upstream from
	 * the DPRX_PHY we train.
	 */
	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		preemph_max = intel_dp->preemph_max(intel_dp);
	else
		preemph_max = intel_dp_lttpr_preemph_max(intel_dp, dp_phy + 1);

	drm_WARN_ON_ONCE(display->drm,
			 preemph_max != DP_TRAIN_PRE_EMPH_LEVEL_2 &&
			 preemph_max != DP_TRAIN_PRE_EMPH_LEVEL_3);

	return preemph_max;
}

static bool has_per_lane_signal_levels(struct intel_dp *intel_dp,
				       enum drm_dp_phy dp_phy)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);

	return !intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy) ||
		DISPLAY_VER(display) >= 10 || IS_BROXTON(i915);
}

/* 128b/132b */
static u8 intel_dp_get_lane_adjust_tx_ffe_preset(struct intel_dp *intel_dp,
						 const struct intel_crtc_state *crtc_state,
						 enum drm_dp_phy dp_phy,
						 const u8 link_status[DP_LINK_STATUS_SIZE],
						 int lane)
{
	u8 tx_ffe = 0;

	if (has_per_lane_signal_levels(intel_dp, dp_phy)) {
		lane = min(lane, crtc_state->lane_count - 1);
		tx_ffe = drm_dp_get_adjust_tx_ffe_preset(link_status, lane);
	} else {
		for (lane = 0; lane < crtc_state->lane_count; lane++)
			tx_ffe = max(tx_ffe, drm_dp_get_adjust_tx_ffe_preset(link_status, lane));
	}

	return tx_ffe;
}

/* 8b/10b */
static u8 intel_dp_get_lane_adjust_vswing_preemph(struct intel_dp *intel_dp,
						  const struct intel_crtc_state *crtc_state,
						  enum drm_dp_phy dp_phy,
						  const u8 link_status[DP_LINK_STATUS_SIZE],
						  int lane)
{
	u8 v = 0;
	u8 p = 0;
	u8 voltage_max;
	u8 preemph_max;

	if (has_per_lane_signal_levels(intel_dp, dp_phy)) {
		lane = min(lane, crtc_state->lane_count - 1);

		v = drm_dp_get_adjust_request_voltage(link_status, lane);
		p = drm_dp_get_adjust_request_pre_emphasis(link_status, lane);
	} else {
		for (lane = 0; lane < crtc_state->lane_count; lane++) {
			v = max(v, drm_dp_get_adjust_request_voltage(link_status, lane));
			p = max(p, drm_dp_get_adjust_request_pre_emphasis(link_status, lane));
		}
	}

	preemph_max = intel_dp_phy_preemph_max(intel_dp, dp_phy);
	if (p >= preemph_max)
		p = preemph_max | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	v = min(v, dp_voltage_max(p));

	voltage_max = intel_dp_phy_voltage_max(intel_dp, crtc_state, dp_phy);
	if (v >= voltage_max)
		v = voltage_max | DP_TRAIN_MAX_SWING_REACHED;

	return v | p;
}

static u8 intel_dp_get_lane_adjust_train(struct intel_dp *intel_dp,
					 const struct intel_crtc_state *crtc_state,
					 enum drm_dp_phy dp_phy,
					 const u8 link_status[DP_LINK_STATUS_SIZE],
					 int lane)
{
	if (intel_dp_is_uhbr(crtc_state))
		return intel_dp_get_lane_adjust_tx_ffe_preset(intel_dp, crtc_state,
							      dp_phy, link_status, lane);
	else
		return intel_dp_get_lane_adjust_vswing_preemph(intel_dp, crtc_state,
							       dp_phy, link_status, lane);
}

#define TRAIN_REQ_FMT "%d/%d/%d/%d"
#define _TRAIN_REQ_VSWING_ARGS(link_status, lane) \
	(drm_dp_get_adjust_request_voltage((link_status), (lane)) >> DP_TRAIN_VOLTAGE_SWING_SHIFT)
#define TRAIN_REQ_VSWING_ARGS(link_status) \
	_TRAIN_REQ_VSWING_ARGS(link_status, 0), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 1), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 2), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 3)
#define _TRAIN_REQ_PREEMPH_ARGS(link_status, lane) \
	(drm_dp_get_adjust_request_pre_emphasis((link_status), (lane)) >> DP_TRAIN_PRE_EMPHASIS_SHIFT)
#define TRAIN_REQ_PREEMPH_ARGS(link_status) \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 0), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 1), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 2), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 3)
#define _TRAIN_REQ_TX_FFE_ARGS(link_status, lane) \
	drm_dp_get_adjust_tx_ffe_preset((link_status), (lane))
#define TRAIN_REQ_TX_FFE_ARGS(link_status) \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 0), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 1), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 2), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 3)

void
intel_dp_get_adjust_train(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state,
			  enum drm_dp_phy dp_phy,
			  const u8 link_status[DP_LINK_STATUS_SIZE])
{
	int lane;

	if (intel_dp_is_uhbr(crtc_state)) {
		lt_dbg(intel_dp, dp_phy,
		       "128b/132b, lanes: %d, "
		       "TX FFE request: " TRAIN_REQ_FMT "\n",
		       crtc_state->lane_count,
		       TRAIN_REQ_TX_FFE_ARGS(link_status));
	} else {
		lt_dbg(intel_dp, dp_phy,
		       "8b/10b, lanes: %d, "
		       "vswing request: " TRAIN_REQ_FMT ", "
		       "pre-emphasis request: " TRAIN_REQ_FMT "\n",
		       crtc_state->lane_count,
		       TRAIN_REQ_VSWING_ARGS(link_status),
		       TRAIN_REQ_PREEMPH_ARGS(link_status));
	}

	for (lane = 0; lane < 4; lane++)
		intel_dp->train_set[lane] =
			intel_dp_get_lane_adjust_train(intel_dp, crtc_state,
						       dp_phy, link_status, lane);
}

static int intel_dp_training_pattern_set_reg(struct intel_dp *intel_dp,
					     enum drm_dp_phy dp_phy)
{
	return dp_phy == DP_PHY_DPRX ?
		DP_TRAINING_PATTERN_SET :
		DP_TRAINING_PATTERN_SET_PHY_REPEATER(dp_phy);
}

static bool
intel_dp_set_link_train(struct intel_dp *intel_dp,
			const struct intel_crtc_state *crtc_state,
			enum drm_dp_phy dp_phy,
			u8 dp_train_pat)
{
	int reg = intel_dp_training_pattern_set_reg(intel_dp, dp_phy);
	u8 buf[sizeof(intel_dp->train_set) + 1];
	int len;

	intel_dp_program_link_training_pattern(intel_dp, crtc_state,
					       dp_phy, dp_train_pat);

	buf[0] = dp_train_pat;
	/* DP_TRAINING_LANEx_SET follow DP_TRAINING_PATTERN_SET */
	memcpy(buf + 1, intel_dp->train_set, crtc_state->lane_count);
	len = crtc_state->lane_count + 1;

	return drm_dp_dpcd_write(&intel_dp->aux, reg, buf, len) == len;
}

static char dp_training_pattern_name(u8 train_pat)
{
	switch (train_pat) {
	case DP_TRAINING_PATTERN_1:
	case DP_TRAINING_PATTERN_2:
	case DP_TRAINING_PATTERN_3:
		return '0' + train_pat;
	case DP_TRAINING_PATTERN_4:
		return '4';
	default:
		MISSING_CASE(train_pat);
		return '?';
	}
}

void
intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
				       const struct intel_crtc_state *crtc_state,
				       enum drm_dp_phy dp_phy,
				       u8 dp_train_pat)
{
	u8 train_pat = intel_dp_training_pattern_symbol(dp_train_pat);

	if (train_pat != DP_TRAINING_PATTERN_DISABLE)
		lt_dbg(intel_dp, dp_phy, "Using DP training pattern TPS%c\n",
		       dp_training_pattern_name(train_pat));

	intel_dp->set_link_train(intel_dp, crtc_state, dp_train_pat);
}

#define TRAIN_SET_FMT "%d%s/%d%s/%d%s/%d%s"
#define _TRAIN_SET_VSWING_ARGS(train_set) \
	((train_set) & DP_TRAIN_VOLTAGE_SWING_MASK) >> DP_TRAIN_VOLTAGE_SWING_SHIFT, \
	(train_set) & DP_TRAIN_MAX_SWING_REACHED ? "(max)" : ""
#define TRAIN_SET_VSWING_ARGS(train_set) \
	_TRAIN_SET_VSWING_ARGS((train_set)[0]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[1]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[2]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[3])
#define _TRAIN_SET_PREEMPH_ARGS(train_set) \
	((train_set) & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT, \
	(train_set) & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED ? "(max)" : ""
#define TRAIN_SET_PREEMPH_ARGS(train_set) \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[0]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[1]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[2]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[3])
#define _TRAIN_SET_TX_FFE_ARGS(train_set) \
	((train_set) & DP_TX_FFE_PRESET_VALUE_MASK), ""
#define TRAIN_SET_TX_FFE_ARGS(train_set) \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[0]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[1]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[2]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[3])

void intel_dp_set_signal_levels(struct intel_dp *intel_dp,
				const struct intel_crtc_state *crtc_state,
				enum drm_dp_phy dp_phy)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;

	if (intel_dp_is_uhbr(crtc_state)) {
		lt_dbg(intel_dp, dp_phy,
		       "128b/132b, lanes: %d, "
		       "TX FFE presets: " TRAIN_SET_FMT "\n",
		       crtc_state->lane_count,
		       TRAIN_SET_TX_FFE_ARGS(intel_dp->train_set));
	} else {
		lt_dbg(intel_dp, dp_phy,
		       "8b/10b, lanes: %d, "
		       "vswing levels: " TRAIN_SET_FMT ", "
		       "pre-emphasis levels: " TRAIN_SET_FMT "\n",
		       crtc_state->lane_count,
		       TRAIN_SET_VSWING_ARGS(intel_dp->train_set),
		       TRAIN_SET_PREEMPH_ARGS(intel_dp->train_set));
	}

	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		encoder->set_signal_levels(encoder, crtc_state);
}

static bool
intel_dp_reset_link_train(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state,
			  enum drm_dp_phy dp_phy,
			  u8 dp_train_pat)
{
	memset(intel_dp->train_set, 0, sizeof(intel_dp->train_set));
	intel_dp_set_signal_levels(intel_dp, crtc_state, dp_phy);
	return intel_dp_set_link_train(intel_dp, crtc_state, dp_phy, dp_train_pat);
}

static bool
intel_dp_update_link_train(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *crtc_state,
			   enum drm_dp_phy dp_phy)
{
	int reg = dp_phy == DP_PHY_DPRX ?
			    DP_TRAINING_LANE0_SET :
			    DP_TRAINING_LANE0_SET_PHY_REPEATER(dp_phy);
	int ret;

	intel_dp_set_signal_levels(intel_dp, crtc_state, dp_phy);

	ret = drm_dp_dpcd_write(&intel_dp->aux, reg,
				intel_dp->train_set, crtc_state->lane_count);

	return ret == crtc_state->lane_count;
}

/* 128b/132b */
static bool intel_dp_lane_max_tx_ffe_reached(u8 train_set_lane)
{
	return (train_set_lane & DP_TX_FFE_PRESET_VALUE_MASK) ==
		DP_TX_FFE_PRESET_VALUE_MASK;
}

/*
 * 8b/10b
 *
 * FIXME: The DP spec is very confusing here, also the Link CTS spec seems to
 * have self contradicting tests around this area.
 *
 * In lieu of better ideas let's just stop when we've reached the max supported
 * vswing with its max pre-emphasis, which is either 2+1 or 3+0 depending on
 * whether vswing level 3 is supported or not.
 */
static bool intel_dp_lane_max_vswing_reached(u8 train_set_lane)
{
	u8 v = (train_set_lane & DP_TRAIN_VOLTAGE_SWING_MASK) >>
		DP_TRAIN_VOLTAGE_SWING_SHIFT;
	u8 p = (train_set_lane & DP_TRAIN_PRE_EMPHASIS_MASK) >>
		DP_TRAIN_PRE_EMPHASIS_SHIFT;

	if ((train_set_lane & DP_TRAIN_MAX_SWING_REACHED) == 0)
		return false;

	if (v + p != 3)
		return false;

	return true;
}

static bool intel_dp_link_max_vswing_reached(struct intel_dp *intel_dp,
					     const struct intel_crtc_state *crtc_state)
{
	int lane;

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		u8 train_set_lane = intel_dp->train_set[lane];

		if (intel_dp_is_uhbr(crtc_state)) {
			if (!intel_dp_lane_max_tx_ffe_reached(train_set_lane))
				return false;
		} else {
			if (!intel_dp_lane_max_vswing_reached(train_set_lane))
				return false;
		}
	}

	return true;
}

void intel_dp_link_training_set_mode(struct intel_dp *intel_dp, int link_rate, bool is_vrr)
{
	u8 link_config[2];

	link_config[0] = is_vrr ? DP_MSA_TIMING_PAR_IGNORE_EN : 0;
	link_config[1] = drm_dp_is_uhbr_rate(link_rate) ?
			 DP_SET_ANSI_128B132B : DP_SET_ANSI_8B10B;
	drm_dp_dpcd_write(&intel_dp->aux, DP_DOWNSPREAD_CTRL, link_config, 2);
}

static void intel_dp_update_downspread_ctrl(struct intel_dp *intel_dp,
					    const struct intel_crtc_state *crtc_state)
{
	intel_dp_link_training_set_mode(intel_dp,
					crtc_state->port_clock, crtc_state->vrr.flipline);
}

void intel_dp_link_training_set_bw(struct intel_dp *intel_dp,
				   int link_bw, int rate_select, int lane_count,
				   bool enhanced_framing)
{
	if (enhanced_framing)
		lane_count |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	if (link_bw) {
		/* DP and eDP v1.3 and earlier link bw set method. */
		u8 link_config[] = { link_bw, lane_count };

		drm_dp_dpcd_write(&intel_dp->aux, DP_LINK_BW_SET, link_config,
				  ARRAY_SIZE(link_config));
	} else {
		/*
		 * eDP v1.4 and later link rate set method.
		 *
		 * eDP v1.4x sinks shall ignore DP_LINK_RATE_SET if
		 * DP_LINK_BW_SET is set. Avoid writing DP_LINK_BW_SET.
		 *
		 * eDP v1.5 sinks allow choosing either, and the last choice
		 * shall be active.
		 */
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_LANE_COUNT_SET, lane_count);
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_LINK_RATE_SET, rate_select);
	}
}

static void intel_dp_update_link_bw_set(struct intel_dp *intel_dp,
					const struct intel_crtc_state *crtc_state,
					u8 link_bw, u8 rate_select)
{
	intel_dp_link_training_set_bw(intel_dp, link_bw, rate_select, crtc_state->lane_count,
				      crtc_state->enhanced_framing);
}

/*
 * Prepare link training by configuring the link parameters. On DDI platforms
 * also enable the port here.
 */
static bool
intel_dp_prepare_link_train(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	u8 link_bw, rate_select;

	if (intel_dp->prepare_link_retrain)
		intel_dp->prepare_link_retrain(intel_dp, crtc_state);

	intel_dp_compute_rate(intel_dp, crtc_state->port_clock,
			      &link_bw, &rate_select);

	/*
	 * WaEdpLinkRateDataReload
	 *
	 * Parade PS8461E MUX (used on varius TGL+ laptops) needs
	 * to snoop the link rates reported by the sink when we
	 * use LINK_RATE_SET in order to operate in jitter cleaning
	 * mode (as opposed to redriver mode). Unfortunately it
	 * loses track of the snooped link rates when powered down,
	 * so we need to make it re-snoop often. Without this high
	 * link rates are not stable.
	 */
	if (!link_bw) {
		__le16 sink_rates[DP_MAX_SUPPORTED_RATES];

		lt_dbg(intel_dp, DP_PHY_DPRX, "Reloading eDP link rates\n");

		drm_dp_dpcd_read(&intel_dp->aux, DP_SUPPORTED_LINK_RATES,
				 sink_rates, sizeof(sink_rates));
	}

	if (link_bw)
		lt_dbg(intel_dp, DP_PHY_DPRX, "Using LINK_BW_SET value %02x\n",
		       link_bw);
	else
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Using LINK_RATE_SET value %02x\n",
		       rate_select);
	/*
	 * Spec DP2.1 Section 3.5.2.16
	 * Prior to LT DPTX should set 128b/132b DP Channel coding and then set link rate
	 */
	intel_dp_update_downspread_ctrl(intel_dp, crtc_state);
	intel_dp_update_link_bw_set(intel_dp, crtc_state, link_bw,
				    rate_select);

	return true;
}

static bool intel_dp_adjust_request_changed(const struct intel_crtc_state *crtc_state,
					    const u8 old_link_status[DP_LINK_STATUS_SIZE],
					    const u8 new_link_status[DP_LINK_STATUS_SIZE])
{
	int lane;

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		u8 old, new;

		if (intel_dp_is_uhbr(crtc_state)) {
			old = drm_dp_get_adjust_tx_ffe_preset(old_link_status, lane);
			new = drm_dp_get_adjust_tx_ffe_preset(new_link_status, lane);
		} else {
			old = drm_dp_get_adjust_request_voltage(old_link_status, lane) |
				drm_dp_get_adjust_request_pre_emphasis(old_link_status, lane);
			new = drm_dp_get_adjust_request_voltage(new_link_status, lane) |
				drm_dp_get_adjust_request_pre_emphasis(new_link_status, lane);
		}

		if (old != new)
			return true;
	}

	return false;
}

void
intel_dp_dump_link_status(struct intel_dp *intel_dp, enum drm_dp_phy dp_phy,
			  const u8 link_status[DP_LINK_STATUS_SIZE])
{
	lt_dbg(intel_dp, dp_phy,
	       "ln0_1:0x%x ln2_3:0x%x align:0x%x sink:0x%x adj_req0_1:0x%x adj_req2_3:0x%x\n",
	       link_status[0], link_status[1], link_status[2],
	       link_status[3], link_status[4], link_status[5]);
}

/*
 * Perform the link training clock recovery phase on the given DP PHY using
 * training pattern 1.
 */
static bool
intel_dp_link_training_clock_recovery(struct intel_dp *intel_dp,
				      const struct intel_crtc_state *crtc_state,
				      enum drm_dp_phy dp_phy)
{
	u8 old_link_status[DP_LINK_STATUS_SIZE] = {};
	int voltage_tries, cr_tries, max_cr_tries;
	u8 link_status[DP_LINK_STATUS_SIZE];
	bool max_vswing_reached = false;
	int delay_us;

	delay_us = drm_dp_read_clock_recovery_delay(&intel_dp->aux,
						    intel_dp->dpcd, dp_phy,
						    intel_dp_is_uhbr(crtc_state));

	/* clock recovery */
	if (!intel_dp_reset_link_train(intel_dp, crtc_state, dp_phy,
				       DP_TRAINING_PATTERN_1 |
				       DP_LINK_SCRAMBLING_DISABLE)) {
		lt_err(intel_dp, dp_phy, "Failed to enable link training\n");
		return false;
	}

	/*
	 * The DP 1.4 spec defines the max clock recovery retries value
	 * as 10 but for pre-DP 1.4 devices we set a very tolerant
	 * retry limit of 80 (4 voltage levels x 4 preemphasis levels x
	 * x 5 identical voltage retries). Since the previous specs didn't
	 * define a limit and created the possibility of an infinite loop
	 * we want to prevent any sync from triggering that corner case.
	 */
	if (intel_dp->dpcd[DP_DPCD_REV] >= DP_DPCD_REV_14)
		max_cr_tries = 10;
	else
		max_cr_tries = 80;

	voltage_tries = 1;
	for (cr_tries = 0; cr_tries < max_cr_tries; ++cr_tries) {
		fsleep(delay_us);

		if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, dp_phy,
						     link_status) < 0) {
			lt_err(intel_dp, dp_phy, "Failed to get link status\n");
			return false;
		}

		if (drm_dp_clock_recovery_ok(link_status, crtc_state->lane_count)) {
			lt_dbg(intel_dp, dp_phy, "Clock recovery OK\n");
			return true;
		}

		if (voltage_tries == 5) {
			intel_dp_dump_link_status(intel_dp, dp_phy, link_status);
			lt_dbg(intel_dp, dp_phy, "Same voltage tried 5 times\n");
			return false;
		}

		if (max_vswing_reached) {
			intel_dp_dump_link_status(intel_dp, dp_phy, link_status);
			lt_dbg(intel_dp, dp_phy, "Max Voltage Swing reached\n");
			return false;
		}

		/* Update training set as requested by target */
		intel_dp_get_adjust_train(intel_dp, crtc_state, dp_phy,
					  link_status);
		if (!intel_dp_update_link_train(intel_dp, crtc_state, dp_phy)) {
			lt_err(intel_dp, dp_phy, "Failed to update link training\n");
			return false;
		}

		if (!intel_dp_adjust_request_changed(crtc_state, old_link_status, link_status))
			++voltage_tries;
		else
			voltage_tries = 1;

		memcpy(old_link_status, link_status, sizeof(link_status));

		if (intel_dp_link_max_vswing_reached(intel_dp, crtc_state))
			max_vswing_reached = true;
	}

	intel_dp_dump_link_status(intel_dp, dp_phy, link_status);
	lt_err(intel_dp, dp_phy, "Failed clock recovery %d times, giving up!\n",
	       max_cr_tries);

	return false;
}

/*
 * Pick Training Pattern Sequence (TPS) for channel equalization. 128b/132b TPS2
 * for UHBR+, TPS4 for HBR3 or for 1.4 devices that support it, TPS3 for HBR2 or
 * 1.2 devices that support it, TPS2 otherwise.
 */
static u32 intel_dp_training_pattern(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     enum drm_dp_phy dp_phy)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);
	bool source_tps3, sink_tps3, source_tps4, sink_tps4;

	/* UHBR+ use separate 128b/132b TPS2 */
	if (intel_dp_is_uhbr(crtc_state))
		return DP_TRAINING_PATTERN_2;

	/*
	 * TPS4 support is mandatory for all downstream devices that
	 * support HBR3. There are no known eDP panels that support
	 * TPS4 as of Feb 2018 as per VESA eDP_v1.4b_E1 specification.
	 * LTTPRs must support TPS4.
	 */
	source_tps4 = intel_dp_source_supports_tps4(i915);
	sink_tps4 = dp_phy != DP_PHY_DPRX ||
		    drm_dp_tps4_supported(intel_dp->dpcd);
	if (source_tps4 && sink_tps4) {
		return DP_TRAINING_PATTERN_4;
	} else if (crtc_state->port_clock == 810000) {
		if (!source_tps4)
			lt_dbg(intel_dp, dp_phy,
			       "8.1 Gbps link rate without source TPS4 support\n");
		if (!sink_tps4)
			lt_dbg(intel_dp, dp_phy,
			       "8.1 Gbps link rate without sink TPS4 support\n");
	}

	/*
	 * TPS3 support is mandatory for downstream devices that
	 * support HBR2. However, not all sinks follow the spec.
	 */
	source_tps3 = intel_dp_source_supports_tps3(i915);
	sink_tps3 = dp_phy != DP_PHY_DPRX ||
		    drm_dp_tps3_supported(intel_dp->dpcd);
	if (source_tps3 && sink_tps3) {
		return  DP_TRAINING_PATTERN_3;
	} else if (crtc_state->port_clock >= 540000) {
		if (!source_tps3)
			lt_dbg(intel_dp, dp_phy,
			       ">=5.4/6.48 Gbps link rate without source TPS3 support\n");
		if (!sink_tps3)
			lt_dbg(intel_dp, dp_phy,
			       ">=5.4/6.48 Gbps link rate without sink TPS3 support\n");
	}

	return DP_TRAINING_PATTERN_2;
}

/*
 * Perform the link training channel equalization phase on the given DP PHY
 * using one of training pattern 2, 3 or 4 depending on the source and
 * sink capabilities.
 */
static bool
intel_dp_link_training_channel_equalization(struct intel_dp *intel_dp,
					    const struct intel_crtc_state *crtc_state,
					    enum drm_dp_phy dp_phy)
{
	int tries;
	u32 training_pattern;
	u8 link_status[DP_LINK_STATUS_SIZE];
	bool channel_eq = false;
	int delay_us;

	delay_us = drm_dp_read_channel_eq_delay(&intel_dp->aux,
						intel_dp->dpcd, dp_phy,
						intel_dp_is_uhbr(crtc_state));

	training_pattern = intel_dp_training_pattern(intel_dp, crtc_state, dp_phy);
	/* Scrambling is disabled for TPS2/3 and enabled for TPS4 */
	if (training_pattern != DP_TRAINING_PATTERN_4)
		training_pattern |= DP_LINK_SCRAMBLING_DISABLE;

	/* channel equalization */
	if (!intel_dp_set_link_train(intel_dp, crtc_state, dp_phy,
				     training_pattern)) {
		lt_err(intel_dp, dp_phy, "Failed to start channel equalization\n");
		return false;
	}

	for (tries = 0; tries < 5; tries++) {
		fsleep(delay_us);

		if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, dp_phy,
						     link_status) < 0) {
			lt_err(intel_dp, dp_phy, "Failed to get link status\n");
			break;
		}

		/* Make sure clock is still ok */
		if (!drm_dp_clock_recovery_ok(link_status,
					      crtc_state->lane_count)) {
			intel_dp_dump_link_status(intel_dp, dp_phy, link_status);
			lt_dbg(intel_dp, dp_phy,
			       "Clock recovery check failed, cannot continue channel equalization\n");
			break;
		}

		if (drm_dp_channel_eq_ok(link_status,
					 crtc_state->lane_count)) {
			channel_eq = true;
			lt_dbg(intel_dp, dp_phy, "Channel EQ done. DP Training successful\n");
			break;
		}

		/* Update training set as requested by target */
		intel_dp_get_adjust_train(intel_dp, crtc_state, dp_phy,
					  link_status);
		if (!intel_dp_update_link_train(intel_dp, crtc_state, dp_phy)) {
			lt_err(intel_dp, dp_phy, "Failed to update link training\n");
			break;
		}
	}

	/* Try 5 times, else fail and try at lower BW */
	if (tries == 5) {
		intel_dp_dump_link_status(intel_dp, dp_phy, link_status);
		lt_dbg(intel_dp, dp_phy, "Channel equalization failed 5 times\n");
	}

	return channel_eq;
}

static bool intel_dp_disable_dpcd_training_pattern(struct intel_dp *intel_dp,
						   enum drm_dp_phy dp_phy)
{
	int reg = intel_dp_training_pattern_set_reg(intel_dp, dp_phy);
	u8 val = DP_TRAINING_PATTERN_DISABLE;

	return drm_dp_dpcd_write(&intel_dp->aux, reg, &val, 1) == 1;
}

static int
intel_dp_128b132b_intra_hop(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	u8 sink_status;
	int ret;

	ret = drm_dp_dpcd_readb(&intel_dp->aux, DP_SINK_STATUS, &sink_status);
	if (ret != 1) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "Failed to read sink status\n");
		return ret < 0 ? ret : -EIO;
	}

	return sink_status & DP_INTRA_HOP_AUX_REPLY_INDICATION ? 1 : 0;
}

/**
 * intel_dp_stop_link_train - stop link training
 * @intel_dp: DP struct
 * @crtc_state: state for CRTC attached to the encoder
 *
 * Stop the link training of the @intel_dp port, disabling the training
 * pattern in the sink's DPCD, and disabling the test pattern symbol
 * generation on the port.
 *
 * What symbols are output on the port after this point is
 * platform specific: On DDI/VLV/CHV platforms it will be the idle pattern
 * with the pipe being disabled, on older platforms it's HW specific if/how an
 * idle pattern is generated, as the pipe is already enabled here for those.
 *
 * This function must be called after intel_dp_start_link_train().
 */
void intel_dp_stop_link_train(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state)
{
	intel_dp->link_trained = true;

	intel_dp_disable_dpcd_training_pattern(intel_dp, DP_PHY_DPRX);
	intel_dp_program_link_training_pattern(intel_dp, crtc_state, DP_PHY_DPRX,
					       DP_TRAINING_PATTERN_DISABLE);

	if (intel_dp_is_uhbr(crtc_state) &&
	    wait_for(intel_dp_128b132b_intra_hop(intel_dp, crtc_state) == 0, 500)) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "128b/132b intra-hop not clearing\n");
	}
}

static bool
intel_dp_link_train_phy(struct intel_dp *intel_dp,
			const struct intel_crtc_state *crtc_state,
			enum drm_dp_phy dp_phy)
{
	bool ret = false;

	if (!intel_dp_link_training_clock_recovery(intel_dp, crtc_state, dp_phy))
		goto out;

	if (!intel_dp_link_training_channel_equalization(intel_dp, crtc_state, dp_phy))
		goto out;

	ret = true;

out:
	lt_dbg(intel_dp, dp_phy,
	       "Link Training %s at link rate = %d, lane count = %d\n",
	       ret ? "passed" : "failed",
	       crtc_state->port_clock, crtc_state->lane_count);

	return ret;
}

static bool intel_dp_can_link_train_fallback_for_edp(struct intel_dp *intel_dp,
						     int link_rate,
						     u8 lane_count)
{
	/* FIXME figure out what we actually want here */
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(intel_dp->attached_connector);
	int mode_rate, max_rate;

	mode_rate = intel_dp_link_required(fixed_mode->clock, 18);
	max_rate = intel_dp_max_link_data_rate(intel_dp, link_rate, lane_count);
	if (mode_rate > max_rate)
		return false;

	return true;
}

static bool reduce_link_params_in_bw_order(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state,
					   int *new_link_rate, int *new_lane_count)
{
	int link_rate;
	int lane_count;
	int i;

	i = intel_dp_link_config_index(intel_dp, crtc_state->port_clock, crtc_state->lane_count);
	for (i--; i >= 0; i--) {
		intel_dp_link_config_get(intel_dp, i, &link_rate, &lane_count);

		if ((intel_dp->link.force_rate &&
		     intel_dp->link.force_rate != link_rate) ||
		    (intel_dp->link.force_lane_count &&
		     intel_dp->link.force_lane_count != lane_count))
			continue;

		break;
	}

	if (i < 0)
		return false;

	*new_link_rate = link_rate;
	*new_lane_count = lane_count;

	return true;
}

static int reduce_link_rate(struct intel_dp *intel_dp, int current_rate)
{
	int rate_index;
	int new_rate;

	if (intel_dp->link.force_rate)
		return -1;

	rate_index = intel_dp_rate_index(intel_dp->common_rates,
					 intel_dp->num_common_rates,
					 current_rate);

	if (rate_index <= 0)
		return -1;

	new_rate = intel_dp_common_rate(intel_dp, rate_index - 1);

	/* TODO: Make switching from UHBR to non-UHBR rates work. */
	if (drm_dp_is_uhbr_rate(current_rate) != drm_dp_is_uhbr_rate(new_rate))
		return -1;

	return new_rate;
}

static int reduce_lane_count(struct intel_dp *intel_dp, int current_lane_count)
{
	if (intel_dp->link.force_lane_count)
		return -1;

	if (current_lane_count == 1)
		return -1;

	return current_lane_count >> 1;
}

static bool reduce_link_params_in_rate_lane_order(struct intel_dp *intel_dp,
						  const struct intel_crtc_state *crtc_state,
						  int *new_link_rate, int *new_lane_count)
{
	int link_rate;
	int lane_count;

	lane_count = crtc_state->lane_count;
	link_rate = reduce_link_rate(intel_dp, crtc_state->port_clock);
	if (link_rate < 0) {
		lane_count = reduce_lane_count(intel_dp, crtc_state->lane_count);
		link_rate = intel_dp_max_common_rate(intel_dp);
	}

	if (lane_count < 0)
		return false;

	*new_link_rate = link_rate;
	*new_lane_count = lane_count;

	return true;
}

static bool reduce_link_params(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state,
			       int *new_link_rate, int *new_lane_count)
{
	/* TODO: Use the same fallback logic on SST as on MST. */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		return reduce_link_params_in_bw_order(intel_dp, crtc_state,
						      new_link_rate, new_lane_count);
	else
		return reduce_link_params_in_rate_lane_order(intel_dp, crtc_state,
							     new_link_rate, new_lane_count);
}

static int intel_dp_get_link_train_fallback_values(struct intel_dp *intel_dp,
						   const struct intel_crtc_state *crtc_state)
{
	int new_link_rate;
	int new_lane_count;

	if (intel_dp_is_edp(intel_dp) && !intel_dp->use_max_params) {
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Retrying Link training for eDP with max parameters\n");
		intel_dp->use_max_params = true;
		return 0;
	}

	if (!reduce_link_params(intel_dp, crtc_state, &new_link_rate, &new_lane_count))
		return -1;

	if (intel_dp_is_edp(intel_dp) &&
	    !intel_dp_can_link_train_fallback_for_edp(intel_dp, new_link_rate, new_lane_count)) {
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Retrying Link training for eDP with same parameters\n");
		return 0;
	}

	lt_dbg(intel_dp, DP_PHY_DPRX,
	       "Reducing link parameters from %dx%d to %dx%d\n",
	       crtc_state->lane_count, crtc_state->port_clock,
	       new_lane_count, new_link_rate);

	intel_dp->link.max_rate = new_link_rate;
	intel_dp->link.max_lane_count = new_lane_count;

	return 0;
}

static bool intel_dp_schedule_fallback_link_training(struct intel_atomic_state *state,
						     struct intel_dp *intel_dp,
						     const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;

	if (!intel_digital_port_connected(&dp_to_dig_port(intel_dp)->base)) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "Link Training failed on disconnected sink.\n");
		return true;
	}

	if (intel_dp->hobl_active) {
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Link Training failed with HOBL active, not enabling it from now on\n");
		intel_dp->hobl_failed = true;
	} else if (intel_dp_get_link_train_fallback_values(intel_dp, crtc_state)) {
		return false;
	}

	/* Schedule a Hotplug Uevent to userspace to start modeset */
	intel_dp_queue_modeset_retry_for_link(state, encoder, crtc_state);

	return true;
}

/* Perform the link training on all LTTPRs and the DPRX on a link. */
static bool
intel_dp_link_train_all_phys(struct intel_dp *intel_dp,
			     const struct intel_crtc_state *crtc_state,
			     int lttpr_count)
{
	bool ret = true;
	int i;

	for (i = lttpr_count - 1; i >= 0; i--) {
		enum drm_dp_phy dp_phy = DP_PHY_LTTPR(i);

		ret = intel_dp_link_train_phy(intel_dp, crtc_state, dp_phy);
		intel_dp_disable_dpcd_training_pattern(intel_dp, dp_phy);

		if (!ret)
			break;
	}

	if (ret)
		ret = intel_dp_link_train_phy(intel_dp, crtc_state, DP_PHY_DPRX);

	if (intel_dp->set_idle_link_train)
		intel_dp->set_idle_link_train(intel_dp, crtc_state);

	return ret;
}

/*
 * 128b/132b DP LANEx_EQ_DONE Sequence (DP 2.0 E11 3.5.2.16.1)
 */
static bool
intel_dp_128b132b_lane_eq(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	int delay_us;
	int try, max_tries = 20;
	unsigned long deadline;
	bool timeout = false;

	/*
	 * Reset signal levels. Start transmitting 128b/132b TPS1.
	 *
	 * Put DPRX and LTTPRs (if any) into intra-hop AUX mode by writing TPS1
	 * in DP_TRAINING_PATTERN_SET.
	 */
	if (!intel_dp_reset_link_train(intel_dp, crtc_state, DP_PHY_DPRX,
				       DP_TRAINING_PATTERN_1)) {
		lt_err(intel_dp, DP_PHY_DPRX, "Failed to start 128b/132b TPS1\n");
		return false;
	}

	delay_us = drm_dp_128b132b_read_aux_rd_interval(&intel_dp->aux);

	/* Read the initial TX FFE settings. */
	if (drm_dp_dpcd_read_link_status(&intel_dp->aux, link_status) < 0) {
		lt_err(intel_dp, DP_PHY_DPRX, "Failed to read TX FFE presets\n");
		return false;
	}

	/* Update signal levels and training set as requested. */
	intel_dp_get_adjust_train(intel_dp, crtc_state, DP_PHY_DPRX, link_status);
	if (!intel_dp_update_link_train(intel_dp, crtc_state, DP_PHY_DPRX)) {
		lt_err(intel_dp, DP_PHY_DPRX, "Failed to set initial TX FFE settings\n");
		return false;
	}

	/* Start transmitting 128b/132b TPS2. */
	if (!intel_dp_set_link_train(intel_dp, crtc_state, DP_PHY_DPRX,
				     DP_TRAINING_PATTERN_2)) {
		lt_err(intel_dp, DP_PHY_DPRX, "Failed to start 128b/132b TPS2\n");
		return false;
	}

	/* Time budget for the LANEx_EQ_DONE Sequence */
	deadline = jiffies + msecs_to_jiffies_timeout(400);

	for (try = 0; try < max_tries; try++) {
		fsleep(delay_us);

		/*
		 * The delay may get updated. The transmitter shall read the
		 * delay before link status during link training.
		 */
		delay_us = drm_dp_128b132b_read_aux_rd_interval(&intel_dp->aux);

		if (drm_dp_dpcd_read_link_status(&intel_dp->aux, link_status) < 0) {
			lt_err(intel_dp, DP_PHY_DPRX, "Failed to read link status\n");
			return false;
		}

		if (drm_dp_128b132b_link_training_failed(link_status)) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX,
			       "Downstream link training failure\n");
			return false;
		}

		if (drm_dp_128b132b_lane_channel_eq_done(link_status, crtc_state->lane_count)) {
			lt_dbg(intel_dp, DP_PHY_DPRX, "Lane channel eq done\n");
			break;
		}

		if (timeout) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX, "Lane channel eq timeout\n");
			return false;
		}

		if (time_after(jiffies, deadline))
			timeout = true; /* try one last time after deadline */

		/* Update signal levels and training set as requested. */
		intel_dp_get_adjust_train(intel_dp, crtc_state, DP_PHY_DPRX, link_status);
		if (!intel_dp_update_link_train(intel_dp, crtc_state, DP_PHY_DPRX)) {
			lt_err(intel_dp, DP_PHY_DPRX, "Failed to update TX FFE settings\n");
			return false;
		}
	}

	if (try == max_tries) {
		intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
		lt_err(intel_dp, DP_PHY_DPRX, "Max loop count reached\n");
		return false;
	}

	for (;;) {
		if (time_after(jiffies, deadline))
			timeout = true; /* try one last time after deadline */

		if (drm_dp_dpcd_read_link_status(&intel_dp->aux, link_status) < 0) {
			lt_err(intel_dp, DP_PHY_DPRX, "Failed to read link status\n");
			return false;
		}

		if (drm_dp_128b132b_link_training_failed(link_status)) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX, "Downstream link training failure\n");
			return false;
		}

		if (drm_dp_128b132b_eq_interlane_align_done(link_status)) {
			lt_dbg(intel_dp, DP_PHY_DPRX, "Interlane align done\n");
			break;
		}

		if (timeout) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX, "Interlane align timeout\n");
			return false;
		}

		usleep_range(2000, 3000);
	}

	return true;
}

/*
 * 128b/132b DP LANEx_CDS_DONE Sequence (DP 2.0 E11 3.5.2.16.2)
 */
static bool
intel_dp_128b132b_lane_cds(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *crtc_state,
			   int lttpr_count)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	unsigned long deadline;

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_TRAINING_PATTERN_SET,
			       DP_TRAINING_PATTERN_2_CDS) != 1) {
		lt_err(intel_dp, DP_PHY_DPRX, "Failed to start 128b/132b TPS2 CDS\n");
		return false;
	}

	/* Time budget for the LANEx_CDS_DONE Sequence */
	deadline = jiffies + msecs_to_jiffies_timeout((lttpr_count + 1) * 20);

	for (;;) {
		bool timeout = false;

		if (time_after(jiffies, deadline))
			timeout = true; /* try one last time after deadline */

		usleep_range(2000, 3000);

		if (drm_dp_dpcd_read_link_status(&intel_dp->aux, link_status) < 0) {
			lt_err(intel_dp, DP_PHY_DPRX, "Failed to read link status\n");
			return false;
		}

		if (drm_dp_128b132b_eq_interlane_align_done(link_status) &&
		    drm_dp_128b132b_cds_interlane_align_done(link_status) &&
		    drm_dp_128b132b_lane_symbol_locked(link_status, crtc_state->lane_count)) {
			lt_dbg(intel_dp, DP_PHY_DPRX, "CDS interlane align done\n");
			break;
		}

		if (drm_dp_128b132b_link_training_failed(link_status)) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX, "Downstream link training failure\n");
			return false;
		}

		if (timeout) {
			intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
			lt_err(intel_dp, DP_PHY_DPRX, "CDS timeout\n");
			return false;
		}
	}

	return true;
}

/*
 * 128b/132b link training sequence. (DP 2.0 E11 SCR on link training.)
 */
static bool
intel_dp_128b132b_link_train(struct intel_dp *intel_dp,
			     const struct intel_crtc_state *crtc_state,
			     int lttpr_count)
{
	bool passed = false;

	if (wait_for(intel_dp_128b132b_intra_hop(intel_dp, crtc_state) == 0, 500)) {
		lt_err(intel_dp, DP_PHY_DPRX, "128b/132b intra-hop not clear\n");
		return false;
	}

	if (intel_dp_128b132b_lane_eq(intel_dp, crtc_state) &&
	    intel_dp_128b132b_lane_cds(intel_dp, crtc_state, lttpr_count))
		passed = true;

	lt_dbg(intel_dp, DP_PHY_DPRX,
	       "128b/132b Link Training %s at link rate = %d, lane count = %d\n",
	       passed ? "passed" : "failed",
	       crtc_state->port_clock, crtc_state->lane_count);

	return passed;
}

/**
 * intel_dp_start_link_train - start link training
 * @state: Atomic state
 * @intel_dp: DP struct
 * @crtc_state: state for CRTC attached to the encoder
 *
 * Start the link training of the @intel_dp port, scheduling a fallback
 * retraining with reduced link rate/lane parameters if the link training
 * fails.
 * After calling this function intel_dp_stop_link_train() must be called.
 */
void intel_dp_start_link_train(struct intel_atomic_state *state,
			       struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	bool passed;
	/*
	 * Reinit the LTTPRs here to ensure that they are switched to
	 * non-transparent mode. During an earlier LTTPR detection this
	 * could've been prevented by an active link.
	 */
	int lttpr_count = intel_dp_init_lttpr_and_dprx_caps(intel_dp);

	if (lttpr_count < 0)
		/* Still continue with enabling the port and link training. */
		lttpr_count = 0;

	intel_dp_prepare_link_train(intel_dp, crtc_state);

	if (intel_dp_is_uhbr(crtc_state))
		passed = intel_dp_128b132b_link_train(intel_dp, crtc_state, lttpr_count);
	else
		passed = intel_dp_link_train_all_phys(intel_dp, crtc_state, lttpr_count);

	if (intel_dp->link.force_train_failure) {
		intel_dp->link.force_train_failure--;
		lt_dbg(intel_dp, DP_PHY_DPRX, "Forcing link training failure\n");
	} else if (passed) {
		intel_dp->link.seq_train_failures = 0;
		intel_encoder_link_check_queue_work(encoder, 2000);
		return;
	}

	intel_dp->link.seq_train_failures++;

	/*
	 * Ignore the link failure in CI
	 *
	 * In fixed enviroments like CI, sometimes unexpected long HPDs are
	 * generated by the displays. If ignore_long_hpd flag is set, such long
	 * HPDs are ignored. And probably as a consequence of these ignored
	 * long HPDs, subsequent link trainings are failed resulting into CI
	 * execution failures.
	 *
	 * For test cases which rely on the link training or processing of HPDs
	 * ignore_long_hpd flag can unset from the testcase.
	 */
	if (display->hotplug.ignore_long_hpd) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "Ignore the link failure\n");
		return;
	}

	if (intel_dp->link.seq_train_failures < 2) {
		intel_encoder_link_check_queue_work(encoder, 0);
		return;
	}

	if (intel_dp_schedule_fallback_link_training(state, intel_dp, crtc_state))
		return;

	intel_dp->link.retrain_disabled = true;

	if (!passed)
		lt_err(intel_dp, DP_PHY_DPRX, "Can't reduce link training parameters after failure\n");
	else
		lt_dbg(intel_dp, DP_PHY_DPRX, "Can't reduce link training parameters after forced failure\n");
}

void intel_dp_128b132b_sdp_crc16(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	/*
	 * VIDEO_DIP_CTL register bit 31 should be set to '0' to not
	 * disable SDP CRC. This is applicable for Display version 13.
	 * Default value of bit 31 is '0' hence discarding the write
	 * TODO: Corrective actions on SDP corruption yet to be defined
	 */
	if (!intel_dp_is_uhbr(crtc_state))
		return;

	/* DP v2.0 SCR on SDP CRC16 for 128b/132b Link Layer */
	drm_dp_dpcd_writeb(&intel_dp->aux,
			   DP_SDP_ERROR_DETECTION_CONFIGURATION,
			   DP_SDP_CRC16_128B132B_EN);

	lt_dbg(intel_dp, DP_PHY_DPRX, "DP2.0 SDP CRC16 for 128b/132b enabled\n");
}

static int i915_dp_force_link_rate_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int current_rate = -1;
	int force_rate;
	int err;
	int i;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	if (intel_dp->link_trained)
		current_rate = intel_dp->link_rate;
	force_rate = intel_dp->link.force_rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	seq_printf(m, "%sauto%s",
		   force_rate == 0 ? "[" : "",
		   force_rate == 0 ? "]" : "");

	for (i = 0; i < intel_dp->num_source_rates; i++)
		seq_printf(m, " %s%d%s%s",
			   intel_dp->source_rates[i] == force_rate ? "[" : "",
			   intel_dp->source_rates[i],
			   intel_dp->source_rates[i] == current_rate ? "*" : "",
			   intel_dp->source_rates[i] == force_rate ? "]" : "");

	seq_putc(m, '\n');

	return 0;
}

static int parse_link_rate(struct intel_dp *intel_dp, const char __user *ubuf, size_t len)
{
	char *kbuf;
	const char *p;
	int rate;
	int ret = 0;

	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	p = strim(kbuf);

	if (!strcmp(p, "auto")) {
		rate = 0;
	} else {
		ret = kstrtoint(p, 0, &rate);
		if (ret < 0)
			goto out_free;

		if (intel_dp_rate_index(intel_dp->source_rates,
					intel_dp->num_source_rates,
					rate) < 0)
			ret = -EINVAL;
	}

out_free:
	kfree(kbuf);

	return ret < 0 ? ret : rate;
}

static ssize_t i915_dp_force_link_rate_write(struct file *file,
					     const char __user *ubuf,
					     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int rate;
	int err;

	rate = parse_link_rate(intel_dp, ubuf, len);
	if (rate < 0)
		return rate;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_reset_link_params(intel_dp);
	intel_dp->link.force_rate = rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	*offp += len;

	return len;
}
DEFINE_SHOW_STORE_ATTRIBUTE(i915_dp_force_link_rate);

static int i915_dp_force_lane_count_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int current_lane_count = -1;
	int force_lane_count;
	int err;
	int i;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	if (intel_dp->link_trained)
		current_lane_count = intel_dp->lane_count;
	force_lane_count = intel_dp->link.force_lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	seq_printf(m, "%sauto%s",
		   force_lane_count == 0 ? "[" : "",
		   force_lane_count == 0 ? "]" : "");

	for (i = 1; i <= 4; i <<= 1)
		seq_printf(m, " %s%d%s%s",
			   i == force_lane_count ? "[" : "",
			   i,
			   i == current_lane_count ? "*" : "",
			   i == force_lane_count ? "]" : "");

	seq_putc(m, '\n');

	return 0;
}

static int parse_lane_count(const char __user *ubuf, size_t len)
{
	char *kbuf;
	const char *p;
	int lane_count;
	int ret = 0;

	kbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	p = strim(kbuf);

	if (!strcmp(p, "auto")) {
		lane_count = 0;
	} else {
		ret = kstrtoint(p, 0, &lane_count);
		if (ret < 0)
			goto out_free;

		switch (lane_count) {
		case 1:
		case 2:
		case 4:
			break;
		default:
			ret = -EINVAL;
		}
	}

out_free:
	kfree(kbuf);

	return ret < 0 ? ret : lane_count;
}

static ssize_t i915_dp_force_lane_count_write(struct file *file,
					      const char __user *ubuf,
					      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int lane_count;
	int err;

	lane_count = parse_lane_count(ubuf, len);
	if (lane_count < 0)
		return lane_count;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp_reset_link_params(intel_dp);
	intel_dp->link.force_lane_count = lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	*offp += len;

	return len;
}
DEFINE_SHOW_STORE_ATTRIBUTE(i915_dp_force_lane_count);

static int i915_dp_max_link_rate_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	*val = intel_dp->link.max_rate;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_max_link_rate_fops, i915_dp_max_link_rate_show, NULL, "%llu\n");

static int i915_dp_max_lane_count_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	*val = intel_dp->link.max_lane_count;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_max_lane_count_fops, i915_dp_max_lane_count_show, NULL, "%llu\n");

static int i915_dp_force_link_training_failure_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	*val = intel_dp->link.force_train_failure;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}

static int i915_dp_force_link_training_failure_write(void *data, u64 val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	if (val > 2)
		return -EINVAL;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp->link.force_train_failure = val;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_force_link_training_failure_fops,
			 i915_dp_force_link_training_failure_show,
			 i915_dp_force_link_training_failure_write, "%llu\n");

static int i915_dp_force_link_retrain_show(void *data, u64 *val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	*val = intel_dp->link.force_retrain;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}

static int i915_dp_force_link_retrain_write(void *data, u64 val)
{
	struct intel_connector *connector = to_intel_connector(data);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	intel_dp->link.force_retrain = val;

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	intel_hpd_trigger_irq(dp_to_dig_port(intel_dp));

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(i915_dp_force_link_retrain_fops,
			 i915_dp_force_link_retrain_show,
			 i915_dp_force_link_retrain_write, "%llu\n");

static int i915_dp_link_retrain_disabled_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = to_intel_connector(m->private);
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	int err;

	err = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (err)
		return err;

	seq_printf(m, "%s\n", str_yes_no(intel_dp->link.retrain_disabled));

	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_dp_link_retrain_disabled);

void intel_dp_link_training_debugfs_add(struct intel_connector *connector)
{
	struct dentry *root = connector->base.debugfs_entry;

	if (connector->base.connector_type != DRM_MODE_CONNECTOR_DisplayPort &&
	    connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return;

	debugfs_create_file("i915_dp_force_link_rate", 0644, root,
			    connector, &i915_dp_force_link_rate_fops);

	debugfs_create_file("i915_dp_force_lane_count", 0644, root,
			    connector, &i915_dp_force_lane_count_fops);

	debugfs_create_file("i915_dp_max_link_rate", 0444, root,
			    connector, &i915_dp_max_link_rate_fops);

	debugfs_create_file("i915_dp_max_lane_count", 0444, root,
			    connector, &i915_dp_max_lane_count_fops);

	debugfs_create_file("i915_dp_force_link_training_failure", 0644, root,
			    connector, &i915_dp_force_link_training_failure_fops);

	debugfs_create_file("i915_dp_force_link_retrain", 0644, root,
			    connector, &i915_dp_force_link_retrain_fops);

	debugfs_create_file("i915_dp_link_retrain_disabled", 0444, root,
			    connector, &i915_dp_link_retrain_disabled_fops);
}
