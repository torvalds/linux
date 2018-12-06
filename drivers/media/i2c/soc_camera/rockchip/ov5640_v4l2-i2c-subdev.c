// SPDX-License-Identifier: GPL-2.0
/*
 * ov5640 sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
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

#define ov5640_DRIVER_NAME "ov5640"

#define ov5640_FETCH_LSB_GAIN(VAL)             ((VAL) & 0x00ff)
#define ov5640_FETCH_MSB_GAIN(VAL)             (((VAL) >> 8) & 0xff)
#define ov5640_AEC_PK_LONG_GAIN_HIGH_REG       0x350a	/* Real gain Bits 6-13 */
#define ov5640_AEC_PK_LONG_GAIN_LOW_REG        0x350b	/* Real gain Bits 0-5 */

#define ov5640_AEC_PK_LONG_EXPO_3RD_REG        0x3500	/* Exposure Bits 16-19 */
#define ov5640_AEC_PK_LONG_EXPO_2ND_REG        0x3501	/* Exposure Bits 8-15 */
#define ov5640_AEC_PK_LONG_EXPO_1ST_REG        0x3502	/* Exposure Bits 0-7 */

#define ov5640_AEC_GROUP_UPDATE_ADDRESS        0x3212
#define ov5640_AEC_GROUP_UPDATE_START_DATA     0x00
#define ov5640_AEC_GROUP_UPDATE_END_DATA       0x10
#define ov5640_AEC_GROUP_UPDATE_END_LAUNCH     0xA0

#define ov5640_FETCH_3RD_BYTE_EXP(VAL)         (((VAL) >> 16) & 0xF)	/* 4 Bits */
#define ov5640_FETCH_2ND_BYTE_EXP(VAL)         (((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define ov5640_FETCH_1ST_BYTE_EXP(VAL)         ((VAL) & 0xFF)	/* 8 Bits */

#define ov5640_PIDH_ADDR                       0x300a
#define ov5640_PIDL_ADDR                       0x300b

#define ov5640_TIMING_VTS_HIGH_REG             0x380e
#define ov5640_TIMING_VTS_LOW_REG              0x380f
#define ov5640_TIMING_HTS_HIGH_REG             0x380c
#define ov5640_TIMING_HTS_LOW_REG              0x380d
#define ov5640_INTEGRATION_TIME_MARGIN         8
#define ov5640_FINE_INTG_TIME_MIN              0
#define ov5640_FINE_INTG_TIME_MAX_MARGIN       0
#define ov5640_COARSE_INTG_TIME_MIN            16
#define ov5640_COARSE_INTG_TIME_MAX_MARGIN     4
#define ov5640_TIMING_X_INC                    0x3814
#define ov5640_TIMING_Y_INC                    0x3815
#define ov5640_HORIZONTAL_START_HIGH_REG       0x3800
#define ov5640_HORIZONTAL_START_LOW_REG        0x3801
#define ov5640_VERTICAL_START_HIGH_REG         0x3802
#define ov5640_VERTICAL_START_LOW_REG          0x3803
#define ov5640_HORIZONTAL_END_HIGH_REG         0x3804
#define ov5640_HORIZONTAL_END_LOW_REG          0x3805
#define ov5640_VERTICAL_END_HIGH_REG           0x3806
#define ov5640_VERTICAL_END_LOW_REG            0x3807
#define ov5640_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define ov5640_HORIZONTAL_OUTPUT_SIZE_LOW_REG  0x3809
#define ov5640_VERTICAL_OUTPUT_SIZE_HIGH_REG   0x380a
#define ov5640_VERTICAL_OUTPUT_SIZE_LOW_REG    0x380b
#define ov5640_FLIP_REG                        0x3820
#define ov5640_MIRROR_REG                      0x3821

#define ov5640_EXT_CLK                         24000000
#define ov5640_FULL_SIZE_RESOLUTION_WIDTH      2592
/* High byte of product ID */
#define ov5640_PIDH_MAGIC                      0x56
/* Low byte of product ID  */
#define ov5640_PIDL_MAGIC                      0x40

static struct ov_camera_module ov5640;
static struct ov_camera_module_custom_config ov5640_custom_config;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
static struct ov_camera_module_reg ov5640_init_tab_800_600_30fps[] = {
	/* OV5640_g_aRegDescription */
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3103, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x82},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x42},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3103, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3017, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3034, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3035, 0x21},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3036, 0x46},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3037, 0x13},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3108, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x36},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x0e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3632, 0xe2},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x12},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3621, 0xe0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3704, 0xa0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3703, 0x5a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3715, 0x78},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3717, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370b, 0x60},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0x1a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3905, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3906, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3901, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3731, 0x12},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3600, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3601, 0x33},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302d, 0x60},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0x52},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x371b, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x471c, 0x50},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a13, 0x43},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a18, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a19, 0x78},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3635, 0x13},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3634, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c01, 0x34},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c04, 0x28},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c05, 0x98},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c06, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c07, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c08, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c09, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0a, 0x9c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c0b, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x41},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x3f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0x9b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0x58},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x68},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3618, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3612, 0x29},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3708, 0x64},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x52},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370c, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a02, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a03, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a08, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a09, 0x27},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0b, 0xf6},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0e, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0d, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a14, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a15, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4001, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4004, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3000, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3004, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3006, 0xc3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300e, 0x58},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302e, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4740, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4300, 0x32},/* uyvy:0x32,yuyv:0x30 */
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x501f, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4713, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4407, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x440e, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x460b, 0x35},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x460c, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3824, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0xa7},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0xa3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5180, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5181, 0xf2},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5182, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5183, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5184, 0x25},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5185, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5186, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5187, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5188, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5189, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518a, 0x5d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518b, 0xe3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518c, 0xa7},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518d, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518e, 0x33},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518f, 0x5e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5190, 0x4e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5191, 0xf8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5192, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5193, 0x70},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5194, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5195, 0xf0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5196, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5197, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5198, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5199, 0xd0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x519a, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x519b, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x519c, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x519d, 0x87},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x519e, 0x38},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5381, 0x27},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5382, 0x41},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5383, 0x18},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5384, 0x0d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5385, 0x59},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5386, 0x66},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5387, 0x63},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5388, 0x55},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5389, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x538a, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x538b, 0x98},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5300, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5301, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5302, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5303, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5304, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5305, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5306, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5307, 0x16},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5309, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x530a, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x530b, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x530c, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5480, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5481, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5482, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5483, 0x28},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5484, 0x51},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5485, 0x65},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5486, 0x71},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5487, 0x7d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5488, 0x87},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5489, 0x91},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548a, 0x9a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548b, 0xaa},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548c, 0xb8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548d, 0xcd},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548e, 0xdd},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x548f, 0xea},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5490, 0x1d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5580, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5583, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5584, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5589, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x558a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x558b, 0xf8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5800, 0x23},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5801, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5802, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5803, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5804, 0x12},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5805, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5806, 0x0c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5807, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5808, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5809, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580a, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580b, 0x0d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580c, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580d, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580e, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x580f, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5810, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5811, 0x09},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5812, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5813, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5814, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5815, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5816, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5817, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5818, 0x0d},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5819, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581a, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581b, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581c, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581d, 0x0e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581e, 0x29},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x581f, 0x17},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5820, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5821, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5822, 0x15},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5823, 0x28},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5824, 0x46},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5825, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5826, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5827, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5828, 0x64},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5829, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582a, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582b, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582c, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582d, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582e, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x582f, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5830, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5831, 0x42},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5832, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5833, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5834, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5835, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5836, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5837, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5838, 0x44},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5839, 0x24},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x583a, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x583b, 0x28},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x583c, 0x42},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x583d, 0xce},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5025, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0f, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a10, 0x28},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1b, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1e, 0x26},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a11, 0x60},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1f, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c07, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x41},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0x9b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0x58},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x68},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3618, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3612, 0x29},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x52},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370c, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a02, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a03, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a08, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a09, 0x94},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0b, 0x7b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0e, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0d, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a14, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a15, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4004, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4713, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3035, 0x21},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3036, 0x46},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3824, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0xa3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4005, 0x1a},
	/* OV5640_g_svga */
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a00, 0x7c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3c07, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x41},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x31},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0x9b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0x58},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x68},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3618, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3612, 0x29},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x52},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370c, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a02, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a03, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a08, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a09, 0x94},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0b, 0x7b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0e, 0x06},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0d, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a14, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a15, 0x14},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4004, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x1c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4713, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3035, 0x21},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3036, 0x46},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3824, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0xa3},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x503d, 0x80},
};

/* ======================================================================== */

static struct ov_camera_module_config ov5640_configs[] = {
	{
		.name = "800x600_30fps",
		.frm_fmt = {
			.width = 800,
			.height = 600,
			.code = MEDIA_BUS_FMT_UYVY8_2X8
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
		.reg_table = (void *)ov5640_init_tab_800_600_30fps,
		.reg_table_num_entries =
			sizeof(ov5640_init_tab_800_600_30fps) /
			sizeof(ov5640_init_tab_800_600_30fps[0]),
		.v_blanking_time_us = 1000,
		PLTFRM_CAM_ITF_DVP_CFG(
			PLTFRM_CAM_ITF_BT601_8,
			PLTFRM_CAM_SIGNAL_LOW_LEVEL,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SDR_NEG_EDG,
			ov5640_EXT_CLK)
	}
};

/*--------------------------------------------------------------------------*/

static int ov5640_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ov_camera_module_pr_debug(cam_mod, "\n");
	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov5640_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov5640_TIMING_VTS_LOW_REG,
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

/*--------------------------------------------------------------------------*/

static int ov5640_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	ov_camera_module_pr_debug(cam_mod, "\n");
	if ((cam_mod->exp_config.exp_time + ov5640_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = cam_mod->exp_config.exp_time +
			ov5640_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;
	ret = ov_camera_module_write_reg(cam_mod,
		ov5640_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov5640_TIMING_VTS_HIGH_REG,
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

/*--------------------------------------------------------------------------*/

static int ov5640_write_aec(struct ov_camera_module *cam_mod)
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
	if (cam_mod->state == OV_CAMERA_MODULE_SW_STANDBY ||
	    cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time;

		a_gain = a_gain > 0x7ff ? 0x7ff : a_gain;
		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;
		exp_time = cam_mod->exp_config.exp_time << 4;
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
				ov5640_AEC_GROUP_UPDATE_ADDRESS,
				ov5640_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = ov5640_auto_adjust_fps(cam_mod,
						cam_mod->exp_config.exp_time);
		ret |= ov_camera_module_write_reg(cam_mod,
			ov5640_AEC_PK_LONG_GAIN_HIGH_REG,
			ov5640_FETCH_MSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov5640_AEC_PK_LONG_GAIN_LOW_REG,
			ov5640_FETCH_LSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov5640_AEC_PK_LONG_EXPO_3RD_REG,
			ov5640_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov5640_AEC_PK_LONG_EXPO_2ND_REG,
			ov5640_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov5640_AEC_PK_LONG_EXPO_1ST_REG,
			ov5640_FETCH_1ST_BYTE_EXP(exp_time));
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret = ov_camera_module_write_reg(cam_mod,
				ov5640_AEC_GROUP_UPDATE_ADDRESS,
				ov5640_AEC_GROUP_UPDATE_END_DATA);
			ret |= ov_camera_module_write_reg(cam_mod,
				ov5640_AEC_GROUP_UPDATE_ADDRESS,
				ov5640_AEC_GROUP_UPDATE_END_LAUNCH);
		}
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov5640_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
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

static int ov5640_filltimings(struct ov_camera_module_custom_config *custom)
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
			case ov5640_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case ov5640_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case ov5640_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case ov5640_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case ov5640_TIMING_X_INC:
				timings->binning_factor_x =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case ov5640_TIMING_Y_INC:
				timings->binning_factor_y =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case ov5640_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_start &
					0xff));
				break;
			case ov5640_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start =
					(reg_table[j].val |
					(timings->crop_horizontal_start &
					0xff00));
				break;
			case ov5640_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_start & 0xff));
				break;
			case ov5640_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start =
					((reg_table[j].val) |
					(timings->crop_vertical_start &
					0xff00));
				break;
			case ov5640_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_end & 0xff));
				break;
			case ov5640_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end =
					(reg_table[j].val |
					(timings->crop_horizontal_end &
					0xff00));
				break;
			case ov5640_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_end & 0xff));
				break;
			case ov5640_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end =
					(reg_table[j].val |
					(timings->crop_vertical_end & 0xff00));
				break;
			case ov5640_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case ov5640_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width &
					0xff00));
				break;
			case ov5640_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case ov5640_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height &
					0xff00));
				break;
			case ov5640_AEC_PK_LONG_EXPO_1ST_REG:
				timings->exp_time =
					((reg_table[j].val) |
					(timings->exp_time & 0xffff00));
				break;
			case ov5640_AEC_PK_LONG_EXPO_2ND_REG:
				timings->exp_time =
					((reg_table[j].val << 8) |
					(timings->exp_time & 0x00ff00));
				break;
			case ov5640_AEC_PK_LONG_EXPO_3RD_REG:
				timings->exp_time =
					(((reg_table[j].val & 0x0f) << 16) |
					(timings->exp_time & 0xff0000));
				break;
			case ov5640_AEC_PK_LONG_GAIN_LOW_REG:
				timings->gain =
					(reg_table[j].val |
					(timings->gain & 0x0700));
				break;
			case ov5640_AEC_PK_LONG_GAIN_HIGH_REG:
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
			ov5640_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			ov5640_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			ov5640_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			ov5640_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int ov5640_g_timings(struct ov_camera_module *cam_mod,
	struct ov_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	ov_camera_module_pr_debug(cam_mod, "\n");
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

static int ov5640_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov5640_write_aec(cam_mod);
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

static int ov5640_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	/* Handles only exposure and gain together special case. */
	if (ctrls->count == 1)
		ret = ov5640_s_ctrl(cam_mod, ctrls->ctrls[0].id);
	else if ((ctrls->count == 3) &&
		 ((ctrls->ctrls[0].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[1].id == V4L2_CID_EXPOSURE) ||
		  (ctrls->ctrls[1].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[0].id == V4L2_CID_EXPOSURE)))
		ret = ov5640_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov5640_set_flip(
	struct ov_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 match_reg[2];

	ov_camera_module_pr_debug(cam_mod, "\n");
	mode = ov_camera_module_get_flip_mirror(cam_mod);
	if (mode == -1) {
		ov_camera_module_pr_info(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		switch (cam_mod->active_config->frm_fmt.width) {
		case ov5640_FULL_SIZE_RESOLUTION_WIDTH:
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
		default:
				return 0;
		}

		for (i = len; i > 0; i--) {
			if (reglist[i].reg == ov5640_FLIP_REG)
				reglist[i].val = match_reg[0];
			else if (reglist[i].reg == ov5640_MIRROR_REG)
				reglist[i].val = match_reg[1];
		}
	}

	return 0;
}

static int ov5640_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, ov5640_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, ov5640_PIDL_ADDR, &pidl);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pidh == ov5640_PIDH_MAGIC && pidl == ov5640_PIDL_MAGIC) {
		ov_camera_module_pr_debug(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			ov5640_PIDH_MAGIC, ov5640_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov5640_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = ov5640_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov5640_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0x00);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */
static struct v4l2_subdev_core_ops ov5640_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov5640_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov5640_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov5640_camera_module_ops = {
	.core = &ov5640_camera_module_core_ops,
	.video = &ov5640_camera_module_video_ops,
	.pad = &ov5640_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov5640_custom_config = {
	.start_streaming = ov5640_start_streaming,
	.stop_streaming = ov5640_stop_streaming,
	.s_ctrl = ov5640_s_ctrl,
	.s_ext_ctrls = ov5640_s_ext_ctrls,
	.g_ctrl = ov5640_g_ctrl,
	.s_vts = ov5640_auto_adjust_fps,
	.g_timings = ov5640_g_timings,
	.check_camera_id = ov5640_check_camera_id,
	.set_flip = ov5640_set_flip,
	.configs = ov5640_configs,
	.num_configs = ARRAY_SIZE(ov5640_configs),
	.power_up_delays_ms = {20, 20, 0}

};

static int ov5640_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	ov5640_filltimings(&ov5640_custom_config);
	v4l2_i2c_subdev_init(&ov5640.sd, client, &ov5640_camera_module_ops);

	ov5640.custom = ov5640_custom_config;

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov5640_id[] = {
	{ ov5640_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov5640_of_match[] = {
	{.compatible = "omnivision,ov5640-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = ov5640_DRIVER_NAME,
		.of_match_table = ov5640_of_match
	},
	.probe = ov5640_probe,
	.remove = ov5640_remove,
	.id_table = ov5640_id,
};

module_i2c_driver(ov5640_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov5640");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
