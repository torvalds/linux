// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define IMX355_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX355_MODE_STANDBY		0x00
#define IMX355_MODE_STREAMING		0x01

/* Chip ID */
#define IMX355_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX355_CHIP_ID			0x0355

#define IMX355_REG_LANE_SEL		CCI_REG8(0x0114)

/* PLL registers that depend on the external clock frequency */
#define IMX355_REG_EXTCLK_FREQ		CCI_REG16(0x0136)
#define IMX355_REG_PLL_OP_PREDIV	CCI_REG8(0x030d)
#define IMX355_REG_PLL_OP_MUL		CCI_REG16(0x030e)
#define IMX355_REG_PLL_IVT_PCK_DIV	CCI_REG8(0x0301)
#define IMX355_REG_PLL_IVT_SYSCK_DIV	CCI_REG8(0x0303)
#define IMX355_PLL_OP_PREDIV		2
#define IMX355_PLL_IVT_PCK_DIV		5

/* V_TIMING internal */
#define IMX355_REG_FLL			CCI_REG16(0x0340)
#define IMX355_FLL_MAX			0xffff
#define IMX355_VBLANK_MIN		20

#define IMX355_REG_LLP			CCI_REG16(0x0342)
#define IMX355_LLP_MAX			0xffff

#define IMX355_REG_X_ADD_START		CCI_REG16(0x0344)
#define IMX355_REG_Y_ADD_START		CCI_REG16(0x0346)
#define IMX355_REG_X_ADD_END		CCI_REG16(0x0348)
#define IMX355_REG_Y_ADD_END		CCI_REG16(0x034a)
#define IMX355_REG_X_OUT_SIZE		CCI_REG16(0x034c)
#define IMX355_REG_Y_OUT_SIZE		CCI_REG16(0x034e)

/* Exposure control */
#define IMX355_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX355_EXPOSURE_MIN		1
#define IMX355_EXPOSURE_STEP		1
#define IMX355_EXPOSURE_DEFAULT		0x0282
#define IMX355_EXPOSURE_OFFSET		10

/* Analog gain control */
#define IMX355_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define IMX355_ANA_GAIN_MIN		0
#define IMX355_ANA_GAIN_MAX		960
#define IMX355_ANA_GAIN_STEP		1
#define IMX355_ANA_GAIN_DEFAULT		0

/* Digital gain control */
#define IMX355_REG_DPGA_USE_GLOBAL_GAIN	CCI_REG8(0x3070)
#define IMX355_REG_DIG_GAIN_GLOBAL	CCI_REG16(0x020e)
#define IMX355_DGTL_GAIN_MIN		256
#define IMX355_DGTL_GAIN_MAX		4095
#define IMX355_DGTL_GAIN_STEP		1
#define IMX355_DGTL_GAIN_DEFAULT	256

/* Test Pattern Control */
#define IMX355_REG_TEST_PATTERN		CCI_REG16(0x0600)
#define IMX355_TEST_PATTERN_DISABLED		0
#define IMX355_TEST_PATTERN_SOLID_COLOR		1
#define IMX355_TEST_PATTERN_COLOR_BARS		2
#define IMX355_TEST_PATTERN_GRAY_COLOR_BARS	3
#define IMX355_TEST_PATTERN_PN9			4

#define IMX355_REG_REQ_LINK_BIT_RATE	CCI_REG16(0x0820)

#define IMX355_REG_BINNING_MODE		CCI_REG8(0x0900)
#define IMX355_REG_BINNING_TYPE		CCI_REG8(0x0901)
#define IMX355_REG_BINNING_WEIGHTING	CCI_REG8(0x0902)

/* Flip Control */
#define IMX355_REG_ORIENTATION		CCI_REG8(0x0101)

#define IMX355_PIXEL_ARRAY_TOP		0
#define IMX355_PIXEL_ARRAY_LEFT		0
#define IMX355_PIXEL_ARRAY_WIDTH	3280
#define IMX355_PIXEL_ARRAY_HEIGHT	2464

struct imx355_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/* Mode : resolution and related config&values */
struct imx355_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;
	struct v4l2_rect crop;

	/* V-timing */
	u32 fll_def;

	/* H-timing */
	u32 llp;

	/* Default register values */
	struct imx355_reg_list reg_list;
};

struct imx355_clk_params {
	u32 ext_clk;
	u16 extclk_freq;	/* External clock (MHz) in 8.8 fixed point) */
	u16 pll_op_mpy[2];	/* OP system PLL multiplier */
	u8 pll_op_prediv[2];	/* OP system pre PLL d */
};

/*
 * The clock tree is in single PLL mode, so PREDIV_VT and MPY_IVT do nothing.
 * In 4 lane mode the MIPI rate is 360Mhz (720Mbit/s) and pixel rate is
 * 288MPix/s.
 * In 2 lane mode the MIPI rate is 444MHz (888Mbit/s) and pixel rate
 * 177.6MPix/s with a 24MHz clock, and 441.6MHz (883.2Mbit/s) and 176.6MPix/s
 * with a 19.2MHz clock.
 */
static const struct imx355_clk_params imx355_clk_params[] = {
	{
		.ext_clk = 19200000,
		.extclk_freq = 0x1333,
		.pll_op_mpy = { 75, 92 },
		.pll_op_prediv = { 2, 2 }
	},
	{
		.ext_clk = 24000000,
		.extclk_freq = 0x1800,
		.pll_op_mpy = { 60, 111 },
		.pll_op_prediv = { 2, 3 }
	},
};

struct imx355_hwcfg {
	s64 link_freq_menu;
	unsigned long link_freq_bitmap;
	unsigned int num_lanes;
};

struct imx355 {
	struct device *dev;
	struct clk *clk;
	struct regmap *regmap;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	struct imx355_hwcfg *hwcfg;
	const struct imx355_clk_params *clk_params;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data imx355_supplies[] = {
	{ .supply = "avdd" },
	{ .supply = "dvdd" },
	{ .supply = "dovdd" },
};

static const struct cci_reg_sequence imx355_global_regs[] = {
	{ CCI_REG8(0x304e), 0x03 },
	{ CCI_REG8(0x4348), 0x16 },
	{ CCI_REG8(0x4350), 0x19 },
	{ CCI_REG8(0x4408), 0x0a },
	{ CCI_REG8(0x440c), 0x0b },
	{ CCI_REG8(0x4411), 0x5f },
	{ CCI_REG8(0x4412), 0x2c },
	{ CCI_REG8(0x4623), 0x00 },
	{ CCI_REG8(0x462c), 0x0f },
	{ CCI_REG8(0x462d), 0x00 },
	{ CCI_REG8(0x462e), 0x00 },
	{ CCI_REG8(0x4684), 0x54 },
	{ CCI_REG8(0x480a), 0x07 },
	{ CCI_REG8(0x4908), 0x07 },
	{ CCI_REG8(0x4909), 0x07 },
	{ CCI_REG8(0x490d), 0x0a },
	{ CCI_REG8(0x491e), 0x0f },
	{ CCI_REG8(0x4921), 0x06 },
	{ CCI_REG8(0x4923), 0x28 },
	{ CCI_REG8(0x4924), 0x28 },
	{ CCI_REG8(0x4925), 0x29 },
	{ CCI_REG8(0x4926), 0x29 },
	{ CCI_REG8(0x4927), 0x1f },
	{ CCI_REG8(0x4928), 0x20 },
	{ CCI_REG8(0x4929), 0x20 },
	{ CCI_REG8(0x492a), 0x20 },
	{ CCI_REG8(0x492c), 0x05 },
	{ CCI_REG8(0x492d), 0x06 },
	{ CCI_REG8(0x492e), 0x06 },
	{ CCI_REG8(0x492f), 0x06 },
	{ CCI_REG8(0x4930), 0x03 },
	{ CCI_REG8(0x4931), 0x04 },
	{ CCI_REG8(0x4932), 0x04 },
	{ CCI_REG8(0x4933), 0x05 },
	{ CCI_REG8(0x595e), 0x01 },
	{ CCI_REG8(0x5963), 0x01 },
	{ CCI_REG8(0x3030), 0x01 },
	{ CCI_REG8(0x3031), 0x01 },
	{ CCI_REG8(0x3045), 0x01 },
	{ CCI_REG8(0x4010), 0x00 },
	{ CCI_REG8(0x4011), 0x00 },
	{ CCI_REG8(0x4012), 0x00 },
	{ CCI_REG8(0x4013), 0x01 },
	{ CCI_REG8(0x68a8), 0xfe },
	{ CCI_REG8(0x68a9), 0xff },
	{ CCI_REG8(0x6888), 0x00 },
	{ CCI_REG8(0x6889), 0x00 },
	{ CCI_REG8(0x68b0), 0x00 },
	{ CCI_REG8(0x3058), 0x00 },
	{ CCI_REG8(0x305a), 0x00 },
	{ CCI_REG8(0x0112), 0x0a },
	{ CCI_REG8(0x0113), 0x0a },
	{ IMX355_REG_PLL_IVT_PCK_DIV, IMX355_PLL_IVT_PCK_DIV },
	{ CCI_REG8(0x0303), 0x01 },
	{ CCI_REG8(0x0305), 0x02 },
	{ CCI_REG8(0x0306), 0x00 },
	{ CCI_REG8(0x0307), 0x78 },
	{ CCI_REG8(0x030b), 0x01 },
	{ IMX355_REG_PLL_OP_PREDIV, IMX355_PLL_OP_PREDIV },
	{ CCI_REG8(0x0310), 0x00 },
	{ CCI_REG8(0x0220), 0x00 },
	{ CCI_REG8(0x0222), 0x01 },
	{ CCI_REG8(0x3088), 0x04 },
	{ CCI_REG8(0x6813), 0x02 },
	{ CCI_REG8(0x6835), 0x07 },
	{ CCI_REG8(0x6836), 0x01 },
	{ CCI_REG8(0x6837), 0x04 },
	{ CCI_REG8(0x684d), 0x07 },
	{ CCI_REG8(0x684e), 0x01 },
	{ CCI_REG8(0x684f), 0x04 },
};

static const struct cci_reg_sequence mode_3268x2448_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_3264x2448_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_3280x2464_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1940x1096_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1936x1096_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1924x1080_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1920x1080_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1640x1232_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1640x922_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1300x736_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1296x736_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1284x720_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_1280x720_regs[] = {
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x10 },
};

static const struct cci_reg_sequence mode_820x616_regs[] = {
	{ CCI_REG8(0x0700), 0x02 },
	{ CCI_REG8(0x0701), 0x78 },
};

static const char * const imx355_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

/* Mode configs */
static const struct imx355_mode supported_modes[] = {
	{
		.width = 3280,
		.height = 2464,
		.crop = {
			.width = 3280,
			.height = 2464,
			.left = 0,
			.top = 0,
		},
		.fll_def = 2615,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3280x2464_regs),
			.regs = mode_3280x2464_regs,
		},
	},
	{
		.width = 3268,
		.height = 2448,
		.crop = {
			.width = 3268,
			.height = 2448,
			.left = 8,
			.top = 8,
		},
		.fll_def = 2615,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3268x2448_regs),
			.regs = mode_3268x2448_regs,
		},
	},
	{
		.width = 3264,
		.height = 2448,
		.crop = {
			.width = 3264,
			.height = 2448,
			.left = 8,
			.top = 8,
		},
		.fll_def = 2615,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3264x2448_regs),
			.regs = mode_3264x2448_regs,
		},
	},
	{
		.width = 1940,
		.height = 1096,
		.crop = {
			.width = 1940,
			.height = 1096,
			.left = 672,
			.top = 684,
		},
		.fll_def = 1306,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1940x1096_regs),
			.regs = mode_1940x1096_regs,
		},
	},
	{
		.width = 1936,
		.height = 1096,
		.crop = {
			.width = 1936,
			.height = 1096,
			.left = 672,
			.top = 684,
		},
		.fll_def = 1306,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1936x1096_regs),
			.regs = mode_1936x1096_regs,
		},
	},
	{
		.width = 1924,
		.height = 1080,
		.crop = {
			.width = 1924,
			.height = 1080,
			.left = 680,
			.top = 692,
		},
		.fll_def = 1306,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1924x1080_regs),
			.regs = mode_1924x1080_regs,
		},
	},
	{
		.width = 1920,
		.height = 1080,
		.crop = {
			.width = 1920,
			.height = 1080,
			.left = 680,
			.top = 692,
		},
		.fll_def = 1306,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920x1080_regs),
			.regs = mode_1920x1080_regs,
		},
	},
	{
		.width = 1640,
		.height = 1232,
		.crop = {
			.width = 3280,
			.height = 2464,
			.left = 0,
			.top = 0,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1640x1232_regs),
			.regs = mode_1640x1232_regs,
		},
	},
	{
		.width = 1640,
		.height = 922,
		.crop = {
			.width = 3280,
			.height = 1844,
			.left = 0,
			.top = 304,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1640x922_regs),
			.regs = mode_1640x922_regs,
		},
	},
	{
		.width = 1300,
		.height = 736,
		.crop = {
			.width = 2600,
			.height = 1472,
			.left = 344,
			.top = 496,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1300x736_regs),
			.regs = mode_1300x736_regs,
		},
	},
	{
		.width = 1296,
		.height = 736,
		.crop = {
			.width = 2592,
			.height = 1472,
			.left = 344,
			.top = 496,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1296x736_regs),
			.regs = mode_1296x736_regs,
		},
	},
	{
		.width = 1284,
		.height = 720,
		.crop = {
			.width = 2568,
			.height = 1440,
			.left = 360,
			.top = 512,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1284x720_regs),
			.regs = mode_1284x720_regs,
		},
	},
	{
		.width = 1280,
		.height = 720,
		.crop = {
			.width = 2560,
			.height = 1440,
			.left = 360,
			.top = 512,
		},
		.fll_def = 1306,
		.llp = 1836,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_regs),
			.regs = mode_1280x720_regs,
		},
	},
	{
		.width = 820,
		.height = 616,
		.crop = {
			.width = 3280,
			.height = 2464,
			.left = 0,
			.top = 0,
		},
		.fll_def = 652,
		.llp = 3672,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_820x616_regs),
			.regs = mode_820x616_regs,
		},
	},
};

static inline struct imx355 *to_imx355(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx355, sd);
}

/* Get bayer order based on flip setting. */
static u32 imx355_get_format_code(struct imx355 *imx355)
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

	code = codes[imx355->vflip->val][imx355->hflip->val];

	return code;
}

static int imx355_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx355 *imx355 = container_of(ctrl->handler,
					     struct imx355, ctrl_handler);
	const struct v4l2_mbus_framefmt *format = NULL;
	struct v4l2_subdev_state *state;
	s64 max;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&imx355->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = format->height + ctrl->val - IMX355_EXPOSURE_OFFSET;
		__v4l2_ctrl_modify_range(imx355->exposure,
					 imx355->exposure->minimum,
					 max, imx355->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(imx355->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		/* Analog gain = 1024/(1024 - ctrl->val) times */
		ret = cci_write(imx355->regmap, IMX355_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(imx355->regmap, IMX355_REG_DIG_GAIN_GLOBAL,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(imx355->regmap, IMX355_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = cci_write(imx355->regmap, IMX355_REG_FLL,
				format->height + ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(imx355->regmap, IMX355_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = cci_write(imx355->regmap, IMX355_REG_ORIENTATION,
				imx355->hflip->val | imx355->vflip->val << 1,
				NULL);
		break;
	default:
		ret = -EINVAL;
		dev_info(imx355->dev, "ctrl(id:0x%x,val:0x%x) is not handled",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(imx355->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx355_ctrl_ops = {
	.s_ctrl = imx355_set_ctrl,
};

static int imx355_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx355 *imx355 = to_imx355(sd);

	if (code->index > 0)
		return -EINVAL;

	code->code = imx355_get_format_code(imx355);

	return 0;
}

static int imx355_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx355 *imx355 = to_imx355(sd);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != imx355_get_format_code(imx355)) {
		return -EINVAL;
	}

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx355_update_pad_format(struct imx355 *imx355,
				     const struct imx355_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = imx355_get_format_code(imx355);
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

static int
imx355_set_pad_format(struct v4l2_subdev *sd,
		      struct v4l2_subdev_state *sd_state,
		      struct v4l2_subdev_format *fmt)
{
	struct imx355 *imx355 = to_imx355(sd);
	const struct imx355_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	struct v4l2_rect *crop;
	s64 h_blank;

	/*
	 * Only one bayer order is supported.
	 * It depends on the flip settings.
	 */
	fmt->format.code = imx355_get_format_code(imx355);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	imx355_update_pad_format(imx355, mode, fmt);
	framefmt = v4l2_subdev_state_get_format(sd_state, 0);

	*framefmt = fmt->format;

	crop = v4l2_subdev_state_get_crop(sd_state, 0);
	crop->width = mode->crop.width;
	crop->height = mode->crop.height;
	crop->left = mode->crop.left;
	crop->top = mode->crop.top;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Update limits and set FPS to default */
		__v4l2_ctrl_modify_range(imx355->vblank, IMX355_VBLANK_MIN,
					 IMX355_FLL_MAX - mode->height, 1,
					 mode->fll_def - mode->height);
		__v4l2_ctrl_s_ctrl(imx355->vblank, mode->fll_def - mode->height);

		h_blank = mode->llp - mode->width;

		/*
		 * Currently hblank is not changeable.
		 * So FPS control is done only by vblank.
		 */
		__v4l2_ctrl_modify_range(imx355->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	return 0;
}

static int imx355_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, 0);
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = IMX355_PIXEL_ARRAY_TOP;
		sel->r.left = IMX355_PIXEL_ARRAY_LEFT;
		sel->r.width = IMX355_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX355_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int imx355_entity_init_state(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = { };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt.format.width = supported_modes[0].width;
	fmt.format.height = supported_modes[0].height;

	imx355_set_pad_format(subdev, sd_state, &fmt);

	return 0;
}

/* Start streaming */
static int imx355_start_streaming(struct imx355 *imx355)
{
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_state *state;
	const struct imx355_mode *mode;
	int lane_idx = imx355->hwcfg->num_lanes == 4 ? 0 : 1;
	struct v4l2_rect *crop;
	u64 link_bitrate;
	u8 binning_mode;
	int ret = 0;

	/* Global Setting */
	cci_multi_reg_write(imx355->regmap, imx355_global_regs,
			    ARRAY_SIZE(imx355_global_regs), &ret);

	/* Apply values of current mode */
	state = v4l2_subdev_get_locked_active_state(&imx355->sd);
	fmt = v4l2_subdev_state_get_format(state, 0);
	crop = v4l2_subdev_state_get_crop(state, 0);
	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height, fmt->width, fmt->height);
	cci_multi_reg_write(imx355->regmap, mode->reg_list.regs,
			    mode->reg_list.num_of_regs, &ret);

	/* Set readout crop and size registers  */
	cci_write(imx355->regmap, IMX355_REG_X_ADD_START, crop->left,
		  &ret);
	cci_write(imx355->regmap, IMX355_REG_Y_ADD_START, crop->top, &ret);
	cci_write(imx355->regmap, IMX355_REG_X_ADD_END,
		  crop->width + crop->left - 1, &ret);
	cci_write(imx355->regmap, IMX355_REG_Y_ADD_END,
		  crop->height + crop->top - 1, &ret);
	cci_write(imx355->regmap, IMX355_REG_X_OUT_SIZE, fmt->width, &ret);
	cci_write(imx355->regmap, IMX355_REG_Y_OUT_SIZE, fmt->height, &ret);

	binning_mode = ((crop->width / fmt->width) << 4) |
			(crop->height / fmt->height);
	cci_write(imx355->regmap, IMX355_REG_BINNING_MODE,
		  binning_mode == 0x11 ? 0x00 : 0x01, &ret);
	cci_write(imx355->regmap, IMX355_REG_BINNING_TYPE, binning_mode, &ret);
	cci_write(imx355->regmap, IMX355_REG_BINNING_WEIGHTING, 0x00, &ret);

	/* Set PLL registers for the external clock frequency */
	cci_write(imx355->regmap, IMX355_REG_EXTCLK_FREQ,
		  imx355->clk_params->extclk_freq, &ret);
	cci_write(imx355->regmap, IMX355_REG_PLL_OP_MUL,
		  imx355->clk_params->pll_op_mpy[lane_idx], &ret);
	cci_write(imx355->regmap, IMX355_REG_PLL_OP_PREDIV,
		  imx355->clk_params->pll_op_prediv[lane_idx], &ret);
	cci_write(imx355->regmap, IMX355_REG_PLL_IVT_SYSCK_DIV,
		  lane_idx ? 2 : 1, &ret);

	/* Set MIPI configuration */
	cci_write(imx355->regmap, IMX355_REG_LANE_SEL,
		  imx355->hwcfg->num_lanes - 1, &ret);

	link_bitrate = imx355->link_freq->qmenu_int[imx355->link_freq->val] *
		       imx355->hwcfg->num_lanes * 2;
	do_div(link_bitrate, 1000000);
	cci_write(imx355->regmap, IMX355_REG_REQ_LINK_BIT_RATE, link_bitrate,
		  &ret);

	/* set digital gain control to all color mode */
	cci_write(imx355->regmap, IMX355_REG_DPGA_USE_GLOBAL_GAIN, 1, &ret);

	/* set line length */
	cci_write(imx355->regmap, IMX355_REG_LLP,
		  imx355->hblank->val + fmt->width, &ret);

	/* Apply customized values from user */
	if (!ret)
		ret = __v4l2_ctrl_handler_setup(imx355->sd.ctrl_handler);

	cci_write(imx355->regmap, IMX355_REG_MODE_SELECT, IMX355_MODE_STREAMING,
		  &ret);

	return ret;
}

/* Stop streaming */
static int imx355_stop_streaming(struct imx355 *imx355)
{
	return cci_write(imx355->regmap, IMX355_REG_MODE_SELECT,
			 IMX355_MODE_STANDBY, NULL);
}

static int imx355_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx355 *imx355 = to_imx355(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx355->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx355_start_streaming(imx355);
		if (ret)
			goto err_rpm_put;
	} else {
		imx355_stop_streaming(imx355);
		pm_runtime_put_autosuspend(imx355->dev);
	}

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(imx355->vflip, enable);
	__v4l2_ctrl_grab(imx355->hflip, enable);

	v4l2_subdev_unlock_state(state);

	return ret;

err_rpm_put:
	pm_runtime_put_autosuspend(imx355->dev);
err_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

/* Verify chip ID */
static int imx355_identify_module(struct imx355 *imx355)
{
	int ret;
	u64 val;

	ret = cci_read(imx355->regmap, IMX355_REG_CHIP_ID, &val, NULL);
	if (ret)
		return ret;

	if (val != IMX355_CHIP_ID) {
		dev_err(imx355->dev, "chip id mismatch: %x!=%llx",
			IMX355_CHIP_ID, val);
		return -EIO;
	}
	return 0;
}

static const struct v4l2_subdev_core_ops imx355_subdev_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx355_video_ops = {
	.s_stream = imx355_set_stream,
};

static const struct v4l2_subdev_pad_ops imx355_pad_ops = {
	.enum_mbus_code = imx355_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx355_set_pad_format,
	.enum_frame_size = imx355_enum_frame_size,
	.get_selection = imx355_get_selection,
};

static const struct v4l2_subdev_ops imx355_subdev_ops = {
	.core = &imx355_subdev_core_ops,
	.video = &imx355_video_ops,
	.pad = &imx355_pad_ops,
};

static const struct media_entity_operations imx355_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops imx355_internal_ops = {
	.init_state = imx355_entity_init_state,
};

static int imx355_power_off(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx355 *imx355 = to_imx355(sd);

	gpiod_set_value_cansleep(imx355->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx355_supplies), imx355->supplies);
	clk_disable_unprepare(imx355->clk);

	return 0;
}

static int imx355_power_on(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx355 *imx355 = to_imx355(sd);
	int ret;

	ret = clk_prepare_enable(imx355->clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clocks");

	ret = regulator_bulk_enable(ARRAY_SIZE(imx355_supplies),
				    imx355->supplies);
	if (ret) {
		dev_err_probe(dev, ret, "failed to enable regulators");
		goto error_disable_clocks;
	}

	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(imx355->reset_gpio, 0);
	usleep_range(10000, 11000);

	return 0;

error_disable_clocks:
	clk_disable_unprepare(imx355->clk);
	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx355_pm_ops, imx355_power_off,
				 imx355_power_on, NULL);

/* Initialize control handlers */
static int imx355_init_controls(struct imx355 *imx355)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct imx355_mode *mode = &supported_modes[0];
	s64 exposure_max;
	s64 vblank_def;
	s64 hblank;
	u64 pixel_rate;
	int ret;

	ctrl_hdlr = &imx355->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	imx355->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx355_ctrl_ops,
						   V4L2_CID_LINK_FREQ, 0, 0,
						   &imx355->hwcfg->link_freq_menu);
	if (imx355->link_freq)
		imx355->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* pixel_rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = imx355->hwcfg->link_freq_menu * 2 * imx355->hwcfg->num_lanes;
	do_div(pixel_rate, 10);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);

	/* Initialize vblank/hblank/exposure parameters based on current mode */
	vblank_def = mode->fll_def - mode->height;
	imx355->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops,
					   V4L2_CID_VBLANK, IMX355_VBLANK_MIN,
					   IMX355_FLL_MAX - mode->height,
					   1, vblank_def);

	hblank = mode->llp - mode->width;
	imx355->hblank = v4l2_ctrl_new_std(ctrl_hdlr, NULL, V4L2_CID_HBLANK,
					   hblank, hblank, 1, hblank);
	if (imx355->hblank)
		imx355->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* fll >= exposure time + adjust parameter (default value is 10) */
	exposure_max = mode->fll_def - IMX355_EXPOSURE_OFFSET;
	imx355->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX355_EXPOSURE_MIN, exposure_max,
					     IMX355_EXPOSURE_STEP,
					     IMX355_EXPOSURE_DEFAULT);

	imx355->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx355->hflip)
		imx355->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	imx355->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx355->vflip)
		imx355->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX355_ANA_GAIN_MIN, IMX355_ANA_GAIN_MAX,
			  IMX355_ANA_GAIN_STEP, IMX355_ANA_GAIN_DEFAULT);

	/* Digital gain */
	v4l2_ctrl_new_std(ctrl_hdlr, &imx355_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX355_DGTL_GAIN_MIN, IMX355_DGTL_GAIN_MAX,
			  IMX355_DGTL_GAIN_STEP, IMX355_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx355_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx355_test_pattern_menu) - 1,
				     0, 0, imx355_test_pattern_menu);
	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(imx355->dev, "control init failed: %d", ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(imx355->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx355_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx355->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static struct imx355_hwcfg *imx355_get_hwcfg(struct imx355 *imx355)
{
	struct device *dev = imx355->dev;
	struct imx355_hwcfg *cfg;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	const struct imx355_clk_params *clk = imx355->clk_params;
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	int lane_idx;
	int ret;

	if (!fwnode)
		return NULL;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return NULL;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	if (ret)
		goto out_err;

	cfg = devm_kzalloc(dev, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		goto out_err;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2 &&
	    bus_cfg.bus.mipi_csi2.num_data_lanes != 4)
		goto out_err;

	cfg->num_lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;

	lane_idx = cfg->num_lanes == 4 ? 0 : 1;
	cfg->link_freq_menu = (clk->ext_clk * clk->pll_op_mpy[lane_idx]) /
			      (clk->pll_op_prediv[lane_idx] * 2);
	ret = v4l2_link_freq_to_bitmap(dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       &cfg->link_freq_menu, 1,
				       &cfg->link_freq_bitmap);
	if (ret)
		goto out_err;

	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(ep);
	return cfg;

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(ep);
	return NULL;
}

static int imx355_probe(struct i2c_client *client)
{
	struct imx355 *imx355;
	unsigned long freq;
	int ret;

	imx355 = devm_kzalloc(&client->dev, sizeof(*imx355), GFP_KERNEL);
	if (!imx355)
		return -ENOMEM;

	imx355->dev = &client->dev;

	imx355->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx355->regmap))
		return dev_err_probe(imx355->dev, PTR_ERR(imx355->regmap),
				     "Unable to initialize I2C\n");

	imx355->clk = devm_v4l2_sensor_clk_get(imx355->dev, NULL);
	if (IS_ERR(imx355->clk))
		return dev_err_probe(imx355->dev, PTR_ERR(imx355->clk),
				     "failed to get clock\n");

	freq = clk_get_rate(imx355->clk);
	for (unsigned int i = 0; i < ARRAY_SIZE(imx355_clk_params); i++) {
		if (freq == imx355_clk_params[i].ext_clk) {
			imx355->clk_params = &imx355_clk_params[i];
			break;
		}
	}
	if (!imx355->clk_params)
		return dev_err_probe(imx355->dev, -EINVAL,
				     "external clock %lu is not supported\n",
				     freq);

	ret = devm_regulator_bulk_get_const(imx355->dev,
					    ARRAY_SIZE(imx355_supplies),
					    imx355_supplies,
					    &imx355->supplies);
	if (ret) {
		dev_err_probe(imx355->dev, ret, "could not get regulators");
		return ret;
	}

	imx355->reset_gpio = devm_gpiod_get_optional(imx355->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(imx355->reset_gpio)) {
		ret = dev_err_probe(imx355->dev, PTR_ERR(imx355->reset_gpio),
				    "failed to get gpios");
		return ret;
	}

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx355->sd, client, &imx355_subdev_ops);

	imx355->hwcfg = imx355_get_hwcfg(imx355);
	if (!imx355->hwcfg) {
		dev_err(imx355->dev, "failed to get hwcfg");
		return -ENODEV;
	}

	ret = imx355_power_on(imx355->dev);
	if (ret)
		return ret;

	/* Check module identity */
	ret = imx355_identify_module(imx355);
	if (ret) {
		dev_err(imx355->dev, "failed to find sensor: %d", ret);
		goto error_power_off;
	}

	ret = imx355_init_controls(imx355);
	if (ret) {
		dev_err(imx355->dev, "failed to init controls: %d", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx355->sd.internal_ops = &imx355_internal_ops;
	imx355->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		V4L2_SUBDEV_FL_HAS_EVENTS;
	imx355->sd.entity.ops = &imx355_subdev_entity_ops;
	imx355->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx355->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx355->sd.entity, 1, &imx355->pad);
	if (ret) {
		dev_err(imx355->dev, "failed to init entity pads: %d", ret);
		goto error_handler_free;
	}

	imx355->sd.state_lock = imx355->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx355->sd);
	if (ret < 0) {
		dev_err_probe(imx355->dev, ret, "subdev init error\n");
		goto error_media_entity_free;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(imx355->dev);
	pm_runtime_enable(imx355->dev);
	pm_runtime_set_autosuspend_delay(imx355->dev, 1000);
	pm_runtime_use_autosuspend(imx355->dev);

	ret = v4l2_async_register_subdev_sensor(&imx355->sd);
	if (ret < 0)
		goto error_subdev_cleanup_runtime_pm;

	pm_runtime_idle(imx355->dev);

	return 0;

error_subdev_cleanup_runtime_pm:
	pm_runtime_disable(imx355->dev);
	pm_runtime_set_suspended(imx355->dev);
	pm_runtime_dont_use_autosuspend(imx355->dev);
	v4l2_subdev_cleanup(&imx355->sd);
error_media_entity_free:
	media_entity_cleanup(&imx355->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx355->sd.ctrl_handler);

error_power_off:
	imx355_power_off(imx355->dev);

	return ret;
}

static void imx355_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx355 *imx355 = to_imx355(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(imx355->dev);

	if (!pm_runtime_status_suspended(imx355->dev)) {
		imx355_power_off(imx355->dev);
		pm_runtime_set_suspended(imx355->dev);
	}

	pm_runtime_dont_use_autosuspend(imx355->dev);
}

static const struct acpi_device_id imx355_acpi_ids[] __maybe_unused = {
	{ "SONY355A" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, imx355_acpi_ids);

static const struct of_device_id imx355_match_table[] = {
	{ .compatible = "sony,imx355", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx355_match_table);

static struct i2c_driver imx355_i2c_driver = {
	.driver = {
		.name = "imx355",
		.acpi_match_table = ACPI_PTR(imx355_acpi_ids),
		.of_match_table = imx355_match_table,
		.pm = &imx355_pm_ops,
	},
	.probe = imx355_probe,
	.remove = imx355_remove,
};
module_i2c_driver(imx355_i2c_driver);

MODULE_AUTHOR("Qiu, Tianshu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Rapolu, Chiranjeevi");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Yang, Hyungwoo");
MODULE_DESCRIPTION("Sony imx355 sensor driver");
MODULE_LICENSE("GPL v2");
