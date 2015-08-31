/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include "mxc_v4l2_capture.h"

#define OV5640_VOLTAGE_ANALOG               2800000
#define OV5640_VOLTAGE_DIGITAL_CORE         1500000
#define OV5640_VOLTAGE_DIGITAL_IO           1800000

#define MIN_FPS 15
#define MAX_FPS 30
#define DEFAULT_FPS 30

#define OV5640_XCLK_MIN 6000000
#define OV5640_XCLK_MAX 24000000

#define OV5640_CHIP_ID_HIGH_BYTE        0x300A
#define OV5640_CHIP_ID_LOW_BYTE         0x300B

enum ov5640_mode {
	ov5640_mode_MIN = 0,
	ov5640_mode_VGA_640_480 = 0,
	ov5640_mode_QVGA_320_240 = 1,
	ov5640_mode_NTSC_720_480 = 2,
	ov5640_mode_PAL_720_576 = 3,
	ov5640_mode_720P_1280_720 = 4,
	ov5640_mode_1080P_1920_1080 = 5,
	ov5640_mode_QSXGA_2592_1944 = 6,
	ov5640_mode_QCIF_176_144 = 7,
	ov5640_mode_XGA_1024_768 = 8,
	ov5640_mode_MAX = 8
};

enum ov5640_frame_rate {
	ov5640_15_fps,
	ov5640_30_fps
};

static int ov5640_framerates[] = {
	[ov5640_15_fps] = 15,
	[ov5640_30_fps] = 30,
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

struct ov5640_mode_info {
	enum ov5640_mode mode;
	u32 width;
	u32 height;
	struct reg_value *init_data_ptr;
	u32 init_data_size;
};

/*!
 * Maintains the information on the current state of the sesor.
 */
static struct sensor_data ov5640_data;
static int pwn_gpio, rst_gpio;
static int prev_sysclk;
static int AE_Target = 52, night_mode;
static int prev_HTS;
static int AE_high, AE_low;

static struct reg_value ov5640_global_init_setting[] = {
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0}, {0x3017, 0xff, 0, 0}, {0x3018, 0xff, 0, 0},
	{0x3034, 0x1a, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3108, 0x01, 0, 0},
	{0x3630, 0x36, 0, 0}, {0x3631, 0x0e, 0, 0}, {0x3632, 0xe2, 0, 0},
	{0x3633, 0x12, 0, 0}, {0x3621, 0xe0, 0, 0}, {0x3704, 0xa0, 0, 0},
	{0x3703, 0x5a, 0, 0}, {0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3905, 0x02, 0, 0},
	{0x3906, 0x10, 0, 0}, {0x3901, 0x0a, 0, 0}, {0x3731, 0x12, 0, 0},
	{0x3600, 0x08, 0, 0}, {0x3601, 0x33, 0, 0}, {0x302d, 0x60, 0, 0},
	{0x3620, 0x52, 0, 0}, {0x371b, 0x20, 0, 0}, {0x471c, 0x50, 0, 0},
	{0x3a13, 0x43, 0, 0}, {0x3a18, 0x00, 0, 0}, {0x3a19, 0x7c, 0, 0},
	{0x3635, 0x13, 0, 0}, {0x3636, 0x03, 0, 0}, {0x3634, 0x40, 0, 0},
	{0x3622, 0x01, 0, 0}, {0x3c01, 0x34, 0, 0}, {0x3c04, 0x28, 0, 0},
	{0x3c05, 0x98, 0, 0}, {0x3c06, 0x00, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3c08, 0x00, 0, 0}, {0x3c09, 0x1c, 0, 0}, {0x3c0a, 0x9c, 0, 0},
	{0x3c0b, 0x40, 0, 0}, {0x3810, 0x00, 0, 0}, {0x3811, 0x10, 0, 0},
	{0x3812, 0x00, 0, 0}, {0x3708, 0x64, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4005, 0x1a, 0, 0}, {0x3000, 0x00, 0, 0}, {0x3004, 0xff, 0, 0},
	{0x300e, 0x58, 0, 0}, {0x302e, 0x00, 0, 0}, {0x4300, 0x30, 0, 0},
	{0x501f, 0x00, 0, 0}, {0x440e, 0x00, 0, 0}, {0x5000, 0xa7, 0, 0},
	{0x3008, 0x02, 0, 0},
};

static struct reg_value ov5640_init_setting_30fps_VGA[] = {
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0}, {0x3017, 0xff, 0, 0}, {0x3018, 0xff, 0, 0},
	{0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0}, {0x3036, 0x46, 0, 0},
	{0x3037, 0x13, 0, 0}, {0x3108, 0x01, 0, 0}, {0x3630, 0x36, 0, 0},
	{0x3631, 0x0e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x12, 0, 0},
	{0x3621, 0xe0, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x370b, 0x60, 0, 0},
	{0x3705, 0x1a, 0, 0}, {0x3905, 0x02, 0, 0}, {0x3906, 0x10, 0, 0},
	{0x3901, 0x0a, 0, 0}, {0x3731, 0x12, 0, 0}, {0x3600, 0x08, 0, 0},
	{0x3601, 0x33, 0, 0}, {0x302d, 0x60, 0, 0}, {0x3620, 0x52, 0, 0},
	{0x371b, 0x20, 0, 0}, {0x471c, 0x50, 0, 0}, {0x3a13, 0x43, 0, 0},
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3635, 0x13, 0, 0},
	{0x3636, 0x03, 0, 0}, {0x3634, 0x40, 0, 0}, {0x3622, 0x01, 0, 0},
	{0x3c01, 0x34, 0, 0}, {0x3c04, 0x28, 0, 0}, {0x3c05, 0x98, 0, 0},
	{0x3c06, 0x00, 0, 0}, {0x3c07, 0x08, 0, 0}, {0x3c08, 0x00, 0, 0},
	{0x3c09, 0x1c, 0, 0}, {0x3c0a, 0x9c, 0, 0}, {0x3c0b, 0x40, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x9b, 0, 0},
	{0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x01, 0, 0},
	{0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x68, 0, 0},
	{0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0}, {0x3810, 0x00, 0, 0},
	{0x3811, 0x10, 0, 0}, {0x3812, 0x00, 0, 0}, {0x3813, 0x06, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3708, 0x64, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x03, 0, 0},
	{0x3a03, 0xd8, 0, 0}, {0x3a08, 0x01, 0, 0}, {0x3a09, 0x27, 0, 0},
	{0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0}, {0x3a0e, 0x03, 0, 0},
	{0x3a0d, 0x04, 0, 0}, {0x3a14, 0x03, 0, 0}, {0x3a15, 0xd8, 0, 0},
	{0x4001, 0x02, 0, 0}, {0x4004, 0x02, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3004, 0xff, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x300e, 0x58, 0, 0}, {0x302e, 0x00, 0, 0}, {0x4300, 0x30, 0, 0},
	{0x501f, 0x00, 0, 0}, {0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0},
	{0x440e, 0x00, 0, 0}, {0x460b, 0x35, 0, 0}, {0x460c, 0x22, 0, 0},
	{0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0}, {0x5000, 0xa7, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x5180, 0xff, 0, 0}, {0x5181, 0xf2, 0, 0},
	{0x5182, 0x00, 0, 0}, {0x5183, 0x14, 0, 0}, {0x5184, 0x25, 0, 0},
	{0x5185, 0x24, 0, 0}, {0x5186, 0x09, 0, 0}, {0x5187, 0x09, 0, 0},
	{0x5188, 0x09, 0, 0}, {0x5189, 0x88, 0, 0}, {0x518a, 0x54, 0, 0},
	{0x518b, 0xee, 0, 0}, {0x518c, 0xb2, 0, 0}, {0x518d, 0x50, 0, 0},
	{0x518e, 0x34, 0, 0}, {0x518f, 0x6b, 0, 0}, {0x5190, 0x46, 0, 0},
	{0x5191, 0xf8, 0, 0}, {0x5192, 0x04, 0, 0}, {0x5193, 0x70, 0, 0},
	{0x5194, 0xf0, 0, 0}, {0x5195, 0xf0, 0, 0}, {0x5196, 0x03, 0, 0},
	{0x5197, 0x01, 0, 0}, {0x5198, 0x04, 0, 0}, {0x5199, 0x6c, 0, 0},
	{0x519a, 0x04, 0, 0}, {0x519b, 0x00, 0, 0}, {0x519c, 0x09, 0, 0},
	{0x519d, 0x2b, 0, 0}, {0x519e, 0x38, 0, 0}, {0x5381, 0x1e, 0, 0},
	{0x5382, 0x5b, 0, 0}, {0x5383, 0x08, 0, 0}, {0x5384, 0x0a, 0, 0},
	{0x5385, 0x7e, 0, 0}, {0x5386, 0x88, 0, 0}, {0x5387, 0x7c, 0, 0},
	{0x5388, 0x6c, 0, 0}, {0x5389, 0x10, 0, 0}, {0x538a, 0x01, 0, 0},
	{0x538b, 0x98, 0, 0}, {0x5300, 0x08, 0, 0}, {0x5301, 0x30, 0, 0},
	{0x5302, 0x10, 0, 0}, {0x5303, 0x00, 0, 0}, {0x5304, 0x08, 0, 0},
	{0x5305, 0x30, 0, 0}, {0x5306, 0x08, 0, 0}, {0x5307, 0x16, 0, 0},
	{0x5309, 0x08, 0, 0}, {0x530a, 0x30, 0, 0}, {0x530b, 0x04, 0, 0},
	{0x530c, 0x06, 0, 0}, {0x5480, 0x01, 0, 0}, {0x5481, 0x08, 0, 0},
	{0x5482, 0x14, 0, 0}, {0x5483, 0x28, 0, 0}, {0x5484, 0x51, 0, 0},
	{0x5485, 0x65, 0, 0}, {0x5486, 0x71, 0, 0}, {0x5487, 0x7d, 0, 0},
	{0x5488, 0x87, 0, 0}, {0x5489, 0x91, 0, 0}, {0x548a, 0x9a, 0, 0},
	{0x548b, 0xaa, 0, 0}, {0x548c, 0xb8, 0, 0}, {0x548d, 0xcd, 0, 0},
	{0x548e, 0xdd, 0, 0}, {0x548f, 0xea, 0, 0}, {0x5490, 0x1d, 0, 0},
	{0x5580, 0x02, 0, 0}, {0x5583, 0x40, 0, 0}, {0x5584, 0x10, 0, 0},
	{0x5589, 0x10, 0, 0}, {0x558a, 0x00, 0, 0}, {0x558b, 0xf8, 0, 0},
	{0x5800, 0x23, 0, 0}, {0x5801, 0x14, 0, 0}, {0x5802, 0x0f, 0, 0},
	{0x5803, 0x0f, 0, 0}, {0x5804, 0x12, 0, 0}, {0x5805, 0x26, 0, 0},
	{0x5806, 0x0c, 0, 0}, {0x5807, 0x08, 0, 0}, {0x5808, 0x05, 0, 0},
	{0x5809, 0x05, 0, 0}, {0x580a, 0x08, 0, 0}, {0x580b, 0x0d, 0, 0},
	{0x580c, 0x08, 0, 0}, {0x580d, 0x03, 0, 0}, {0x580e, 0x00, 0, 0},
	{0x580f, 0x00, 0, 0}, {0x5810, 0x03, 0, 0}, {0x5811, 0x09, 0, 0},
	{0x5812, 0x07, 0, 0}, {0x5813, 0x03, 0, 0}, {0x5814, 0x00, 0, 0},
	{0x5815, 0x01, 0, 0}, {0x5816, 0x03, 0, 0}, {0x5817, 0x08, 0, 0},
	{0x5818, 0x0d, 0, 0}, {0x5819, 0x08, 0, 0}, {0x581a, 0x05, 0, 0},
	{0x581b, 0x06, 0, 0}, {0x581c, 0x08, 0, 0}, {0x581d, 0x0e, 0, 0},
	{0x581e, 0x29, 0, 0}, {0x581f, 0x17, 0, 0}, {0x5820, 0x11, 0, 0},
	{0x5821, 0x11, 0, 0}, {0x5822, 0x15, 0, 0}, {0x5823, 0x28, 0, 0},
	{0x5824, 0x46, 0, 0}, {0x5825, 0x26, 0, 0}, {0x5826, 0x08, 0, 0},
	{0x5827, 0x26, 0, 0}, {0x5828, 0x64, 0, 0}, {0x5829, 0x26, 0, 0},
	{0x582a, 0x24, 0, 0}, {0x582b, 0x22, 0, 0}, {0x582c, 0x24, 0, 0},
	{0x582d, 0x24, 0, 0}, {0x582e, 0x06, 0, 0}, {0x582f, 0x22, 0, 0},
	{0x5830, 0x40, 0, 0}, {0x5831, 0x42, 0, 0}, {0x5832, 0x24, 0, 0},
	{0x5833, 0x26, 0, 0}, {0x5834, 0x24, 0, 0}, {0x5835, 0x22, 0, 0},
	{0x5836, 0x22, 0, 0}, {0x5837, 0x26, 0, 0}, {0x5838, 0x44, 0, 0},
	{0x5839, 0x24, 0, 0}, {0x583a, 0x26, 0, 0}, {0x583b, 0x28, 0, 0},
	{0x583c, 0x42, 0, 0}, {0x583d, 0xce, 0, 0}, {0x5025, 0x00, 0, 0},
	{0x3a0f, 0x30, 0, 0}, {0x3a10, 0x28, 0, 0}, {0x3a1b, 0x30, 0, 0},
	{0x3a1e, 0x26, 0, 0}, {0x3a11, 0x60, 0, 0}, {0x3a1f, 0x14, 0, 0},
	{0x3008, 0x02, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_30fps_VGA_640_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3503, 0x00, 0, 0},
};

static struct reg_value ov5640_setting_15fps_VGA_640_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3503, 0x00, 0, 0},
};

static struct reg_value ov5640_setting_30fps_QVGA_320_240[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x01, 0, 0}, {0x3809, 0x40, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0xf0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_QVGA_320_240[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x01, 0, 0}, {0x3809, 0x40, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0xf0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_30fps_NTSC_720_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0},
	{0x3807, 0xd4, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_NTSC_720_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0},
	{0x3807, 0xd4, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_30fps_PAL_720_576[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x60, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x09, 0, 0}, {0x3805, 0x7e, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x02, 0, 0}, {0x380b, 0x40, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_PAL_720_576[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x60, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x09, 0, 0}, {0x3805, 0x7e, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x02, 0, 0}, {0x380b, 0x40, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_30fps_720P_1280_720[] = {
	{0x3035, 0x21, 0, 0}, {0x3036, 0x69, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0xfa, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0}, {0x3807, 0xa9, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x64, 0, 0},
	{0x380e, 0x02, 0, 0}, {0x380f, 0xe4, 0, 0}, {0x3813, 0x04, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x370c, 0x03, 0, 0}, {0x3a02, 0x02, 0, 0}, {0x3a03, 0xe0, 0, 0},
	{0x3a14, 0x02, 0, 0}, {0x3a15, 0xe0, 0, 0}, {0x4004, 0x02, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0}, {0x4713, 0x03, 0, 0},
	{0x4407, 0x04, 0, 0}, {0x460b, 0x37, 0, 0}, {0x460c, 0x20, 0, 0},
	{0x4837, 0x16, 0, 0}, {0x3824, 0x04, 0, 0}, {0x5001, 0x83, 0, 0},
	{0x3503, 0x00, 0, 0},
};

static struct reg_value ov5640_setting_15fps_720P_1280_720[] = {
	{0x3035, 0x41, 0, 0}, {0x3036, 0x69, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0xfa, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0}, {0x3807, 0xa9, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x64, 0, 0},
	{0x380e, 0x02, 0, 0}, {0x380f, 0xe4, 0, 0}, {0x3813, 0x04, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x370c, 0x03, 0, 0}, {0x3a02, 0x02, 0, 0}, {0x3a03, 0xe0, 0, 0},
	{0x3a14, 0x02, 0, 0}, {0x3a15, 0xe0, 0, 0}, {0x4004, 0x02, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0}, {0x4713, 0x03, 0, 0},
	{0x4407, 0x04, 0, 0}, {0x460b, 0x37, 0, 0}, {0x460c, 0x20, 0, 0},
	{0x4837, 0x16, 0, 0}, {0x3824, 0x04, 0, 0}, {0x5001, 0x83, 0, 0},
	{0x3503, 0x00, 0, 0},
};

static struct reg_value ov5640_setting_30fps_QCIF_176_144[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x00, 0, 0}, {0x3809, 0xb0, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0x90, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_QCIF_176_144[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x00, 0, 0}, {0x3809, 0xb0, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0x90, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_30fps_XGA_1024_768[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x04, 0, 0}, {0x3809, 0x00, 0, 0},
	{0x380a, 0x03, 0, 0}, {0x380b, 0x00, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_XGA_1024_768[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x04, 0, 0}, {0x3809, 0x00, 0, 0},
	{0x380a, 0x03, 0, 0}, {0x380b, 0x00, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};


static struct reg_value ov5640_setting_15fps_1080P_1920_1080[] = {
	{0x3c07, 0x07, 0, 0}, {0x3820, 0x40, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0xee, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x05, 0, 0},
	{0x3807, 0xc3, 0, 0}, {0x3808, 0x07, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x04, 0, 0}, {0x380b, 0x38, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x1c, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb0, 0, 0},
	{0x3813, 0x04, 0, 0}, {0x3618, 0x04, 0, 0}, {0x3612, 0x2b, 0, 0},
	{0x3709, 0x12, 0, 0}, {0x370c, 0x00, 0, 0}, {0x3a02, 0x07, 0, 0},
	{0x3a03, 0xae, 0, 0}, {0x3a14, 0x07, 0, 0}, {0x3a15, 0xae, 0, 0},
	{0x4004, 0x06, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x02, 0, 0}, {0x4407, 0x0c, 0, 0}, {0x460b, 0x37, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x2c, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0x83, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct reg_value ov5640_setting_15fps_QSXGA_2592_1944[] = {
	{0x3c07, 0x07, 0, 0}, {0x3820, 0x40, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x00, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9f, 0, 0}, {0x3808, 0x0a, 0, 0}, {0x3809, 0x20, 0, 0},
	{0x380a, 0x07, 0, 0}, {0x380b, 0x98, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x1c, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb0, 0, 0},
	{0x3813, 0x04, 0, 0}, {0x3618, 0x04, 0, 0}, {0x3612, 0x2b, 0, 0},
	{0x3709, 0x12, 0, 0}, {0x370c, 0x00, 0, 0}, {0x3a02, 0x07, 0, 0},
	{0x3a03, 0xae, 0, 0}, {0x3a14, 0x07, 0, 0}, {0x3a15, 0xae, 0, 0},
	{0x4004, 0x06, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x02, 0, 0}, {0x4407, 0x0c, 0, 0}, {0x460b, 0x37, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x2c, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0x83, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static struct ov5640_mode_info ov5640_mode_info_data[2][ov5640_mode_MAX + 1] = {
	{
		{ov5640_mode_VGA_640_480,      640,  480,
		ov5640_setting_15fps_VGA_640_480,
		ARRAY_SIZE(ov5640_setting_15fps_VGA_640_480)},
		{ov5640_mode_QVGA_320_240,     320,  240,
		ov5640_setting_15fps_QVGA_320_240,
		ARRAY_SIZE(ov5640_setting_15fps_QVGA_320_240)},
		{ov5640_mode_NTSC_720_480,     720,  480,
		ov5640_setting_15fps_NTSC_720_480,
		ARRAY_SIZE(ov5640_setting_15fps_NTSC_720_480)},
		{ov5640_mode_PAL_720_576,      720,  576,
		ov5640_setting_15fps_PAL_720_576,
		ARRAY_SIZE(ov5640_setting_15fps_PAL_720_576)},
		{ov5640_mode_720P_1280_720,   1280,  720,
		ov5640_setting_15fps_720P_1280_720,
		ARRAY_SIZE(ov5640_setting_15fps_720P_1280_720)},
		{ov5640_mode_1080P_1920_1080, 1920, 1080,
		ov5640_setting_15fps_1080P_1920_1080,
		ARRAY_SIZE(ov5640_setting_15fps_1080P_1920_1080)},
		{ov5640_mode_QSXGA_2592_1944, 2592, 1944,
		ov5640_setting_15fps_QSXGA_2592_1944,
		ARRAY_SIZE(ov5640_setting_15fps_QSXGA_2592_1944)},
		{ov5640_mode_QCIF_176_144,     176,  144,
		ov5640_setting_15fps_QCIF_176_144,
		ARRAY_SIZE(ov5640_setting_15fps_QCIF_176_144)},
		{ov5640_mode_XGA_1024_768,    1024,  768,
		ov5640_setting_15fps_XGA_1024_768,
		ARRAY_SIZE(ov5640_setting_15fps_XGA_1024_768)},
	},
	{
		{ov5640_mode_VGA_640_480,      640,  480,
		ov5640_setting_30fps_VGA_640_480,
		ARRAY_SIZE(ov5640_setting_30fps_VGA_640_480)},
		{ov5640_mode_QVGA_320_240,     320,  240,
		ov5640_setting_30fps_QVGA_320_240,
		ARRAY_SIZE(ov5640_setting_30fps_QVGA_320_240)},
		{ov5640_mode_NTSC_720_480,     720,  480,
		ov5640_setting_30fps_NTSC_720_480,
		ARRAY_SIZE(ov5640_setting_30fps_NTSC_720_480)},
		{ov5640_mode_PAL_720_576,      720,  576,
		ov5640_setting_30fps_PAL_720_576,
		ARRAY_SIZE(ov5640_setting_30fps_PAL_720_576)},
		{ov5640_mode_720P_1280_720,   1280,  720,
		ov5640_setting_30fps_720P_1280_720,
		ARRAY_SIZE(ov5640_setting_30fps_720P_1280_720)},
		{ov5640_mode_1080P_1920_1080, 0, 0, NULL, 0},
		{ov5640_mode_QSXGA_2592_1944, 0, 0, NULL, 0},
		{ov5640_mode_QCIF_176_144,     176,  144,
		ov5640_setting_30fps_QCIF_176_144,
		ARRAY_SIZE(ov5640_setting_30fps_QCIF_176_144)},
		{ov5640_mode_XGA_1024_768,    1024,  768,
		ov5640_setting_30fps_XGA_1024_768,
		ARRAY_SIZE(ov5640_setting_30fps_XGA_1024_768)},
	},
};

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;

static int ov5640_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int ov5640_remove(struct i2c_client *client);

static s32 ov5640_read_reg(u16 reg, u8 *val);
static s32 ov5640_write_reg(u16 reg, u8 val);

static const struct i2c_device_id ov5640_id[] = {
	{"ov564x", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "ov564x",
		  },
	.probe  = ov5640_probe,
	.remove = ov5640_remove,
	.id_table = ov5640_id,
};

static inline void ov5640_power_down(int enable)
{
	gpio_set_value(pwn_gpio, enable);

	msleep(2);
}

static inline void ov5640_reset(void)
{
	/* camera reset */
	gpio_set_value(rst_gpio, 1);

	/* camera power down */
	gpio_set_value(pwn_gpio, 1);
	msleep(5);
	gpio_set_value(pwn_gpio, 0);
	msleep(5);
	gpio_set_value(rst_gpio, 0);
	msleep(1);
	gpio_set_value(rst_gpio, 1);
	msleep(5);
	gpio_set_value(pwn_gpio, 1);
}

static int ov5640_regulator_enable(struct device *dev)
{
	int ret = 0;

	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      OV5640_VOLTAGE_DIGITAL_IO,
				      OV5640_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(io_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		io_regulator = NULL;
		dev_warn(dev, "cannot get io voltage\n");
	}

	core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(core_regulator)) {
		regulator_set_voltage(core_regulator,
				      OV5640_VOLTAGE_DIGITAL_CORE,
				      OV5640_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(core_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		core_regulator = NULL;
		dev_warn(dev, "cannot get core voltage\n");
	}

	analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      OV5640_VOLTAGE_ANALOG,
				      OV5640_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		analog_regulator = NULL;
		dev_warn(dev, "cannot get analog voltage\n");
	}

	return ret;
}

static s32 ov5640_write_reg(u16 reg, u8 val)
{
	u8 au8Buf[3] = {0};

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(ov5640_data.i2c_client, au8Buf, 3) < 0) {
		pr_err("%s:write reg error:reg=%x,val=%x\n",
			__func__, reg, val);
		return -1;
	}

	return 0;
}

static s32 ov5640_read_reg(u16 reg, u8 *val)
{
	u8 au8RegBuf[2] = {0};
	u8 u8RdVal = 0;

	au8RegBuf[0] = reg >> 8;
	au8RegBuf[1] = reg & 0xff;

	if (2 != i2c_master_send(ov5640_data.i2c_client, au8RegBuf, 2)) {
		pr_err("%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
	}

	if (1 != i2c_master_recv(ov5640_data.i2c_client, &u8RdVal, 1)) {
		pr_err("%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, u8RdVal);
		return -1;
	}

	*val = u8RdVal;

	return u8RdVal;
}

static void ov5640_soft_reset(void)
{
	/* sysclk from pad */
	ov5640_write_reg(0x3103, 0x11);

	/* software reset */
	ov5640_write_reg(0x3008, 0x82);

	/* delay at least 5ms */
	msleep(10);
}

/* set sensor driver capability
 * 0x302c[7:6] - strength
	00     - 1x
	01     - 2x
	10     - 3x
	11     - 4x
 */
static int ov5640_driver_capability(int strength)
{
	u8 temp = 0;

	if (strength > 4 || strength < 1) {
		pr_err("The valid driver capability of ov5640 is 1x~4x\n");
		return -EINVAL;
	}

	ov5640_read_reg(0x302c, &temp);

	temp &= ~0xc0;	/* clear [7:6] */
	temp |= ((strength - 1) << 6);	/* set [7:6] */

	ov5640_write_reg(0x302c, temp);

	return 0;
}

/* calculate sysclk */
static int ov5640_get_sysclk(void)
{
	int xvclk = ov5640_data.mclk / 10000;
	int sysclk;
	int temp1, temp2;
	int Multiplier, PreDiv, VCO, SysDiv, Pll_rdiv, Bit_div2x, sclk_rdiv;
	int sclk_rdiv_map[] = {1, 2, 4, 8};
	u8 regval = 0;

	temp1 = ov5640_read_reg(0x3034, &regval);
	temp2 = temp1 & 0x0f;
	if (temp2 == 8 || temp2 == 10) {
		Bit_div2x = temp2 / 2;
	} else {
		pr_err("ov5640: unsupported bit mode %d\n", temp2);
		return -1;
	}

	temp1 = ov5640_read_reg(0x3035, &regval);
	SysDiv = temp1 >> 4;
	if (SysDiv == 0)
		SysDiv = 16;

	temp1 = ov5640_read_reg(0x3036, &regval);
	Multiplier = temp1;
	temp1 = ov5640_read_reg(0x3037, &regval);
	PreDiv = temp1 & 0x0f;
	Pll_rdiv = ((temp1 >> 4) & 0x01) + 1;

	temp1 = ov5640_read_reg(0x3108, &regval);
	temp2 = temp1 & 0x03;

	sclk_rdiv = sclk_rdiv_map[temp2];
	VCO = xvclk * Multiplier / PreDiv;
	sysclk = VCO / SysDiv / Pll_rdiv * 2 / Bit_div2x / sclk_rdiv;

	return sysclk;
}

/* read HTS from register settings */
static int ov5640_get_HTS(void)
{
	int HTS;
	u8 temp = 0;

	HTS = ov5640_read_reg(0x380c, &temp);
	HTS = (HTS<<8) + ov5640_read_reg(0x380d, &temp);
	return HTS;
}

/* read VTS from register settings */
static int ov5640_get_VTS(void)
{
	int VTS;
	u8 temp = 0;

	VTS = ov5640_read_reg(0x380e, &temp);
	VTS = (VTS<<8) + ov5640_read_reg(0x380f, &temp);

	return VTS;
}

/* write VTS to registers */
static int ov5640_set_VTS(int VTS)
{
	int temp;

	temp = VTS & 0xff;
	ov5640_write_reg(0x380f, temp);

	temp = VTS>>8;
	ov5640_write_reg(0x380e, temp);
	return 0;
}

/* read shutter, in number of line period */
static int ov5640_get_shutter(void)
{
	int shutter;
	u8 regval;

	shutter = (ov5640_read_reg(0x03500, &regval) & 0x0f);

	shutter = (shutter<<8) + ov5640_read_reg(0x3501, &regval);
	shutter = (shutter<<4) + (ov5640_read_reg(0x3502, &regval)>>4);

	return shutter;
}

/* write shutter, in number of line period */
static int ov5640_set_shutter(int shutter)
{
	int temp;

	shutter = shutter & 0xffff;
	temp = shutter & 0x0f;
	temp = temp<<4;
	ov5640_write_reg(0x3502, temp);

	temp = shutter & 0xfff;
	temp = temp>>4;
	ov5640_write_reg(0x3501, temp);

	temp = shutter>>12;
	ov5640_write_reg(0x3500, temp);

	return 0;
}

/* read gain, 16 = 1x */
static int ov5640_get_gain16(void)
{
	int gain16;
	u8 regval;

	gain16 = ov5640_read_reg(0x350a, &regval) & 0x03;
	gain16 = (gain16<<8) + ov5640_read_reg(0x350b, &regval);

	return gain16;
}

/* write gain, 16 = 1x */
static int ov5640_set_gain16(int gain16)
{
	int temp;

	gain16 = gain16 & 0x3ff;
	temp = gain16 & 0xff;

	ov5640_write_reg(0x350b, temp);
	temp = gain16>>8;

	ov5640_write_reg(0x350a, temp);
	return 0;
}

/* get banding filter value */
static int ov5640_get_light_freq(void)
{
	int temp, temp1, light_frequency;
	u8 regval;

	temp = ov5640_read_reg(0x3c01, &regval);
	if (temp & 0x80) {
		/* manual */
		temp1 = ov5640_read_reg(0x3c00, &regval);
		if (temp1 & 0x04) {
			/* 50Hz */
			light_frequency = 50;
		} else {
			/* 60Hz */
			light_frequency = 60;
		}
	} else {
		/* auto */
		temp1 = ov5640_read_reg(0x3c0c, &regval);
		if (temp1 & 0x01) {
			/* 50Hz */
			light_frequency = 50;
		} else {
			/* 60Hz */
			light_frequency = 60;
		}
	}

	return light_frequency;
}

static void ov5640_set_bandingfilter(void)
{
	int prev_VTS;
	int band_step60, max_band60, band_step50, max_band50;

	/* read preview PCLK */
	prev_sysclk = ov5640_get_sysclk();

	/* read preview HTS */
	prev_HTS = ov5640_get_HTS();

	/* read preview VTS */
	prev_VTS = ov5640_get_VTS();

	/* calculate banding filter */
	/* 60Hz */
	band_step60 = prev_sysclk * 100/prev_HTS * 100/120;
	ov5640_write_reg(0x3a0a, (band_step60 >> 8));
	ov5640_write_reg(0x3a0b, (band_step60 & 0xff));

	max_band60 = (int)((prev_VTS-4)/band_step60);
	ov5640_write_reg(0x3a0d, max_band60);

	/* 50Hz */
	band_step50 = prev_sysclk * 100/prev_HTS;
	ov5640_write_reg(0x3a08, (band_step50 >> 8));
	ov5640_write_reg(0x3a09, (band_step50 & 0xff));

	max_band50 = (int)((prev_VTS-4)/band_step50);
	ov5640_write_reg(0x3a0e, max_band50);
}

/* stable in high */
static int ov5640_set_AE_target(int target)
{
	int fast_high, fast_low;

	AE_low = target * 23 / 25; /* 0.92 */
	AE_high = target * 27 / 25; /* 1.08 */
	fast_high = AE_high << 1;

	if (fast_high > 255)
		fast_high = 255;
	fast_low = AE_low >> 1;

	ov5640_write_reg(0x3a0f, AE_high);
	ov5640_write_reg(0x3a10, AE_low);
	ov5640_write_reg(0x3a1b, AE_high);
	ov5640_write_reg(0x3a1e, AE_low);
	ov5640_write_reg(0x3a11, fast_high);
	ov5640_write_reg(0x3a1f, fast_low);

	return 0;
}

/* enable = 0 to turn off night mode
   enable = 1 to turn on night mode */
static int ov5640_set_night_mode(int enable)
{
	u8 mode;

	ov5640_read_reg(0x3a00, &mode);

	if (enable) {
		/* night mode on */
		mode |= 0x04;
		ov5640_write_reg(0x3a00, mode);
	} else {
		/* night mode off */
		mode &= 0xfb;
		ov5640_write_reg(0x3a00, mode);
	}

	return 0;
}

/* enable = 0 to turn off AEC/AGC
   enable = 1 to turn on AEC/AGC */
static void ov5640_turn_on_AE_AG(int enable)
{
	u8 ae_ag_ctrl;

	ov5640_read_reg(0x3503, &ae_ag_ctrl);
	if (enable) {
		/* turn on auto AE/AG */
		ae_ag_ctrl = ae_ag_ctrl & ~(0x03);
	} else {
		/* turn off AE/AG */
		ae_ag_ctrl = ae_ag_ctrl | 0x03;
	}
	ov5640_write_reg(0x3503, ae_ag_ctrl);
}

/* download ov5640 settings to sensor through i2c */
static int ov5640_download_firmware(struct reg_value *pModeSetting, s32 ArySize)
{
	register u32 Delay_ms = 0;
	register u16 RegAddr = 0;
	register u8 Mask = 0;
	register u8 Val = 0;
	u8 RegVal = 0;
	int i, retval = 0;

	for (i = 0; i < ArySize; ++i, ++pModeSetting) {
		Delay_ms = pModeSetting->u32Delay_ms;
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u8Val;
		Mask = pModeSetting->u8Mask;

		if (Mask) {
			retval = ov5640_read_reg(RegAddr, &RegVal);
			if (retval < 0)
				goto err;

			RegVal &= ~(u8)Mask;
			Val &= Mask;
			Val |= RegVal;
		}

		retval = ov5640_write_reg(RegAddr, Val);
		if (retval < 0)
			goto err;

		if (Delay_ms)
			msleep(Delay_ms);
	}
err:
	return retval;
}

static int ov5640_init_mode(void)
{
	struct reg_value *pModeSetting = NULL;
	int ArySize = 0, retval = 0;

	ov5640_soft_reset();

	pModeSetting = ov5640_global_init_setting;
	ArySize = ARRAY_SIZE(ov5640_global_init_setting);
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	pModeSetting = ov5640_init_setting_30fps_VGA;
	ArySize = ARRAY_SIZE(ov5640_init_setting_30fps_VGA);
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* change driver capability to 2x according to validation board.
	 * if the image is not stable, please increase the driver strength.
	 */
	ov5640_driver_capability(2);
	ov5640_set_bandingfilter();
	ov5640_set_AE_target(AE_Target);
	ov5640_set_night_mode(night_mode);

	/* skip 9 vysnc: start capture at 10th vsync */
	msleep(300);

	/* turn off night mode */
	night_mode = 0;
	ov5640_data.pix.width = 640;
	ov5640_data.pix.height = 480;
err:
	return retval;
}

/* change to or back to subsampling mode set the mode directly
 * image size below 1280 * 960 is subsampling mode */
static int ov5640_change_mode_direct(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;

	if (mode > ov5640_mode_MAX || mode < ov5640_mode_MIN) {
		pr_err("Wrong ov5640 mode detected!\n");
		return -1;
	}

	pModeSetting = ov5640_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5640_mode_info_data[frame_rate][mode].init_data_size;

	ov5640_data.pix.width = ov5640_mode_info_data[frame_rate][mode].width;
	ov5640_data.pix.height = ov5640_mode_info_data[frame_rate][mode].height;

	if (ov5640_data.pix.width == 0 || ov5640_data.pix.height == 0 ||
	    pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	/* set ov5640 to subsampling mode */
	retval = ov5640_download_firmware(pModeSetting, ArySize);

	/* turn on AE AG for subsampling mode, in case the firmware didn't */
	ov5640_turn_on_AE_AG(1);

	/* calculate banding filter */
	ov5640_set_bandingfilter();

	/* set AE target */
	ov5640_set_AE_target(AE_Target);

	/* update night mode setting */
	ov5640_set_night_mode(night_mode);

	/* skip 9 vysnc: start capture at 10th vsync */
	if (mode == ov5640_mode_XGA_1024_768 && frame_rate == ov5640_30_fps) {
		pr_warning("ov5640: actual frame rate of XGA is 22.5fps\n");
		/* 1/22.5 * 9*/
		msleep(400);
		return retval;
	}

	if (frame_rate == ov5640_15_fps) {
		/* 1/15 * 9*/
		msleep(600);
	} else if (frame_rate == ov5640_30_fps) {
		/* 1/30 * 9*/
		msleep(300);
	}

	return retval;
}

/* change to scaling mode go through exposure calucation
 * image size above 1280 * 960 is scaling mode */
static int ov5640_change_mode_exposure_calc(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	int prev_shutter, prev_gain16, average;
	int cap_shutter, cap_gain16;
	int cap_sysclk, cap_HTS, cap_VTS;
	int light_freq, cap_bandfilt, cap_maxband;
	long cap_gain16_shutter;
	u8 temp;
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;

	/* check if the input mode and frame rate is valid */
	pModeSetting =
		ov5640_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5640_mode_info_data[frame_rate][mode].init_data_size;

	ov5640_data.pix.width =
		ov5640_mode_info_data[frame_rate][mode].width;
	ov5640_data.pix.height =
		ov5640_mode_info_data[frame_rate][mode].height;

	if (ov5640_data.pix.width == 0 || ov5640_data.pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	/* read preview shutter */
	prev_shutter = ov5640_get_shutter();

	/* read preview gain */
	prev_gain16 = ov5640_get_gain16();

	/* get average */
	average = ov5640_read_reg(0x56a1, &temp);

	/* turn off night mode for capture */
	ov5640_set_night_mode(0);

	/* turn off overlay */
	ov5640_write_reg(0x3022, 0x06);

	/* Write capture setting */
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* turn off AE AG when capture image. */
	ov5640_turn_on_AE_AG(0);

	/* read capture VTS */
	cap_VTS = ov5640_get_VTS();
	cap_HTS = ov5640_get_HTS();
	cap_sysclk = ov5640_get_sysclk();

	/* calculate capture banding filter */
	light_freq = ov5640_get_light_freq();
	if (light_freq == 60) {
		/* 60Hz */
		cap_bandfilt = cap_sysclk * 100 / cap_HTS * 100 / 120;
	} else {
		/* 50Hz */
		cap_bandfilt = cap_sysclk * 100 / cap_HTS;
	}
	cap_maxband = (int)((cap_VTS - 4)/cap_bandfilt);
	/* calculate capture shutter/gain16 */
	if (average > AE_low && average < AE_high) {
		/* in stable range */
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/prev_sysclk *
			prev_HTS/cap_HTS * AE_Target / average;
	} else {
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/prev_sysclk *
			prev_HTS/cap_HTS;
	}

	/* gain to shutter */
	if (cap_gain16_shutter < (cap_bandfilt * 16)) {
		/* shutter < 1/100 */
		cap_shutter = cap_gain16_shutter/16;
		if (cap_shutter < 1)
			cap_shutter = 1;
		cap_gain16 = cap_gain16_shutter/cap_shutter;
		if (cap_gain16 < 16)
			cap_gain16 = 16;
	} else {
		if (cap_gain16_shutter > (cap_bandfilt*cap_maxband*16)) {
			/* exposure reach max */
			cap_shutter = cap_bandfilt*cap_maxband;
			cap_gain16 = cap_gain16_shutter / cap_shutter;
		} else {
			/* 1/100 < cap_shutter =< max, cap_shutter = n/100 */
			cap_shutter =
				((int)(cap_gain16_shutter/16/cap_bandfilt))
				* cap_bandfilt;
			cap_gain16 = cap_gain16_shutter / cap_shutter;
		}
	}

	/* write capture gain */
	ov5640_set_gain16(cap_gain16);

	/* write capture shutter */
	if (cap_shutter > (cap_VTS - 4)) {
		cap_VTS = cap_shutter + 4;
		ov5640_set_VTS(cap_VTS);
	}

	ov5640_set_shutter(cap_shutter);

	/* skip 2 vysnc: start capture at 3rd vsync
	 * frame rate of QSXGA and 1080P is 7.5fps: 1/7.5 * 2
	 */
	pr_warning("ov5640: the actual frame rate of %s is 7.5fps\n",
		mode == ov5640_mode_1080P_1920_1080 ? "1080P" : "QSXGA");
	msleep(267);
err:
	return retval;
}

static int ov5640_change_mode(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	int retval = 0;

	if (mode > ov5640_mode_MAX || mode < ov5640_mode_MIN) {
		pr_err("Wrong ov5640 mode detected!\n");
		return -1;
	}

	if (mode == ov5640_mode_1080P_1920_1080 ||
			mode == ov5640_mode_QSXGA_2592_1944) {
		/* change to scaling mode go through exposure calucation
		 * image size above 1280 * 960 is scaling mode */
		retval = ov5640_change_mode_exposure_calc(frame_rate, mode);
	} else {
		/* change back to subsampling modem download firmware directly
		 * image size below 1280 * 960 is subsampling mode */
		retval = ov5640_change_mode_direct(frame_rate, mode);
	}

	return retval;
}

/* --------------- IOCTL functions from v4l2_int_ioctl_desc --------------- */

static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	if (s == NULL) {
		pr_err("   ERROR!! no slave device set!\n");
		return -1;
	}

	memset(p, 0, sizeof(*p));
	p->u.bt656.clock_curr = ov5640_data.mclk;
	pr_debug("   clock_curr=mclk=%d\n", ov5640_data.mclk);
	p->if_type = V4L2_IF_TYPE_BT656;
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.clock_min = OV5640_XCLK_MIN;
	p->u.bt656.clock_max = OV5640_XCLK_MAX;
	p->u.bt656.bt_sync_correct = 1;  /* Indicate external vsync */

	return 0;
}

/*!
 * ioctl_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	struct sensor_data *sensor = s->priv;

	if (on && !sensor->on) {
		if (io_regulator)
			if (regulator_enable(io_regulator) != 0)
				return -EIO;
		if (core_regulator)
			if (regulator_enable(core_regulator) != 0)
				return -EIO;
		if (analog_regulator)
			if (regulator_enable(analog_regulator) != 0)
				return -EIO;
		/* Make sure power on */
		ov5640_power_down(0);
	} else if (!on && sensor->on) {
		if (analog_regulator)
			regulator_disable(analog_regulator);
		if (core_regulator)
			regulator_disable(core_regulator);
		if (io_regulator)
			regulator_disable(io_regulator);

		ov5640_power_down(1);
}

	sensor->on = on;

	return 0;
}

/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor_data *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor_data *sensor = s->priv;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum ov5640_frame_rate frame_rate;
	int ret = 0;

	/* Make sure power on */
	ov5640_power_down(0);

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == 15)
			frame_rate = ov5640_15_fps;
		else if (tgt_fps == 30)
			frame_rate = ov5640_30_fps;
		else {
			pr_err(" The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		ret = ov5640_change_mode(frame_rate,
				a->parm.capture.capturemode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode = a->parm.capture.capturemode;

		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("   type is not " \
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",
			a->type);
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct sensor_data *sensor = s->priv;

	f->fmt.pix = sensor->pix;

	return 0;
}

/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int ret = 0;

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		vc->value = ov5640_data.brightness;
		break;
	case V4L2_CID_HUE:
		vc->value = ov5640_data.hue;
		break;
	case V4L2_CID_CONTRAST:
		vc->value = ov5640_data.contrast;
		break;
	case V4L2_CID_SATURATION:
		vc->value = ov5640_data.saturation;
		break;
	case V4L2_CID_RED_BALANCE:
		vc->value = ov5640_data.red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		vc->value = ov5640_data.blue;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = ov5640_data.ae_mode;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;

	pr_debug("In ov5640:ioctl_s_ctrl %d\n",
		 vc->id);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		break;
	case V4L2_CID_RED_BALANCE:
		break;
	case V4L2_CID_BLUE_BALANCE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_GAIN:
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	default:
		retval = -EPERM;
		break;
	}

	return retval;
}

/*!
 * ioctl_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index > ov5640_mode_MAX)
		return -EINVAL;

	fsize->pixel_format = ov5640_data.pix.pixelformat;
	fsize->discrete.width =
			max(ov5640_mode_info_data[0][fsize->index].width,
			    ov5640_mode_info_data[1][fsize->index].width);
	fsize->discrete.height =
			max(ov5640_mode_info_data[0][fsize->index].height,
			    ov5640_mode_info_data[1][fsize->index].height);
	return 0;
}

/*!
 * ioctl_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_frameintervals(struct v4l2_int_device *s,
					 struct v4l2_frmivalenum *fival)
{
	int i, j, count;

	if (fival->index < 0 || fival->index > ov5640_mode_MAX)
		return -EINVAL;

	if (fival->width == 0 || fival->height == 0 ||
	    fival->pixel_format == 0) {
		pr_warning("Please assign pixelformat, width and height.\n");
		return -EINVAL;
	}

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(ov5640_mode_info_data); i++) {
		for (j = 0; j < (ov5640_mode_MAX + 1); j++) {
			if (fival->pixel_format == ov5640_data.pix.pixelformat
			 && fival->width == ov5640_mode_info_data[i][j].width
			 && fival->height == ov5640_mode_info_data[i][j].height
			 && ov5640_mode_info_data[i][j].init_data_ptr != NULL) {
				count++;
			}
			if (fival->index == (count - 1)) {
				fival->discrete.denominator =
						ov5640_framerates[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name, "ov5640_camera");

	return 0;
}

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{

	return 0;
}

/*!
 * ioctl_enum_fmt_cap - V4L2 sensor interface handler for VIDIOC_ENUM_FMT
 * @s: pointer to standard V4L2 device structure
 * @fmt: pointer to standard V4L2 fmt description structure
 *
 * Return 0.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	if (fmt->index > ov5640_mode_MAX)
		return -EINVAL;

	fmt->pixelformat = ov5640_data.pix.pixelformat;

	return 0;
}

/*!
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct sensor_data *sensor = s->priv;
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum ov5640_frame_rate frame_rate;
	int ret;

	ov5640_data.on = true;

	/* mclk */
	tgt_xclk = ov5640_data.mclk;
	tgt_xclk = min(tgt_xclk, (u32)OV5640_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5640_XCLK_MIN);
	ov5640_data.mclk = tgt_xclk;

	pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);
	clk_set_rate(ov5640_data.sensor_clk, ov5640_data.mclk);

	/* Default camera frame rate is set in probe */
	tgt_fps = sensor->streamcap.timeperframe.denominator /
		  sensor->streamcap.timeperframe.numerator;

	if (tgt_fps == 15)
		frame_rate = ov5640_15_fps;
	else if (tgt_fps == 30)
		frame_rate = ov5640_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	ret = ov5640_init_mode();
	return ret;
}

/*!
 * ioctl_dev_exit - V4L2 sensor interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the device when slave detaches to the master.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	return 0;
}

/*!
 * This structure defines all the ioctls for this module and links them to the
 * enumeration.
 */
static struct v4l2_int_ioctl_desc ov5640_ioctl_desc[] = {
	{ vidioc_int_dev_init_num,
	  (v4l2_int_ioctl_func *)ioctl_dev_init },
	{ vidioc_int_dev_exit_num,
	  ioctl_dev_exit},
	{ vidioc_int_s_power_num,
	  (v4l2_int_ioctl_func *)ioctl_s_power },
	{ vidioc_int_g_ifparm_num,
	  (v4l2_int_ioctl_func *)ioctl_g_ifparm },
	{ vidioc_int_init_num,
	  (v4l2_int_ioctl_func *)ioctl_init },
	{ vidioc_int_enum_fmt_cap_num,
	  (v4l2_int_ioctl_func *)ioctl_enum_fmt_cap },
	{ vidioc_int_g_fmt_cap_num,
	  (v4l2_int_ioctl_func *)ioctl_g_fmt_cap },
	{ vidioc_int_g_parm_num,
	  (v4l2_int_ioctl_func *)ioctl_g_parm },
	{ vidioc_int_s_parm_num,
	  (v4l2_int_ioctl_func *)ioctl_s_parm },
	{ vidioc_int_g_ctrl_num,
	  (v4l2_int_ioctl_func *)ioctl_g_ctrl },
	{ vidioc_int_s_ctrl_num,
	  (v4l2_int_ioctl_func *)ioctl_s_ctrl },
	{ vidioc_int_enum_framesizes_num,
	  (v4l2_int_ioctl_func *)ioctl_enum_framesizes },
	{ vidioc_int_enum_frameintervals_num,
	  (v4l2_int_ioctl_func *)ioctl_enum_frameintervals },
	{ vidioc_int_g_chip_ident_num,
	  (v4l2_int_ioctl_func *)ioctl_g_chip_ident },
};

static struct v4l2_int_slave ov5640_slave = {
	.ioctls = ov5640_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(ov5640_ioctl_desc),
};

static struct v4l2_int_device ov5640_int_device = {
	.module = THIS_MODULE,
	.name = "ov564x",
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &ov5640_slave,
	},
};

/*!
 * ov5640 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	u8 chip_id_high, chip_id_low;

	/* ov5640 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "setup pinctrl failed\n");
		return PTR_ERR(pinctrl);
	}

	/* request power down pin */
	pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(pwn_gpio)) {
		dev_err(dev, "no sensor pwdn pin available\n");
		return -ENODEV;
	}
	retval = devm_gpio_request_one(dev, pwn_gpio, GPIOF_OUT_INIT_HIGH,
					"ov5640_pwdn");
	if (retval < 0)
		return retval;

	/* request reset pin */
	rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(rst_gpio)) {
		dev_err(dev, "no sensor reset pin available\n");
		return -EINVAL;
	}
	retval = devm_gpio_request_one(dev, rst_gpio, GPIOF_OUT_INIT_HIGH,
					"ov5640_reset");
	if (retval < 0)
		return retval;

	/* Set initial values for the sensor struct. */
	memset(&ov5640_data, 0, sizeof(ov5640_data));
	ov5640_data.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(ov5640_data.sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(ov5640_data.sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&ov5640_data.mclk);
	if (retval) {
		dev_err(dev, "mclk frequency is invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(ov5640_data.mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(ov5640_data.csi));
	if (retval) {
		dev_err(dev, "csi_id invalid\n");
		return retval;
	}

	clk_prepare_enable(ov5640_data.sensor_clk);

	ov5640_data.io_init = ov5640_reset;
	ov5640_data.i2c_client = client;
	ov5640_data.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	ov5640_data.pix.width = 640;
	ov5640_data.pix.height = 480;
	ov5640_data.streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	ov5640_data.streamcap.capturemode = 0;
	ov5640_data.streamcap.timeperframe.denominator = DEFAULT_FPS;
	ov5640_data.streamcap.timeperframe.numerator = 1;

	ov5640_regulator_enable(&client->dev);

	ov5640_reset();

	ov5640_power_down(0);

	retval = ov5640_read_reg(OV5640_CHIP_ID_HIGH_BYTE, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		clk_disable_unprepare(ov5640_data.sensor_clk);
		pr_warning("camera ov5640 is not found\n");
		return -ENODEV;
	}
	retval = ov5640_read_reg(OV5640_CHIP_ID_LOW_BYTE, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x40) {
		clk_disable_unprepare(ov5640_data.sensor_clk);
		pr_warning("camera ov5640 is not found\n");
		return -ENODEV;
	}

	ov5640_power_down(1);

	clk_disable_unprepare(ov5640_data.sensor_clk);

	ov5640_int_device.priv = &ov5640_data;
	retval = v4l2_int_device_register(&ov5640_int_device);

	pr_info("camera ov5640 is found\n");
	return retval;
}

/*!
 * ov5640 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int ov5640_remove(struct i2c_client *client)
{
	v4l2_int_device_unregister(&ov5640_int_device);

	if (analog_regulator)
		regulator_disable(analog_regulator);

	if (core_regulator)
		regulator_disable(core_regulator);

	if (io_regulator)
		regulator_disable(io_regulator);

	return 0;
}

module_i2c_driver(ov5640_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV5640 Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
