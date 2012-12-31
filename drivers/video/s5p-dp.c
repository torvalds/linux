/*
 * Samsung S5P SoC series DP (Display Port) interface driver.
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <plat/dp.h>
#include <plat/regs-dp.h>
#include <plat/cpu.h>

#include "s5p-dp.h"

static int s5p_dp_init_dp(struct s5p_dp_device *dp)
{
	s5p_dp_reset(dp);

	/* SW defined function Normal operation */
	s5p_dp_enable_sw_function(dp);

	if (!soc_is_exynos5250())
		s5p_dp_config_interrupt(dp);

	s5p_dp_init_analog_func(dp);

	s5p_dp_init_hpd(dp);
	s5p_dp_init_aux(dp);

	return 0;
}

static int s5p_dp_detect_hpd(struct s5p_dp_device *dp)
{
	int timeout_loop = 0;

	s5p_dp_init_hpd(dp);

	udelay(200);

	while (s5p_dp_get_plug_in_status(dp) != 0) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "failed to get hpd plug status\n");
			return -ETIMEDOUT;
		}
		udelay(10);
	}

	return 0;
}

static unsigned char s5p_dp_calc_edid_check_sum(unsigned char *edid_data)
{
	int i;
	unsigned char sum = 0;

	for (i = 0; i < EDID_BLOCK_LENGTH; i++)
		sum = sum + edid_data[i];

	return sum;
}

static int s5p_dp_read_edid(struct s5p_dp_device *dp)
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
	s5p_dp_read_byte_from_i2c(dp, I2C_EDID_DEVICE_ADDR,
				EDID_EXTENSION_FLAG,
				&extend_block);

	if (extend_block > 0) {
		dev_dbg(dp->dev, "EDID data includes a single extension!\n");

		/* Read EDID data */
		retval = s5p_dp_read_bytes_from_i2c(dp, I2C_EDID_DEVICE_ADDR,
						EDID_HEADER_PATTERN,
						EDID_BLOCK_LENGTH,
						&edid[EDID_HEADER_PATTERN]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = s5p_dp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		/* Read additional EDID data */
		retval = s5p_dp_read_bytes_from_i2c(dp,
				I2C_EDID_DEVICE_ADDR,
				EDID_BLOCK_LENGTH,
				EDID_BLOCK_LENGTH,
				&edid[EDID_BLOCK_LENGTH]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = s5p_dp_calc_edid_check_sum(&edid[EDID_BLOCK_LENGTH]);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		s5p_dp_read_byte_from_dpcd(dp, DPCD_ADDR_TEST_REQUEST,
					&test_vector);
		if (test_vector & DPCD_TEST_EDID_READ) {
			s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TEST_EDID_CHECKSUM,
				edid[EDID_BLOCK_LENGTH + EDID_CHECKSUM]);
			s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TEST_RESPONSE,
				DPCD_TEST_EDID_CHECKSUM_WRITE);
		}
	} else {
		dev_info(dp->dev, "EDID data does not include any extensions.\n");

		/* Read EDID data */
		retval = s5p_dp_read_bytes_from_i2c(dp,
				I2C_EDID_DEVICE_ADDR,
				EDID_HEADER_PATTERN,
				EDID_BLOCK_LENGTH,
				&edid[EDID_HEADER_PATTERN]);
		if (retval != 0) {
			dev_err(dp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = s5p_dp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_err(dp->dev, "EDID bad checksum!\n");
			return -EIO;
		}

		s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_TEST_REQUEST,
			&test_vector);
		if (test_vector & DPCD_TEST_EDID_READ) {
			s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TEST_EDID_CHECKSUM,
				edid[EDID_CHECKSUM]);
			s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TEST_RESPONSE,
				DPCD_TEST_EDID_CHECKSUM_WRITE);
		}
	}

	dev_err(dp->dev, "EDID Read success!\n");
	return 0;
}

static int s5p_dp_handle_edid(struct s5p_dp_device *dp)
{
	u8 buf[12];
	int i;
	int retval;

	/* Read DPCD 0x0000-0x000b */
	s5p_dp_read_bytes_from_dpcd(dp,
		DPCD_ADDR_DPCD_REV,
		12, buf);

	/* Read EDID */
	for (i = 0; i < 3; i++) {
		retval = s5p_dp_read_edid(dp);
		if (retval == 0)
			break;
	}

	return retval;
}

static void s5p_dp_enable_rx_to_enhanced_mode(struct s5p_dp_device *dp,
						bool enable)
{
	u8 data;

	s5p_dp_read_byte_from_dpcd(dp, DPCD_ADDR_LANE_COUNT_SET, &data);

	if (enable)
		s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_LANE_COUNT_SET,
			DPCD_ENHANCED_FRAME_EN |
			DPCD_LANE_COUNT_SET(data));
	else
		s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_LANE_COUNT_SET,
			DPCD_LANE_COUNT_SET(data));
}

#ifndef HW_LINK_TRAINING
static int s5p_dp_is_enhanced_mode_available(struct s5p_dp_device *dp)
{
	u8 data;
	int retval;

	s5p_dp_read_byte_from_dpcd(dp, DPCD_ADDR_MAX_LANE_COUNT, &data);
	retval = DPCD_ENHANCED_FRAME_CAP(data);

	return retval;
}

static void s5p_dp_set_enhanced_mode(struct s5p_dp_device *dp)
{
	u8 data;

	data = s5p_dp_is_enhanced_mode_available(dp);
	s5p_dp_enable_rx_to_enhanced_mode(dp, data);
	s5p_dp_enable_enhanced_mode(dp, data);
}

static void s5p_dp_training_pattern_dis(struct s5p_dp_device *dp)
{
	s5p_dp_set_training_pattern(dp, DP_NONE);

	s5p_dp_write_byte_to_dpcd(dp,
		DPCD_ADDR_TRAINING_PATTERN_SET,
		DPCD_TRAINING_PATTERN_DISABLED);
}

static void s5p_dp_link_start(struct s5p_dp_device *dp)
{
	u8 buf[5];

	dp->link_train.lt_state = CLOCK_RECOVERY;
	dp->link_train.eq_loop = 0;
	dp->link_train.cr_loop[0] = 0;
	dp->link_train.cr_loop[1] = 0;
	dp->link_train.cr_loop[2] = 0;
	dp->link_train.cr_loop[3] = 0;

	/* Set sink to D0 (Sink Not Ready) mode. */
	s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_SINK_POWER_STATE,
				DPCD_SET_POWER_STATE_D0);

	/* Set link rate and count as you want to establish*/
	s5p_dp_set_link_bandwidth(dp, dp->link_train.link_rate);
	s5p_dp_set_lane_count(dp, dp->link_train.lane_count);

	/* Setup RX configuration */
	buf[0] = dp->link_train.link_rate;
	buf[1] = dp->link_train.lane_count;
	s5p_dp_write_bytes_to_dpcd(dp, DPCD_ADDR_LINK_BW_SET,
				2, buf);

	/* Set TX pre-emphasis to minimum */
	s5p_dp_set_lane0_pre_emphasis(dp, PRE_EMPHASIS_LEVEL_0);
	s5p_dp_set_lane1_pre_emphasis(dp, PRE_EMPHASIS_LEVEL_0);
	s5p_dp_set_lane2_pre_emphasis(dp, PRE_EMPHASIS_LEVEL_0);
	s5p_dp_set_lane3_pre_emphasis(dp, PRE_EMPHASIS_LEVEL_0);

	/* Set training pattern 1 */
	s5p_dp_set_training_pattern(dp, TRAINING_PTN1);

	/* Set RX training pattern */
	buf[0] = DPCD_SCRAMBLING_DISABLED |
		DPCD_TRAINING_PATTERN_1;

	buf[1] = DPCD_PRE_EMPHASIS_SET_PATTERN_2_LEVEL_0 |
		DPCD_VOLTAGE_SWING_SET_PATTERN_1_LEVEL_0;
	buf[2] = DPCD_PRE_EMPHASIS_SET_PATTERN_2_LEVEL_0 |
		DPCD_VOLTAGE_SWING_SET_PATTERN_1_LEVEL_0;
	buf[3] = DPCD_PRE_EMPHASIS_SET_PATTERN_2_LEVEL_0 |
		DPCD_VOLTAGE_SWING_SET_PATTERN_1_LEVEL_0;
	buf[4] = DPCD_PRE_EMPHASIS_SET_PATTERN_2_LEVEL_0 |
		DPCD_VOLTAGE_SWING_SET_PATTERN_1_LEVEL_0;

	s5p_dp_write_bytes_to_dpcd(dp,
		DPCD_ADDR_TRAINING_PATTERN_SET,
		5, buf);
}

static int s5p_dp_process_clock_recovery(struct s5p_dp_device *dp)
{
	u32 reg;
	u8 data;
	u8 lane0_1_status;
	u8 lane2_3_status;
	u8 adjust_requst_lane0_1;
	u8 adjust_requst_lane2_3;
	u8 buf[6];

	u8 all_cr_done0_1 = 0;
	u8 voltage_swing_lane0 = 0;
	u8 voltage_swing_lane1 = 0;
	u8 pre_emphasis_lane0 = 0;
	u8 pre_emphasis_lane1 = 0;
	u8 training_lane0_set = 0;
	u8 training_lane1_set = 0;

	u8 all_cr_done2_3 = 0;
	u8 voltage_swing_lane2 = 0;
	u8 voltage_swing_lane3 = 0;
	u8 pre_emphasis_lane2 = 0;
	u8 pre_emphasis_lane3 = 0;
	u8 training_lane2_set = 0;
	u8 training_lane3_set = 0;

	udelay(100);

	if (dp->link_train.lane_count == 4) {
		/* lane 0,1,2,3 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		lane2_3_status = buf[1];
		adjust_requst_lane0_1 = buf[4];
		adjust_requst_lane2_3 = buf[5];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		dev_dbg(dp->dev, "Reading lane status: lane2_3_status = %.2x\n",
			(u32)lane2_3_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane2_3 = %.2x\n",
			(u32)adjust_requst_lane2_3);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		all_cr_done2_3 = lane2_3_status;
		all_cr_done2_3 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if ((all_cr_done0_1 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE)) &&
		    (all_cr_done2_3 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE))) {
			dev_dbg(dp->dev, "Clock Recovery training succeed\n");

			/* set training pattern 2 for EQ */
			s5p_dp_set_training_pattern(dp, TRAINING_PTN2);

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			/* Lane 1 setting */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			training_lane1_set =
				DRIVE_CURRENT_SET_1_SET(voltage_swing_lane1) |
				PRE_EMPHASIS_SET_1_SET(pre_emphasis_lane1);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
				training_lane1_set |= MAX_DRIVE_CURRENT_REACH_1;
				training_lane1_set |= MAX_PRE_EMPHASIS_REACH_1;
			}

			/* Lane 2 setting */
			voltage_swing_lane2 =
				DPCD_VOLTAGE_SWING_LANE2(adjust_requst_lane2_3);
			pre_emphasis_lane2 =
				DPCD_PRE_EMPHASIS_LANE2(adjust_requst_lane2_3);

			training_lane2_set =
				DRIVE_CURRENT_SET_2_SET(voltage_swing_lane2) |
				PRE_EMPHASIS_SET_2_SET(pre_emphasis_lane2);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane2 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane2 == PRE_EMPHASIS_LEVEL_3) {
				training_lane2_set |= MAX_DRIVE_CURRENT_REACH_2;
				training_lane2_set |= MAX_PRE_EMPHASIS_REACH_2;
			}

			/* Lane 3 setting */
			voltage_swing_lane3 =
				DPCD_VOLTAGE_SWING_LANE3(adjust_requst_lane2_3);
			pre_emphasis_lane3 =
				DPCD_PRE_EMPHASIS_LANE3(adjust_requst_lane2_3);

			training_lane3_set =
				DRIVE_CURRENT_SET_3_SET(voltage_swing_lane3) |
				PRE_EMPHASIS_SET_3_SET(pre_emphasis_lane3);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane3 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane3 == PRE_EMPHASIS_LEVEL_3) {
				training_lane3_set |= MAX_DRIVE_CURRENT_REACH_3;
				training_lane3_set |= MAX_PRE_EMPHASIS_REACH_3;
			}

			s5p_dp_set_lane0_link_training(dp, training_lane0_set);
			s5p_dp_set_lane1_link_training(dp, training_lane1_set);
			s5p_dp_set_lane2_link_training(dp, training_lane2_set);
			s5p_dp_set_lane3_link_training(dp, training_lane3_set);

			/* Write TRAINING_PATTERN_SET */
			buf[0] = DPCD_SCRAMBLING_DISABLED |
				 DPCD_TRAINING_PATTERN_2;
			buf[1] = training_lane0_set;
			buf[2] = training_lane1_set;
			buf[3] = training_lane2_set;
			buf[4] = training_lane3_set;

			s5p_dp_write_bytes_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				5, buf);

			dp->link_train.lt_state = EQUALIZER_TRAINING;
		} else {
			reg = s5p_dp_get_lane0_link_training(dp);
			training_lane0_set = (u8)reg;
			reg = s5p_dp_get_lane1_link_training(dp);
			training_lane1_set = (u8)reg;
			reg = s5p_dp_get_lane2_link_training(dp);
			training_lane2_set = (u8)reg;
			reg = s5p_dp_get_lane3_link_training(dp);
			training_lane3_set = (u8)reg;
			s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
				&data);
			adjust_requst_lane0_1 = data;

			s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_ADJUST_REQUEST_LANE2_3,
				&data);
			adjust_requst_lane2_3 = data;

			/* lane 0 same voltage count */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			if ((DRIVE_CURRENT_SET_0_GET(training_lane0_set) ==
			   voltage_swing_lane0) &&
			   (PRE_EMPHASIS_SET_0_GET(training_lane0_set) ==
			   pre_emphasis_lane0))
				dp->link_train.cr_loop[0]++;

			/* lane 1 same voltage count */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			if ((DRIVE_CURRENT_SET_1_GET(training_lane1_set) ==
			   voltage_swing_lane1) &&
			   (PRE_EMPHASIS_SET_1_GET(training_lane1_set) ==
			   pre_emphasis_lane1))
				dp->link_train.cr_loop[1]++;

			/* lane 2 same voltage count */
			voltage_swing_lane2 =
				DPCD_VOLTAGE_SWING_LANE2(adjust_requst_lane2_3);
			pre_emphasis_lane2 =
				DPCD_PRE_EMPHASIS_LANE2(adjust_requst_lane2_3);

			if ((DRIVE_CURRENT_SET_2_GET(training_lane2_set) ==
			   voltage_swing_lane2) &&
			   (PRE_EMPHASIS_SET_2_GET(training_lane2_set) ==
			   pre_emphasis_lane2))
				dp->link_train.cr_loop[2]++;

			/* lane 3 same voltage count */
			voltage_swing_lane3 =
				DPCD_VOLTAGE_SWING_LANE3(adjust_requst_lane2_3);
			pre_emphasis_lane3 =
				DPCD_PRE_EMPHASIS_LANE3(adjust_requst_lane2_3);

			if ((DRIVE_CURRENT_SET_3_GET(training_lane3_set) ==
			   voltage_swing_lane3) &&
			   (PRE_EMPHASIS_SET_3_GET(training_lane3_set) ==
			   pre_emphasis_lane3))
				dp->link_train.cr_loop[3]++;

			/*
			 * if max swing reached or same voltage 5 times,
			 * try reduced bit-rate
			 */
			if (((voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   voltage_swing_lane1 == PRE_EMPHASIS_LEVEL_3) ||
			   (dp->link_train.cr_loop[0] == MAX_CR_LOOP ||
			   dp->link_train.cr_loop[1] == MAX_CR_LOOP))
				|| ((voltage_swing_lane2 == VOLTAGE_LEVEL_3 ||
			   voltage_swing_lane3 == PRE_EMPHASIS_LEVEL_3) ||
			   (dp->link_train.cr_loop[2] == MAX_CR_LOOP ||
			   dp->link_train.cr_loop[3] == MAX_CR_LOOP))) {
				/* try reduced bit rate */
				if (dp->link_train.link_rate ==
				    LINK_RATE_2_70GBPS) {
					/* set to reduced bit rate */
					dp->link_train.link_rate =
						LINK_RATE_1_62GBPS;
					dev_err(dp->dev, "set to bandwidth %.2x\n",
						dp->link_train.link_rate);
					dp->link_train.lt_state = START;
				} else {
					/* bit-rate already reduced */
					/*
					 * traing pattern: Set to Normal,
					 * and enable scramble
					 */
					s5p_dp_training_pattern_dis(dp);

					/* set enhanced mode if available */
					s5p_dp_set_enhanced_mode(dp);

					dp->link_train.lt_state = FAILED;
				}
			} else {
				/*
				 * increase voltage swing as requested,
				 * write an updated value
				 */

				/* Lane 0 setting */
				voltage_swing_lane0 =
					DPCD_VOLTAGE_SWING_LANE0(
						adjust_requst_lane0_1);
				pre_emphasis_lane0 =
					DPCD_PRE_EMPHASIS_LANE0(
						adjust_requst_lane0_1);

				training_lane0_set =
					DRIVE_CURRENT_SET_0_SET(
						voltage_swing_lane0) |
					PRE_EMPHASIS_SET_0_SET(
						pre_emphasis_lane0);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
					training_lane0_set |=
						MAX_DRIVE_CURRENT_REACH_0;
					training_lane0_set |=
						MAX_PRE_EMPHASIS_REACH_0;
				}

				/* Lane 1 setting */
				voltage_swing_lane1 =
					DPCD_VOLTAGE_SWING_LANE1(
						adjust_requst_lane0_1);
				pre_emphasis_lane1 =
					DPCD_PRE_EMPHASIS_LANE1(
						adjust_requst_lane0_1);

				training_lane1_set =
					DRIVE_CURRENT_SET_1_SET(
						voltage_swing_lane1) |
					PRE_EMPHASIS_SET_1_SET(
						pre_emphasis_lane1);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
					training_lane1_set |=
						MAX_DRIVE_CURRENT_REACH_1;
					training_lane1_set |=
						MAX_PRE_EMPHASIS_REACH_1;
				}

				/* Lane 2 setting */
				voltage_swing_lane2 =
					DPCD_VOLTAGE_SWING_LANE2(
						adjust_requst_lane2_3);
				pre_emphasis_lane2 =
					DPCD_PRE_EMPHASIS_LANE2(
						adjust_requst_lane2_3);

				training_lane2_set =
					DRIVE_CURRENT_SET_2_SET(
						voltage_swing_lane2) |
					PRE_EMPHASIS_SET_2_SET(
						pre_emphasis_lane2);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane2 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane2 == PRE_EMPHASIS_LEVEL_3) {
					training_lane2_set |=
						MAX_DRIVE_CURRENT_REACH_0;
					training_lane2_set |=
						MAX_PRE_EMPHASIS_REACH_0;
				}

				/* Lane 3 setting */
				voltage_swing_lane3 =
					DPCD_VOLTAGE_SWING_LANE3(
						adjust_requst_lane2_3);
				pre_emphasis_lane3 =
					DPCD_PRE_EMPHASIS_LANE3(
						adjust_requst_lane2_3);

				training_lane3_set =
					DRIVE_CURRENT_SET_3_SET(
						voltage_swing_lane3) |
					PRE_EMPHASIS_SET_3_SET(
						pre_emphasis_lane3);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane3 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane3 == PRE_EMPHASIS_LEVEL_3) {
					training_lane3_set |=
						MAX_DRIVE_CURRENT_REACH_3;
					training_lane3_set |=
						MAX_PRE_EMPHASIS_REACH_3;
				}

				s5p_dp_set_lane0_link_training(dp,
					training_lane0_set);
				s5p_dp_set_lane1_link_training(dp,
					training_lane1_set);
				s5p_dp_set_lane2_link_training(dp,
					training_lane2_set);
				s5p_dp_set_lane3_link_training(dp,
					training_lane3_set);

				/*
				 * Write TRAINING_LANE0_SET
				 * and TRAINING_LANE1_SET
				 */
				buf[0] = training_lane0_set;
				buf[1] = training_lane1_set;
				buf[2] = training_lane2_set;
				buf[3] = training_lane3_set;
				s5p_dp_write_bytes_to_dpcd(dp,
					DPCD_ADDR_TRAINING_LANE0_SET,
					4, buf);
			}
		}
	} else if (dp->link_train.lane_count == 2) {
		/* lane 0,1 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		adjust_requst_lane0_1 = buf[4];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if (all_cr_done0_1 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE)) {
			dev_dbg(dp->dev, "Clock Recovery training succeed\n");

			/* set training pattern 2 for EQ */
			s5p_dp_set_training_pattern(dp, TRAINING_PTN2);

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			/* Lane 1 setting */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			training_lane1_set =
				DRIVE_CURRENT_SET_1_SET(voltage_swing_lane1) |
				PRE_EMPHASIS_SET_1_SET(pre_emphasis_lane1);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
				training_lane1_set |= MAX_DRIVE_CURRENT_REACH_1;
				training_lane1_set |= MAX_PRE_EMPHASIS_REACH_1;
			}

			s5p_dp_set_lane0_link_training(dp, training_lane0_set);
			s5p_dp_set_lane1_link_training(dp, training_lane1_set);

			/* Write TRAINING_PATTERN_SET */
			buf[0] = DPCD_SCRAMBLING_DISABLED |
				 DPCD_TRAINING_PATTERN_2;
			buf[1] = training_lane0_set;
			buf[2] = training_lane1_set;

			s5p_dp_write_bytes_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				3, buf);

			dp->link_train.lt_state = EQUALIZER_TRAINING;
		} else {
			reg = s5p_dp_get_lane0_link_training(dp);
			training_lane0_set = (u8)reg;
			reg = s5p_dp_get_lane1_link_training(dp);
			training_lane1_set = (u8)reg;

			s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
				&data);
			adjust_requst_lane0_1 = data;

			/* lane 0 same voltage count */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			if ((DRIVE_CURRENT_SET_0_GET(training_lane0_set) ==
			   voltage_swing_lane0) &&
			   (PRE_EMPHASIS_SET_0_GET(training_lane0_set) ==
			   pre_emphasis_lane0))
				dp->link_train.cr_loop[0]++;

			/* lane1 same voltage count */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			if ((DRIVE_CURRENT_SET_1_GET(training_lane0_set) ==
			   voltage_swing_lane1) &&
			   (PRE_EMPHASIS_SET_1_GET(training_lane0_set) ==
			   pre_emphasis_lane1))
				dp->link_train.cr_loop[1]++;

			/*
			 * if max swing reached or same voltage 5 times,
			 * try reduced bit-rate
			 */
			if ((voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   voltage_swing_lane1 == PRE_EMPHASIS_LEVEL_3) ||
			   (dp->link_train.cr_loop[0] == MAX_CR_LOOP ||
			   dp->link_train.cr_loop[1] == MAX_CR_LOOP)) {
				/* try reduced bit rate */
				if (dp->link_train.link_rate ==
				    LINK_RATE_2_70GBPS) {
					/* set to reduced bit rate */
					dp->link_train.link_rate =
						LINK_RATE_1_62GBPS;
					dev_err(dp->dev, "set to bandwidth %.2x\n",
						dp->link_train.link_rate);
					dp->link_train.lt_state = START;
				} else {
					/* bit-rate already reduced */
					/*
					 * traing pattern: Set to Normal,
					 * and enable scramble
					 */
					s5p_dp_training_pattern_dis(dp);

					/* set enhanced mode if available */
					s5p_dp_set_enhanced_mode(dp);

					dp->link_train.lt_state = FAILED;
				}
			} else {
				/*
				 * increase voltage swing as requested,
				 * write an updated value
				 */

				/* Lane 0 setting */
				voltage_swing_lane0 =
					DPCD_VOLTAGE_SWING_LANE0(
						adjust_requst_lane0_1);
				pre_emphasis_lane0 =
					DPCD_PRE_EMPHASIS_LANE0(
						adjust_requst_lane0_1);

				training_lane0_set =
					DRIVE_CURRENT_SET_0_SET(
						voltage_swing_lane0) |
					PRE_EMPHASIS_SET_0_SET(
						pre_emphasis_lane0);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
					training_lane0_set |=
						MAX_DRIVE_CURRENT_REACH_0;
					training_lane0_set |=
						MAX_PRE_EMPHASIS_REACH_0;
				}

				/* Lane 1 setting */
				voltage_swing_lane1 =
					DPCD_VOLTAGE_SWING_LANE1(
						adjust_requst_lane0_1);
				pre_emphasis_lane1 =
					DPCD_PRE_EMPHASIS_LANE1(
						adjust_requst_lane0_1);

				training_lane1_set =
					DRIVE_CURRENT_SET_1_SET(
						voltage_swing_lane1) |
					PRE_EMPHASIS_SET_1_SET(
						pre_emphasis_lane1);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
					training_lane1_set |=
						MAX_DRIVE_CURRENT_REACH_1;
					training_lane1_set |=
						MAX_PRE_EMPHASIS_REACH_1;
				}

				s5p_dp_set_lane0_link_training(dp,
					training_lane0_set);
				s5p_dp_set_lane1_link_training(dp,
					training_lane1_set);

				/*
				 * Write TRAINING_LANE0_SET
				 * and TRAINING_LANE1_SET
				 */
				buf[0] = training_lane0_set;
				buf[1] = training_lane1_set;
				s5p_dp_write_bytes_to_dpcd(dp,
					DPCD_ADDR_TRAINING_LANE0_SET,
					2, buf);
			}
		}
	} else {
		/* one lane */
		/* lane 0,1 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		adjust_requst_lane0_1 = buf[4];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if (all_cr_done0_1 == DPCD_LANE0_CR_DONE) {
			dev_dbg(dp->dev, "Clock Recovery training succeed\n");

			/* set training pattern 2 for EQ */
			s5p_dp_set_training_pattern(dp, TRAINING_PTN2);

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			s5p_dp_set_lane0_link_training(dp, training_lane0_set);

			/* Write TRAINING_PATTERN_SET */
			buf[0] = DPCD_SCRAMBLING_DISABLED |
				 DPCD_TRAINING_PATTERN_2;
			buf[1] = training_lane0_set;

			s5p_dp_write_bytes_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				2, buf);

			dp->link_train.lt_state = EQUALIZER_TRAINING;
		} else {
			reg = s5p_dp_get_lane0_link_training(dp);
			training_lane0_set = (u8)reg;

			s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
				&data);
			adjust_requst_lane0_1 = data;

			/* lane 0 same voltage count */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			if ((DRIVE_CURRENT_SET_0_GET(training_lane0_set) ==
			   voltage_swing_lane0) &&
			   (PRE_EMPHASIS_SET_0_GET(training_lane0_set) ==
			   pre_emphasis_lane0))
				dp->link_train.cr_loop[0]++;

			/*
			 * if max swing reached or same voltage 5 times,
			 * try reduced bit-rate
			 */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   dp->link_train.cr_loop[0] == MAX_CR_LOOP) {
				/* try reduced bit rate */
				if (dp->link_train.link_rate ==
				    LINK_RATE_2_70GBPS) {
					/* set to reduced bit rate */
					dp->link_train.link_rate =
						LINK_RATE_1_62GBPS;
					dev_err(dp->dev, "set to bandwidth %.2x\n",
						dp->link_train.link_rate);
					dp->link_train.lt_state = START;
				} else {
					/* bit-rate already reduced */
					/*
					 * traing pattern: Set to Normal,
					 * and enable scramble
					 */
					s5p_dp_training_pattern_dis(dp);

					/* set enhanced mode if available */
					s5p_dp_set_enhanced_mode(dp);

					dp->link_train.lt_state = FAILED;
				}
			} else {
				/*
				 * increase voltage swing as requested,
				 * write an updated value
				 */

				/* Lane 0 setting */
				voltage_swing_lane0 =
					DPCD_VOLTAGE_SWING_LANE0(
						adjust_requst_lane0_1);
				pre_emphasis_lane0 =
					DPCD_PRE_EMPHASIS_LANE0(
						adjust_requst_lane0_1);

				training_lane0_set =
					DRIVE_CURRENT_SET_0_SET(
						voltage_swing_lane0) |
					PRE_EMPHASIS_SET_0_SET(
						pre_emphasis_lane0);

				/* max swing reached or max pre-emphasis */
				if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
				   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
					training_lane0_set |=
						MAX_DRIVE_CURRENT_REACH_0;
					training_lane0_set |=
						MAX_PRE_EMPHASIS_REACH_0;
				}

				s5p_dp_set_lane0_link_training(dp,
					training_lane0_set);

				/* Write TRAINING_LANE0_SET */
				buf[0] = training_lane0_set;
				s5p_dp_write_bytes_to_dpcd(dp,
					DPCD_ADDR_TRAINING_LANE0_SET,
					1, buf);
			}
		}
	}

	return 0;
}

static int s5p_dp_process_equalizer_training(struct s5p_dp_device *dp)
{
	u32 data;
	u8 lane0_1_status;
	u8 lane2_3_status;
	u8 adjust_requst_lane0_1;
	u8 adjust_requst_lane2_3;
	u8 lane_align_status_updated;
	u8 buf[6];

	u8 all_cr_done0_1 = 0;
	u8 channel_eq_done0_1 = 0;
	u8 voltage_swing_lane0 = 0;
	u8 pre_emphasis_lane0 = 0;
	u8 voltage_swing_lane1 = 0;
	u8 pre_emphasis_lane1 = 0;
	u8 training_lane0_set = 0;
	u8 training_lane1_set = 0;

	u8 all_cr_done2_3 = 0;
	u8 channel_eq_done2_3 = 0;
	u8 voltage_swing_lane2 = 0;
	u8 pre_emphasis_lane2 = 0;
	u8 voltage_swing_lane3 = 0;
	u8 pre_emphasis_lane3 = 0;
	u8 training_lane2_set = 0;
	u8 training_lane3_set = 0;

	u8 interlane_aligned = 0;

	udelay(400);

	if (dp->link_train.lane_count == 4) {
		/* lane 0,1,2,3 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		lane2_3_status = buf[1];
		adjust_requst_lane0_1 = buf[4];
		adjust_requst_lane2_3 = buf[5];
		lane_align_status_updated = buf[2];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		dev_dbg(dp->dev, "Reading lane status: lane2_3_status = %.2x\n",
			(u32)lane2_3_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane2_3 = %.2x\n",
			(u32)adjust_requst_lane2_3);

		dev_dbg(dp->dev, "Reading lane status: lane_align_status_updated = %.2x\n",
			(u32)lane_align_status_updated);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		all_cr_done2_3 = lane2_3_status;
		all_cr_done2_3 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if ((all_cr_done0_1 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE)) &&
		    (all_cr_done2_3 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE))) {

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			/* Lane 1 setting */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			training_lane1_set =
				DRIVE_CURRENT_SET_1_SET(voltage_swing_lane1) |
				PRE_EMPHASIS_SET_1_SET(pre_emphasis_lane1);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
				training_lane1_set |= MAX_DRIVE_CURRENT_REACH_1;
				training_lane1_set |= MAX_PRE_EMPHASIS_REACH_1;
			}

			/* Lane 2 setting */
			voltage_swing_lane2 =
				DPCD_VOLTAGE_SWING_LANE2(adjust_requst_lane2_3);
			pre_emphasis_lane2 =
				DPCD_PRE_EMPHASIS_LANE2(adjust_requst_lane2_3);

			training_lane2_set =
				DRIVE_CURRENT_SET_2_SET(voltage_swing_lane2) |
				PRE_EMPHASIS_SET_2_SET(pre_emphasis_lane2);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane2 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane2 == PRE_EMPHASIS_LEVEL_3) {
				training_lane2_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane2_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			/* Lane 3 setting */
			voltage_swing_lane3 =
				DPCD_VOLTAGE_SWING_LANE3(adjust_requst_lane2_3);
			pre_emphasis_lane3 =
				DPCD_PRE_EMPHASIS_LANE3(adjust_requst_lane2_3);

			training_lane3_set =
				DRIVE_CURRENT_SET_3_SET(voltage_swing_lane3) |
				PRE_EMPHASIS_SET_3_SET(pre_emphasis_lane3);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane3 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane3 == PRE_EMPHASIS_LEVEL_3) {
				training_lane3_set |= MAX_DRIVE_CURRENT_REACH_3;
				training_lane3_set |= MAX_PRE_EMPHASIS_REACH_3;
			}

			channel_eq_done0_1 = lane0_1_status;
			channel_eq_done0_1 &= DPCD_LANE1_SYMBOL_LOCKED |
					   DPCD_LANE1_CHANNEL_EQ_DONE |
					   DPCD_LANE0_SYMBOL_LOCKED |
					   DPCD_LANE0_CHANNEL_EQ_DONE;
			channel_eq_done2_3 = lane2_3_status;
			channel_eq_done2_3 &= DPCD_LANE1_SYMBOL_LOCKED |
					   DPCD_LANE1_CHANNEL_EQ_DONE |
					   DPCD_LANE0_SYMBOL_LOCKED |
					   DPCD_LANE0_CHANNEL_EQ_DONE;
			interlane_aligned = lane_align_status_updated;
			interlane_aligned &= DPCD_INTERLANE_ALIGN_DONE;

			if ((channel_eq_done0_1 ==
			    (DPCD_LANE1_SYMBOL_LOCKED |
			     DPCD_LANE1_CHANNEL_EQ_DONE |
			     DPCD_LANE0_SYMBOL_LOCKED |
			     DPCD_LANE0_CHANNEL_EQ_DONE)) &&
				(channel_eq_done2_3 ==
			    (DPCD_LANE1_SYMBOL_LOCKED |
			     DPCD_LANE1_CHANNEL_EQ_DONE |
			     DPCD_LANE0_SYMBOL_LOCKED |
			     DPCD_LANE0_CHANNEL_EQ_DONE)) &&
			    (interlane_aligned == DPCD_INTERLANE_ALIGN_DONE)) {
				/* EQ succeed */
				/* traing pattern Set to Normal */
				s5p_dp_training_pattern_dis(dp);

				dev_info(dp->dev, "Link Training success!\n");

				s5p_dp_get_link_bandwidth(dp, &data);
				dp->link_train.link_rate = data;
				dev_dbg(dp->dev, "final bandwidth = %.2x\n",
					dp->link_train.link_rate);

				s5p_dp_get_lane_count(dp, &data);
				dp->link_train.lane_count = data;
				dev_dbg(dp->dev, "final lane count = %.2x\n",
					dp->link_train.lane_count);

				/* set enhanced mode if available */
				s5p_dp_set_enhanced_mode(dp);

				dp->link_train.lt_state = FINISHED;
			} else {
				/* not all locked */
				dp->link_train.eq_loop++;

				if (dp->link_train.eq_loop  > MAX_EQ_LOOP) {
					/* try reduced bit rate */
					if (dp->link_train.link_rate ==
					    LINK_RATE_2_70GBPS) {
						/* set to reduced bit rate */
						dp->link_train.link_rate =
							LINK_RATE_1_62GBPS;
						dev_err(dp->dev, "set to bandwidth %.2x\n",
							dp->link_train.
							link_rate);
						dp->link_train.lt_state =
							START;
					} else {
						/* bit-rate already reduced*/
						s5p_dp_training_pattern_dis(dp);

						/*
						 * set enhanced mode
						 * if available
						 */
						s5p_dp_set_enhanced_mode(dp);

						dp->link_train.lt_state =
							FAILED;
					}
				} else {
					/* adjust pre-emphasis level */
					s5p_dp_set_lane0_link_training(dp,
						training_lane0_set);
					s5p_dp_set_lane1_link_training(dp,
						training_lane1_set);
					s5p_dp_set_lane2_link_training(dp,
						training_lane2_set);
					s5p_dp_set_lane3_link_training(dp,
						training_lane3_set);

					/*
					 * Write TRAINING_LANE0_SET
					 * and TRAINING_LANE1_SET
					 */
					buf[0] = training_lane0_set;
					buf[1] = training_lane1_set;
					buf[2] = training_lane2_set;
					buf[3] = training_lane3_set;

					s5p_dp_write_bytes_to_dpcd(dp,
						DPCD_ADDR_TRAINING_LANE0_SET,
						4, buf);
				}
			}
		} else if (dp->link_train.link_rate == LINK_RATE_2_70GBPS) {
			/* try reduced bit rate and return to CR training */
			dp->link_train.link_rate = LINK_RATE_1_62GBPS;
			dev_err(dp->dev, "set to bandwidth %.2x\n",
				dp->link_train.link_rate);
			dp->link_train.lt_state = START;
		} else {
			/* bit-rate already reduced */
			/* traing pattern Set to Normal, and enable scramble */
			s5p_dp_training_pattern_dis(dp);

			/* set enhanced mode if available */
			s5p_dp_set_enhanced_mode(dp);

			dp->link_train.lt_state = FAILED;
		}
	} else if (dp->link_train.lane_count == 2) {
		/* lane 0,1 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		adjust_requst_lane0_1 = buf[4];
		lane_align_status_updated = buf[2];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		dev_dbg(dp->dev, "Reading lane status: lane_align_status_updated = %.2x\n",
			(u32)lane_align_status_updated);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if (all_cr_done0_1 ==
			(DPCD_LANE1_CR_DONE | DPCD_LANE0_CR_DONE)) {

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}
			/* Lane 1 setting */
			voltage_swing_lane1 =
				DPCD_VOLTAGE_SWING_LANE1(adjust_requst_lane0_1);
			pre_emphasis_lane1 =
				DPCD_PRE_EMPHASIS_LANE1(adjust_requst_lane0_1);

			training_lane1_set =
				DRIVE_CURRENT_SET_1_SET(voltage_swing_lane1) |
				PRE_EMPHASIS_SET_1_SET(pre_emphasis_lane1);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane1 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane1 == PRE_EMPHASIS_LEVEL_3) {
				training_lane1_set |= MAX_DRIVE_CURRENT_REACH_1;
				training_lane1_set |= MAX_PRE_EMPHASIS_REACH_1;
			}

			channel_eq_done0_1 = lane0_1_status;
			channel_eq_done0_1 &= DPCD_LANE1_SYMBOL_LOCKED |
					   DPCD_LANE1_CHANNEL_EQ_DONE |
					   DPCD_LANE0_SYMBOL_LOCKED |
					   DPCD_LANE0_CHANNEL_EQ_DONE;
			interlane_aligned = lane_align_status_updated;
			interlane_aligned &= DPCD_INTERLANE_ALIGN_DONE;

			if (channel_eq_done0_1 ==
			    (DPCD_LANE1_SYMBOL_LOCKED |
			     DPCD_LANE1_CHANNEL_EQ_DONE |
			     DPCD_LANE0_SYMBOL_LOCKED |
			     DPCD_LANE0_CHANNEL_EQ_DONE) &&
			    (interlane_aligned == DPCD_INTERLANE_ALIGN_DONE)) {
				/* EQ succeed */
				/* traing pattern Set to Normal */
				s5p_dp_training_pattern_dis(dp);

				dev_info(dp->dev, "Link Training success!\n");

				s5p_dp_get_link_bandwidth(dp, &data);
				dp->link_train.link_rate = data;
				dev_dbg(dp->dev, "final bandwidth = %.2x\n",
					dp->link_train.link_rate);

				s5p_dp_get_lane_count(dp, &data);
				dp->link_train.lane_count = data;
				dev_dbg(dp->dev, "final lane count = %.2x\n",
					dp->link_train.lane_count);

				/* set enhanced mode if available */
				s5p_dp_set_enhanced_mode(dp);

				dp->link_train.lt_state = FINISHED;
			} else {
				/* not all locked */
				dp->link_train.eq_loop++;

				if (dp->link_train.eq_loop  > MAX_EQ_LOOP) {
					/* try reduced bit rate */
					if (dp->link_train.link_rate ==
					    LINK_RATE_2_70GBPS) {
						/* set to reduced bit rate */
						dp->link_train.link_rate =
							LINK_RATE_1_62GBPS;
						dev_err(dp->dev, "set to bandwidth %.2x\n",
							dp->link_train.
							link_rate);
						dp->link_train.lt_state =
							START;
					} else {
						/* bit-rate already reduced*/
						s5p_dp_training_pattern_dis(dp);

						/*
						 * set enhanced mode
						 * if available
						 */
						s5p_dp_set_enhanced_mode(dp);

						dp->link_train.lt_state =
							FAILED;
					}
				} else {
					/* adjust pre-emphasis level */
					s5p_dp_set_lane0_link_training(dp,
						training_lane0_set);
					s5p_dp_set_lane1_link_training(dp,
						training_lane1_set);

					/*
					 * Write TRAINING_LANE0_SET
					 * and TRAINING_LANE1_SET
					 */
					buf[0] = training_lane0_set;
					buf[1] = training_lane1_set;

					s5p_dp_write_bytes_to_dpcd(dp,
						DPCD_ADDR_TRAINING_LANE0_SET,
						2, buf);
				}
			}
		} else if (dp->link_train.link_rate == LINK_RATE_2_70GBPS) {
			/* try reduced bit rate and return to CR training */
			dp->link_train.link_rate = LINK_RATE_1_62GBPS;
			dev_err(dp->dev, "set to bandwidth %.2x\n",
				dp->link_train.link_rate);
			dp->link_train.lt_state = START;
		} else {
			/* bit-rate already reduced */
			/* traing pattern Set to Normal, and enable scramble */
			s5p_dp_training_pattern_dis(dp);

			/* set enhanced mode if available */
			s5p_dp_set_enhanced_mode(dp);

			dp->link_train.lt_state = FAILED;
		}
	} else {
		/* one lane */
		/* lane 0,1 status */
		s5p_dp_read_bytes_from_dpcd(dp, DPCD_ADDR_LANE0_1_STATUS,
					6, buf);
		lane0_1_status = buf[0];
		adjust_requst_lane0_1 = buf[4];
		lane_align_status_updated = buf[2];

		dev_dbg(dp->dev, "Reading lane status: lane0_1_status = %.2x\n",
			(u32)lane0_1_status);
		dev_dbg(dp->dev, "Reading lane status: adjust_requst_lane0_1 = %.2x\n",
			(u32)adjust_requst_lane0_1);

		dev_dbg(dp->dev, "Reading lane status: lane_align_status_updated = %.2x\n",
			(u32)lane_align_status_updated);

		all_cr_done0_1 = lane0_1_status;
		all_cr_done0_1 &= DPCD_LANE0_CR_DONE;

		/* all channel CR done */
		if (all_cr_done0_1 == DPCD_LANE0_CR_DONE) {

			/* Lane 0 setting */
			voltage_swing_lane0 =
				DPCD_VOLTAGE_SWING_LANE0(adjust_requst_lane0_1);
			pre_emphasis_lane0 =
				DPCD_PRE_EMPHASIS_LANE0(adjust_requst_lane0_1);

			training_lane0_set =
				DRIVE_CURRENT_SET_0_SET(voltage_swing_lane0) |
				PRE_EMPHASIS_SET_0_SET(pre_emphasis_lane0);

			/* max swing reached or max pre-emphasis */
			if (voltage_swing_lane0 == VOLTAGE_LEVEL_3 ||
			   pre_emphasis_lane0 == PRE_EMPHASIS_LEVEL_3) {
				training_lane0_set |= MAX_DRIVE_CURRENT_REACH_0;
				training_lane0_set |= MAX_PRE_EMPHASIS_REACH_0;
			}

			channel_eq_done0_1 = lane0_1_status;
			channel_eq_done0_1 &= DPCD_LANE0_SYMBOL_LOCKED |
					   DPCD_LANE0_CHANNEL_EQ_DONE;
			interlane_aligned = lane_align_status_updated;
			interlane_aligned &= DPCD_INTERLANE_ALIGN_DONE;

			if (channel_eq_done0_1 ==
			    (DPCD_LANE0_SYMBOL_LOCKED |
			     DPCD_LANE0_CHANNEL_EQ_DONE) &&
			    (interlane_aligned == DPCD_INTERLANE_ALIGN_DONE)) {
				/* EQ succeed */
				/* traing pattern Set to Normal */
				s5p_dp_training_pattern_dis(dp);

				dev_info(dp->dev, "Link Training success!\n");

				s5p_dp_get_link_bandwidth(dp, &data);
				dp->link_train.link_rate = data;
				dev_dbg(dp->dev, "final bandwidth = %.2x\n",
					dp->link_train.link_rate);

				s5p_dp_get_lane_count(dp, &data);
				dp->link_train.lane_count = data;
				dev_dbg(dp->dev, "final lane count = %.2x\n",
					dp->link_train.lane_count);

				/* set enhanced mode if available */
				s5p_dp_set_enhanced_mode(dp);

				dp->link_train.lt_state = FINISHED;
			} else {
				/* not all locked */
				dp->link_train.eq_loop++;

				if (dp->link_train.eq_loop  > MAX_EQ_LOOP) {
					/* try reduced bit rate */
					if (dp->link_train.link_rate ==
					    LINK_RATE_2_70GBPS) {
						/* set to reduced bit rate */
						dp->link_train.link_rate =
							LINK_RATE_1_62GBPS;
						dev_err(dp->dev, "set to bandwidth %.2x\n",
							dp->link_train.
							link_rate);
						dp->link_train.lt_state =
							START;
					} else {
						/* bit-rate already reduced */
						s5p_dp_training_pattern_dis(dp);

						/*
						 * set enhanced mode
						 * if available
						 */
						s5p_dp_set_enhanced_mode(dp);

						dp->link_train.lt_state =
							FAILED;
					}
				} else {
					/* adjust pre-emphasis level */
					s5p_dp_set_lane0_link_training(dp,
						training_lane0_set);

					/*
					 * Write TRAINING_LANE0_SET
					 * and TRAINING_LANE1_SET
					 */
					buf[0] = training_lane0_set;

					s5p_dp_write_bytes_to_dpcd(dp,
						DPCD_ADDR_TRAINING_LANE0_SET,
						1, buf);
				}
			}
		} else if (dp->link_train.link_rate == LINK_RATE_2_70GBPS) {
			/* try reduced bit rate and return to CR training */
			dp->link_train.link_rate = LINK_RATE_1_62GBPS;
			dev_err(dp->dev, "set to bandwidth %.2x\n",
				dp->link_train.link_rate);
			dp->link_train.lt_state = START;
		} else {
			/* bit-rate already reduced */
			/* traing pattern Set to Normal, and enable scramble */
			s5p_dp_training_pattern_dis(dp);

			/* set enhanced mode if available */
			s5p_dp_set_enhanced_mode(dp);

			dp->link_train.lt_state = FAILED;
		}
	}

	return 0;
}
#endif

static void s5p_dp_get_max_rx_bandwidth(struct s5p_dp_device *dp,
			u8 *bandwidth)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 */
	s5p_dp_read_byte_from_dpcd(dp, DPCD_ADDR_MAX_LINK_RATE, &data);
	*bandwidth = data;
}

static void s5p_dp_get_max_rx_lane_count(struct s5p_dp_device *dp,
			u8 *lane_count)
{
	u8 data;

	/*
	 * For DP rev.1.1, Maximum number of Main Link lanes
	 * 0x01 = 1 lane, 0x02 = 2 lanes, 0x04 = 4 lanes
	 */
	s5p_dp_read_byte_from_dpcd(dp, DPCD_ADDR_MAX_LANE_COUNT, &data);
	*lane_count = DPCD_MAX_LANE_COUNT(data);
}

#ifdef HW_LINK_TRAINING
static int s5p_dp_hw_link_training(struct s5p_dp_device *dp,
				u32 max_lane,
				u32 max_rate)
{
	u32 data;

	s5p_dp_stop_video(dp);

	if (s5p_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		dev_err(dp->dev, "PLL is not locked yet.\n");
		return -EINVAL;
	}

	s5p_dp_reset_macro(dp);

	/* set minimum PRE-EMPHASIS to TX */
	s5p_dp_set_lane0_link_training(dp, 0);
	s5p_dp_set_lane1_link_training(dp, 0);
	s5p_dp_set_lane2_link_training(dp, 0);
	s5p_dp_set_lane3_link_training(dp, 0);

	/* All DP analog module power up */
	s5p_dp_set_analog_power_down(dp, POWER_ALL, 0);

	/* Initialize by reading RX's DPCD */
	s5p_dp_get_max_rx_bandwidth(dp, &dp->link_train.link_rate);
	s5p_dp_get_max_rx_lane_count(dp, &dp->link_train.lane_count);

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

	/* Set link rate and count as you want to establish*/
	s5p_dp_set_lane_count(dp, dp->video_info->lane_count);
	s5p_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	/* Set sink to D0 (Sink Not Ready) mode. */
	s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_SINK_POWER_STATE,
				DPCD_SET_POWER_STATE_D0);

	/* Start HW link training */
	s5p_dp_start_hw_link_training(dp);

	/* Wait unitl HW link training done */
	s5p_dp_wait_hw_link_training_done(dp);

	/* Get hardware link training status */
	data = s5p_dp_get_hw_link_training_status(dp);

	if (data != 0)
		dev_err(dp->dev, " H/W link training failure: 0x%x\n", data);

	s5p_dp_get_link_bandwidth(dp, &data);
	dp->link_train.link_rate = data;
	dev_dbg(dp->dev, "final bandwidth = %.2x\n",
		dp->link_train.link_rate);

	s5p_dp_get_lane_count(dp, &data);
	dp->link_train.lane_count = data;
	dev_dbg(dp->dev, "final lane count = %.2x\n",
		dp->link_train.lane_count);

	return 0;
}

static int s5p_dp_set_link_train(struct s5p_dp_device *dp,
				u32 count,
				u32 bwtype)
{
	int i;
	int retval;

	for (i = 0; i < DP_TIMEOUT_LOOP_COUNT; i++) {
		retval = s5p_dp_hw_link_training(dp, count, bwtype);
		if (retval == 0)
			break;

		udelay(100);	/* need for H/W link retraining */
	}
	return retval;
}
#else
static void s5p_dp_init_training(struct s5p_dp_device *dp,
			enum link_lane_count_type max_lane,
			enum link_rate_type max_rate)
{
	/*
	 * MACRO_RST must be applied after the PLL_LOCK to avoid
	 * the DP inter pair skew issue for at least 10 us
	 */
	s5p_dp_reset_macro(dp);

	/* Initialize by reading RX's DPCD */
	s5p_dp_get_max_rx_bandwidth(dp, &dp->link_train.link_rate);
	s5p_dp_get_max_rx_lane_count(dp, &dp->link_train.lane_count);

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
	s5p_dp_set_analog_power_down(dp, POWER_ALL, 0);
}

static int s5p_dp_sw_link_training(struct s5p_dp_device *dp)
{
	int retval = 0;
	int training_finished;

	/* Turn off unnecessary lane */
	if (dp->link_train.lane_count == 1)
		s5p_dp_set_analog_power_down(dp, CH1_BLOCK, 1);

	training_finished = 0;

	dp->link_train.lt_state = START;

	/* Process here */
	while (!training_finished) {
		switch (dp->link_train.lt_state) {
		case START:
			s5p_dp_link_start(dp);
			break;
		case CLOCK_RECOVERY:
			s5p_dp_process_clock_recovery(dp);
			break;
		case EQUALIZER_TRAINING:
			s5p_dp_process_equalizer_training(dp);
			break;
		case FINISHED:
			training_finished = 1;
			break;
		case FAILED:
			return -EREMOTEIO;
		}
	}

	return retval;
}

static int s5p_dp_set_link_train(struct s5p_dp_device *dp,
				u32 count,
				u32 bwtype)
{
	int i;
	int retval;

	for (i = 0; i < DP_TIMEOUT_LOOP_COUNT; i++) {
		s5p_dp_init_training(dp, count, bwtype);
		retval = s5p_dp_sw_link_training(dp);
		if (retval == 0)
			break;

		udelay(100);	/* need for H/W link retraining */
	}

	return retval;
}
#endif

static int s5p_dp_get_bpc_component(enum color_depth color_depth)
{
	int bpc;

	switch (color_depth) {
	case COLOR_6:
		bpc = 6;
		break;
	case COLOR_8:
		bpc = 8;
		break;
	case COLOR_10:
		bpc = 10;
		break;
	case COLOR_12:
		bpc = 12;
		break;
	default:
		bpc = 0;
	}

	return bpc;
}

static int s5p_dp_get_bpp(enum color_space color_space, int bpc)
{
	int bpp;

	switch (color_space) {
	case COLOR_RGB:
		bpp = bpc * 3;
		break;
	case COLOR_YCBCR422:
		bpp = bpc * 2;
		break;
	case COLOR_YCBCR444:
		bpp = bpc * 3;
		break;
	default:
		bpp = 0;
	}

	return bpp;
}


static int s5p_dp_config_video(struct s5p_dp_device *dp,
			struct video_info *video_info)
{
	u32 stream_clk;
	int retval = 0;
	int bpc;
	int bpp;
	int max_available_vsync_rate;
	int v_total_bpp_per_frame;
	int timeout_loop = 0;
	int v_start;
	int h_start;
	int done_count = 0;

	/* Our device specification. */
	if (!((video_info->v_total >= 1) && (video_info->v_total <= 4095)))
		dev_err(dp->dev, "video_info->v_total %d is not supported.\n",
			video_info->v_total);

	if (!((video_info->v_active >= 1) && (video_info->v_active <= 4095)))
		dev_err(dp->dev, "video_info->v_active %d is not supported.\n",
			video_info->v_active);

	if (!((video_info->v_front_porch >= 1) &&
	   (video_info->v_front_porch <= 255)))
		dev_err(dp->dev, "video_info->v_front_porch %d is not supported.\n",
			video_info->v_front_porch);

	if (!((video_info->v_back_porch >= 1) &&
	   (video_info->v_back_porch <= 254)))
		dev_err(dp->dev, "video_info->v_back_porch %d is not supported.\n",
			video_info->v_back_porch);

	if (!((video_info->v_sync_width >= 1) &&
	   (video_info->v_sync_width <= 254)))
		dev_err(dp->dev, "video_info->v_sync_width %d is not supported.\n",
			video_info->v_sync_width);

	v_start = video_info->v_back_porch + video_info->v_sync_width;

	if (!((v_start >= 2) && (v_start <= 255)))
		dev_err(dp->dev, "v_start %d is not supported.\r\n", v_start);

	if (!((video_info->h_total >= 1) && (video_info->h_total <= 16383)))
		dev_err(dp->dev, "video_info->h_total %d is not supported.\n",
			video_info->h_total);

	if (!((video_info->h_active >= 1) && (video_info->h_active <= 16383)))
		dev_err(dp->dev, "video_info->h_active %d is not supported.\n",
			video_info->h_active);

	if (!((video_info->h_front_porch >= 1) &&
	   (video_info->h_front_porch <= 4095)))
		dev_err(dp->dev, "video_info->h_front_porch %d is not supported.\n",
			video_info->h_front_porch);

	if (!((video_info->h_back_porch >= 1) &&
	   (video_info->h_back_porch <= 4094)))
		dev_err(dp->dev, "video_info->h_back_porch %d is not supported.\n",
			video_info->h_back_porch);

	if (!((video_info->h_sync_width >= 1) &&
	   (video_info->h_sync_width <= 4094)))
		dev_err(dp->dev, "video_info->h_sync_width %d is not supported.\n",
			video_info->h_sync_width);

	h_start = video_info->h_sync_width + video_info->h_back_porch;
	if (!((h_start >= 2) && (h_start <= 4095)))
		dev_err(dp->dev, "h_start %d is not supported\n", h_start);

	if (video_info->master_mode) {
		s5p_dp_config_video_master_mode(dp, video_info);

		if (video_info->refresh_denominator == REFRESH_DENOMINATOR_1)
			/*
			 * F_STRM_CLK
			 * = VideoVTotalLength * VideoHTotalLength *
			 *   VideoFrameRate
			 * = 1650 * 750 * 60 = 74,250,000
			 */
			stream_clk = video_info->v_total *
				video_info->h_total *
				video_info->v_sync_rate;
		else
			/*
			 * F_STRM_CLK
			 * = VideoVTotalLength * VideoHTotalLength *
			 *   VideoFrameRate / 1.001
			 * = 74,125,824
			 */
			stream_clk = (u32)(video_info->v_total *
				video_info->h_total *
				video_info->v_sync_rate / 1);

		if (video_info->interlaced)
			stream_clk = video_info->v_total *
				video_info->h_total *
				video_info->v_sync_rate >> 1;

		bpc = s5p_dp_get_bpc_component(video_info->color_depth);
		bpp = s5p_dp_get_bpp(video_info->color_space, bpc);

		v_total_bpp_per_frame = (video_info->v_total *
				video_info->h_total * bpp) >>
				video_info->interlaced;

		if (video_info->link_rate == LINK_RATE_2_70GBPS) {
			if (stream_clk > 135000000)
				dev_err(dp->dev, "Too high stream clock %d\n",
					stream_clk);
			max_available_vsync_rate = (int)(270000000 *
					dp->link_train.lane_count /
					v_total_bpp_per_frame);
			max_available_vsync_rate *= 8;
		} else {
			if (stream_clk > 81000000)
				dev_err(dp->dev, "Too high stream clock %d\n",
					stream_clk);
			max_available_vsync_rate = (int)(162000000 *
					dp->link_train.lane_count /
					v_total_bpp_per_frame);
			max_available_vsync_rate *= 8;
		}

		if (video_info->v_sync_rate > max_available_vsync_rate) {
			dev_err(dp->dev, "VSync for master interface is too high\n");
			dev_err(dp->dev, "VSync Rate %d\n",
				video_info->v_sync_rate);
		}

		s5p_dp_set_video_master_data_mn(dp, stream_clk,
					video_info->link_rate);
	} else {
		/* debug slave */
		s5p_dp_config_video_slave_mode(dp, video_info);
	}

	s5p_dp_set_video_color_format(dp, video_info->color_depth,
			video_info->color_space,
			video_info->dynamic_range,
			video_info->ycbcr_coeff);

	if (video_info->bist_mode) {
		if (s5p_dp_config_video_bist(dp, video_info) != 0)
			return -EINVAL;
	}

	if (s5p_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		dev_err(dp->dev, "PLL is not locked yet.\n");
		return -EINVAL;
	}

	if (video_info->master_mode == 0) {
		for (;;) {
			timeout_loop++;
			if (s5p_dp_is_slave_video_stream_clock_on(dp) == 0)
				break;
			if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
				dev_err(dp->dev, "Timeout of video streamclk ok\n");
				return -ETIMEDOUT;
			}

			mdelay(100);
		}
	}

	/* Set to use the register calculated M/N video */
	s5p_dp_set_video_cr_mn(dp, CALCULATED_M, 0, 0);
	/* s5p_dp_set_video_cr_mn(dp, REGISTER_M, 4505, 27000); */

	/* For video bist, Video timing must be generated by register */
	s5p_dp_set_video_timing_mode(dp, VIDEO_TIMING_FROM_CAPTURE);

	/* Enable video bist */
	if (video_info->test_pattern != COLOR_RAMP &&
	    video_info->test_pattern != BALCK_WHITE_V_LINES &&
	    video_info->test_pattern != COLOR_SQUARE)
		s5p_dp_enable_video_bist(dp, video_info->bist_mode);
	else
		s5p_dp_enable_video_bist(dp, 0);

	/* Disable video mute */
	s5p_dp_enable_video_mute(dp, 0);

	/* Configure video Master or Slave mode */
	s5p_dp_enable_video_master(dp, video_info->master_mode);

	/* Enable video */
	s5p_dp_start_video(dp);

	if (!video_info->master_mode) {
		timeout_loop = 0;

		for (;;) {
			timeout_loop++;
			if (s5p_dp_is_video_stream_on(dp) == 0) {
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

			mdelay(100);
		}
	}

	if (retval != 0)
		dev_err(dp->dev, "Video stream is not detected!\n");

	return retval;
}

static void s5p_dp_enable_scramble(struct s5p_dp_device *dp, bool enable)
{
	u8 data;

	if (enable) {
		s5p_dp_enable_scrambling(dp);

		s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			&data);
		s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			(u8)(data & ~DPCD_SCRAMBLING_DISABLED));
	} else {
		s5p_dp_disable_scrambling(dp);

		s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			&data);
		s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			(u8)(data | DPCD_SCRAMBLING_DISABLED));
	}
}

static void s5p_dp_show_video_format(struct s5p_dp_device *dp,
			struct video_info *video_info)
{
	dev_dbg(dp->dev, "Horizontal Total = %d\n", video_info->h_total);
	dev_dbg(dp->dev, "Horizontal Active, xres = %d\n",
		video_info->h_active);
	dev_dbg(dp->dev, "Horizontal SyncWidth, hsync_len = %d\n",
		video_info->h_sync_width);
	dev_dbg(dp->dev, "Horizontal BackPorch, left_margin = %d\n",
		video_info->h_back_porch);
	dev_dbg(dp->dev, "Horizontal FrontPorch, right_margin = %d\n",
		video_info->h_front_porch);

	dev_dbg(dp->dev, "Vertical Total = %d\n", video_info->v_total);
	dev_dbg(dp->dev, "Vertical Active, yres = %d\n", video_info->v_active);
	dev_dbg(dp->dev, "Vertical SyncWidth, vsync_len = %d\n",
		video_info->v_sync_width);
	dev_dbg(dp->dev, "Vertical BackPorch, upper_margin = %d\n",
		video_info->v_back_porch);
	dev_dbg(dp->dev, "Vertical FrontPorch, lower_margin = %d\n",
		video_info->v_front_porch);

	dev_dbg(dp->dev, "Horizontal Sync Polartity: %s is active\n",
		(video_info->h_sync_polarity == 0) ? "High" : "Low");
	dev_dbg(dp->dev, "Vertical Sync Polarity: %s is active\n",
		(video_info->v_sync_polarity == 0) ? "High" : "Low");
	dev_dbg(dp->dev, "%s Scan\n", (video_info->interlaced == 0) ?
		"Progressive" : "Interlaced");
	dev_dbg(dp->dev, "Video VSync Rate = %d Hz\n",
		video_info->v_sync_rate);

	dev_dbg(dp->dev, "color_space = %s\n", (video_info->color_space == 0) ?
		"RGB" : (video_info->color_space == 1) ?
		"YCbCr422" : (video_info->color_space == 2) ?
		"YCbCr444" : "Unknown");
	dev_dbg(dp->dev, "dynamic_range = %s\n",
		(video_info->dynamic_range == 0) ? "VESA" : "CEA");
	dev_dbg(dp->dev, "ycbcr_coeff = %s\n", (video_info->ycbcr_coeff == 0) ?
		"ITU601" : "ITU709");
	dev_dbg(dp->dev, "color_depth = %s\n", (video_info->color_depth == 0) ?
		"6 bit" : (video_info->color_depth == 1) ?
		"8 bit" : (video_info->color_depth == 2) ?
		"10 bit" : (video_info->color_depth == 3) ?
		"12 bit" : "Unknown");
	dev_dbg(dp->dev, "Current Field = %s\n",
		(video_info->even_field == 0) ? "Odd Field" : "Even Field");
	dev_dbg(dp->dev, "%s\n", (video_info->sync_clock == 0) ?
		"Asynchronous Clock Mode" : "Synchronous Clock Mode");
	dev_dbg(dp->dev, "link_rate = %s\n", (video_info->link_rate == 6) ?
		"1.62Ghz" : (video_info->link_rate == 0xa) ?
		"2.7Ghz" : "Unknown");
	dev_dbg(dp->dev, "lane_count = %.2d\n", video_info->lane_count);
}

static irqreturn_t s5p_dp_irq_handler(int irq, void *arg)
{
	struct s5p_dp_device *dp = arg;

	dev_err(dp->dev, "s5p_dp_irq_handler\n");
	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void s5p_dp_early_suspend(struct early_suspend *handler)
{
	struct platform_device *pdev;
	struct s5p_dp_platdata *pdata;
	struct s5p_dp_device *dp;

	dp = container_of(handler, struct s5p_dp_device, early_suspend);
	pdev = to_platform_device(dp->dev);
	pdata = pdev->dev.platform_data;

	if (pdata->backlight_off)
		pdata->backlight_off();

	if (pdata && pdata->phy_exit)
		pdata->phy_exit();

	clk_disable(dp->clock);
	pm_runtime_put_sync(dp->dev);

	return;
}

static void s5p_dp_late_resume(struct early_suspend *handler)
{
	struct platform_device *pdev;
	struct s5p_dp_platdata *pdata;
	struct s5p_dp_device *dp;

	dp = container_of(handler, struct s5p_dp_device, early_suspend);
	pdev = to_platform_device(dp->dev);
	pdata = pdev->dev.platform_data;

	if (pdata && pdata->phy_init)
		pdata->phy_init();

	pm_runtime_get_sync(dp->dev);
	clk_enable(dp->clock);

	s5p_dp_init_dp(dp);

	if (!soc_is_exynos5250()) {
		s5p_dp_detect_hpd(dp);
		s5p_dp_handle_edid(dp);
	}

	s5p_dp_set_link_train(dp, dp->video_info->lane_count,
				dp->video_info->link_rate);

	if (soc_is_exynos5250()) {
		s5p_dp_enable_scramble(dp, 1);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 1);
		s5p_dp_enable_enhanced_mode(dp, 1);
	} else {
		s5p_dp_enable_scramble(dp, 0);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 0);
		s5p_dp_enable_enhanced_mode(dp, 0);
	}

	s5p_dp_set_lane_count(dp, dp->video_info->lane_count);
	s5p_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	s5p_dp_init_video(dp);
	s5p_dp_config_video(dp, dp->video_info);

	if (pdata->backlight_on)
		pdata->backlight_on();

	return;
}
#endif

static int __devinit s5p_dp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct s5p_dp_device *dp;
	struct s5p_dp_platdata *pdata;

	int ret = 0;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	dp = kzalloc(sizeof(struct s5p_dp_device), GFP_KERNEL);
	if (!dp) {
		dev_err(&pdev->dev, "no memory for device data\n");
		return -ENOMEM;
	}

	dp->dev = &pdev->dev;

	dp->clock = clk_get(&pdev->dev, "dp");
	if (IS_ERR(dp->clock)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(dp->clock);
		goto err_dp;
	}

	clk_enable(dp->clock);

	pm_runtime_enable(dp->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get registers\n");
		ret = -EINVAL;
		goto err_clock;
	}

	res = request_mem_region(res->start, resource_size(res),
				dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev, "failed to request registers region\n");
		ret = -EINVAL;
		goto err_clock;
	}

	dp->res = res;

	dp->reg_base = ioremap(res->start, resource_size(res));
	if (!dp->reg_base) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		ret = -ENOMEM;
		goto err_req_region;
	}

	pm_runtime_get_sync(dp->dev);

	dp->irq = platform_get_irq(pdev, 0);
	if (!dp->irq) {
		dev_err(&pdev->dev, "failed to get irq\n");
		ret = -ENODEV;
		goto err_ioremap;
	}

	ret = request_irq(dp->irq, s5p_dp_irq_handler, 0,
			"s5p-dp", dp);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		goto err_ioremap;
	}

	dp->video_info = pdata->video_info;
	if (pdata->phy_init)
		pdata->phy_init();

	s5p_dp_show_video_format(dp, dp->video_info);

	s5p_dp_init_dp(dp);

	if (!soc_is_exynos5250()) {
		ret = s5p_dp_detect_hpd(dp);
		if (ret) {
			dev_err(&pdev->dev, "unable to detect hpd\n");
			goto err_irq;
		}

		s5p_dp_handle_edid(dp);
	}

	ret = s5p_dp_set_link_train(dp, dp->video_info->lane_count,
				dp->video_info->link_rate);
	if (ret) {
		dev_err(&pdev->dev, "unable to do link train\n");
		goto err_irq;
	}

	if (soc_is_exynos5250()) {
		s5p_dp_enable_scramble(dp, 1);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 1);
		s5p_dp_enable_enhanced_mode(dp, 1);
	} else {
		s5p_dp_enable_scramble(dp, 0);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 0);
		s5p_dp_enable_enhanced_mode(dp, 0);
	}

	s5p_dp_set_lane_count(dp, dp->video_info->lane_count);
	s5p_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	s5p_dp_init_video(dp);
	ret = s5p_dp_config_video(dp, dp->video_info);
	if (ret) {
		dev_err(&pdev->dev, "unable to config video\n");
		goto err_irq;
	}

	if (pdata->backlight_on)
		pdata->backlight_on();

	platform_set_drvdata(pdev, dp);

#ifdef CONFIG_HAS_EARLYSUSPEND
	dp->early_suspend.suspend = s5p_dp_early_suspend;
	dp->early_suspend.resume = s5p_dp_late_resume;
	dp->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&dp->early_suspend);
#endif
	return 0;

err_irq:
	free_irq(dp->irq, dp);
err_ioremap:
	iounmap(dp->reg_base);
err_req_region:
	release_mem_region(res->start, resource_size(res));
err_clock:
	clk_put(dp->clock);
err_dp:
	kfree(dp);

	return ret;
}

static int __devexit s5p_dp_remove(struct platform_device *pdev)
{
	struct s5p_dp_platdata *pdata = pdev->dev.platform_data;
	struct s5p_dp_device *dp = platform_get_drvdata(pdev);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&dp->early_suspend);
#endif

	if (pdata->backlight_off)
		pdata->backlight_off();

	if (pdata && pdata->phy_exit)
		pdata->phy_exit();

	free_irq(dp->irq, dp);
	iounmap(dp->reg_base);

	clk_disable(dp->clock);
	clk_put(dp->clock);

	release_mem_region(dp->res->start, resource_size(dp->res));

	pm_runtime_put_sync(dp->dev);
	pm_runtime_disable(dp->dev);

	kfree(dp);

	return 0;
}

#ifdef CONFIG_PM
#ifndef CONFIG_HAS_EARLYSUSPEND
static int s5p_dp_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_dp_platdata *pdata = pdev->dev.platform_data;
	struct s5p_dp_device *dp = platform_get_drvdata(pdev);

	if (pdata->backlight_off)
		pdata->backlight_off();

	if (pdata && pdata->phy_exit)
		pdata->phy_exit();

	clk_disable(dp->clock);
	pm_runtime_put_sync(dp->dev);

	return 0;
}

static int s5p_dp_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_dp_platdata *pdata = pdev->dev.platform_data;
	struct s5p_dp_device *dp = platform_get_drvdata(pdev);

	if (pdata && pdata->phy_init)
		pdata->phy_init();

	pm_runtime_get_sync(dp->dev);
	clk_enable(dp->clock);

	s5p_dp_init_dp(dp);

	if (!soc_is_exynos5250()) {
		s5p_dp_detect_hpd(dp);
		s5p_dp_handle_edid(dp);
	}

	s5p_dp_set_link_train(dp, dp->video_info->lane_count,
				dp->video_info->link_rate);

	if (soc_is_exynos5250()) {
		s5p_dp_enable_scramble(dp, 1);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 1);
		s5p_dp_enable_enhanced_mode(dp, 1);
	} else {
		s5p_dp_enable_scramble(dp, 0);
		s5p_dp_enable_rx_to_enhanced_mode(dp, 0);
		s5p_dp_enable_enhanced_mode(dp, 0);
	}

	s5p_dp_set_lane_count(dp, dp->video_info->lane_count);
	s5p_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	s5p_dp_init_video(dp);
	s5p_dp_config_video(dp, dp->video_info);

	if (pdata->backlight_on)
		pdata->backlight_on();

	return 0;
}
#endif
static int s5p_dp_runtime_suspend(struct device *dev)
{
	return 0;
}

static int s5p_dp_runtime_resume(struct device *dev)
{
	return 0;
}
#else
#define s5p_dp_suspend NULL
#define s5p_dp_resume NULL
#define s5p_dp_runtime_suspend NULL
#define s5p_dp_runtime_resume NULL
#endif

static const struct dev_pm_ops s5p_dp_pm_ops = {
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend		= s5p_dp_suspend,
	.resume			= s5p_dp_resume,
#endif
	.runtime_suspend	= s5p_dp_runtime_suspend,
	.runtime_resume		= s5p_dp_runtime_resume,
};

static struct platform_driver s5p_dp_driver = {
	.probe		= s5p_dp_probe,
	.remove		= __devexit_p(s5p_dp_remove),
	.driver		= {
		.name	= "s5p-dp",
		.owner	= THIS_MODULE,
		.pm	= &s5p_dp_pm_ops,
	},
};

static int __init s5p_dp_init(void)
{
	return platform_driver_probe(&s5p_dp_driver, s5p_dp_probe);
}

static void __exit s5p_dp_exit(void)
{
	platform_driver_unregister(&s5p_dp_driver);
}

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
late_initcall(s5p_dp_init);
#else
module_init(s5p_dp_init);
#endif
module_exit(s5p_dp_exit);

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Samsung S5P DP interface Driver");
MODULE_LICENSE("GPL");
