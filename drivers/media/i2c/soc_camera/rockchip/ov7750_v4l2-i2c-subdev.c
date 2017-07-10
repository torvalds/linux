/*
 * drivers/media/i2c/soc_camera/rockchip/ov7750_v4l2-i2c-subdev.c
 *
 * ov7750 sensor driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include "ov_camera_module.h"

/*****************************************************************************
 * DEFINES
 *****************************************************************************/
#define ov7750_DRIVER_NAME "ov7750"

#define ov7750_FETCH_LSB_GAIN(VAL) ((VAL) & 0x00ff)
#define ov7750_FETCH_MSB_GAIN(VAL) (((VAL) >> 8) & 0xff)
#define ov7750_AEC_PK_LONG_GAIN_HIGH_REG 0x3508	/* Bit 6-13 */
#define ov7750_AEC_PK_LONG_GAIN_LOW_REG	 0x3509	/* Bits 0 -5 */

#define ov7750_AEC_PK_LONG_EXPO_3RD_REG 0x3500	/* Exposure Bits 16-19 */
#define ov7750_AEC_PK_LONG_EXPO_2ND_REG 0x3501	/* Exposure Bits 8-15 */
#define ov7750_AEC_PK_LONG_EXPO_1ST_REG 0x3502	/* Exposure Bits 0-7 */

#define ov7750_AEC_GROUP_UPDATE_ADDRESS 0x3208
#define ov7750_AEC_GROUP_UPDATE_START_DATA 0x00
#define ov7750_AEC_GROUP_UPDATE_END_DATA 0x10
#define ov7750_AEC_GROUP_UPDATE_END_LAUNCH 0xA0

#define ov7750_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 16) & 0xF)	/* 4 Bits */
#define ov7750_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define ov7750_FETCH_1ST_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 8 Bits */

#define ov7750_PIDH_ADDR     0x300A
#define ov7750_PIDL_ADDR     0x300B

#define ov7750_TIMING_VTS_HIGH_REG 0x380e
#define ov7750_TIMING_VTS_LOW_REG 0x380f
#define ov7750_TIMING_HTS_HIGH_REG 0x380c
#define ov7750_TIMING_HTS_LOW_REG 0x380d
#define ov7750_INTEGRATION_TIME_MARGIN 8
#define ov7750_FINE_INTG_TIME_MIN 0
#define ov7750_FINE_INTG_TIME_MAX_MARGIN 0
#define ov7750_COARSE_INTG_TIME_MIN 16
#define ov7750_COARSE_INTG_TIME_MAX_MARGIN 4
#define ov7750_TIMING_X_INC		0x3814
#define ov7750_TIMING_Y_INC		0x3815
#define ov7750_HORIZONTAL_START_HIGH_REG 0x3800
#define ov7750_HORIZONTAL_START_LOW_REG 0x3801
#define ov7750_VERTICAL_START_HIGH_REG 0x3802
#define ov7750_VERTICAL_START_LOW_REG 0x3803
#define ov7750_HORIZONTAL_END_HIGH_REG 0x3804
#define ov7750_HORIZONTAL_END_LOW_REG 0x3805
#define ov7750_VERTICAL_END_HIGH_REG 0x3806
#define ov7750_VERTICAL_END_LOW_REG 0x3807
#define ov7750_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define ov7750_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3809
#define ov7750_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x380a
#define ov7750_VERTICAL_OUTPUT_SIZE_LOW_REG 0x380b
#define ov7750_FLIP_REG                      0x3820
#define ov7750_MIRROR_REG                      0x3821

#define ov7750_EXT_CLK 26000000
#define ov7750_FULL_SIZE_RESOLUTION_WIDTH 3264
#define ov7750_BINING_SIZE_RESOLUTION_WIDTH 1632
#define ov7750_VIDEO_SIZE_RESOLUTION_WIDTH 3200

#define ov7750_EXP_VALID_FRAMES		4
/* High byte of product ID */
#define ov7750_PIDH_MAGIC 0x77
/* Low byte of product ID  */
#define ov7750_PIDL_MAGIC 0x50

#define BG_RATIO_TYPICAL  0x129
#define RG_RATIO_TYPICAL  0x11f

static struct ov_camera_module ov7750;
static struct ov_camera_module_custom_config ov7750_custom_config;

/*****************************************************************************
 * GLOBALS
 *****************************************************************************/

// Image sensor register settings default values taken from
// data sheet OV7750A_DS_1.1_SiliconImage.pdf.

// The settings may be altered by the code in IsiSetupSensor.
static struct ov_camera_module_reg ov7750_init_tab_640_480_60fps[] = {
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0103, 0x01},// enable soft reset
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00},// stream off
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3005, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3012, 0xc0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3013, 0xd2},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3014, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3016, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3017, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301b, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301c, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3023, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3037, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3098, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3099, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x309a, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x309b, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30b0, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30b1, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30b3, 0x32},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30b4, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30b5, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3106, 0xda},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x1f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350b, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3600, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3602, 0x62},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0xb7},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3626, 0x21},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3627, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x44},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x35},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3634, 0x60},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3662, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3663, 0x70},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3664, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3666, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3669, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366b, 0x50},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3673, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3674, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3675, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0xc1},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x373c, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3742, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3757, 0xb3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3788, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37a8, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37a9, 0xc0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x8b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0xeb},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0xe0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0xa0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382f, 0x0e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3832, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3833, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3834, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3835, 0x0c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3837, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x38b1, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b80, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b81, 0xa5},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b82, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b83, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b84, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b85, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b86, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b87, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b88, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b89, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8b, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8c, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8d, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8e, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b8f, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b94, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b95, 0xf2},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b96, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c00, 0x89},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c01, 0x63},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c02, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c03, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c04, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c05, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c06, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c07, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0c, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0d, 0xd0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0e, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0f, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4001, 0x42},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4004, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4005, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x404e, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4241, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4242, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4300, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4301, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4501, 0x48},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4600, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4601, 0x4e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4801, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4806, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4819, 0xaa},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4823, 0x3e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x19},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4a0d, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4a47, 0x7f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4a49, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4a4b, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0x85},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x1f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350b, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3600, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3602, 0x62},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0xb7},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3626, 0x21},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3627, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x44},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x35},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3634, 0x60},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3662, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3663, 0x70},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3664, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3666, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3669, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366b, 0x50},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3673, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3674, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3675, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0xc1},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x373c, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3742, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3757, 0xb3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3788, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37a8, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37a9, 0xc0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x8b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0xeb},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0xe0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0xa0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382f, 0x0e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3832, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3833, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3834, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3835, 0x0c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3837, 0x00},
};

static struct ov_camera_module_config ov7750_configs[] = {
	{
		.name = "640x480_60fps",
		.frm_fmt = {
			.width = 640,
			.height = 480,
			.code = MEDIA_BUS_FMT_SBGGR10_1X10
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 60
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)ov7750_init_tab_640_480_60fps,
		.reg_table_num_entries =
			sizeof(ov7750_init_tab_640_480_60fps) /
			sizeof(ov7750_init_tab_640_480_60fps[0]),
		.reg_diff_table = NULL,
		.reg_diff_table_num_entries = 0,
		.v_blanking_time_us = 7251,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 1, 800, 24000000)
	}
};

static int ov7750_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov7750_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov7750_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;
	cam_mod->vts_cur = *vts;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((cam_mod->exp_config.exp_time + ov7750_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = cam_mod->exp_config.exp_time +
			ov7750_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;
	ret = ov_camera_module_write_reg(cam_mod,
		ov7750_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov7750_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_debug(cam_mod,
			"updated vts = %d,vts_min=%d\n", vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int ov7750_write_aec(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
				  "exp_time = %d, gain = %d, flash_mode = %d\n",
				  cam_mod->exp_config.exp_time,
				  cam_mod->exp_config.gain,
				  cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */
	if ((cam_mod->state == OV_CAMERA_MODULE_SW_STANDBY) ||
		(cam_mod->state == OV_CAMERA_MODULE_STREAMING)) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time;

		a_gain = a_gain > 0x7ff ? 0x7ff : a_gain;
		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;
		exp_time = cam_mod->exp_config.exp_time << 4;
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
				ov7750_AEC_GROUP_UPDATE_ADDRESS,
				ov7750_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = ov7750_auto_adjust_fps(cam_mod,
						cam_mod->exp_config.exp_time);
		ret |= ov_camera_module_write_reg(cam_mod,
			ov7750_AEC_PK_LONG_GAIN_HIGH_REG,
			ov7750_FETCH_MSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov7750_AEC_PK_LONG_GAIN_LOW_REG,
			ov7750_FETCH_LSB_GAIN(a_gain));
		ret = ov_camera_module_write_reg(cam_mod,
			ov7750_AEC_PK_LONG_EXPO_3RD_REG,
			ov7750_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov7750_AEC_PK_LONG_EXPO_2ND_REG,
			ov7750_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov7750_AEC_PK_LONG_EXPO_1ST_REG,
			ov7750_FETCH_1ST_BYTE_EXP(exp_time));
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret = ov_camera_module_write_reg(cam_mod,
				ov7750_AEC_GROUP_UPDATE_ADDRESS,
				ov7750_AEC_GROUP_UPDATE_END_DATA);
			ret = ov_camera_module_write_reg(cam_mod,
				ov7750_AEC_GROUP_UPDATE_ADDRESS,
				ov7750_AEC_GROUP_UPDATE_END_LAUNCH);
		}
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_filltimings(struct ov_camera_module_custom_config *custom)
{
	int i, j;
	struct ov_camera_module_config *config;
	struct ov_camera_module_timings *timings;
	struct ov_camera_module_reg *reg_table;
	int reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		memset(timings, 0x00, sizeof(*timings));
		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case ov7750_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case ov7750_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case ov7750_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case ov7750_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case ov7750_TIMING_X_INC:
				timings->binning_factor_x =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case ov7750_TIMING_Y_INC:
				timings->binning_factor_y =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case ov7750_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_start &
					0xff));
				break;
			case ov7750_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start =
					(reg_table[j].val |
					(timings->crop_horizontal_start &
					0xff00));
				break;
			case ov7750_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_start & 0xff));
				break;
			case ov7750_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start =
					((reg_table[j].val) |
					(timings->crop_vertical_start &
					0xff00));
				break;
			case ov7750_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_end & 0xff));
				break;
			case ov7750_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end =
					(reg_table[j].val |
					(timings->crop_horizontal_end &
					0xff00));
				break;
			case ov7750_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_end & 0xff));
				break;
			case ov7750_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end =
					(reg_table[j].val |
					(timings->crop_vertical_end & 0xff00));
				break;
			case ov7750_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case ov7750_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width &
					0xff00));
				break;
			case ov7750_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case ov7750_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height &
					0xff00));
				break;
			case ov7750_AEC_PK_LONG_EXPO_1ST_REG:
				timings->exp_time =
					((reg_table[j].val) |
					(timings->exp_time & 0xffff00));
				break;
			case ov7750_AEC_PK_LONG_EXPO_2ND_REG:
				timings->exp_time =
					((reg_table[j].val << 8) |
					(timings->exp_time & 0x00ff00));
				break;
			case ov7750_AEC_PK_LONG_EXPO_3RD_REG:
				timings->exp_time =
					(((reg_table[j].val & 0x0f) << 16) |
					(timings->exp_time & 0xff0000));
				break;
			case ov7750_AEC_PK_LONG_GAIN_LOW_REG:
				timings->gain =
					(reg_table[j].val |
					(timings->gain & 0x0700));
				break;
			case ov7750_AEC_PK_LONG_GAIN_HIGH_REG:
				timings->gain =
					(((reg_table[j].val & 0x07) << 8) |
					(timings->gain & 0xff));
				break;
			}
		}

		timings->exp_time >>= 4;
		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator
			* timings->frame_length_lines
			* timings->line_length_pck;

		timings->coarse_integration_time_min =
			ov7750_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			ov7750_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			ov7750_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			ov7750_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int ov7750_g_timings(struct ov_camera_module *cam_mod,
	struct ov_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	*timings = cam_mod->active_config->timings;

	vts = (!cam_mod->vts_cur) ?
		timings->frame_length_lines :
		cam_mod->vts_cur;
	if (cam_mod->frm_intrvl_valid)
		timings->vt_pix_clk_freq_hz =
			cam_mod->frm_intrvl.interval.denominator *
			vts * timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
		cam_mod->active_config->frm_intrvl.interval.denominator *
		vts * timings->line_length_pck;

	return ret;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov7750_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d) 0x%x\n", ret, ctrl_id);
	return ret;
}

static int ov7750_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	/* Handles only exposure and gain together special case. */
	if (ctrls->count == 1)
		ret = ov7750_s_ctrl(cam_mod, ctrls->ctrls[0].id);
	else if ((ctrls->count == 3) &&
		 ((ctrls->ctrls[0].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[1].id == V4L2_CID_EXPOSURE) ||
		  (ctrls->ctrls[1].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[0].id == V4L2_CID_EXPOSURE)))
		ret = ov7750_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_set_flip(
	struct ov_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 match_reg[2];

	mode = ov_camera_module_get_flip_mirror(cam_mod);
	if (mode == -1) {
		ov_camera_module_pr_info(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		switch (cam_mod->active_config->frm_fmt.width) {
		case ov7750_FULL_SIZE_RESOLUTION_WIDTH:
			if (mode == OV_FLIP_BIT_MASK) {
				match_reg[0] = 0x06;
				match_reg[1] = 0x00;
			} else if (mode == OV_MIRROR_BIT_MASK) {
				match_reg[0] = 0x00;
				match_reg[1] = 0x06;
			} else if (mode == (OV_MIRROR_BIT_MASK |
				OV_FLIP_BIT_MASK)) {
				match_reg[0] = 0x06;
				match_reg[1] = 0x06;
			} else {
				match_reg[0] = 0x00;
				match_reg[1] = 0x00;
			}
			break;
		case ov7750_BINING_SIZE_RESOLUTION_WIDTH:
			if (mode == OV_FLIP_BIT_MASK) {
				match_reg[0] = 0x16;
				match_reg[1] = 0x01;
			} else if (mode == OV_MIRROR_BIT_MASK) {
				match_reg[0] = 0x16;
				match_reg[1] = 0x07;
			} else if (mode == (OV_MIRROR_BIT_MASK |
				OV_FLIP_BIT_MASK)) {
				match_reg[0] = 0x16;
				match_reg[1] = 0x07;
			} else {
				match_reg[0] = 0x10;
				match_reg[1] = 0x01;
			}
			break;
		default:
				return 0;
		}

		for (i = len; i > 0; i--) {
			if (reglist[i].reg == ov7750_FLIP_REG)
				reglist[i].val = match_reg[0];
			else if (reglist[i].reg == ov7750_MIRROR_REG)
				reglist[i].val = match_reg[1];
		}
	}

	return 0;
}

static int ov7750_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	ret = ov7750_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	if (IS_ERR_VALUE(ov_camera_module_write_reg(cam_mod, 0x0100, 1)))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n",
		ret);
	return ret;
}

static int ov7750_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov7750_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, ov7750_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, ov7750_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == ov7750_PIDH_MAGIC) && (pidl == ov7750_PIDL_MAGIC)) {
		ov_camera_module_pr_err(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			ov7750_PIDH_MAGIC, ov7750_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops ov7750_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov7750_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov7750_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov7750_camera_module_ops = {
	.core = &ov7750_camera_module_core_ops,
	.video = &ov7750_camera_module_video_ops,
	.pad = &ov7750_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov7750_custom_config = {
	.start_streaming = ov7750_start_streaming,
	.stop_streaming = ov7750_stop_streaming,
	.s_ctrl = ov7750_s_ctrl,
	.s_ext_ctrls = ov7750_s_ext_ctrls,
	.g_ctrl = ov7750_g_ctrl,
	.g_timings = ov7750_g_timings,
	.check_camera_id = ov7750_check_camera_id,
	.set_flip = ov7750_set_flip,
	.configs = ov7750_configs,
	.num_configs = ARRAY_SIZE(ov7750_configs),
	.power_up_delays_ms = {5, 20, 0}
};

static int ov7750_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	ov7750_filltimings(&ov7750_custom_config);
	v4l2_i2c_subdev_init(&ov7750.sd, client, &ov7750_camera_module_ops);

	ov7750.custom = ov7750_custom_config;

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

static int ov7750_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov7750_id[] = {
	{ ov7750_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov7750_of_match[] = {
	{.compatible = "ovti,ov7750-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov7750_id);

static struct i2c_driver ov7750_i2c_driver = {
	.driver = {
		.name = ov7750_DRIVER_NAME,
		.of_match_table = ov7750_of_match
	},
	.probe = ov7750_probe,
	.remove = ov7750_remove,
	.id_table = ov7750_id,
};

module_i2c_driver(ov7750_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov7750");
MODULE_AUTHOR("Jacob");
MODULE_LICENSE("GPL");
