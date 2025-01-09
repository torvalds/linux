// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/delay.h>
#include <drm/drm_device.h>
#include <drm/drm_print.h>
#include "dp_comm.h"
#include "dp_reg.h"

#define HIBMC_EQ_MAX_RETRY 5

static int hibmc_dp_link_training_configure(struct hibmc_dp_dev *dp)
{
	u8 buf[2];
	int ret;

	/* DP 2 lane */
	hibmc_dp_reg_write_field(dp, HIBMC_DP_PHYIF_CTRL0, HIBMC_DP_CFG_LANE_DATA_EN,
				 dp->link.cap.lanes == 0x2 ? 0x3 : 0x1);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_DPTX_GCTL0, HIBMC_DP_CFG_PHY_LANE_NUM,
				 dp->link.cap.lanes == 0x2 ? 0x1 : 0);

	/* enhanced frame */
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CTRL, HIBMC_DP_CFG_STREAM_FRAME_MODE, 0x1);

	/* set rate and lane count */
	buf[0] = dp->link.cap.link_rate;
	buf[1] = DP_LANE_COUNT_ENHANCED_FRAME_EN | dp->link.cap.lanes;
	ret = drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_dbg_dp(dp->dev, "dp aux write link rate and lanes failed, ret: %d\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	/* set 8b/10b and downspread */
	buf[0] = DP_SPREAD_AMP_0_5;
	buf[1] = DP_SET_ANSI_8B10B;
	ret = drm_dp_dpcd_write(&dp->aux, DP_DOWNSPREAD_CTRL, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_dbg_dp(dp->dev, "dp aux write 8b/10b and downspread failed, ret: %d\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	ret = drm_dp_read_dpcd_caps(&dp->aux, dp->dpcd);
	if (ret)
		drm_err(dp->dev, "dp aux read dpcd failed, ret: %d\n", ret);

	return ret;
}

static int hibmc_dp_link_set_pattern(struct hibmc_dp_dev *dp, int pattern)
{
	int ret;
	u8 val;
	u8 buf;

	buf = (u8)pattern;
	if (pattern != DP_TRAINING_PATTERN_DISABLE && pattern != DP_TRAINING_PATTERN_4) {
		buf |= DP_LINK_SCRAMBLING_DISABLE;
		hibmc_dp_reg_write_field(dp, HIBMC_DP_PHYIF_CTRL0, HIBMC_DP_CFG_SCRAMBLE_EN, 0x1);
	} else {
		hibmc_dp_reg_write_field(dp, HIBMC_DP_PHYIF_CTRL0, HIBMC_DP_CFG_SCRAMBLE_EN, 0);
	}

	switch (pattern) {
	case DP_TRAINING_PATTERN_DISABLE:
		val = 0;
		break;
	case DP_TRAINING_PATTERN_1:
		val = 1;
		break;
	case DP_TRAINING_PATTERN_2:
		val = 2;
		break;
	case DP_TRAINING_PATTERN_3:
		val = 3;
		break;
	case DP_TRAINING_PATTERN_4:
		val = 4;
		break;
	default:
		return -EINVAL;
	}

	hibmc_dp_reg_write_field(dp, HIBMC_DP_PHYIF_CTRL0, HIBMC_DP_CFG_PAT_SEL, val);

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_PATTERN_SET, &buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_dbg_dp(dp->dev, "dp aux write training pattern set failed\n");
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static int hibmc_dp_link_training_cr_pre(struct hibmc_dp_dev *dp)
{
	u8 *train_set = dp->link.train_set;
	int ret;
	u8 i;

	ret = hibmc_dp_link_training_configure(dp);
	if (ret)
		return ret;

	ret = hibmc_dp_link_set_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;

	for (i = 0; i < dp->link.cap.lanes; i++)
		train_set[i] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2;

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, train_set, dp->link.cap.lanes);
	if (ret != dp->link.cap.lanes) {
		drm_dbg_dp(dp->dev, "dp aux write training lane set failed\n");
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static bool hibmc_dp_link_get_adjust_train(struct hibmc_dp_dev *dp,
					   u8 lane_status[DP_LINK_STATUS_SIZE])
{
	u8 train_set[HIBMC_DP_LANE_NUM_MAX] = {0};
	u8 lane;

	for (lane = 0; lane < dp->link.cap.lanes; lane++)
		train_set[lane] = drm_dp_get_adjust_request_voltage(lane_status, lane) |
				  drm_dp_get_adjust_request_pre_emphasis(lane_status, lane);

	if (memcmp(dp->link.train_set, train_set, HIBMC_DP_LANE_NUM_MAX)) {
		memcpy(dp->link.train_set, train_set, HIBMC_DP_LANE_NUM_MAX);
		return true;
	}

	return false;
}

static inline int hibmc_dp_link_reduce_rate(struct hibmc_dp_dev *dp)
{
	switch (dp->link.cap.link_rate) {
	case DP_LINK_BW_2_7:
		dp->link.cap.link_rate = DP_LINK_BW_1_62;
		return 0;
	case DP_LINK_BW_5_4:
		dp->link.cap.link_rate = DP_LINK_BW_2_7;
		return 0;
	case DP_LINK_BW_8_1:
		dp->link.cap.link_rate = DP_LINK_BW_5_4;
		return 0;
	default:
		return -EINVAL;
	}
}

static inline int hibmc_dp_link_reduce_lane(struct hibmc_dp_dev *dp)
{
	switch (dp->link.cap.lanes) {
	case 0x2:
		dp->link.cap.lanes--;
		break;
	case 0x1:
		drm_err(dp->dev, "dp link training reduce lane failed, already reach minimum\n");
		return -EIO;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hibmc_dp_link_training_cr(struct hibmc_dp_dev *dp)
{
	u8 lane_status[DP_LINK_STATUS_SIZE] = {0};
	bool level_changed;
	u32 voltage_tries;
	u32 cr_tries;
	int ret;

	/*
	 * DP 1.4 spec define 10 for maxtries value, for pre DP 1.4 version set a limit of 80
	 * (4 voltage levels x 4 preemphasis levels x 5 identical voltage retries)
	 */

	voltage_tries = 1;
	for (cr_tries = 0; cr_tries < 80; cr_tries++) {
		drm_dp_link_train_clock_recovery_delay(&dp->aux, dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, lane_status);
		if (ret != DP_LINK_STATUS_SIZE) {
			drm_err(dp->dev, "Get lane status failed\n");
			return ret;
		}

		if (drm_dp_clock_recovery_ok(lane_status, dp->link.cap.lanes)) {
			drm_dbg_dp(dp->dev, "dp link training cr done\n");
			dp->link.status.clock_recovered = true;
			return 0;
		}

		if (voltage_tries == 5) {
			drm_dbg_dp(dp->dev, "same voltage tries 5 times\n");
			dp->link.status.clock_recovered = false;
			return 0;
		}

		level_changed = hibmc_dp_link_get_adjust_train(dp, lane_status);
		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, dp->link.train_set,
					dp->link.cap.lanes);
		if (ret != dp->link.cap.lanes) {
			drm_dbg_dp(dp->dev, "Update link training failed\n");
			return ret >= 0 ? -EIO : ret;
		}

		voltage_tries = level_changed ? 1 : voltage_tries + 1;
	}

	drm_err(dp->dev, "dp link training clock recovery 80 times failed\n");
	dp->link.status.clock_recovered = false;

	return 0;
}

static int hibmc_dp_link_training_channel_eq(struct hibmc_dp_dev *dp)
{
	u8 lane_status[DP_LINK_STATUS_SIZE] = {0};
	u8 eq_tries;
	int ret;

	ret = hibmc_dp_link_set_pattern(dp, DP_TRAINING_PATTERN_2);
	if (ret)
		return ret;

	for (eq_tries = 0; eq_tries < HIBMC_EQ_MAX_RETRY; eq_tries++) {
		drm_dp_link_train_channel_eq_delay(&dp->aux, dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, lane_status);
		if (ret != DP_LINK_STATUS_SIZE) {
			drm_err(dp->dev, "get lane status failed\n");
			break;
		}

		if (!drm_dp_clock_recovery_ok(lane_status, dp->link.cap.lanes)) {
			drm_dbg_dp(dp->dev, "clock recovery check failed\n");
			drm_dbg_dp(dp->dev, "cannot continue channel equalization\n");
			dp->link.status.clock_recovered = false;
			break;
		}

		if (drm_dp_channel_eq_ok(lane_status, dp->link.cap.lanes)) {
			dp->link.status.channel_equalized = true;
			drm_dbg_dp(dp->dev, "dp link training eq done\n");
			break;
		}

		hibmc_dp_link_get_adjust_train(dp, lane_status);
		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
					dp->link.train_set, dp->link.cap.lanes);
		if (ret != dp->link.cap.lanes) {
			drm_dbg_dp(dp->dev, "Update link training failed\n");
			ret = (ret >= 0) ? -EIO : ret;
			break;
		}
	}

	if (eq_tries == HIBMC_EQ_MAX_RETRY)
		drm_err(dp->dev, "channel equalization failed %u times\n", eq_tries);

	hibmc_dp_link_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);

	return ret < 0 ? ret : 0;
}

static int hibmc_dp_link_downgrade_training_cr(struct hibmc_dp_dev *dp)
{
	if (hibmc_dp_link_reduce_rate(dp))
		return hibmc_dp_link_reduce_lane(dp);

	return 0;
}

static int hibmc_dp_link_downgrade_training_eq(struct hibmc_dp_dev *dp)
{
	if ((dp->link.status.clock_recovered && !dp->link.status.channel_equalized)) {
		if (!hibmc_dp_link_reduce_lane(dp))
			return 0;
	}

	return hibmc_dp_link_reduce_rate(dp);
}

int hibmc_dp_link_training(struct hibmc_dp_dev *dp)
{
	struct hibmc_dp_link *link = &dp->link;
	int ret;

	while (true) {
		ret = hibmc_dp_link_training_cr_pre(dp);
		if (ret)
			goto err;

		ret = hibmc_dp_link_training_cr(dp);
		if (ret)
			goto err;

		if (!link->status.clock_recovered) {
			ret = hibmc_dp_link_downgrade_training_cr(dp);
			if (ret)
				goto err;
			continue;
		}

		ret = hibmc_dp_link_training_channel_eq(dp);
		if (ret)
			goto err;

		if (!link->status.channel_equalized) {
			ret = hibmc_dp_link_downgrade_training_eq(dp);
			if (ret)
				goto err;
			continue;
		}

		return 0;
	}

err:
	hibmc_dp_link_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);

	return ret;
}
