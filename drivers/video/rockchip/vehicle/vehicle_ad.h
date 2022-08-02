/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_AD_COMMON_H
#define __VEHICLE_AD_COMMON_H
#include <linux/i2c.h>
#include "vehicle_cfg.h"
#include <linux/rk-camera-module.h>

enum vehicle_ad_fix_format {
	AD_FIX_FORMAT_AUTO_DETECT = 0,
	AD_FIX_FORMAT_PAL = 1,
	AD_FIX_FORMAT_NTSC = 2,
	AD_FIX_FORMAT_720P_50FPS = 3,
	AD_FIX_FORMAT_720P_30FPS = 4,
	AD_FIX_FORMAT_720P_25FPS = 5,
	AD_FIX_FORMAT_1080P_30FPS = 6,
	AD_FIX_FORMAT_1080P_25FPS = 7,
};

struct vehicle_camera_device_defrect {
	unsigned int width;
	unsigned int height;
	unsigned int crop_x;
	unsigned int crop_y;
	unsigned int crop_width;
	unsigned int crop_height;
	const char *interface;
};

struct vehicle_state_check_work {
	struct workqueue_struct *state_check_wq;
	struct delayed_work work;
};

struct vehicle_ad_dev {
	struct device *dev;
	struct i2c_adapter *adapter;
	const char *ad_name;
	int resolution;
	int mclk_rate;
	int ad_chl;
	int i2c_chl;
	int i2c_add;
//	int i2c_rate;
	int powerdown;
	int pwdn_active;
	int power;
	int pwr_active;
	int reset;
	int rst_active;
	int cvstd;
	int cvstd_irq_flag;
	int irq;
	int fix_format;
	struct vehicle_camera_device_defrect defrects[4];
	struct vehicle_state_check_work	state_check_work;
	struct vehicle_cfg cfg;
	int cif_error_last_line;
	u32 channel_reso[PAD_MAX];
	u8 detect_status;
	u8 last_detect_status;
};

int vehicle_generic_sensor_write(struct vehicle_ad_dev *ad, char reg, char *pval);
int vehicle_sensor_write(struct vehicle_ad_dev *ad, u8 reg, u8 val);
int vehicle_generic_sensor_read(struct vehicle_ad_dev *ad, char reg);
int vehicle_sensor_read(struct vehicle_ad_dev *ad, u8 reg, u8 *val);
int vehicle_parse_sensor(struct vehicle_ad_dev *ad);
void vehicle_ad_channel_set(struct vehicle_ad_dev *ad, int channel);

int vehicle_ad_init(struct vehicle_ad_dev *ad);
int vehicle_ad_deinit(void);
int vehicle_ad_stream(struct vehicle_ad_dev *ad, int val);
struct vehicle_cfg *vehicle_ad_get_vehicle_cfg(void);
void vehicle_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line);
int vehicle_to_v4l2_drv_init(void);

#endif
