// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/phy/phy.h>

#include "cdn-dp-core.h"
#include "cdn-dp-reg.h"

static void cdn_dp_set_signal_levels(struct cdn_dp_device *dp)
{
	struct cdn_dp_port *port = dp->port[dp->active_port];
	int rate = drm_dp_bw_code_to_link_rate(dp->link.rate);
	u8 swing = (dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) >>
		   DP_TRAIN_VOLTAGE_SWING_SHIFT;
	u8 pre_emphasis = (dp->train_set[0] & DP_TRAIN_PRE_EMPHASIS_MASK)
			  >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

	tcphy_dp_set_phy_config(port->phy, rate, dp->link.num_lanes,
				swing, pre_emphasis);
}

static int cdn_dp_set_pattern(struct cdn_dp_device *dp, uint8_t dp_train_pat)
{
	u32 phy_config, global_config;
	int ret;
	uint8_t pattern = dp_train_pat & DP_TRAINING_PATTERN_MASK;

	global_config = NUM_LANES(dp->link.num_lanes - 1) | SST_MODE |
			GLOBAL_EN | RG_EN | ENC_RST_DIS | WR_VHSYNC_FALL;

	phy_config = DP_TX_PHY_ENCODER_BYPASS(0) |
		     DP_TX_PHY_SKEW_BYPASS(0) |
		     DP_TX_PHY_DISPARITY_RST(0) |
		     DP_TX_PHY_LANE0_SKEW(0) |
		     DP_TX_PHY_LANE1_SKEW(1) |
		     DP_TX_PHY_LANE2_SKEW(2) |
		     DP_TX_PHY_LANE3_SKEW(3) |
		     DP_TX_PHY_10BIT_ENABLE(0);

	if (pattern != DP_TRAINING_PATTERN_DISABLE) {
		global_config |= NO_VIDEO;
		phy_config |= DP_TX_PHY_TRAINING_ENABLE(1) |
			      DP_TX_PHY_SCRAMBLER_BYPASS(1) |
			      DP_TX_PHY_TRAINING_PATTERN(pattern);
	}

	ret = cdn_dp_reg_write(dp, DP_FRAMER_GLOBAL_CONFIG, global_config);
	if (ret) {
		DRM_ERROR("fail to set DP_FRAMER_GLOBAL_CONFIG, error: %d\n",
			  ret);
		return ret;
	}

	ret = cdn_dp_reg_write(dp, DP_TX_PHY_CONFIG_REG, phy_config);
	if (ret) {
		DRM_ERROR("fail to set DP_TX_PHY_CONFIG_REG, error: %d\n",
			  ret);
		return ret;
	}

	ret = cdn_dp_reg_write(dp, DPTX_LANE_EN, BIT(dp->link.num_lanes) - 1);
	if (ret) {
		DRM_ERROR("fail to set DPTX_LANE_EN, error: %d\n", ret);
		return ret;
	}

	if (drm_dp_enhanced_frame_cap(dp->dpcd) ||
	    /*
	     * A setting of 1 indicates that this is an eDP device that uses
	     * only Enhanced Framing, independently of the setting by the
	     * source of ENHANCED_FRAME_EN
	     */
	    dp->dpcd[DP_EDP_CONFIGURATION_CAP] & DP_FRAMING_CHANGE_CAP)
		ret = cdn_dp_reg_write(dp, DPTX_ENHNCD, 1);
	else
		ret = cdn_dp_reg_write(dp, DPTX_ENHNCD, 0);
	if (ret)
		DRM_ERROR("failed to set DPTX_ENHNCD, error: %x\n", ret);

	return ret;
}

static u8 cdn_dp_pre_emphasis_max(u8 voltage_swing)
{
	switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
		return DP_TRAIN_PRE_EMPH_LEVEL_3;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
		return DP_TRAIN_PRE_EMPH_LEVEL_2;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
		return DP_TRAIN_PRE_EMPH_LEVEL_1;
	default:
		return DP_TRAIN_PRE_EMPH_LEVEL_0;
	}
}

static void cdn_dp_get_adjust_train(struct cdn_dp_device *dp,
				    uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	int i;
	uint8_t v = 0, p = 0;
	uint8_t preemph_max;

	for (i = 0; i < dp->link.num_lanes; i++) {
		v = max(v, drm_dp_get_adjust_request_voltage(link_status, i));
		p = max(p, drm_dp_get_adjust_request_pre_emphasis(link_status,
								  i));
	}

	if (v >= VOLTAGE_LEVEL_2)
		v = VOLTAGE_LEVEL_2 | DP_TRAIN_MAX_SWING_REACHED;

	preemph_max = cdn_dp_pre_emphasis_max(v);
	if (p >= preemph_max)
		p = preemph_max | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (i = 0; i < dp->link.num_lanes; i++)
		dp->train_set[i] = v | p;
}

/*
 * Pick training pattern for channel equalization. Training Pattern 3 for HBR2
 * or 1.2 devices that support it, Training Pattern 2 otherwise.
 */
static u32 cdn_dp_select_chaneq_pattern(struct cdn_dp_device *dp)
{
	u32 training_pattern = DP_TRAINING_PATTERN_2;

	/*
	 * cdn dp support HBR2 also support TPS3. TPS3 support is also mandatory
	 * for downstream devices that support HBR2. However, not all sinks
	 * follow the spec.
	 */
	if (drm_dp_tps3_supported(dp->dpcd))
		training_pattern = DP_TRAINING_PATTERN_3;
	else
		DRM_DEBUG_KMS("5.4 Gbps link rate without sink TPS3 support\n");

	return training_pattern;
}


static bool cdn_dp_link_max_vswing_reached(struct cdn_dp_device *dp)
{
	int lane;

	for (lane = 0; lane < dp->link.num_lanes; lane++)
		if ((dp->train_set[lane] & DP_TRAIN_MAX_SWING_REACHED) == 0)
			return false;

	return true;
}

static int cdn_dp_update_link_train(struct cdn_dp_device *dp)
{
	int ret;

	cdn_dp_set_signal_levels(dp);

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
				dp->train_set, dp->link.num_lanes);
	if (ret != dp->link.num_lanes)
		return -EINVAL;

	return 0;
}

static int cdn_dp_set_link_train(struct cdn_dp_device *dp,
				  uint8_t dp_train_pat)
{
	uint8_t buf[sizeof(dp->train_set) + 1];
	int ret, len;

	buf[0] = dp_train_pat;
	if ((dp_train_pat & DP_TRAINING_PATTERN_MASK) ==
	    DP_TRAINING_PATTERN_DISABLE) {
		/* don't write DP_TRAINING_LANEx_SET on disable */
		len = 1;
	} else {
		/* DP_TRAINING_LANEx_SET follow DP_TRAINING_PATTERN_SET */
		memcpy(buf + 1, dp->train_set, dp->link.num_lanes);
		len = dp->link.num_lanes + 1;
	}

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET,
				buf, len);
	if (ret != len)
		return -EINVAL;

	return 0;
}

static int cdn_dp_reset_link_train(struct cdn_dp_device *dp,
				    uint8_t dp_train_pat)
{
	int ret;

	memset(dp->train_set, 0, sizeof(dp->train_set));

	cdn_dp_set_signal_levels(dp);

	ret = cdn_dp_set_pattern(dp, dp_train_pat);
	if (ret)
		return ret;

	return cdn_dp_set_link_train(dp, dp_train_pat);
}

/* Enable corresponding port and start training pattern 1 */
static int cdn_dp_link_training_clock_recovery(struct cdn_dp_device *dp)
{
	u8 voltage;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u32 voltage_tries, max_vswing_tries;
	int ret;

	/* clock recovery */
	ret = cdn_dp_reset_link_train(dp, DP_TRAINING_PATTERN_1 |
					  DP_LINK_SCRAMBLING_DISABLE);
	if (ret) {
		DRM_ERROR("failed to start link train\n");
		return ret;
	}

	voltage_tries = 1;
	max_vswing_tries = 0;
	for (;;) {
		drm_dp_link_train_clock_recovery_delay(dp->dpcd);
		if (drm_dp_dpcd_read_link_status(&dp->aux, link_status) !=
		    DP_LINK_STATUS_SIZE) {
			DRM_ERROR("failed to get link status\n");
			return -EINVAL;
		}

		if (drm_dp_clock_recovery_ok(link_status, dp->link.num_lanes)) {
			DRM_DEBUG_KMS("clock recovery OK\n");
			return 0;
		}

		if (voltage_tries >= 5) {
			DRM_DEBUG_KMS("Same voltage tried 5 times\n");
			return -EINVAL;
		}

		if (max_vswing_tries >= 1) {
			DRM_DEBUG_KMS("Max Voltage Swing reached\n");
			return -EINVAL;
		}

		voltage = dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Update training set as requested by target */
		cdn_dp_get_adjust_train(dp, link_status);
		if (cdn_dp_update_link_train(dp)) {
			DRM_ERROR("failed to update link training\n");
			return -EINVAL;
		}

		if ((dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) ==
		    voltage)
			++voltage_tries;
		else
			voltage_tries = 1;

		if (cdn_dp_link_max_vswing_reached(dp))
			++max_vswing_tries;
	}
}

static int cdn_dp_link_training_channel_equalization(struct cdn_dp_device *dp)
{
	int tries, ret;
	u32 training_pattern;
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	training_pattern = cdn_dp_select_chaneq_pattern(dp);
	training_pattern |= DP_LINK_SCRAMBLING_DISABLE;

	ret = cdn_dp_set_pattern(dp, training_pattern);
	if (ret)
		return ret;

	ret = cdn_dp_set_link_train(dp, training_pattern);
	if (ret) {
		DRM_ERROR("failed to start channel equalization\n");
		return ret;
	}

	for (tries = 0; tries < 5; tries++) {
		drm_dp_link_train_channel_eq_delay(dp->dpcd);
		if (drm_dp_dpcd_read_link_status(&dp->aux, link_status) !=
		    DP_LINK_STATUS_SIZE) {
			DRM_ERROR("failed to get link status\n");
			break;
		}

		/* Make sure clock is still ok */
		if (!drm_dp_clock_recovery_ok(link_status,
					      dp->link.num_lanes)) {
			DRM_DEBUG_KMS("Clock recovery check failed\n");
			break;
		}

		if (drm_dp_channel_eq_ok(link_status,  dp->link.num_lanes)) {
			DRM_DEBUG_KMS("Channel EQ done\n");
			return 0;
		}

		/* Update training set as requested by target */
		cdn_dp_get_adjust_train(dp, link_status);
		if (cdn_dp_update_link_train(dp)) {
			DRM_ERROR("failed to update link training\n");
			break;
		}
	}

	/* Try 5 times, else fail and try at lower BW */
	if (tries == 5)
		DRM_DEBUG_KMS("Channel equalization failed 5 times\n");

	return -EINVAL;
}

static int cdn_dp_stop_link_train(struct cdn_dp_device *dp)
{
	int ret = cdn_dp_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);

	if (ret)
		return ret;

	return cdn_dp_set_link_train(dp, DP_TRAINING_PATTERN_DISABLE);
}

static int cdn_dp_get_lower_link_rate(struct cdn_dp_device *dp)
{
	switch (dp->link.rate) {
	case DP_LINK_BW_1_62:
		return -EINVAL;
	case DP_LINK_BW_2_7:
		dp->link.rate = DP_LINK_BW_1_62;
		break;
	case DP_LINK_BW_5_4:
		dp->link.rate = DP_LINK_BW_2_7;
		break;
	default:
		dp->link.rate = DP_LINK_BW_5_4;
		break;
	}

	return 0;
}

int cdn_dp_software_train_link(struct cdn_dp_device *dp)
{
	struct cdn_dp_port *port = dp->port[dp->active_port];
	int ret, stop_err;
	u8 link_config[2];
	u32 rate, sink_max, source_max;
	bool ssc_on;

	ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd,
			       sizeof(dp->dpcd));
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "Failed to get caps %d\n", ret);
		return ret;
	}

	source_max = dp->lanes;
	sink_max = drm_dp_max_lane_count(dp->dpcd);
	dp->link.num_lanes = min(source_max, sink_max);

	source_max = drm_dp_bw_code_to_link_rate(CDN_DP_MAX_LINK_RATE);
	sink_max = drm_dp_max_link_rate(dp->dpcd);
	rate = min(source_max, sink_max);
	dp->link.rate = drm_dp_link_rate_to_bw_code(rate);

	ssc_on = !!(dp->dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5);
	link_config[0] = ssc_on ? DP_SPREAD_AMP_0_5 : 0;
	link_config[1] = 0;
	if (dp->dpcd[DP_MAIN_LINK_CHANNEL_CODING] & 0x01)
		link_config[1] = DP_SET_ANSI_8B10B;
	drm_dp_dpcd_write(&dp->aux, DP_DOWNSPREAD_CTRL, link_config, 2);

	while (true) {
		ret = tcphy_dp_set_link_rate(port->phy,
				drm_dp_bw_code_to_link_rate(dp->link.rate),
				ssc_on);
		if (ret) {
			DRM_ERROR("failed to set link rate: %d\n", ret);
			return ret;
		}

		ret = tcphy_dp_set_lane_count(port->phy, dp->link.num_lanes);
		if (ret) {
			DRM_ERROR("failed to set lane count: %d\n", ret);
			return ret;
		}

		/* Write the link configuration data */
		link_config[0] = dp->link.rate;
		link_config[1] = dp->link.num_lanes;
		if (drm_dp_enhanced_frame_cap(dp->dpcd))
			link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
		drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, link_config, 2);

		ret = cdn_dp_link_training_clock_recovery(dp);
		if (ret) {
			if (!cdn_dp_get_lower_link_rate(dp))
				continue;

			DRM_ERROR("training clock recovery failed: %d\n", ret);
			break;
		}

		ret = cdn_dp_link_training_channel_equalization(dp);
		if (ret) {
			if (!cdn_dp_get_lower_link_rate(dp))
				continue;

			DRM_ERROR("training channel eq failed: %d\n", ret);
			break;
		}

		break;
	}

	stop_err = cdn_dp_stop_link_train(dp);
	if (stop_err) {
		DRM_ERROR("stop training fail, error: %d\n", stop_err);
		return stop_err;
	}

	return ret;
}
