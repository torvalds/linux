/*
 * Driver for OV5642 CMOS Image Sensor from Omnivision
 *
 * Copyright (C) 2011, Bastian Hecht <hechtb@gmail.com>
 *
 * Based on Sony IMX074 Camera Driver
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Based on Omnivision OV7670 Camera Driver
 * Copyright (C) 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>
#include <linux/v4l2-mediabus.h>

#include <media/soc_camera.h>
#include <media/v4l2-subdev.h>

/* OV5642 registers */
#define REG_CHIP_ID_HIGH		0x300a
#define REG_CHIP_ID_LOW			0x300b

#define REG_WINDOW_START_X_HIGH		0x3800
#define REG_WINDOW_START_X_LOW		0x3801
#define REG_WINDOW_START_Y_HIGH		0x3802
#define REG_WINDOW_START_Y_LOW		0x3803
#define REG_WINDOW_WIDTH_HIGH		0x3804
#define REG_WINDOW_WIDTH_LOW		0x3805
#define REG_WINDOW_HEIGHT_HIGH		0x3806
#define REG_WINDOW_HEIGHT_LOW		0x3807
#define REG_OUT_WIDTH_HIGH		0x3808
#define REG_OUT_WIDTH_LOW		0x3809
#define REG_OUT_HEIGHT_HIGH		0x380a
#define REG_OUT_HEIGHT_LOW		0x380b
#define REG_OUT_TOTAL_WIDTH_HIGH	0x380c
#define REG_OUT_TOTAL_WIDTH_LOW		0x380d
#define REG_OUT_TOTAL_HEIGHT_HIGH	0x380e
#define REG_OUT_TOTAL_HEIGHT_LOW	0x380f
#define REG_OUTPUT_FORMAT		0x4300
#define REG_ISP_CTRL_01			0x5001
#define REG_AVG_WINDOW_END_X_HIGH	0x5682
#define REG_AVG_WINDOW_END_X_LOW	0x5683
#define REG_AVG_WINDOW_END_Y_HIGH	0x5686
#define REG_AVG_WINDOW_END_Y_LOW	0x5687

/* active pixel array size */
#define OV5642_SENSOR_SIZE_X	2592
#define OV5642_SENSOR_SIZE_Y	1944

/*
 * About OV5642 resolution, cropping and binning:
 * This sensor supports it all, at least in the feature description.
 * Unfortunately, no combination of appropriate registers settings could make
 * the chip work the intended way. As it works with predefined register lists,
 * some undocumented registers are presumably changed there to achieve their
 * goals.
 * This driver currently only works for resolutions up to 720 lines with a
 * 1:1 scale. Hopefully these restrictions will be removed in the future.
 */
#define OV5642_MAX_WIDTH	OV5642_SENSOR_SIZE_X
#define OV5642_MAX_HEIGHT	720

/* default sizes */
#define OV5642_DEFAULT_WIDTH	1280
#define OV5642_DEFAULT_HEIGHT	OV5642_MAX_HEIGHT

/* minimum extra blanking */
#define BLANKING_EXTRA_WIDTH		500
#define BLANKING_EXTRA_HEIGHT		20

/*
 * the sensor's autoexposure is buggy when setting total_height low.
 * It tries to expose longer than 1 frame period without taking care of it
 * and this leads to weird output. So we set 1000 lines as minimum.
 */
#define BLANKING_MIN_HEIGHT		1000

struct regval_list {
	u16 reg_num;
	u8 value;
};

static struct regval_list ov5642_default_regs_init[] = {
	{ 0x3103, 0x93 },
	{ 0x3008, 0x82 },
	{ 0x3017, 0x7f },
	{ 0x3018, 0xfc },
	{ 0x3810, 0xc2 },
	{ 0x3615, 0xf0 },
	{ 0x3000, 0x0  },
	{ 0x3001, 0x0  },
	{ 0x3002, 0x0  },
	{ 0x3003, 0x0  },
	{ 0x3004, 0xff },
	{ 0x3030, 0x2b },
	{ 0x3011, 0x8  },
	{ 0x3010, 0x10 },
	{ 0x3604, 0x60 },
	{ 0x3622, 0x60 },
	{ 0x3621, 0x9  },
	{ 0x3709, 0x0  },
	{ 0x4000, 0x21 },
	{ 0x401d, 0x22 },
	{ 0x3600, 0x54 },
	{ 0x3605, 0x4  },
	{ 0x3606, 0x3f },
	{ 0x3c01, 0x80 },
	{ 0x300d, 0x22 },
	{ 0x3623, 0x22 },
	{ 0x5000, 0x4f },
	{ 0x5020, 0x4  },
	{ 0x5181, 0x79 },
	{ 0x5182, 0x0  },
	{ 0x5185, 0x22 },
	{ 0x5197, 0x1  },
	{ 0x5500, 0xa  },
	{ 0x5504, 0x0  },
	{ 0x5505, 0x7f },
	{ 0x5080, 0x8  },
	{ 0x300e, 0x18 },
	{ 0x4610, 0x0  },
	{ 0x471d, 0x5  },
	{ 0x4708, 0x6  },
	{ 0x370c, 0xa0 },
	{ 0x5687, 0x94 },
	{ 0x501f, 0x0  },
	{ 0x5000, 0x4f },
	{ 0x5001, 0xcf },
	{ 0x4300, 0x30 },
	{ 0x4300, 0x30 },
	{ 0x460b, 0x35 },
	{ 0x471d, 0x0  },
	{ 0x3002, 0xc  },
	{ 0x3002, 0x0  },
	{ 0x4713, 0x3  },
	{ 0x471c, 0x50 },
	{ 0x4721, 0x2  },
	{ 0x4402, 0x90 },
	{ 0x460c, 0x22 },
	{ 0x3815, 0x44 },
	{ 0x3503, 0x7  },
	{ 0x3501, 0x73 },
	{ 0x3502, 0x80 },
	{ 0x350b, 0x0  },
	{ 0x3818, 0xc8 },
	{ 0x3824, 0x11 },
	{ 0x3a00, 0x78 },
	{ 0x3a1a, 0x4  },
	{ 0x3a13, 0x30 },
	{ 0x3a18, 0x0  },
	{ 0x3a19, 0x7c },
	{ 0x3a08, 0x12 },
	{ 0x3a09, 0xc0 },
	{ 0x3a0a, 0xf  },
	{ 0x3a0b, 0xa0 },
	{ 0x350c, 0x7  },
	{ 0x350d, 0xd0 },
	{ 0x3a0d, 0x8  },
	{ 0x3a0e, 0x6  },
	{ 0x3500, 0x0  },
	{ 0x3501, 0x0  },
	{ 0x3502, 0x0  },
	{ 0x350a, 0x0  },
	{ 0x350b, 0x0  },
	{ 0x3503, 0x0  },
	{ 0x3a0f, 0x3c },
	{ 0x3a10, 0x32 },
	{ 0x3a1b, 0x3c },
	{ 0x3a1e, 0x32 },
	{ 0x3a11, 0x80 },
	{ 0x3a1f, 0x20 },
	{ 0x3030, 0x2b },
	{ 0x3a02, 0x0  },
	{ 0x3a03, 0x7d },
	{ 0x3a04, 0x0  },
	{ 0x3a14, 0x0  },
	{ 0x3a15, 0x7d },
	{ 0x3a16, 0x0  },
	{ 0x3a00, 0x78 },
	{ 0x3a08, 0x9  },
	{ 0x3a09, 0x60 },
	{ 0x3a0a, 0x7  },
	{ 0x3a0b, 0xd0 },
	{ 0x3a0d, 0x10 },
	{ 0x3a0e, 0xd  },
	{ 0x4407, 0x4  },
	{ 0x5193, 0x70 },
	{ 0x589b, 0x0  },
	{ 0x589a, 0xc0 },
	{ 0x401e, 0x20 },
	{ 0x4001, 0x42 },
	{ 0x401c, 0x6  },
	{ 0x3825, 0xac },
	{ 0x3827, 0xc  },
	{ 0x528a, 0x1  },
	{ 0x528b, 0x4  },
	{ 0x528c, 0x8  },
	{ 0x528d, 0x10 },
	{ 0x528e, 0x20 },
	{ 0x528f, 0x28 },
	{ 0x5290, 0x30 },
	{ 0x5292, 0x0  },
	{ 0x5293, 0x1  },
	{ 0x5294, 0x0  },
	{ 0x5295, 0x4  },
	{ 0x5296, 0x0  },
	{ 0x5297, 0x8  },
	{ 0x5298, 0x0  },
	{ 0x5299, 0x10 },
	{ 0x529a, 0x0  },
	{ 0x529b, 0x20 },
	{ 0x529c, 0x0  },
	{ 0x529d, 0x28 },
	{ 0x529e, 0x0  },
	{ 0x529f, 0x30 },
	{ 0x5282, 0x0  },
	{ 0x5300, 0x0  },
	{ 0x5301, 0x20 },
	{ 0x5302, 0x0  },
	{ 0x5303, 0x7c },
	{ 0x530c, 0x0  },
	{ 0x530d, 0xc  },
	{ 0x530e, 0x20 },
	{ 0x530f, 0x80 },
	{ 0x5310, 0x20 },
	{ 0x5311, 0x80 },
	{ 0x5308, 0x20 },
	{ 0x5309, 0x40 },
	{ 0x5304, 0x0  },
	{ 0x5305, 0x30 },
	{ 0x5306, 0x0  },
	{ 0x5307, 0x80 },
	{ 0x5314, 0x8  },
	{ 0x5315, 0x20 },
	{ 0x5319, 0x30 },
	{ 0x5316, 0x10 },
	{ 0x5317, 0x0  },
	{ 0x5318, 0x2  },
	{ 0x5380, 0x1  },
	{ 0x5381, 0x0  },
	{ 0x5382, 0x0  },
	{ 0x5383, 0x4e },
	{ 0x5384, 0x0  },
	{ 0x5385, 0xf  },
	{ 0x5386, 0x0  },
	{ 0x5387, 0x0  },
	{ 0x5388, 0x1  },
	{ 0x5389, 0x15 },
	{ 0x538a, 0x0  },
	{ 0x538b, 0x31 },
	{ 0x538c, 0x0  },
	{ 0x538d, 0x0  },
	{ 0x538e, 0x0  },
	{ 0x538f, 0xf  },
	{ 0x5390, 0x0  },
	{ 0x5391, 0xab },
	{ 0x5392, 0x0  },
	{ 0x5393, 0xa2 },
	{ 0x5394, 0x8  },
	{ 0x5480, 0x14 },
	{ 0x5481, 0x21 },
	{ 0x5482, 0x36 },
	{ 0x5483, 0x57 },
	{ 0x5484, 0x65 },
	{ 0x5485, 0x71 },
	{ 0x5486, 0x7d },
	{ 0x5487, 0x87 },
	{ 0x5488, 0x91 },
	{ 0x5489, 0x9a },
	{ 0x548a, 0xaa },
	{ 0x548b, 0xb8 },
	{ 0x548c, 0xcd },
	{ 0x548d, 0xdd },
	{ 0x548e, 0xea },
	{ 0x548f, 0x1d },
	{ 0x5490, 0x5  },
	{ 0x5491, 0x0  },
	{ 0x5492, 0x4  },
	{ 0x5493, 0x20 },
	{ 0x5494, 0x3  },
	{ 0x5495, 0x60 },
	{ 0x5496, 0x2  },
	{ 0x5497, 0xb8 },
	{ 0x5498, 0x2  },
	{ 0x5499, 0x86 },
	{ 0x549a, 0x2  },
	{ 0x549b, 0x5b },
	{ 0x549c, 0x2  },
	{ 0x549d, 0x3b },
	{ 0x549e, 0x2  },
	{ 0x549f, 0x1c },
	{ 0x54a0, 0x2  },
	{ 0x54a1, 0x4  },
	{ 0x54a2, 0x1  },
	{ 0x54a3, 0xed },
	{ 0x54a4, 0x1  },
	{ 0x54a5, 0xc5 },
	{ 0x54a6, 0x1  },
	{ 0x54a7, 0xa5 },
	{ 0x54a8, 0x1  },
	{ 0x54a9, 0x6c },
	{ 0x54aa, 0x1  },
	{ 0x54ab, 0x41 },
	{ 0x54ac, 0x1  },
	{ 0x54ad, 0x20 },
	{ 0x54ae, 0x0  },
	{ 0x54af, 0x16 },
	{ 0x54b0, 0x1  },
	{ 0x54b1, 0x20 },
	{ 0x54b2, 0x0  },
	{ 0x54b3, 0x10 },
	{ 0x54b4, 0x0  },
	{ 0x54b5, 0xf0 },
	{ 0x54b6, 0x0  },
	{ 0x54b7, 0xdf },
	{ 0x5402, 0x3f },
	{ 0x5403, 0x0  },
	{ 0x3406, 0x0  },
	{ 0x5180, 0xff },
	{ 0x5181, 0x52 },
	{ 0x5182, 0x11 },
	{ 0x5183, 0x14 },
	{ 0x5184, 0x25 },
	{ 0x5185, 0x24 },
	{ 0x5186, 0x6  },
	{ 0x5187, 0x8  },
	{ 0x5188, 0x8  },
	{ 0x5189, 0x7c },
	{ 0x518a, 0x60 },
	{ 0x518b, 0xb2 },
	{ 0x518c, 0xb2 },
	{ 0x518d, 0x44 },
	{ 0x518e, 0x3d },
	{ 0x518f, 0x58 },
	{ 0x5190, 0x46 },
	{ 0x5191, 0xf8 },
	{ 0x5192, 0x4  },
	{ 0x5193, 0x70 },
	{ 0x5194, 0xf0 },
	{ 0x5195, 0xf0 },
	{ 0x5196, 0x3  },
	{ 0x5197, 0x1  },
	{ 0x5198, 0x4  },
	{ 0x5199, 0x12 },
	{ 0x519a, 0x4  },
	{ 0x519b, 0x0  },
	{ 0x519c, 0x6  },
	{ 0x519d, 0x82 },
	{ 0x519e, 0x0  },
	{ 0x5025, 0x80 },
	{ 0x3a0f, 0x38 },
	{ 0x3a10, 0x30 },
	{ 0x3a1b, 0x3a },
	{ 0x3a1e, 0x2e },
	{ 0x3a11, 0x60 },
	{ 0x3a1f, 0x10 },
	{ 0x5688, 0xa6 },
	{ 0x5689, 0x6a },
	{ 0x568a, 0xea },
	{ 0x568b, 0xae },
	{ 0x568c, 0xa6 },
	{ 0x568d, 0x6a },
	{ 0x568e, 0x62 },
	{ 0x568f, 0x26 },
	{ 0x5583, 0x40 },
	{ 0x5584, 0x40 },
	{ 0x5580, 0x2  },
	{ 0x5000, 0xcf },
	{ 0x5800, 0x27 },
	{ 0x5801, 0x19 },
	{ 0x5802, 0x12 },
	{ 0x5803, 0xf  },
	{ 0x5804, 0x10 },
	{ 0x5805, 0x15 },
	{ 0x5806, 0x1e },
	{ 0x5807, 0x2f },
	{ 0x5808, 0x15 },
	{ 0x5809, 0xd  },
	{ 0x580a, 0xa  },
	{ 0x580b, 0x9  },
	{ 0x580c, 0xa  },
	{ 0x580d, 0xc  },
	{ 0x580e, 0x12 },
	{ 0x580f, 0x19 },
	{ 0x5810, 0xb  },
	{ 0x5811, 0x7  },
	{ 0x5812, 0x4  },
	{ 0x5813, 0x3  },
	{ 0x5814, 0x3  },
	{ 0x5815, 0x6  },
	{ 0x5816, 0xa  },
	{ 0x5817, 0xf  },
	{ 0x5818, 0xa  },
	{ 0x5819, 0x5  },
	{ 0x581a, 0x1  },
	{ 0x581b, 0x0  },
	{ 0x581c, 0x0  },
	{ 0x581d, 0x3  },
	{ 0x581e, 0x8  },
	{ 0x581f, 0xc  },
	{ 0x5820, 0xa  },
	{ 0x5821, 0x5  },
	{ 0x5822, 0x1  },
	{ 0x5823, 0x0  },
	{ 0x5824, 0x0  },
	{ 0x5825, 0x3  },
	{ 0x5826, 0x8  },
	{ 0x5827, 0xc  },
	{ 0x5828, 0xe  },
	{ 0x5829, 0x8  },
	{ 0x582a, 0x6  },
	{ 0x582b, 0x4  },
	{ 0x582c, 0x5  },
	{ 0x582d, 0x7  },
	{ 0x582e, 0xb  },
	{ 0x582f, 0x12 },
	{ 0x5830, 0x18 },
	{ 0x5831, 0x10 },
	{ 0x5832, 0xc  },
	{ 0x5833, 0xa  },
	{ 0x5834, 0xb  },
	{ 0x5835, 0xe  },
	{ 0x5836, 0x15 },
	{ 0x5837, 0x19 },
	{ 0x5838, 0x32 },
	{ 0x5839, 0x1f },
	{ 0x583a, 0x18 },
	{ 0x583b, 0x16 },
	{ 0x583c, 0x17 },
	{ 0x583d, 0x1e },
	{ 0x583e, 0x26 },
	{ 0x583f, 0x53 },
	{ 0x5840, 0x10 },
	{ 0x5841, 0xf  },
	{ 0x5842, 0xd  },
	{ 0x5843, 0xc  },
	{ 0x5844, 0xe  },
	{ 0x5845, 0x9  },
	{ 0x5846, 0x11 },
	{ 0x5847, 0x10 },
	{ 0x5848, 0x10 },
	{ 0x5849, 0x10 },
	{ 0x584a, 0x10 },
	{ 0x584b, 0xe  },
	{ 0x584c, 0x10 },
	{ 0x584d, 0x10 },
	{ 0x584e, 0x11 },
	{ 0x584f, 0x10 },
	{ 0x5850, 0xf  },
	{ 0x5851, 0xc  },
	{ 0x5852, 0xf  },
	{ 0x5853, 0x10 },
	{ 0x5854, 0x10 },
	{ 0x5855, 0xf  },
	{ 0x5856, 0xe  },
	{ 0x5857, 0xb  },
	{ 0x5858, 0x10 },
	{ 0x5859, 0xd  },
	{ 0x585a, 0xd  },
	{ 0x585b, 0xc  },
	{ 0x585c, 0xc  },
	{ 0x585d, 0xc  },
	{ 0x585e, 0xb  },
	{ 0x585f, 0xc  },
	{ 0x5860, 0xc  },
	{ 0x5861, 0xc  },
	{ 0x5862, 0xd  },
	{ 0x5863, 0x8  },
	{ 0x5864, 0x11 },
	{ 0x5865, 0x18 },
	{ 0x5866, 0x18 },
	{ 0x5867, 0x19 },
	{ 0x5868, 0x17 },
	{ 0x5869, 0x19 },
	{ 0x586a, 0x16 },
	{ 0x586b, 0x13 },
	{ 0x586c, 0x13 },
	{ 0x586d, 0x12 },
	{ 0x586e, 0x13 },
	{ 0x586f, 0x16 },
	{ 0x5870, 0x14 },
	{ 0x5871, 0x12 },
	{ 0x5872, 0x10 },
	{ 0x5873, 0x11 },
	{ 0x5874, 0x11 },
	{ 0x5875, 0x16 },
	{ 0x5876, 0x14 },
	{ 0x5877, 0x11 },
	{ 0x5878, 0x10 },
	{ 0x5879, 0xf  },
	{ 0x587a, 0x10 },
	{ 0x587b, 0x14 },
	{ 0x587c, 0x13 },
	{ 0x587d, 0x12 },
	{ 0x587e, 0x11 },
	{ 0x587f, 0x11 },
	{ 0x5880, 0x12 },
	{ 0x5881, 0x15 },
	{ 0x5882, 0x14 },
	{ 0x5883, 0x15 },
	{ 0x5884, 0x15 },
	{ 0x5885, 0x15 },
	{ 0x5886, 0x13 },
	{ 0x5887, 0x17 },
	{ 0x3710, 0x10 },
	{ 0x3632, 0x51 },
	{ 0x3702, 0x10 },
	{ 0x3703, 0xb2 },
	{ 0x3704, 0x18 },
	{ 0x370b, 0x40 },
	{ 0x370d, 0x3  },
	{ 0x3631, 0x1  },
	{ 0x3632, 0x52 },
	{ 0x3606, 0x24 },
	{ 0x3620, 0x96 },
	{ 0x5785, 0x7  },
	{ 0x3a13, 0x30 },
	{ 0x3600, 0x52 },
	{ 0x3604, 0x48 },
	{ 0x3606, 0x1b },
	{ 0x370d, 0xb  },
	{ 0x370f, 0xc0 },
	{ 0x3709, 0x1  },
	{ 0x3823, 0x0  },
	{ 0x5007, 0x0  },
	{ 0x5009, 0x0  },
	{ 0x5011, 0x0  },
	{ 0x5013, 0x0  },
	{ 0x519e, 0x0  },
	{ 0x5086, 0x0  },
	{ 0x5087, 0x0  },
	{ 0x5088, 0x0  },
	{ 0x5089, 0x0  },
	{ 0x302b, 0x0  },
	{ 0x3503, 0x7  },
	{ 0x3011, 0x8  },
	{ 0x350c, 0x2  },
	{ 0x350d, 0xe4 },
	{ 0x3621, 0xc9 },
	{ 0x370a, 0x81 },
	{ 0xffff, 0xff },
};

static struct regval_list ov5642_default_regs_finalise[] = {
	{ 0x3810, 0xc2 },
	{ 0x3818, 0xc9 },
	{ 0x381c, 0x10 },
	{ 0x381d, 0xa0 },
	{ 0x381e, 0x5  },
	{ 0x381f, 0xb0 },
	{ 0x3820, 0x0  },
	{ 0x3821, 0x0  },
	{ 0x3824, 0x11 },
	{ 0x3a08, 0x1b },
	{ 0x3a09, 0xc0 },
	{ 0x3a0a, 0x17 },
	{ 0x3a0b, 0x20 },
	{ 0x3a0d, 0x2  },
	{ 0x3a0e, 0x1  },
	{ 0x401c, 0x4  },
	{ 0x5682, 0x5  },
	{ 0x5683, 0x0  },
	{ 0x5686, 0x2  },
	{ 0x5687, 0xcc },
	{ 0x5001, 0x4f },
	{ 0x589b, 0x6  },
	{ 0x589a, 0xc5 },
	{ 0x3503, 0x0  },
	{ 0x460c, 0x20 },
	{ 0x460b, 0x37 },
	{ 0x471c, 0xd0 },
	{ 0x471d, 0x5  },
	{ 0x3815, 0x1  },
	{ 0x3818, 0xc1 },
	{ 0x501f, 0x0  },
	{ 0x5002, 0xe0 },
	{ 0x4300, 0x32 }, /* UYVY */
	{ 0x3002, 0x1c },
	{ 0x4800, 0x14 },
	{ 0x4801, 0xf  },
	{ 0x3007, 0x3b },
	{ 0x300e, 0x4  },
	{ 0x4803, 0x50 },
	{ 0x3815, 0x1  },
	{ 0x4713, 0x2  },
	{ 0x4842, 0x1  },
	{ 0x300f, 0xe  },
	{ 0x3003, 0x3  },
	{ 0x3003, 0x1  },
	{ 0xffff, 0xff },
};

struct ov5642_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

struct ov5642 {
	struct v4l2_subdev		subdev;
	const struct ov5642_datafmt	*fmt;
	struct v4l2_rect                crop_rect;

	/* blanking information */
	int total_width;
	int total_height;
};

static const struct ov5642_datafmt ov5642_colour_fmts[] = {
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
};

static struct ov5642 *to_ov5642(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5642, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ov5642_datafmt
			*ov5642_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5642_colour_fmts); i++)
		if (ov5642_colour_fmts[i].code == code)
			return ov5642_colour_fmts + i;

	return NULL;
}

static int reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	/* We have 16-bit i2c addresses - care for endianess */
	unsigned char data[2] = { reg >> 8, reg & 0xff };

	ret = i2c_master_send(client, data, 2);
	if (ret < 2) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
			__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(client, val, 1);
	if (ret < 1) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
				__func__, reg);
		return ret < 0 ? ret : -EIO;
	}
	return 0;
}

static int reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	ret = i2c_master_send(client, data, 3);
	if (ret < 3) {
		dev_err(&client->dev, "%s: i2c write error, reg: %x\n",
			__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/*
 * convenience function to write 16 bit register values that are split up
 * into two consecutive high and low parts
 */
static int reg_write16(struct i2c_client *client, u16 reg, u16 val16)
{
	int ret;

	ret = reg_write(client, reg, val16 >> 8);
	if (ret)
		return ret;
	return reg_write(client, reg + 1, val16 & 0x00ff);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5642_get_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 1;

	ret = reg_read(client, reg->reg, &val);
	if (!ret)
		reg->val = (__u64)val;

	return ret;
}

static int ov5642_set_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return reg_write(client, reg->reg, reg->val);
}
#endif

static int ov5642_write_array(struct i2c_client *client,
				struct regval_list *vals)
{
	while (vals->reg_num != 0xffff || vals->value != 0xff) {
		int ret = reg_write(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	dev_dbg(&client->dev, "Register list loaded\n");
	return 0;
}

static int ov5642_set_resolution(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	int width = priv->crop_rect.width;
	int height = priv->crop_rect.height;
	int total_width = priv->total_width;
	int total_height = priv->total_height;
	int start_x = (OV5642_SENSOR_SIZE_X - width) / 2;
	int start_y = (OV5642_SENSOR_SIZE_Y - height) / 2;
	int ret;

	/*
	 * This should set the starting point for cropping.
	 * Doesn't work so far.
	 */
	ret = reg_write16(client, REG_WINDOW_START_X_HIGH, start_x);
	if (!ret)
		ret = reg_write16(client, REG_WINDOW_START_Y_HIGH, start_y);
	if (!ret) {
		priv->crop_rect.left = start_x;
		priv->crop_rect.top = start_y;
	}

	if (!ret)
		ret = reg_write16(client, REG_WINDOW_WIDTH_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_WINDOW_HEIGHT_HIGH, height);
	if (ret)
		return ret;
	priv->crop_rect.width = width;
	priv->crop_rect.height = height;

	/* Set the output window size. Only 1:1 scale is supported so far. */
	ret = reg_write16(client, REG_OUT_WIDTH_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_OUT_HEIGHT_HIGH, height);

	/* Total width = output size + blanking */
	if (!ret)
		ret = reg_write16(client, REG_OUT_TOTAL_WIDTH_HIGH, total_width);
	if (!ret)
		ret = reg_write16(client, REG_OUT_TOTAL_HEIGHT_HIGH, total_height);

	/* Sets the window for AWB calculations */
	if (!ret)
		ret = reg_write16(client, REG_AVG_WINDOW_END_X_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_AVG_WINDOW_END_Y_HIGH, height);

	return ret;
}

static int ov5642_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	const struct ov5642_datafmt *fmt = ov5642_find_datafmt(mf->code);

	mf->width = priv->crop_rect.width;
	mf->height = priv->crop_rect.height;

	if (!fmt) {
		mf->code	= ov5642_colour_fmts[0].code;
		mf->colorspace	= ov5642_colour_fmts[0].colorspace;
	}

	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5642_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);

	/* MIPI CSI could have changed the format, double-check */
	if (!ov5642_find_datafmt(mf->code))
		return -EINVAL;

	ov5642_try_fmt(sd, mf);
	priv->fmt = ov5642_find_datafmt(mf->code);

	return 0;
}

static int ov5642_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);

	const struct ov5642_datafmt *fmt = priv->fmt;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->width	= priv->crop_rect.width;
	mf->height	= priv->crop_rect.height;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5642_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(ov5642_colour_fmts))
		return -EINVAL;

	*code = ov5642_colour_fmts[index].code;
	return 0;
}

static int ov5642_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	struct v4l2_rect rect = a->c;
	int ret;

	v4l_bound_align_image(&rect.width, 48, OV5642_MAX_WIDTH, 1,
			      &rect.height, 32, OV5642_MAX_HEIGHT, 1, 0);

	priv->crop_rect.width	= rect.width;
	priv->crop_rect.height	= rect.height;
	priv->total_width	= rect.width + BLANKING_EXTRA_WIDTH;
	priv->total_height	= max_t(int, rect.height +
							BLANKING_EXTRA_HEIGHT,
							BLANKING_MIN_HEIGHT);
	priv->crop_rect.width		= rect.width;
	priv->crop_rect.height		= rect.height;

	ret = ov5642_write_array(client, ov5642_default_regs_init);
	if (!ret)
		ret = ov5642_set_resolution(sd);
	if (!ret)
		ret = ov5642_write_array(client, ov5642_default_regs_finalise);

	return ret;
}

static int ov5642_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	struct v4l2_rect *rect = &a->c;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	*rect = priv->crop_rect;

	return 0;
}

static int ov5642_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= OV5642_MAX_WIDTH;
	a->bounds.height		= OV5642_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int ov5642_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_2_LANE | V4L2_MBUS_CSI2_CHANNEL_0 |
					V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static int ov5642_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	int ret;

	if (!on)
		return soc_camera_power_off(&client->dev, ssdd);

	ret = soc_camera_power_on(&client->dev, ssdd);
	if (ret < 0)
		return ret;

	ret = ov5642_write_array(client, ov5642_default_regs_init);
	if (!ret)
		ret = ov5642_set_resolution(sd);
	if (!ret)
		ret = ov5642_write_array(client, ov5642_default_regs_finalise);

	return ret;
}

static struct v4l2_subdev_video_ops ov5642_subdev_video_ops = {
	.s_mbus_fmt	= ov5642_s_fmt,
	.g_mbus_fmt	= ov5642_g_fmt,
	.try_mbus_fmt	= ov5642_try_fmt,
	.enum_mbus_fmt	= ov5642_enum_fmt,
	.s_crop		= ov5642_s_crop,
	.g_crop		= ov5642_g_crop,
	.cropcap	= ov5642_cropcap,
	.g_mbus_config	= ov5642_g_mbus_config,
};

static struct v4l2_subdev_core_ops ov5642_subdev_core_ops = {
	.s_power	= ov5642_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5642_get_register,
	.s_register	= ov5642_set_register,
#endif
};

static struct v4l2_subdev_ops ov5642_subdev_ops = {
	.core	= &ov5642_subdev_core_ops,
	.video	= &ov5642_subdev_video_ops,
};

static int ov5642_video_probe(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	int ret;
	u8 id_high, id_low;
	u16 id;

	ret = ov5642_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = reg_read(client, REG_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto done;

	id = id_high << 8;

	ret = reg_read(client, REG_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto done;

	id |= id_low;

	dev_info(&client->dev, "Chip ID 0x%04x detected\n", id);

	if (id != 0x5642) {
		ret = -ENODEV;
		goto done;
	}

	ret = 0;

done:
	ov5642_s_power(subdev, 0);
	return ret;
}

static int ov5642_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov5642 *priv;
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	if (!ssdd) {
		dev_err(&client->dev, "OV5642: missing platform data!\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct ov5642), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov5642_subdev_ops);

	priv->fmt		= &ov5642_colour_fmts[0];

	priv->crop_rect.width	= OV5642_DEFAULT_WIDTH;
	priv->crop_rect.height	= OV5642_DEFAULT_HEIGHT;
	priv->crop_rect.left	= (OV5642_MAX_WIDTH - OV5642_DEFAULT_WIDTH) / 2;
	priv->crop_rect.top	= (OV5642_MAX_HEIGHT - OV5642_DEFAULT_HEIGHT) / 2;
	priv->total_width = OV5642_DEFAULT_WIDTH + BLANKING_EXTRA_WIDTH;
	priv->total_height = BLANKING_MIN_HEIGHT;

	return ov5642_video_probe(client);
}

static int ov5642_remove(struct i2c_client *client)
{
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	if (ssdd->free_bus)
		ssdd->free_bus(ssdd);

	return 0;
}

static const struct i2c_device_id ov5642_id[] = {
	{ "ov5642", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5642_id);

static struct i2c_driver ov5642_i2c_driver = {
	.driver = {
		.name = "ov5642",
	},
	.probe		= ov5642_probe,
	.remove		= ov5642_remove,
	.id_table	= ov5642_id,
};

module_i2c_driver(ov5642_i2c_driver);

MODULE_DESCRIPTION("Omnivision OV5642 Camera driver");
MODULE_AUTHOR("Bastian Hecht <hechtb@gmail.com>");
MODULE_LICENSE("GPL v2");
