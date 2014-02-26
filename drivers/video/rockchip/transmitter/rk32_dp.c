/*
 * DisplayPort driver for rk32xx
 *
 * Copyright (C) ROCKCHIP, Inc.
 *Author:yxj<yxj@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include "rk32_dp.h"
#include "../../../arch/arm/mach-rockchip/iomap.h"
#include "../../../arch/arm/mach-rockchip/grf.h"

#if defined(CONFIG_OF)
#include <linux/of.h>
#endif

/*#define EDP_BIST_MODE*/

static int rk32_edp_init_edp(struct rk32_edp *edp)
{
	rk32_edp_reset(edp);
	rk32_edp_init_analog_param(edp);
	rk32_edp_init_interrupt(edp);

	rk32_edp_enable_sw_function(edp);

	rk32_edp_init_analog_func(edp);

	rk32_edp_init_hpd(edp);
	rk32_edp_init_aux(edp);

	return 0;
}

static int rk32_edp_detect_hpd(struct rk32_edp *edp)
{
	int timeout_loop = 0;

	rk32_edp_init_hpd(edp);

	udelay(200);

	while (rk32_edp_get_plug_in_status(edp) != 0) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_CNT < timeout_loop) {
			dev_err(edp->dev, "failed to get hpd plug status\n");
			return -ETIMEDOUT;
		}
		udelay(10);
	}

	return 0;
}

static int rk32_edp_read_edid(struct rk32_edp *edp)
{
	unsigned char edid[EDID_LENGTH * 2];
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
	retval = rk32_edp_read_byte_from_i2c(edp, EDID_ADDR, EDID_EXTENSION_FLAG,
						&extend_block);
	if (retval < 0) {
		dev_err(edp->dev, "EDID extension flag failed!\n");
		return -EIO;
	}

	if (extend_block > 0) {
		dev_dbg(edp->dev, "EDID data includes a single extension!\n");

		/* Read EDID data */
		retval = rk32_edp_read_bytes_from_i2c(edp, EDID_ADDR, EDID_HEADER,
						EDID_LENGTH, &edid[EDID_HEADER]);
		if (retval != 0) {
			dev_err(edp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = edp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_warn(edp->dev, "EDID bad checksum!\n");
			return 0;
		}

		/* Read additional EDID data */
		retval = rk32_edp_read_bytes_from_i2c(edp, EDID_ADDR, EDID_LENGTH,
						EDID_LENGTH, &edid[EDID_LENGTH]);
		if (retval != 0) {
			dev_err(edp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = edp_calc_edid_check_sum(&edid[EDID_LENGTH]);
		if (sum != 0) {
			dev_warn(edp->dev, "EDID bad checksum!\n");
			return 0;
		}

		retval = rk32_edp_read_byte_from_dpcd(edp, DPCD_TEST_REQUEST,
					&test_vector);
		if (retval < 0) {
			dev_err(edp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = rk32_edp_write_byte_to_dpcd(edp,
					DPCD_TEST_EDID_CHECKSUM,
					edid[EDID_LENGTH + EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = rk32_edp_write_byte_to_dpcd(edp,
					DPCD_TEST_RESPONSE,
					DPCD_TEST_EDID_CHECKSUM_WRITE);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
		}
	} else {
		dev_info(edp->dev, "EDID data does not include any extensions.\n");

		/* Read EDID data */
		retval = rk32_edp_read_bytes_from_i2c(edp, EDID_ADDR, EDID_HEADER,
						EDID_LENGTH, &edid[EDID_HEADER]);
		if (retval != 0) {
			dev_err(edp->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = edp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_warn(edp->dev, "EDID bad checksum!\n");
			return 0;
		}

		retval = rk32_edp_read_byte_from_dpcd(edp,DPCD_TEST_REQUEST,
						&test_vector);
		if (retval < 0) {
			dev_err(edp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = rk32_edp_write_byte_to_dpcd(edp,
					DPCD_TEST_EDID_CHECKSUM,
					edid[EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = rk32_edp_write_byte_to_dpcd(edp,
					DPCD_TEST_RESPONSE,
					DPCD_TEST_EDID_CHECKSUM_WRITE);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
		}
	}

	dev_err(edp->dev, "EDID Read success!\n");
	return 0;
}

static int rk32_edp_handle_edid(struct rk32_edp *edp)
{
	u8 buf[12];
	int i;
	int retval;

	/* Read DPCD DPCD_ADDR_DPCD_REV~RECEIVE_PORT1_CAP_1 */
	retval = rk32_edp_read_bytes_from_dpcd(edp, DPCD_REV, 12, buf);
	if (retval < 0)
		return retval;

	/* Read EDID */
	for (i = 0; i < 3; i++) {
		retval = rk32_edp_read_edid(edp);
		if (retval == 0)
			break;
	}

	return retval;
}

static int rk32_edp_enable_rx_to_enhanced_mode(struct rk32_edp *edp,
						bool enable)
{
	u8 data;
	int retval;

	retval = rk32_edp_read_byte_from_dpcd(edp,
			DPCD_LANE_CNT_SET, &data);
	if (retval < 0)
		return retval;

	if (enable) {
		retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_LANE_CNT_SET,
				DPCD_ENHANCED_FRAME_EN |
				DPCD_LANE_COUNT_SET(data));
	} else {
		/*retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_ADDR_CONFIGURATION_SET, 0);*/

		retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_LANE_CNT_SET,
				DPCD_LANE_COUNT_SET(data));
	}

	return retval;
}

void rk32_edp_rx_control(struct rk32_edp *edp, bool enable)
{
	/*rk32_edp_write_byte_to_dpcd(edp, DPCD_ADDR_USER_DEFINED1,0);
	rk32_edp_write_byte_to_dpcd(edp, DPCD_ADDR_USER_DEFINED2,0x90);

	if (enable) {
		rk32_edp_write_byte_to_dpcd(edp, DPCD_ADDR_USER_DEFINED3,0x84);
		rk32_edp_write_byte_to_dpcd(edp, DPCD_ADDR_USER_DEFINED3,0x00);
	} else {
		rk32_edp_write_byte_to_dpcd(edp, DPCD_ADDR_USER_DEFINED3,0x80);
	}*/
}

static int rk32_edp_is_enhanced_mode_available(struct rk32_edp *edp)
{
	u8 data;
	int retval;

	retval = rk32_edp_read_byte_from_dpcd(edp,
			DPCD_MAX_LANE_CNT, &data);
	if (retval < 0)
		return retval;

	return DPCD_ENHANCED_FRAME_CAP(data);
}

static void rk32_edp_disable_rx_zmux(struct rk32_edp *edp)
{
	/*rk32_edp_write_byte_to_dpcd(edp,
			DPCD_ADDR_USER_DEFINED1, 0);
	rk32_edp_write_byte_to_dpcd(edp,
			DPCD_ADDR_USER_DEFINED2, 0x83);
	rk32_edp_write_byte_to_dpcd(edp,
			DPCD_ADDR_USER_DEFINED3, 0x27);*/
}

static int rk32_edp_set_enhanced_mode(struct rk32_edp *edp)
{
	u8 data;
	int retval;

	retval = rk32_edp_is_enhanced_mode_available(edp);
	if (retval < 0)
		return retval;

	data = (u8)retval;
	retval = rk32_edp_enable_rx_to_enhanced_mode(edp, data);
	if (retval < 0)
		return retval;

	rk32_edp_enable_enhanced_mode(edp, data);

	return 0;
}

static int rk32_edp_training_pattern_dis(struct rk32_edp *edp)
{
	int retval;

	rk32_edp_set_training_pattern(edp, DP_NONE);

	retval = rk32_edp_write_byte_to_dpcd(edp,
			DPCD_TRAINING_PATTERN_SET,
			DPCD_TRAINING_PATTERN_DISABLED);
	if (retval < 0)
		return retval;

	return 0;
}

static void rk32_edp_set_lane_lane_pre_emphasis(struct rk32_edp *edp,
					int pre_emphasis, int lane)
{
	switch (lane) {
	case 0:
		rk32_edp_set_lane0_pre_emphasis(edp, pre_emphasis);
		break;
	case 1:
		rk32_edp_set_lane1_pre_emphasis(edp, pre_emphasis);
		break;

	case 2:
		rk32_edp_set_lane2_pre_emphasis(edp, pre_emphasis);
		break;

	case 3:
		rk32_edp_set_lane3_pre_emphasis(edp, pre_emphasis);
		break;
	}
}

static int rk32_edp_link_start(struct rk32_edp *edp)
{
	u8 buf[4];
	int lane;
	int lane_count;
	int retval;

	lane_count = edp->link_train.lane_count;

	edp->link_train.lt_state = LT_CLK_RECOVERY;
	edp->link_train.eq_loop = 0;

	for (lane = 0; lane < lane_count; lane++)
		edp->link_train.cr_loop[lane] = 0;

	/* Set sink to D0 (Sink Not Ready) mode. */
	retval = rk32_edp_write_byte_to_dpcd(edp, DPCD_SINK_POWER_STATE,
				DPCD_SET_POWER_STATE_D0);
	if (retval < 0) {
		dev_err(edp->dev, "failed to set sink device to D0!\n");
		return retval;
	}

	/* Set link rate and count as you want to establish*/
	rk32_edp_set_link_bandwidth(edp, edp->link_train.link_rate);
	rk32_edp_set_lane_count(edp, edp->link_train.lane_count);

	/* Setup RX configuration */
	buf[0] = edp->link_train.link_rate;
	buf[1] = edp->link_train.lane_count;
	retval = rk32_edp_write_bytes_to_dpcd(edp, DPCD_LINK_BW_SET,
					2, buf);
	if (retval < 0) {
		dev_err(edp->dev, "failed to set bandwidth and lane count!\n");
		return retval;
	}

	/* Set TX pre-emphasis to level1 */
	for (lane = 0; lane < lane_count; lane++)
		rk32_edp_set_lane_lane_pre_emphasis(edp,
			PRE_EMPHASIS_LEVEL_1, lane);

	/* Set training pattern 1 */
	rk32_edp_set_training_pattern(edp, TRAINING_PTN1);

	/* Set RX training pattern */
	retval = rk32_edp_write_byte_to_dpcd(edp,
			DPCD_TRAINING_PATTERN_SET,
			DPCD_SCRAMBLING_DISABLED |
			DPCD_TRAINING_PATTERN_1);
	if (retval < 0) {
		dev_err(edp->dev, "failed to set training pattern 1!\n");
		return retval;
	}

	for (lane = 0; lane < lane_count; lane++)
		buf[lane] = DPCD_PRE_EMPHASIS_PATTERN2_LEVEL0 |
			    DPCD_VOLTAGE_SWING_PATTERN1_LEVEL0;
	retval = rk32_edp_write_bytes_to_dpcd(edp,
			DPCD_TRAINING_LANE0_SET,
			lane_count, buf);
	if (retval < 0) {
		dev_err(edp->dev, "failed to set training lane!\n");
		return retval;
	}

	return 0;
}

static unsigned char rk32_edp_get_lane_status(u8 link_status[2], int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = link_status[lane>>1];

	return (link_value >> shift) & 0xf;
}

static int rk32_edp_clock_recovery_ok(u8 link_status[2], int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = rk32_edp_get_lane_status(link_status, lane);
		if ((lane_status & DPCD_LANE_CR_DONE) == 0)
			return -EINVAL;
	}
	return 0;
}

static int rk32_edp_channel_eq_ok(u8 link_align[3], int lane_count)
{
	int lane;
	u8 lane_align;
	u8 lane_status;

	lane_align = link_align[2];
	if ((lane_align & DPCD_INTERLANE_ALIGN_DONE) == 0)
		return -EINVAL;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = rk32_edp_get_lane_status(link_align, lane);
		lane_status &= DPCD_CHANNEL_EQ_BITS;
		if (lane_status != DPCD_CHANNEL_EQ_BITS)
			return -EINVAL;
	}

	return 0;
}

static unsigned char rk32_edp_get_adjust_request_voltage(u8 adjust_request[2],
							int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return (link_value >> shift) & 0x3;
}

static unsigned char rk32_edp_get_adjust_request_pre_emphasis(
					u8 adjust_request[2],
					int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return ((link_value >> shift) & 0xc) >> 2;
}

static void rk32_edp_set_lane_link_training(struct rk32_edp *edp,
					u8 training_lane_set, int lane)
{
	switch (lane) {
	case 0:
		rk32_edp_set_lane0_link_training(edp, training_lane_set);
		break;
	case 1:
		rk32_edp_set_lane1_link_training(edp, training_lane_set);
		break;

	case 2:
		rk32_edp_set_lane2_link_training(edp, training_lane_set);
		break;

	case 3:
		rk32_edp_set_lane3_link_training(edp, training_lane_set);
		break;
	}
}

static unsigned int rk32_edp_get_lane_link_training(
				struct rk32_edp *edp,
				int lane)
{
	u32 reg;

	switch (lane) {
	case 0:
		reg = rk32_edp_get_lane0_link_training(edp);
		break;
	case 1:
		reg = rk32_edp_get_lane1_link_training(edp);
		break;
	case 2:
		reg = rk32_edp_get_lane2_link_training(edp);
		break;
	case 3:
		reg = rk32_edp_get_lane3_link_training(edp);
		break;
	}

	return reg;
}

static void rk32_edp_reduce_link_rate(struct rk32_edp *edp)
{
	rk32_edp_training_pattern_dis(edp);

	edp->link_train.lt_state = FAILED;
}

static int rk32_edp_process_clock_recovery(struct rk32_edp *edp)
{
	u8 link_status[2];
	int lane;
	int lane_count;

	u8 adjust_request[2];
	u8 voltage_swing;
	u8 pre_emphasis;
	u8 training_lane;
	int retval;

	udelay(100);

	lane_count = edp->link_train.lane_count;

	retval = rk32_edp_read_bytes_from_dpcd(edp,
			DPCD_LANE0_1_STATUS,
			2, link_status);
	if (retval < 0) {
		dev_err(edp->dev, "failed to read lane status!\n");
		return retval;
	}

	if (rk32_edp_clock_recovery_ok(link_status, lane_count) == 0) {
		/* set training pattern 2 for EQ */
		rk32_edp_set_training_pattern(edp, TRAINING_PTN2);

		for (lane = 0; lane < lane_count; lane++) {
			retval = rk32_edp_read_bytes_from_dpcd(edp,
					DPCD_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(edp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = rk32_edp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = rk32_edp_get_adjust_request_pre_emphasis(
							adjust_request, lane);
			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			edp->link_train.training_lane[lane] = training_lane;

			rk32_edp_set_lane_link_training(edp,
				edp->link_train.training_lane[lane],
				lane);
		}

		retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_TRAINING_PATTERN_SET,
				DPCD_SCRAMBLING_DISABLED |
				DPCD_TRAINING_PATTERN_2);
		if (retval < 0) {
			dev_err(edp->dev, "failed to set training pattern 2!\n");
			return retval;
		}

		retval = rk32_edp_write_bytes_to_dpcd(edp,
				DPCD_TRAINING_LANE0_SET,
				lane_count,
				edp->link_train.training_lane);
		if (retval < 0) {
			dev_err(edp->dev, "failed to set training lane!\n");
			return retval;
		}

		dev_info(edp->dev, "Link Training Clock Recovery success\n");
		edp->link_train.lt_state = LT_EQ_TRAINING;
	} else {
		for (lane = 0; lane < lane_count; lane++) {
			training_lane = rk32_edp_get_lane_link_training(
							edp, lane);
			retval = rk32_edp_read_bytes_from_dpcd(edp,
					DPCD_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(edp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = rk32_edp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = rk32_edp_get_adjust_request_pre_emphasis(
							adjust_request, lane);

			if (voltage_swing == VOLTAGE_LEVEL_3 ||
			    pre_emphasis == PRE_EMPHASIS_LEVEL_3) {
				dev_err(edp->dev, "voltage or pre emphasis reached max level\n");
				goto reduce_link_rate;
			}

			if ((DPCD_VOLTAGE_SWING_GET(training_lane) ==
					voltage_swing) &&
			   (DPCD_PRE_EMPHASIS_GET(training_lane) ==
					pre_emphasis)) {
				edp->link_train.cr_loop[lane]++;
				if (edp->link_train.cr_loop[lane] == MAX_CR_LOOP) {
					dev_err(edp->dev, "CR Max loop\n");
					goto reduce_link_rate;
				}
			}

			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			edp->link_train.training_lane[lane] = training_lane;

			rk32_edp_set_lane_link_training(edp,
				edp->link_train.training_lane[lane], lane);
		}

		retval = rk32_edp_write_bytes_to_dpcd(edp,
				DPCD_TRAINING_LANE0_SET,
				lane_count,
				edp->link_train.training_lane);
		if (retval < 0) {
			dev_err(edp->dev, "failed to set training lane!\n");
			return retval;
		}
	}

	return 0;

reduce_link_rate:
	rk32_edp_reduce_link_rate(edp);
	return -EIO;
}

static int rk32_edp_process_equalizer_training(struct rk32_edp *edp)
{
	u8 link_status[2];
	u8 link_align[3];
	int lane;
	int lane_count;
	u32 reg;

	u8 adjust_request[2];
	u8 voltage_swing;
	u8 pre_emphasis;
	u8 training_lane;
	int retval;

	udelay(400);

	lane_count = edp->link_train.lane_count;

	retval = rk32_edp_read_bytes_from_dpcd(edp,
			DPCD_LANE0_1_STATUS,
			2, link_status);
	if (retval < 0) {
		dev_err(edp->dev, "failed to read lane status!\n");
		return retval;
	}

	if (rk32_edp_clock_recovery_ok(link_status, lane_count) == 0) {
		link_align[0] = link_status[0];
		link_align[1] = link_status[1];

		retval = rk32_edp_read_byte_from_dpcd(edp,
				DPCD_LANE_ALIGN_STATUS_UPDATED,
				&link_align[2]);
		if (retval < 0) {
			dev_err(edp->dev, "failed to read lane aligne status!\n");
			return retval;
		}

		for (lane = 0; lane < lane_count; lane++) {
			retval = rk32_edp_read_bytes_from_dpcd(edp,
					DPCD_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(edp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = rk32_edp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = rk32_edp_get_adjust_request_pre_emphasis(
							adjust_request, lane);
			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			edp->link_train.training_lane[lane] = training_lane;
		}

		if (rk32_edp_channel_eq_ok(link_align, lane_count) == 0) {
			/* traing pattern Set to Normal */
			retval = rk32_edp_training_pattern_dis(edp);
			if (retval < 0) {
				dev_err(edp->dev, "failed to disable training pattern!\n");
				return retval;
			}

			dev_info(edp->dev, "Link Training success!\n");

			rk32_edp_get_link_bandwidth(edp, &reg);
			edp->link_train.link_rate = reg;
			dev_dbg(edp->dev, "final bandwidth = %.2x\n",
				edp->link_train.link_rate);

			rk32_edp_get_lane_count(edp, &reg);
			edp->link_train.lane_count = reg;
			dev_dbg(edp->dev, "final lane count = %.2x\n",
				edp->link_train.lane_count);

			edp->link_train.lt_state = FINISHED;
		} else {
			/* not all locked */
			edp->link_train.eq_loop++;

			if (edp->link_train.eq_loop > MAX_EQ_LOOP) {
				dev_err(edp->dev, "EQ Max loop\n");
				goto reduce_link_rate;
			}

			for (lane = 0; lane < lane_count; lane++)
				rk32_edp_set_lane_link_training(edp,
					edp->link_train.training_lane[lane],
					lane);

			retval = rk32_edp_write_bytes_to_dpcd(edp,
					DPCD_TRAINING_LANE0_SET,
					lane_count,
					edp->link_train.training_lane);
			if (retval < 0) {
				dev_err(edp->dev, "failed to set training lane!\n");
				return retval;
			}
		}
	} else {
		goto reduce_link_rate;
	}

	return 0;

reduce_link_rate:
	rk32_edp_reduce_link_rate(edp);
	return -EIO;
}

static int rk32_edp_get_max_rx_bandwidth(struct rk32_edp *edp,
					u8 *bandwidth)
{
	u8 data;
	int retval;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 */
	retval = rk32_edp_read_byte_from_dpcd(edp,
			DPCD_MAX_LINK_RATE, &data);
	if (retval < 0)
		return retval;

	*bandwidth = data;
	return 0;
}

static int rk32_edp_get_max_rx_lane_count(struct rk32_edp *edp,
					u8 *lane_count)
{
	u8 data;
	int retval;

	/*
	 * For DP rev.1.1, Maximum number of Main Link lanes
	 * 0x01 = 1 lane, 0x02 = 2 lanes, 0x04 = 4 lanes
	 */
	retval = rk32_edp_read_byte_from_dpcd(edp,
			DPCD_MAX_LANE_CNT, &data);
	if (retval < 0)
		return retval;

	*lane_count = DPCD_MAX_LANE_COUNT(data);
	return 0;
}

static int rk32_edp_init_training(struct rk32_edp *edp,
			enum link_lane_count_type max_lane,
			u32 max_rate)
{
	int retval;

	/*
	 * MACRO_RST must be applied after the PLL_LOCK to avoid
	 * the DP inter pair skew issue for at least 10 us
	 */
	rk32_edp_reset_macro(edp);

	
	retval = rk32_edp_get_max_rx_bandwidth(edp, &edp->link_train.link_rate);
	if (retval < 0)
		return retval;

	retval = rk32_edp_get_max_rx_lane_count(edp, &edp->link_train.lane_count);
	if (retval < 0)
		return retval;
	dev_info(edp->dev, "max link rate:%d.%dGps max number of lanes:%d\n",
			edp->link_train.link_rate * 27/100,
			edp->link_train.link_rate*27%100,
			edp->link_train.lane_count);
	
	if ((edp->link_train.link_rate != LINK_RATE_1_62GBPS) &&
	   (edp->link_train.link_rate != LINK_RATE_2_70GBPS)) {
		dev_err(edp->dev, "Rx Max Link Rate is abnormal :%x !\n",
			edp->link_train.link_rate);
		edp->link_train.link_rate = LINK_RATE_1_62GBPS;
	}

	if (edp->link_train.lane_count == 0) {
		dev_err(edp->dev, "Rx Max Lane count is abnormal :%x !\n",
			edp->link_train.lane_count);
		edp->link_train.lane_count = (u8)LANE_CNT1;
	}

	
	if (edp->link_train.lane_count > max_lane)
		edp->link_train.lane_count = max_lane;
	if (edp->link_train.link_rate > max_rate)
		edp->link_train.link_rate = max_rate;

	
	rk32_edp_analog_power_ctr(edp, 1);

	return 0;
}

static int rk32_edp_sw_link_training(struct rk32_edp *edp)
{
	int retval = 0;
	int training_finished = 0;

	edp->link_train.lt_state = LT_START;

	/* Process here */
	while (!training_finished) {
		switch (edp->link_train.lt_state) {
		case LT_START:
			retval = rk32_edp_link_start(edp);
			if (retval)
				dev_err(edp->dev, "LT Start failed\n");
			break;
		case LT_CLK_RECOVERY:
			retval = rk32_edp_process_clock_recovery(edp);
			if (retval)
				dev_err(edp->dev, "LT CR failed\n");
			break;
		case LT_EQ_TRAINING:
			retval = rk32_edp_process_equalizer_training(edp);
			if (retval)
				dev_err(edp->dev, "LT EQ failed\n");
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


static int rk32_edp_hw_link_training(struct rk32_edp *edp)
{
	u32 cnt = 50;
	u32 val;
	/* Set link rate and count as you want to establish*/
	rk32_edp_set_link_bandwidth(edp, edp->link_train.link_rate);
	rk32_edp_set_lane_count(edp, edp->link_train.lane_count);
	rk32_edp_hw_link_training_en(edp);
	mdelay(1);
	val = rk32_edp_wait_hw_lt_done(edp);
	while (val) {
		if (cnt-- <= 0) {
			dev_err(edp->dev, "hw lt timeout");
			return -ETIMEDOUT;
		}
		mdelay(1);
		val = rk32_edp_wait_hw_lt_done(edp);
	}
	
	val = rk32_edp_get_hw_lt_status(edp);
	if (val)
		dev_err(edp->dev, "hw lt err:%d\n", val);
	return val;
		
}
static int rk32_edp_set_link_train(struct rk32_edp *edp,
				u32 count,
				u32 bwtype)
{
	int retval;

	retval = rk32_edp_init_training(edp, count, bwtype);
	if (retval < 0)
		dev_err(edp->dev, "DP LT init failed!\n");
#if 0
	retval = rk32_edp_sw_link_training(edp);
#else
	retval = rk32_edp_hw_link_training(edp);
#endif

	return retval;
}

static int rk32_edp_config_video(struct rk32_edp *edp,
			struct video_info *video_info)
{
	int retval = 0;
	int timeout_loop = 0;
	int done_count = 0;

	rk32_edp_config_video_slave_mode(edp, video_info);

	rk32_edp_set_video_color_format(edp, video_info->color_depth,
			video_info->color_space,
			video_info->dynamic_range,
			video_info->ycbcr_coeff);

	if (rk32_edp_get_pll_lock_status(edp) == DP_PLL_UNLOCKED) {
		dev_err(edp->dev, "PLL is not locked yet.\n");
		return -EINVAL;
	}

	for (;;) {
		timeout_loop++;
		if (rk32_edp_is_slave_video_stream_clock_on(edp) == 0)
			break;
		if (DP_TIMEOUT_LOOP_CNT < timeout_loop) {
			dev_err(edp->dev, "Timeout of video streamclk ok\n");
			return -ETIMEDOUT;
		}

		usleep_range(1, 1);
	}

	/* Set to use the register calculated M/N video */
	rk32_edp_set_video_cr_mn(edp, CALCULATED_M, 0, 0);

	/* For video bist, Video timing must be generated by register */
	rk32_edp_set_video_timing_mode(edp, VIDEO_TIMING_FROM_CAPTURE);

	/* Disable video mute */
	rk32_edp_enable_video_mute(edp, 0);

	/* Configure video slave mode */
	rk32_edp_enable_video_master(edp, 0);

	/* Enable video */
	rk32_edp_start_video(edp);

	timeout_loop = 0;

	for (;;) {
		timeout_loop++;
		if (rk32_edp_is_video_stream_on(edp) == 0) {
			done_count++;
			if (done_count > 10)
				break;
		} else if (done_count) {
			done_count = 0;
		}
		if (DP_TIMEOUT_LOOP_CNT < timeout_loop) {
			dev_err(edp->dev, "Timeout of video streamclk ok\n");
			return -ETIMEDOUT;
		}

		usleep_range(1000, 1000);
	}

	if (retval != 0)
		dev_err(edp->dev, "Video stream is not detected!\n");

	return retval;
}

static int rk32_edp_enable_scramble(struct rk32_edp *edp, bool enable)
{
	u8 data;
	int retval;

	if (enable) {
		rk32_edp_enable_scrambling(edp);

		retval = rk32_edp_read_byte_from_dpcd(edp,
				DPCD_TRAINING_PATTERN_SET,
				&data);
		if (retval < 0)
			return retval;

		retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_TRAINING_PATTERN_SET,
				(u8)(data & ~DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	} else {
		rk32_edp_disable_scrambling(edp);

		retval = rk32_edp_read_byte_from_dpcd(edp,
				DPCD_TRAINING_PATTERN_SET,
				&data);
		if (retval < 0)
			return retval;

		retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_TRAINING_PATTERN_SET,
				(u8)(data | DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	}

	return 0;
}

static irqreturn_t rk32_edp_isr(int irq, void *arg)
{
	struct rk32_edp *edp = arg;

	dev_info(edp->dev, "rk32_edp_isr\n");
	return IRQ_HANDLED;
}

static int rk32_edp_enable(struct rk32_edp *edp)
{
	int ret = 0;
	int retry = 0;

	if (edp->enabled)
		goto out;

	edp->enabled = 1;
	clk_prepare_enable(edp->clk_edp);
	clk_prepare_enable(edp->clk_24m);
edp_phy_init:

	
	rk32_edp_init_edp(edp);

	
	ret = rk32_edp_handle_edid(edp);
	if (ret) {
		dev_err(edp->dev, "unable to handle edid\n");
		goto out;
	}

	rk32_edp_disable_rx_zmux(edp);

	
	ret = rk32_edp_enable_scramble(edp, 0);
	if (ret) {
		dev_err(edp->dev, "unable to set scramble\n");
		goto out;
	}

	ret = rk32_edp_enable_rx_to_enhanced_mode(edp, 0);
	if (ret) {
		dev_err(edp->dev, "unable to set enhanced mode\n");
		goto out;
	}
	rk32_edp_enable_enhanced_mode(edp, 0);

	
	rk32_edp_rx_control(edp,0);

       /* Link Training */
	ret = rk32_edp_set_link_train(edp, LANE_CNT4, LINK_RATE_2_70GBPS);
	if (ret) {
		dev_err(edp->dev, "link train failed\n");
		goto out;
	}

	/* Rx data enable */
	rk32_edp_rx_control(edp,1);

	rk32_edp_set_lane_count(edp, edp->video_info.lane_count);
	rk32_edp_set_link_bandwidth(edp, edp->video_info.link_rate);

#ifdef EDP_BIST_MODE
	rk32_edp_bist_cfg(edp);
#else
	rk32_edp_init_video(edp);
	ret = rk32_edp_config_video(edp, &edp->video_info);
	if (ret) {
		dev_err(edp->dev, "unable to config video\n");
		goto out;
	}
#endif

	return 0;

out:

	if (retry < 3) {
		retry++;
		goto edp_phy_init;
	}
	
	dev_err(edp->dev, "DP LT exceeds max retry count");

	return ret;
}
	
static void rk32_edp_disable(struct rk32_edp *edp)
{
	if (!edp->enabled)
		return ;

	edp->enabled = 0;

	rk32_edp_reset(edp);
	rk32_edp_analog_power_ctr(edp, 0);

	clk_disable(edp->clk_24m);
	clk_disable(edp->clk_edp);
}



static void rk32_edp_init(struct rk32_edp *edp)
{

	rk32_edp_enable(edp);
}
static int rk32_edp_probe(struct platform_device *pdev)
{
	struct rk32_edp *edp;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	edp = devm_kzalloc(&pdev->dev, sizeof(struct rk32_edp), GFP_KERNEL);
	if (!edp) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
	edp->dev = &pdev->dev;
	edp->video_info.h_sync_polarity	= 0;
	edp->video_info.v_sync_polarity	= 0;
	edp->video_info.interlaced	= 0;
	edp->video_info.color_space	= CS_RGB;
	edp->video_info.dynamic_range	= VESA;
	edp->video_info.ycbcr_coeff	= COLOR_YCBCR601;
	edp->video_info.color_depth	= COLOR_8;

	edp->video_info.link_rate	= LINK_RATE_2_70GBPS;
	edp->video_info.lane_count	= LANE_CNT4;
	rk_fb_get_prmry_screen(&edp->screen);
	if (edp->screen.type != SCREEN_EDP) {
		dev_err(&pdev->dev, "screen is not edp!\n");
		return -EINVAL;
	}
	platform_set_drvdata(pdev, edp);
	dev_set_name(edp->dev, "rk32-edp");
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edp->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(edp->regs)) {
		dev_err(&pdev->dev, "ioremap reg failed\n");
		return PTR_ERR(edp->regs);
	}
	ret = devm_request_irq(&pdev->dev, edp->irq, rk32_edp_isr, 0,
			dev_name(&pdev->dev), edp);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", edp->irq);
		return ret;
	}
	
	rk32_edp_init(edp);
	dev_info(&pdev->dev, "rk32 edp driver probe success\n");

	return 0;
}

static void rk32_edp_shutdown(struct platform_device *pdev)
{

}

#if defined(CONFIG_OF)
static const struct of_device_id rk32_edp_dt_ids[] = {
	{.compatible = "rockchip, rk32-edp",},
	{}
};

MODULE_DEVICE_TABLE(of, rk32_edp_dt_ids);
#endif

static struct platform_driver rk32_edp_driver = {
	.probe = rk32_edp_probe,
	.driver = {
		   .name = "rk32-edp",
		   .owner = THIS_MODULE,
#if defined(CONFIG_OF)
		   .of_match_table = of_match_ptr(rk32_edp_dt_ids),
#endif
	},
	.shutdown = rk32_edp_shutdown,
};

static int __init rk32_edp_module_init(void)
{
	return platform_driver_register(&rk32_edp_driver);
}

static void __exit rk32_edp_module_exit(void)
{

}

fs_initcall(rk32_edp_module_init);
module_exit(rk32_edp_module_exit);
