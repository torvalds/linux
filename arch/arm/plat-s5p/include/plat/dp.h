/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * Samsung S5P series DP device support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PLAT_S5P_DP_H_
#define PLAT_S5P_DP_H_ __FILE__

#define DP_TIMEOUT_LOOP_COUNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 4

enum link_rate_type {
	LINK_RATE_1_62GBPS = 0x06,
	LINK_RATE_2_70GBPS = 0x0a
};

enum link_lane_count_type {
	LANE_COUNT1 = 1,
	LANE_COUNT2 = 2,
	LANE_COUNT4 = 4
};

/* link training state machine */
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

enum refresh_denominator_type {
	REFRESH_DENOMINATOR_1 = 0x0,
	REFRESH_DENOMINATOR_1P001 = 0x1
};

enum pattern_type {
	NO_PATTERN,
	COLOR_RAMP,
	BALCK_WHITE_V_LINES,
	COLOR_SQUARE,
	INVALID_PATTERN,
	COLORBAR_32,
	COLORBAR_64,
	WHITE_GRAY_BALCKBAR_32,
	WHITE_GRAY_BALCKBAR_64,
	MOBILE_WHITEBAR_32,
	MOBILE_WHITEBAR_64
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

struct video_info {
	char *name;

	u32 h_total;
	u32 h_active;
	u32 h_sync_width;
	u32 h_back_porch;
	u32 h_front_porch;

	u32 v_total;
	u32 v_active;
	u32 v_sync_width;
	u32 v_back_porch;
	u32 v_front_porch;

	u32 v_sync_rate;

	u32 mvid;
	u32 nvid;

	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;

	enum color_space color_space;
	enum dynamic_range dynamic_range;
	enum color_coefficient ycbcr_coeff;
	enum color_depth color_depth;

	bool sync_clock;
	bool even_field;

	enum refresh_denominator_type refresh_denominator;

	enum pattern_type test_pattern;
	enum link_rate_type link_rate;
	enum link_lane_count_type lane_count;

	bool video_mute_on;

	bool master_mode;
	bool bist_mode;
};

struct s5p_dp_platdata {
	struct video_info *video_info;

	void (*phy_init)(void);
	void (*phy_exit)(void);
	void (*backlight_on)(void);
	void (*backlight_off)(void);
};

extern void s5p_dp_set_platdata(struct s5p_dp_platdata *pd);
extern void s5p_dp_phy_init(void);
extern void s5p_dp_phy_exit(void);

#endif /* PLAT_S5P_DP_H_ */
