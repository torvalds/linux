// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Intel Corporation.

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define HI556_REG_VALUE_08BIT		1
#define HI556_REG_VALUE_16BIT		2
#define HI556_REG_VALUE_24BIT		3

#define HI556_LINK_FREQ_437MHZ		437000000ULL
#define HI556_MCLK			19200000
#define HI556_DATA_LANES		2
#define HI556_RGB_DEPTH			10

#define HI556_REG_CHIP_ID		0x0f16
#define HI556_CHIP_ID			0x0556

#define HI556_REG_MODE_SELECT		0x0a00
#define HI556_MODE_STANDBY		0x0000
#define HI556_MODE_STREAMING		0x0100

/* vertical-timings from sensor */
#define HI556_REG_FLL			0x0006
#define HI556_FLL_30FPS			0x0814
#define HI556_FLL_30FPS_MIN		0x0814
#define HI556_FLL_MAX			0x7fff

/* horizontal-timings from sensor */
#define HI556_REG_LLP			0x0008

/* Exposure controls from sensor */
#define HI556_REG_EXPOSURE		0x0074
#define HI556_EXPOSURE_MIN		6
#define HI556_EXPOSURE_MAX_MARGIN	2
#define HI556_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define HI556_REG_ANALOG_GAIN		0x0077
#define HI556_ANAL_GAIN_MIN		0
#define HI556_ANAL_GAIN_MAX		240
#define HI556_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define HI556_REG_MWB_GR_GAIN		0x0078
#define HI556_REG_MWB_GB_GAIN		0x007a
#define HI556_REG_MWB_R_GAIN		0x007c
#define HI556_REG_MWB_B_GAIN		0x007e
#define HI556_DGTL_GAIN_MIN		0
#define HI556_DGTL_GAIN_MAX		2048
#define HI556_DGTL_GAIN_STEP		1
#define HI556_DGTL_GAIN_DEFAULT		256

/* Test Pattern Control */
#define HI556_REG_ISP			0X0a05
#define HI556_REG_ISP_TPG_EN		0x01
#define HI556_REG_TEST_PATTERN		0x0201

/* HI556 native and active pixel array size. */
#define HI556_NATIVE_WIDTH		2592U
#define HI556_NATIVE_HEIGHT		1944U
#define HI556_PIXEL_ARRAY_LEFT		0U
#define HI556_PIXEL_ARRAY_TOP		0U
#define HI556_PIXEL_ARRAY_WIDTH	2592U
#define HI556_PIXEL_ARRAY_HEIGHT	1944U

enum {
	HI556_LINK_FREQ_437MHZ_INDEX,
};

struct hi556_reg {
	u16 address;
	u16 val;
};

struct hi556_reg_list {
	u32 num_of_regs;
	const struct hi556_reg *regs;
};

struct hi556_link_freq_config {
	const struct hi556_reg_list reg_list;
};

struct hi556_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	/* Horizontal timining size */
	u32 llp;

	/* Default vertical timining size */
	u32 fll_def;

	/* Min vertical timining size */
	u32 fll_min;

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct hi556_reg_list reg_list;
};

#define to_hi556(_sd) container_of(_sd, struct hi556, sd)

//SENSOR_INITIALIZATION
static const struct hi556_reg mipi_data_rate_874mbps[] = {
	{0x0e00, 0x0102},
	{0x0e02, 0x0102},
	{0x0e0c, 0x0100},
	{0x2000, 0x7400},
	{0x2002, 0x001c},
	{0x2004, 0x0242},
	{0x2006, 0x0942},
	{0x2008, 0x7007},
	{0x200a, 0x0fd9},
	{0x200c, 0x0259},
	{0x200e, 0x7008},
	{0x2010, 0x160e},
	{0x2012, 0x0047},
	{0x2014, 0x2118},
	{0x2016, 0x0041},
	{0x2018, 0x00d8},
	{0x201a, 0x0145},
	{0x201c, 0x0006},
	{0x201e, 0x0181},
	{0x2020, 0x13cc},
	{0x2022, 0x2057},
	{0x2024, 0x7001},
	{0x2026, 0x0fca},
	{0x2028, 0x00cb},
	{0x202a, 0x009f},
	{0x202c, 0x7002},
	{0x202e, 0x13cc},
	{0x2030, 0x019b},
	{0x2032, 0x014d},
	{0x2034, 0x2987},
	{0x2036, 0x2766},
	{0x2038, 0x0020},
	{0x203a, 0x2060},
	{0x203c, 0x0e5d},
	{0x203e, 0x181d},
	{0x2040, 0x2066},
	{0x2042, 0x20c4},
	{0x2044, 0x5000},
	{0x2046, 0x0005},
	{0x2048, 0x0000},
	{0x204a, 0x01db},
	{0x204c, 0x025a},
	{0x204e, 0x00c0},
	{0x2050, 0x0005},
	{0x2052, 0x0006},
	{0x2054, 0x0ad9},
	{0x2056, 0x0259},
	{0x2058, 0x0618},
	{0x205a, 0x0258},
	{0x205c, 0x2266},
	{0x205e, 0x20c8},
	{0x2060, 0x2060},
	{0x2062, 0x707b},
	{0x2064, 0x0fdd},
	{0x2066, 0x81b8},
	{0x2068, 0x5040},
	{0x206a, 0x0020},
	{0x206c, 0x5060},
	{0x206e, 0x3143},
	{0x2070, 0x5081},
	{0x2072, 0x025c},
	{0x2074, 0x7800},
	{0x2076, 0x7400},
	{0x2078, 0x001c},
	{0x207a, 0x0242},
	{0x207c, 0x0942},
	{0x207e, 0x0bd9},
	{0x2080, 0x0259},
	{0x2082, 0x7008},
	{0x2084, 0x160e},
	{0x2086, 0x0047},
	{0x2088, 0x2118},
	{0x208a, 0x0041},
	{0x208c, 0x00d8},
	{0x208e, 0x0145},
	{0x2090, 0x0006},
	{0x2092, 0x0181},
	{0x2094, 0x13cc},
	{0x2096, 0x2057},
	{0x2098, 0x7001},
	{0x209a, 0x0fca},
	{0x209c, 0x00cb},
	{0x209e, 0x009f},
	{0x20a0, 0x7002},
	{0x20a2, 0x13cc},
	{0x20a4, 0x019b},
	{0x20a6, 0x014d},
	{0x20a8, 0x2987},
	{0x20aa, 0x2766},
	{0x20ac, 0x0020},
	{0x20ae, 0x2060},
	{0x20b0, 0x0e5d},
	{0x20b2, 0x181d},
	{0x20b4, 0x2066},
	{0x20b6, 0x20c4},
	{0x20b8, 0x50a0},
	{0x20ba, 0x0005},
	{0x20bc, 0x0000},
	{0x20be, 0x01db},
	{0x20c0, 0x025a},
	{0x20c2, 0x00c0},
	{0x20c4, 0x0005},
	{0x20c6, 0x0006},
	{0x20c8, 0x0ad9},
	{0x20ca, 0x0259},
	{0x20cc, 0x0618},
	{0x20ce, 0x0258},
	{0x20d0, 0x2266},
	{0x20d2, 0x20c8},
	{0x20d4, 0x2060},
	{0x20d6, 0x707b},
	{0x20d8, 0x0fdd},
	{0x20da, 0x86b8},
	{0x20dc, 0x50e0},
	{0x20de, 0x0020},
	{0x20e0, 0x5100},
	{0x20e2, 0x3143},
	{0x20e4, 0x5121},
	{0x20e6, 0x7800},
	{0x20e8, 0x3140},
	{0x20ea, 0x01c4},
	{0x20ec, 0x01c1},
	{0x20ee, 0x01c0},
	{0x20f0, 0x01c4},
	{0x20f2, 0x2700},
	{0x20f4, 0x3d40},
	{0x20f6, 0x7800},
	{0x20f8, 0xffff},
	{0x27fe, 0xe000},
	{0x3000, 0x60f8},
	{0x3002, 0x187f},
	{0x3004, 0x7060},
	{0x3006, 0x0114},
	{0x3008, 0x60b0},
	{0x300a, 0x1473},
	{0x300c, 0x0013},
	{0x300e, 0x140f},
	{0x3010, 0x0040},
	{0x3012, 0x100f},
	{0x3014, 0x60f8},
	{0x3016, 0x187f},
	{0x3018, 0x7060},
	{0x301a, 0x0114},
	{0x301c, 0x60b0},
	{0x301e, 0x1473},
	{0x3020, 0x0013},
	{0x3022, 0x140f},
	{0x3024, 0x0040},
	{0x3026, 0x000f},

	{0x0b00, 0x0000},
	{0x0b02, 0x0045},
	{0x0b04, 0xb405},
	{0x0b06, 0xc403},
	{0x0b08, 0x0081},
	{0x0b0a, 0x8252},
	{0x0b0c, 0xf814},
	{0x0b0e, 0xc618},
	{0x0b10, 0xa828},
	{0x0b12, 0x004c},
	{0x0b14, 0x4068},
	{0x0b16, 0x0000},
	{0x0f30, 0x5b15},
	{0x0f32, 0x7067},
	{0x0954, 0x0009},
	{0x0956, 0x0000},
	{0x0958, 0xbb80},
	{0x095a, 0x5140},
	{0x0c00, 0x1110},
	{0x0c02, 0x0011},
	{0x0c04, 0x0000},
	{0x0c06, 0x0200},
	{0x0c10, 0x0040},
	{0x0c12, 0x0040},
	{0x0c14, 0x0040},
	{0x0c16, 0x0040},
	{0x0a10, 0x4000},
	{0x3068, 0xf800},
	{0x306a, 0xf876},
	{0x006c, 0x0000},
	{0x005e, 0x0200},
	{0x000e, 0x0100},
	{0x0e0a, 0x0001},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a33},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c9},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x07bc},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0074, 0x080e},
	{0x0070, 0x0407},
	{0x0002, 0x0000},
	{0x0a02, 0x0100},
	{0x0a24, 0x0100},
	{0x0046, 0x0000},
	{0x0076, 0x0000},
	{0x0060, 0x0000},
	{0x0062, 0x0530},
	{0x0064, 0x0500},
	{0x0066, 0x0530},
	{0x0068, 0x0500},
	{0x0122, 0x0300},
	{0x015a, 0xff08},
	{0x0804, 0x0300},
	{0x0806, 0x0100},
	{0x005c, 0x0102},
	{0x0a1a, 0x0800},
};

static const struct hi556_reg mode_2592x1944_regs[] = {
	{0x0a00, 0x0000},
	{0x0b0a, 0x8252},
	{0x0f30, 0x5b15},
	{0x0f32, 0x7067},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a33},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0028},
	{0x002a, 0x002d},
	{0x0026, 0x0030},
	{0x002c, 0x07c9},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x0814},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x0798},
	{0x003e, 0x0000},
	{0x0074, 0x0812},
	{0x0070, 0x0409},
	{0x0804, 0x0300},
	{0x0806, 0x0100},
	{0x0a04, 0x014a},
	{0x090c, 0x0fdc},
	{0x090e, 0x002d},

	{0x0902, 0x4319},
	{0x0914, 0xc10a},
	{0x0916, 0x071f},
	{0x0918, 0x0408},
	{0x091a, 0x0c0d},
	{0x091c, 0x0f09},
	{0x091e, 0x0a00},
	{0x0958, 0xbb80},
};

static const struct hi556_reg mode_2592x1444_regs[] = {
	{0x0a00, 0x0000},
	{0x0b0a, 0x8252},
	{0x0f30, 0xe545},
	{0x0f32, 0x7067},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x000c, 0x0022},
	{0x0008, 0x0b00},
	{0x005a, 0x0202},
	{0x0012, 0x000e},
	{0x0018, 0x0a33},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0122},
	{0x002a, 0x0127},
	{0x0026, 0x012a},
	{0x002c, 0x06cf},
	{0x002e, 0x1111},
	{0x0030, 0x1111},
	{0x0032, 0x1111},
	{0x0006, 0x0821},
	{0x0a22, 0x0000},
	{0x0a12, 0x0a20},
	{0x0a14, 0x05a4},
	{0x003e, 0x0000},
	{0x0074, 0x081f},
	{0x0070, 0x040f},
	{0x0804, 0x0300},
	{0x0806, 0x0100},
	{0x0a04, 0x014a},
	{0x090c, 0x0fdc},
	{0x090e, 0x002d},
	{0x0902, 0x4319},
	{0x0914, 0xc10a},
	{0x0916, 0x071f},
	{0x0918, 0x0408},
	{0x091a, 0x0c0d},
	{0x091c, 0x0f09},
	{0x091e, 0x0a00},
	{0x0958, 0xbb80},
};

static const struct hi556_reg mode_1296x972_regs[] = {
	{0x0a00, 0x0000},
	{0x0b0a, 0x8259},
	{0x0f30, 0x5b15},
	{0x0f32, 0x7167},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0122},
	{0x0008, 0x0b00},
	{0x005a, 0x0404},
	{0x0012, 0x000c},
	{0x0018, 0x0a33},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0022},
	{0x002a, 0x002b},
	{0x0026, 0x0030},
	{0x002c, 0x07c9},
	{0x002e, 0x3311},
	{0x0030, 0x3311},
	{0x0032, 0x3311},
	{0x0006, 0x0814},
	{0x0a22, 0x0000},
	{0x0a12, 0x0510},
	{0x0a14, 0x03cc},
	{0x003e, 0x0000},
	{0x0074, 0x0812},
	{0x0070, 0x0409},
	{0x0804, 0x0308},
	{0x0806, 0x0100},
	{0x0a04, 0x016a},
	{0x090e, 0x0010},
	{0x090c, 0x09c0},

	{0x0902, 0x4319},
	{0x0914, 0xc106},
	{0x0916, 0x040e},
	{0x0918, 0x0304},
	{0x091a, 0x0708},
	{0x091c, 0x0e06},
	{0x091e, 0x0300},
	{0x0958, 0xbb80},
};

static const struct hi556_reg mode_1296x722_regs[] = {
	{0x0a00, 0x0000},
	{0x0b0a, 0x8259},
	{0x0f30, 0x5b15},
	{0x0f32, 0x7167},
	{0x004a, 0x0100},
	{0x004c, 0x0000},
	{0x004e, 0x0100},
	{0x000c, 0x0122},
	{0x0008, 0x0b00},
	{0x005a, 0x0404},
	{0x0012, 0x000c},
	{0x0018, 0x0a33},
	{0x0022, 0x0008},
	{0x0028, 0x0017},
	{0x0024, 0x0022},
	{0x002a, 0x002b},
	{0x0026, 0x012a},
	{0x002c, 0x06cf},
	{0x002e, 0x3311},
	{0x0030, 0x3311},
	{0x0032, 0x3311},
	{0x0006, 0x0814},
	{0x0a22, 0x0000},
	{0x0a12, 0x0510},
	{0x0a14, 0x02d2},
	{0x003e, 0x0000},
	{0x0074, 0x0812},
	{0x0070, 0x0409},
	{0x0804, 0x0308},
	{0x0806, 0x0100},
	{0x0a04, 0x016a},
	{0x090c, 0x09c0},
	{0x090e, 0x0010},
	{0x0902, 0x4319},
	{0x0914, 0xc106},
	{0x0916, 0x040e},
	{0x0918, 0x0304},
	{0x091a, 0x0708},
	{0x091c, 0x0e06},
	{0x091e, 0x0300},
	{0x0958, 0xbb80},
};

static const char * const hi556_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"100% Colour Bars",
	"Fade To Grey Colour Bars",
	"PN9",
	"Gradient Horizontal",
	"Gradient Vertical",
	"Check Board",
	"Slant Pattern",
};

static const s64 link_freq_menu_items[] = {
	HI556_LINK_FREQ_437MHZ,
};

static const struct hi556_link_freq_config link_freq_configs[] = {
	[HI556_LINK_FREQ_437MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_874mbps),
			.regs = mipi_data_rate_874mbps,
		}
	}
};

static const struct hi556_mode supported_modes[] = {
	{
		.width = HI556_PIXEL_ARRAY_WIDTH,
		.height = HI556_PIXEL_ARRAY_HEIGHT,
		.crop = {
			.left = HI556_PIXEL_ARRAY_LEFT,
			.top = HI556_PIXEL_ARRAY_TOP,
			.width = HI556_PIXEL_ARRAY_WIDTH,
			.height = HI556_PIXEL_ARRAY_HEIGHT
		},
		.fll_def = HI556_FLL_30FPS,
		.fll_min = HI556_FLL_30FPS_MIN,
		.llp = 0x0b00,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2592x1944_regs),
			.regs = mode_2592x1944_regs,
		},
		.link_freq_index = HI556_LINK_FREQ_437MHZ_INDEX,
	},
	{
		.width = HI556_PIXEL_ARRAY_WIDTH,
		.height = 1444,
		.crop = {
			.left = HI556_PIXEL_ARRAY_LEFT,
			.top = 250,
			.width = HI556_PIXEL_ARRAY_WIDTH,
			.height = 1444
		},
		.fll_def = 0x821,
		.fll_min = 0x821,
		.llp = 0x0b00,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2592x1444_regs),
			.regs = mode_2592x1444_regs,
		},
		.link_freq_index = HI556_LINK_FREQ_437MHZ_INDEX,
	},
	{
		.width = 1296,
		.height = 972,
		.crop = {
			.left = HI556_PIXEL_ARRAY_LEFT,
			.top = HI556_PIXEL_ARRAY_TOP,
			.width = HI556_PIXEL_ARRAY_WIDTH,
			.height = HI556_PIXEL_ARRAY_HEIGHT
		},
		.fll_def = HI556_FLL_30FPS,
		.fll_min = HI556_FLL_30FPS_MIN,
		.llp = 0x0b00,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1296x972_regs),
			.regs = mode_1296x972_regs,
		},
		.link_freq_index = HI556_LINK_FREQ_437MHZ_INDEX,
	},
	{
		.width = 1296,
		.height = 722,
		.crop = {
			.left = HI556_PIXEL_ARRAY_LEFT,
			.top = 250,
			.width = HI556_PIXEL_ARRAY_WIDTH,
			.height = 1444
		},
		.fll_def = HI556_FLL_30FPS,
		.fll_min = HI556_FLL_30FPS_MIN,
		.llp = 0x0b00,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1296x722_regs),
			.regs = mode_1296x722_regs,
		},
		.link_freq_index = HI556_LINK_FREQ_437MHZ_INDEX,
	},
};

struct hi556 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* GPIOs, clocks, etc. */
	struct gpio_desc *reset_gpio;
	struct clk *clk;
	struct regulator *avdd;

	/* Current mode */
	const struct hi556_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* True if the device has been identified */
	bool identified;
};

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * HI556_DATA_LANES;

	do_div(pixel_rate, HI556_RGB_DEPTH);

	return pixel_rate;
}

static int hi556_read_reg(struct hi556 *hi556, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
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

static int hi556_write_reg(struct hi556 *hi556, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int hi556_write_reg_list(struct hi556 *hi556,
				const struct hi556_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = hi556_write_reg(hi556, r_list->regs[i].address,
				      HI556_REG_VALUE_16BIT,
				      r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "failed to write reg 0x%4.4x. error = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int hi556_update_digital_gain(struct hi556 *hi556, u32 d_gain)
{
	int ret;

	ret = hi556_write_reg(hi556, HI556_REG_MWB_GR_GAIN,
			      HI556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = hi556_write_reg(hi556, HI556_REG_MWB_GB_GAIN,
			      HI556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = hi556_write_reg(hi556, HI556_REG_MWB_R_GAIN,
			      HI556_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	return hi556_write_reg(hi556, HI556_REG_MWB_B_GAIN,
			       HI556_REG_VALUE_16BIT, d_gain);
}

static int hi556_test_pattern(struct hi556 *hi556, u32 pattern)
{
	int ret;
	u32 val;

	if (pattern) {
		ret = hi556_read_reg(hi556, HI556_REG_ISP,
				     HI556_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;

		ret = hi556_write_reg(hi556, HI556_REG_ISP,
				      HI556_REG_VALUE_08BIT,
				      val | HI556_REG_ISP_TPG_EN);
		if (ret)
			return ret;
	}

	return hi556_write_reg(hi556, HI556_REG_TEST_PATTERN,
			       HI556_REG_VALUE_08BIT, pattern);
}

static int hi556_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hi556 *hi556 = container_of(ctrl->handler,
					     struct hi556, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = hi556->cur_mode->height + ctrl->val -
			       HI556_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(hi556->exposure,
					 hi556->exposure->minimum,
					 exposure_max, hi556->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = hi556_write_reg(hi556, HI556_REG_ANALOG_GAIN,
				      HI556_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = hi556_update_digital_gain(hi556, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = hi556_write_reg(hi556, HI556_REG_EXPOSURE,
				      HI556_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = hi556_write_reg(hi556, HI556_REG_FLL,
				      HI556_REG_VALUE_16BIT,
				      hi556->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = hi556_test_pattern(hi556, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops hi556_ctrl_ops = {
	.s_ctrl = hi556_set_ctrl,
};

static int hi556_init_controls(struct hi556 *hi556)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &hi556->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &hi556->mutex;
	hi556->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &hi556_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(link_freq_menu_items) - 1,
					0, link_freq_menu_items);
	if (hi556->link_freq)
		hi556->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	hi556->pixel_rate = v4l2_ctrl_new_std
			    (ctrl_hdlr, &hi556_ctrl_ops,
			     V4L2_CID_PIXEL_RATE, 0,
			     to_pixel_rate(HI556_LINK_FREQ_437MHZ_INDEX),
			     1,
			     to_pixel_rate(HI556_LINK_FREQ_437MHZ_INDEX));
	hi556->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &hi556_ctrl_ops,
					  V4L2_CID_VBLANK,
					  hi556->cur_mode->fll_min -
					  hi556->cur_mode->height,
					  HI556_FLL_MAX -
					  hi556->cur_mode->height, 1,
					  hi556->cur_mode->fll_def -
					  hi556->cur_mode->height);

	h_blank = hi556->cur_mode->llp - hi556->cur_mode->width;

	hi556->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &hi556_ctrl_ops,
					  V4L2_CID_HBLANK, h_blank, h_blank, 1,
					  h_blank);
	if (hi556->hblank)
		hi556->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &hi556_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  HI556_ANAL_GAIN_MIN, HI556_ANAL_GAIN_MAX,
			  HI556_ANAL_GAIN_STEP, HI556_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &hi556_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  HI556_DGTL_GAIN_MIN, HI556_DGTL_GAIN_MAX,
			  HI556_DGTL_GAIN_STEP, HI556_DGTL_GAIN_DEFAULT);
	exposure_max = hi556->cur_mode->fll_def - HI556_EXPOSURE_MAX_MARGIN;
	hi556->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &hi556_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    HI556_EXPOSURE_MIN, exposure_max,
					    HI556_EXPOSURE_STEP,
					    exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &hi556_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(hi556_test_pattern_menu) - 1,
				     0, 0, hi556_test_pattern_menu);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	hi556->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void hi556_assign_pad_format(const struct hi556_mode *mode,
				    struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int hi556_identify_module(struct hi556 *hi556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	int ret;
	u32 val;

	if (hi556->identified)
		return 0;

	ret = hi556_read_reg(hi556, HI556_REG_CHIP_ID,
			     HI556_REG_VALUE_16BIT, &val);
	if (ret)
		return ret;

	if (val != HI556_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			HI556_CHIP_ID, val);
		return -ENXIO;
	}

	hi556->identified = true;

	return 0;
}

static const struct v4l2_rect *
__hi556_get_pad_crop(struct hi556 *hi556,
		     struct v4l2_subdev_state *sd_state,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_crop(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &hi556->cur_mode->crop;
	}

	return NULL;
}

static int hi556_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct hi556 *hi556 = to_hi556(sd);

		mutex_lock(&hi556->mutex);
		sel->r = *__hi556_get_pad_crop(hi556, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&hi556->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = HI556_NATIVE_WIDTH;
		sel->r.height = HI556_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = HI556_PIXEL_ARRAY_TOP;
		sel->r.left = HI556_PIXEL_ARRAY_LEFT;
		sel->r.width = HI556_PIXEL_ARRAY_WIDTH;
		sel->r.height = HI556_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int hi556_start_streaming(struct hi556 *hi556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);
	const struct hi556_reg_list *reg_list;
	int link_freq_index, ret;

	ret = hi556_identify_module(hi556);
	if (ret)
		return ret;

	link_freq_index = hi556->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = hi556_write_reg_list(hi556, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &hi556->cur_mode->reg_list;
	ret = hi556_write_reg_list(hi556, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(hi556->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = hi556_write_reg(hi556, HI556_REG_MODE_SELECT,
			      HI556_REG_VALUE_16BIT, HI556_MODE_STREAMING);

	if (ret) {
		dev_err(&client->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void hi556_stop_streaming(struct hi556 *hi556)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi556->sd);

	if (hi556_write_reg(hi556, HI556_REG_MODE_SELECT,
			    HI556_REG_VALUE_16BIT, HI556_MODE_STANDBY))
		dev_err(&client->dev, "failed to set stream");
}

static int hi556_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct hi556 *hi556 = to_hi556(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&hi556->mutex);
	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0) {
			mutex_unlock(&hi556->mutex);
			return ret;
		}

		ret = hi556_start_streaming(hi556);
		if (ret) {
			enable = 0;
			hi556_stop_streaming(hi556);
			pm_runtime_put(&client->dev);
		}
	} else {
		hi556_stop_streaming(hi556);
		pm_runtime_put(&client->dev);
	}

	mutex_unlock(&hi556->mutex);

	return ret;
}

static int hi556_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct hi556 *hi556 = to_hi556(sd);
	const struct hi556_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&hi556->mutex);
	hi556_assign_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		hi556->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(hi556->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(hi556->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->fll_def - mode->height;
		__v4l2_ctrl_modify_range(hi556->vblank,
					 mode->fll_min - mode->height,
					 HI556_FLL_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(hi556->vblank, vblank_def);

		h_blank = hi556->cur_mode->llp - hi556->cur_mode->width;

		__v4l2_ctrl_modify_range(hi556->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&hi556->mutex);

	return 0;
}

static int hi556_get_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct hi556 *hi556 = to_hi556(sd);

	mutex_lock(&hi556->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							    fmt->pad);
	else
		hi556_assign_pad_format(hi556->cur_mode, &fmt->format);

	mutex_unlock(&hi556->mutex);

	return 0;
}

static int hi556_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int hi556_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int hi556_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct hi556 *hi556 = to_hi556(sd);
	struct v4l2_rect *try_crop;

	mutex_lock(&hi556->mutex);
	hi556_assign_pad_format(&supported_modes[0],
				v4l2_subdev_state_get_format(fh->state, 0));

	/* Initialize try_crop rectangle. */
	try_crop = v4l2_subdev_state_get_crop(fh->state, 0);
	try_crop->top = HI556_PIXEL_ARRAY_TOP;
	try_crop->left = HI556_PIXEL_ARRAY_LEFT;
	try_crop->width = HI556_PIXEL_ARRAY_WIDTH;
	try_crop->height = HI556_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&hi556->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops hi556_video_ops = {
	.s_stream = hi556_set_stream,
};

static const struct v4l2_subdev_pad_ops hi556_pad_ops = {
	.set_fmt = hi556_set_format,
	.get_fmt = hi556_get_format,
	.get_selection = hi556_get_selection,
	.enum_mbus_code = hi556_enum_mbus_code,
	.enum_frame_size = hi556_enum_frame_size,
};

static const struct v4l2_subdev_ops hi556_subdev_ops = {
	.video = &hi556_video_ops,
	.pad = &hi556_pad_ops,
};

static const struct media_entity_operations hi556_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops hi556_internal_ops = {
	.open = hi556_open,
};

static int hi556_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 mclk;
	int ret = 0;
	unsigned int i, j;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver,
	 * wait for this.
	 */
	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EPROBE_DEFER;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &mclk);
	if (ret) {
		dev_err(dev, "can't get clock frequency");
		return ret;
	}

	if (mclk != HI556_MCLK) {
		dev_err(dev, "external clock %d is not supported", mclk);
		return -EINVAL;
	}

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (link_freq_menu_items[i] ==
				bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				link_freq_menu_items[i]);
			ret = -EINVAL;
			goto check_hwcfg_error;
		}
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static void hi556_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi556 *hi556 = to_hi556(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&hi556->mutex);
}

static int hi556_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hi556 *hi556 = to_hi556(sd);
	int ret;

	gpiod_set_value_cansleep(hi556->reset_gpio, 1);

	ret = regulator_disable(hi556->avdd);
	if (ret) {
		dev_err(dev, "failed to disable avdd: %d\n", ret);
		gpiod_set_value_cansleep(hi556->reset_gpio, 0);
		return ret;
	}

	clk_disable_unprepare(hi556->clk);
	return 0;
}

static int hi556_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hi556 *hi556 = to_hi556(sd);
	int ret;

	ret = clk_prepare_enable(hi556->clk);
	if (ret)
		return ret;

	ret = regulator_enable(hi556->avdd);
	if (ret) {
		dev_err(dev, "failed to enable avdd: %d\n", ret);
		clk_disable_unprepare(hi556->clk);
		return ret;
	}

	gpiod_set_value_cansleep(hi556->reset_gpio, 0);
	usleep_range(5000, 5500);
	return 0;
}

static int hi556_probe(struct i2c_client *client)
{
	struct hi556 *hi556;
	bool full_power;
	int ret;

	ret = hi556_check_hwcfg(&client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	hi556 = devm_kzalloc(&client->dev, sizeof(*hi556), GFP_KERNEL);
	if (!hi556)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&hi556->sd, client, &hi556_subdev_ops);

	hi556->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hi556->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(hi556->reset_gpio),
				     "failed to get reset GPIO\n");

	hi556->clk = devm_clk_get_optional(&client->dev, "clk");
	if (IS_ERR(hi556->clk))
		return dev_err_probe(&client->dev, PTR_ERR(hi556->clk),
				     "failed to get clock\n");

	/* The regulator core will provide a "dummy" regulator if necessary */
	hi556->avdd = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(hi556->avdd))
		return dev_err_probe(&client->dev, PTR_ERR(hi556->avdd),
				     "failed to get avdd regulator\n");

	full_power = acpi_dev_state_d0(&client->dev);
	if (full_power) {
		/* Ensure non ACPI managed resources are enabled */
		ret = hi556_resume(&client->dev);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "failed to power on sensor\n");

		ret = hi556_identify_module(hi556);
		if (ret) {
			dev_err(&client->dev, "failed to find sensor: %d", ret);
			goto probe_error_power_off;
		}
	}

	mutex_init(&hi556->mutex);
	hi556->cur_mode = &supported_modes[0];
	ret = hi556_init_controls(hi556);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	hi556->sd.internal_ops = &hi556_internal_ops;
	hi556->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	hi556->sd.entity.ops = &hi556_subdev_entity_ops;
	hi556->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	hi556->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&hi556->sd.entity, 1, &hi556->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&hi556->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	/* Set the device's state to active if it's in D0 state. */
	if (full_power)
		pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&hi556->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(hi556->sd.ctrl_handler);
	mutex_destroy(&hi556->mutex);

probe_error_power_off:
	if (full_power)
		hi556_suspend(&client->dev);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(hi556_pm_ops, hi556_suspend, hi556_resume,
				 NULL);

#ifdef CONFIG_ACPI
static const struct acpi_device_id hi556_acpi_ids[] = {
	{"INT3537"},
	{}
};

MODULE_DEVICE_TABLE(acpi, hi556_acpi_ids);
#endif

static struct i2c_driver hi556_i2c_driver = {
	.driver = {
		.name = "hi556",
		.acpi_match_table = ACPI_PTR(hi556_acpi_ids),
		.pm = pm_sleep_ptr(&hi556_pm_ops),
	},
	.probe = hi556_probe,
	.remove = hi556_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(hi556_i2c_driver);

MODULE_AUTHOR("Shawn Tu");
MODULE_DESCRIPTION("Hynix HI556 sensor driver");
MODULE_LICENSE("GPL v2");
