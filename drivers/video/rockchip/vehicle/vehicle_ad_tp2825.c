// SPDX-License-Identifier: GPL-2.0
/*
 * vehicle sensor tp2825
 *
 * Copyright (C) 2020 Rockchip Electronics Co.Ltd
 * Authors:
 *      Zhiqin Wei <wzq@rock-chips.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sysctl.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include "vehicle_cfg.h"
#include "vehicle_main.h"
#include "vehicle_ad.h"
#include "vehicle_ad_tp2825.h"

enum {
	CVSTD_720P60 = 0,
	CVSTD_720P50,
	CVSTD_1080P30,
	CVSTD_1080P25,
	CVSTD_720P30,
	CVSTD_720P25,
	CVSTD_SD,
	CVSTD_NTSC,
	CVSTD_PAL
};

enum {
	FORCE_PAL_WIDTH = 960,
	FORCE_PAL_HEIGHT = 576,
	FORCE_NTSC_WIDTH = 960,
	FORCE_NTSC_HEIGHT = 480,
	FORCE_CIF_OUTPUT_FORMAT = CIF_OUTPUT_FORMAT_420,
};

enum {
	VIDEO_UNPLUG,
	VIDEO_IN,
	VIDEO_LOCKED,
	VIDEO_UNLOCK
};
#define FLAG_LOSS				(0x1 << 7)
#define FLAG_V_LOCKED			(0x1 << 6)
#define FLAG_H_LOCKED			(0x1 << 5)
#define FLAG_CARRIER_PLL_LOCKED	(0x1 << 4)
#define FLAG_VIDEO_DETECTED		(0x1 << 3)
#define FLAG_EQ_SD_DETECTED		(0x1 << 2)
#define FLAG_PROGRESSIVE		(0x1 << 1)
#define FLAG_NO_CARRIER			(0x1 << 0)
#define FLAG_LOCKED		(FLAG_V_LOCKED | FLAG_H_LOCKED)

static struct vehicle_ad_dev *tp2825_g_addev;
static int cvstd_mode = CVSTD_720P50;
static int cvstd_old = CVSTD_720P50;
static int cvstd_sd = CVSTD_NTSC;
static int cvstd_state = VIDEO_UNPLUG;
static int cvstd_old_state = VIDEO_UNPLUG;

#define SENSOR_REGISTER_LEN	1	/* sensor register address bytes*/
#define SENSOR_VALUE_LEN	1	/* sensor register value bytes*/

struct rk_sensor_reg {
	unsigned int reg;
	unsigned int val;
};

#define SENSOR_CHANNEL_REG		0x41

#define SEQCMD_END  0xFF000000
#define SensorEnd   {SEQCMD_END, 0x00}

#define SENSOR_DG VEHICLE_DG

/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data_ntsc[] = {
	{0x02, 0xCF},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x5e},
	{0x11, 0x40},
	{0x12, 0x44},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x13},
	{0x16, 0x4E},
	{0x17, 0xBC},
	{0x18, 0x15},
	{0x19, 0xF0},
	{0x1A, 0x07},
	{0x1B, 0x00},
	{0x1C, 0x09},
	{0x1D, 0x38},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0xA0},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x26, 0x12},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x1A},
	{0x2D, 0x68},
	{0x2E, 0x5E},
	{0x2F, 0x00},
	{0x30, 0x62},
	{0x31, 0xBB},
	{0x32, 0x96},
	{0x33, 0xC0},
	{0x34, 0x00},
	{0x35, 0x65},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x84},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	{0x4D, 0x00},
	{0x4E, 0x37},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x00},
	{0xBA, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_pal[] = {
	{0x02, 0xCE},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x11},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x70},
	{0x11, 0x4D},
	{0x12, 0x40},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x13},
	{0x16, 0x67},
	{0x17, 0xBC},
	{0x18, 0x16},
	{0x19, 0x20},
	{0x1A, 0x17},
	{0x1B, 0x00},
	{0x1C, 0x09},
	{0x1D, 0x48},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0xB0},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x26, 0x02},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x1A},
	{0x2D, 0x60},
	{0x2E, 0x5E},
	{0x2F, 0x00},
	{0x30, 0x7A},
	{0x31, 0x4A},
	{0x32, 0x4D},
	{0x33, 0xF0},
	{0x34, 0x00},
	{0x35, 0x65},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x84},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	{0x4D, 0x00},
	{0x4E, 0x37},
	{0x4F, 0x00},
	{0xB5, 0x01},
	{0xB8, 0x00},
	{0xBA, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_50hz[] = {
	{0x02, 0xCA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x43},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x18},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x07},
	{0x1D, 0xBC},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x26, 0x02},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x4A},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x05},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x8C},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	{0x4D, 0x00},
	{0x4E, 0x03},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x00},
	{0xBA, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_30hz[] = {
	{0x02, 0xDA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x19},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x06},
	{0x1D, 0x72},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x26, 0x02},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x4A},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x25},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x88},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x03},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	{0x4D, 0x00},
	{0x4E, 0x17},
	{0x4F, 0x01},
	{0x85, 0x00},
	{0x88, 0x00},
	{0x8A, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_25hz[] = {
	{0x02, 0xCA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x19},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x07},
	{0x1D, 0xBC},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x26, 0x02},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x25},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x88},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	{0x4D, 0x00},
	{0x4E, 0x17},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x00},
	{0xBA, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static void tp2825_reinit_parameter(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	int i = 0, defrect_index = 0;

	switch (cvstd) {
	case CVSTD_PAL:
		ad->cfg.width = FORCE_PAL_WIDTH;
		ad->cfg.height = FORCE_PAL_HEIGHT;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_PAL;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_LOW |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
		break;
	case CVSTD_NTSC:
		ad->cfg.width = FORCE_NTSC_WIDTH;
		ad->cfg.height = FORCE_NTSC_HEIGHT;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_NTSC;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 30;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_LOW |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
		break;
	default:
		ad->cfg.width = 1280;
		ad->cfg.height = 720;
		ad->cfg.start_x = 8;
		ad->cfg.start_y = 20;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 1;
		ad->cfg.frame_rate = 50;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_HIGH |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
		break;
	}

	/* fix crop info from dts config */
	for (i = 0; i < 4; i++) {
		if ((ad->defrects[i].width == ad->cfg.width) &&
		    (ad->defrects[i].height == ad->cfg.height)) {
			ad->cfg.start_x = ad->defrects[i].crop_x;
			ad->cfg.start_y = ad->defrects[i].crop_y;
			ad->cfg.width = ad->defrects[i].crop_width;
			ad->cfg.height = ad->defrects[i].crop_height;
			defrect_index = i;
		}
	}

#ifdef CVBS_DOUBLE_FPS_MODE
	switch (cvstd) {
	case CVSTD_PAL:
		if (!strstr(ad->defrects[defrect_index].interface, "pal")) {
			ad->cfg.height /= 2;
			ad->cfg.input_format =
				CIF_INPUT_FORMAT_PAL_SW_COMPOSITE;
			ad->cfg.href = 0;
			ad->cfg.vsync = 1;
			ad->cfg.frame_rate = 50;
		}
	break;
	case CVSTD_NTSC:
		if (!strstr(ad->defrects[defrect_index].interface, "ntsc")) {
			ad->cfg.height /= 2;
			ad->cfg.input_format =
				CIF_INPUT_FORMAT_NTSC_SW_COMPOSITE;
			ad->cfg.href = 0;
			ad->cfg.vsync = 1;
			ad->cfg.frame_rate = 60;
		}
	break;
	}
#endif
	SENSOR_DG("%s,crop(%d,%d)", __func__, ad->cfg.start_x, ad->cfg.start_y);
}

static void tp2825_reg_init(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	struct rk_sensor_reg *sensor;
	int i;
	unsigned char val[2];

	switch (cvstd) {
	case CVSTD_720P50:
		sensor = sensor_preview_data_720p_50hz;
		break;
	case CVSTD_720P30:
		sensor = sensor_preview_data_720p_30hz;
		break;
	case CVSTD_720P25:
		sensor = sensor_preview_data_720p_25hz;
		break;
	case CVSTD_PAL:
		sensor = sensor_preview_data_pal;
		break;
	case CVSTD_NTSC:
		sensor = sensor_preview_data_ntsc;
		break;
	default:
		sensor = sensor_preview_data_720p_50hz;
		break;
	}
	i = 0;
	while ((sensor[i].reg != SEQCMD_END) && (sensor[i].reg != 0xFC000000)) {
		if (sensor[i].reg == SENSOR_CHANNEL_REG)
			sensor[i].val = ad->ad_chl;

		val[0] = sensor[i].val;
		vehicle_generic_sensor_write(ad, sensor[i].reg, val);
		i++;
	}
}

void tp2825_channel_set(struct vehicle_ad_dev *ad, int channel)
{
	unsigned int reg = 0x41;
	unsigned char val[0];

	val[0] = channel;
	ad->ad_chl = channel;

	vehicle_generic_sensor_write(ad, reg, val);
}

int tp2825_ad_get_cfg(struct vehicle_cfg **cfg)
{
	if (!tp2825_g_addev)
		return -1;

	switch (cvstd_state) {
	case VIDEO_UNPLUG:
		tp2825_g_addev->cfg.ad_ready = false;
		break;
	case VIDEO_LOCKED:
		tp2825_g_addev->cfg.ad_ready = true;
		break;
	case VIDEO_IN:
		tp2825_g_addev->cfg.ad_ready = false;
		break;
	}

	*cfg = &tp2825_g_addev->cfg;

	return 0;
}

void tp2825_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line)
{
	SENSOR_DG("%s, last_line %d\n", __func__, last_line);
	if (last_line < 1)
		return;

	ad->cif_error_last_line = last_line;
	if (cvstd_mode == CVSTD_PAL) {
		if (last_line == FORCE_NTSC_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_NTSC) {
		if (last_line == FORCE_PAL_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	}
}

int tp2825_check_id(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	int pidh, pidl;

	pidh = vehicle_generic_sensor_read(ad, 0xfe);
	pidl = vehicle_generic_sensor_read(ad, 0xff);
	if (pidh != 0x28 || pidl != 0x25) {
		SENSOR_DG("%s: expected 0x2825, detected 0x%02x 0x%02x\n",
		    ad->ad_name, pidh, pidl);
		ret = -EINVAL;
	}

	return ret;
}

static int tp2825_check_cvstd(struct vehicle_ad_dev *ad, bool activate_check)
{
	unsigned char cvstd = 0;
	unsigned char status = 0;
	static bool is_first = true;
	static int state = VIDEO_UNPLUG;
	int check_count = 20;
	unsigned char v[2];

check_continue:
	status = vehicle_generic_sensor_read(ad, 0x01);

	if (status & FLAG_LOSS) {
		state = VIDEO_UNPLUG;
		v[0] = 0x01;
		vehicle_generic_sensor_write(ad, 0x26, v);
	} else if (FLAG_LOCKED == (status & FLAG_LOCKED)) {
		/* video locked */
		state = VIDEO_LOCKED;
		v[0] = 0x02;
		vehicle_generic_sensor_write(ad, 0x26, v);
	} else {
		/* video in but unlocked */
		state = VIDEO_IN;
		v[0] = 0x02;
		vehicle_generic_sensor_write(ad, 0x26, v);
	}

	if (state == VIDEO_IN) {
		cvstd = vehicle_generic_sensor_read(ad, 0x03);
		SENSOR_DG("%s(%d): cvstd_old %d, read 0x03 return 0x%x",
			  __func__, __LINE__, cvstd_old, cvstd);

		cvstd &= 0x07;
		if (cvstd == cvstd_old)
			goto check_end;

		if (cvstd == CVSTD_720P30) {
			cvstd_mode = CVSTD_720P30;
			SENSOR_DG("%s(%d): 720P30\n", __func__, __LINE__);
		} else if (cvstd == CVSTD_720P25) {
			cvstd_mode = CVSTD_720P25;
			SENSOR_DG("%s(%d): 720P25\n", __func__, __LINE__);
		} else if (cvstd == CVSTD_720P60) {
			SENSOR_DG("%s(%d): 720P60", __func__, __LINE__);
		} else if (cvstd == CVSTD_720P50) {
			cvstd_mode = CVSTD_720P50;
			SENSOR_DG("%s(%d): 720P50\n", __func__, __LINE__);
		} else if (cvstd == CVSTD_1080P30) {
			SENSOR_DG("%s(%d): 1080P30", __func__, __LINE__);
		} else if (cvstd == CVSTD_1080P25) {
			SENSOR_DG("%s(%d): 1080P25", __func__, __LINE__);
		} else if (cvstd == CVSTD_SD) {
			msleep(80);
			status = vehicle_generic_sensor_read(ad, 0x01);
			SENSOR_DG("%s(%d): read 0x01 return 0x%x\n",
				  __func__, __LINE__, status);

			/*
			 * 1: pal  0: ntsc
			 */
			if ((status >> 2) & 0x01)
				cvstd_sd = CVSTD_PAL;
			else
				cvstd_sd = CVSTD_NTSC;

			SENSOR_DG("%s(%d): cvstd_sd is %s\n",
				  __func__, __LINE__,
				  (cvstd_sd == CVSTD_PAL) ? "PAL" : "NTSC");
			cvstd_mode = cvstd_sd;
		}
		tp2825_reinit_parameter(ad, cvstd_mode);
	} else if (state == VIDEO_LOCKED) {
		goto check_end;
	} else {
		SENSOR_DG("%s: check sensor statue failed!\n", __func__);
		goto check_end;
	}

	tp2825_reg_init(ad, cvstd_mode);
check_end:
	if (check_count && is_first && (state != VIDEO_LOCKED)) {
		check_count--;
		if (cvstd == CVSTD_SD)
			mdelay(100);
		else
			mdelay(100);
		goto check_continue;
	}
	is_first = false;
	cvstd_state = state;

	return 0;
}
int tp2825_stream(struct vehicle_ad_dev *ad, int enable)
{
	char val;

	if (enable)
		val = 0x03; //stream on
	else
		val = 0x00; //stream off
	SENSOR_DG("stream write 0x%x to reg 0x4D\n", val);
	vehicle_generic_sensor_write(ad, 0x4D, &val);

	return 0;
}
static void power_on(struct vehicle_ad_dev *ad)
{
	/* gpio_direction_output(ad->power, ad->pwr_active); */

	if (gpio_is_valid(ad->powerdown)) {
		gpio_request(ad->powerdown, "ad_powerdown");
		gpio_direction_output(ad->powerdown, !ad->pwdn_active);
		/* gpio_set_value(ad->powerdown, !ad->pwdn_active); */
	}

	if (gpio_is_valid(ad->power)) {
		gpio_request(ad->power, "ad_power");
		gpio_direction_output(ad->power, ad->pwr_active);
		/* gpio_set_value(ad->power, ad->pwr_active); */
	}
}

static void power_off(struct vehicle_ad_dev *ad)
{
	if (gpio_is_valid(ad->power))
		gpio_free(ad->power);
	if (gpio_is_valid(ad->powerdown))
		gpio_free(ad->powerdown);
}

static void tp2825_check_state_work(struct work_struct *work)
{
	struct vehicle_ad_dev *ad;

	ad = tp2825_g_addev;

	if (ad->cif_error_last_line > 0) {
		tp2825_check_cvstd(ad, true);
		ad->cif_error_last_line = 0;
	} else {
		tp2825_check_cvstd(ad, false);
	}

	if (cvstd_old != cvstd_mode || cvstd_old_state != cvstd_state) {
		cvstd_old = cvstd_mode;
		cvstd_old_state = cvstd_state;
		SENSOR_DG("ad signal change notify\n");
		vehicle_ad_stat_change_notify();
	}

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(100));
}

int tp2825_ad_deinit(void)
{
	struct vehicle_ad_dev *ad;

	ad = tp2825_g_addev;

	if (!ad)
		return -1;

	if (ad->state_check_work.state_check_wq) {
		cancel_delayed_work_sync(&ad->state_check_work.work);
		flush_delayed_work(&ad->state_check_work.work);
		flush_workqueue(ad->state_check_work.state_check_wq);
		destroy_workqueue(ad->state_check_work.state_check_wq);
	}
	if (ad->irq)
		free_irq(ad->irq, ad);
	power_off(ad);

	return 0;
}

static int get_ad_mode_from_fix_format(int fix_format)
{
	int mode = -1;

	switch (fix_format) {
	case AD_FIX_FORMAT_PAL:
		mode = CVSTD_PAL;
		break;
	case AD_FIX_FORMAT_NTSC:
		mode = CVSTD_NTSC;
		break;
	case AD_FIX_FORMAT_720P_50FPS:
		mode = CVSTD_720P50;
		break;
	case AD_FIX_FORMAT_720P_30FPS:
		mode = CVSTD_720P30;
		break;
	case AD_FIX_FORMAT_720P_25FPS:
		mode = CVSTD_720P25;
		break;
	default:
		mode = -1;
		break;
	}

	return mode;
}

int tp2825_ad_init(struct vehicle_ad_dev *ad)
{
	int val = 0;
	int i = 0;
	int mode;

	tp2825_g_addev = ad;

	/*  1. i2c init */
	while (ad->adapter == NULL) {
		ad->adapter = i2c_get_adapter(ad->i2c_chl);
		usleep_range(10000, 12000);
	}
	if (ad->adapter == NULL)
		return -ENODEV;

	if (!i2c_check_functionality(ad->adapter, I2C_FUNC_I2C))
		return -EIO;

	/*  2. ad power on sequence */
	power_on(ad);

	while (++i < 5) {
		usleep_range(1000, 1200);
		val = vehicle_generic_sensor_read(ad, 0x12);
		if (val != 0xff)
			break;
		SENSOR_DG("tp2825_init i2c_reg_read fail\n");
	}

	/* fix mode */
	mode = get_ad_mode_from_fix_format(ad->fix_format);
	if (mode > 0) {
		SENSOR_DG("fix format %d, fix cvxtd mode %d\n", ad->fix_format, mode);
		tp2825_reg_init(ad, mode);
		tp2825_reinit_parameter(ad, mode);
		SENSOR_DG("%s after init\n", __func__);
		/* wait for signal locked; */
		i = 0;
		while (++i < 10) {
			msleep(100);
			val = vehicle_generic_sensor_read(ad, 0x01);
			if ((FLAG_LOCKED == (val & FLAG_LOCKED)))
				break;
		}
		cvstd_state = VIDEO_LOCKED;
		return 0;
	}

	/*  3 .init default format params */
	tp2825_reg_init(ad, cvstd_mode);
	tp2825_reinit_parameter(ad, cvstd_mode);
	SENSOR_DG("%s after reinit init\n", __func__);

	/*  5. create workqueue to detect signal change */
	INIT_DELAYED_WORK(&ad->state_check_work.work, tp2825_check_state_work);
	ad->state_check_work.state_check_wq =
		create_singlethread_workqueue("vehicle-ad-tp2825");

	/* tp2825_check_cvstd(ad, true); */

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(100));

	return 0;
}


