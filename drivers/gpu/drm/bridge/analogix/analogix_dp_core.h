/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for Analogix DP (Display Port) core interface driver.
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#ifndef _ANALOGIX_DP_CORE_H
#define _ANALOGIX_DP_CORE_H

#include <drm/drm_crtc.h>
#include <drm/drm_bridge.h>
#include <drm/drm_dp_helper.h>

#define DP_TIMEOUT_LOOP_COUNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 5
#define MAX_PLL_LOCK_LOOP 5

/* Training takes 22ms if AUX channel comm fails. Use this as retry interval */
#define DP_TIMEOUT_TRAINING_US			22000
#define DP_TIMEOUT_PSR_LOOP_MS			300

/* DP_MAX_LANE_COUNT */
#define DPCD_ENHANCED_FRAME_CAP(x)		(((x) >> 7) & 0x1)
#define DPCD_MAX_LANE_COUNT(x)			((x) & 0x1f)

/* DP_LANE_COUNT_SET */
#define DPCD_LANE_COUNT_SET(x)			((x) & 0x1f)

/* DP_TRAINING_LANE0_SET */
#define DPCD_PRE_EMPHASIS_SET(x)		(((x) & 0x3) << 3)
#define DPCD_PRE_EMPHASIS_GET(x)		(((x) >> 3) & 0x3)
#define DPCD_VOLTAGE_SWING_SET(x)		(((x) & 0x3) << 0)
#define DPCD_VOLTAGE_SWING_GET(x)		(((x) >> 0) & 0x3)

struct gpio_desc;

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
	TRAINING_PTN3,
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

struct video_info {
	char *name;
	struct drm_display_mode mode;

	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;

	enum color_space color_space;
	enum dynamic_range dynamic_range;
	enum color_coefficient ycbcr_coeff;
	enum color_depth color_depth;

	int max_link_rate;
	enum link_lane_count_type max_lane_count;
	u32 lane_map[4];

	bool video_bist_enable;
};

struct link_train {
	int eq_loop;
	int cr_loop[4];

	u8 link_rate;
	u8 lane_count;
	u8 training_lane[4];
	bool ssc;
	bool enhanced_framing;

	enum link_training_state lt_state;
};

struct analogix_dp_device {
	struct drm_encoder	*encoder;
	struct device		*dev;
	struct drm_device	*drm_dev;
	struct drm_connector	connector;
	struct drm_bridge	bridge;
	struct drm_dp_aux       aux;
	struct clk_bulk_data	*clks;
	int			nr_clks;
	unsigned int		irq;
	void __iomem		*reg_base;

	struct video_info	video_info;
	struct link_train	link_train;
	struct phy		*phy;
	int			dpms_mode;
	struct gpio_desc	*hpd_gpiod;
	bool                    force_hpd;
	bool			fast_train_enable;
	bool			psr_supported;
	struct work_struct	modeset_retry_work;

	struct mutex		panel_lock;
	bool			panel_is_prepared;

	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	struct analogix_dp_plat_data *plat_data;
	struct extcon_dev *extcon;
};

/* analogix_dp_reg.c */
void analogix_dp_enable_video_mute(struct analogix_dp_device *dp, bool enable);
void analogix_dp_stop_video(struct analogix_dp_device *dp);
void analogix_dp_init_analog_param(struct analogix_dp_device *dp);
void analogix_dp_init_interrupt(struct analogix_dp_device *dp);
void analogix_dp_reset(struct analogix_dp_device *dp);
void analogix_dp_swreset(struct analogix_dp_device *dp);
void analogix_dp_config_interrupt(struct analogix_dp_device *dp);
void analogix_dp_mute_hpd_interrupt(struct analogix_dp_device *dp);
void analogix_dp_unmute_hpd_interrupt(struct analogix_dp_device *dp);
enum pll_status analogix_dp_get_pll_lock_status(struct analogix_dp_device *dp);
void analogix_dp_set_pll_power_down(struct analogix_dp_device *dp, bool enable);
void analogix_dp_set_analog_power_down(struct analogix_dp_device *dp,
				       enum analog_power_block block,
				       bool enable);
int analogix_dp_init_analog_func(struct analogix_dp_device *dp);
void analogix_dp_init_hpd(struct analogix_dp_device *dp);
void analogix_dp_force_hpd(struct analogix_dp_device *dp);
void analogix_dp_clear_hotplug_interrupts(struct analogix_dp_device *dp);
void analogix_dp_reset_aux(struct analogix_dp_device *dp);
void analogix_dp_init_aux(struct analogix_dp_device *dp);
int analogix_dp_get_plug_in_status(struct analogix_dp_device *dp);
void analogix_dp_enable_sw_function(struct analogix_dp_device *dp);
void analogix_dp_set_link_bandwidth(struct analogix_dp_device *dp, u32 bwtype);
void analogix_dp_get_link_bandwidth(struct analogix_dp_device *dp, u32 *bwtype);
void analogix_dp_set_lane_count(struct analogix_dp_device *dp, u32 count);
void analogix_dp_get_lane_count(struct analogix_dp_device *dp, u32 *count);
void analogix_dp_enable_enhanced_mode(struct analogix_dp_device *dp,
				      bool enable);
bool analogix_dp_get_enhanced_mode(struct analogix_dp_device *dp);
void analogix_dp_set_training_pattern(struct analogix_dp_device *dp,
				      enum pattern_set pattern);
void analogix_dp_set_lane_link_training(struct analogix_dp_device *dp);
u32 analogix_dp_get_lane_link_training(struct analogix_dp_device *dp, u8 lane);
void analogix_dp_reset_macro(struct analogix_dp_device *dp);
void analogix_dp_init_video(struct analogix_dp_device *dp);

void analogix_dp_set_video_color_format(struct analogix_dp_device *dp);
int analogix_dp_is_slave_video_stream_clock_on(struct analogix_dp_device *dp);
void analogix_dp_set_video_cr_mn(struct analogix_dp_device *dp,
				 enum clock_recovery_m_value_type type,
				 u32 m_value,
				 u32 n_value);
void analogix_dp_set_video_timing_mode(struct analogix_dp_device *dp, u32 type);
void analogix_dp_enable_video_master(struct analogix_dp_device *dp,
				     bool enable);
void analogix_dp_start_video(struct analogix_dp_device *dp);
int analogix_dp_is_video_stream_on(struct analogix_dp_device *dp);
void analogix_dp_config_video_slave_mode(struct analogix_dp_device *dp);
void analogix_dp_enable_scrambling(struct analogix_dp_device *dp);
void analogix_dp_disable_scrambling(struct analogix_dp_device *dp);
void analogix_dp_enable_psr_crc(struct analogix_dp_device *dp);
int analogix_dp_send_psr_spd(struct analogix_dp_device *dp,
			     struct dp_sdp *vsc, bool blocking);
ssize_t analogix_dp_transfer(struct analogix_dp_device *dp,
			     struct drm_dp_aux_msg *msg);
void analogix_dp_set_video_format(struct analogix_dp_device *dp);
void analogix_dp_video_bist_enable(struct analogix_dp_device *dp);
bool analogix_dp_ssc_supported(struct analogix_dp_device *dp);
int analogix_dp_phy_power_on(struct analogix_dp_device *dp);
void analogix_dp_phy_power_off(struct analogix_dp_device *dp);
void analogix_dp_audio_config_spdif(struct analogix_dp_device *dp);
void analogix_dp_audio_config_i2s(struct analogix_dp_device *dp);
void analogix_dp_audio_enable(struct analogix_dp_device *dp);
void analogix_dp_audio_disable(struct analogix_dp_device *dp);
void analogix_dp_init(struct analogix_dp_device *dp);
void analogix_dp_irq_handler(struct analogix_dp_device *dp);

#endif /* _ANALOGIX_DP_CORE_H */
