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

#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"

static void
intel_dp_dump_link_status(const u8 link_status[DP_LINK_STATUS_SIZE])
{

	DRM_DEBUG_KMS("ln0_1:0x%x ln2_3:0x%x align:0x%x sink:0x%x adj_req0_1:0x%x adj_req2_3:0x%x",
		      link_status[0], link_status[1], link_status[2],
		      link_status[3], link_status[4], link_status[5]);
}

static void intel_dp_reset_lttpr_common_caps(struct intel_dp *intel_dp)
{
	memset(&intel_dp->lttpr_common_caps, 0, sizeof(intel_dp->lttpr_common_caps));
}

static void intel_dp_reset_lttpr_count(struct intel_dp *intel_dp)
{
	intel_dp->lttpr_common_caps[DP_PHY_REPEATER_CNT -
				    DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV] = 0;
}

static const char *intel_dp_phy_name(enum drm_dp_phy dp_phy,
				     char *buf, size_t buf_size)
{
	if (dp_phy == DP_PHY_DPRX)
		snprintf(buf, buf_size, "DPRX");
	else
		snprintf(buf, buf_size, "LTTPR %d", dp_phy - DP_PHY_LTTPR1 + 1);

	return buf;
}

static u8 *intel_dp_lttpr_phy_caps(struct intel_dp *intel_dp,
				   enum drm_dp_phy dp_phy)
{
	return intel_dp->lttpr_phy_caps[dp_phy - DP_PHY_LTTPR1];
}

static void intel_dp_read_lttpr_phy_caps(struct intel_dp *intel_dp,
					 enum drm_dp_phy dp_phy)
{
	u8 *phy_caps = intel_dp_lttpr_phy_caps(intel_dp, dp_phy);
	char phy_name[10];

	intel_dp_phy_name(dp_phy, phy_name, sizeof(phy_name));

	if (drm_dp_read_lttpr_phy_caps(&intel_dp->aux, dp_phy, phy_caps) < 0) {
		drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
			    "failed to read the PHY caps for %s\n",
			    phy_name);
		return;
	}

	drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
		    "%s PHY capabilities: %*ph\n",
		    phy_name,
		    (int)sizeof(intel_dp->lttpr_phy_caps[0]),
		    phy_caps);
}

static bool intel_dp_read_lttpr_common_caps(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (intel_dp_is_edp(intel_dp))
		return false;

	/*
	 * Detecting LTTPRs must be avoided on platforms with an AUX timeout
	 * period < 3.2ms. (see DP Standard v2.0, 2.11.2, 3.6.6.1).
	 */
	if (INTEL_GEN(i915) < 10)
		return false;

	if (drm_dp_read_lttpr_common_caps(&intel_dp->aux,
					  intel_dp->lttpr_common_caps) < 0)
		goto reset_caps;

	drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
		    "LTTPR common capabilities: %*ph\n",
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

	return drm_dp_dpcd_write(&intel_dp->aux, DP_PHY_REPEATER_MODE, &val, 1) == 1;
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
	int lttpr_count;
	bool ret;
	int i;

	ret = intel_dp_read_lttpr_common_caps(intel_dp);

	/* The DPTX shall read the DPRX caps after LTTPR detection. */
	if (drm_dp_read_dpcd_caps(&intel_dp->aux, intel_dp->dpcd)) {
		intel_dp_reset_lttpr_common_caps(intel_dp);
		return -EIO;
	}

	if (!ret)
		return 0;

	/*
	 * The 0xF0000-0xF02FF range is only valid if the DPCD revision is
	 * at least 1.4.
	 */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x14) {
		intel_dp_reset_lttpr_common_caps(intel_dp);
		return 0;
	}

	lttpr_count = drm_dp_lttpr_count(intel_dp->lttpr_common_caps);
	/*
	 * Prevent setting LTTPR transparent mode explicitly if no LTTPRs are
	 * detected as this breaks link training at least on the Dell WD19TB
	 * dock.
	 */
	if (lttpr_count == 0)
		return 0;

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
		return 0;

	if (!intel_dp_set_lttpr_transparent_mode(intel_dp, false)) {
		drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
			    "Switching to LTTPR non-transparent LT mode failed, fall-back to transparent mode\n");

		intel_dp_set_lttpr_transparent_mode(intel_dp, true);
		intel_dp_reset_lttpr_count(intel_dp);

		return 0;
	}

	for (i = 0; i < lttpr_count; i++)
		intel_dp_read_lttpr_phy_caps(intel_dp, DP_PHY_LTTPR(i));

	return lttpr_count;
}
EXPORT_SYMBOL(intel_dp_init_lttpr_and_dprx_caps);

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
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int lttpr_count = drm_dp_lttpr_count(intel_dp->lttpr_common_caps);

	drm_WARN_ON_ONCE(&i915->drm, lttpr_count <= 0 && dp_phy != DP_PHY_DPRX);

	return lttpr_count <= 0 || dp_phy == DP_PHY_LTTPR(lttpr_count - 1);
}

static u8 intel_dp_phy_voltage_max(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state,
				   enum drm_dp_phy dp_phy)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 voltage_max;

	/*
	 * Get voltage_max from the DPTX_PHY (source or LTTPR) upstream from
	 * the DPRX_PHY we train.
	 */
	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		voltage_max = intel_dp->voltage_max(intel_dp, crtc_state);
	else
		voltage_max = intel_dp_lttpr_voltage_max(intel_dp, dp_phy + 1);

	drm_WARN_ON_ONCE(&i915->drm,
			 voltage_max != DP_TRAIN_VOLTAGE_SWING_LEVEL_2 &&
			 voltage_max != DP_TRAIN_VOLTAGE_SWING_LEVEL_3);

	return voltage_max;
}

static u8 intel_dp_phy_preemph_max(struct intel_dp *intel_dp,
				   enum drm_dp_phy dp_phy)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 preemph_max;

	/*
	 * Get preemph_max from the DPTX_PHY (source or LTTPR) upstream from
	 * the DPRX_PHY we train.
	 */
	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		preemph_max = intel_dp->preemph_max(intel_dp);
	else
		preemph_max = intel_dp_lttpr_preemph_max(intel_dp, dp_phy + 1);

	drm_WARN_ON_ONCE(&i915->drm,
			 preemph_max != DP_TRAIN_PRE_EMPH_LEVEL_2 &&
			 preemph_max != DP_TRAIN_PRE_EMPH_LEVEL_3);

	return preemph_max;
}

void
intel_dp_get_adjust_train(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state,
			  enum drm_dp_phy dp_phy,
			  const u8 link_status[DP_LINK_STATUS_SIZE])
{
	u8 v = 0;
	u8 p = 0;
	int lane;
	u8 voltage_max;
	u8 preemph_max;

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		v = max(v, drm_dp_get_adjust_request_voltage(link_status, lane));
		p = max(p, drm_dp_get_adjust_request_pre_emphasis(link_status, lane));
	}

	preemph_max = intel_dp_phy_preemph_max(intel_dp, dp_phy);
	if (p >= preemph_max)
		p = preemph_max | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	v = min(v, dp_voltage_max(p));

	voltage_max = intel_dp_phy_voltage_max(intel_dp, crtc_state, dp_phy);
	if (v >= voltage_max)
		v = voltage_max | DP_TRAIN_MAX_SWING_REACHED;

	for (lane = 0; lane < 4; lane++)
		intel_dp->train_set[lane] = v | p;
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
					       dp_train_pat);

	buf[0] = dp_train_pat;
	/* DP_TRAINING_LANEx_SET follow DP_TRAINING_PATTERN_SET */
	memcpy(buf + 1, intel_dp->train_set, crtc_state->lane_count);
	len = crtc_state->lane_count + 1;

	return drm_dp_dpcd_write(&intel_dp->aux, reg, buf, len) == len;
}

void intel_dp_set_signal_levels(struct intel_dp *intel_dp,
				const struct intel_crtc_state *crtc_state,
				enum drm_dp_phy dp_phy)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_set = intel_dp->train_set[0];
	char phy_name[10];

	drm_dbg_kms(&dev_priv->drm, "Using vswing level %d%s, pre-emphasis level %d%s, at %s\n",
		    train_set & DP_TRAIN_VOLTAGE_SWING_MASK,
		    train_set & DP_TRAIN_MAX_SWING_REACHED ? " (max)" : "",
		    (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) >>
		    DP_TRAIN_PRE_EMPHASIS_SHIFT,
		    train_set & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED ?
		    " (max)" : "",
		    intel_dp_phy_name(dp_phy, phy_name, sizeof(phy_name)));

	if (intel_dp_phy_is_downstream_of_source(intel_dp, dp_phy))
		intel_dp->set_signal_levels(intel_dp, crtc_state);
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

static bool intel_dp_link_max_vswing_reached(struct intel_dp *intel_dp,
					     const struct intel_crtc_state *crtc_state)
{
	int lane;

	for (lane = 0; lane < crtc_state->lane_count; lane++)
		if ((intel_dp->train_set[lane] &
		     DP_TRAIN_MAX_SWING_REACHED) == 0)
			return false;

	return true;
}

/*
 * Prepare link training by configuring the link parameters. On DDI platforms
 * also enable the port here.
 */
static bool
intel_dp_prepare_link_train(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 link_config[2];
	u8 link_bw, rate_select;

	if (intel_dp->prepare_link_retrain)
		intel_dp->prepare_link_retrain(intel_dp, crtc_state);

	intel_dp_compute_rate(intel_dp, crtc_state->port_clock,
			      &link_bw, &rate_select);

	if (link_bw)
		drm_dbg_kms(&i915->drm,
			    "Using LINK_BW_SET value %02x\n", link_bw);
	else
		drm_dbg_kms(&i915->drm,
			    "Using LINK_RATE_SET value %02x\n", rate_select);

	/* Write the link configuration data */
	link_config[0] = link_bw;
	link_config[1] = crtc_state->lane_count;
	if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
		link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	drm_dp_dpcd_write(&intel_dp->aux, DP_LINK_BW_SET, link_config, 2);

	/* eDP 1.4 rate select method. */
	if (!link_bw)
		drm_dp_dpcd_write(&intel_dp->aux, DP_LINK_RATE_SET,
				  &rate_select, 1);

	link_config[0] = crtc_state->vrr.enable ? DP_MSA_TIMING_PAR_IGNORE_EN : 0;
	link_config[1] = DP_SET_ANSI_8B10B;
	drm_dp_dpcd_write(&intel_dp->aux, DP_DOWNSPREAD_CTRL, link_config, 2);

	intel_dp->DP |= DP_PORT_EN;

	return true;
}

static void intel_dp_link_training_clock_recovery_delay(struct intel_dp *intel_dp,
							enum drm_dp_phy dp_phy)
{
	if (dp_phy == DP_PHY_DPRX)
		drm_dp_link_train_clock_recovery_delay(intel_dp->dpcd);
	else
		drm_dp_lttpr_link_train_clock_recovery_delay();
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
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 voltage;
	int voltage_tries, cr_tries, max_cr_tries;
	bool max_vswing_reached = false;

	/* clock recovery */
	if (!intel_dp_reset_link_train(intel_dp, crtc_state, dp_phy,
				       DP_TRAINING_PATTERN_1 |
				       DP_LINK_SCRAMBLING_DISABLE)) {
		drm_err(&i915->drm, "failed to enable link training\n");
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
		u8 link_status[DP_LINK_STATUS_SIZE];

		intel_dp_link_training_clock_recovery_delay(intel_dp, dp_phy);

		if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, dp_phy,
						     link_status) < 0) {
			drm_err(&i915->drm, "failed to get link status\n");
			return false;
		}

		if (drm_dp_clock_recovery_ok(link_status, crtc_state->lane_count)) {
			drm_dbg_kms(&i915->drm, "clock recovery OK\n");
			return true;
		}

		if (voltage_tries == 5) {
			drm_dbg_kms(&i915->drm,
				    "Same voltage tried 5 times\n");
			return false;
		}

		if (max_vswing_reached) {
			drm_dbg_kms(&i915->drm, "Max Voltage Swing reached\n");
			return false;
		}

		voltage = intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Update training set as requested by target */
		intel_dp_get_adjust_train(intel_dp, crtc_state, dp_phy,
					  link_status);
		if (!intel_dp_update_link_train(intel_dp, crtc_state, dp_phy)) {
			drm_err(&i915->drm,
				"failed to update link training\n");
			return false;
		}

		if ((intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) ==
		    voltage)
			++voltage_tries;
		else
			voltage_tries = 1;

		if (intel_dp_link_max_vswing_reached(intel_dp, crtc_state))
			max_vswing_reached = true;

	}
	drm_err(&i915->drm,
		"Failed clock recovery %d times, giving up!\n", max_cr_tries);
	return false;
}

/*
 * Pick training pattern for channel equalization. Training pattern 4 for HBR3
 * or for 1.4 devices that support it, training Pattern 3 for HBR2
 * or 1.2 devices that support it, Training Pattern 2 otherwise.
 */
static u32 intel_dp_training_pattern(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     enum drm_dp_phy dp_phy)
{
	bool source_tps3, sink_tps3, source_tps4, sink_tps4;

	/*
	 * Intel platforms that support HBR3 also support TPS4. It is mandatory
	 * for all downstream devices that support HBR3. There are no known eDP
	 * panels that support TPS4 as of Feb 2018 as per VESA eDP_v1.4b_E1
	 * specification.
	 * LTTPRs must support TPS4.
	 */
	source_tps4 = intel_dp_source_supports_hbr3(intel_dp);
	sink_tps4 = dp_phy != DP_PHY_DPRX ||
		    drm_dp_tps4_supported(intel_dp->dpcd);
	if (source_tps4 && sink_tps4) {
		return DP_TRAINING_PATTERN_4;
	} else if (crtc_state->port_clock == 810000) {
		if (!source_tps4)
			drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
				    "8.1 Gbps link rate without source HBR3/TPS4 support\n");
		if (!sink_tps4)
			drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
				    "8.1 Gbps link rate without sink TPS4 support\n");
	}
	/*
	 * Intel platforms that support HBR2 also support TPS3. TPS3 support is
	 * also mandatory for downstream devices that support HBR2. However, not
	 * all sinks follow the spec.
	 */
	source_tps3 = intel_dp_source_supports_hbr2(intel_dp);
	sink_tps3 = dp_phy != DP_PHY_DPRX ||
		    drm_dp_tps3_supported(intel_dp->dpcd);
	if (source_tps3 && sink_tps3) {
		return  DP_TRAINING_PATTERN_3;
	} else if (crtc_state->port_clock >= 540000) {
		if (!source_tps3)
			drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
				    ">=5.4/6.48 Gbps link rate without source HBR2/TPS3 support\n");
		if (!sink_tps3)
			drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
				    ">=5.4/6.48 Gbps link rate without sink TPS3 support\n");
	}

	return DP_TRAINING_PATTERN_2;
}

static void
intel_dp_link_training_channel_equalization_delay(struct intel_dp *intel_dp,
						  enum drm_dp_phy dp_phy)
{
	if (dp_phy == DP_PHY_DPRX) {
		drm_dp_link_train_channel_eq_delay(intel_dp->dpcd);
	} else {
		const u8 *phy_caps = intel_dp_lttpr_phy_caps(intel_dp, dp_phy);

		drm_dp_lttpr_link_train_channel_eq_delay(phy_caps);
	}
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
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int tries;
	u32 training_pattern;
	u8 link_status[DP_LINK_STATUS_SIZE];
	bool channel_eq = false;

	training_pattern = intel_dp_training_pattern(intel_dp, crtc_state, dp_phy);
	/* Scrambling is disabled for TPS2/3 and enabled for TPS4 */
	if (training_pattern != DP_TRAINING_PATTERN_4)
		training_pattern |= DP_LINK_SCRAMBLING_DISABLE;

	/* channel equalization */
	if (!intel_dp_set_link_train(intel_dp, crtc_state, dp_phy,
				     training_pattern)) {
		drm_err(&i915->drm, "failed to start channel equalization\n");
		return false;
	}

	for (tries = 0; tries < 5; tries++) {
		intel_dp_link_training_channel_equalization_delay(intel_dp,
								  dp_phy);
		if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, dp_phy,
						     link_status) < 0) {
			drm_err(&i915->drm,
				"failed to get link status\n");
			break;
		}

		/* Make sure clock is still ok */
		if (!drm_dp_clock_recovery_ok(link_status,
					      crtc_state->lane_count)) {
			intel_dp_dump_link_status(link_status);
			drm_dbg_kms(&i915->drm,
				    "Clock recovery check failed, cannot "
				    "continue channel equalization\n");
			break;
		}

		if (drm_dp_channel_eq_ok(link_status,
					 crtc_state->lane_count)) {
			channel_eq = true;
			drm_dbg_kms(&i915->drm, "Channel EQ done. DP Training "
				    "successful\n");
			break;
		}

		/* Update training set as requested by target */
		intel_dp_get_adjust_train(intel_dp, crtc_state, dp_phy,
					  link_status);
		if (!intel_dp_update_link_train(intel_dp, crtc_state, dp_phy)) {
			drm_err(&i915->drm,
				"failed to update link training\n");
			break;
		}
	}

	/* Try 5 times, else fail and try at lower BW */
	if (tries == 5) {
		intel_dp_dump_link_status(link_status);
		drm_dbg_kms(&i915->drm,
			    "Channel equalization failed 5 times\n");
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
	intel_dp_program_link_training_pattern(intel_dp, crtc_state,
					       DP_TRAINING_PATTERN_DISABLE);
}

static bool
intel_dp_link_train_phy(struct intel_dp *intel_dp,
			const struct intel_crtc_state *crtc_state,
			enum drm_dp_phy dp_phy)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	char phy_name[10];
	bool ret = false;

	if (!intel_dp_link_training_clock_recovery(intel_dp, crtc_state, dp_phy))
		goto out;

	if (!intel_dp_link_training_channel_equalization(intel_dp, crtc_state, dp_phy))
		goto out;

	ret = true;

out:
	drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
		    "[CONNECTOR:%d:%s] Link Training %s at link rate = %d, lane count = %d, at %s",
		    intel_connector->base.base.id,
		    intel_connector->base.name,
		    ret ? "passed" : "failed",
		    crtc_state->port_clock, crtc_state->lane_count,
		    intel_dp_phy_name(dp_phy, phy_name, sizeof(phy_name)));

	return ret;
}

static void intel_dp_schedule_fallback_link_training(struct intel_dp *intel_dp,
						     const struct intel_crtc_state *crtc_state)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;

	if (intel_dp->hobl_active) {
		drm_dbg_kms(&dp_to_i915(intel_dp)->drm,
			    "Link Training failed with HOBL active, not enabling it from now on");
		intel_dp->hobl_failed = true;
	} else if (intel_dp_get_link_train_fallback_values(intel_dp,
							   crtc_state->port_clock,
							   crtc_state->lane_count)) {
		return;
	}

	/* Schedule a Hotplug Uevent to userspace to start modeset */
	schedule_work(&intel_connector->modeset_retry_work);
}

/* Perform the link training on all LTTPRs and the DPRX on a link. */
static bool
intel_dp_link_train_all_phys(struct intel_dp *intel_dp,
			     const struct intel_crtc_state *crtc_state,
			     int lttpr_count)
{
	bool ret = true;
	int i;

	intel_dp_prepare_link_train(intel_dp, crtc_state);

	for (i = lttpr_count - 1; i >= 0; i--) {
		enum drm_dp_phy dp_phy = DP_PHY_LTTPR(i);

		ret = intel_dp_link_train_phy(intel_dp, crtc_state, dp_phy);
		intel_dp_disable_dpcd_training_pattern(intel_dp, dp_phy);

		if (!ret)
			break;
	}

	if (ret)
		intel_dp_link_train_phy(intel_dp, crtc_state, DP_PHY_DPRX);

	if (intel_dp->set_idle_link_train)
		intel_dp->set_idle_link_train(intel_dp, crtc_state);

	return ret;
}

/**
 * intel_dp_start_link_train - start link training
 * @intel_dp: DP struct
 * @crtc_state: state for CRTC attached to the encoder
 *
 * Start the link training of the @intel_dp port, scheduling a fallback
 * retraining with reduced link rate/lane parameters if the link training
 * fails.
 * After calling this function intel_dp_stop_link_train() must be called.
 */
void intel_dp_start_link_train(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	/*
	 * TODO: Reiniting LTTPRs here won't be needed once proper connector
	 * HW state readout is added.
	 */
	int lttpr_count = intel_dp_init_lttpr_and_dprx_caps(intel_dp);

	if (lttpr_count < 0)
		/* Still continue with enabling the port and link training. */
		lttpr_count = 0;

	if (!intel_dp_link_train_all_phys(intel_dp, crtc_state, lttpr_count))
		intel_dp_schedule_fallback_link_training(intel_dp, crtc_state);
}
