// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony IMX708 cameras.
 * Copyright (C) 2022-2023, Raspberry Pi Ltd
 *
 * Based on Sony imx477 camera driver
 * Copyright (C) 2020 Raspberry Pi Ltd
 */
#include <asm/unaligned.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#define IMX708_REG_VALUE_08BIT		1
#define IMX708_REG_VALUE_16BIT		2

/* Chip ID */
#define IMX708_REG_CHIP_ID		0x0016
#define IMX708_CHIP_ID			0x0708

#define IMX708_REG_MODE_SELECT		0x0100
#define IMX708_MODE_STANDBY		0x00
#define IMX708_MODE_STREAMING		0x01

#define IMX708_REG_ORIENTATION		0x101

#define IMX708_XCLK_FREQ		24000000

#define IMX708_DEFAULT_LINK_FREQ	450000000

/* V_TIMING internal */
#define IMX708_REG_FRAME_LENGTH		0x0340
#define IMX708_FRAME_LENGTH_MAX		0xffff

/* Long exposure multiplier */
#define IMX708_LONG_EXP_SHIFT_MAX	7
#define IMX708_LONG_EXP_SHIFT_REG	0x3100

/* Exposure control */
#define IMX708_REG_EXPOSURE		0x0202
#define IMX708_EXPOSURE_OFFSET		48
#define IMX708_EXPOSURE_DEFAULT		0x640
#define IMX708_EXPOSURE_STEP		1
#define IMX708_EXPOSURE_MIN		1
#define IMX708_EXPOSURE_MAX		(IMX708_FRAME_LENGTH_MAX - \
					 IMX708_EXPOSURE_OFFSET)

/* Analog gain control */
#define IMX708_REG_ANALOG_GAIN		0x0204
#define IMX708_ANA_GAIN_MIN		112
#define IMX708_ANA_GAIN_MAX		960
#define IMX708_ANA_GAIN_STEP		1
#define IMX708_ANA_GAIN_DEFAULT		IMX708_ANA_GAIN_MIN

/* Digital gain control */
#define IMX708_REG_DIGITAL_GAIN		0x020e
#define IMX708_DGTL_GAIN_MIN		0x0100
#define IMX708_DGTL_GAIN_MAX		0xffff
#define IMX708_DGTL_GAIN_DEFAULT	0x0100
#define IMX708_DGTL_GAIN_STEP		1

/* Colour balance controls */
#define IMX708_REG_COLOUR_BALANCE_RED   0x0b90
#define IMX708_REG_COLOUR_BALANCE_BLUE	0x0b92
#define IMX708_COLOUR_BALANCE_MIN	0x01
#define IMX708_COLOUR_BALANCE_MAX	0xffff
#define IMX708_COLOUR_BALANCE_STEP	0x01
#define IMX708_COLOUR_BALANCE_DEFAULT	0x100

/* Test Pattern Control */
#define IMX708_REG_TEST_PATTERN		0x0600
#define IMX708_TEST_PATTERN_DISABLE	0
#define IMX708_TEST_PATTERN_SOLID_COLOR	1
#define IMX708_TEST_PATTERN_COLOR_BARS	2
#define IMX708_TEST_PATTERN_GREY_COLOR	3
#define IMX708_TEST_PATTERN_PN9		4

/* Test pattern colour components */
#define IMX708_REG_TEST_PATTERN_R	0x0602
#define IMX708_REG_TEST_PATTERN_GR	0x0604
#define IMX708_REG_TEST_PATTERN_B	0x0606
#define IMX708_REG_TEST_PATTERN_GB	0x0608
#define IMX708_TEST_PATTERN_COLOUR_MIN	0
#define IMX708_TEST_PATTERN_COLOUR_MAX	0x0fff
#define IMX708_TEST_PATTERN_COLOUR_STEP	1

#define IMX708_REG_BASE_SPC_GAINS_L	0x7b10
#define IMX708_REG_BASE_SPC_GAINS_R	0x7c00

/* HDR exposure ratio (long:med == med:short) */
#define IMX708_HDR_EXPOSURE_RATIO       4
#define IMX708_REG_MID_EXPOSURE		0x3116
#define IMX708_REG_SHT_EXPOSURE		0x0224
#define IMX708_REG_MID_ANALOG_GAIN	0x3118
#define IMX708_REG_SHT_ANALOG_GAIN	0x0216

/* IMX708 native and active pixel array size. */
#define IMX708_NATIVE_WIDTH		4640U
#define IMX708_NATIVE_HEIGHT		2658U
#define IMX708_PIXEL_ARRAY_LEFT		16U
#define IMX708_PIXEL_ARRAY_TOP		24U
#define IMX708_PIXEL_ARRAY_WIDTH	4608U
#define IMX708_PIXEL_ARRAY_HEIGHT	2592U

struct imx708_reg {
	u16 address;
	u8 val;
};

struct imx708_reg_list {
	unsigned int num_of_regs;
	const struct imx708_reg *regs;
};

/* Mode : resolution and related config&values */
struct imx708_mode {
	/* Frame width */
	unsigned int width;

	/* Frame height */
	unsigned int height;

	/* H-timing in pixels */
	unsigned int line_length_pix;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	/* Highest possible framerate. */
	unsigned int vblank_min;

	/* Default framerate. */
	unsigned int vblank_default;

	/* Default register values */
	struct imx708_reg_list reg_list;

	/* Not all modes have the same pixel rate. */
	u64 pixel_rate;

	/* Not all modes have the same minimum exposure. */
	u32 exposure_lines_min;

	/* Not all modes have the same exposure lines step. */
	u32 exposure_lines_step;

	/* HDR flag, currently not used at runtime */
	bool hdr;
};

/* Default PDAF pixel correction gains */
static const u8 pdaf_gains[2][9] = {
	{ 0x4c, 0x4c, 0x4c, 0x46, 0x3e, 0x38, 0x35, 0x35, 0x35 },
	{ 0x35, 0x35, 0x35, 0x38, 0x3e, 0x46, 0x4c, 0x4c, 0x4c }
};

static const struct imx708_reg mode_common_regs[] = {
	{0x0100, 0x00},
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x33f0, 0x02},
	{0x33f1, 0x05},
	{0x3062, 0x00},
	{0x3063, 0x12},
	{0x3068, 0x00},
	{0x3069, 0x12},
	{0x306a, 0x00},
	{0x306b, 0x30},
	{0x3076, 0x00},
	{0x3077, 0x30},
	{0x3078, 0x00},
	{0x3079, 0x30},
	{0x5e54, 0x0c},
	{0x6e44, 0x00},
	{0xb0b6, 0x01},
	{0xe829, 0x00},
	{0xf001, 0x08},
	{0xf003, 0x08},
	{0xf00d, 0x10},
	{0xf00f, 0x10},
	{0xf031, 0x08},
	{0xf033, 0x08},
	{0xf03d, 0x10},
	{0xf03f, 0x10},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x01},
	{0x0b8e, 0x01},
	{0x0b8f, 0x00},
	{0x0b94, 0x01},
	{0x0b95, 0x00},
	{0x3400, 0x01},
	{0x3478, 0x01},
	{0x3479, 0x1c},
	{0x3091, 0x01},
	{0x3092, 0x00},
	{0x3419, 0x00},
	{0xbcf1, 0x02},
	{0x3094, 0x01},
	{0x3095, 0x01},
	{0x3362, 0x00},
	{0x3363, 0x00},
	{0x3364, 0x00},
	{0x3365, 0x00},
	{0x0138, 0x01},
};

/* 10-bit. */
static const struct imx708_reg mode_4608x2592_regs[] = {
	{0x0342, 0x3d},
	{0x0343, 0x20},
	{0x0340, 0x0a},
	{0x0341, 0x59},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x11},
	{0x0349, 0xff},
	{0x034a, 0x0a},
	{0x034b, 0x1f},
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x0a},
	{0x3200, 0x01},
	{0x3201, 0x01},
	{0x32d5, 0x01},
	{0x32d6, 0x00},
	{0x32db, 0x01},
	{0x32df, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x12},
	{0x040d, 0x00},
	{0x040e, 0x0a},
	{0x040f, 0x20},
	{0x034c, 0x12},
	{0x034d, 0x00},
	{0x034e, 0x0a},
	{0x034f, 0x20},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0x7c},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x2c},
	{0x0310, 0x01},
	{0x3ca0, 0x00},
	{0x3ca1, 0x64},
	{0x3ca4, 0x00},
	{0x3ca5, 0x00},
	{0x3ca6, 0x00},
	{0x3ca7, 0x00},
	{0x3caa, 0x00},
	{0x3cab, 0x00},
	{0x3cb8, 0x00},
	{0x3cb9, 0x08},
	{0x3cba, 0x00},
	{0x3cbb, 0x00},
	{0x3cbc, 0x00},
	{0x3cbd, 0x3c},
	{0x3cbe, 0x00},
	{0x3cbf, 0x00},
	{0x0202, 0x0a},
	{0x0203, 0x29},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{0x3116, 0x01},
	{0x3117, 0xf4},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x3118, 0x00},
	{0x3119, 0x00},
	{0x311a, 0x01},
	{0x311b, 0x00},
	{0x341a, 0x00},
	{0x341b, 0x00},
	{0x341c, 0x00},
	{0x341d, 0x00},
	{0x341e, 0x01},
	{0x341f, 0x20},
	{0x3420, 0x00},
	{0x3421, 0xd8},
	{0xc428, 0x00},
	{0xc429, 0x04},
	{0x3366, 0x00},
	{0x3367, 0x00},
	{0x3368, 0x00},
	{0x3369, 0x00},
};

static const struct imx708_reg mode_2x2binned_regs[] = {
	{0x0342, 0x1e},
	{0x0343, 0x90},
	{0x0340, 0x05},
	{0x0341, 0x38},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x11},
	{0x0349, 0xff},
	{0x034a, 0x0a},
	{0x034b, 0x1f},
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x08},
	{0x3200, 0x41},
	{0x3201, 0x41},
	{0x32d5, 0x00},
	{0x32d6, 0x00},
	{0x32db, 0x01},
	{0x32df, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x09},
	{0x040d, 0x00},
	{0x040e, 0x05},
	{0x040f, 0x10},
	{0x034c, 0x09},
	{0x034d, 0x00},
	{0x034e, 0x05},
	{0x034f, 0x10},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0x7a},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x2c},
	{0x0310, 0x01},
	{0x3ca0, 0x00},
	{0x3ca1, 0x3c},
	{0x3ca4, 0x00},
	{0x3ca5, 0x3c},
	{0x3ca6, 0x00},
	{0x3ca7, 0x00},
	{0x3caa, 0x00},
	{0x3cab, 0x00},
	{0x3cb8, 0x00},
	{0x3cb9, 0x1c},
	{0x3cba, 0x00},
	{0x3cbb, 0x08},
	{0x3cbc, 0x00},
	{0x3cbd, 0x1e},
	{0x3cbe, 0x00},
	{0x3cbf, 0x0a},
	{0x0202, 0x05},
	{0x0203, 0x08},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{0x3116, 0x01},
	{0x3117, 0xf4},
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x3118, 0x00},
	{0x3119, 0x70},
	{0x311a, 0x01},
	{0x311b, 0x00},
	{0x341a, 0x00},
	{0x341b, 0x00},
	{0x341c, 0x00},
	{0x341d, 0x00},
	{0x341e, 0x00},
	{0x341f, 0x90},
	{0x3420, 0x00},
	{0x3421, 0x6c},
	{0x3366, 0x00},
	{0x3367, 0x00},
	{0x3368, 0x00},
	{0x3369, 0x00},
};

static const struct imx708_reg mode_2x2binned_720p_regs[] = {
	{0x0342, 0x14},
	{0x0343, 0x60},
	{0x0340, 0x04},
	{0x0341, 0xb6},
	{0x0344, 0x03},
	{0x0345, 0x00},
	{0x0346, 0x01},
	{0x0347, 0xb0},
	{0x0348, 0x0e},
	{0x0349, 0xff},
	{0x034a, 0x08},
	{0x034b, 0x6f},
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x08},
	{0x3200, 0x41},
	{0x3201, 0x41},
	{0x32d5, 0x00},
	{0x32d6, 0x00},
	{0x32db, 0x01},
	{0x32df, 0x01},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x06},
	{0x040d, 0x00},
	{0x040e, 0x03},
	{0x040f, 0x60},
	{0x034c, 0x06},
	{0x034d, 0x00},
	{0x034e, 0x03},
	{0x034f, 0x60},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0x76},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x2c},
	{0x0310, 0x01},
	{0x3ca0, 0x00},
	{0x3ca1, 0x3c},
	{0x3ca4, 0x01},
	{0x3ca5, 0x5e},
	{0x3ca6, 0x00},
	{0x3ca7, 0x00},
	{0x3caa, 0x00},
	{0x3cab, 0x00},
	{0x3cb8, 0x00},
	{0x3cb9, 0x0c},
	{0x3cba, 0x00},
	{0x3cbb, 0x04},
	{0x3cbc, 0x00},
	{0x3cbd, 0x1e},
	{0x3cbe, 0x00},
	{0x3cbf, 0x05},
	{0x0202, 0x04},
	{0x0203, 0x86},
	{0x0224, 0x01},
	{0x0225, 0xf4},
	{0x3116, 0x01},
	{0x3117, 0xf4},
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x3118, 0x00},
	{0x3119, 0x70},
	{0x311a, 0x01},
	{0x311b, 0x00},
	{0x341a, 0x00},
	{0x341b, 0x00},
	{0x341c, 0x00},
	{0x341d, 0x00},
	{0x341e, 0x00},
	{0x341f, 0x60},
	{0x3420, 0x00},
	{0x3421, 0x48},
	{0x3366, 0x00},
	{0x3367, 0x00},
	{0x3368, 0x00},
	{0x3369, 0x00},
};

static const struct imx708_reg mode_hdr_regs[] = {
	{0x0342, 0x14},
	{0x0343, 0x60},
	{0x0340, 0x0a},
	{0x0341, 0x5b},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x11},
	{0x0349, 0xff},
	{0x034a, 0x0a},
	{0x034b, 0x1f},
	{0x0220, 0x01},
	{0x0222, IMX708_HDR_EXPOSURE_RATIO},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x0a},
	{0x3200, 0x01},
	{0x3201, 0x01},
	{0x32d5, 0x00},
	{0x32d6, 0x00},
	{0x32db, 0x01},
	{0x32df, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x09},
	{0x040d, 0x00},
	{0x040e, 0x05},
	{0x040f, 0x10},
	{0x034c, 0x09},
	{0x034d, 0x00},
	{0x034e, 0x05},
	{0x034f, 0x10},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0xa2},
	{0x030b, 0x02},
	{0x030d, 0x04},
	{0x030e, 0x01},
	{0x030f, 0x2c},
	{0x0310, 0x01},
	{0x3ca0, 0x00},
	{0x3ca1, 0x00},
	{0x3ca4, 0x00},
	{0x3ca5, 0x00},
	{0x3ca6, 0x00},
	{0x3ca7, 0x28},
	{0x3caa, 0x00},
	{0x3cab, 0x00},
	{0x3cb8, 0x00},
	{0x3cb9, 0x30},
	{0x3cba, 0x00},
	{0x3cbb, 0x00},
	{0x3cbc, 0x00},
	{0x3cbd, 0x32},
	{0x3cbe, 0x00},
	{0x3cbf, 0x00},
	{0x0202, 0x0a},
	{0x0203, 0x2b},
	{0x0224, 0x0a},
	{0x0225, 0x2b},
	{0x3116, 0x0a},
	{0x3117, 0x2b},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x3118, 0x00},
	{0x3119, 0x00},
	{0x311a, 0x01},
	{0x311b, 0x00},
	{0x341a, 0x00},
	{0x341b, 0x00},
	{0x341c, 0x00},
	{0x341d, 0x00},
	{0x341e, 0x00},
	{0x341f, 0x90},
	{0x3420, 0x00},
	{0x3421, 0x6c},
	{0x3360, 0x01},
	{0x3361, 0x01},
	{0x3366, 0x09},
	{0x3367, 0x00},
	{0x3368, 0x05},
	{0x3369, 0x10},
};

/* Mode configs. Keep separate lists for when HDR is enabled or not. */
static const struct imx708_mode supported_modes_10bit_no_hdr[] = {
	{
		/* Full resolution. */
		.width = 4608,
		.height = 2592,
		.line_length_pix = 0x3d20,
		.crop = {
			.left = IMX708_PIXEL_ARRAY_LEFT,
			.top = IMX708_PIXEL_ARRAY_TOP,
			.width = 4608,
			.height = 2592,
		},
		.vblank_min = 58,
		.vblank_default = 58,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4608x2592_regs),
			.regs = mode_4608x2592_regs,
		},
		.pixel_rate = 595200000,
		.exposure_lines_min = 8,
		.exposure_lines_step = 1,
		.hdr = false
	},
	{
		/* regular 2x2 binned. */
		.width = 2304,
		.height = 1296,
		.line_length_pix = 0x1e90,
		.crop = {
			.left = IMX708_PIXEL_ARRAY_LEFT,
			.top = IMX708_PIXEL_ARRAY_TOP,
			.width = 4608,
			.height = 2592,
		},
		.vblank_min = 40,
		.vblank_default = 1198,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2x2binned_regs),
			.regs = mode_2x2binned_regs,
		},
		.pixel_rate = 585600000,
		.exposure_lines_min = 4,
		.exposure_lines_step = 2,
		.hdr = false
	},
	{
		/* 2x2 binned and cropped for 720p. */
		.width = 1536,
		.height = 864,
		.line_length_pix = 0x1460,
		.crop = {
			.left = IMX708_PIXEL_ARRAY_LEFT + 768,
			.top = IMX708_PIXEL_ARRAY_TOP + 432,
			.width = 3072,
			.height = 1728,
		},
		.vblank_min = 40,
		.vblank_default = 2755,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2x2binned_720p_regs),
			.regs = mode_2x2binned_720p_regs,
		},
		.pixel_rate = 566400000,
		.exposure_lines_min = 4,
		.exposure_lines_step = 2,
		.hdr = false
	},
};

static const struct imx708_mode supported_modes_10bit_hdr[] = {
	{
		/* There's only one HDR mode, which is 2x2 downscaled */
		.width = 2304,
		.height = 1296,
		.line_length_pix = 0x1460,
		.crop = {
			.left = IMX708_PIXEL_ARRAY_LEFT,
			.top = IMX708_PIXEL_ARRAY_TOP,
			.width = 4608,
			.height = 2592,
		},
		.vblank_min = 3673,
		.vblank_default = 3673,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_hdr_regs),
			.regs = mode_hdr_regs,
		},
		.pixel_rate = 777600000,
		.exposure_lines_min = 8 * IMX708_HDR_EXPOSURE_RATIO *
				      IMX708_HDR_EXPOSURE_RATIO,
		.exposure_lines_step = 2 * IMX708_HDR_EXPOSURE_RATIO *
				       IMX708_HDR_EXPOSURE_RATIO,
		.hdr = true
	}
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	/* 10-bit modes. */
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

static const char * const imx708_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int imx708_test_pattern_val[] = {
	IMX708_TEST_PATTERN_DISABLE,
	IMX708_TEST_PATTERN_COLOR_BARS,
	IMX708_TEST_PATTERN_SOLID_COLOR,
	IMX708_TEST_PATTERN_GREY_COLOR,
	IMX708_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char * const imx708_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA1",  /* Analog1 (2.8V) supply */
	"VANA2",  /* Analog2 (1.8V) supply */
	"VDIG",  /* Digital Core (1.1V) supply */
	"VDDL",  /* IF (1.8V) supply */
};

#define IMX708_NUM_SUPPLIES ARRAY_SIZE(imx708_supply_name)

/*
 * Initialisation delay between XCLR low->high and the moment when the sensor
 * can start capture (i.e. can leave software standby), given by T7 in the
 * datasheet is 8ms.  This does include I2C setup time as well.
 *
 * Note, that delay between XCLR low->high and reading the CCI ID register (T6
 * in the datasheet) is much smaller - 600us.
 */
#define IMX708_XCLR_MIN_DELAY_US	8000
#define IMX708_XCLR_DELAY_RANGE_US	1000

struct imx708 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_mbus_framefmt fmt;

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX708_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *red_balance;
	struct v4l2_ctrl *blue_balance;
	struct v4l2_ctrl *hdr_mode;

	/* Current mode */
	const struct imx708_mode *mode;

	/* Mutex for serialized access */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;

	/* Rewrite common registers on stream on? */
	bool common_regs_written;

	/* Current long exposure factor in use. Set through V4L2_CID_VBLANK */
	unsigned int long_exp_shift;
};

static inline struct imx708 *to_imx708(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx708, sd);
}

static inline void get_mode_table(const struct imx708_mode **mode_list,
				  unsigned int *num_modes,
				  bool hdr_enable)
{
	if (hdr_enable) {
		*mode_list = supported_modes_10bit_hdr;
		*num_modes = ARRAY_SIZE(supported_modes_10bit_hdr);
	} else {
		*mode_list = supported_modes_10bit_no_hdr;
		*num_modes = ARRAY_SIZE(supported_modes_10bit_no_hdr);
	}
}

/* Read registers up to 2 at a time */
static int imx708_read_reg(struct imx708 *imx708, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

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

/* Write registers up to 2 at a time */
static int imx708_write_reg(struct imx708 *imx708, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
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
static int imx708_write_regs(struct imx708 *imx708,
			     const struct imx708_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx708_write_reg(imx708, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

/* Get bayer order based on flip setting. */
static u32 imx708_get_format_code(struct imx708 *imx708)
{
	unsigned int i;

	lockdep_assert_held(&imx708->mutex);

	i = (imx708->vflip->val ? 2 : 0) |
	    (imx708->hflip->val ? 1 : 0);

	return codes[i];
}

static void imx708_set_default_format(struct imx708 *imx708)
{
	struct v4l2_mbus_framefmt *fmt = &imx708->fmt;

	/* Set default mode to max resolution */
	imx708->mode = &supported_modes_10bit_no_hdr[0];

	/* fmt->code not set as it will always be computed based on flips */
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = imx708->mode->width;
	fmt->height = imx708->mode->height;
	fmt->field = V4L2_FIELD_NONE;
}

static int imx708_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx708 *imx708 = to_imx708(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_get_try_format(sd, fh->state, 0);
	struct v4l2_rect *try_crop;

	mutex_lock(&imx708->mutex);

	/* Initialize try_fmt for the image pad */
	if (imx708->hdr_mode->val) {
		try_fmt_img->width = supported_modes_10bit_hdr[0].width;
		try_fmt_img->height = supported_modes_10bit_hdr[0].height;
	} else {
		try_fmt_img->width = supported_modes_10bit_no_hdr[0].width;
		try_fmt_img->height = supported_modes_10bit_no_hdr[0].height;
	}
	try_fmt_img->code = imx708_get_format_code(imx708);
	try_fmt_img->field = V4L2_FIELD_NONE;

	/* Initialize try_crop */
	try_crop = v4l2_subdev_get_try_crop(sd, fh->state, 0);
	try_crop->left = IMX708_PIXEL_ARRAY_LEFT;
	try_crop->top = IMX708_PIXEL_ARRAY_TOP;
	try_crop->width = IMX708_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX708_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx708->mutex);

	return 0;
}

static int imx708_set_exposure(struct imx708 *imx708, unsigned int val)
{
	int ret;

	val = max(val, imx708->mode->exposure_lines_min);
	val -= val % imx708->mode->exposure_lines_step;

	/*
	 * In HDR mode this will set the longest exposure. The sensor
	 * will automatically divide the medium and short ones by 4,16.
	 */
	ret = imx708_write_reg(imx708, IMX708_REG_EXPOSURE,
			       IMX708_REG_VALUE_16BIT,
			       val >> imx708->long_exp_shift);

	return ret;
}

static void imx708_adjust_exposure_range(struct imx708 *imx708,
					 struct v4l2_ctrl *ctrl)
{
	int exposure_max, exposure_def;

	/* Honour the VBLANK limits when setting exposure. */
	exposure_max = imx708->mode->height + imx708->vblank->val -
		IMX708_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx708->exposure->val);
	__v4l2_ctrl_modify_range(imx708->exposure, imx708->exposure->minimum,
				 exposure_max, imx708->exposure->step,
				 exposure_def);
}

static int imx708_set_analogue_gain(struct imx708 *imx708, unsigned int val)
{
	int ret;

	/*
	 * In HDR mode this will set the gain for the longest exposure,
	 * and by default the sensor uses the same gain for all of them.
	 */
	ret = imx708_write_reg(imx708, IMX708_REG_ANALOG_GAIN,
			       IMX708_REG_VALUE_16BIT, val);

	return ret;
}

static int imx708_set_frame_length(struct imx708 *imx708, unsigned int val)
{
	int ret = 0;

	imx708->long_exp_shift = 0;

	while (val > IMX708_FRAME_LENGTH_MAX) {
		imx708->long_exp_shift++;
		val >>= 1;
	}

	ret = imx708_write_reg(imx708, IMX708_REG_FRAME_LENGTH,
			       IMX708_REG_VALUE_16BIT, val);
	if (ret)
		return ret;

	return imx708_write_reg(imx708, IMX708_LONG_EXP_SHIFT_REG,
				IMX708_REG_VALUE_08BIT, imx708->long_exp_shift);
}

static void imx708_set_framing_limits(struct imx708 *imx708)
{
	unsigned int hblank;
	const struct imx708_mode *mode = imx708->mode;

	/* Default to no long exposure multiplier */
	imx708->long_exp_shift = 0;

	__v4l2_ctrl_modify_range(imx708->pixel_rate,
				 mode->pixel_rate, mode->pixel_rate,
				 1, mode->pixel_rate);

	/* Update limits and set FPS to default */
	__v4l2_ctrl_modify_range(imx708->vblank, mode->vblank_min,
				 ((1 << IMX708_LONG_EXP_SHIFT_MAX) *
					IMX708_FRAME_LENGTH_MAX) - mode->height,
				 1, mode->vblank_default);

	/*
	 * Currently PPL is fixed to the mode specified value, so hblank
	 * depends on mode->width only, and is not changeable in any
	 * way other than changing the mode.
	 */
	hblank = mode->line_length_pix - mode->width;
	__v4l2_ctrl_modify_range(imx708->hblank, hblank, hblank, 1, hblank);
}

static int imx708_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx708 *imx708 =
		container_of(ctrl->handler, struct imx708, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	const struct imx708_mode *mode_list;
	unsigned int num_modes;
	int ret;

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK)
		imx708_adjust_exposure_range(imx708, ctrl);

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx708_set_analogue_gain(imx708, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = imx708_set_exposure(imx708, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx708_write_reg(imx708, IMX708_REG_DIGITAL_GAIN,
				       IMX708_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx708_write_reg(imx708, IMX708_REG_TEST_PATTERN,
				       IMX708_REG_VALUE_16BIT,
				       imx708_test_pattern_val[ctrl->val]);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		ret = imx708_write_reg(imx708, IMX708_REG_TEST_PATTERN_R,
				       IMX708_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		ret = imx708_write_reg(imx708, IMX708_REG_TEST_PATTERN_GR,
				       IMX708_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		ret = imx708_write_reg(imx708, IMX708_REG_TEST_PATTERN_B,
				       IMX708_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		ret = imx708_write_reg(imx708, IMX708_REG_TEST_PATTERN_GB,
				       IMX708_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = imx708_write_reg(imx708, IMX708_REG_ORIENTATION, 1,
				       imx708->hflip->val |
				       imx708->vflip->val << 1);
		break;
	case V4L2_CID_VBLANK:
		ret = imx708_set_frame_length(imx708,
					      imx708->mode->height + ctrl->val);
		break;
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		get_mode_table(&mode_list, &num_modes, ctrl->val);
		imx708->mode = v4l2_find_nearest_size(mode_list,
						      num_modes,
						      width, height,
						      imx708->mode->width,
						      imx708->mode->height);
		imx708_set_framing_limits(imx708);
		ret = 0;
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx708_ctrl_ops = {
	.s_ctrl = imx708_set_ctrl,
};

static int imx708_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx708 *imx708 = to_imx708(sd);

	if (code->index >= 1)
		return -EINVAL;

	code->code = imx708_get_format_code(imx708);

	return 0;
}

static int imx708_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx708 *imx708 = to_imx708(sd);
	const struct imx708_mode *mode_list;
	unsigned int num_modes;

	get_mode_table(&mode_list, &num_modes, imx708->hdr_mode->val);

	if (fse->index >= num_modes)
		return -EINVAL;

	if (fse->code != imx708_get_format_code(imx708))
		return -EINVAL;

	fse->min_width = mode_list[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = mode_list[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx708_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx708_update_image_pad_format(struct imx708 *imx708,
					   const struct imx708_mode *mode,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx708_reset_colorspace(&fmt->format);
}

static int imx708_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx708 *imx708 = to_imx708(sd);

	mutex_lock(&imx708->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(&imx708->sd, sd_state,
						   fmt->pad);
		/* update the code which could change due to vflip or hflip */
		try_fmt->code = imx708_get_format_code(imx708);
		fmt->format = *try_fmt;
	} else {
		imx708_update_image_pad_format(imx708, imx708->mode, fmt);
		fmt->format.code = imx708_get_format_code(imx708);
	}

	mutex_unlock(&imx708->mutex);
	return 0;
}

static int imx708_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx708_mode *mode;
	struct imx708 *imx708 = to_imx708(sd);
	const struct imx708_mode *mode_list;
	unsigned int num_modes;

	mutex_lock(&imx708->mutex);

	/* Bayer order varies with flips */
	fmt->format.code = imx708_get_format_code(imx708);

	get_mode_table(&mode_list, &num_modes, imx708->hdr_mode->val);

	mode = v4l2_find_nearest_size(mode_list, num_modes, width, height,
				      fmt->format.width, fmt->format.height);
	imx708_update_image_pad_format(imx708, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		imx708->mode = mode;
		imx708_set_framing_limits(imx708);
	}

	mutex_unlock(&imx708->mutex);

	return 0;
}

static const struct v4l2_rect *
__imx708_get_pad_crop(struct imx708 *imx708, struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&imx708->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx708->mode->crop;
	}

	return NULL;
}

static int imx708_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct imx708 *imx708 = to_imx708(sd);

		mutex_lock(&imx708->mutex);
		sel->r = *__imx708_get_pad_crop(imx708, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&imx708->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX708_NATIVE_WIDTH;
		sel->r.height = IMX708_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = IMX708_PIXEL_ARRAY_LEFT;
		sel->r.top = IMX708_PIXEL_ARRAY_TOP;
		sel->r.width = IMX708_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX708_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

/* Start streaming */
static int imx708_start_streaming(struct imx708 *imx708)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	const struct imx708_reg_list *reg_list;
	int i, ret;
	u32 val;

	if (!imx708->common_regs_written) {
		ret = imx708_write_regs(imx708, mode_common_regs,
					ARRAY_SIZE(mode_common_regs));
		if (ret) {
			dev_err(&client->dev, "%s failed to set common settings\n",
				__func__);
			return ret;
		}

		ret = imx708_read_reg(imx708, IMX708_REG_BASE_SPC_GAINS_L,
				      IMX708_REG_VALUE_08BIT, &val);
		if (ret == 0 && val == 0x40) {
			for (i = 0; i < 54 && ret == 0; i++) {
				u16 reg = IMX708_REG_BASE_SPC_GAINS_L + i;

				ret = imx708_write_reg(imx708, reg,
						       IMX708_REG_VALUE_08BIT,
						       pdaf_gains[0][i % 9]);
			}
			for (i = 0; i < 54 && ret == 0; i++) {
				u16 reg = IMX708_REG_BASE_SPC_GAINS_R + i;

				ret = imx708_write_reg(imx708, reg,
						       IMX708_REG_VALUE_08BIT,
						       pdaf_gains[1][i % 9]);
			}
		}
		if (ret) {
			dev_err(&client->dev, "%s failed to set PDAF gains\n",
				__func__);
			return ret;
		}

		imx708->common_regs_written = true;
	}

	/* Apply default values of current mode */
	reg_list = &imx708->mode->reg_list;
	ret = imx708_write_regs(imx708, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx708->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return imx708_write_reg(imx708, IMX708_REG_MODE_SELECT,
				IMX708_REG_VALUE_08BIT, IMX708_MODE_STREAMING);
}

/* Stop streaming */
static void imx708_stop_streaming(struct imx708 *imx708)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	int ret;

	/* set stream off register */
	ret = imx708_write_reg(imx708, IMX708_REG_MODE_SELECT,
			       IMX708_REG_VALUE_08BIT, IMX708_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx708_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx708 *imx708 = to_imx708(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx708->mutex);
	if (imx708->streaming == enable) {
		mutex_unlock(&imx708->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx708_start_streaming(imx708);
		if (ret)
			goto err_rpm_put;
	} else {
		imx708_stop_streaming(imx708);
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

	imx708->streaming = enable;

	/* vflip/hflip and hdr mode cannot change during streaming */
	__v4l2_ctrl_grab(imx708->vflip, enable);
	__v4l2_ctrl_grab(imx708->hflip, enable);
	__v4l2_ctrl_grab(imx708->hdr_mode, enable);

	mutex_unlock(&imx708->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put_sync(&client->dev);
err_unlock:
	mutex_unlock(&imx708->mutex);

	return ret;
}

/* Power/clock management functions */
static int imx708_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx708 *imx708 = to_imx708(sd);
	int ret;

	ret = regulator_bulk_enable(IMX708_NUM_SUPPLIES,
				    imx708->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx708->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx708->reset_gpio, 1);
	usleep_range(IMX708_XCLR_MIN_DELAY_US,
		     IMX708_XCLR_MIN_DELAY_US + IMX708_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX708_NUM_SUPPLIES, imx708->supplies);
	return ret;
}

static int imx708_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx708 *imx708 = to_imx708(sd);

	gpiod_set_value_cansleep(imx708->reset_gpio, 0);
	clk_disable_unprepare(imx708->xclk);
	regulator_bulk_disable(IMX708_NUM_SUPPLIES, imx708->supplies);

	/* Force reprogramming of the common registers when powered up again. */
	imx708->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx708_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx708 *imx708 = to_imx708(sd);

	if (imx708->streaming)
		imx708_stop_streaming(imx708);

	return 0;
}

static int __maybe_unused imx708_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx708 *imx708 = to_imx708(sd);
	int ret;

	if (imx708->streaming) {
		ret = imx708_start_streaming(imx708);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx708_stop_streaming(imx708);
	imx708->streaming = 0;
	return ret;
}

static int imx708_get_regulators(struct imx708 *imx708)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	unsigned int i;

	for (i = 0; i < IMX708_NUM_SUPPLIES; i++)
		imx708->supplies[i].supply = imx708_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX708_NUM_SUPPLIES,
				       imx708->supplies);
}

/* Verify chip ID */
static int imx708_identify_module(struct imx708 *imx708)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	int ret;
	u32 val;

	ret = imx708_read_reg(imx708, IMX708_REG_CHIP_ID,
			      IMX708_REG_VALUE_16BIT, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x, with error %d\n",
			IMX708_CHIP_ID, ret);
		return ret;
	}

	if (val != IMX708_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			IMX708_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops imx708_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx708_video_ops = {
	.s_stream = imx708_set_stream,
};

static const struct v4l2_subdev_pad_ops imx708_pad_ops = {
	.enum_mbus_code = imx708_enum_mbus_code,
	.get_fmt = imx708_get_pad_format,
	.set_fmt = imx708_set_pad_format,
	.get_selection = imx708_get_selection,
	.enum_frame_size = imx708_enum_frame_size,
};

static const struct v4l2_subdev_ops imx708_subdev_ops = {
	.core = &imx708_core_ops,
	.video = &imx708_video_ops,
	.pad = &imx708_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx708_internal_ops = {
	.open = imx708_open,
};

/* Initialize control handlers */
static int imx708_init_controls(struct imx708 *imx708)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx708->sd);
	struct v4l2_fwnode_device_properties props;
	unsigned int i;
	int ret;

	ctrl_hdlr = &imx708->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	mutex_init(&imx708->mutex);
	ctrl_hdlr->lock = &imx708->mutex;

	/* By default, PIXEL_RATE is read only */
	imx708->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       imx708->mode->pixel_rate,
					       imx708->mode->pixel_rate, 1,
					       imx708->mode->pixel_rate);

	/*
	 * Create the controls here, but mode specific limits are setup
	 * in the imx708_set_framing_limits() call below.
	 */
	imx708->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					   V4L2_CID_VBLANK, 0, 0xffff, 1, 0);
	imx708->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx708->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX708_EXPOSURE_MIN,
					     IMX708_EXPOSURE_MAX,
					     IMX708_EXPOSURE_STEP,
					     IMX708_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX708_ANA_GAIN_MIN, IMX708_ANA_GAIN_MAX,
			  IMX708_ANA_GAIN_STEP, IMX708_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX708_DGTL_GAIN_MIN, IMX708_DGTL_GAIN_MAX,
			  IMX708_DGTL_GAIN_STEP, IMX708_DGTL_GAIN_DEFAULT);

	imx708->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);

	imx708->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx708_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx708_test_pattern_menu) - 1,
				     0, 0, imx708_test_pattern_menu);
	for (i = 0; i < 4; i++) {
		/*
		 * The assumption is that
		 * V4L2_CID_TEST_PATTERN_GREENR == V4L2_CID_TEST_PATTERN_RED + 1
		 * V4L2_CID_TEST_PATTERN_BLUE   == V4L2_CID_TEST_PATTERN_RED + 2
		 * V4L2_CID_TEST_PATTERN_GREENB == V4L2_CID_TEST_PATTERN_RED + 3
		 */
		v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED + i,
				  IMX708_TEST_PATTERN_COLOUR_MIN,
				  IMX708_TEST_PATTERN_COLOUR_MAX,
				  IMX708_TEST_PATTERN_COLOUR_STEP,
				  IMX708_TEST_PATTERN_COLOUR_MAX);
		/* The "Solid color" pattern is white by default */
	}

	imx708->hdr_mode = v4l2_ctrl_new_std(ctrl_hdlr, &imx708_ctrl_ops,
					     V4L2_CID_WIDE_DYNAMIC_RANGE,
					     0, 1, 1, 0);

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx708_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	imx708->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	imx708->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	imx708->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	imx708->hdr_mode->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx708->sd.ctrl_handler = ctrl_hdlr;

	/* Setup exposure and frame/line length limits. */
	imx708_set_framing_limits(imx708);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx708->mutex);

	return ret;
}

static void imx708_free_controls(struct imx708 *imx708)
{
	v4l2_ctrl_handler_free(imx708->sd.ctrl_handler);
	mutex_destroy(&imx708->mutex);
}

static int imx708_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	/* Check the number of MIPI CSI2 data lanes */
	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "only 2 data lanes are currently supported\n");
		goto error_out;
	}

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	if (ep_cfg.nr_of_link_frequencies != 1 ||
	    ep_cfg.link_frequencies[0] != IMX708_DEFAULT_LINK_FREQ) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int imx708_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx708 *imx708;
	int ret;

	imx708 = devm_kzalloc(&client->dev, sizeof(*imx708), GFP_KERNEL);
	if (!imx708)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx708->sd, client, &imx708_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (imx708_check_hwcfg(dev))
		return -EINVAL;

	/* Get system clock (xclk) */
	imx708->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx708->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx708->xclk);
	}

	imx708->xclk_freq = clk_get_rate(imx708->xclk);
	if (imx708->xclk_freq != IMX708_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx708->xclk_freq);
		return -EINVAL;
	}

	ret = imx708_get_regulators(imx708);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	/* Request optional enable pin */
	imx708->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	/*
	 * The sensor must be powered for imx708_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = imx708_power_on(dev);
	if (ret)
		return ret;

	ret = imx708_identify_module(imx708);
	if (ret)
		goto error_power_off;

	/* Initialize default format */
	imx708_set_default_format(imx708);

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	/* This needs the pm runtime to be registered. */
	ret = imx708_init_controls(imx708);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	imx708->sd.internal_ops = &imx708_internal_ops;
	imx708->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx708->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx708->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx708->sd.entity, 1, &imx708->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx708->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	/*
	 * Decrease the PM usage count. The device will get suspended after the
	 * autosuspend delay, turning the power off.
	 */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx708->sd.entity);

error_handler_free:
	imx708_free_controls(imx708);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	imx708_power_off(&client->dev);

	return ret;
}

static int imx708_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx708 *imx708 = to_imx708(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx708_free_controls(imx708);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx708_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id imx708_dt_ids[] = {
	{ .compatible = "sony,imx708" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx708_dt_ids);

static const struct dev_pm_ops imx708_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx708_suspend, imx708_resume)
	SET_RUNTIME_PM_OPS(imx708_power_off, imx708_power_on, NULL)
};

static struct i2c_driver imx708_i2c_driver = {
	.driver = {
		.name = "imx708",
		.of_match_table	= imx708_dt_ids,
		.pm = &imx708_pm_ops,
	},
	.probe_new = imx708_probe,
	.remove = imx708_remove,
};

module_i2c_driver(imx708_i2c_driver);

MODULE_AUTHOR("David Plowman <david.plowman@raspberrypi.com>");
MODULE_DESCRIPTION("Sony IMX708 sensor driver");
MODULE_LICENSE("GPL");
