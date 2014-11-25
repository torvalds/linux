/*
 * Samsung SoC DP (Display Port) interface driver.
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/component.h>
#include <linux/phy/phy.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>
#include <drm/bridge/ptn3460.h>

#include "exynos_dp_core.h"

#define ctx_from_connector(c)	container_of(c, struct exynos_dp_device, \
					connector)

static inline struct exynos_dp_device *
display_to_dp(struct exynos_drm_display *d)
{
	return container_of(d, struct exynos_dp_device, display);
}

struct bridge_init {
	struct i2c_client *client;
	struct device_node *node;
};

static void exynos_dp_init_dp(struct exynos_dp_device *dp)
{
	exynos_dp_reset(dp);

	exynos_dp_swreset(dp);

	exynos_dp_init_analog_param(dp);
	exynos_dp_init_interrupt(dp);

	/* SW defined function Normal operation */
	exynos_dp_enable_sw_function(dp);

	exynos_dp_config_interrupt(dp);
	exynos_dp_init_analog_func(dp);

	exynos_dp_init_hpd(dp);
	exynos_dp_init_aux(dp);
}

static int exynos_dp_detect_hpd(struct exynos_dp_device *dp)
{
	int timeout_loop = 0;

	while (exynos_dp_get_plug_in_status(dp) != 0) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "failed to get hpd plug status\n");
			return -ETIMEDOUT;
		}
		usleep_range(10, 11);
	}

	return 0;
}

static unsigned char exynos_dp_calc_edid_check_sum(unsigned char *edid_data)
{
	int i;
	unsigned char sum = 0;

	for (i = 0; i < EDID_BLOCK_LENGTH; i++)
		sum = sum + edid_data[i];

	return sum;
}

static int exynos_dp_read_edid(struct exynos_dp_device *dp)
{
	unsigned char edid[EDID_BLOCK_LENGTH * 2];
	unsigned int extend_block = 0;
	unsigned char sum;
	unsigned char test_vector;
	int retval;

	/*
	 * EDID device address is 0x50.
	 * However, if necessary, you must have set upper address
	 * into E-EDID in I2C device, 0x30.
	 */

	/* Read Extension Flag, Number of 128-byte EDID extension blocks */
	retval = exynos_dp_read_byte_from_i2c(dp, I2C_EDID_DEVICE_ADDR,
				EDID_EXTENSION_FLAG,
				&extend_block);
	if (retval)
		return retval;

	if (extend_block > 0) {
		dev_dbg(dp->dev, "EDID data includes a single extension!\n");

		/* Read EDID data */
		retval = exynos_dp_read_bytes_from_i2c(dp, I2C_EDID_DEVICE_ADDR,
						EDID_HEADER_PATTERN,
						EDID_BLOCK_LENGTH,
						&edid[EDID_HEADER_PATTERN]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = exynos_dp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		/* Read additional EDID data */
		retval = exynos_dp_read_bytes_from_i2c(dp,
				I2C_EDID_DEVICE_ADDR,
				EDID_BLOCK_LENGTH,
				EDID_BLOCK_LENGTH,
				&edid[EDID_BLOCK_LENGTH]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = exynos_dp_calc_edid_check_sum(&edid[EDID_BLOCK_LENGTH]);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		exynos_dp_read_byte_from_dpcd(dp, DP_TEST_REQUEST,
					&test_vector);
		if (test_vector & DP_TEST_LINK_EDID_READ) {
			exynos_dp_write_byte_to_dpcd(dp,
				DP_TEST_EDID_CHECKSUM,
				edid[EDID_BLOCK_LENGTH + EDID_CHECKSUM]);
			exynos_dp_write_byte_to_dpcd(dp,
				DP_TEST_RESPONSE,
				DP_TEST_EDID_CHECKSUM_WRITE);
		}
	} else {
		dev_info(dp->dev, "EDID data does not include any extensions.\n");

		/* Read EDID data */
		retval = exynos_dp_read_bytes_from_i2c(dp,
				I2C_EDID_DEVICE_ADDR,
				EDID_HEADER_PATTERN,
				EDID_BLOCK_LENGTH,
				&edid[EDID_HEADER_PATTERN]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = exynos_dp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		exynos_dp_read_byte_from_dpcd(dp,
			DP_TEST_REQUEST,
			&test_vector);
		if (test_vector & DP_TEST_LINK_EDID_READ) {
			exynos_dp_write_byte_to_dpcd(dp,
				DP_TEST_EDID_CHECKSUM,
				edid[EDID_CHECKSUM]);
			exynos_dp_write_byte_to_dpcd(dp,
				DP_TEST_RESPONSE,
				DP_TEST_EDID_CHECKSUM_WRITE);
		}
	}

	dev_err(dp->dev, "EDID Read success!\n");
	return 0;
}

static int exynos_dp_handle_edid(struct exynos_dp_device *dp)
{
	u8 buf[12];
	int i;
	int retval;

	/* Read DPCD DP_DPCD_REV~RECEIVE_PORT1_CAP_1 */
	retval = exynos_dp_read_bytes_from_dpcd(dp, DP_DPCD_REV,
				12, buf);
	if (retval)
		return retval;

	/* Read EDID */
	for (i = 0; i < 3; i++) {
		retval = exynos_dp_read_edid(dp);
		if (!retval)
			break;
	}

	return retval;
}

static void exynos_dp_enable_rx_to_enhanced_mode(struct exynos_dp_device *dp,
						bool enable)
{
	u8 data;

	exynos_dp_read_byte_from_dpcd(dp, DP_LANE_COUNT_SET, &data);

	if (enable)
		exynos_dp_write_byte_to_dpcd(dp, DP_LANE_COUNT_SET,
			DP_LANE_COUNT_ENHANCED_FRAME_EN |
			DPCD_LANE_COUNT_SET(data));
	else
		exynos_dp_write_byte_to_dpcd(dp, DP_LANE_COUNT_SET,
			DPCD_LANE_COUNT_SET(data));
}

static int exynos_dp_is_enhanced_mode_available(struct exynos_dp_device *dp)
{
	u8 data;
	int retval;

	exynos_dp_read_byte_from_dpcd(dp, DP_MAX_LANE_COUNT, &data);
	retval = DPCD_ENHANCED_FRAME_CAP(data);

	return retval;
}

static void exynos_dp_set_enhanced_mode(struct exynos_dp_device *dp)
{
	u8 data;

	data = exynos_dp_is_enhanced_mode_available(dp);
	exynos_dp_enable_rx_to_enhanced_mode(dp, data);
	exynos_dp_enable_enhanced_mode(dp, data);
}

static void exynos_dp_training_pattern_dis(struct exynos_dp_device *dp)
{
	exynos_dp_set_training_pattern(dp, DP_NONE);

	exynos_dp_write_byte_to_dpcd(dp,
		DP_TRAINING_PATTERN_SET,
		DP_TRAINING_PATTERN_DISABLE);
}

static void exynos_dp_set_lane_lane_pre_emphasis(struct exynos_dp_device *dp,
					int pre_emphasis, int lane)
{
	switch (lane) {
	case 0:
		exynos_dp_set_lane0_pre_emphasis(dp, pre_emphasis);
		break;
	case 1:
		exynos_dp_set_lane1_pre_emphasis(dp, pre_emphasis);
		break;

	case 2:
		exynos_dp_set_lane2_pre_emphasis(dp, pre_emphasis);
		break;

	case 3:
		exynos_dp_set_lane3_pre_emphasis(dp, pre_emphasis);
		break;
	}
}

static int exynos_dp_link_start(struct exynos_dp_device *dp)
{
	u8 buf[4];
	int lane, lane_count, pll_tries, retval;

	lane_count = dp->link_train.lane_count;

	dp->link_train.lt_state = CLOCK_RECOVERY;
	dp->link_train.eq_loop = 0;

	for (lane = 0; lane < lane_count; lane++)
		dp->link_train.cr_loop[lane] = 0;

	/* Set link rate and count as you want to establish*/
	exynos_dp_set_link_bandwidth(dp, dp->link_train.link_rate);
	exynos_dp_set_lane_count(dp, dp->link_train.lane_count);

	/* Setup RX configuration */
	buf[0] = dp->link_train.link_rate;
	buf[1] = dp->link_train.lane_count;
	retval = exynos_dp_write_bytes_to_dpcd(dp, DP_LINK_BW_SET,
				2, buf);
	if (retval)
		return retval;

	/* Set TX pre-emphasis to minimum */
	for (lane = 0; lane < lane_count; lane++)
		exynos_dp_set_lane_lane_pre_emphasis(dp,
			PRE_EMPHASIS_LEVEL_0, lane);

	/* Wait for PLL lock */
	pll_tries = 0;
	while (exynos_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		if (pll_tries == DP_TIMEOUT_LOOP_COUNT) {
			dev_err(dp->dev, "Wait for PLL lock timed out\n");
			return -ETIMEDOUT;
		}

		pll_tries++;
		usleep_range(90, 120);
	}

	/* Set training pattern 1 */
	exynos_dp_set_training_pattern(dp, TRAINING_PTN1);

	/* Set RX training pattern */
	retval = exynos_dp_write_byte_to_dpcd(dp,
			DP_TRAINING_PATTERN_SET,
			DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_1);
	if (retval)
		return retval;

	for (lane = 0; lane < lane_count; lane++)
		buf[lane] = DP_TRAIN_PRE_EMPH_LEVEL_0 |
			    DP_TRAIN_VOLTAGE_SWING_LEVEL_0;

	retval = exynos_dp_write_bytes_to_dpcd(dp, DP_TRAINING_LANE0_SET,
			lane_count, buf);

	return retval;
}

static unsigned char exynos_dp_get_lane_status(u8 link_status[2], int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = link_status[lane>>1];

	return (link_value >> shift) & 0xf;
}

static int exynos_dp_clock_recovery_ok(u8 link_status[2], int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = exynos_dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return -EINVAL;
	}
	return 0;
}

static int exynos_dp_channel_eq_ok(u8 link_status[2], u8 link_align,
				int lane_count)
{
	int lane;
	u8 lane_status;

	if ((link_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return -EINVAL;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = exynos_dp_get_lane_status(link_status, lane);
		lane_status &= DP_CHANNEL_EQ_BITS;
		if (lane_status != DP_CHANNEL_EQ_BITS)
			return -EINVAL;
	}

	return 0;
}

static unsigned char exynos_dp_get_adjust_request_voltage(u8 adjust_request[2],
							int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return (link_value >> shift) & 0x3;
}

static unsigned char exynos_dp_get_adjust_request_pre_emphasis(
					u8 adjust_request[2],
					int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return ((link_value >> shift) & 0xc) >> 2;
}

static void exynos_dp_set_lane_link_training(struct exynos_dp_device *dp,
					u8 training_lane_set, int lane)
{
	switch (lane) {
	case 0:
		exynos_dp_set_lane0_link_training(dp, training_lane_set);
		break;
	case 1:
		exynos_dp_set_lane1_link_training(dp, training_lane_set);
		break;

	case 2:
		exynos_dp_set_lane2_link_training(dp, training_lane_set);
		break;

	case 3:
		exynos_dp_set_lane3_link_training(dp, training_lane_set);
		break;
	}
}

static unsigned int exynos_dp_get_lane_link_training(
				struct exynos_dp_device *dp,
				int lane)
{
	u32 reg;

	switch (lane) {
	case 0:
		reg = exynos_dp_get_lane0_link_training(dp);
		break;
	case 1:
		reg = exynos_dp_get_lane1_link_training(dp);
		break;
	case 2:
		reg = exynos_dp_get_lane2_link_training(dp);
		break;
	case 3:
		reg = exynos_dp_get_lane3_link_training(dp);
		break;
	default:
		WARN_ON(1);
		return 0;
	}

	return reg;
}

static void exynos_dp_reduce_link_rate(struct exynos_dp_device *dp)
{
	exynos_dp_training_pattern_dis(dp);
	exynos_dp_set_enhanced_mode(dp);

	dp->link_train.lt_state = FAILED;
}

static void exynos_dp_get_adjust_training_lane(struct exynos_dp_device *dp,
					u8 adjust_request[2])
{
	int lane, lane_count;
	u8 voltage_swing, pre_emphasis, training_lane;

	lane_count = dp->link_train.lane_count;
	for (lane = 0; lane < lane_count; lane++) {
		voltage_swing = exynos_dp_get_adjust_request_voltage(
						adjust_request, lane);
		pre_emphasis = exynos_dp_get_adjust_request_pre_emphasis(
						adjust_request, lane);
		training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
				DPCD_PRE_EMPHASIS_SET(pre_emphasis);

		if (voltage_swing == VOLTAGE_LEVEL_3)
			training_lane |= DP_TRAIN_MAX_SWING_REACHED;
		if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
			training_lane |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		dp->link_train.training_lane[lane] = training_lane;
	}
}

static int exynos_dp_process_clock_recovery(struct exynos_dp_device *dp)
{
	int lane, lane_count, retval;
	u8 voltage_swing, pre_emphasis, training_lane;
	u8 link_status[2], adjust_request[2];

	usleep_range(100, 101);

	lane_count = dp->link_train.lane_count;

	retval =  exynos_dp_read_bytes_from_dpcd(dp,
			DP_LANE0_1_STATUS, 2, link_status);
	if (retval)
		return retval;

	retval =  exynos_dp_read_bytes_from_dpcd(dp,
			DP_ADJUST_REQUEST_LANE0_1, 2, adjust_request);
	if (retval)
		return retval;

	if (exynos_dp_clock_recovery_ok(link_status, lane_count) == 0) {
		/* set training pattern 2 for EQ */
		exynos_dp_set_training_pattern(dp, TRAINING_PTN2);

		retval = exynos_dp_write_byte_to_dpcd(dp,
				DP_TRAINING_PATTERN_SET,
				DP_LINK_SCRAMBLING_DISABLE |
				DP_TRAINING_PATTERN_2);
		if (retval)
			return retval;

		dev_info(dp->dev, "Link Training Clock Recovery success\n");
		dp->link_train.lt_state = EQUALIZER_TRAINING;
	} else {
		for (lane = 0; lane < lane_count; lane++) {
			training_lane = exynos_dp_get_lane_link_training(
							dp, lane);
			voltage_swing = exynos_dp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = exynos_dp_get_adjust_request_pre_emphasis(
							adjust_request, lane);

			if (DPCD_VOLTAGE_SWING_GET(training_lane) ==
					voltage_swing &&
			    DPCD_PRE_EMPHASIS_GET(training_lane) ==
					pre_emphasis)
				dp->link_train.cr_loop[lane]++;

			if (dp->link_train.cr_loop[lane] == MAX_CR_LOOP ||
			    voltage_swing == VOLTAGE_LEVEL_3 ||
			    pre_emphasis == PRE_EMPHASIS_LEVEL_3) {
				dev_err(dp->dev, "CR Max reached (%d,%d,%d)\n",
					dp->link_train.cr_loop[lane],
					voltage_swing, pre_emphasis);
				exynos_dp_reduce_link_rate(dp);
				return -EIO;
			}
		}
	}

	exynos_dp_get_adjust_training_lane(dp, adjust_request);

	for (lane = 0; lane < lane_count; lane++)
		exynos_dp_set_lane_link_training(dp,
			dp->link_train.training_lane[lane], lane);

	retval = exynos_dp_write_bytes_to_dpcd(dp,
			DP_TRAINING_LANE0_SET, lane_count,
			dp->link_train.training_lane);
	if (retval)
		return retval;

	return retval;
}

static int exynos_dp_process_equalizer_training(struct exynos_dp_device *dp)
{
	int lane, lane_count, retval;
	u32 reg;
	u8 link_align, link_status[2], adjust_request[2];

	usleep_range(400, 401);

	lane_count = dp->link_train.lane_count;

	retval = exynos_dp_read_bytes_from_dpcd(dp,
			DP_LANE0_1_STATUS, 2, link_status);
	if (retval)
		return retval;

	if (exynos_dp_clock_recovery_ok(link_status, lane_count)) {
		exynos_dp_reduce_link_rate(dp);
		return -EIO;
	}

	retval = exynos_dp_read_bytes_from_dpcd(dp,
			DP_ADJUST_REQUEST_LANE0_1, 2, adjust_request);
	if (retval)
		return retval;

	retval = exynos_dp_read_byte_from_dpcd(dp,
			DP_LANE_ALIGN_STATUS_UPDATED, &link_align);
	if (retval)
		return retval;

	exynos_dp_get_adjust_training_lane(dp, adjust_request);

	if (!exynos_dp_channel_eq_ok(link_status, link_align, lane_count)) {
		/* traing pattern Set to Normal */
		exynos_dp_training_pattern_dis(dp);

		dev_info(dp->dev, "Link Training success!\n");

		exynos_dp_get_link_bandwidth(dp, &reg);
		dp->link_train.link_rate = reg;
		dev_dbg(dp->dev, "final bandwidth = %.2x\n",
			dp->link_train.link_rate);

		exynos_dp_get_lane_count(dp, &reg);
		dp->link_train.lane_count = reg;
		dev_dbg(dp->dev, "final lane count = %.2x\n",
			dp->link_train.lane_count);

		/* set enhanced mode if available */
		exynos_dp_set_enhanced_mode(dp);
		dp->link_train.lt_state = FINISHED;

		return 0;
	}

	/* not all locked */
	dp->link_train.eq_loop++;

	if (dp->link_train.eq_loop > MAX_EQ_LOOP) {
		dev_err(dp->dev, "EQ Max loop\n");
		exynos_dp_reduce_link_rate(dp);
		return -EIO;
	}

	for (lane = 0; lane < lane_count; lane++)
		exynos_dp_set_lane_link_training(dp,
			dp->link_train.training_lane[lane], lane);

	retval = exynos_dp_write_bytes_to_dpcd(dp, DP_TRAINING_LANE0_SET,
			lane_count, dp->link_train.training_lane);

	return retval;
}

static void exynos_dp_get_max_rx_bandwidth(struct exynos_dp_device *dp,
					u8 *bandwidth)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 */
	exynos_dp_read_byte_from_dpcd(dp, DP_MAX_LINK_RATE, &data);
	*bandwidth = data;
}

static void exynos_dp_get_max_rx_lane_count(struct exynos_dp_device *dp,
					u8 *lane_count)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum number of Main Link lanes
	 * 0x01 = 1 lane, 0x02 = 2 lanes, 0x04 = 4 lanes
	 */
	exynos_dp_read_byte_from_dpcd(dp, DP_MAX_LANE_COUNT, &data);
	*lane_count = DPCD_MAX_LANE_COUNT(data);
}

static void exynos_dp_init_training(struct exynos_dp_device *dp,
			enum link_lane_count_type max_lane,
			enum link_rate_type max_rate)
{
	/*
	 * MACRO_RST must be applied after the PLL_LOCK to avoid
	 * the DP inter pair skew issue for at least 10 us
	 */
	exynos_dp_reset_macro(dp);

	/* Initialize by reading RX's DPCD */
	exynos_dp_get_max_rx_bandwidth(dp, &dp->link_train.link_rate);
	exynos_dp_get_max_rx_lane_count(dp, &dp->link_train.lane_count);

	if ((dp->link_train.link_rate != LINK_RATE_1_62GBPS) &&
	   (dp->link_train.link_rate != LINK_RATE_2_70GBPS)) {
		dev_err(dp->dev, "Rx Max Link Rate is abnormal :%x !\n",
			dp->link_train.link_rate);
		dp->link_train.link_rate = LINK_RATE_1_62GBPS;
	}

	if (dp->link_train.lane_count == 0) {
		dev_err(dp->dev, "Rx Max Lane count is abnormal :%x !\n",
			dp->link_train.lane_count);
		dp->link_train.lane_count = (u8)LANE_COUNT1;
	}

	/* Setup TX lane count & rate */
	if (dp->link_train.lane_count > max_lane)
		dp->link_train.lane_count = max_lane;
	if (dp->link_train.link_rate > max_rate)
		dp->link_train.link_rate = max_rate;

	/* All DP analog module power up */
	exynos_dp_set_analog_power_down(dp, POWER_ALL, 0);
}

static int exynos_dp_sw_link_training(struct exynos_dp_device *dp)
{
	int retval = 0, training_finished = 0;

	dp->link_train.lt_state = START;

	/* Process here */
	while (!retval && !training_finished) {
		switch (dp->link_train.lt_state) {
		case START:
			retval = exynos_dp_link_start(dp);
			if (retval)
				dev_err(dp->dev, "LT link start failed!\n");
			break;
		case CLOCK_RECOVERY:
			retval = exynos_dp_process_clock_recovery(dp);
			if (retval)
				dev_err(dp->dev, "LT CR failed!\n");
			break;
		case EQUALIZER_TRAINING:
			retval = exynos_dp_process_equalizer_training(dp);
			if (retval)
				dev_err(dp->dev, "LT EQ failed!\n");
			break;
		case FINISHED:
			training_finished = 1;
			break;
		case FAILED:
			return -EREMOTEIO;
		}
	}
	if (retval)
		dev_err(dp->dev, "eDP link training failed (%d)\n", retval);

	return retval;
}

static int exynos_dp_set_link_train(struct exynos_dp_device *dp,
				u32 count,
				u32 bwtype)
{
	int i;
	int retval;

	for (i = 0; i < DP_TIMEOUT_LOOP_COUNT; i++) {
		exynos_dp_init_training(dp, count, bwtype);
		retval = exynos_dp_sw_link_training(dp);
		if (retval == 0)
			break;

		usleep_range(100, 110);
	}

	return retval;
}

static int exynos_dp_config_video(struct exynos_dp_device *dp)
{
	int retval = 0;
	int timeout_loop = 0;
	int done_count = 0;

	exynos_dp_config_video_slave_mode(dp);

	exynos_dp_set_video_color_format(dp);

	if (exynos_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		dev_err(dp->dev, "PLL is not locked yet.\n");
		return -EINVAL;
	}

	for (;;) {
		timeout_loop++;
		if (exynos_dp_is_slave_video_stream_clock_on(dp) == 0)
			break;
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "Timeout of video streamclk ok\n");
			return -ETIMEDOUT;
		}

		usleep_range(1, 2);
	}

	/* Set to use the register calculated M/N video */
	exynos_dp_set_video_cr_mn(dp, CALCULATED_M, 0, 0);

	/* For video bist, Video timing must be generated by register */
	exynos_dp_set_video_timing_mode(dp, VIDEO_TIMING_FROM_CAPTURE);

	/* Disable video mute */
	exynos_dp_enable_video_mute(dp, 0);

	/* Configure video slave mode */
	exynos_dp_enable_video_master(dp, 0);

	/* Enable video */
	exynos_dp_start_video(dp);

	timeout_loop = 0;

	for (;;) {
		timeout_loop++;
		if (exynos_dp_is_video_stream_on(dp) == 0) {
			done_count++;
			if (done_count > 10)
				break;
		} else if (done_count) {
			done_count = 0;
		}
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "Timeout of video streamclk ok\n");
			return -ETIMEDOUT;
		}

		usleep_range(1000, 1001);
	}

	if (retval != 0)
		dev_err(dp->dev, "Video stream is not detected!\n");

	return retval;
}

static void exynos_dp_enable_scramble(struct exynos_dp_device *dp, bool enable)
{
	u8 data;

	if (enable) {
		exynos_dp_enable_scrambling(dp);

		exynos_dp_read_byte_from_dpcd(dp,
			DP_TRAINING_PATTERN_SET,
			&data);
		exynos_dp_write_byte_to_dpcd(dp,
			DP_TRAINING_PATTERN_SET,
			(u8)(data & ~DP_LINK_SCRAMBLING_DISABLE));
	} else {
		exynos_dp_disable_scrambling(dp);

		exynos_dp_read_byte_from_dpcd(dp,
			DP_TRAINING_PATTERN_SET,
			&data);
		exynos_dp_write_byte_to_dpcd(dp,
			DP_TRAINING_PATTERN_SET,
			(u8)(data | DP_LINK_SCRAMBLING_DISABLE));
	}
}

static irqreturn_t exynos_dp_irq_handler(int irq, void *arg)
{
	struct exynos_dp_device *dp = arg;

	enum dp_irq_type irq_type;

	irq_type = exynos_dp_get_irq_type(dp);
	switch (irq_type) {
	case DP_IRQ_TYPE_HP_CABLE_IN:
		dev_dbg(dp->dev, "Received irq - cable in\n");
		schedule_work(&dp->hotplug_work);
		exynos_dp_clear_hotplug_interrupts(dp);
		break;
	case DP_IRQ_TYPE_HP_CABLE_OUT:
		dev_dbg(dp->dev, "Received irq - cable out\n");
		exynos_dp_clear_hotplug_interrupts(dp);
		break;
	case DP_IRQ_TYPE_HP_CHANGE:
		/*
		 * We get these change notifications once in a while, but there
		 * is nothing we can do with them. Just ignore it for now and
		 * only handle cable changes.
		 */
		dev_dbg(dp->dev, "Received irq - hotplug change; ignoring.\n");
		exynos_dp_clear_hotplug_interrupts(dp);
		break;
	default:
		dev_err(dp->dev, "Received irq - unknown type!\n");
		break;
	}
	return IRQ_HANDLED;
}

static void exynos_dp_hotplug(struct work_struct *work)
{
	struct exynos_dp_device *dp;

	dp = container_of(work, struct exynos_dp_device, hotplug_work);

	if (dp->drm_dev)
		drm_helper_hpd_irq_event(dp->drm_dev);
}

static void exynos_dp_commit(struct exynos_drm_display *display)
{
	struct exynos_dp_device *dp = display_to_dp(display);
	int ret;

	/* Keep the panel disabled while we configure video */
	if (dp->panel) {
		if (drm_panel_disable(dp->panel))
			DRM_ERROR("failed to disable the panel\n");
	}

	ret = exynos_dp_detect_hpd(dp);
	if (ret) {
		/* Cable has been disconnected, we're done */
		return;
	}

	ret = exynos_dp_handle_edid(dp);
	if (ret) {
		dev_err(dp->dev, "unable to handle edid\n");
		return;
	}

	ret = exynos_dp_set_link_train(dp, dp->video_info->lane_count,
					dp->video_info->link_rate);
	if (ret) {
		dev_err(dp->dev, "unable to do link train\n");
		return;
	}

	exynos_dp_enable_scramble(dp, 1);
	exynos_dp_enable_rx_to_enhanced_mode(dp, 1);
	exynos_dp_enable_enhanced_mode(dp, 1);

	exynos_dp_set_lane_count(dp, dp->video_info->lane_count);
	exynos_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	exynos_dp_init_video(dp);
	ret = exynos_dp_config_video(dp);
	if (ret)
		dev_err(dp->dev, "unable to config video\n");

	/* Safe to enable the panel now */
	if (dp->panel) {
		if (drm_panel_enable(dp->panel))
			DRM_ERROR("failed to enable the panel\n");
	}
}

static enum drm_connector_status exynos_dp_detect(
				struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void exynos_dp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs exynos_dp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = exynos_dp_detect,
	.destroy = exynos_dp_connector_destroy,
};

static int exynos_dp_get_modes(struct drm_connector *connector)
{
	struct exynos_dp_device *dp = ctx_from_connector(connector);
	struct drm_display_mode *mode;

	if (dp->panel)
		return drm_panel_get_modes(dp->panel);

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode.\n");
		return 0;
	}

	drm_display_mode_from_videomode(&dp->priv.vm, mode);
	mode->width_mm = dp->priv.width_mm;
	mode->height_mm = dp->priv.height_mm;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static struct drm_encoder *exynos_dp_best_encoder(
			struct drm_connector *connector)
{
	struct exynos_dp_device *dp = ctx_from_connector(connector);

	return dp->encoder;
}

static struct drm_connector_helper_funcs exynos_dp_connector_helper_funcs = {
	.get_modes = exynos_dp_get_modes,
	.best_encoder = exynos_dp_best_encoder,
};

static bool find_bridge(const char *compat, struct bridge_init *bridge)
{
	bridge->client = NULL;
	bridge->node = of_find_compatible_node(NULL, NULL, compat);
	if (!bridge->node)
		return false;

	bridge->client = of_find_i2c_device_by_node(bridge->node);
	if (!bridge->client)
		return false;

	return true;
}

/* returns the number of bridges attached */
static int exynos_drm_attach_lcd_bridge(struct drm_device *dev,
		struct drm_encoder *encoder)
{
	struct bridge_init bridge;
	int ret;

	if (find_bridge("nxp,ptn3460", &bridge)) {
		ret = ptn3460_init(dev, encoder, bridge.client, bridge.node);
		if (!ret)
			return 1;
	}
	return 0;
}

static int exynos_dp_create_connector(struct exynos_drm_display *display,
				struct drm_encoder *encoder)
{
	struct exynos_dp_device *dp = display_to_dp(display);
	struct drm_connector *connector = &dp->connector;
	int ret;

	dp->encoder = encoder;

	/* Pre-empt DP connector creation if there's a bridge */
	ret = exynos_drm_attach_lcd_bridge(dp->drm_dev, encoder);
	if (ret)
		return 0;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(dp->drm_dev, connector,
			&exynos_dp_connector_funcs, DRM_MODE_CONNECTOR_eDP);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &exynos_dp_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);

	if (dp->panel)
		ret = drm_panel_attach(dp->panel, &dp->connector);

	return ret;
}

static void exynos_dp_phy_init(struct exynos_dp_device *dp)
{
	if (dp->phy)
		phy_power_on(dp->phy);
}

static void exynos_dp_phy_exit(struct exynos_dp_device *dp)
{
	if (dp->phy)
		phy_power_off(dp->phy);
}

static void exynos_dp_poweron(struct exynos_drm_display *display)
{
	struct exynos_dp_device *dp = display_to_dp(display);

	if (dp->dpms_mode == DRM_MODE_DPMS_ON)
		return;

	if (dp->panel) {
		if (drm_panel_prepare(dp->panel)) {
			DRM_ERROR("failed to setup the panel\n");
			return;
		}
	}

	clk_prepare_enable(dp->clock);
	exynos_dp_phy_init(dp);
	exynos_dp_init_dp(dp);
	enable_irq(dp->irq);
	exynos_dp_commit(display);
}

static void exynos_dp_poweroff(struct exynos_drm_display *display)
{
	struct exynos_dp_device *dp = display_to_dp(display);

	if (dp->dpms_mode != DRM_MODE_DPMS_ON)
		return;

	if (dp->panel) {
		if (drm_panel_disable(dp->panel)) {
			DRM_ERROR("failed to disable the panel\n");
			return;
		}
	}

	disable_irq(dp->irq);
	flush_work(&dp->hotplug_work);
	exynos_dp_phy_exit(dp);
	clk_disable_unprepare(dp->clock);

	if (dp->panel) {
		if (drm_panel_unprepare(dp->panel))
			DRM_ERROR("failed to turnoff the panel\n");
	}
}

static void exynos_dp_dpms(struct exynos_drm_display *display, int mode)
{
	struct exynos_dp_device *dp = display_to_dp(display);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		exynos_dp_poweron(display);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		exynos_dp_poweroff(display);
		break;
	default:
		break;
	}
	dp->dpms_mode = mode;
}

static struct exynos_drm_display_ops exynos_dp_display_ops = {
	.create_connector = exynos_dp_create_connector,
	.dpms = exynos_dp_dpms,
	.commit = exynos_dp_commit,
};

static struct video_info *exynos_dp_dt_parse_pdata(struct device *dev)
{
	struct device_node *dp_node = dev->of_node;
	struct video_info *dp_video_config;

	dp_video_config = devm_kzalloc(dev,
				sizeof(*dp_video_config), GFP_KERNEL);
	if (!dp_video_config)
		return ERR_PTR(-ENOMEM);

	dp_video_config->h_sync_polarity =
		of_property_read_bool(dp_node, "hsync-active-high");

	dp_video_config->v_sync_polarity =
		of_property_read_bool(dp_node, "vsync-active-high");

	dp_video_config->interlaced =
		of_property_read_bool(dp_node, "interlaced");

	if (of_property_read_u32(dp_node, "samsung,color-space",
				&dp_video_config->color_space)) {
		dev_err(dev, "failed to get color-space\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(dp_node, "samsung,dynamic-range",
				&dp_video_config->dynamic_range)) {
		dev_err(dev, "failed to get dynamic-range\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(dp_node, "samsung,ycbcr-coeff",
				&dp_video_config->ycbcr_coeff)) {
		dev_err(dev, "failed to get ycbcr-coeff\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(dp_node, "samsung,color-depth",
				&dp_video_config->color_depth)) {
		dev_err(dev, "failed to get color-depth\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(dp_node, "samsung,link-rate",
				&dp_video_config->link_rate)) {
		dev_err(dev, "failed to get link-rate\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(dp_node, "samsung,lane-count",
				&dp_video_config->lane_count)) {
		dev_err(dev, "failed to get lane-count\n");
		return ERR_PTR(-EINVAL);
	}

	return dp_video_config;
}

static int exynos_dp_dt_parse_panel(struct exynos_dp_device *dp)
{
	int ret;

	ret = of_get_videomode(dp->dev->of_node, &dp->priv.vm,
			OF_USE_NATIVE_MODE);
	if (ret) {
		DRM_ERROR("failed: of_get_videomode() : %d\n", ret);
		return ret;
	}
	return 0;
}

static int exynos_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct exynos_dp_device *dp = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm_dev = data;
	struct resource *res;
	unsigned int irq_flags;
	int ret = 0;

	dp->dev = &pdev->dev;
	dp->dpms_mode = DRM_MODE_DPMS_OFF;

	dp->video_info = exynos_dp_dt_parse_pdata(&pdev->dev);
	if (IS_ERR(dp->video_info))
		return PTR_ERR(dp->video_info);

	dp->phy = devm_phy_get(dp->dev, "dp");
	if (IS_ERR(dp->phy)) {
		dev_err(dp->dev, "no DP phy configured\n");
		ret = PTR_ERR(dp->phy);
		if (ret) {
			/*
			 * phy itself is not enabled, so we can move forward
			 * assigning NULL to phy pointer.
			 */
			if (ret == -ENOSYS || ret == -ENODEV)
				dp->phy = NULL;
			else
				return ret;
		}
	}

	if (!dp->panel) {
		ret = exynos_dp_dt_parse_panel(dp);
		if (ret)
			return ret;
	}

	dp->clock = devm_clk_get(&pdev->dev, "dp");
	if (IS_ERR(dp->clock)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(dp->clock);
	}

	clk_prepare_enable(dp->clock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dp->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dp->reg_base))
		return PTR_ERR(dp->reg_base);

	dp->hpd_gpio = of_get_named_gpio(dev->of_node, "samsung,hpd-gpio", 0);

	if (gpio_is_valid(dp->hpd_gpio)) {
		/*
		 * Set up the hotplug GPIO from the device tree as an interrupt.
		 * Simply specifying a different interrupt in the device tree
		 * doesn't work since we handle hotplug rather differently when
		 * using a GPIO.  We also need the actual GPIO specifier so
		 * that we can get the current state of the GPIO.
		 */
		ret = devm_gpio_request_one(&pdev->dev, dp->hpd_gpio, GPIOF_IN,
					    "hpd_gpio");
		if (ret) {
			dev_err(&pdev->dev, "failed to get hpd gpio\n");
			return ret;
		}
		dp->irq = gpio_to_irq(dp->hpd_gpio);
		irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	} else {
		dp->hpd_gpio = -ENODEV;
		dp->irq = platform_get_irq(pdev, 0);
		irq_flags = 0;
	}

	if (dp->irq == -ENXIO) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return -ENODEV;
	}

	INIT_WORK(&dp->hotplug_work, exynos_dp_hotplug);

	exynos_dp_phy_init(dp);

	exynos_dp_init_dp(dp);

	ret = devm_request_irq(&pdev->dev, dp->irq, exynos_dp_irq_handler,
			irq_flags, "exynos-dp", dp);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return ret;
	}
	disable_irq(dp->irq);

	dp->drm_dev = drm_dev;

	return exynos_drm_create_enc_conn(drm_dev, &dp->display);
}

static void exynos_dp_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct exynos_dp_device *dp = dev_get_drvdata(dev);

	exynos_dp_dpms(&dp->display, DRM_MODE_DPMS_OFF);
}

static const struct component_ops exynos_dp_ops = {
	.bind	= exynos_dp_bind,
	.unbind	= exynos_dp_unbind,
};

static int exynos_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *panel_node;
	struct exynos_dp_device *dp;
	int ret;

	dp = devm_kzalloc(&pdev->dev, sizeof(struct exynos_dp_device),
				GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->display.type = EXYNOS_DISPLAY_TYPE_LCD;
	dp->display.ops = &exynos_dp_display_ops;
	platform_set_drvdata(pdev, dp);

	ret = exynos_drm_component_add(&pdev->dev, EXYNOS_DEVICE_TYPE_CONNECTOR,
					dp->display.type);
	if (ret)
		return ret;

	panel_node = of_parse_phandle(dev->of_node, "panel", 0);
	if (panel_node) {
		dp->panel = of_drm_find_panel(panel_node);
		of_node_put(panel_node);
		if (!dp->panel)
			return -EPROBE_DEFER;
	}

	ret = component_add(&pdev->dev, &exynos_dp_ops);
	if (ret)
		exynos_drm_component_del(&pdev->dev,
						EXYNOS_DEVICE_TYPE_CONNECTOR);

	return ret;
}

static int exynos_dp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &exynos_dp_ops);
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CONNECTOR);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_dp_suspend(struct device *dev)
{
	struct exynos_dp_device *dp = dev_get_drvdata(dev);

	exynos_dp_dpms(&dp->display, DRM_MODE_DPMS_OFF);
	return 0;
}

static int exynos_dp_resume(struct device *dev)
{
	struct exynos_dp_device *dp = dev_get_drvdata(dev);

	exynos_dp_dpms(&dp->display, DRM_MODE_DPMS_ON);
	return 0;
}
#endif

static const struct dev_pm_ops exynos_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_dp_suspend, exynos_dp_resume)
};

static const struct of_device_id exynos_dp_match[] = {
	{ .compatible = "samsung,exynos5-dp" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_dp_match);

struct platform_driver dp_driver = {
	.probe		= exynos_dp_probe,
	.remove		= exynos_dp_remove,
	.driver		= {
		.name	= "exynos-dp",
		.owner	= THIS_MODULE,
		.pm	= &exynos_dp_pm_ops,
		.of_match_table = exynos_dp_match,
	},
};

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DP Driver");
MODULE_LICENSE("GPL v2");
