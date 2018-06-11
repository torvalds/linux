/*
 * ov4689 sensor driver
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

#define ov4689_DRIVER_NAME "ov4689"

#define ov4689_AEC_PK_LONG_GAIN_HIGH_REG 0x3508	/* Bit 6-13 */
#define ov4689_AEC_PK_LONG_GAIN_LOW_REG	 0x3509	/* Bits 0 -5 */
#define ov4689_FETCH_LSB_GAIN(VAL) ((VAL) & 0x00ff)
#define ov4689_FETCH_MSB_GAIN(VAL) (((VAL) >> 8) & 0xff)

#define ov4689_AEC_PK_LONG_EXPO_3RD_REG 0x3500	/* Exposure Bits 16-19 */
#define ov4689_AEC_PK_LONG_EXPO_2ND_REG 0x3501	/* Exposure Bits 8-15 */
#define ov4689_AEC_PK_LONG_EXPO_1ST_REG 0x3502	/* Exposure Bits 0-7 */
#define ov4689_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 12) & 0xF)	/* 4 Bits */
#define ov4689_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 4) & 0xFF)	/* 8 Bits */
#define ov4689_FETCH_1ST_BYTE_EXP(VAL) (((VAL) & 0x0F) << 4)	/* 4 Bits */

#define ov4689_AEC_GROUP_UPDATE_ADDRESS		0x3208
#define ov4689_AEC_GROUP_UPDATE_START_DATA	0x00
#define ov4689_AEC_GROUP_UPDATE_END_DATA	0x10
#define ov4689_AEC_GROUP_UPDATE_END_LAUNCH	0xA0

#define ov4689_PIDH_ADDR    0x300A
#define ov4689_PIDL_ADDR	0x300B

#define ov4689_PIDH_MAGIC	0x46
#define ov4689_PIDL_MAGIC	0x88

#define ov4689_TIMING_VTS_HIGH_REG 0x380e
#define ov4689_TIMING_VTS_LOW_REG 0x380f
#define ov4689_TIMING_HTS_HIGH_REG 0x380c
#define ov4689_TIMING_HTS_LOW_REG 0x380d
#define ov4689_INTEGRATION_TIME_MARGIN 8
#define ov4689_FINE_INTG_TIME_MIN 0
#define ov4689_FINE_INTG_TIME_MAX_MARGIN 0
#define ov4689_COARSE_INTG_TIME_MIN 16
#define ov4689_COARSE_INTG_TIME_MAX_MARGIN 4
#define ov4689_TIMING_X_INC		0x3814
#define ov4689_TIMING_Y_INC		0x3815
#define ov4689_HORIZONTAL_START_HIGH_REG 0x3800
#define ov4689_HORIZONTAL_START_LOW_REG 0x3801
#define ov4689_VERTICAL_START_HIGH_REG 0x3802
#define ov4689_VERTICAL_START_LOW_REG 0x3803
#define ov4689_HORIZONTAL_END_HIGH_REG 0x3804
#define ov4689_HORIZONTAL_END_LOW_REG 0x3805
#define ov4689_VERTICAL_END_HIGH_REG 0x3806
#define ov4689_VERTICAL_END_LOW_REG 0x3807
#define ov4689_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define ov4689_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3809
#define ov4689_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x380a
#define ov4689_VERTICAL_OUTPUT_SIZE_LOW_REG 0x380b
#define ov4689_FLIP_REG                     0x3820
#define ov4689_MIRROR_REG					0x3821

#define ov4689_EXT_CLK 24000000

#define ov4689_FULL_SIZE_RESOLUTION_WIDTH 2688
#define ov4689_BINING_SIZE_RESOLUTION_WIDTH 1280

static struct ov_camera_module ov4689;
static struct ov_camera_module_custom_config ov4689_custom_config;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */

/* MCLK:24MHz  2688x1520  30fps   mipi 4lane   1008Mbps/lane */
static struct ov_camera_module_reg ov4689_init_tab_2688_1520_30fps[] = {
/* global setting */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0103, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3638, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0300, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0302, 0x2a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0303, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0304, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030d, 0x1e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030e, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030f, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0312, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x031e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3000, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x32},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3020, 0x93},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3021, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3031, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303f, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3305, 0xf1},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3307, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3309, 0x29},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3504, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3505, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3506, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3507, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3508, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350f, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3510, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3511, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3512, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3513, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3514, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3515, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3516, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3517, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3518, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3519, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351b, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3520, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3521, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3522, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3524, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3526, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3528, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x352a, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3602, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3603, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3604, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3605, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3606, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3607, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3609, 0x12},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x360a, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x360c, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x360f, 0xe5},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3608, 0x8f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3611, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3613, 0xf7},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3616, 0x58},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3619, 0x99},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x361b, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x361c, 0x7a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x361e, 0x79},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x361f, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3632, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3634, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3635, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x15},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3646, 0x86},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x364a, 0x0b},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3700, 0x17},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3701, 0x22},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3703, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370a, 0x37},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3706, 0x63},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0x3c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370b, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370c, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3710, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3711, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3716, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3720, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3729, 0x7b},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x372a, 0x84},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x372b, 0xbd},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x372c, 0xbc},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x372e, 0x52},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x373c, 0x0e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x373e, 0x33},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3743, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3744, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3745, 0xc0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x374a, 0x43},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x374c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x374e, 0x23},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3751, 0x7b},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3752, 0x84},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3753, 0xbd},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3754, 0xbc},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3756, 0x52},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x375c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3760, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3761, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3762, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3763, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3764, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3767, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3768, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3769, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x376a, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x376b, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x376c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x376d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x376e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3773, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3774, 0x51},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3776, 0xbd},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3777, 0xbd},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3781, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3783, 0x25},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3798, 0x1b},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x97},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0xfb},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0xf0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x12},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3819, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3829, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382a, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382b, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382d, 0x7f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3830, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3836, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3837, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3841, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3846, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3847, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3d85, 0x36},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3d8c, 0x71},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3d8d, 0xcb},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3f0a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4000, 0xf1},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4001, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4002, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4003, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x400e, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4011, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401c, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4020, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4021, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4022, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4023, 0xcf},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4024, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4025, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4026, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4027, 0x6f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4028, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4029, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402a, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402b, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402c, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402d, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402e, 0x0e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x402f, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4302, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4303, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4304, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4305, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4306, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4308, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4500, 0x6c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4501, 0xc4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4502, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4503, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4601, 0xa7},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4800, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4813, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x481f, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4829, 0x78},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4b00, 0x2a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4b0d, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d00, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d01, 0x42},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d02, 0xd1},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d03, 0x93},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d04, 0xf5},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4d05, 0xc1},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0xf3},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0x11},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5004, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x500a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x500b, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5032, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5040, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5050, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5500, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5501, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5502, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5503, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8000, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8001, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8002, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8003, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8004, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8005, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8006, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8007, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x8008, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3638, 0x00},
/* {OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x01} */
};

static struct ov_camera_module_config ov4689_configs[] = {
	{
		.name = "2688x1520_30fps",
		.frm_fmt = {
			.width = 2688,
			.height = 1520,
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
		.reg_table = (void *)ov4689_init_tab_2688_1520_30fps,
		.reg_table_num_entries =
			sizeof(ov4689_init_tab_2688_1520_30fps) /
			sizeof(ov4689_init_tab_2688_1520_30fps[0]),
		.v_blanking_time_us = 5000,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 999, ov4689_EXT_CLK)
	}
};

static int ov4689_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov4689_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		ov4689_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov4689_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((exp_time + ov4689_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = exp_time + ov4689_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;
	ret = ov_camera_module_write_reg(cam_mod,
		ov4689_TIMING_VTS_LOW_REG, vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov4689_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod,
			"updated vts = %d,vts_min=%d\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int ov4689_set_vts(struct ov_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts < cam_mod->vts_min)
		return ret;

	if (vts > 0xfff)
		vts = 0xfff;

	ret = ov_camera_module_write_reg(cam_mod,
		ov4689_TIMING_VTS_LOW_REG, vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov4689_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod, "updated vts=%d,vts_min=%d\n", vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}
	return ret;
}

static int ov4689_write_aec(struct ov_camera_module *cam_mod)
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
		u32 exp_time = cam_mod->exp_config.exp_time;

		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		mutex_lock(&cam_mod->lock);
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_GROUP_UPDATE_ADDRESS,
			ov4689_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = ov4689_auto_adjust_fps(cam_mod,
						cam_mod->exp_config.exp_time);
		ret |= ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_PK_LONG_GAIN_HIGH_REG,
			ov4689_FETCH_MSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_PK_LONG_GAIN_LOW_REG,
			ov4689_FETCH_LSB_GAIN(a_gain));
		ret = ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_PK_LONG_EXPO_3RD_REG,
			ov4689_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_PK_LONG_EXPO_2ND_REG,
			ov4689_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov4689_AEC_PK_LONG_EXPO_1ST_REG,
			ov4689_FETCH_1ST_BYTE_EXP(exp_time));
		if (!cam_mod->auto_adjust_fps)
			ret |= ov4689_set_vts(cam_mod, cam_mod->exp_config.vts_value);
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret = ov_camera_module_write_reg(cam_mod,
				ov4689_AEC_GROUP_UPDATE_ADDRESS,
				ov4689_AEC_GROUP_UPDATE_END_DATA);
			ret = ov_camera_module_write_reg(cam_mod,
				ov4689_AEC_GROUP_UPDATE_ADDRESS,
				ov4689_AEC_GROUP_UPDATE_END_LAUNCH);
		}
		mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov4689_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
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

static int ov4689_filltimings(struct ov_camera_module_custom_config *custom)
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

		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case ov4689_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					reg_table[j].val << 8;
				break;
			case ov4689_TIMING_VTS_LOW_REG:
				timings->frame_length_lines |= reg_table[j].val;
				break;
			case ov4689_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					(reg_table[j].val << 8);
				break;
			case ov4689_TIMING_HTS_LOW_REG:
				timings->line_length_pck |= reg_table[j].val;
				break;
			case ov4689_TIMING_X_INC:
				timings->binning_factor_x =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case ov4689_TIMING_Y_INC:
				timings->binning_factor_y =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case ov4689_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					reg_table[j].val << 8;
				break;
			case ov4689_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start |=
					reg_table[j].val;
				break;
			case ov4689_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					reg_table[j].val << 8;
				break;
			case ov4689_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start |=
					reg_table[j].val;
				break;
			case ov4689_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					reg_table[j].val << 8;
				break;
			case ov4689_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end |=
					reg_table[j].val;
				break;
			case ov4689_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					reg_table[j].val << 8;
				break;
			case ov4689_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end |= reg_table[j].val;
				break;
			case ov4689_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					reg_table[j].val << 8;
				break;
			case ov4689_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width |=
					reg_table[j].val;
				break;
			case ov4689_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					reg_table[j].val << 8;
				break;
			case ov4689_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height |=
					reg_table[j].val;
				break;
			}
		}

		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator *
				timings->frame_length_lines *
				timings->line_length_pck;

		timings->coarse_integration_time_min =
			ov4689_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			ov4689_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min = ov4689_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
				ov4689_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int ov4689_g_timings(struct ov_camera_module *cam_mod,
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
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov4689_set_flip(
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
		case ov4689_FULL_SIZE_RESOLUTION_WIDTH:
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
		case ov4689_BINING_SIZE_RESOLUTION_WIDTH:
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
			if (reglist[i].reg == ov4689_FLIP_REG)
				reglist[i].val = match_reg[0];
			else if (reglist[i].reg == ov4689_MIRROR_REG)
				reglist[i].val = match_reg[1];
		}
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int ov4689_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov4689_write_aec(cam_mod);
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

/*--------------------------------------------------------------------------*/

static int ov4689_s_ext_ctrls(struct ov_camera_module *cam_mod,
	struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = ov4689_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov4689_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	ret = ov4689_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 1);
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

static int ov4689_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0);
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
static int ov4689_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, ov4689_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, ov4689_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == ov4689_PIDH_MAGIC) && (pidl == ov4689_PIDL_MAGIC))
		ov_camera_module_pr_debug(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			ov4689_PIDH_MAGIC, ov4689_PIDL_MAGIC, pidh, pidl);
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

static struct v4l2_subdev_core_ops ov4689_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov4689_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.g_frame_interval = ov_camera_module_g_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov4689_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov4689_camera_module_ops = {
	.core = &ov4689_camera_module_core_ops,
	.video = &ov4689_camera_module_video_ops,
	.pad = &ov4689_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov4689_custom_config = {
	.start_streaming = ov4689_start_streaming,
	.stop_streaming = ov4689_stop_streaming,
	.s_ctrl = ov4689_s_ctrl,
	.s_ext_ctrls = ov4689_s_ext_ctrls,
	.g_ctrl = ov4689_g_ctrl,
	.g_timings = ov4689_g_timings,
	.check_camera_id = ov4689_check_camera_id,
	.set_flip = ov4689_set_flip,
	.s_vts = ov4689_auto_adjust_fps,
	.configs = ov4689_configs,
	.num_configs = ARRAY_SIZE(ov4689_configs),
	.power_up_delays_ms = {5, 20, 0},
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	.exposure_valid_frame = {4, 4}
};

static int ov4689_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	ov4689_filltimings(&ov4689_custom_config);
	v4l2_i2c_subdev_init(&ov4689.sd, client, &ov4689_camera_module_ops);
	ov4689.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov4689.custom = ov4689_custom_config;

	mutex_init(&ov4689.lock);
	dev_info(&client->dev, "probing successful\n");
	return 0;
}

static int ov4689_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	mutex_destroy(&cam_mod->lock);
	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov4689_id[] = {
	{ ov4689_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov4689_of_match[] = {
	{.compatible = "omnivision,ov4689-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov4689_id);

static struct i2c_driver ov4689_i2c_driver = {
	.driver = {
		.name = ov4689_DRIVER_NAME,
		.of_match_table = ov4689_of_match
	},
	.probe = ov4689_probe,
	.remove = ov4689_remove,
	.id_table = ov4689_id,
};

module_i2c_driver(ov4689_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov4689");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
