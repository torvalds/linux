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

#include "intel_drv.h"

static void
intel_dp_dump_link_status(const uint8_t link_status[DP_LINK_STATUS_SIZE])
{

	DRM_DEBUG_KMS("ln0_1:0x%x ln2_3:0x%x align:0x%x sink:0x%x adj_req0_1:0x%x adj_req2_3:0x%x",
		      link_status[0], link_status[1], link_status[2],
		      link_status[3], link_status[4], link_status[5]);
}

static void
intel_get_adjust_train(struct intel_dp *intel_dp,
		       const uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	uint8_t v = 0;
	uint8_t p = 0;
	int lane;
	uint8_t voltage_max;
	uint8_t preemph_max;

	for (lane = 0; lane < intel_dp->lane_count; lane++) {
		uint8_t this_v = drm_dp_get_adjust_request_voltage(link_status, lane);
		uint8_t this_p = drm_dp_get_adjust_request_pre_emphasis(link_status, lane);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}

	voltage_max = intel_dp_voltage_max(intel_dp);
	if (v >= voltage_max)
		v = voltage_max | DP_TRAIN_MAX_SWING_REACHED;

	preemph_max = intel_dp_pre_emphasis_max(intel_dp, v);
	if (p >= preemph_max)
		p = preemph_max | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (lane = 0; lane < 4; lane++)
		intel_dp->train_set[lane] = v | p;
}

static bool
intel_dp_set_link_train(struct intel_dp *intel_dp,
			uint8_t dp_train_pat)
{
	uint8_t buf[sizeof(intel_dp->train_set) + 1];
	int ret, len;

	intel_dp_program_link_training_pattern(intel_dp, dp_train_pat);

	buf[0] = dp_train_pat;
	if ((dp_train_pat & DP_TRAINING_PATTERN_MASK) ==
	    DP_TRAINING_PATTERN_DISABLE) {
		/* don't write DP_TRAINING_LANEx_SET on disable */
		len = 1;
	} else {
		/* DP_TRAINING_LANEx_SET follow DP_TRAINING_PATTERN_SET */
		memcpy(buf + 1, intel_dp->train_set, intel_dp->lane_count);
		len = intel_dp->lane_count + 1;
	}

	ret = drm_dp_dpcd_write(&intel_dp->aux, DP_TRAINING_PATTERN_SET,
				buf, len);

	return ret == len;
}

static bool
intel_dp_reset_link_train(struct intel_dp *intel_dp,
			uint8_t dp_train_pat)
{
	memset(intel_dp->train_set, 0, sizeof(intel_dp->train_set));
	intel_dp_set_signal_levels(intel_dp);
	return intel_dp_set_link_train(intel_dp, dp_train_pat);
}

static bool
intel_dp_update_link_train(struct intel_dp *intel_dp)
{
	int ret;

	intel_dp_set_signal_levels(intel_dp);

	ret = drm_dp_dpcd_write(&intel_dp->aux, DP_TRAINING_LANE0_SET,
				intel_dp->train_set, intel_dp->lane_count);

	return ret == intel_dp->lane_count;
}

static bool intel_dp_link_max_vswing_reached(struct intel_dp *intel_dp)
{
	int lane;

	for (lane = 0; lane < intel_dp->lane_count; lane++)
		if ((intel_dp->train_set[lane] &
		     DP_TRAIN_MAX_SWING_REACHED) == 0)
			return false;

	return true;
}

/* Enable corresponding port and start training pattern 1 */
static bool
intel_dp_link_training_clock_recovery(struct intel_dp *intel_dp)
{
	uint8_t voltage;
	int voltage_tries, max_vswing_tries;
	uint8_t link_config[2];
	uint8_t link_bw, rate_select;

	if (intel_dp->prepare_link_retrain)
		intel_dp->prepare_link_retrain(intel_dp);

	intel_dp_compute_rate(intel_dp, intel_dp->link_rate,
			      &link_bw, &rate_select);

	/* Write the link configuration data */
	link_config[0] = link_bw;
	link_config[1] = intel_dp->lane_count;
	if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
		link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	drm_dp_dpcd_write(&intel_dp->aux, DP_LINK_BW_SET, link_config, 2);

	if (intel_dp->num_sink_rates)
		drm_dp_dpcd_write(&intel_dp->aux, DP_LINK_RATE_SET,
				  &rate_select, 1);

	link_config[0] = 0;
	link_config[1] = DP_SET_ANSI_8B10B;
	drm_dp_dpcd_write(&intel_dp->aux, DP_DOWNSPREAD_CTRL, link_config, 2);

	intel_dp->DP |= DP_PORT_EN;

	/* clock recovery */
	if (!intel_dp_reset_link_train(intel_dp,
				       DP_TRAINING_PATTERN_1 |
				       DP_LINK_SCRAMBLING_DISABLE)) {
		DRM_ERROR("failed to enable link training\n");
		return false;
	}

	voltage_tries = 1;
	max_vswing_tries = 0;
	for (;;) {
		uint8_t link_status[DP_LINK_STATUS_SIZE];

		drm_dp_link_train_clock_recovery_delay(intel_dp->dpcd);

		if (!intel_dp_get_link_status(intel_dp, link_status)) {
			DRM_ERROR("failed to get link status\n");
			return false;
		}

		if (drm_dp_clock_recovery_ok(link_status, intel_dp->lane_count)) {
			DRM_DEBUG_KMS("clock recovery OK\n");
			return true;
		}

		if (voltage_tries == 5) {
			DRM_DEBUG_KMS("Same voltage tried 5 times\n");
			return false;
		}

		if (max_vswing_tries == 1) {
			DRM_DEBUG_KMS("Max Voltage Swing reached\n");
			return false;
		}

		voltage = intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Update training set as requested by target */
		intel_get_adjust_train(intel_dp, link_status);
		if (!intel_dp_update_link_train(intel_dp)) {
			DRM_ERROR("failed to update link training\n");
			return false;
		}

		if ((intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) ==
		    voltage)
			++voltage_tries;
		else
			voltage_tries = 1;

		if (intel_dp_link_max_vswing_reached(intel_dp))
			++max_vswing_tries;

	}
}

/*
 * Pick training pattern for channel equalization. Training Pattern 3 for HBR2
 * or 1.2 devices that support it, Training Pattern 2 otherwise.
 */
static u32 intel_dp_training_pattern(struct intel_dp *intel_dp)
{
	u32 training_pattern = DP_TRAINING_PATTERN_2;
	bool source_tps3, sink_tps3;

	/*
	 * Intel platforms that support HBR2 also support TPS3. TPS3 support is
	 * also mandatory for downstream devices that support HBR2. However, not
	 * all sinks follow the spec.
	 *
	 * Due to WaDisableHBR2 SKL < B0 is the only exception where TPS3 is
	 * supported in source but still not enabled.
	 */
	source_tps3 = intel_dp_source_supports_hbr2(intel_dp);
	sink_tps3 = drm_dp_tps3_supported(intel_dp->dpcd);

	if (source_tps3 && sink_tps3) {
		training_pattern = DP_TRAINING_PATTERN_3;
	} else if (intel_dp->link_rate == 540000) {
		if (!source_tps3)
			DRM_DEBUG_KMS("5.4 Gbps link rate without source HBR2/TPS3 support\n");
		if (!sink_tps3)
			DRM_DEBUG_KMS("5.4 Gbps link rate without sink TPS3 support\n");
	}

	return training_pattern;
}

static bool
intel_dp_link_training_channel_equalization(struct intel_dp *intel_dp)
{
	int tries;
	u32 training_pattern;
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	training_pattern = intel_dp_training_pattern(intel_dp);

	/* channel equalization */
	if (!intel_dp_set_link_train(intel_dp,
				     training_pattern |
				     DP_LINK_SCRAMBLING_DISABLE)) {
		DRM_ERROR("failed to start channel equalization\n");
		return false;
	}

	intel_dp->channel_eq_status = false;
	for (tries = 0; tries < 5; tries++) {

		drm_dp_link_train_channel_eq_delay(intel_dp->dpcd);
		if (!intel_dp_get_link_status(intel_dp, link_status)) {
			DRM_ERROR("failed to get link status\n");
			break;
		}

		/* Make sure clock is still ok */
		if (!drm_dp_clock_recovery_ok(link_status,
					      intel_dp->lane_count)) {
			intel_dp_dump_link_status(link_status);
			DRM_DEBUG_KMS("Clock recovery check failed, cannot "
				      "continue channel equalization\n");
			break;
		}

		if (drm_dp_channel_eq_ok(link_status,
					 intel_dp->lane_count)) {
			intel_dp->channel_eq_status = true;
			DRM_DEBUG_KMS("Channel EQ done. DP Training "
				      "successful\n");
			break;
		}

		/* Update training set as requested by target */
		intel_get_adjust_train(intel_dp, link_status);
		if (!intel_dp_update_link_train(intel_dp)) {
			DRM_ERROR("failed to update link training\n");
			break;
		}
	}

	/* Try 5 times, else fail and try at lower BW */
	if (tries == 5) {
		intel_dp_dump_link_status(link_status);
		DRM_DEBUG_KMS("Channel equalization failed 5 times\n");
	}

	intel_dp_set_idle_link_train(intel_dp);

	return intel_dp->channel_eq_status;

}

void intel_dp_stop_link_train(struct intel_dp *intel_dp)
{
	intel_dp_set_link_train(intel_dp,
				DP_TRAINING_PATTERN_DISABLE);
}

void
intel_dp_start_link_train(struct intel_dp *intel_dp)
{
	intel_dp_link_training_clock_recovery(intel_dp);
	intel_dp_link_training_channel_equalization(intel_dp);
}
