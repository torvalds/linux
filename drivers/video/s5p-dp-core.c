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
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/lcd.h>

#include <video/s5p-dp.h>

#include <plat/cpu.h>

#include "s5p-dp-core.h"

#if defined(CONFIG_MACH_ODROIDXU)    

#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>

#define DP_HOTPLUG_RETRY    5

struct dp_hotplug   {
    struct s5p_dp_device    *dp;
    unsigned char           init_status;
    unsigned char           gpio_status;
    unsigned char           retry;
    unsigned int            irq_gpio;
    unsigned int            irq;
    unsigned int            irq_flags;
	struct delayed_work		work;
};

static struct  dp_hotplug dp_hotplug = {
    .irq_gpio = EXYNOS5410_GPX3(0),
    .irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_DISABLED,
};

static void s5p_dp_disable  (struct s5p_dp_device *dp);
static int  s5p_dp_enable   (struct s5p_dp_device *dp);

static 	void 	dp_hotplug_work		(struct work_struct *work)
{
    if(dp_hotplug.gpio_status)  {
        if(!dp_hotplug.init_status) {
            if(s5p_dp_enable(dp_hotplug.dp))    {
                if(dp_hotplug.retry < DP_HOTPLUG_RETRY) {
                    dp_hotplug.retry++;
                    schedule_delayed_work(&dp_hotplug.work, msecs_to_jiffies(500));
                }
                else
                    s5p_dp_disable(dp_hotplug.dp);
            }
            else    {
                dp_hotplug.init_status = 1;
                dp_hotplug.retry = 0;
            }
        }
    }
    else    {
        if(dp_hotplug.init_status)  {
            dp_hotplug.init_status = 0;
            s5p_dp_disable(dp_hotplug.dp);
        }
    }
}

static irqreturn_t dp_hotplug_irq_handler(int irq, void *arg)
{
	if(dp_hotplug.gpio_status != gpio_get_value(dp_hotplug.irq_gpio))   {
    	dp_hotplug.gpio_status = gpio_get_value(dp_hotplug.irq_gpio);
	}

	schedule_delayed_work(&dp_hotplug.work, msecs_to_jiffies(500));
	
	return IRQ_HANDLED;
}
#endif

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

#if !defined(CONFIG_MACH_ODROIDXU)    
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
#endif

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
	retval = s5p_dp_read_byte_from_i2c(dp, I2C_EDID_DEVICE_ADDR,
				EDID_EXTENSION_FLAG,
				&extend_block);
	if (retval < 0) {
		dev_err(dp->dev, "EDID extension flag failed!\n");
		return -EIO;
	}

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

		retval = s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_TEST_REQUEST,
				&test_vector);
		if (retval < 0) {
			dev_err(dp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = s5p_dp_write_byte_to_dpcd(dp,
					DPCD_ADDR_TEST_EDID_CHECKSUM,
					edid[EDID_BLOCK_LENGTH + EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(dp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = s5p_dp_write_byte_to_dpcd(dp,
					DPCD_ADDR_TEST_RESPONSE,
					DPCD_TEST_EDID_CHECKSUM_WRITE);
			if (retval < 0) {
				dev_err(dp->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
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

		retval = s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_TEST_REQUEST,
				&test_vector);
		if (retval < 0) {
			dev_err(dp->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = s5p_dp_write_byte_to_dpcd(dp,
					DPCD_ADDR_TEST_EDID_CHECKSUM,
					edid[EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(dp->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			retval = s5p_dp_write_byte_to_dpcd(dp,
					DPCD_ADDR_TEST_RESPONSE,
					DPCD_TEST_EDID_CHECKSUM_WRITE);
			if (retval < 0) {
				dev_err(dp->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
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

	/* Read DPCD DPCD_ADDR_DPCD_REV~RECEIVE_PORT1_CAP_1 */
	retval = s5p_dp_read_bytes_from_dpcd(dp,
			DPCD_ADDR_DPCD_REV,
			12, buf);
	if (retval < 0)
		return retval;

	/* Read EDID */
	for (i = 0; i < 3; i++) {
		retval = s5p_dp_read_edid(dp);
		if (retval == 0)
			break;
	}

	return retval;
}

static int s5p_dp_enable_rx_to_enhanced_mode(struct s5p_dp_device *dp,
						bool enable)
{
	u8 data;
	int retval;

	retval = s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_LANE_COUNT_SET, &data);
	if (retval < 0)
		return retval;

	if (enable) {
		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_LANE_COUNT_SET,
				DPCD_ENHANCED_FRAME_EN |
				DPCD_LANE_COUNT_SET(data));
	} else {
		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_CONFIGURATION_SET, 0);

		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_LANE_COUNT_SET,
				DPCD_LANE_COUNT_SET(data));
	}

	return retval;
}

void s5p_dp_rx_control(struct s5p_dp_device *dp, bool enable)
{
#if !defined(CONFIG_MACH_ODROIDXU)    
	s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_USER_DEFINED1,0);
	s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_USER_DEFINED2,0x90);

	if (enable) {
		s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_USER_DEFINED3,0x84);
		s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_USER_DEFINED3,0x00);
	} else {
		s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_USER_DEFINED3,0x80);
	}
#endif	
}

static int s5p_dp_is_enhanced_mode_available(struct s5p_dp_device *dp)
{
	u8 data;
	int retval;

	retval = s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_MAX_LANE_COUNT, &data);
	if (retval < 0)
		return retval;

	return DPCD_ENHANCED_FRAME_CAP(data);
}

static void s5p_dp_disable_rx_zmux(struct s5p_dp_device *dp)
{
#if !defined(CONFIG_MACH_ODROIDXU)    
	s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_USER_DEFINED1, 0);
	s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_USER_DEFINED2, 0x83);
	s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_USER_DEFINED3, 0x27);
#endif			
}

static int s5p_dp_set_enhanced_mode(struct s5p_dp_device *dp)
{
	u8 data;
	int retval;

	retval = s5p_dp_is_enhanced_mode_available(dp);
	if (retval < 0)
		return retval;

	data = (u8)retval;
	retval = s5p_dp_enable_rx_to_enhanced_mode(dp, data);
	if (retval < 0)
		return retval;

	s5p_dp_enable_enhanced_mode(dp, data);

	return 0;
}

static int s5p_dp_training_pattern_dis(struct s5p_dp_device *dp)
{
	int retval;

	s5p_dp_set_training_pattern(dp, DP_NONE);

	retval = s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			DPCD_TRAINING_PATTERN_DISABLED);
	if (retval < 0)
		return retval;

	return 0;
}

static void s5p_dp_set_lane_lane_pre_emphasis(struct s5p_dp_device *dp,
					int pre_emphasis, int lane)
{
	switch (lane) {
	case 0:
		s5p_dp_set_lane0_pre_emphasis(dp, pre_emphasis);
		break;
	case 1:
		s5p_dp_set_lane1_pre_emphasis(dp, pre_emphasis);
		break;

	case 2:
		s5p_dp_set_lane2_pre_emphasis(dp, pre_emphasis);
		break;

	case 3:
		s5p_dp_set_lane3_pre_emphasis(dp, pre_emphasis);
		break;
	}
}

static int s5p_dp_link_start(struct s5p_dp_device *dp)
{
	u8 buf[4];
	int lane;
	int lane_count;
	int retval;

	lane_count = dp->link_train.lane_count;

	dp->link_train.lt_state = CLOCK_RECOVERY;
	dp->link_train.eq_loop = 0;

	for (lane = 0; lane < lane_count; lane++)
		dp->link_train.cr_loop[lane] = 0;

	/* Set sink to D0 (Sink Not Ready) mode. */
	retval = s5p_dp_write_byte_to_dpcd(dp, DPCD_ADDR_SINK_POWER_STATE,
				DPCD_SET_POWER_STATE_D0);
	if (retval < 0) {
		dev_err(dp->dev, "failed to set sink device to D0!\n");
		return retval;
	}

	/* Set link rate and count as you want to establish*/
	s5p_dp_set_link_bandwidth(dp, dp->link_train.link_rate);
	s5p_dp_set_lane_count(dp, dp->link_train.lane_count);

	/* Setup RX configuration */
	buf[0] = dp->link_train.link_rate;
	buf[1] = dp->link_train.lane_count;
	retval = s5p_dp_write_bytes_to_dpcd(dp, DPCD_ADDR_LINK_BW_SET,
					2, buf);
	if (retval < 0) {
		dev_err(dp->dev, "failed to set bandwidth and lane count!\n");
		return retval;
	}

	/* Set TX pre-emphasis to level1 */
	for (lane = 0; lane < lane_count; lane++)
		s5p_dp_set_lane_lane_pre_emphasis(dp,
			PRE_EMPHASIS_LEVEL_1, lane);

	/* Set training pattern 1 */
	s5p_dp_set_training_pattern(dp, TRAINING_PTN1);

	/* Set RX training pattern */
	retval = s5p_dp_write_byte_to_dpcd(dp,
			DPCD_ADDR_TRAINING_PATTERN_SET,
			DPCD_SCRAMBLING_DISABLED |
			DPCD_TRAINING_PATTERN_1);
	if (retval < 0) {
		dev_err(dp->dev, "failed to set training pattern 1!\n");
		return retval;
	}

	for (lane = 0; lane < lane_count; lane++)
		buf[lane] = DPCD_PRE_EMPHASIS_PATTERN2_LEVEL0 |
			    DPCD_VOLTAGE_SWING_PATTERN1_LEVEL0;
	retval = s5p_dp_write_bytes_to_dpcd(dp,
			DPCD_ADDR_TRAINING_LANE0_SET,
			lane_count, buf);
	if (retval < 0) {
		dev_err(dp->dev, "failed to set training lane!\n");
		return retval;
	}

	return 0;
}

static unsigned char s5p_dp_get_lane_status(u8 link_status[2], int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = link_status[lane>>1];

	return (link_value >> shift) & 0xf;
}

static int s5p_dp_clock_recovery_ok(u8 link_status[2], int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = s5p_dp_get_lane_status(link_status, lane);
		if ((lane_status & DPCD_LANE_CR_DONE) == 0)
			return -EINVAL;
	}
	return 0;
}

static int s5p_dp_channel_eq_ok(u8 link_align[3], int lane_count)
{
	int lane;
	u8 lane_align;
	u8 lane_status;

	lane_align = link_align[2];
	if ((lane_align & DPCD_INTERLANE_ALIGN_DONE) == 0)
		return -EINVAL;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = s5p_dp_get_lane_status(link_align, lane);
		lane_status &= DPCD_CHANNEL_EQ_BITS;
		if (lane_status != DPCD_CHANNEL_EQ_BITS)
			return -EINVAL;
	}

	return 0;
}

static unsigned char s5p_dp_get_adjust_request_voltage(u8 adjust_request[2],
							int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return (link_value >> shift) & 0x3;
}

static unsigned char s5p_dp_get_adjust_request_pre_emphasis(
					u8 adjust_request[2],
					int lane)
{
	int shift = (lane & 1) * 4;
	u8 link_value = adjust_request[lane>>1];

	return ((link_value >> shift) & 0xc) >> 2;
}

static void s5p_dp_set_lane_link_training(struct s5p_dp_device *dp,
					u8 training_lane_set, int lane)
{
	switch (lane) {
	case 0:
		s5p_dp_set_lane0_link_training(dp, training_lane_set);
		break;
	case 1:
		s5p_dp_set_lane1_link_training(dp, training_lane_set);
		break;

	case 2:
		s5p_dp_set_lane2_link_training(dp, training_lane_set);
		break;

	case 3:
		s5p_dp_set_lane3_link_training(dp, training_lane_set);
		break;
	}
}

static unsigned int s5p_dp_get_lane_link_training(
				struct s5p_dp_device *dp,
				int lane)
{
	u32 reg = 0;

	switch (lane) {
	case 0:
		reg = s5p_dp_get_lane0_link_training(dp);
		break;
	case 1:
		reg = s5p_dp_get_lane1_link_training(dp);
		break;
	case 2:
		reg = s5p_dp_get_lane2_link_training(dp);
		break;
	case 3:
		reg = s5p_dp_get_lane3_link_training(dp);
		break;
	}

	return reg;
}

static void s5p_dp_reduce_link_rate(struct s5p_dp_device *dp)
{
	s5p_dp_training_pattern_dis(dp);

	dp->link_train.lt_state = FAILED;
}

static int s5p_dp_process_clock_recovery(struct s5p_dp_device *dp)
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

	lane_count = dp->link_train.lane_count;

	retval = s5p_dp_read_bytes_from_dpcd(dp,
			DPCD_ADDR_LANE0_1_STATUS,
			2, link_status);
	if (retval < 0) {
		dev_err(dp->dev, "failed to read lane status!\n");
		return retval;
	}

	if (s5p_dp_clock_recovery_ok(link_status, lane_count) == 0) {
		/* set training pattern 2 for EQ */
		s5p_dp_set_training_pattern(dp, TRAINING_PTN2);

		for (lane = 0; lane < lane_count; lane++) {
			retval = s5p_dp_read_bytes_from_dpcd(dp,
					DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(dp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = s5p_dp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = s5p_dp_get_adjust_request_pre_emphasis(
							adjust_request, lane);
			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			dp->link_train.training_lane[lane] = training_lane;

			s5p_dp_set_lane_link_training(dp,
				dp->link_train.training_lane[lane],
				lane);
		}

		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				DPCD_SCRAMBLING_DISABLED |
				DPCD_TRAINING_PATTERN_2);
		if (retval < 0) {
			dev_err(dp->dev, "failed to set training pattern 2!\n");
			return retval;
		}

		retval = s5p_dp_write_bytes_to_dpcd(dp,
				DPCD_ADDR_TRAINING_LANE0_SET,
				lane_count,
				dp->link_train.training_lane);
		if (retval < 0) {
			dev_err(dp->dev, "failed to set training lane!\n");
			return retval;
		}

		dev_info(dp->dev, "Link Training Clock Recovery success\n");
		dp->link_train.lt_state = EQUALIZER_TRAINING;
	} else {
		for (lane = 0; lane < lane_count; lane++) {
			training_lane = s5p_dp_get_lane_link_training(
							dp, lane);
			retval = s5p_dp_read_bytes_from_dpcd(dp,
					DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(dp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = s5p_dp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = s5p_dp_get_adjust_request_pre_emphasis(
							adjust_request, lane);

			if (voltage_swing == VOLTAGE_LEVEL_3 ||
			    pre_emphasis == PRE_EMPHASIS_LEVEL_3) {
				dev_err(dp->dev, "voltage or pre emphasis reached max level\n");
				goto reduce_link_rate;
			}

			if ((DPCD_VOLTAGE_SWING_GET(training_lane) ==
					voltage_swing) &&
			   (DPCD_PRE_EMPHASIS_GET(training_lane) ==
					pre_emphasis)) {
				dp->link_train.cr_loop[lane]++;
				if (dp->link_train.cr_loop[lane] == MAX_CR_LOOP) {
					dev_err(dp->dev, "CR Max loop\n");
					goto reduce_link_rate;
				}
			}

			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			dp->link_train.training_lane[lane] = training_lane;

			s5p_dp_set_lane_link_training(dp,
				dp->link_train.training_lane[lane], lane);
		}

		retval = s5p_dp_write_bytes_to_dpcd(dp,
				DPCD_ADDR_TRAINING_LANE0_SET,
				lane_count,
				dp->link_train.training_lane);
		if (retval < 0) {
			dev_err(dp->dev, "failed to set training lane!\n");
			return retval;
		}
	}

	return 0;

reduce_link_rate:
	s5p_dp_reduce_link_rate(dp);
	return -EIO;
}

static int s5p_dp_process_equalizer_training(struct s5p_dp_device *dp)
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

	lane_count = dp->link_train.lane_count;

	retval = s5p_dp_read_bytes_from_dpcd(dp,
			DPCD_ADDR_LANE0_1_STATUS,
			2, link_status);
	if (retval < 0) {
		dev_err(dp->dev, "failed to read lane status!\n");
		return retval;
	}

	if (s5p_dp_clock_recovery_ok(link_status, lane_count) == 0) {
		link_align[0] = link_status[0];
		link_align[1] = link_status[1];

		retval = s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_LANE_ALIGN_STATUS_UPDATED,
				&link_align[2]);
		if (retval < 0) {
			dev_err(dp->dev, "failed to read lane aligne status!\n");
			return retval;
		}

		for (lane = 0; lane < lane_count; lane++) {
			retval = s5p_dp_read_bytes_from_dpcd(dp,
					DPCD_ADDR_ADJUST_REQUEST_LANE0_1,
					2, adjust_request);
			if (retval < 0) {
				dev_err(dp->dev, "failed to read adjust request!\n");
				return retval;
			}

			voltage_swing = s5p_dp_get_adjust_request_voltage(
							adjust_request, lane);
			pre_emphasis = s5p_dp_get_adjust_request_pre_emphasis(
							adjust_request, lane);
			training_lane = DPCD_VOLTAGE_SWING_SET(voltage_swing) |
					DPCD_PRE_EMPHASIS_SET(pre_emphasis);

			if (voltage_swing == VOLTAGE_LEVEL_3)
				training_lane |= DPCD_MAX_SWING_REACHED;
			if (pre_emphasis == PRE_EMPHASIS_LEVEL_3)
				training_lane |= DPCD_MAX_PRE_EMPHASIS_REACHED;

			dp->link_train.training_lane[lane] = training_lane;
		}

		if (s5p_dp_channel_eq_ok(link_align, lane_count) == 0) {
			/* traing pattern Set to Normal */
			retval = s5p_dp_training_pattern_dis(dp);
			if (retval < 0) {
				dev_err(dp->dev, "failed to disable training pattern!\n");
				return retval;
			}

			dev_info(dp->dev, "Link Training success!\n");

			s5p_dp_get_link_bandwidth(dp, &reg);
			dp->link_train.link_rate = reg;
			dev_dbg(dp->dev, "final bandwidth = %.2x\n",
				dp->link_train.link_rate);

			s5p_dp_get_lane_count(dp, &reg);
			dp->link_train.lane_count = reg;
			dev_dbg(dp->dev, "final lane count = %.2x\n",
				dp->link_train.lane_count);

			dp->link_train.lt_state = FINISHED;
		} else {
			/* not all locked */
			dp->link_train.eq_loop++;

			if (dp->link_train.eq_loop > MAX_EQ_LOOP) {
				dev_err(dp->dev, "EQ Max loop\n");
				goto reduce_link_rate;
			}

			for (lane = 0; lane < lane_count; lane++)
				s5p_dp_set_lane_link_training(dp,
					dp->link_train.training_lane[lane],
					lane);

			retval = s5p_dp_write_bytes_to_dpcd(dp,
					DPCD_ADDR_TRAINING_LANE0_SET,
					lane_count,
					dp->link_train.training_lane);
			if (retval < 0) {
				dev_err(dp->dev, "failed to set training lane!\n");
				return retval;
			}
		}
	} else {
		goto reduce_link_rate;
	}

	return 0;

reduce_link_rate:
	s5p_dp_reduce_link_rate(dp);
	return -EIO;
}

static int s5p_dp_get_max_rx_bandwidth(struct s5p_dp_device *dp,
					u8 *bandwidth)
{
	u8 data;
	int retval;

	/*
	 * For DP rev.1.1, Maximum link rate of Main Link lanes
	 * 0x06 = 1.62 Gbps, 0x0a = 2.7 Gbps
	 */
	retval = s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_MAX_LINK_RATE, &data);
	if (retval < 0)
		return retval;

	*bandwidth = data;
	return 0;
}

static int s5p_dp_get_max_rx_lane_count(struct s5p_dp_device *dp,
					u8 *lane_count)
{
	u8 data;
	int retval;

	/*
	 * For DP rev.1.1, Maximum number of Main Link lanes
	 * 0x01 = 1 lane, 0x02 = 2 lanes, 0x04 = 4 lanes
	 */
	retval = s5p_dp_read_byte_from_dpcd(dp,
			DPCD_ADDR_MAX_LANE_COUNT, &data);
	if (retval < 0)
		return retval;

	*lane_count = DPCD_MAX_LANE_COUNT(data);
	return 0;
}

static int s5p_dp_init_training(struct s5p_dp_device *dp,
			enum link_lane_count_type max_lane,
			enum link_rate_type max_rate)
{
	int retval;

	/*
	 * MACRO_RST must be applied after the PLL_LOCK to avoid
	 * the DP inter pair skew issue for at least 10 us
	 */
	s5p_dp_reset_macro(dp);

	/* Initialize by reading RX's DPCD */
	retval = s5p_dp_get_max_rx_bandwidth(dp, &dp->link_train.link_rate);
	if (retval < 0)
		return retval;

	retval = s5p_dp_get_max_rx_lane_count(dp, &dp->link_train.lane_count);
	if (retval < 0)
		return retval;

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

	return 0;
}

static int s5p_dp_sw_link_training(struct s5p_dp_device *dp)
{
	int retval = 0;
	int training_finished = 0;

	dp->link_train.lt_state = START;

	/* Process here */
	while (!training_finished) {
		switch (dp->link_train.lt_state) {
		case START:
			retval = s5p_dp_link_start(dp);
			if (retval)
				dev_err(dp->dev, "LT Start failed\n");
			break;
		case CLOCK_RECOVERY:
			retval = s5p_dp_process_clock_recovery(dp);
			if (retval)
				dev_err(dp->dev, "LT CR failed\n");
			break;
		case EQUALIZER_TRAINING:
			retval = s5p_dp_process_equalizer_training(dp);
			if (retval)
				dev_err(dp->dev, "LT EQ failed\n");
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
	int retval;

	retval = s5p_dp_init_training(dp, count, bwtype);
	if (retval < 0)
		dev_err(dp->dev, "DP LT init failed!\n");

	retval = s5p_dp_sw_link_training(dp);
	if (retval < 0)
		dev_err(dp->dev, "DP LT failed!\n");

	return retval;
}

static int s5p_dp_config_video(struct s5p_dp_device *dp,
			struct video_info *video_info)
{
	int retval = 0;
	int timeout_loop = 0;
	int done_count = 0;

	s5p_dp_config_video_slave_mode(dp, video_info);

	s5p_dp_set_video_color_format(dp, video_info->color_depth,
			video_info->color_space,
			video_info->dynamic_range,
			video_info->ycbcr_coeff);

	if (s5p_dp_get_pll_lock_status(dp) == PLL_UNLOCKED) {
		dev_err(dp->dev, "PLL is not locked yet.\n");
		return -EINVAL;
	}

	for (;;) {
		timeout_loop++;
		if (s5p_dp_is_slave_video_stream_clock_on(dp) == 0)
			break;
		if (DP_TIMEOUT_LOOP_COUNT < timeout_loop) {
			dev_err(dp->dev, "Timeout of video streamclk ok\n");
			return -ETIMEDOUT;
		}

		usleep_range(1, 1);
	}

	/* Set to use the register calculated M/N video */
	s5p_dp_set_video_cr_mn(dp, CALCULATED_M, 0, 0);

	/* For video bist, Video timing must be generated by register */
	s5p_dp_set_video_timing_mode(dp, VIDEO_TIMING_FROM_CAPTURE);

	/* Disable video mute */
	s5p_dp_enable_video_mute(dp, 0);

	/* Configure video slave mode */
	s5p_dp_enable_video_master(dp, 0);

	/* Enable video */
	s5p_dp_start_video(dp);

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

		usleep_range(1000, 1000);
	}

	if (retval != 0)
		dev_err(dp->dev, "Video stream is not detected!\n");

	return retval;
}

static int s5p_dp_enable_scramble(struct s5p_dp_device *dp, bool enable)
{
	u8 data;
	int retval;

	if (enable) {
		s5p_dp_enable_scrambling(dp);

		retval = s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				&data);
		if (retval < 0)
			return retval;

		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				(u8)(data & ~DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	} else {
		s5p_dp_disable_scrambling(dp);

		retval = s5p_dp_read_byte_from_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				&data);
		if (retval < 0)
			return retval;

		retval = s5p_dp_write_byte_to_dpcd(dp,
				DPCD_ADDR_TRAINING_PATTERN_SET,
				(u8)(data | DPCD_SCRAMBLING_DISABLED));
		if (retval < 0)
			return retval;
	}

	return 0;
}

static irqreturn_t s5p_dp_irq_handler(int irq, void *arg)
{
	struct s5p_dp_device *dp = arg;

	dev_err(dp->dev, "s5p_dp_irq_handler\n");
	return IRQ_HANDLED;
}

static int s5p_dp_enable(struct s5p_dp_device *dp)
{
	int ret = 0;
	int retry = 0;
	struct s5p_dp_platdata *pdata = dp->dev->platform_data;

	mutex_lock(&dp->lock);

	if (dp->enabled)
		goto out;

	dp->enabled = 1;

	clk_enable(dp->clock);
	pm_runtime_get_sync(dp->dev);

dp_phy_init:

	if (pdata->phy_init)
		pdata->phy_init();

	s5p_dp_init_dp(dp);

	ret = s5p_dp_handle_edid(dp);
	if (ret) {
		dev_err(dp->dev, "unable to handle edid\n");
		goto out;
	}

	s5p_dp_disable_rx_zmux(dp);

	/* Non-enhance mode setting */
	ret = s5p_dp_enable_scramble(dp, 0);
	if (ret) {
		dev_err(dp->dev, "unable to set scramble\n");
		goto out;
	}

	ret = s5p_dp_enable_rx_to_enhanced_mode(dp, 0);
	if (ret) {
		dev_err(dp->dev, "unable to set enhanced mode\n");
		goto out;
	}
	s5p_dp_enable_enhanced_mode(dp, 0);

	/* Rx data disable */
	s5p_dp_rx_control(dp,0);

       /* Link Training */
	ret = s5p_dp_set_link_train(dp, dp->video_info->lane_count,
				dp->video_info->link_rate);
	if (ret) {
		dev_err(dp->dev, "unable to do link train\n");
		goto out;
	}

	/* Rx data enable */
	s5p_dp_rx_control(dp,1);

	s5p_dp_set_lane_count(dp, dp->video_info->lane_count);
	s5p_dp_set_link_bandwidth(dp, dp->video_info->link_rate);

	s5p_dp_init_video(dp);
	ret = s5p_dp_config_video(dp, dp->video_info);
	if (ret) {
		dev_err(dp->dev, "unable to config video\n");
		goto out;
	}

	if (pdata->backlight_on)
		pdata->backlight_on();

	mutex_unlock(&dp->lock);
	return 0;

out:
	if (pdata->phy_exit)
		pdata->phy_exit();

	if (retry < 3) {
		if (pdata->lcd_off)
			pdata->lcd_off();

		if (pdata->lcd_on)
			pdata->lcd_on();

		retry++;
		goto dp_phy_init;
	}
	dev_err(dp->dev, "DP LT exceeds max retry count");

	mutex_unlock(&dp->lock);
	return ret;
}

static void s5p_dp_disable(struct s5p_dp_device *dp)
{
	struct s5p_dp_platdata *pdata = dp->dev->platform_data;

	mutex_lock(&dp->lock);

	if (!dp->enabled)
		goto out;

	dp->enabled = 0;

	s5p_dp_reset(dp);
	s5p_dp_set_pll_power_down(dp, 1);
	s5p_dp_set_analog_power_down(dp, POWER_ALL, 1);

	if (pdata && pdata->phy_exit)
		pdata->phy_exit();

	clk_disable(dp->clock);
	pm_runtime_put_sync(dp->dev);

out:
	mutex_unlock(&dp->lock);
}

static int s5p_dp_set_power(struct lcd_device *lcd, int power)
{
	struct s5p_dp_device *dp = lcd_get_data(lcd);
	int retval;

	if (power == FB_BLANK_UNBLANK) {
		retval = s5p_dp_enable(dp);
		if (retval < 0)
			return retval;
	} else {
		s5p_dp_disable(dp);
	}

	return 0;
}

struct lcd_ops s5p_dp_lcd_ops = {
	.set_power = s5p_dp_set_power,
};

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

	mutex_init(&dp->lock);

	dp->dev = &pdev->dev;

	dp->clock = clk_get(&pdev->dev, "dp");
	if (IS_ERR(dp->clock)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(dp->clock);
		goto err_dp;
	}

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

	platform_set_drvdata(pdev, dp);

	dp->lcd = lcd_device_register("s5p_dp", &pdev->dev, dp, &s5p_dp_lcd_ops);
	if (IS_ERR(dp->lcd)) {
		ret = PTR_ERR(dp->lcd);
		goto err_irq;
	}

#if defined(CONFIG_MACH_ODROIDXU)    
	printk("--------------------------------------------\n");
	if(gpio_request(dp_hotplug.irq_gpio, "dp_hotplug"))	{
	    printk("%s : DP Hotplug GPIO request error!\n", __func__);
	}
	else	{
	    gpio_direction_input(dp_hotplug.irq_gpio);
	    dp_hotplug.gpio_status = gpio_get_value(dp_hotplug.irq_gpio);
	    printk("%s : dp_hotplug.gpio_status = %d\n", __func__, dp_hotplug.gpio_status);
		gpio_free(dp_hotplug.irq_gpio);
	}

    dp_hotplug.irq = gpio_to_irq(dp_hotplug.irq_gpio);

	if(request_irq(dp_hotplug.irq, dp_hotplug_irq_handler, dp_hotplug.irq_flags, "dp_hotplug_irq", (void *)&dp_hotplug))	{
		printk("%s : irq %d request fail!\n", __func__, dp_hotplug.irq);
	}

	INIT_DELAYED_WORK_DEFERRABLE(&dp_hotplug.work, dp_hotplug_work);
	schedule_delayed_work(&dp_hotplug.work, msecs_to_jiffies(500));
	
	dp_hotplug.dp = dp;
	
	printk("%s : dp init status = %d\n", __func__, dp_hotplug.init_status);
	printk("--------------------------------------------\n");
#else
    
	ret = s5p_dp_enable(dp);
	if (ret)
		goto err_fb;
#endif		

	return 0;

#if !defined(CONFIG_MACH_ODROIDXU)    
err_fb:
	lcd_device_unregister(dp->lcd);
#endif
	
err_irq:
	free_irq(dp->irq, dp);
err_ioremap:
	iounmap(dp->reg_base);
err_req_region:
	release_mem_region(res->start, resource_size(res));
err_clock:
	clk_put(dp->clock);
err_dp:
	mutex_destroy(&dp->lock);
	kfree(dp);

	return ret;
}

static int __devexit s5p_dp_remove(struct platform_device *pdev)
{
	struct s5p_dp_device *dp = platform_get_drvdata(pdev);

	free_irq(dp->irq, dp);

	lcd_device_unregister(dp->lcd);

	s5p_dp_disable(dp);

	iounmap(dp->reg_base);
	clk_put(dp->clock);

	release_mem_region(dp->res->start, resource_size(dp->res));

	pm_runtime_disable(dp->dev);

	kfree(dp);

	return 0;
}

static void  s5p_dp_shutdown(struct platform_device *pdev)
{
	struct s5p_dp_device *dp = platform_get_drvdata(pdev);
	struct s5p_dp_platdata *pdata = dp->dev->platform_data;

	lcd_device_unregister(dp->lcd);

	if (pdata->backlight_off)
		pdata->backlight_off();

	if (pdata->lcd_off)
		pdata->lcd_off();

	s5p_dp_disable(dp);

	free_irq(dp->irq, dp);
	iounmap(dp->reg_base);
	clk_put(dp->clock);

	release_mem_region(dp->res->start, resource_size(dp->res));

	pm_runtime_disable(dp->dev);

	kfree(dp);
}

static struct platform_driver s5p_dp_driver = {
	.probe		= s5p_dp_probe,
	.remove		= __devexit_p(s5p_dp_remove),
	.shutdown       = s5p_dp_shutdown,
	.driver		= {
		.name	= "s5p-dp",
		.owner	= THIS_MODULE,
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
MODULE_DESCRIPTION("Samsung SoC DP Driver");
MODULE_LICENSE("GPL");
