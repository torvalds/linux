/*
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
#include <media/v4l2-controls_rockchip.h>
#include "ov_camera_module.h"

#define OV9750_DRIVER_NAME "ov9750"

#define OV9750_FETCH_LSB_GAIN(VAL) (VAL & 0xFF)
#define OV9750_FETCH_MSB_GAIN(VAL) ((VAL >> 8) & 0xff)
#define OV9750_AEC_PK_LONG_GAIN_HIGH_REG 0x3508
#define OV9750_AEC_PK_LONG_GAIN_LOW_REG 0x3509

#define OV9750_AEC_PK_LONG_EXPO_3RD_REG 0x3500
#define OV9750_AEC_PK_LONG_EXPO_2ND_REG 0x3501
#define OV9750_AEC_PK_LONG_EXPO_1ST_REG 0x3502

#define OV9750_AEC_GROUP_UPDATE_ADDRESS 0x3208
#define OV9750_AEC_GROUP_UPDATE_START_DATA 0x00
#define OV9750_AEC_GROUP_UPDATE_END_DATA 0x10
#define OV9750_AEC_GROUP_UPDATE_END_LAUNCH 0xA0

#define OV9750_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 12) & 0xF)
#define OV9750_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 4) & 0xFF)
#define OV9750_FETCH_1ST_BYTE_EXP(VAL) (((VAL) & 0x0F) << 4)

#define OV9750_PIDH_ADDR 0x300B
#define OV9750_PIDL_ADDR 0x300C

#define OV9750_PIDH_MAGIC 0x97
#define OV9750_PIDL_MAGIC 0x50

#define OV9750_EXT_CLK 12000000
#define OV9750_TIMING_VTS_HIGH_REG 0x380e
#define OV9750_TIMING_VTS_LOW_REG 0x380f
#define OV9750_TIMING_HTS_HIGH_REG 0x380c
#define OV9750_TIMING_HTS_LOW_REG 0x380d
#define OV9750_FINE_INTG_TIME_MIN 0
#define OV9750_FINE_INTG_TIME_MAX_MARGIN 0
#define OV9750_COARSE_INTG_TIME_MIN 1
#define OV9750_COARSE_INTG_TIME_MAX_MARGIN 4
#define OV9750_TIMING_X_INC 0x3814
#define OV9750_TIMING_Y_INC 0x3815
#define OV9750_HORIZONTAL_START_HIGH_REG 0x3800
#define OV9750_HORIZONTAL_START_LOW_REG 0x3801
#define OV9750_VERTICAL_START_HIGH_REG 0x3802
#define OV9750_VERTICAL_START_LOW_REG 0x3803
#define OV9750_HORIZONTAL_END_HIGH_REG 0x3804
#define OV9750_HORIZONTAL_END_LOW_REG 0x3805
#define OV9750_VERTICAL_END_HIGH_REG 0x3806
#define OV9750_VERTICAL_END_LOW_REG 0x3807
#define OV9750_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define OV9750_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3809
#define OV9750_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x380a
#define OV9750_VERTICAL_OUTPUT_SIZE_LOW_REG 0x380b
#define OV9750_H_WIN_OFF_HIGH_REG 0x3810
#define OV9750_H_WIN_OFF_LOW_REG 0x3811
#define OV9750_V_WIN_OFF_HIGH_REG 0x3812
#define OV9750_V_WIN_OFF_LOW_REG 0x3813

static int cam_num;
static struct ov_camera_module ov9750[2];

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* 1280x960 2lane MCLK:24MHz 60fps 800Mbps/lane, MCLK:12Mhz 30fps 400Mbps/lane */
static struct ov_camera_module_reg ov9750_init_tab_1280_960_30fps[] = {
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0103, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0300, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0302, 0x64},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0303, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0304, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0305, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0306, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030d, 0x1e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030e, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030f, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0312, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x031e, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3000, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3001, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x21},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3005, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3016, 0x53},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x32},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301a, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301b, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301c, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301d, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301e, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3031, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3032, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303c, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303e, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3040, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3041, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3042, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3104, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3106, 0x15},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3107, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3504, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3505, 0x83},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3508, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3600, 0x65},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3601, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3602, 0x22},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3610, 0xe8},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3611, 0x56},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3612, 0x48},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3613, 0x5a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3614, 0x91},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3615, 0x79},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3617, 0x57},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3621, 0x90},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3623, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3625, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3634, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3635, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x13},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3650, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3652, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3654, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3653, 0x34},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3655, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3656, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3657, 0xc4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x365a, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x365b, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x365e, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x365f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3668, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366a, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366e, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3702, 0x1d},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3703, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3704, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3706, 0x27},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370b, 0x7d},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3714, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x371a, 0x5e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3730, 0x82},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3733, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x373e, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3755, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3758, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x375b, 0x13},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3772, 0x23},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3773, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3774, 0x16},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3775, 0x12},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3776, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37a8, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37b5, 0x36},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37c2, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37c5, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37c7, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37c8, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x37d1, 0x13},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0xcb},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0xc0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x03},//hts
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x2a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x03},//vts
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0xdc},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3816, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3817, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3818, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3819, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3826, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3827, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382a, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382b, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3836, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3838, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3861, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3862, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3863, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3b00, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c00, 0x89},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c01, 0xab},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c02, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c03, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c04, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c05, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c06, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c07, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c40, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c41, 0xa3},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c43, 0x7d},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c56, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c80, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c82, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c83, 0x61},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3d85, 0x17},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3f08, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3f0a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3f0b, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4000, 0xcd},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4003, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4009, 0x0d},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4010, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4011, 0x70},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4017, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4040, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4041, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4303, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4307, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4500, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4502, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4503, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4508, 0xaa},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x450b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x450c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4600, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4601, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4700, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4704, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4705, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x484a, 0x3f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5002, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5004, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5006, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5007, 0xe0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5008, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5009, 0xb0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x502a, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5901, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a01, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a03, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a04, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a05, 0xe0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a06, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a07, 0xb0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5a08, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5e00, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5e10, 0xfc},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3733, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3610, 0xe8},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3611, 0x56},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3635, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x13},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0x84},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3614, 0x96},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x481f, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3788, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3789, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x378a, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x378b, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3799, 0x27},
};

/* ======================================================================== */

static struct ov_camera_module_config ov9750_configs[] = {
	{
		.name = "1280x960_30fps",
		.frm_fmt = {
			.width = 1280,
			.height = 960,
			.code = MEDIA_BUS_FMT_SBGGR10_1X10
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 30
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)ov9750_init_tab_1280_960_30fps,
		.reg_table_num_entries =
			ARRAY_SIZE(ov9750_init_tab_1280_960_30fps),
		.v_blanking_time_us = 3078,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 402, OV9750_EXT_CLK)
	}
};

/*--------------------------------------------------------------------------*/

static int ov9750_dual_mode(struct ov_camera_module *cam_mod)
{
	if (cam_mod->as_master == 1) {
		ov_camera_module_write_reg(cam_mod, 0x3002, 0xa1);
		ov_camera_module_write_reg(cam_mod, 0x3007, 0x02);
		ov_camera_module_write_reg(cam_mod, 0x3816, 0x00);
		ov_camera_module_write_reg(cam_mod, 0x3817, 0x00);
		ov_camera_module_write_reg(cam_mod, 0x3818, 0x00);
		ov_camera_module_write_reg(cam_mod, 0x3819, 0x01);
		ov_camera_module_write_reg(cam_mod, 0x3823, 0x00);
		ov_camera_module_write_reg(cam_mod, 0x3824, 0x00);
	} else if (cam_mod->as_master == 0) {
		ov_camera_module_write_reg(cam_mod, 0x3002, 0x21);
		ov_camera_module_write_reg(cam_mod, 0x3823, 0x48);
		ov_camera_module_write_reg(cam_mod, 0x3824, 0x11);
	} else {
		;/* do nothing */
	}
	return 0;
}

/*--------------------------------------------------------------------------*/

static int ov9750_set_flip(struct ov_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 match_reg[3];

	mode = ov_camera_module_get_flip_mirror(cam_mod);

	if (mode == -1) {
		ov_camera_module_pr_debug(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (mode == OV_FLIP_BIT_MASK) {
		match_reg[0] = 0x86;
		match_reg[1] = 0x40;
		match_reg[2] = 0x20;
	} else if (mode == OV_MIRROR_BIT_MASK) {
		match_reg[0] = 0x80;
		match_reg[1] = 0x46;
		match_reg[2] = 0x00;
	} else if (mode == (OV_MIRROR_BIT_MASK |
		OV_FLIP_BIT_MASK)) {
		match_reg[0] = 0x86;
		match_reg[1] = 0x46;
		match_reg[2] = 0x20;
	} else {
		match_reg[0] = 0x80;
		match_reg[1] = 0x40;
		match_reg[2] = 0x00;
	}

	for (i = len; i > 0; i--) {
		if (reglist[i].reg == 0x3820)
			reglist[i].val = match_reg[0];
		else if (reglist[i].reg == 0x3821)
			reglist[i].val = match_reg[1];
		else if (reglist[i].reg == 0x450b)
			reglist[i].val = match_reg[2];
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int OV9750_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(cam_mod,
		OV9750_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(cam_mod,
		OV9750_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int OV9750_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((exp_time + OV9750_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = exp_time + OV9750_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;

	ret = ov_camera_module_write_reg(cam_mod,
		OV9750_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		OV9750_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0x0F);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int ov9750_set_vts(struct ov_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts <= cam_mod->vts_min)
		return ret;

	ret = ov_camera_module_write_reg(cam_mod,
		OV9750_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		OV9750_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0x0F);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}
	return ret;
}

static int ov9750_write_aec(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"exp_time = %d lines, gain = %d, flash_mode = %d\n",
		cam_mod->exp_config.exp_time,
		cam_mod->exp_config.gain,
		cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */
	if (cam_mod->state == OV_CAMERA_MODULE_SW_STANDBY ||
		cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time = cam_mod->exp_config.exp_time;

		mutex_lock(&cam_mod->lock);
		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;
		if (a_gain < 0x80)
			a_gain = 0x80;
		if (a_gain > 0x7c0)
			a_gain = 0x7c0;
		if (exp_time < 4)
			exp_time = 4;

		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
				OV9750_AEC_GROUP_UPDATE_ADDRESS,
				OV9750_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = OV9750_auto_adjust_fps(cam_mod,
				cam_mod->exp_config.exp_time);
		ret |= ov_camera_module_write_reg(cam_mod,
			OV9750_AEC_PK_LONG_GAIN_HIGH_REG,
			OV9750_FETCH_MSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV9750_AEC_PK_LONG_GAIN_LOW_REG,
			OV9750_FETCH_LSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV9750_AEC_PK_LONG_EXPO_3RD_REG,
			OV9750_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV9750_AEC_PK_LONG_EXPO_2ND_REG,
			OV9750_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV9750_AEC_PK_LONG_EXPO_1ST_REG,
			OV9750_FETCH_1ST_BYTE_EXP(exp_time));

		if (!cam_mod->auto_adjust_fps)
			ret |= ov9750_set_vts(cam_mod, cam_mod->exp_config.vts_value);

		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret |= ov_camera_module_write_reg(cam_mod,
				OV9750_AEC_GROUP_UPDATE_ADDRESS,
				OV9750_AEC_GROUP_UPDATE_END_DATA);
			ret |= ov_camera_module_write_reg(cam_mod,
				OV9750_AEC_GROUP_UPDATE_ADDRESS,
				OV9750_AEC_GROUP_UPDATE_END_LAUNCH);
		}
		mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov9750_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
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

/*--------------------------------------------------------------------------*/

static int ov9750_filltimings(struct ov_camera_module_custom_config *custom)
{
	u32 i, j;
	u32 win_h_off = 0, win_v_off = 0;
	struct ov_camera_module_config *config;
	struct ov_camera_module_timings *timings;
	struct ov_camera_module_reg *reg_table;
	u32 reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		memset(timings, 0x00, sizeof(*timings));
		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case OV9750_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case OV9750_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case OV9750_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case OV9750_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case OV9750_TIMING_X_INC:
				timings->binning_factor_x =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case OV9750_TIMING_Y_INC:
				timings->binning_factor_y =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case OV9750_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_start &
					0xff));
				break;
			case OV9750_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start =
					(reg_table[j].val |
					(timings->crop_horizontal_start &
					0xff00));
				break;
			case OV9750_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_start & 0xff));
				break;
			case OV9750_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start =
					((reg_table[j].val) |
					(timings->crop_vertical_start &
					0xff00));
				break;
			case OV9750_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_end & 0xff));
				break;
			case OV9750_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end =
					(reg_table[j].val |
					(timings->crop_horizontal_end &
					0xff00));
				break;
			case OV9750_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_end & 0xff));
				break;
			case OV9750_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end =
					(reg_table[j].val |
					(timings->crop_vertical_end & 0xff00));
				break;
			case OV9750_H_WIN_OFF_HIGH_REG:
				win_h_off = (reg_table[j].val & 0xf) << 8;
				break;
			case OV9750_H_WIN_OFF_LOW_REG:
				win_h_off |= (reg_table[j].val & 0xff);
				break;
			case OV9750_V_WIN_OFF_HIGH_REG:
				win_v_off = (reg_table[j].val & 0xf) << 8;
				break;
			case OV9750_V_WIN_OFF_LOW_REG:
				win_v_off |= (reg_table[j].val & 0xff);
				break;
			case OV9750_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case OV9750_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width &
					0xff00));
				break;
			case OV9750_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case OV9750_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height &
					0xff00));
				break;
			}
		}

		timings->crop_horizontal_start += win_h_off;
		timings->crop_horizontal_end -= win_h_off;
		timings->crop_vertical_start += win_v_off;
		timings->crop_vertical_end -= win_v_off;

		timings->exp_time >>= 4;
		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator *
			timings->frame_length_lines *
			timings->line_length_pck;

		timings->coarse_integration_time_min =
			OV9750_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			OV9750_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			OV9750_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			OV9750_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int ov9750_g_timings(struct ov_camera_module *cam_mod,
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
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
			cam_mod->active_config->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;

	timings->frame_length_lines = vts;
	return ret;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9750_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov9750_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	/*
	 * case RK_V4L2_CID_FPS_CTRL:
	 * if (cam_mod->auto_adjust_fps)
	 * ret = OV9750_auto_adjust_fps(
	 * cam_mod,
	 * cam_mod->exp_config.exp_time);
	 * break;
	 */
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9750_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	/* Handles only exposure and gain together special case. */
	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = ov9750_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9750_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod,
		"active config=%s\n",
		cam_mod->active_config->name);

	ov9750_dual_mode(cam_mod);

	ret = OV9750_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;
	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0x01);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);
	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n",
		ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9750_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");
	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0x00);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);
	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9750_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, OV9750_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, OV9750_PIDL_ADDR, &pidl);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pidh == OV9750_PIDH_MAGIC && pidl == OV9750_PIDL_MAGIC) {
		ov_camera_module_pr_info(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			OV9750_PIDH_MAGIC, OV9750_PIDL_MAGIC, pidh, pidl);
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

static struct v4l2_subdev_core_ops ov9750_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov9750_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.g_frame_interval = ov_camera_module_g_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov9750_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov9750_camera_module_ops = {
	.core = &ov9750_camera_module_core_ops,
	.video = &ov9750_camera_module_video_ops,
	.pad = &ov9750_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov9750_custom_config = {
	.start_streaming = ov9750_start_streaming,
	.stop_streaming = ov9750_stop_streaming,
	.s_ctrl = ov9750_s_ctrl,
	.g_ctrl = ov9750_g_ctrl,
	.s_ext_ctrls = ov9750_s_ext_ctrls,
	.g_timings = ov9750_g_timings,
	.set_flip = ov9750_set_flip,
	.s_vts = OV9750_auto_adjust_fps,
	.check_camera_id = ov9750_check_camera_id,
	.configs = ov9750_configs,
	.num_configs = ARRAY_SIZE(ov9750_configs),
	.power_up_delays_ms = {5, 30, 30},
	.exposure_valid_frame = {4, 4}
};

static int ov9750_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	int as_master = -1;
	struct device_node *np = of_node_get(client->dev.of_node);

	dev_info(&client->dev, "probing cam_num:%d 0x%x\n",
		cam_num, client->addr);

	ov9750_filltimings(&ov9750_custom_config);
	v4l2_i2c_subdev_init(&ov9750[cam_num].sd, client,
		&ov9750_camera_module_ops);

	ov9750[cam_num].sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov9750[cam_num].custom = ov9750_custom_config;

	mutex_init(&ov9750[cam_num].lock);

	ret = of_property_read_u32(np, "as-master", &as_master);
	ov9750[cam_num].as_master = (ret == 0) ? as_master : -1;
	cam_num++;

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

/* ======================================================================== */

static int ov9750_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;

	mutex_destroy(&cam_mod->lock);
	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov9750_id[] = {
	{ OV9750_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov9750_of_match[] = {
	{.compatible = "omnivision,ov9750-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov9750_id);

static struct i2c_driver ov9750_i2c_driver = {
	.driver = {
		.name = OV9750_DRIVER_NAME,
		.of_match_table = ov9750_of_match
	},
	.probe = ov9750_probe,
	.remove = ov9750_remove,
	.id_table = ov9750_id,
};

module_i2c_driver(ov9750_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov9750");
MODULE_AUTHOR("Cain");
MODULE_LICENSE("GPL");
