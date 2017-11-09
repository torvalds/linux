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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#if defined(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_device.h>
#endif

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rk32_dp.h"

/*#define EDP_BIST_MODE*/
/*#define SW_LT*/

#define RK3368_GRF_SOC_CON4	0x410
#define RK3399_GRF_SOC_CON20	0x6250

static struct rk32_edp *rk32_edp;

static int rk32_edp_clk_enable(struct rk32_edp *edp)
{
	int ret;

	if (!edp->clk_on) {
		if (edp->pd)
			clk_prepare_enable(edp->pd);
		clk_prepare_enable(edp->pclk);
		clk_prepare_enable(edp->clk_edp);

		if (edp->soctype != SOC_RK3399) {
			ret = clk_set_rate(edp->clk_24m, 24000000);
			if (ret < 0)
				pr_err("cannot set edp clk_24m %d\n", ret);
			clk_prepare_enable(edp->clk_24m);
		}
		edp->clk_on = true;
	}

	return 0;
}

static int rk32_edp_clk_disable(struct rk32_edp *edp)
{
	if (edp->clk_on) {
		clk_disable_unprepare(edp->pclk);
		clk_disable_unprepare(edp->clk_edp);

		if (edp->soctype != SOC_RK3399)
			clk_disable_unprepare(edp->clk_24m);

		if (edp->pd)
			clk_disable_unprepare(edp->pd);
		edp->clk_on = false;
	}

	return 0;
}

static int rk32_edp_pre_init(struct rk32_edp *edp)
{
	u32 val;

	if (cpu_is_rk3288()) {
#if 0
		val = GRF_EDP_REF_CLK_SEL_INTER |
			(GRF_EDP_REF_CLK_SEL_INTER << 16);
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON12);

		val = 0x80008000;
		writel_relaxed(val, RK_CRU_VIRT + 0x01d0); /*reset edp*/
		dsb(sy);
		udelay(1);
		val = 0x80000000;
		writel_relaxed(val, RK_CRU_VIRT + 0x01d0);
		dsb(sy);
		udelay(1);
#endif
	} else {
		/* The rk3368 reset the edp 24M clock and apb bus
		 * according to the CRU_SOFTRST6_CON and CRU_SOFTRST7_CON.
		 */
		if (edp->soctype != SOC_RK3399) {
			val = 0x01 | (0x01 << 16);
			regmap_write(edp->grf, RK3368_GRF_SOC_CON4, val);

			reset_control_assert(edp->rst_24m);
			usleep_range(10, 20);
			reset_control_deassert(edp->rst_24m);
		}

		reset_control_assert(edp->rst_apb);
		usleep_range(10, 20);
		reset_control_deassert(edp->rst_apb);
	}
	return 0;
}

static int rk32_edp_init_edp(struct rk32_edp *edp)
{
	struct rk_screen *screen = &edp->screen;
	u32 val = 0;

	rk_fb_get_prmry_screen(screen);

	if (cpu_is_rk3288()) {
#if 0
		if (screen->lcdc_id == 1)  /*select lcdc*/
			val = EDP_SEL_VOP_LIT | (EDP_SEL_VOP_LIT << 16);
		else
			val = EDP_SEL_VOP_LIT << 16;
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
#endif
	}

	if (edp->soctype == SOC_RK3399) {
		if (screen->lcdc_id == 1)  /*select lcdc*/
			val = EDP_SEL_VOP_LIT | (EDP_SEL_VOP_LIT << 16);
		else
			val = EDP_SEL_VOP_LIT << 16;
		clk_prepare_enable(edp->grf_clk);
		regmap_write(edp->grf, RK3399_GRF_SOC_CON20, val);
		clk_disable_unprepare(edp->grf_clk);
	}

	rk32_edp_reset(edp);
	rk32_edp_init_refclk(edp);
	rk32_edp_init_interrupt(edp);
	rk32_edp_enable_sw_function(edp);
	rk32_edp_init_analog_func(edp);
	rk32_edp_init_hpd(edp);
	rk32_edp_init_aux(edp);

	return 0;
}

/*#if 0
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
#endif*/

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
	retval = rk32_edp_read_byte_from_i2c
			(edp,
			 EDID_ADDR, EDID_EXTENSION_FLAG, &extend_block);
	if (retval < 0) {
		dev_err(edp->dev, "EDID extension flag failed!\n");
		return -EIO;
	}

	if (extend_block > 0) {
		dev_dbg(edp->dev, "EDID data includes a single extension!\n");

		/* Read EDID data */
		retval = rk32_edp_read_bytes_from_i2c
			       (edp,
				EDID_ADDR, EDID_HEADER,
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
		retval = rk32_edp_read_bytes_from_i2c
			       (edp,
				EDID_ADDR, EDID_LENGTH,
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

		retval = rk32_edp_read_byte_from_dpcd
				(edp,
				 DPCD_TEST_REQUEST, &test_vector);
		if (retval < 0) {
			dev_err(edp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = rk32_edp_write_byte_to_dpcd
				       (edp,
					DPCD_TEST_EDID_CHECKSUM,
					edid[EDID_LENGTH + EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = rk32_edp_write_byte_to_dpcd
				       (edp,
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
		retval = rk32_edp_read_bytes_from_i2c
			       (edp,
				EDID_ADDR, EDID_HEADER,
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

		retval = rk32_edp_read_byte_from_dpcd
				(edp,
				 DPCD_TEST_REQUEST, &test_vector);
		if (retval < 0) {
			dev_err(edp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = rk32_edp_write_byte_to_dpcd
					(edp,
					 DPCD_TEST_EDID_CHECKSUM,
					 edid[EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = rk32_edp_write_byte_to_dpcd
				       (edp,
					DPCD_TEST_RESPONSE,
					DPCD_TEST_EDID_CHECKSUM_WRITE);
			if (retval < 0) {
				dev_err(edp->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
		}
	}
	fb_edid_to_monspecs(edid, &edp->specs);
	dev_err(edp->dev, "EDID Read success!\n");
	return 0;
}
#define open_t 0
#if open_t
static int rk32_edp_handle_edid(struct rk32_edp *edp)
{
	u8 buf[12];
	int i;
	int retval;

	/* Read DPCD DPCD_ADDR_DPCD_REV~RECEIVE_PORT1_CAP_1 */
	retval = rk32_edp_read_bytes_from_dpcd(edp, DPCD_REV, 12, buf);
	if (retval < 0)
		return retval;

	for (i = 0; i < 12; i++)
		dev_info(edp->dev, "%d:>>0x%02x\n", i, buf[i]);
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

	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 DPCD_LANE_CNT_SET, &data);
	if (retval < 0)
		return retval;

	if (enable) {
		retval = rk32_edp_write_byte_to_dpcd
				(edp,
				 DPCD_LANE_CNT_SET,
				 DPCD_ENHANCED_FRAME_EN |
				 DPCD_LANE_COUNT_SET(data));
	} else {
		/*retval = rk32_edp_write_byte_to_dpcd(edp,
				DPCD_ADDR_CONFIGURATION_SET, 0);*/

		retval = rk32_edp_write_byte_to_dpcd
				(edp,
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

	retval = rk32_edp_read_byte_from_dpcd
			(edp,
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
#endif


#if defined(SW_LT)
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
		rk32_edp_set_lane_lane_pre_emphasis
			(edp,
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

	/*udelay(100);*/
	usleep_range(99, 100);

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
			retval = rk32_edp_read_bytes_from_dpcd
					(edp,
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

			rk32_edp_set_lane_link_training
				(edp,
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
			retval = rk32_edp_read_bytes_from_dpcd
					(edp,
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
				if (edp->link_train.cr_loop[lane] ==
					MAX_CR_LOOP) {
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

			rk32_edp_set_lane_link_training
				(edp,
				 edp->link_train.training_lane[lane], lane);
		}

		retval = rk32_edp_write_bytes_to_dpcd
				(edp,
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

	/*udelay(400);*/
	usleep_range(399, 400);

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

		retval = rk32_edp_read_byte_from_dpcd
				(edp,
				 DPCD_LANE_ALIGN_STATUS_UPDATED,
				 &link_align[2]);
		if (retval < 0) {
			dev_err(edp->dev, "failed to read lane aligne status!\n");
			return retval;
		}

		for (lane = 0; lane < lane_count; lane++) {
			retval = rk32_edp_read_bytes_from_dpcd
					(edp,
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
				rk32_edp_set_lane_link_training
				(edp,
				 edp->link_train.training_lane[lane],
				 lane);

			retval = rk32_edp_write_bytes_to_dpcd
					(edp,
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
#endif
static int rk32_edp_get_max_rx_bandwidth(struct rk32_edp *edp,
					 u8 *bandwidth)
{
	u8 data;
	int retval = 0;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 */
	retval = rk32_edp_read_byte_from_dpcd(edp,
					      DPCD_MAX_LINK_RATE, &data);
	if (retval < 0)
		*bandwidth = 0;
	else
		*bandwidth = data;
	return retval;
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
		*lane_count = 0;
	else
		*lane_count = DPCD_MAX_LANE_COUNT(data);
	return retval;
}

static int rk32_edp_init_training(struct rk32_edp *edp)
{
	int retval;

	/*
	 * MACRO_RST must be applied after the PLL_LOCK to avoid
	 * the DP inter pair skew issue for at least 10 us
	 */
	rk32_edp_reset_macro(edp);


	retval = rk32_edp_get_max_rx_bandwidth(edp,
					       &edp->link_train.link_rate);
	retval = rk32_edp_get_max_rx_lane_count(edp,
						&edp->link_train.lane_count);
	dev_info(edp->dev, "max link rate:%d.%dGps max number of lanes:%d\n",
		 edp->link_train.link_rate * 27/100,
		 edp->link_train.link_rate*27%100,
		 edp->link_train.lane_count);

	if ((edp->link_train.link_rate != LINK_RATE_1_62GBPS) &&
	    (edp->link_train.link_rate != LINK_RATE_2_70GBPS)) {
		dev_warn
		(edp->dev,
		 "Rx Mx Link Rate is abnormal:%x!default link rate:%d.%dGps\n",
		 edp->link_train.link_rate,
		 edp->video_info.link_rate*27/100,
		 edp->video_info.link_rate*27%100);
		edp->link_train.link_rate = edp->video_info.link_rate;
	}

	if (edp->link_train.lane_count == 0) {
		dev_err
		(edp->dev,
		 "Rx Max Lane count is abnormal :%x !use default lanes:%d\n",
		 edp->link_train.lane_count,
		 edp->video_info.lane_count);
		edp->link_train.lane_count = edp->video_info.lane_count;
	}

	rk32_edp_analog_power_ctr(edp, 1);


	return 0;
}

#if defined(SW_LT)
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

#else
static int rk32_edp_hw_link_training(struct rk32_edp *edp)
{
	u32 cnt = 50;
	u32 val;
	/* Set link rate and count as you want to establish*/
	rk32_edp_set_link_bandwidth(edp, edp->link_train.link_rate);
	rk32_edp_set_lane_count(edp, edp->link_train.lane_count);
	rk32_edp_hw_link_training_en(edp);
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
#endif

static int rk32_edp_set_link_train(struct rk32_edp *edp)
{
	int retval;

	retval = rk32_edp_init_training(edp);
	if (retval < 0)
		dev_err(edp->dev, "DP LT init failed!\n");
#if defined(SW_LT)
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

		udelay(1);
	}

	/* Set to use the register calculated M/N video */
	rk32_edp_set_video_cr_mn(edp, CALCULATED_M, 0, 0);

	/* For video bist, Video timing must be generated by register */
#ifndef EDP_BIST_MODE
	rk32_edp_set_video_timing_mode(edp, VIDEO_TIMING_FROM_CAPTURE);
#endif
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

		mdelay(1);
	}

	if (retval != 0)
		dev_err(edp->dev, "Video stream is not detected!\n");

	return retval;
}

static irqreturn_t rk32_edp_isr(int irq, void *arg)
{
	struct rk32_edp *edp = arg;
	enum dp_irq_type irq_type;

	irq_type = rk32_edp_get_irq_type(edp);
	switch (irq_type) {
	case DP_IRQ_TYPE_HP_CABLE_IN:
		dev_info(edp->dev, "Received irq - cable in\n");
		rk32_edp_clear_hotplug_interrupts(edp);
		break;
	case DP_IRQ_TYPE_HP_CABLE_OUT:
		dev_info(edp->dev, "Received irq - cable out\n");
		rk32_edp_clear_hotplug_interrupts(edp);
		break;
	case DP_IRQ_TYPE_HP_CHANGE:
		/*
		 * We get these change notifications once in a while, but there
		 * is nothing we can do with them. Just ignore it for now and
		 * only handle cable changes.
		 */
		dev_info(edp->dev, "Received irq - hotplug change; ignoring.\n");
		rk32_edp_clear_hotplug_interrupts(edp);
		break;
	default:
		dev_err(edp->dev, "Received irq - unknown type!\n");
		break;
	}
	return IRQ_HANDLED;
}

static int rk32_edp_enable(void)
{
	int ret = 0;
	struct rk32_edp *edp = rk32_edp;

	if (!edp->edp_en) {
		rk32_edp_clk_enable(edp);
		pm_runtime_get_sync(edp->dev);
		rk32_edp_pre_init(edp);
		rk32_edp_init_edp(edp);
		enable_irq(edp->irq);
		/*ret = rk32_edp_handle_edid(edp);
		if (ret) {
			dev_err(edp->dev, "unable to handle edid\n");
			//goto out;
		}

		ret = rk32_edp_enable_scramble(edp, 0);
		if (ret) {
			dev_err(edp->dev, "unable to set scramble\n");
			//goto out;
		}

		ret = rk32_edp_enable_rx_to_enhanced_mode(edp, 0);
		if (ret) {
			dev_err(edp->dev, "unable to set enhanced mode\n");
			//goto out;
		}
		rk32_edp_enable_enhanced_mode(edp, 1);*/

		ret = rk32_edp_set_link_train(edp);
		if (ret)
			dev_err(edp->dev, "link train failed!\n");
		else
			dev_info(edp->dev, "link training success.\n");

		rk32_edp_set_lane_count(edp, edp->link_train.lane_count);
		rk32_edp_set_link_bandwidth(edp, edp->link_train.link_rate);
		rk32_edp_init_video(edp);

#ifdef EDP_BIST_MODE
		rk32_edp_bist_cfg(edp);
#endif
		ret = rk32_edp_config_video(edp, &edp->video_info);
		if (ret)
			dev_err(edp->dev, "unable to config video\n");

		edp->edp_en = true;
	}
	return ret;
}

static int  rk32_edp_disable(void)
{
	struct rk32_edp *edp = rk32_edp;

	if (edp->edp_en) {
		edp->edp_en = false;
		disable_irq(edp->irq);
		rk32_edp_reset(edp);
		rk32_edp_analog_power_ctr(edp, 0);
		rk32_edp_clk_disable(edp);
		pm_runtime_put_sync(edp->dev);
	}

	return 0;
}


static struct rk_fb_trsm_ops trsm_edp_ops = {
	.enable = rk32_edp_enable,
	.disable = rk32_edp_disable,
};

/*#if 0
static int rk32_edp_enable_scramble(struct rk32_edp *edp, bool enable)
{
	u8 data;
	int retval;

	if (enable) {
		rk32_edp_enable_scrambling(edp);

		retval = rk32_edp_read_byte_from_dpcd
				(edp,
				 DPCD_TRAINING_PATTERN_SET,
				 &data);
		if (retval < 0)
			return retval;

		retval = rk32_edp_write_byte_to_dpcd
				(edp,
				 DPCD_TRAINING_PATTERN_SET,
				 (u8)(data & ~DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	} else {
		rk32_edp_disable_scrambling(edp);

		retval = rk32_edp_read_byte_from_dpcd
				(edp,
				 DPCD_TRAINING_PATTERN_SET,
				 &data);
		if (retval < 0)
			return retval;

		retval = rk32_edp_write_byte_to_dpcd
				(edp,
				 DPCD_TRAINING_PATTERN_SET,
				 (u8)(data | DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	}

	return 0;
}
#endif*/
static int rk32_edp_psr_enable(struct rk32_edp *edp)
{
	u8 buf;
	int retval;
	char date, psr_version;

	/*if support PSR*/
	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 PANEL_SELF_REFRESH_CAPABILITY_SUPPORTED_AND_VERSION,
			 &psr_version);
	if (retval < 0) {
		dev_err(edp->dev, "PSR DPCD Read failed!\n");
		return retval;
	} else {
		pr_info("PSR supporter and version:%x\n", psr_version);
	}

	 /*PSR capabilities*/
	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 PANEL_SELF_REFRESH_CAPABILITIES, &date);
	if (retval < 0) {
		dev_err(edp->dev, "PSR DPCD Read failed!\n");
		return retval;
	} else {
		pr_info("PSR capabilities:%x\n", date);
	}

	if (psr_version & PSR_SUPPORT) {
		pr_info("PSR config psr\n");

		/*config sink PSR*/
		buf = 0x02;
		retval = rk32_edp_write_bytes_to_dpcd(edp, PSR_ENABLE,
						      1, &buf);
		if (retval < 0) {
			dev_err(edp->dev, "PSR failed to config sink PSR!\n");
			return retval;
		} else {
			/*enable the PSR*/
			buf = 0x03;
			retval = rk32_edp_write_bytes_to_dpcd(edp,
							      PSR_ENABLE,
							      1, &buf);
			if (retval < 0) {
				dev_err(edp->dev, "PSR failed to enable the PSR!\n");
				return retval;
			}
			/*read sink config state*/
			retval = rk32_edp_read_byte_from_dpcd
						(edp,
						 PSR_ENABLE, &date);
			if (retval < 0) {
				dev_err(edp->dev, "PSR DPCD Read failed!\n");
				return retval;
			} else {
				pr_info("PSR sink config state:%x\n", date);
			}
		}

		/*enable sink crc*/
		retval = rk32_edp_read_byte_from_dpcd(edp, 0x270, &buf);
		buf |= 0x01;
		retval = rk32_edp_write_bytes_to_dpcd(edp, 0x270, 1, &buf);
	}

		return 0;
}
static int psr_header_HB_PB(struct rk32_edp *edp)
{
	u32 val;

	val = 0x0;
	writel(val, edp->regs + HB0);/*HB0*/
	val = 0x07;
	writel(val, edp->regs + HB1);/*HB1*/
	val = 0x02;
	writel(val, edp->regs + HB2);/*HB2*/
	val = 0x08;
	writel(val, edp->regs + HB3);/*HB3*/
	val = 0x00;
	writel(val, edp->regs + PB0);/*PB0*/
	val = 0x16;
	writel(val, edp->regs + PB1);/*PB1*/
	val = 0xce;
	writel(val, edp->regs + PB2);/*PB2*/
	val = 0x5d;
	writel(val, edp->regs + PB3);/*PB3*/

	return 0;
}

static int psr_enable_sdp(struct rk32_edp *edp)
{
	u32 val;

	val = readl(edp->regs + SPDIF_AUDIO_CTL_0);
	val |= 0x08;
	writel(val, edp->regs + SPDIF_AUDIO_CTL_0);/*enable SDP*/
	val = readl(edp->regs + SPDIF_AUDIO_CTL_0);
	pr_info("PSR reuse_spd_en:%x\n", val);

	val = 0x83;
	writel(val, edp->regs + IF_TYPE);/*enable IF_TYPE*/
	val = readl(edp->regs + IF_TYPE);
	pr_info("PSR IF_TYPE :%x\n", val);

	val = readl(edp->regs + PKT_SEND_CTL);
	val |= 0x10;
	writel(val, edp->regs + PKT_SEND_CTL);/*enable IF_UP*/
	val = readl(edp->regs + PKT_SEND_CTL);
	pr_info("PSR if_up :%x\n", val);

	val = readl(edp->regs + PKT_SEND_CTL);
	val |= 0x01;
	writel(val, edp->regs + PKT_SEND_CTL);/*enable IF_EN*/
	val = readl(edp->regs + PKT_SEND_CTL);
	pr_info("PSR if_en:%x\n", val);
	return 0;
}
static int edp_disable_psr(struct rk32_edp *edp)
{
	u8 buf;
	int retval;
	char date;

	/*disable sink PSR*/
	retval = rk32_edp_read_byte_from_dpcd(edp,
					      PSR_ENABLE, &date);
	if (retval < 0) {
		dev_err(edp->dev, "PSR sink original config Read failed!\n");
		return retval;
	}
	buf = date&0xfe;
	retval = rk32_edp_write_bytes_to_dpcd
					(edp,
					 PSR_ENABLE,
					 1, &buf);
	if (retval < 0) {
		dev_err(edp->dev, "PSR failed to disable sink PSR!\n");
		return retval;
	}

	pr_info("PSR disable success!!\n");
	return 0;
}

static int edp_psr_state(struct rk32_edp *edp, int state)
{
		u32 val;
		/*wait for VD blank*/
		if  (rk_fb_poll_wait_frame_complete()) {
			psr_header_HB_PB(edp);

			val = state;
			writel(val, edp->regs + DB1);
			/*val = readl(edp->regs + DB1);
			pr_info("PSR set DB1 state 0x0:%x\n", val);

			for (i = 0; i < 22; i++)
				 writel(0, edp->regs + DB2 + 4 * i);*/

			psr_enable_sdp(edp);
		}
	return 0;
}


static int phy_power_channel(struct rk32_edp *edp, int state)
{
	u32 val;

	val = state;
	writel(val, edp->regs + DP_PD);

	return 0;
}

#if defined(CONFIG_DEBUG_FS)

static int edp_dpcd_debugfs_show(struct seq_file *s, void *v)
{
	int retval;
	unsigned char buf[12];
	struct rk32_edp *edp = s->private;

	if (!edp) {
		dev_err(edp->dev, "no edp device!\n");
		return -ENODEV;
	}

	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 PANEL_SELF_REFRESH_CAPABILITY_SUPPORTED_AND_VERSION,
			 &buf[0]);
	seq_printf(s, "0x70 %x\n", buf[0]);

	/*PSR capabilities*/
	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 PANEL_SELF_REFRESH_CAPABILITIES, &buf[0]);
	seq_printf(s, "0x71 %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd
			(edp,
			 PSR_ENABLE, &buf[0]);
	seq_printf(s, "0x170 %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd(edp, 0x2006, &buf[0]);
	seq_printf(s, "0x2006 %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd(edp, 0x2007, &buf[0]);
	seq_printf(s, "0x2007 %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd(edp, 0x2008, &buf[0]);
	seq_printf(s, "0x2008 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x2009, &buf[0]);
	seq_printf(s, "0x2009 %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd(edp, 0x200a, &buf[0]);
	seq_printf(s, "0x200a %x\n", buf[0]);

	retval = rk32_edp_read_byte_from_dpcd(edp, 0x240, &buf[0]);
	seq_printf(s, "0x240 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x241, &buf[0]);
	seq_printf(s, "0x241 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x242, &buf[0]);
	seq_printf(s, "0x242 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x243, &buf[0]);
	seq_printf(s, "0x243 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x244, &buf[0]);
	seq_printf(s, "0x244 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x245, &buf[0]);
	seq_printf(s, "0x245 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x270, &buf[0]);
	seq_printf(s, "0x270 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x246, &buf[0]);
	seq_printf(s, "0x246 %x\n", buf[0]);

	/*retval = rk32_edp_read_byte_from_dpcd(edp, 0x222, &buf[0]);
	seq_printf(s, "0x222 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x223, &buf[0]);
	seq_printf(s, "0x223 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x224, &buf[0]);
	seq_printf(s, "0x224 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x225, &buf[0]);
	seq_printf(s, "0x225 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x226, &buf[0]);
	seq_printf(s, "0x226 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x227, &buf[0]);
	seq_printf(s, "0x227 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x228, &buf[0]);
	seq_printf(s, "0x228 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x229, &buf[0]);
	seq_printf(s, "0x229 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22a, &buf[0]);
	seq_printf(s, "0x22a %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22b, &buf[0]);
	seq_printf(s, "0x22b %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22c, &buf[0]);
	seq_printf(s, "0x22c %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22d, &buf[0]);
	seq_printf(s, "0x22d %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22e, &buf[0]);
	seq_printf(s, "0x22e %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x22f, &buf[0]);
	seq_printf(s, "0x22f %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x230, &buf[0]);
	seq_printf(s, "0x230 %x\n", buf[0]);
	retval = rk32_edp_read_byte_from_dpcd(edp, 0x231, &buf[0]);
	seq_printf(s, "0x231 %x\n", buf[0]);*/

	/*rk32_edp_read_bytes_from_dpcd(edp,
			DPCD_SYMBOL_ERR_CONUT_LANE0, 12, buf);
	for (i = 0; i < 12; i++)
		seq_printf(s, "0x%02x>>0x%02x\n", 0x210 + i, buf[i]);*/
	return 0;
}

static ssize_t edp_dpcd_write(struct file *file,
			      const char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	return count;
}

static int edp_edid_debugfs_show(struct seq_file *s, void *v)
{
	struct rk32_edp *edp = s->private;

	if (!edp) {
		dev_err(edp->dev, "no edp device!\n");
		return -ENODEV;
	}
	rk32_edp_read_edid(edp);
	seq_puts(s, "edid");
	return 0;
}

static ssize_t edp_edid_write(struct file *file,
			      const char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	struct rk32_edp *edp =
		((struct seq_file *)file->private_data)->private;

	if (!edp) {
		dev_err(edp->dev, "no edp device!\n");
		return -ENODEV;
	}
	rk32_edp_disable();
	rk32_edp_enable();
	return count;
}

static int edp_reg_debugfs_show(struct seq_file *s, void *v)
{
	int i = 0;
	struct rk32_edp *edp = s->private;

	if (!edp) {
		dev_err(edp->dev, "no edp device!\n");
		return -ENODEV;
	}

	for (i = 0; i < 0x284; i++) {
		if (!(i%4))
			seq_printf(s, "\n%08x:  ", i*4);
		seq_printf(s, "%08x ", readl(edp->regs + i*4));
	}
	return 0;
}

static ssize_t edp_reg_write(struct file *file,
			     const char __user *buf, size_t count,
			     loff_t *ppos)
{
	return count;
}

static int edp_psr_debugfs_show(struct seq_file *s, void *v)
{
	return 0;
}
static ssize_t edp_psr_write(struct file *file,
			     const char __user *buf,
			     size_t count, loff_t *ppos)
{
	int a;
	char kbuf[25];
	int retval;
	struct rk32_edp *edp =
		((struct seq_file *)file->private_data)->private;

	if (!edp) {
		dev_err(edp->dev, "no edp device!\n");
		return -ENODEV;
	}
	memset(kbuf, 0, 25);
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	retval = kstrtoint(kbuf, 0, &a);
	if (retval)
		return retval;
	/*retval = sscanf(kbuf, "%d", &a);
	if (retval < 0) {
		dev_err(edp->dev, "PSR failed sscanf!\n");
		return retval;
	}*/
	/*disable psr*/
	if (0 == a)
		edp_disable_psr(edp);
	/*enable psr*/
	if (1 == a)
		rk32_edp_psr_enable(edp);
	/*inactive psr*/
	if (2 == a)
		edp_psr_state(edp, 0x0);
	/*sink state 2*/
	if  (3 == a)
		edp_psr_state(edp, 0x01);
	/*sink state 3*/
	if  (4 == a)
		edp_psr_state(edp, 0x03);
	/*open 4 lanes*/
	if  (5 == a) {
		phy_power_channel(edp, 0xff);
		usleep_range(9, 10);
		phy_power_channel(edp, 0x7f);
		usleep_range(9, 10);
		phy_power_channel(edp, 0x0);
	}
	/*close 4 lanes*/
	if (6 == a) {
		phy_power_channel(edp, 0x7f);
		usleep_range(9, 10);
		phy_power_channel(edp, 0x0f);
	}

	return count;
}

#define EDP_DEBUG_ENTRY(name) \
static int edp_##name##_debugfs_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, edp_##name##_debugfs_show, inode->i_private); \
} \
\
static const struct file_operations edp_##name##_debugfs_fops = { \
	.owner = THIS_MODULE, \
	.open = edp_##name##_debugfs_open, \
	.read = seq_read, \
	.write = edp_##name##_write,	\
	.llseek = seq_lseek, \
	.release = single_release, \
}

EDP_DEBUG_ENTRY(psr);
EDP_DEBUG_ENTRY(dpcd);
EDP_DEBUG_ENTRY(edid);
EDP_DEBUG_ENTRY(reg);
#endif

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

	edp->video_info.link_rate	= LINK_RATE_1_62GBPS;
	edp->video_info.lane_count	= LANE_CNT4;
	rk_fb_get_prmry_screen(&edp->screen);
	if (edp->screen.type != SCREEN_EDP) {
		dev_err(&pdev->dev, "screen is not edp!\n");
		return -EINVAL;
	}

	edp->soctype = (unsigned long)of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, edp);
	dev_set_name(edp->dev, "rk32-edp");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edp->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(edp->regs)) {
		dev_err(&pdev->dev, "ioremap reg failed\n");
		return PTR_ERR(edp->regs);
	}

	edp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(edp->grf) && !cpu_is_rk3288()) {
		dev_err(&pdev->dev, "can't find rockchip,grf property\n");
		return PTR_ERR(edp->grf);
	}

	if (edp->soctype == SOC_RK3399) {
		edp->grf_clk = devm_clk_get(&pdev->dev, "clk_grf");
		if (IS_ERR(edp->grf_clk)) {
			dev_err(&pdev->dev, "cannot get grf clk\n");
			return PTR_ERR(edp->grf_clk);
		}
	}

	edp->pd = devm_clk_get(&pdev->dev, "pd_edp");
	if (IS_ERR(edp->pd)) {
		dev_err(&pdev->dev, "cannot get pd\n");
		edp->pd = NULL;
	}

	edp->clk_edp = devm_clk_get(&pdev->dev, "clk_edp");
	if (IS_ERR(edp->clk_edp)) {
		dev_err(&pdev->dev, "cannot get clk_edp\n");
		return PTR_ERR(edp->clk_edp);
	}

	if (edp->soctype != SOC_RK3399) {
		edp->clk_24m = devm_clk_get(&pdev->dev, "clk_edp_24m");
		if (IS_ERR(edp->clk_24m)) {
			dev_err(&pdev->dev, "cannot get clk_edp_24m\n");
			return PTR_ERR(edp->clk_24m);
		}
	}

	edp->pclk = devm_clk_get(&pdev->dev, "pclk_edp");
	if (IS_ERR(edp->pclk)) {
		dev_err(&pdev->dev, "cannot get pclk\n");
		return PTR_ERR(edp->pclk);
	}

	/* We use the reset API to control the software reset at this version
	 * and later, and we reserve the code that setting the cru regs directly
	 * in the rk3288.
	 */
	if (edp->soctype != SOC_RK3399) {
		/*edp 24m need sorft reset*/
		edp->rst_24m = devm_reset_control_get(&pdev->dev, "edp_24m");
		if (IS_ERR(edp->rst_24m))
			dev_err(&pdev->dev, "failed to get reset\n");
	}

	/* edp ctrl apb bus need sorft reset */
	edp->rst_apb = devm_reset_control_get(&pdev->dev, "edp_apb");
	if (IS_ERR(edp->rst_apb))
		dev_err(&pdev->dev, "failed to get reset\n");
	rk32_edp_clk_enable(edp);
	if (!support_uboot_display())
		rk32_edp_pre_init(edp);
	edp->irq = platform_get_irq(pdev, 0);
	if (edp->irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return edp->irq;
	}
	ret = devm_request_irq(&pdev->dev, edp->irq, rk32_edp_isr, 0,
			       dev_name(&pdev->dev), edp);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", edp->irq);
		return ret;
	}
	disable_irq_nosync(edp->irq);
	if (!support_uboot_display())
		rk32_edp_clk_disable(edp);

	pm_runtime_enable(&pdev->dev);
	if (support_uboot_display()) {
		edp->edp_en = true;
		pm_runtime_get_sync(&pdev->dev);
	}

	rk32_edp = edp;
	rk_fb_trsm_ops_register(&trsm_edp_ops, SCREEN_EDP);
#if defined(CONFIG_DEBUG_FS)
	edp->debugfs_dir = debugfs_create_dir("edp", NULL);
	if (IS_ERR(edp->debugfs_dir)) {
		dev_err(edp->dev, "failed to create debugfs dir for edp!\n");
	} else {
		debugfs_create_file("dpcd", S_IRUSR, edp->debugfs_dir,
				    edp, &edp_dpcd_debugfs_fops);
		debugfs_create_file("edid", S_IRUSR, edp->debugfs_dir,
				    edp, &edp_edid_debugfs_fops);
		debugfs_create_file("reg", S_IRUSR, edp->debugfs_dir,
				    edp, &edp_reg_debugfs_fops);
		debugfs_create_file("psr", S_IRUSR, edp->debugfs_dir,
				    edp, &edp_psr_debugfs_fops);
	}

#endif
	dev_info(&pdev->dev, "rk32 edp driver probe success\n");

	return 0;
}

static int rockchip_edp_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id rk32_edp_dt_ids[] = {
	{.compatible = "rockchip,rk32-edp", .data = (void *)SOC_COMMON},
	{.compatible = "rockchip,rk3399-edp-fb", .data = (void *)SOC_RK3399},
	{}
};

MODULE_DEVICE_TABLE(of, rk32_edp_dt_ids);
#endif

static struct platform_driver rk32_edp_driver = {
	.probe = rk32_edp_probe,
	.remove = rockchip_edp_remove,
	.driver = {
		   .name = "rk32-edp",
		   .owner = THIS_MODULE,
#if defined(CONFIG_OF)
		   .of_match_table = of_match_ptr(rk32_edp_dt_ids),
#endif
	},
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
