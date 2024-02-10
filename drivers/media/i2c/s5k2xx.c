// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Intel Corporation.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define S5K2XX_MCLK			24000000
#define S5K2XX_DATA_LANES		4
#define S5K2XX_MAX_COLOR_DEPTH		10
#define S5K2XX_MAX_COLOR_VAL		(BIT(S5K2XX_MAX_COLOR_DEPTH) - 1)

#define S5K2XX_REG_CHIP_ID		0x0000

#define S5K2XX_REG_MODE_SELECT		0x0100
#define S5K2XX_MODE_STANDBY		0x0000
#define S5K2XX_MODE_STREAMING		0x0100

/* vertical-timings from sensor */
#define S5K2XX_REG_FLL			0x0340
#define S5K2XX_FLL_30FPS		0x0e1a
#define S5K2XX_FLL_30FPS_MIN		0x0e1a
#define S5K2XX_FLL_MAX			0xffff

/* horizontal-timings from sensor */
#define S5K2XX_REG_LLP			0x0342

/* Exposure controls from sensor */
#define S5K2XX_REG_EXPOSURE		0x0202
#define S5K2XX_EXPOSURE_MIN		7
#define S5K2XX_EXPOSURE_MAX_MARGIN	8
#define S5K2XX_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define S5K2XX_REG_ANALOG_GAIN_MIN	0x0084
#define S5K2XX_REG_ANALOG_GAIN_MAX	0x0086
#define S5K2XX_REG_ANALOG_GAIN		0x0204
#define S5K2XX_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define S5K2XX_REG_DIG_GAIN		0x020e
#define S5K2XX_DGTL_GAIN_MIN		0x100
#define S5K2XX_DGTL_GAIN_MAX		0x1000
#define S5K2XX_DGTL_GAIN_STEP		1
#define S5K2XX_DGTL_GAIN_DEFAULT	0x100

/* Test pattern generator */
#define S5K2XX_REG_TEST_PATTERN		0x0600
#define S5K2XX_REG_TEST_PATTERN_RED	0x0602
#define S5K2XX_REG_TEST_PATTERN_GREENR	0x0604
#define S5K2XX_REG_TEST_PATTERN_BLUE	0x0606
#define S5K2XX_REG_TEST_PATTERN_GREENB	0x0608

static const char * const s5k2xx_supply_names[] = { "avdd", "dvdd", "vio", "aux" };

#define S5K2XX_NUM_SUPPLIES		ARRAY_SIZE(s5k2xx_supply_names)
#define S5K2XX_LINK_FREQ_562MHZ 562000000
#define S5K2XX_LINK_FREQ_580MHZ 580500000
#define S5K2XX_LINK_FREQ_720MHZ 720000000
#define S5K2XX_LINK_FREQ_678MHZ 678000000
#define S5K2XX_LINK_FREQ_1050MHZ 1050000000

enum {
	S5K2XX_LINK_FREQ_562MHZ_INDEX,
	S5K2XX_LINK_FREQ_580MHZ_INDEX,
	S5K2XX_LINK_FREQ_678MHZ_INDEX,
	S5K2XX_LINK_FREQ_720MHZ_INDEX,
	S5K2XX_LINK_FREQ_1050MHZ_INDEX,
};

static const s64 link_freq_menu_items[] = {
	S5K2XX_LINK_FREQ_562MHZ,
	S5K2XX_LINK_FREQ_580MHZ,
	S5K2XX_LINK_FREQ_678MHZ,
	S5K2XX_LINK_FREQ_720MHZ,
	S5K2XX_LINK_FREQ_1050MHZ,
};

struct s5k2xx_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Horizontal timining size */
	u32 llp;

	/* Default vertical timining size */
	u32 fll_def;

	/* Min vertical timining size */
	u32 fll_min;

	/* Refresh rate */
	u32 fps;

	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct reg_sequence *regs;
	u32 num_regs;
};

struct s5k2xx_data {
	const char *model;
	u32 chip_id;
	const struct s5k2xx_mode *modes;
	size_t num_modes;
	const struct reg_sequence *init_regs;
	u32 num_init_regs;
};

#define to_s5k2(_sd) container_of(_sd, struct s5k2xx, sd)

static const char * const s5k2xx_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"100% Colour Bars",
	"Fade To Grey Colour Bars",
	"PN9",
};

static const struct reg_sequence s5k2p6_init[] = {
	{ 0xfcfc, 0x4000 },
	{ 0x6028, 0x2000 },
	{ 0x0100, 0x0000 },
	{ 0x0344, 0x0018 },
	{ 0x0348, 0x1217 },
	{ 0x0380, 0x0001 },
	{ 0x0382, 0x0001 },
	{ 0x0384, 0x0001 },
	{ 0x0408, 0x0000 },
	{ 0x040a, 0x0000 },
	{ 0x0136, 0x1800 },
	{ 0x0300, 0x0003 },
	{ 0x0302, 0x0001 },
	{ 0x0304, 0x0006 },
	{ 0x030c, 0x0004 }, // MIPI divider, Default multiplier: 124
	{ 0x0200, 0x0200 },
	{ 0x0204, 0x0080 },
	{ 0x0114, 0x0300 },
};

static const struct reg_sequence s5k2p6_mode_4608x3456_regs[] = {
	{ 0xfcfc, 0x4000 },
	{ 0x6028, 0x2000 },
	{ 0x6214, 0x7971 },
	{ 0x6218, 0x7150 },
	{ 0x30ce, 0x0000 },
	{ 0x37f6, 0x0021 },
	{ 0x3198, 0x0007 },
	{ 0x319a, 0x0100 },
	{ 0x3056, 0x0100 },
	{ 0x602a, 0x1bb0 },
	{ 0x6f12, 0x0000 },
	{ 0x0b0e, 0x0100 },
	{ 0x30d8, 0x0100 },
	{ 0x31b0, 0x0008 },
	{ 0x0340, 0x0e1a },
	{ 0x0342, 0x1428 },
	{ 0x0346, 0x0010 },
	{ 0x034a, 0x0d8f },
	{ 0x034c, 0x1200 },
	{ 0x034e, 0x0d80 },
	{ 0x0900, 0x0011 },
	{ 0x0386, 0x0001 },
	{ 0x0400, 0x0000 },
	{ 0x0404, 0x0000 },
	{ 0x0306, 0x0069 },
	{ 0x1130, 0x440c },
	{ 0x030e, 0x0078 },
	{ 0x0202, 0x0e10 },
	{ 0x021e, 0x0e10 },
	{ 0x0216, 0x0000 },
	{ 0x6214, 0x7970 },
};

static const struct reg_sequence s5k2p6_mode_1152x656_regs[] = {
	{ 0xfcfc, 0x4000 },
	{ 0x6028, 0x2000 },
	{ 0x6214, 0x7971 },
	{ 0x6218, 0x7150 },
	{ 0x30ce, 0x0000 },
	{ 0x37f6, 0x0021 },
	{ 0x3198, 0x0007 },
	{ 0x319a, 0x0000 },
	{ 0x3056, 0x0100 },
	{ 0x602a, 0x1bb0 },
	{ 0x6f12, 0x0000 },
	{ 0x0b0e, 0x0100 },
	{ 0x30d8, 0x0100 },
	{ 0x31b0, 0x0008 },
	{ 0x0340, 0x039c },
	{ 0x0342, 0x1448 },
	{ 0x0346, 0x01b0 },
	{ 0x034a, 0x0bef },
	{ 0x034c, 0x0480 },
	{ 0x034e, 0x0290 },
	{ 0x0900, 0x0114 },
	{ 0x0386, 0x0007 },
	{ 0x0400, 0x0001 },
	{ 0x0404, 0x0040 },
	{ 0x0306, 0x006c },
	{ 0x1130, 0x4411 },
	{ 0x030e, 0x0071 },
	{ 0x0202, 0x0e10 },
	{ 0x021e, 0x0e10 },
	{ 0x0216, 0x0000 },
	{ 0x6214, 0x7970 },
};

static const struct s5k2xx_mode s5k2p6_modes[] = {
	{
		.width = 4608,
		.height = 3456,
		.fps = 30,
		.fll_def = 3610,
		.fll_min = 3610,
		.llp = 5160,
		.link_freq_index = S5K2XX_LINK_FREQ_720MHZ_INDEX,
		.regs = s5k2p6_mode_4608x3456_regs,
		.num_regs = ARRAY_SIZE(s5k2p6_mode_4608x3456_regs),
	}, {
		.width = 1152,
		.height = 656,
		.fps = 120,
		.fll_def = 924,
		.fll_min = 924,
		.llp = 5192,
		.link_freq_index = S5K2XX_LINK_FREQ_678MHZ_INDEX,
		.regs = s5k2p6_mode_1152x656_regs,
		.num_regs = ARRAY_SIZE(s5k2p6_mode_1152x656_regs),
	}
};

static struct s5k2xx_data s5k2p6sx_data = {
	.model = "s5k2p6sx",
	.chip_id = 0x2106,
	.modes = s5k2p6_modes,
	.num_modes = ARRAY_SIZE(s5k2p6_modes),
	.init_regs = s5k2p6_init,
	.num_init_regs = ARRAY_SIZE(s5k2p6_init),
};

static const struct reg_sequence s5k2x7_init[] = {
	{ 0xfcfc, 0x4000 },
	{ 0x6000, 0x0085 },
	{ 0x6214, 0x7971 },
	{ 0x6218, 0x7150 },
	{ 0x0200, 0x0000 },
	{ 0x0202, 0x0620 },
	{ 0x0204, 0x0020 },
	{ 0x0216, 0x0000 },
	{ 0x021c, 0x0000 },
	{ 0x021e, 0x0040 },
	{ 0x0900, 0x0111 },
	{ 0x0b00, 0x0080 },
	{ 0x0b04, 0x0101 },
	{ 0x0b08, 0x0000 },
	{ 0x0b0e, 0x0000 },
	{ 0x112c, 0x4220 },
	{ 0x112e, 0x0000 },
	{ 0x3000, 0x0001 },
	{ 0x3050, 0x0000 },
	{ 0x3052, 0x0400 },
	{ 0x3054, 0x0000 },
	{ 0x305e, 0x0000 },
	{ 0x306a, 0x0340 },
	{ 0x3076, 0x0000 },
	{ 0x319e, 0x0100 },
	{ 0x31e0, 0x0000 },
	{ 0x31e2, 0x0000 },
	{ 0x31e4, 0x0000 },
	{ 0x31f8, 0x0008 },
	{ 0x31fa, 0x1158 },
	{ 0x3264, 0x0009 },
	{ 0x3284, 0x0044 },
	{ 0x3290, 0x0000 },
	{ 0x3298, 0x0020 },
	{ 0x33e0, 0x00b9 },
	{ 0x33e4, 0x006f },
	{ 0x33e8, 0x0076 },
	{ 0x33ec, 0x011a },
	{ 0x33f0, 0x0004 },
	{ 0x33f4, 0x0000 },
	{ 0x33f8, 0x0000 },
	{ 0x33fc, 0x0000 },
	{ 0x3400, 0x00b9 },
	{ 0x3404, 0x006f },
	{ 0x3408, 0x0076 },
	{ 0x340c, 0x011a },
	{ 0x3410, 0x0004 },
	{ 0x3414, 0x0000 },
	{ 0x3418, 0x0000 },
	{ 0x341c, 0x0000 },
	{ 0x3834, 0x0803 },
	{ 0x3842, 0x240f },
	{ 0x3844, 0x00ff },
	{ 0x3850, 0x005a },
	{ 0x655e, 0x03e8 },
	{ 0xb134, 0x0180 },
	{ 0xb13c, 0x0400 },
	{ 0xf150, 0x000a },
	{ 0xf160, 0x000b },
	{ 0xf458, 0x0009 },
	{ 0xf45a, 0x000e },
	{ 0xf45c, 0x000e },
	{ 0xf45e, 0x0018 },
	{ 0xf460, 0x0010 },
	{ 0xf462, 0x0018 },
	{ 0xf46c, 0x002d },
	{ 0xf47a, 0x00fc },
	{ 0xf47c, 0x0000 },
	{ 0xf404, 0x0ff3 },
	{ 0xf426, 0x00a0 },
	{ 0xf428, 0x44c2 },
	{ 0xf486, 0x0480 },
	{ 0xf4ae, 0x003c },
	{ 0xf4b0, 0x1124 },
	{ 0xf4b2, 0x003d },
	{ 0xf4b4, 0x1125 },
	{ 0xf4b6, 0x0044 },
	{ 0xf4b8, 0x112c },
	{ 0xf4ba, 0x0045 },
	{ 0xf4bc, 0x112d },
	{ 0xf4be, 0x004c },
	{ 0xf4c0, 0x1134 },
	{ 0xf4c2, 0x004d },
	{ 0xf4c4, 0x1135 },
};

static const struct reg_sequence s5k2x7_mode_2832x2124_regs[] = {
	{ 0x6000, 0x0005 },
	{ 0xfcfc, 0x2000 },
	{ 0x00e0, 0x0100 },
	{ 0x03e0, 0x0001 },
	{ 0x03e2, 0x0100 },
	{ 0x03e4, 0x0000 },
	{ 0x03f4, 0x0100 },
	{ 0x03f6, 0x0120 },
	{ 0x2750, 0x0100 },
	{ 0x2770, 0x0100 },
	{ 0xfcfc, 0x4000 },
	{ 0x6000, 0x0085 },
	{ 0x0306, 0x00f0 },
	{ 0x030c, 0x0004 },
	{ 0x030e, 0x0183 },
	{ 0x0340, 0x1088 },
	{ 0x0342, 0x1d88 },
	{ 0x0344, 0x0010 },
	{ 0x0346, 0x0004 },
	{ 0x0348, 0x161f },
	{ 0x034a, 0x109b },
	{ 0x034c, 0x0b00 },
	{ 0x034e, 0x084c },
	{ 0x0380, 0x0002 },
	{ 0x0382, 0x0002 },
	{ 0x0384, 0x0002 },
	{ 0x0386, 0x0002 },
	{ 0x3002, 0x0100 },
	{ 0x306e, 0x0000 },
	{ 0x3070, 0x0000 },
	{ 0x3072, 0x0000 },
	{ 0x3074, 0x0000 },
	{ 0x31a0, 0xffff },
	{ 0x31c0, 0x0004 },
	{ 0x324a, 0x0300 },
	{ 0x3258, 0x0036 },
	{ 0x3260, 0x002a },
	{ 0x3268, 0x0001 },
	{ 0x3270, 0x0024 },
	{ 0x3288, 0x0030 },
	{ 0x32a0, 0x0024 },
	{ 0x32c4, 0x0029 },
	{ 0x337c, 0x0066 },
	{ 0x3458, 0x002e },
	{ 0x382e, 0x060f },
	{ 0x3830, 0x0805 },
	{ 0x3832, 0x0605 },
	{ 0x3840, 0x0064 },
	{ 0xf424, 0x0000 },
	{ 0x30b6, 0x0004 },
	{ 0x30b8, 0x0500 },
	{ 0x30be, 0x0000 },
	{ 0x30c2, 0x0100 },
	{ 0x30c4, 0x0100 },
	{ 0x30c0, 0x0100 },
	{ 0x300c, 0x0001 },
	{ 0x3844, 0x00bf },
};

static const struct reg_sequence s5k2x7_mode_5664x4248_regs[] = {
	{ 0x6000, 0x0005 },
	{ 0xfcfc, 0x2000 },
	{ 0x00e0, 0x0000 },
	{ 0x03e0, 0x0000 },
	{ 0x03e2, 0x0101 },
	{ 0x03e4, 0x0001 },
	{ 0x03f4, 0x0000 },
	{ 0x03f6, 0x0020 },
	{ 0x2750, 0x0100 },
	{ 0x2770, 0x0100 },
	{ 0xfcfc, 0x4000 },
	{ 0x6000, 0x0085 },
	{ 0x0306, 0x00f0 },
	{ 0x030c, 0x0002 },
	{ 0x030e, 0x00af },
	{ 0x0340, 0x1100 },
	{ 0x0306, 0x00f0 },
	{ 0x030c, 0x0002 },
	{ 0x030e, 0x00af },
	{ 0x0340, 0x1198 },
	{ 0x0342, 0x1bc0 },
	{ 0x0344, 0x0000 },
	{ 0x0346, 0x0004 },
	{ 0x0348, 0x161f },
	{ 0x0382, 0x0001 },
	{ 0x0384, 0x0001 },
	{ 0x0386, 0x0001 },
	{ 0x3002, 0x0000 },
	{ 0x306e, 0x0100 },
	{ 0x3070, 0x0000 },
	{ 0x3072, 0x0001 },
	{ 0x3074, 0x0001 },
	{ 0x31a0, 0x0050 },
	{ 0x31c0, 0x0008 },
	{ 0x324a, 0x0100 },
	{ 0x3258, 0x0012 },
	{ 0x3260, 0x0036 },
	{ 0x3268, 0x0036 },
	{ 0x3270, 0x0030 },
	{ 0x3288, 0x0024 },
	{ 0x32a0, 0x0018 },
	{ 0x32c4, 0x0035 },
	{ 0x337c, 0x005a },
	{ 0x3458, 0x0046 },
	{ 0x382e, 0x090f },
	{ 0x3830, 0x0f05 },
	{ 0x3832, 0x0005 },
	{ 0x3840, 0x0064 },
	{ 0xf424, 0x0040 },
	{ 0x30b6, 0x0004 },
	{ 0x30b8, 0x0500 },
	{ 0x30be, 0x0000 },
	{ 0x30c2, 0x0100 },
	{ 0x30c4, 0x0100 },
	{ 0x30c0, 0x0100 },
	{ 0x300c, 0x0000 },
	{ 0x3844, 0x00ff },
};

static const struct reg_sequence s5k2x7_mode_640x480_regs[] = {
	{ 0x6000, 0x0005 },
	{ 0xfcfc, 0x2000 },
	{ 0x00e0, 0x0100 },
	{ 0x03e0, 0x0001 },
	{ 0x03e2, 0x0100 },
	{ 0x03e4, 0x0000 },
	{ 0x03f4, 0x0100 },
	{ 0x03f6, 0x0120 },
	{ 0x2750, 0x0100 },
	{ 0x2770, 0x0100 },
	{ 0xfcfc, 0x4000 },
	{ 0x6000, 0x0085 },
	{ 0x0306, 0x00f0 },
	{ 0x030c, 0x0004 },
	{ 0x030e, 0x0183 },
	{ 0x0340, 0x0839 },
	{ 0x0342, 0x0ed8 },
	{ 0x0344, 0x0890 },
	{ 0x0346, 0x0670 },
	{ 0x0348, 0x0d90 },
	{ 0x034a, 0x0a30 },
	{ 0x034c, 0x0280 },
	{ 0x034e, 0x01e0 },
	{ 0x0380, 0x0002 },
	{ 0x0382, 0x0002 },
	{ 0x0384, 0x0002 },
	{ 0x0386, 0x0002 },
	{ 0x3002, 0x0100 },
	{ 0x306e, 0x0000 },
	{ 0x3070, 0x0000 },
	{ 0x3072, 0x0000 },
	{ 0x3074, 0x0000 },
	{ 0x31a0, 0xffff },
	{ 0x31c0, 0x0004 },
	{ 0x324a, 0x0300 },
	{ 0x3258, 0x0036 },
	{ 0x3260, 0x002a },
	{ 0x3268, 0x0001 },
	{ 0x3270, 0x0024 },
	{ 0x3288, 0x0030 },
	{ 0x32a0, 0x0024 },
	{ 0x32c4, 0x0029 },
	{ 0x337c, 0x0066 },
	{ 0x3458, 0x002e },
	{ 0x382e, 0x060f },
	{ 0x3830, 0x0805 },
	{ 0x3832, 0x0605 },
	{ 0x3840, 0x0064 },
	{ 0xf424, 0x0000 },
	{ 0x30b6, 0x0004 },
	{ 0x30b8, 0x0500 },
	{ 0x30be, 0x0000 },
	{ 0x30c2, 0x0100 },
	{ 0x30c4, 0x0100 },
	{ 0x30c0, 0x0100 },
	{ 0x300c, 0x0001 },
	{ 0x3844, 0x00bf },
};

static const struct s5k2xx_mode s5k2x7_modes[] = {
	{
		.width = 2816,
		.height = 2124,
		.fps = 30,
		.fll_def = 4232,
		.fll_min = 4232,
		.llp = 7560,
		.link_freq_index = S5K2XX_LINK_FREQ_580MHZ_INDEX,
		.regs = s5k2x7_mode_2832x2124_regs,
		.num_regs = ARRAY_SIZE(s5k2x7_mode_2832x2124_regs),
	}, {
		.width = 5664,
		.height = 4248,
		.fps = 30,
		.fll_def = 4504,
		.fll_min = 4504,
		.llp = 7104,
		.link_freq_index = S5K2XX_LINK_FREQ_1050MHZ_INDEX,
		.num_regs = ARRAY_SIZE(s5k2x7_mode_5664x4248_regs),
		.regs = s5k2x7_mode_5664x4248_regs,
	}, {
		.width = 640,
		.height = 480,
		.fps = 120,
		.fll_def = 2105,
		.fll_min = 2105,
		.llp = 3800,
		.link_freq_index = S5K2XX_LINK_FREQ_580MHZ_INDEX,
		.regs = s5k2x7_mode_640x480_regs,
		.num_regs = ARRAY_SIZE(s5k2x7_mode_640x480_regs),
	},
};

static struct s5k2xx_data s5k2x7sp_data = {
	.model = "s5k2x7sp",
	.chip_id = 0x2187,
	.modes = s5k2x7_modes,
	.num_modes = ARRAY_SIZE(s5k2x7_modes),
	.init_regs = s5k2x7_init,
	.num_init_regs = ARRAY_SIZE(s5k2x7_init),
};

static const struct reg_sequence s5k3p8sp_init[] = {
	{0x6028, 0x2000},
	{0x602a, 0x2f38},
	{0x6f12, 0x0088},
	{0x6f12, 0x0d70},
	{0x0202, 0x0200},
	{0x0200, 0x0618},
	{0x3604, 0x0002},
	{0x3606, 0x0103},
	{0xf496, 0x0048},
	{0xf470, 0x0020},
	{0xf43a, 0x0015},
	{0xf484, 0x0006},
	{0xf440, 0x00af},
	{0xf442, 0x44c6},
	{0xf408, 0xfff7},
	{0x3664, 0x0019},
	{0xf494, 0x1010},
	{0x367a, 0x0100},
	{0x362a, 0x0104},
	{0x362e, 0x0404},
	{0x32b2, 0x0008},
	{0x3286, 0x0003},
	{0x328a, 0x0005},
	{0xf47c, 0x001f},
	{0xf62e, 0x00c5},
	{0xf630, 0x00cd},
	{0xf632, 0x00dd},
	{0xf634, 0x00e5},
	{0xf636, 0x00f5},
	{0xf638, 0x00fd},
	{0xf63a, 0x010d},
	{0xf63c, 0x0115},
	{0xf63e, 0x0125},
	{0xf640, 0x012d},
	{0x3070, 0x0000},
	{0x0b0e, 0x0000},
	{0x31c0, 0x00c8},
	{0x1006, 0x0004},
};

static const struct reg_sequence s5k3p8sp_mode_4608x3456_regs[] = {
	{0x6028, 0x2000},
	{0x0136, 0x1800},
	{0x0304, 0x0006},
	{0x0306, 0x0069},
	{0x0302, 0x0001},
	{0x0300, 0x0003},
	{0x030c, 0x0004},
	{0x030e, 0x0071},
	{0x030a, 0x0001},
	{0x0308, 0x0008},
	{0x0344, 0x0018},
	{0x0346, 0x0018},
	{0x0348, 0x1217},
	{0x034a, 0x0d97},
	{0x034c, 0x1200},
	{0x034e, 0x0d80},
	{0x0408, 0x0000},
	{0x0900, 0x0011},
	{0x0380, 0x0001},
	{0x0382, 0x0001},
	{0x0384, 0x0001},
	{0x0386, 0x0001},
	{0x0400, 0x0000},
	{0x0404, 0x0010},
	{0x0342, 0x1400},
	{0x0340, 0x0e3b},
	{0x602a, 0x1704},
	{0x6f12, 0x8010},
	{0x317a, 0x0130},
	{0x31a4, 0x0102},
};

static const struct s5k2xx_mode s5k3p8sp_modes[] = {
	{
		.width = 4608,
		.height = 3456,
		.fps = 30,
		.fll_def = 3643,
		.fll_min = 3643,
		.llp = 5120,
		.link_freq_index = S5K2XX_LINK_FREQ_678MHZ_INDEX,
		.regs = s5k3p8sp_mode_4608x3456_regs,
		.num_regs = ARRAY_SIZE(s5k3p8sp_mode_4608x3456_regs),
	},
};

static struct s5k2xx_data s5k3p8sp_data = {
	.model = "s5k3p8sp",
	.chip_id = 0x3108,
	.modes = s5k3p8sp_modes,
	.num_modes = ARRAY_SIZE(s5k3p8sp_modes),
	.init_regs = s5k3p8sp_init,
	.num_init_regs = ARRAY_SIZE(s5k3p8sp_init),
};

static const struct reg_sequence s5k3l8_init[] = {
	{ 0xfcfc, 0x4000 },
	{ 0x6028, 0x2000 },
	{ 0x0100, 0x0000 },
	{ 0x0344, 0x0008 },
	{ 0x0348, 0x1077 },
	{ 0x0380, 0x0001 },
	{ 0x0382, 0x0001 },
	{ 0x0384, 0x0001 },
	{ 0x0136, 0x1800 },
	{ 0x0300, 0x0005 },
	{ 0x0302, 0x0001 },
	{ 0x0304, 0x0006 }, //?
	{ 0x030c, 0x0006 }, // MIPI divider, Default multiplier: 124
	{ 0x0200, 0x00c6 },
	{ 0x0204, 0x0020 },
	{ 0x0114, 0x0300 },
	{ 0x3052, 0x0000 }, //Disable heading embedded line(0x3051 on s5k2 and disabled by default), 
	
};

static const struct reg_sequence s5k3l8_mode_4208x3120_regs[] = {
	{0x6028, 0x4000},
	{0x0100, 0x0000},  
	{0x6028, 0x2000},  
	{0x602A, 0x0F74},
	{0x6F12, 0x0040}, 
	{0x6F12, 0x0040}, 
	{0x6028, 0x4000}, 
	{0x0344, 0x0008},
	{0x0346, 0x0008},  
	{0x0348, 0x1077}, 
	{0x034A, 0x0C37}, 
	{0x034C, 0x1070},
	{0x034E, 0x0C30},  
	{0x0900, 0x0011},  
	{0x0380, 0x0001}, 
	{0x0382, 0x0001},
	{0x0384, 0x0001},  
	{0x0386, 0x0001}, 
	{0x0400, 0x0000}, 
	{0x0404, 0x0010},
	{0x0114, 0x0300}, 
	{0x0110, 0x0002}, 
	{0x0136, 0x1800},
	{0x0304, 0x0006},
	{0x0306, 0x00B1},  
	{0x0302, 0x0001}, 
	{0x0300, 0x0005},  
	{0x030C, 0x0006},
	{0x030E, 0x0119},  
	{0x030A, 0x0001}, 
	{0x0308, 0x0008}, 
	{0x0342, 0x16B0},
	{0x0340, 0x0CB2},  
	{0x0202, 0x0200},  
	{0x0200, 0x00C6}, 
	{0x0B04, 0x0101},
	{0x0B08, 0x0000}, 
	{0x0B00, 0x0007},  
	{0x316A, 0x00A0},  
	{0x6028, 0x4000},
	{0x30C0, 0x0300},
};

static const struct reg_sequence s5k3l8_mode_1280x720_regs[] = {
	{0x6028, 0x4000},
	{0x0100, 0x0000},
	{0x6028, 0x2000},
	{0x602A, 0x0F74},
	{0x6F12, 0x0040},
	{0x6F12, 0x0040},
	{0x6028, 0x4000},
	{0x0344, 0x00C0},
	{0x0346, 0x01E8},
	{0x0348, 0x0FBF},
	{0x034A, 0x0A57},
	{0x034C, 0x0500},
	{0x034E, 0x02D0},
	{0x0900, 0x0113}, 
	{0x0380, 0x0001}, 
	{0x0382, 0x0001},
	{0x0384, 0x0001},
	{0x0386, 0x0005}, 
	{0x0400, 0x0001}, 
	{0x0404, 0x0030},
	{0x0114, 0x0300},
	{0x0110, 0x0002}, 
	{0x0136, 0x1800}, 
	{0x0304, 0x0006},
	{0x0306, 0x00B1},
	{0x0302, 0x0001}, 
	{0x0300, 0x0005}, 
	{0x030C, 0x0006},
	{0x030E, 0x0119},
	{0x030A, 0x0001}, 
	{0x0308, 0x0008}, 
	{0x0342, 0x16B0},
	{0x0340, 0x032C},
	{0x0202, 0x0200}, 
	{0x0200, 0x00C6}, 
	{0x0B04, 0x0101},
	{0x0B08, 0x0000},
	{0x0B00, 0x0007}, 
	{0x316A, 0x00A0}, 
	{0x6028, 0x4000},
	{0x30C0, 0x0300},
};

static const struct s5k2xx_mode s5k3l8_modes[] = {
	{
		.width = 4208,
		.height = 3120,
		.fps = 30,
		.fll_def = 3250,
		.fll_min = 3250,
		.llp = 5808,
		.link_freq_index = S5K2XX_LINK_FREQ_562MHZ_INDEX,
		.regs = s5k3l8_mode_4208x3120_regs,
		.num_regs = ARRAY_SIZE(s5k3l8_mode_4208x3120_regs),
	}, {
		.width = 1280,
		.height = 720,
		.fps = 120,
		.fll_def = 812,
		.fll_min = 812,
		.llp = 5808,
		.link_freq_index = S5K2XX_LINK_FREQ_562MHZ_INDEX,
		.regs = s5k3l8_mode_1280x720_regs,
		.num_regs = ARRAY_SIZE(s5k3l8_mode_1280x720_regs),
	}
};

static struct s5k2xx_data s5k3l8_data = {
	.model = "s5k3l8",
	.chip_id = 0x30c8,
	.modes = s5k3l8_modes,
	.num_modes = ARRAY_SIZE(s5k3l8_modes),
	.init_regs = s5k3l8_init,
	.num_init_regs = ARRAY_SIZE(s5k3l8_init),
};

struct s5k2xx {
	struct v4l2_subdev sd;
	struct regmap *rmap;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	struct regulator_bulk_data supplies[S5K2XX_NUM_SUPPLIES];
	struct clk	 *mclk;
	struct gpio_desc *reset_gpio;
	int enabled;

	unsigned int min_again;
	unsigned int max_again;

	/* Current mode */
	const struct s5k2xx_mode *cur_mode;
	const struct s5k2xx_data *data;

	/* To serialize asynchronus callbacks */
	struct mutex lock;

	/* Streaming on/off */
	bool streaming;
};

static int s5k2xx_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k2xx *s5k2xx = container_of(ctrl->handler,
					     struct s5k2xx, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&s5k2xx->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = s5k2xx->cur_mode->height + ctrl->val -
			       S5K2XX_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(s5k2xx->exposure,
					 s5k2xx->exposure->minimum,
					 exposure_max, s5k2xx->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_EXPOSURE,
				ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_ANALOG_GAIN,
				ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_DIG_GAIN,
				ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_FLL,
				s5k2xx->cur_mode->height + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_TEST_PATTERN,
				ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_TEST_PATTERN_RED,
				ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_TEST_PATTERN_GREENR,
				ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_TEST_PATTERN_BLUE,
				ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_TEST_PATTERN_GREENB,
				ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5k2xx_ctrl_ops = {
	.s_ctrl = s5k2xx_set_ctrl,
};

static s64 calc_pixel_rate(const struct s5k2xx_mode *mode)
{
	u64 pixel_rate = link_freq_menu_items[mode->link_freq_index] * 2 * S5K2XX_DATA_LANES;

	do_div(pixel_rate, S5K2XX_MAX_COLOR_DEPTH);

	return pixel_rate;
}

static int s5k2xx_init_controls(struct s5k2xx *s5k2xx)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k2xx->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	s64 exposure_max, h_blank;
	s64 rate_max = INT_MIN, rate_min = INT_MAX;
	int ret, i;

	ctrl_hdlr = &s5k2xx->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &s5k2xx->lock;

	for (i = 0; i < s5k2xx->data->num_modes; i++) {
		rate_max = max(rate_max, calc_pixel_rate(&s5k2xx->data->modes[i]));
		rate_min = min(rate_min, calc_pixel_rate(&s5k2xx->data->modes[i]));
	}

	s5k2xx->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &s5k2xx_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(link_freq_menu_items) - 1,
					0, link_freq_menu_items);
	if (s5k2xx->link_freq)
		s5k2xx->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s5k2xx->pixel_rate = v4l2_ctrl_new_std (ctrl_hdlr, &s5k2xx_ctrl_ops,
			V4L2_CID_PIXEL_RATE, rate_min, rate_max,
			1, calc_pixel_rate(s5k2xx->cur_mode));

	s5k2xx->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops,
					  V4L2_CID_VBLANK,
					  s5k2xx->cur_mode->fll_min -
					  s5k2xx->cur_mode->height,
					  S5K2XX_FLL_MAX -
					  s5k2xx->cur_mode->height, 1,
					  s5k2xx->cur_mode->fll_def -
					  s5k2xx->cur_mode->height);

	h_blank = s5k2xx->cur_mode->llp - s5k2xx->cur_mode->width;

	s5k2xx->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops,
					  V4L2_CID_HBLANK, h_blank, h_blank, 1,
					  h_blank);
	if (s5k2xx->hblank)
		s5k2xx->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  s5k2xx->min_again, s5k2xx->max_again,
			  S5K2XX_ANAL_GAIN_STEP, s5k2xx->min_again);
	v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  S5K2XX_DGTL_GAIN_MIN, S5K2XX_DGTL_GAIN_MAX,
			  S5K2XX_DGTL_GAIN_STEP, S5K2XX_DGTL_GAIN_DEFAULT);
	exposure_max = s5k2xx->cur_mode->fll_def - S5K2XX_EXPOSURE_MAX_MARGIN;
	s5k2xx->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    S5K2XX_EXPOSURE_MIN, exposure_max,
					    S5K2XX_EXPOSURE_STEP,
					    exposure_max);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &s5k2xx_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5k2xx_test_pattern_menu) - 1,
				     0, 0, s5k2xx_test_pattern_menu);

	for (i = 0; i < 4; i++) {
		v4l2_ctrl_new_std(ctrl_hdlr, &s5k2xx_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED + i,
				  0, S5K2XX_MAX_COLOR_VAL,
				  1, S5K2XX_MAX_COLOR_VAL);
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5k2xx_ctrl_ops,
					      &props);
	if (ret)
		return ret;

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	s5k2xx->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void s5k2xx_assign_pad_format(const struct s5k2xx_mode *mode,
				    struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int s5k2xx_start_streaming(struct s5k2xx *s5k2xx)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k2xx->sd);
	int ret;

	ret = regmap_multi_reg_write(s5k2xx->rmap, s5k2xx->data->init_regs,
						   s5k2xx->data->num_init_regs);
	if (ret) {
		dev_err(&client->dev, "failed to set plls: %d", ret);
		return ret;
	}

	ret = regmap_multi_reg_write(s5k2xx->rmap, s5k2xx->cur_mode->regs,
						   s5k2xx->cur_mode->num_regs);
	if (ret) {
		dev_err(&client->dev, "failed to set mode: %d", ret);
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(s5k2xx->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = regmap_write(s5k2xx->rmap, S5K2XX_REG_MODE_SELECT,
			      S5K2XX_MODE_STREAMING);

	if (ret) {
		dev_err(&client->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void s5k2xx_stop_streaming(struct s5k2xx *s5k2xx)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k2xx->sd);

	if (regmap_write(s5k2xx->rmap, S5K2XX_REG_MODE_SELECT,
			    S5K2XX_MODE_STANDBY))
		dev_err(&client->dev, "failed to set stream");
}

static int s5k2xx_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5k2xx *s5k2xx = to_s5k2(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (s5k2xx->streaming == enable)
		return 0;

	mutex_lock(&s5k2xx->lock);
	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			mutex_unlock(&s5k2xx->lock);
			return ret;
		}

		ret = s5k2xx_start_streaming(s5k2xx);
		if (ret) {
			enable = 0;
			s5k2xx_stop_streaming(s5k2xx);
			pm_runtime_put(&client->dev);
		}
	} else {
		s5k2xx_stop_streaming(s5k2xx);
		pm_runtime_put(&client->dev);
	}

	s5k2xx->streaming = enable;
	mutex_unlock(&s5k2xx->lock);

	return ret;
}

static int __maybe_unused s5k2xx_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	mutex_lock(&s5k2xx->lock);
	if (s5k2xx->streaming)
		s5k2xx_stop_streaming(s5k2xx);

	mutex_unlock(&s5k2xx->lock);

	return 0;
}

static int __maybe_unused s5k2xx_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k2xx *s5k2xx = to_s5k2(sd);
	int ret;

	mutex_lock(&s5k2xx->lock);
	if (s5k2xx->streaming) {
		ret = s5k2xx_start_streaming(s5k2xx);
		if (ret)
			goto error;
	}

	mutex_unlock(&s5k2xx->lock);

	return 0;

error:
	s5k2xx_stop_streaming(s5k2xx);
	s5k2xx->streaming = 0;
	mutex_unlock(&s5k2xx->lock);
	return ret;
}

static int s5k2xx_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k2xx *s5k2xx = to_s5k2(sd);
	int ret;

	if ((s5k2xx->enabled)++)
		return 0;

	ret = regulator_bulk_enable(S5K2XX_NUM_SUPPLIES,
				    s5k2xx->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	if (s5k2xx->mclk) {
		ret = clk_set_rate(s5k2xx->mclk, S5K2XX_MCLK);
		if (ret) {
			dev_err(dev, "can't set clock frequency");
			return ret;
		}
	}

	ret = clk_prepare_enable(s5k2xx->mclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(s5k2xx->reset_gpio, 0);

	usleep_range(10000, 11000);

	return 0;

reg_off:
	regulator_bulk_disable(S5K2XX_NUM_SUPPLIES, s5k2xx->supplies);

	return ret;
}

static int s5k2xx_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	if (--(s5k2xx->enabled) > 0)
		return 0;

	gpiod_set_value_cansleep(s5k2xx->reset_gpio, 1);

	regulator_bulk_disable(S5K2XX_NUM_SUPPLIES, s5k2xx->supplies);
	clk_disable_unprepare(s5k2xx->mclk);

	return 0;
}

static int s5k2xx_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct s5k2xx *s5k2xx = to_s5k2(sd);
	const struct s5k2xx_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(s5k2xx->data->modes,
				      s5k2xx->data->num_modes, width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&s5k2xx->lock);
	s5k2xx_assign_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		s5k2xx->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(s5k2xx->link_freq, mode->link_freq_index);

		__v4l2_ctrl_s_ctrl_int64(s5k2xx->pixel_rate,
					 calc_pixel_rate(mode));

		/* Update limits and set FPS to default */
		vblank_def = mode->fll_def - mode->height;
		__v4l2_ctrl_modify_range(s5k2xx->vblank,
					 mode->fll_min - mode->height,
					 S5K2XX_FLL_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(s5k2xx->vblank, vblank_def);

		h_blank = s5k2xx->cur_mode->llp - s5k2xx->cur_mode->width;

		__v4l2_ctrl_modify_range(s5k2xx->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&s5k2xx->lock);

	return 0;
}

static int s5k2xx_get_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	mutex_lock(&s5k2xx->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							  fmt->pad);
	else
		s5k2xx_assign_pad_format(s5k2xx->cur_mode, &fmt->format);

	mutex_unlock(&s5k2xx->lock);

	return 0;
}

static int s5k2xx_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int s5k2xx_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	if (fse->index >= s5k2xx->data->num_modes)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = s5k2xx->data->modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = s5k2xx->data->modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5k2xx_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	mutex_lock(&s5k2xx->lock);
	s5k2xx_assign_pad_format(&s5k2xx->data->modes[0],
				v4l2_subdev_state_get_format(fh->state, 0));
	mutex_unlock(&s5k2xx->lock);

	return 0;
}

static const struct v4l2_subdev_video_ops s5k2xx_video_ops = {
	.s_stream = s5k2xx_set_stream,
};

static const struct v4l2_subdev_pad_ops s5k2xx_pad_ops = {
	.set_fmt = s5k2xx_set_format,
	.get_fmt = s5k2xx_get_format,
	.enum_mbus_code = s5k2xx_enum_mbus_code,
	.enum_frame_size = s5k2xx_enum_frame_size,
};

static const struct v4l2_subdev_ops s5k2xx_subdev_ops = {
	.video = &s5k2xx_video_ops,
	.pad = &s5k2xx_pad_ops,
};

static const struct media_entity_operations s5k2xx_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops s5k2xx_internal_ops = {
	.open = s5k2xx_open,
};

static int s5k2xx_identify_module(struct s5k2xx *s5k2xx)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k2xx->sd);
	int ret;
	u32 val;

	ret = regmap_read(s5k2xx->rmap, S5K2XX_REG_CHIP_ID, &val);
	if (ret)
		return ret;

	if (val != s5k2xx->data->chip_id) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			s5k2xx->data->chip_id, val);
		return -ENXIO;
	}

	ret = regmap_read(s5k2xx->rmap, S5K2XX_REG_ANALOG_GAIN_MIN, &val);
	if (ret)
		return ret;

	s5k2xx->min_again = val;

	ret = regmap_read(s5k2xx->rmap, S5K2XX_REG_ANALOG_GAIN_MAX, &val);
	if (ret)
		return ret;

	s5k2xx->max_again = val;

	dev_info(&client->dev, "Analog gain: min=%u, max=%u", s5k2xx->min_again, val);

	return 0;
}

static int s5k2xx_check_hwcfg(struct s5k2xx *s5k2xx, struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 mclk;
	int ret = 0;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &mclk);
	if (ret) {
		dev_err(dev, "can't get clock frequency");
		return ret;
	}

	if (mclk != S5K2XX_MCLK) {
		dev_err(dev, "external clock %d is not supported", mclk);
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5K2XX_DATA_LANES) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
	}

	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static void s5k2xx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k2xx *s5k2xx = to_s5k2(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&s5k2xx->lock);
}

static int s5k2xx_of_init(struct s5k2xx *s5k2xx, struct device *dev)
{
	int i, ret;

	if (!dev->of_node)
		return 0;

	s5k2xx->data = (struct s5k2xx_data*) of_device_get_match_data(dev);

	for (i = 0; i < S5K2XX_NUM_SUPPLIES; i++)
		s5k2xx->supplies[i].supply = s5k2xx_supply_names[i];

	ret = devm_regulator_bulk_get(dev, S5K2XX_NUM_SUPPLIES, s5k2xx->supplies);
	if (ret < 0)
		return ret;

	s5k2xx->mclk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(s5k2xx->mclk))
		return PTR_ERR(s5k2xx->mclk);

	/* Request optional enable pin */
	s5k2xx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s5k2xx->reset_gpio))
		return PTR_ERR(s5k2xx->reset_gpio);

	return 0;
}

static const struct regmap_config s5k2xx_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.cache_type = REGCACHE_NONE,
};

static int s5k2xx_probe(struct i2c_client *client)
{
	struct s5k2xx *s5k2xx;
	int ret;

	s5k2xx = devm_kzalloc(&client->dev, sizeof(*s5k2xx), GFP_KERNEL);
	if (!s5k2xx)
		return -ENOMEM;

	ret = s5k2xx_of_init(s5k2xx, &client->dev);
	if (ret)
		return ret;

	ret = s5k2xx_check_hwcfg(s5k2xx, &client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	s5k2xx->rmap = devm_regmap_init_i2c(client, &s5k2xx_regmap_config);
	if (IS_ERR_OR_NULL(s5k2xx->rmap)) {
		dev_err(&client->dev, "failed to initialize regmap: %ld\n",
			PTR_ERR(s5k2xx->rmap));
		return PTR_ERR(s5k2xx->rmap) ?: -ENODATA;
	}

	v4l2_i2c_subdev_init(&s5k2xx->sd, client, &s5k2xx_subdev_ops);

	s5k2xx_power_on(&client->dev);

	ret = s5k2xx_identify_module(s5k2xx);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		goto power_off;
	}

	mutex_init(&s5k2xx->lock);
	s5k2xx->cur_mode = &s5k2xx->data->modes[0];
	ret = s5k2xx_init_controls(s5k2xx);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	snprintf(s5k2xx->sd.name, sizeof(s5k2xx->sd.name),
			"%s %s", s5k2xx->data->model, dev_name(&client->dev));

	s5k2xx->sd.internal_ops = &s5k2xx_internal_ops;
	s5k2xx->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k2xx->sd.entity.ops = &s5k2xx_subdev_entity_ops;
	s5k2xx->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5k2xx->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s5k2xx->sd.entity, 1, &s5k2xx->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&s5k2xx->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&s5k2xx->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(s5k2xx->sd.ctrl_handler);
	mutex_destroy(&s5k2xx->lock);

power_off:
	s5k2xx_power_off(&client->dev);

	return ret;
}

static const struct dev_pm_ops s5k2xx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(s5k2xx_suspend, s5k2xx_resume)
	SET_RUNTIME_PM_OPS(s5k2xx_power_off, s5k2xx_power_on, NULL)
};

static const struct of_device_id s5k2xx_of_match[] = {
	{ .compatible = "samsung,s5k3l8", &s5k3l8_data },
	{ .compatible = "samsung,s5k3p8sp", &s5k3p8sp_data },
	{ .compatible = "samsung,s5k2p6sx", &s5k2p6sx_data },
	{ .compatible = "samsung,s5k2x7sp", &s5k2x7sp_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k2xx_of_match);

static struct i2c_driver s5k2xx_i2c_driver = {
	.driver = {
		.name = "s5k2xx",
		.pm = &s5k2xx_pm_ops,
		.of_match_table	= of_match_ptr(s5k2xx_of_match),
	},
	.probe = s5k2xx_probe,
	.remove = s5k2xx_remove,
};

module_i2c_driver(s5k2xx_i2c_driver);

MODULE_DESCRIPTION("Samsung S5K2xx sensor driver");
MODULE_LICENSE("GPL v2");
