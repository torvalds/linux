/*
 * Header file for Samsung DP (Display Port) interface driver.
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _S5P_DP_CORE_H
#define _S5P_DP_CORE_H

#include <linux/lcd.h>

struct link_train {
	int eq_loop;
	int cr_loop[4];

	u8 link_rate;
	u8 lane_count;
	u8 training_lane[4];

	enum link_training_state lt_state;
};

struct s5p_dp_device {
	struct device		*dev;
	struct resource		*res;
	struct clk		*clock;
	unsigned int		irq;
	void __iomem		*reg_base;
	int 			enabled;
	bool			user_disabled;
	struct mutex		lock;

	struct video_info	*video_info;
	struct link_train	link_train;

	struct lcd_device	*lcd;
};

/* s5p_dp_reg.c */
void s5p_dp_enable_video_mute(struct s5p_dp_device *dp, bool enable);
void s5p_dp_stop_video(struct s5p_dp_device *dp);
void s5p_dp_lane_swap(struct s5p_dp_device *dp, bool enable);
void s5p_dp_init_analog_param(struct s5p_dp_device *dp);
void s5p_dp_init_interrupt(struct s5p_dp_device *dp);
void s5p_dp_reset(struct s5p_dp_device *dp);
void s5p_dp_config_interrupt(struct s5p_dp_device *dp);
u32 s5p_dp_get_pll_lock_status(struct s5p_dp_device *dp);
void s5p_dp_set_pll_power_down(struct s5p_dp_device *dp, bool enable);
void s5p_dp_set_analog_power_down(struct s5p_dp_device *dp,
				enum analog_power_block block,
				bool enable);
void s5p_dp_init_analog_func(struct s5p_dp_device *dp);
void s5p_dp_init_hpd(struct s5p_dp_device *dp);
void s5p_dp_reset_aux(struct s5p_dp_device *dp);
void s5p_dp_init_aux(struct s5p_dp_device *dp);
int s5p_dp_get_plug_in_status(struct s5p_dp_device *dp);
void s5p_dp_enable_sw_function(struct s5p_dp_device *dp);
int s5p_dp_start_aux_transaction(struct s5p_dp_device *dp);
int s5p_dp_write_byte_to_dpcd(struct s5p_dp_device *dp,
				unsigned int reg_addr,
				unsigned char data);
int s5p_dp_read_byte_from_dpcd(struct s5p_dp_device *dp,
				unsigned int reg_addr,
				unsigned char *data);
int s5p_dp_write_bytes_to_dpcd(struct s5p_dp_device *dp,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char data[]);
int s5p_dp_read_bytes_from_dpcd(struct s5p_dp_device *dp,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char data[]);
int s5p_dp_select_i2c_device(struct s5p_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr);
int s5p_dp_read_byte_from_i2c(struct s5p_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr,
				unsigned int *data);
int s5p_dp_read_bytes_from_i2c(struct s5p_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char edid[]);
void s5p_dp_set_link_bandwidth(struct s5p_dp_device *dp, u32 bwtype);
void s5p_dp_get_link_bandwidth(struct s5p_dp_device *dp, u32 *bwtype);
void s5p_dp_set_lane_count(struct s5p_dp_device *dp, u32 count);
void s5p_dp_get_lane_count(struct s5p_dp_device *dp, u32 *count);
void s5p_dp_enable_enhanced_mode(struct s5p_dp_device *dp, bool enable);
void s5p_dp_set_training_pattern(struct s5p_dp_device *dp,
				 enum pattern_set pattern);
void s5p_dp_set_lane0_pre_emphasis(struct s5p_dp_device *dp, u32 level);
void s5p_dp_set_lane1_pre_emphasis(struct s5p_dp_device *dp, u32 level);
void s5p_dp_set_lane2_pre_emphasis(struct s5p_dp_device *dp, u32 level);
void s5p_dp_set_lane3_pre_emphasis(struct s5p_dp_device *dp, u32 level);
void s5p_dp_set_lane0_link_training(struct s5p_dp_device *dp,
				u32 training_lane);
void s5p_dp_set_lane1_link_training(struct s5p_dp_device *dp,
				u32 training_lane);
void s5p_dp_set_lane2_link_training(struct s5p_dp_device *dp,
				u32 training_lane);
void s5p_dp_set_lane3_link_training(struct s5p_dp_device *dp,
				u32 training_lane);
u32 s5p_dp_get_lane0_link_training(struct s5p_dp_device *dp);
u32 s5p_dp_get_lane1_link_training(struct s5p_dp_device *dp);
u32 s5p_dp_get_lane2_link_training(struct s5p_dp_device *dp);
u32 s5p_dp_get_lane3_link_training(struct s5p_dp_device *dp);
void s5p_dp_reset_macro(struct s5p_dp_device *dp);
int s5p_dp_init_video(struct s5p_dp_device *dp);

void s5p_dp_set_video_color_format(struct s5p_dp_device *dp,
				u32 color_depth,
				u32 color_space,
				u32 dynamic_range,
				u32 coeff);
int s5p_dp_is_slave_video_stream_clock_on(struct s5p_dp_device *dp);
void s5p_dp_set_video_cr_mn(struct s5p_dp_device *dp,
			enum clock_recovery_m_value_type type,
			u32 m_value,
			u32 n_value);
void s5p_dp_set_video_timing_mode(struct s5p_dp_device *dp, u32 type);
void s5p_dp_enable_video_master(struct s5p_dp_device *dp, bool enable);
void s5p_dp_start_video(struct s5p_dp_device *dp);
int s5p_dp_is_video_stream_on(struct s5p_dp_device *dp);
void s5p_dp_config_video_slave_mode(struct s5p_dp_device *dp,
			struct video_info *video_info);
void s5p_dp_enable_scrambling(struct s5p_dp_device *dp);
void s5p_dp_disable_scrambling(struct s5p_dp_device *dp);
void s5p_dp_rx_control(struct s5p_dp_device *dp, bool enable);

/* I2C EDID Chip ID, Slave Address */
#define I2C_EDID_DEVICE_ADDR			0x50
#define I2C_E_EDID_DEVICE_ADDR			0x30

#define EDID_BLOCK_LENGTH			0x80
#define EDID_HEADER_PATTERN			0x00
#define EDID_EXTENSION_FLAG			0x7e
#define EDID_CHECKSUM				0x7f

/* Definition for DPCD Register */
#define DPCD_ADDR_DPCD_REV			0x0000
#define DPCD_ADDR_MAX_LINK_RATE			0x0001
#define DPCD_ADDR_MAX_LANE_COUNT		0x0002
#define DPCD_ADDR_LINK_BW_SET			0x0100
#define DPCD_ADDR_LANE_COUNT_SET		0x0101
#define DPCD_ADDR_TRAINING_PATTERN_SET		0x0102
#define DPCD_ADDR_TRAINING_LANE0_SET		0x0103
#define DPCD_ADDR_CONFIGURATION_SET		0x010a
#define DPCD_ADDR_LANE0_1_STATUS		0x0202
#define DPCD_ADDR_LANE_ALIGN_STATUS_UPDATED	0x0204
#define DPCD_ADDR_ADJUST_REQUEST_LANE0_1	0x0206
#define DPCD_ADDR_ADJUST_REQUEST_LANE2_3	0x0207
#define DPCD_ADDR_TEST_REQUEST			0x0218
#define DPCD_ADDR_TEST_RESPONSE			0x0260
#define DPCD_ADDR_TEST_EDID_CHECKSUM		0x0261
#define DPCD_ADDR_USER_DEFINED1			0x0491
#define DPCD_ADDR_USER_DEFINED2			0x0492
#define DPCD_ADDR_USER_DEFINED3			0x0493
#define DPCD_ADDR_SINK_POWER_STATE		0x0600

/* DPCD_ADDR_MAX_LANE_COUNT */
#define DPCD_ENHANCED_FRAME_CAP(x)		(((x) >> 7) & 0x1)
#define DPCD_MAX_LANE_COUNT(x)			((x) & 0x1f)

/* DPCD_ADDR_LANE_COUNT_SET */
#define DPCD_ENHANCED_FRAME_EN			(0x1 << 7)
#define DPCD_LANE_COUNT_SET(x)			((x) & 0x1f)

/* DPCD_ADDR_TRAINING_PATTERN_SET */
#define DPCD_SCRAMBLING_DISABLED		(0x1 << 5)
#define DPCD_SCRAMBLING_ENABLED			(0x0 << 5)
#define DPCD_TRAINING_PATTERN_2			(0x2 << 0)
#define DPCD_TRAINING_PATTERN_1			(0x1 << 0)
#define DPCD_TRAINING_PATTERN_DISABLED		(0x0 << 0)

/* DPCD_ADDR_TRAINING_LANE0_SET */
#define DPCD_MAX_PRE_EMPHASIS_REACHED		(0x1 << 5)
#define DPCD_PRE_EMPHASIS_SET(x)		(((x) & 0x3) << 3)
#define DPCD_PRE_EMPHASIS_GET(x)		(((x) >> 3) & 0x3)
#define DPCD_PRE_EMPHASIS_PATTERN2_LEVEL0	(0x0 << 3)
#define DPCD_MAX_SWING_REACHED			(0x1 << 2)
#define DPCD_VOLTAGE_SWING_SET(x)		(((x) & 0x3) << 0)
#define DPCD_VOLTAGE_SWING_GET(x)		(((x) >> 0) & 0x3)
#define DPCD_VOLTAGE_SWING_PATTERN1_LEVEL0	(0x0 << 0)

/* DPCD_ADDR_LANE0_1_STATUS */
#define DPCD_LANE_SYMBOL_LOCKED			(0x1 << 2)
#define DPCD_LANE_CHANNEL_EQ_DONE		(0x1 << 1)
#define DPCD_LANE_CR_DONE			(0x1 << 0)
#define DPCD_CHANNEL_EQ_BITS			(DPCD_LANE_CR_DONE|	\
						 DPCD_LANE_CHANNEL_EQ_DONE|\
						 DPCD_LANE_SYMBOL_LOCKED)

/* DPCD_ADDR_LANE_ALIGN__STATUS_UPDATED */
#define DPCD_LINK_STATUS_UPDATED		(0x1 << 7)
#define DPCD_DOWNSTREAM_PORT_STATUS_CHANGED	(0x1 << 6)
#define DPCD_INTERLANE_ALIGN_DONE		(0x1 << 0)

/* DPCD_ADDR_TEST_REQUEST */
#define DPCD_TEST_EDID_READ			(0x1 << 2)

/* DPCD_ADDR_TEST_RESPONSE */
#define DPCD_TEST_EDID_CHECKSUM_WRITE		(0x1 << 2)

/* DPCD_ADDR_SINK_POWER_STATE */
#define DPCD_SET_POWER_STATE_D0			(0x1 << 0)
#define DPCD_SET_POWER_STATE_D4			(0x2 << 0)

#endif /* _S5P_DP_CORE_H */
