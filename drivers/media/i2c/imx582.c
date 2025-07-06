// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for the Sony IMX582 camera sensor.
 * Copyright (C) 2025 Vitalii Skorkin <nikroks@mainlining.com>
 *
 * Based on Sony imx355 camera driver
 * Copyright (C) 2018 Intel Corporation
 */
#include <linux/unaligned.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define IMX582_REG_MODE_SELECT		0x0100
#define IMX582_MODE_STANDBY		0x00
#define IMX582_MODE_STREAMING		0x01

#define IMX582_REG_RESET		0x0103

/* Chip ID */
#define IMX582_REG_CHIP_ID		0x0016
#define IMX582_CHIP_ID			0x0582

/* V_TIMING internal */
#define IMX582_REG_FLL			0x0340
#define IMX582_FLL_MAX			0xffff

/* Exposure control */
#define IMX582_REG_EXPOSURE		0x0202
#define IMX582_EXPOSURE_MIN		1
#define IMX582_EXPOSURE_STEP		1
#define IMX582_EXPOSURE_DEFAULT		0x0282

/* Analog gain control */
#define IMX582_REG_ANALOG_GAIN		0x0204
#define IMX582_ANA_GAIN_MIN		0
#define IMX582_ANA_GAIN_MAX		960
#define IMX582_ANA_GAIN_STEP		1
#define IMX582_ANA_GAIN_DEFAULT		0

/* Test Pattern Control */
#define IMX582_REG_TEST_PATTERN		0x0600
#define IMX582_TEST_PATTERN_DISABLED		0
#define IMX582_TEST_PATTERN_SOLID_COLOR		1
#define IMX582_TEST_PATTERN_COLOR_BARS		2
#define IMX582_TEST_PATTERN_GRAY_COLOR_BARS	3
#define IMX582_TEST_PATTERN_PN9			4

/* Flip Control */
#define IMX582_REG_ORIENTATION		0x0101

/* external clock */
#define IMX582_EXT_CLK			19200000

struct imx582_reg {
	u16 address;
	u8 val;
};

struct imx582_reg_list {
	u32 num_of_regs;
	const struct imx582_reg *regs;
};

/* Mode : resolution and related config&values */
struct imx582_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	/* V-timing */
	u32 fll_def;
	u32 fll_min;

	/* H-timing */
	u32 llp;

	/* index of link frequency */
	u32 link_freq_index;

	/* Default register values */
	struct imx582_reg_list reg_list;
};

static const char *imx582_supply_names[] = {
	"vana",
	"vdig",
	"vio",
	"custom",
};

struct imx582 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	/* Current mode */
	const struct imx582_mode *cur_mode;

	/*
	* Mutex for serialized access:
	* Protect sensor set pad format and start/stop streaming safely.
	* Protect access to sensor v4l2 controls.
	*/
	struct mutex mutex;

	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx582_supply_names)];
};

static const struct imx582_reg imx582_global_regs[] = {
	{ 0x0136, 0x13 },
	{ 0x0137, 0x33 },
	{ 0x3c7e, 0x02 },
	{ 0x3c7f, 0x06 },
	{ 0x3c00, 0x10 },
	{ 0x3c01, 0x10 },
	{ 0x3c02, 0x10 },
	{ 0x3c03, 0x10 },
	{ 0x3c04, 0x10 },
	{ 0x3c05, 0x01 },
	{ 0x3c06, 0x00 },
	{ 0x3c07, 0x00 },
	{ 0x3c08, 0x03 },
	{ 0x3c09, 0xff },
	{ 0x3c0a, 0x01 },
	{ 0x3c0b, 0x00 },
	{ 0x3c0c, 0x00 },
	{ 0x3c0d, 0x03 },
	{ 0x3c0e, 0xff },
	{ 0x3c0f, 0x20 },
	{ 0x6e1d, 0x00 },
	{ 0x6e25, 0x00 },
	{ 0x6e38, 0x03 },
	{ 0x6e3b, 0x01 },
	{ 0x9004, 0x2c },
	{ 0x9200, 0xf4 },
	{ 0x9201, 0xa7 },
	{ 0x9202, 0xf4 },
	{ 0x9203, 0xaa },
	{ 0x9204, 0xf4 },
	{ 0x9205, 0xad },
	{ 0x9206, 0xf4 },
	{ 0x9207, 0xb0 },
	{ 0x9208, 0xf4 },
	{ 0x9209, 0xb3 },
	{ 0x920a, 0xb7 },
	{ 0x920b, 0x34 },
	{ 0x920c, 0xb7 },
	{ 0x920d, 0x36 },
	{ 0x920e, 0xb7 },
	{ 0x920f, 0x37 },
	{ 0x9210, 0xb7 },
	{ 0x9211, 0x38 },
	{ 0x9212, 0xb7 },
	{ 0x9213, 0x39 },
	{ 0x9214, 0xb7 },
	{ 0x9215, 0x3a },
	{ 0x9216, 0xb7 },
	{ 0x9217, 0x3c },
	{ 0x9218, 0xb7 },
	{ 0x9219, 0x3d },
	{ 0x921a, 0xb7 },
	{ 0x921b, 0x3e },
	{ 0x921c, 0xb7 },
	{ 0x921d, 0x3f },
	{ 0x921e, 0x85 },
	{ 0x921f, 0x77 },
	{ 0x9226, 0x42 },
	{ 0x9227, 0x52 },
	{ 0x9228, 0x60 },
	{ 0x9229, 0xb9 },
	{ 0x922a, 0x60 },
	{ 0x922b, 0xbf },
	{ 0x922c, 0x60 },
	{ 0x922d, 0xc5 },
	{ 0x922e, 0x60 },
	{ 0x922f, 0xcb },
	{ 0x9230, 0x60 },
	{ 0x9231, 0xd1 },
	{ 0x9232, 0x60 },
	{ 0x9233, 0xd7 },
	{ 0x9234, 0x60 },
	{ 0x9235, 0xdd },
	{ 0x9236, 0x60 },
	{ 0x9237, 0xe3 },
	{ 0x9238, 0x60 },
	{ 0x9239, 0xe9 },
	{ 0x923a, 0x60 },
	{ 0x923b, 0xef },
	{ 0x923c, 0x60 },
	{ 0x923d, 0xf5 },
	{ 0x923e, 0x60 },
	{ 0x923f, 0xf9 },
	{ 0x9240, 0x60 },
	{ 0x9241, 0xfd },
	{ 0x9242, 0x61 },
	{ 0x9243, 0x01 },
	{ 0x9244, 0x61 },
	{ 0x9245, 0x05 },
	{ 0x924a, 0x61 },
	{ 0x924b, 0x6b },
	{ 0x924c, 0x61 },
	{ 0x924d, 0x7f },
	{ 0x924e, 0x61 },
	{ 0x924f, 0x92 },
	{ 0x9250, 0x61 },
	{ 0x9251, 0x9c },
	{ 0x9252, 0x61 },
	{ 0x9253, 0xab },
	{ 0x9254, 0x61 },
	{ 0x9255, 0xc4 },
	{ 0x9256, 0x61 },
	{ 0x9257, 0xce },
	{ 0x9810, 0x14 },
	{ 0x9814, 0x14 },
	{ 0xc449, 0x04 },
	{ 0xc44a, 0x01 },
	{ 0xe286, 0x31 },
	{ 0xe2a6, 0x32 },
	{ 0xe2c6, 0x33 },
	{ 0x88d6, 0x60 },
	{ 0x9852, 0x00 },
	{ 0xae09, 0xff },
	{ 0xae0a, 0xff },
	{ 0xae12, 0x58 },
	{ 0xae13, 0x58 },
	{ 0xae15, 0x10 },
	{ 0xae16, 0x10 },
	{ 0xb071, 0x00 }
};

static const struct imx582_reg_list imx582_global_setting = {
	.num_of_regs = ARRAY_SIZE(imx582_global_regs),
	.regs = imx582_global_regs,
};

static const struct imx582_reg mode_8000x6000_regs[] = {
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x03 },
	{ 0x0342, 0x23 },
	{ 0x0343, 0xe0 },
	{ 0x0340, 0x18 },
	{ 0x0341, 0x2c },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x1f },
	{ 0x0349, 0x3f },
	{ 0x034a, 0x17 },
	{ 0x034b, 0x6f },
	{ 0x0900, 0x00 },
	{ 0x0901, 0x11 },
	{ 0x0902, 0x0a },
	{ 0x3246, 0x01 },
	{ 0x3247, 0x01 },
	{ 0x0401, 0x00 },
	{ 0x0404, 0x00 },
	{ 0x0405, 0x10 },
	{ 0x0408, 0x00 },
	{ 0x0409, 0x00 },
	{ 0x040a, 0x00 },
	{ 0x040b, 0x00 },
	{ 0x040c, 0x1f },
	{ 0x040d, 0x40 },
	{ 0x040e, 0x17 },
	{ 0x040f, 0x70 },
	{ 0x034c, 0x1f },
	{ 0x034d, 0x40 },
	{ 0x034e, 0x17 },
	{ 0x034f, 0x70 },
	{ 0x0301, 0x05 },
	{ 0x0303, 0x02 },
	{ 0x0305, 0x03 },
	{ 0x0306, 0x01 },
	{ 0x0307, 0x4d },
	{ 0x030b, 0x01 },
	{ 0x030d, 0x10 },
	{ 0x030e, 0x06 },
	{ 0x030f, 0xd5 },
	{ 0x0310, 0x01 },
	{ 0x3620, 0x01 },
	{ 0x3621, 0x01 },
	{ 0x380c, 0x80 },
	{ 0x3c13, 0x00 },
	{ 0x3c14, 0x28 },
	{ 0x3c15, 0x28 },
	{ 0x3c16, 0x32 },
	{ 0x3c17, 0x46 },
	{ 0x3c18, 0x67 },
	{ 0x3c19, 0x8f },
	{ 0x3c1a, 0x8f },
	{ 0x3c1b, 0x99 },
	{ 0x3c1c, 0xad },
	{ 0x3c1d, 0xce },
	{ 0x3c1e, 0x8f },
	{ 0x3c1f, 0x8f },
	{ 0x3c20, 0x99 },
	{ 0x3c21, 0xad },
	{ 0x3c22, 0xce },
	{ 0x3c25, 0x22 },
	{ 0x3c26, 0x23 },
	{ 0x3c27, 0xe6 },
	{ 0x3c28, 0xe6 },
	{ 0x3c29, 0x08 },
	{ 0x3c2a, 0x0f },
	{ 0x3c2b, 0x14 },
	{ 0x3f0c, 0x00 },
	{ 0x3f14, 0x01 },
	{ 0x3f80, 0x04 },
	{ 0x3f81, 0xb0 },
	{ 0x3f82, 0x00 },
	{ 0x3f83, 0x00 },
	{ 0x3f8c, 0x03 },
	{ 0x3f8d, 0x5c },
	{ 0x3ff4, 0x00 },
	{ 0x3ff5, 0x00 },
	{ 0x3ffc, 0x00 },
	{ 0x3ffd, 0x00 },
	{ 0x0202, 0x17 },
	{ 0x0203, 0xfc },
	{ 0x0224, 0x01 },
	{ 0x0225, 0xf4 },
	{ 0x3fe0, 0x01 },
	{ 0x3fe1, 0xf4 },
	{ 0x0204, 0x00 },
	{ 0x0205, 0x70 },
	{ 0x0216, 0x00 },
	{ 0x0217, 0x70 },
	{ 0x0218, 0x01 },
	{ 0x0219, 0x00 },
	{ 0x020e, 0x01 },
	{ 0x020f, 0x00 },
	{ 0x0210, 0x01 },
	{ 0x0211, 0x00 },
	{ 0x0212, 0x01 },
	{ 0x0213, 0x00 },
	{ 0x0214, 0x01 },
	{ 0x0215, 0x00 },
	{ 0x3fe2, 0x00 },
	{ 0x3fe3, 0x70 },
	{ 0x3fe4, 0x01 },
	{ 0x3fe5, 0x00 },
	{ 0x3e20, 0x02 },
	{ 0x3e3b, 0x01 },
	{ 0x4034, 0x01 },
	{ 0x4035, 0xf0 },
};

static const struct imx582_reg mode_4000x3000_regs[] = {
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x03 },
	{ 0x0342, 0x1e },
	{ 0x0343, 0xc0 },
	{ 0x0340, 0x0b },
	{ 0x0341, 0xee },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x1f },
	{ 0x0349, 0x3f },
	{ 0x034a, 0x17 },
	{ 0x034b, 0x6f },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0902, 0x08 },
	{ 0x3246, 0x81 },
	{ 0x3247, 0x81 },
	{ 0x0401, 0x00 },
	{ 0x0404, 0x00 },
	{ 0x0405, 0x10 },
	{ 0x0408, 0x00 },
	{ 0x0409, 0x00 },
	{ 0x040a, 0x00 },
	{ 0x040b, 0x00 },
	{ 0x040c, 0x0f },
	{ 0x040d, 0xa0 },
	{ 0x040e, 0x0b },
	{ 0x040f, 0xb8 },
	{ 0x034c, 0x0f },
	{ 0x034d, 0xa0 },
	{ 0x034e, 0x0b },
	{ 0x034f, 0xb8 },
	{ 0x0301, 0x05 },
	{ 0x0303, 0x02 },
	{ 0x0305, 0x03 },
	{ 0x0306, 0x01 },
	{ 0x0307, 0x1a },
	{ 0x030b, 0x01 },
	{ 0x030d, 0x10 },
	{ 0x030e, 0x03 },
	{ 0x030f, 0xe4 },
	{ 0x0310, 0x01 },
	{ 0x3620, 0x00 },
	{ 0x3621, 0x00 },
	{ 0x380c, 0x80 },
	{ 0x3c13, 0x00 },
	{ 0x3c14, 0x28 },
	{ 0x3c15, 0x28 },
	{ 0x3c16, 0x32 },
	{ 0x3c17, 0x46 },
	{ 0x3c18, 0x67 },
	{ 0x3c19, 0x8f },
	{ 0x3c1a, 0x8f },
	{ 0x3c1b, 0x99 },
	{ 0x3c1c, 0xad },
	{ 0x3c1d, 0xce },
	{ 0x3c1e, 0x8f },
	{ 0x3c1f, 0x8f },
	{ 0x3c20, 0x99 },
	{ 0x3c21, 0xad },
	{ 0x3c22, 0xce },
	{ 0x3c25, 0x22 },
	{ 0x3c26, 0x23 },
	{ 0x3c27, 0xe6 },
	{ 0x3c28, 0xe6 },
	{ 0x3c29, 0x08 },
	{ 0x3c2a, 0x0f },
	{ 0x3c2b, 0x14 },
	{ 0x3f0c, 0x01 },
	{ 0x3f14, 0x00 },
	{ 0x3f80, 0x00 },
	{ 0x3f81, 0x00 },
	{ 0x3f82, 0x00 },
	{ 0x3f83, 0x00 },
	{ 0x3f8c, 0x07 },
	{ 0x3f8d, 0xd0 },
	{ 0x3ff4, 0x00 },
	{ 0x3ff5, 0x00 },
	{ 0x3ffc, 0x04 },
	{ 0x3ffd, 0xb0 },
	{ 0x0202, 0x0b },
	{ 0x0203, 0xbe },
	{ 0x0224, 0x01 },
	{ 0x0225, 0xf4 },
	{ 0x3fe0, 0x01 },
	{ 0x3fe1, 0xf4 },
	{ 0x0204, 0x00 },
	{ 0x0205, 0x70 },
	{ 0x0216, 0x00 },
	{ 0x0217, 0x70 },
	{ 0x0218, 0x01 },
	{ 0x0219, 0x00 },
	{ 0x020e, 0x01 },
	{ 0x020f, 0x00 },
	{ 0x0210, 0x01 },
	{ 0x0211, 0x00 },
	{ 0x0212, 0x01 },
	{ 0x0213, 0x00 },
	{ 0x0214, 0x01 },
	{ 0x0215, 0x00 },
	{ 0x3fe2, 0x00 },
	{ 0x3fe3, 0x70 },
	{ 0x3fe4, 0x01 },
	{ 0x3fe5, 0x00 },
	{ 0x3e20, 0x02 },
	{ 0x3e3b, 0x01 },
	{ 0x4034, 0x01 },
	{ 0x4035, 0xf0 },
};

static const struct imx582_reg mode_4000x2256_regs[] = {
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x03 },
	{ 0x0342, 0x1e },
	{ 0x0343, 0xc0 },
	{ 0x0340, 0x0b },
	{ 0x0341, 0xee },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x1f },
	{ 0x0349, 0x3f },
	{ 0x034a, 0x17 },
	{ 0x034b, 0x6f },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0902, 0x08 },
	{ 0x3246, 0x81 },
	{ 0x3247, 0x81 },
	{ 0x0401, 0x00 },
	{ 0x0404, 0x00 },
	{ 0x0405, 0x10 },
	{ 0x0408, 0x00 },
	{ 0x0409, 0x00 },
	{ 0x040a, 0x00 },
	{ 0x040b, 0x00 },
	{ 0x040c, 0x0f },
	{ 0x040d, 0xa0 },
	{ 0x040e, 0x0b },
	{ 0x040f, 0xb8 },
	{ 0x034c, 0x0f },
	{ 0x034d, 0xa0 },
	{ 0x034e, 0x0b },
	{ 0x034f, 0xb8 },
	{ 0x0301, 0x05 },
	{ 0x0303, 0x02 },
	{ 0x0305, 0x03 },
	{ 0x0306, 0x01 },
	{ 0x0307, 0x1a },
	{ 0x030b, 0x01 },
	{ 0x030d, 0x10 },
	{ 0x030e, 0x03 },
	{ 0x030f, 0xe4 },
	{ 0x0310, 0x01 },
	{ 0x3620, 0x00 },
	{ 0x3621, 0x00 },
	{ 0x380c, 0x80 },
	{ 0x3c13, 0x00 },
	{ 0x3c14, 0x28 },
	{ 0x3c15, 0x28 },
	{ 0x3c16, 0x32 },
	{ 0x3c17, 0x46 },
	{ 0x3c18, 0x67 },
	{ 0x3c19, 0x8f },
	{ 0x3c1a, 0x8f },
	{ 0x3c1b, 0x99 },
	{ 0x3c1c, 0xad },
	{ 0x3c1d, 0xce },
	{ 0x3c1e, 0x8f },
	{ 0x3c1f, 0x8f },
	{ 0x3c20, 0x99 },
	{ 0x3c21, 0xad },
	{ 0x3c22, 0xce },
	{ 0x3c25, 0x22 },
	{ 0x3c26, 0x23 },
	{ 0x3c27, 0xe6 },
	{ 0x3c28, 0xe6 },
	{ 0x3c29, 0x08 },
	{ 0x3c2a, 0x0f },
	{ 0x3c2b, 0x14 },
	{ 0x3f0c, 0x01 },
	{ 0x3f14, 0x00 },
	{ 0x3f80, 0x00 },
	{ 0x3f81, 0x00 },
	{ 0x3f82, 0x00 },
	{ 0x3f83, 0x00 },
	{ 0x3f8c, 0x07 },
	{ 0x3f8d, 0xd0 },
	{ 0x3ff4, 0x00 },
	{ 0x3ff5, 0x00 },
	{ 0x3ffc, 0x04 },
	{ 0x3ffd, 0xb0 },
	{ 0x0202, 0x0b },
	{ 0x0203, 0xbe },
	{ 0x0224, 0x01 },
	{ 0x0225, 0xf4 },
	{ 0x3fe0, 0x01 },
	{ 0x3fe1, 0xf4 },
	{ 0x0204, 0x00 },
	{ 0x0205, 0x70 },
	{ 0x0216, 0x00 },
	{ 0x0217, 0x70 },
	{ 0x0218, 0x01 },
	{ 0x0219, 0x00 },
	{ 0x020e, 0x01 },
	{ 0x020f, 0x00 },
	{ 0x0210, 0x01 },
	{ 0x0211, 0x00 },
	{ 0x0212, 0x01 },
	{ 0x0213, 0x00 },
	{ 0x0214, 0x01 },
	{ 0x0215, 0x00 },
	{ 0x3fe2, 0x00 },
	{ 0x3fe3, 0x70 },
	{ 0x3fe4, 0x01 },
	{ 0x3fe5, 0x00 },
	{ 0x3e20, 0x02 },
	{ 0x3e3b, 0x01 },
	{ 0x4034, 0x01 },
	{ 0x4035, 0xf0 },
};

static const struct imx582_reg mode_1920x1080_regs[] = {
	{ 0x0112, 0x0a },
	{ 0x0113, 0x0a },
	{ 0x0114, 0x03 },
	{ 0x0342, 0x0b },
	{ 0x0343, 0x60 },
	{ 0x0340, 0x04 },
	{ 0x0341, 0x7e },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x03 },
	{ 0x0347, 0x40 },
	{ 0x0348, 0x1f },
	{ 0x0349, 0x3f },
	{ 0x034a, 0x14 },
	{ 0x034b, 0x1f },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x44 },
	{ 0x0902, 0x08 },
	{ 0x3246, 0x89 },
	{ 0x3247, 0x89 },
	{ 0x0401, 0x00 },
	{ 0x0404, 0x00 },
	{ 0x0405, 0x10 },
	{ 0x0408, 0x00 },
	{ 0x0409, 0x28 },
	{ 0x040a, 0x00 },
	{ 0x040b, 0x00 },
	{ 0x040c, 0x07 },
	{ 0x040d, 0x80 },
	{ 0x040e, 0x04 },
	{ 0x040f, 0x38 },
	{ 0x034c, 0x07 },
	{ 0x034d, 0x80 },
	{ 0x034e, 0x04 },
	{ 0x034f, 0x38 },
	{ 0x0301, 0x05 },
	{ 0x0303, 0x04 },
	{ 0x0305, 0x03 },
	{ 0x0306, 0x01 },
	{ 0x0307, 0x3a },
	{ 0x030b, 0x02 },
	{ 0x030d, 0x10 },
	{ 0x030e, 0x04 },
	{ 0x030f, 0xa9 },
	{ 0x0310, 0x01 },
	{ 0x3620, 0x00 },
	{ 0x3621, 0x00 },
	{ 0x380c, 0x80 },
	{ 0x3c13, 0x00 },
	{ 0x3c14, 0x28 },
	{ 0x3c15, 0x28 },
	{ 0x3c16, 0x32 },
	{ 0x3c17, 0x46 },
	{ 0x3c18, 0x67 },
	{ 0x3c19, 0x8f },
	{ 0x3c1a, 0x8f },
	{ 0x3c1b, 0x99 },
	{ 0x3c1c, 0xad },
	{ 0x3c1d, 0xce },
	{ 0x3c1e, 0x8f },
	{ 0x3c1f, 0x8f },
	{ 0x3c20, 0x99 },
	{ 0x3c21, 0xad },
	{ 0x3c22, 0xce },
	{ 0x3c25, 0x22 },
	{ 0x3c26, 0x23 },
	{ 0x3c27, 0xe6 },
	{ 0x3c28, 0xe6 },
	{ 0x3c29, 0x08 },
	{ 0x3c2a, 0x0f },
	{ 0x3c2b, 0x14 },
	{ 0x3f0c, 0x00 },
	{ 0x3f14, 0x00 },
	{ 0x3f80, 0x00 },
	{ 0x3f81, 0x00 },
	{ 0x3f82, 0x00 },
	{ 0x3f83, 0x00 },
	{ 0x3f8c, 0x00 },
	{ 0x3f8d, 0x00 },
	{ 0x3ff4, 0x00 },
	{ 0x3ff5, 0x4c },
	{ 0x3ffc, 0x00 },
	{ 0x3ffd, 0x00 },
	{ 0x0202, 0x04 },
	{ 0x0203, 0x4e },
	{ 0x0224, 0x01 },
	{ 0x0225, 0xf4 },
	{ 0x3fe0, 0x01 },
	{ 0x3fe1, 0xf4 },
	{ 0x0204, 0x00 },
	{ 0x0205, 0x70 },
	{ 0x0216, 0x00 },
	{ 0x0217, 0x70 },
	{ 0x0218, 0x01 },
	{ 0x0219, 0x00 },
	{ 0x020e, 0x01 },
	{ 0x020f, 0x00 },
	{ 0x0210, 0x01 },
	{ 0x0211, 0x00 },
	{ 0x0212, 0x01 },
	{ 0x0213, 0x00 },
	{ 0x0214, 0x01 },
	{ 0x0215, 0x00 },
	{ 0x3fe2, 0x00 },
	{ 0x3fe3, 0x70 },
	{ 0x3fe4, 0x01 },
	{ 0x3fe5, 0x00 },
	{ 0x3e20, 0x01 },
	{ 0x3e3b, 0x00 },
	{ 0x4034, 0x01 },
	{ 0x4035, 0xf0 },
};


static const char * const imx582_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

static const s64 link_freq_menu_items[] = {
	720000000
};

/* Mode configs */
static const struct imx582_mode supported_modes[] = {
	{
		.width = 4000,
		.height = 3000,
		.fll_def = 3054,
		.fll_min = 3054,
		.llp = 7872,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4000x3000_regs),
			.regs = mode_4000x3000_regs,
		},
	},
	{
		.width = 4000,
		.height = 2256,
		.fll_def = 2340,
		.fll_min = 2340,
		.llp = 4592,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4000x2256_regs),
			.regs = mode_4000x2256_regs,
		},
	},
	{
		.width = 1920,
		.height = 1080,
		.fll_def = 1150,
		.fll_min = 1150,
		.llp = 2912,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920x1080_regs),
			.regs = mode_1920x1080_regs,
		},
	}
};

static inline struct imx582 *to_imx582(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx582, sd);
}

/* Get bayer order based on flip setting. */
static u32 imx582_get_format_code(struct imx582 *imx582)
{
	/*
	* Only one bayer order is supported.
	* It depends on the flip settings.
	*/
	u32 code;
	static const u32 codes[2][2] = {
		{ MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SGRBG10_1X10, },
		{ MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SBGGR10_1X10, },
	};

	lockdep_assert_held(&imx582->mutex);
	code = codes[imx582->vflip->val][imx582->hflip->val];

	return code;
}

/* Read registers up to 4 at a time */
static int imx582_read_reg(struct imx582 *imx582, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = { 0 };
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/* Write registers up to 4 at a time */
static int imx582_write_reg(struct imx582 *imx582, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int imx582_write_regs(struct imx582 *imx582,
				const struct imx582_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = imx582_write_reg(imx582, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
						"write reg 0x%4.4x return err %d",
						regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static void imx582_update_pad_format(struct imx582 *imx582,
	const struct imx582_mode *mode,
	struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = imx582_get_format_code(imx582);
	fmt->format.field = V4L2_FIELD_NONE;
}

static int
imx582_set_pad_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_format *fmt)
{
	struct imx582 *imx582 = to_imx582(sd);
	const struct imx582_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	u64 pixel_rate;
	u32 height;

	mutex_lock(&imx582->mutex);

	/*
	* Only one bayer order is supported.
	* It depends on the flip settings.
	*/
	fmt->format.code = imx582_get_format_code(imx582);

	mode = v4l2_find_nearest_size(supported_modes,
					ARRAY_SIZE(supported_modes),
					width, height,
					fmt->format.width, fmt->format.height);
	imx582_update_pad_format(imx582, mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		imx582->cur_mode = mode;
		pixel_rate = link_freq_menu_items[imx582->cur_mode->link_freq_index] * 2 * 4;
		do_div(pixel_rate, 10);
		__v4l2_ctrl_s_ctrl_int64(imx582->pixel_rate, pixel_rate);
		/* Update limits and set FPS to default */
		height = imx582->cur_mode->height;
		vblank_def = imx582->cur_mode->fll_def - height;
		vblank_min = imx582->cur_mode->fll_min - height;
		height = IMX582_FLL_MAX - height;
		__v4l2_ctrl_modify_range(imx582->vblank, vblank_min, height, 1,
					vblank_def);
		__v4l2_ctrl_s_ctrl(imx582->vblank, vblank_def);
		h_blank = mode->llp - imx582->cur_mode->width;
		/*
		* Currently hblank is not changeable.
		* So FPS control is done only by vblank.
		*/
		__v4l2_ctrl_modify_range(imx582->hblank, h_blank,
					h_blank, 1, h_blank);
	}

	mutex_unlock(&imx582->mutex);

	return 0;
}

static int imx582_init_state(struct v4l2_subdev *sd,
	struct v4l2_subdev_state *sd_state)
{
	struct imx582 *imx582 = to_imx582(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = imx582->cur_mode->width,
			.height = imx582->cur_mode->height,
		},
	};

	mutex_lock(&imx582->mutex);
	fmt.format.code = imx582_get_format_code(imx582);
	mutex_unlock(&imx582->mutex);

	return imx582_set_pad_format(sd, sd_state, &fmt);
}

static int imx582_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx582 *imx582 = container_of(ctrl->handler,
						struct imx582, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	s64 max;
	int ret;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx582->cur_mode->height + ctrl->val - 10;
		__v4l2_ctrl_modify_range(imx582->exposure,
					imx582->exposure->minimum,
					max, imx582->exposure->step, max);
		break;
	}

	/*
	* Applying V4L2 control value only happens
	* when power is up for streaming
	*/
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		/* Analog gain = 1024/(1024 - ctrl->val) times */
		ret = imx582_write_reg(imx582, IMX582_REG_ANALOG_GAIN, 2,
					ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = imx582_write_reg(imx582, IMX582_REG_EXPOSURE, 2,
					ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = imx582_write_reg(imx582, IMX582_REG_FLL, 2,
					imx582->cur_mode->height + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx582_write_reg(imx582, IMX582_REG_TEST_PATTERN,
					2, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = imx582_write_reg(imx582, IMX582_REG_ORIENTATION, 1,
					imx582->hflip->val |
					imx582->vflip->val << 1);
		break;
	default:
		ret = -EINVAL;
		dev_info(&client->dev, "ctrl(id:0x%x,val:0x%x) is not handled",
			ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx582_ctrl_ops = {
	.s_ctrl = imx582_set_ctrl,
};

static int imx582_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx582 *imx582 = to_imx582(sd);

	if (code->index > 0)
		return -EINVAL;

	mutex_lock(&imx582->mutex);
	code->code = imx582_get_format_code(imx582);
	mutex_unlock(&imx582->mutex);

	return 0;
}

static int imx582_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx582 *imx582 = to_imx582(sd);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	mutex_lock(&imx582->mutex);
	if (fse->code != imx582_get_format_code(imx582)) {
		mutex_unlock(&imx582->mutex);
		return -EINVAL;
	}
	mutex_unlock(&imx582->mutex);

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx582_do_get_pad_format(struct imx582 *imx582,
					struct v4l2_subdev_state *sd_state,
					struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx582_update_pad_format(imx582, imx582->cur_mode, fmt);
	}

	return 0;
}

static int imx582_get_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_format *fmt)
{
	struct imx582 *imx582 = to_imx582(sd);
	int ret;

	mutex_lock(&imx582->mutex);
	ret = imx582_do_get_pad_format(imx582, sd_state, fmt);
	mutex_unlock(&imx582->mutex);

	return ret;
}

/* Start streaming */
static int imx582_start_streaming(struct imx582 *imx582)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	const struct imx582_reg_list *reg_list;
	int ret;

	ret = imx582_write_reg(imx582, IMX582_REG_RESET, 0x01, 0x01);
	if (ret) {
		dev_err(&client->dev, "%s failed to reset sensor\n", __func__);
		return ret;
	}

	/* 12ms is required from poweron to standby */
	fsleep(12000);

	/* Global Setting */
	reg_list = &imx582_global_setting;
	ret = imx582_write_regs(imx582, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "failed to set global settings");
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &imx582->cur_mode->reg_list;
	ret = imx582_write_regs(imx582, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx582->sd.ctrl_handler);
	if (ret)
		return ret;

	return imx582_write_reg(imx582, IMX582_REG_MODE_SELECT,
				1, IMX582_MODE_STREAMING);
}

/* Stop streaming */
static int imx582_stop_streaming(struct imx582 *imx582)
{
	return imx582_write_reg(imx582, IMX582_REG_MODE_SELECT,
				1, IMX582_MODE_STANDBY);
}

static int imx582_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx582 *imx582 = to_imx582(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx582->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		* Apply default & customized values
		* and then start streaming.
		*/
		ret = imx582_start_streaming(imx582);
		if (ret)
			goto err_rpm_put;
	} else {
		imx582_stop_streaming(imx582);
		pm_runtime_put(&client->dev);
	}

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(imx582->vflip, enable);
	__v4l2_ctrl_grab(imx582->hflip, enable);

	mutex_unlock(&imx582->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx582->mutex);

	return ret;
}

/* Verify chip ID */
static int imx582_identify_module(struct imx582 *imx582)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	int ret;
	u32 val;

	ret = imx582_read_reg(imx582, IMX582_REG_CHIP_ID, 2, &val);
	if (ret)
		return ret;

	if (val != IMX582_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			IMX582_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops imx582_video_ops = {
	.s_stream = imx582_set_stream,
};

static const struct v4l2_subdev_pad_ops imx582_pad_ops = {
	.enum_mbus_code = imx582_enum_mbus_code,
	.enum_frame_size = imx582_enum_frame_size,
	.get_fmt = imx582_get_pad_format,
	.set_fmt = imx582_set_pad_format,
};

static const struct v4l2_subdev_ops imx582_subdev_ops = {
	.video = &imx582_video_ops,
	.pad = &imx582_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx582_internal_ops = {
	.init_state = imx582_init_state,
};

static int imx582_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx582 *imx582 = to_imx582(sd);
	int ret;

	clk_disable_unprepare(imx582->mclk);

	gpiod_set_value_cansleep(imx582->reset_gpio, 0);

	ret = regulator_bulk_disable(ARRAY_SIZE(imx582->supplies),
					imx582->supplies);
	if (ret) {
		dev_err(dev, "failed to disable regulators: %d\n", ret);
		return ret;
	}

	return 0;
}

static int imx582_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx582 *imx582 = to_imx582(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx582->supplies),
					imx582->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(imx582->reset_gpio, 1);

	clk_prepare_enable(imx582->mclk);
	usleep_range(12000, 13000);

	return 0;
}

DEFINE_RUNTIME_DEV_PM_OPS(imx582_pm_ops, imx582_suspend, imx582_resume, NULL);

/* Initialize control handlers */
static int imx582_init_controls(struct imx582 *imx582)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx582->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 hblank;
	u64 pixel_rate;
	const struct imx582_mode *mode;
	u32 max;
	int ret;

	ctrl_hdlr = &imx582->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &imx582->mutex;
	max = ARRAY_SIZE(link_freq_menu_items) - 1;
	imx582->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx582_ctrl_ops,
						V4L2_CID_LINK_FREQ, max, 0,
						link_freq_menu_items);
	if (imx582->link_freq)
		imx582->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* pixel_rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = link_freq_menu_items[0] * 2 * 4;
	do_div(pixel_rate, 10);
	/* By default, PIXEL_RATE is read only */
	imx582->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
						V4L2_CID_PIXEL_RATE, pixel_rate,
						pixel_rate, 1, pixel_rate);

	/* Initialize vblank/hblank/exposure parameters based on current mode */
	mode = imx582->cur_mode;
	vblank_def = mode->fll_def - mode->height;
	vblank_min = mode->fll_min - mode->height;
	imx582->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
					V4L2_CID_VBLANK, vblank_min,
					IMX582_FLL_MAX - mode->height,
					1, vblank_def);

	hblank = mode->llp - mode->width;
	imx582->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
					V4L2_CID_HBLANK, hblank, hblank,
					1, hblank);
	if (imx582->hblank)
		imx582->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* fll >= exposure time + adjust parameter (default value is 10) */
	exposure_max = mode->fll_def - 10;
	imx582->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
						V4L2_CID_EXPOSURE,
						IMX582_EXPOSURE_MIN, exposure_max,
						IMX582_EXPOSURE_STEP,
						IMX582_EXPOSURE_DEFAULT);

	imx582->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
					V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx582->hflip)
		imx582->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	imx582->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops,
					V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx582->vflip)
		imx582->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std(ctrl_hdlr, &imx582_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			IMX582_ANA_GAIN_MIN, IMX582_ANA_GAIN_MAX,
			IMX582_ANA_GAIN_STEP, IMX582_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx582_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(imx582_test_pattern_menu) - 1,
					0, 0, imx582_test_pattern_menu);
	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "control init failed: %d", ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx582_ctrl_ops,
						&props);
	if (ret)
		goto error;

	imx582->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int imx582_probe(struct i2c_client *client)
{
	struct imx582 *imx582;
	size_t i;
	int ret;

	imx582 = devm_kzalloc(&client->dev, sizeof(*imx582), GFP_KERNEL);
	if (!imx582)
		return -ENOMEM;

	mutex_init(&imx582->mutex);

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx582->sd, client, &imx582_subdev_ops);

	for (i = 0; i < ARRAY_SIZE(imx582_supply_names); i++)
		imx582->supplies[i].supply = imx582_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev,
					ARRAY_SIZE(imx582->supplies),
					imx582->supplies);
	if (ret) {
		dev_err_probe(&client->dev, ret, "could not get regulators");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(imx582->supplies),
					imx582->supplies);
	if (ret) {
		dev_err(&client->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	imx582->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(imx582->reset_gpio)) {
		ret = PTR_ERR(imx582->reset_gpio);
		dev_err_probe(&client->dev, ret, "failed to get gpios");
		goto error_vreg_disable;
	}

	imx582->mclk = devm_clk_get(&client->dev, "mclk");
	if (IS_ERR(imx582->mclk)) {
		ret = PTR_ERR(imx582->mclk);
		dev_err_probe(&client->dev, ret, "failed to get mclk");
		goto error_vreg_disable;
	}

	clk_prepare_enable(imx582->mclk);
	usleep_range(12000, 13000);

	/* Check module identity */
	ret = imx582_identify_module(imx582);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		goto error_probe;
	}

	/* Set default mode to max resolution */
	imx582->cur_mode = &supported_modes[0];

	ret = imx582_init_controls(imx582);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto error_probe;
	}

	/* Initialize subdev */
	imx582->sd.internal_ops = &imx582_internal_ops;
	imx582->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx582->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx582->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx582->sd.entity, 1, &imx582->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	/*
	* Device is already turned on by i2c-core with ACPI domain PM.
	* Enable runtime PM and turn off the device.
	*/
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&imx582->sd);
	if (ret < 0)
		goto error_media_entity_runtime_pm;

	return 0;

error_media_entity_runtime_pm:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	media_entity_cleanup(&imx582->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx582->sd.ctrl_handler);

error_probe:
	mutex_destroy(&imx582->mutex);
	clk_disable_unprepare(imx582->mclk);

error_vreg_disable:
	regulator_bulk_disable(ARRAY_SIZE(imx582->supplies), imx582->supplies);

	return ret;
}

static void imx582_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx582 *imx582 = to_imx582(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx582->mutex);
}

static const struct of_device_id imx582_match_table[] __maybe_unused = {
	{ .compatible = "sony,imx582", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx582_match_table);

static struct i2c_driver imx582_i2c_driver = {
	.driver = {
		.name = "imx582",
		.of_match_table = of_match_ptr(imx582_match_table),
		.pm = &imx582_pm_ops,
	},
	.probe = imx582_probe,
	.remove = imx582_remove,
};
module_i2c_driver(imx582_i2c_driver);

MODULE_DESCRIPTION("Sony IMX582 sensor driver");
MODULE_LICENSE("GPL");
