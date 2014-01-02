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

#ifndef _EXYNOS_DP_CORE_H
#define _EXYNOS_DP_CORE_H

#define DP_TIMEOUT_LOOP_COUNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 5

enum link_rate_type {
	LINK_RATE_1_62GBPS = 0x06,
	LINK_RATE_2_70GBPS = 0x0a
};

enum link_lane_count_type {
	LANE_COUNT1 = 1,
	LANE_COUNT2 = 2,
	LANE_COUNT4 = 4
};

enum link_training_state {
	START,
	CLOCK_RECOVERY,
	EQUALIZER_TRAINING,
	FINISHED,
	FAILED
};

enum voltage_swing_level {
	VOLTAGE_LEVEL_0,
	VOLTAGE_LEVEL_1,
	VOLTAGE_LEVEL_2,
	VOLTAGE_LEVEL_3,
};

enum pre_emphasis_level {
	PRE_EMPHASIS_LEVEL_0,
	PRE_EMPHASIS_LEVEL_1,
	PRE_EMPHASIS_LEVEL_2,
	PRE_EMPHASIS_LEVEL_3,
};

enum pattern_set {
	PRBS7,
	D10_2,
	TRAINING_PTN1,
	TRAINING_PTN2,
	DP_NONE
};

enum color_space {
	COLOR_RGB,
	COLOR_YCBCR422,
	COLOR_YCBCR444
};

enum color_depth {
	COLOR_6,
	COLOR_8,
	COLOR_10,
	COLOR_12
};

enum color_coefficient {
	COLOR_YCBCR601,
	COLOR_YCBCR709
};

enum dynamic_range {
	VESA,
	CEA
};

enum pll_status {
	PLL_UNLOCKED,
	PLL_LOCKED
};

enum clock_recovery_m_value_type {
	CALCULATED_M,
	REGISTER_M
};

enum video_timing_recognition_type {
	VIDEO_TIMING_FROM_CAPTURE,
	VIDEO_TIMING_FROM_REGISTER
};

enum analog_power_block {
	AUX_BLOCK,
	CH0_BLOCK,
	CH1_BLOCK,
	CH2_BLOCK,
	CH3_BLOCK,
	ANALOG_TOTAL,
	POWER_ALL
};

enum dp_irq_type {
	DP_IRQ_TYPE_HP_CABLE_IN,
	DP_IRQ_TYPE_HP_CABLE_OUT,
	DP_IRQ_TYPE_HP_CHANGE,
	DP_IRQ_TYPE_UNKNOWN,
};

struct video_info {
	char *name;

	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;

	enum color_space color_space;
	enum dynamic_range dynamic_range;
	enum color_coefficient ycbcr_coeff;
	enum color_depth color_depth;

	enum link_rate_type link_rate;
	enum link_lane_count_type lane_count;
};

struct link_train {
	int eq_loop;
	int cr_loop[4];

	u8 link_rate;
	u8 lane_count;
	u8 training_lane[4];

	enum link_training_state lt_state;
};

struct exynos_dp_device {
	struct device		*dev;
	struct clk		*clock;
	unsigned int		irq;
	void __iomem		*reg_base;
	void __iomem		*phy_addr;
	unsigned int		enable_mask;

	struct video_info	*video_info;
	struct link_train	link_train;
	struct work_struct	hotplug_work;
	struct phy		*phy;
};

/* exynos_dp_reg.c */
void exynos_dp_enable_video_mute(struct exynos_dp_device *dp, bool enable);
void exynos_dp_stop_video(struct exynos_dp_device *dp);
void exynos_dp_lane_swap(struct exynos_dp_device *dp, bool enable);
void exynos_dp_init_analog_param(struct exynos_dp_device *dp);
void exynos_dp_init_interrupt(struct exynos_dp_device *dp);
void exynos_dp_reset(struct exynos_dp_device *dp);
void exynos_dp_swreset(struct exynos_dp_device *dp);
void exynos_dp_config_interrupt(struct exynos_dp_device *dp);
enum pll_status exynos_dp_get_pll_lock_status(struct exynos_dp_device *dp);
void exynos_dp_set_pll_power_down(struct exynos_dp_device *dp, bool enable);
void exynos_dp_set_analog_power_down(struct exynos_dp_device *dp,
				enum analog_power_block block,
				bool enable);
void exynos_dp_init_analog_func(struct exynos_dp_device *dp);
void exynos_dp_init_hpd(struct exynos_dp_device *dp);
enum dp_irq_type exynos_dp_get_irq_type(struct exynos_dp_device *dp);
void exynos_dp_clear_hotplug_interrupts(struct exynos_dp_device *dp);
void exynos_dp_reset_aux(struct exynos_dp_device *dp);
void exynos_dp_init_aux(struct exynos_dp_device *dp);
int exynos_dp_get_plug_in_status(struct exynos_dp_device *dp);
void exynos_dp_enable_sw_function(struct exynos_dp_device *dp);
int exynos_dp_start_aux_transaction(struct exynos_dp_device *dp);
int exynos_dp_write_byte_to_dpcd(struct exynos_dp_device *dp,
				unsigned int reg_addr,
				unsigned char data);
int exynos_dp_read_byte_from_dpcd(struct exynos_dp_device *dp,
				unsigned int reg_addr,
				unsigned char *data);
int exynos_dp_write_bytes_to_dpcd(struct exynos_dp_device *dp,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char data[]);
int exynos_dp_read_bytes_from_dpcd(struct exynos_dp_device *dp,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char data[]);
int exynos_dp_select_i2c_device(struct exynos_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr);
int exynos_dp_read_byte_from_i2c(struct exynos_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr,
				unsigned int *data);
int exynos_dp_read_bytes_from_i2c(struct exynos_dp_device *dp,
				unsigned int device_addr,
				unsigned int reg_addr,
				unsigned int count,
				unsigned char edid[]);
void exynos_dp_set_link_bandwidth(struct exynos_dp_device *dp, u32 bwtype);
void exynos_dp_get_link_bandwidth(struct exynos_dp_device *dp, u32 *bwtype);
void exynos_dp_set_lane_count(struct exynos_dp_device *dp, u32 count);
void exynos_dp_get_lane_count(struct exynos_dp_device *dp, u32 *count);
void exynos_dp_enable_enhanced_mode(struct exynos_dp_device *dp, bool enable);
void exynos_dp_set_training_pattern(struct exynos_dp_device *dp,
				 enum pattern_set pattern);
void exynos_dp_set_lane0_pre_emphasis(struct exynos_dp_device *dp, u32 level);
void exynos_dp_set_lane1_pre_emphasis(struct exynos_dp_device *dp, u32 level);
void exynos_dp_set_lane2_pre_emphasis(struct exynos_dp_device *dp, u32 level);
void exynos_dp_set_lane3_pre_emphasis(struct exynos_dp_device *dp, u32 level);
void exynos_dp_set_lane0_link_training(struct exynos_dp_device *dp,
				u32 training_lane);
void exynos_dp_set_lane1_link_training(struct exynos_dp_device *dp,
				u32 training_lane);
void exynos_dp_set_lane2_link_training(struct exynos_dp_device *dp,
				u32 training_lane);
void exynos_dp_set_lane3_link_training(struct exynos_dp_device *dp,
				u32 training_lane);
u32 exynos_dp_get_lane0_link_training(struct exynos_dp_device *dp);
u32 exynos_dp_get_lane1_link_training(struct exynos_dp_device *dp);
u32 exynos_dp_get_lane2_link_training(struct exynos_dp_device *dp);
u32 exynos_dp_get_lane3_link_training(struct exynos_dp_device *dp);
void exynos_dp_reset_macro(struct exynos_dp_device *dp);
void exynos_dp_init_video(struct exynos_dp_device *dp);

void exynos_dp_set_video_color_format(struct exynos_dp_device *dp);
int exynos_dp_is_slave_video_stream_clock_on(struct exynos_dp_device *dp);
void exynos_dp_set_video_cr_mn(struct exynos_dp_device *dp,
			enum clock_recovery_m_value_type type,
			u32 m_value,
			u32 n_value);
void exynos_dp_set_video_timing_mode(struct exynos_dp_device *dp, u32 type);
void exynos_dp_enable_video_master(struct exynos_dp_device *dp, bool enable);
void exynos_dp_start_video(struct exynos_dp_device *dp);
int exynos_dp_is_video_stream_on(struct exynos_dp_device *dp);
void exynos_dp_config_video_slave_mode(struct exynos_dp_device *dp);
void exynos_dp_enable_scrambling(struct exynos_dp_device *dp);
void exynos_dp_disable_scrambling(struct exynos_dp_device *dp);

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
#define DPCD_ADDR_LANE0_1_STATUS		0x0202
#define DPCD_ADDR_LANE_ALIGN_STATUS_UPDATED	0x0204
#define DPCD_ADDR_ADJUST_REQUEST_LANE0_1	0x0206
#define DPCD_ADDR_ADJUST_REQUEST_LANE2_3	0x0207
#define DPCD_ADDR_TEST_REQUEST			0x0218
#define DPCD_ADDR_TEST_RESPONSE			0x0260
#define DPCD_ADDR_TEST_EDID_CHECKSUM		0x0261
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

#endif /* _EXYNOS_DP_CORE_H */
