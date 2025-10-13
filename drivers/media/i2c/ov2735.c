// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Support for the OV2735
 *
 * Copyright (C) 2025 Silicon Signals Pvt. Ltd.
 *
 * Based on Rockchip ov2735 Camera Driver
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Inspired from ov8858, imx219, imx283 camera drivers.
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>
#include <linux/types.h>
#include <linux/time.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#define OV2735_XCLK_FREQ			(24 * HZ_PER_MHZ)

/* Add page number in CCI private bits [31:28] of the register address */
#define OV2735_PAGE_REG8(p, x)	(((p) << CCI_REG_PRIVATE_SHIFT) | CCI_REG8(x))
#define OV2735_PAGE_REG16(p, x)	(((p) << CCI_REG_PRIVATE_SHIFT) | CCI_REG16(x))

#define OV2735_REG_PAGE_SELECT			CCI_REG8(0xfd)

/* Page 0 */
#define OV2735_REG_CHIPID			OV2735_PAGE_REG16(0x00, 0x02)
#define OV2735_CHIPID				0x2735

#define OV2735_REG_SOFT_RESET			OV2735_PAGE_REG8(0x00, 0x20)

/* Clock Settings */
#define OV2735_REG_PLL_CTRL			OV2735_PAGE_REG8(0x00, 0x2f)
#define OV2735_PLL_CTRL_ENABLE			0x7f
#define OV2735_REG_PLL_OUTDIV			OV2735_PAGE_REG8(0x00, 0x34)
#define OV2735_REG_CLK_MODE			OV2735_PAGE_REG8(0x00, 0x30)
#define OV2735_REG_CLOCK_REG1			OV2735_PAGE_REG8(0x00, 0x33)
#define OV2735_REG_CLOCK_REG2			OV2735_PAGE_REG8(0x00, 0x35)

/* Page 1 */
#define OV2735_REG_STREAM_CTRL			OV2735_PAGE_REG8(0x01, 0xa0)
#define OV2735_STREAM_CTRL_ON			0x01
#define OV2735_STREAM_CTRL_OFF			0x00

#define OV2735_REG_UPDOWN_MIRROR		OV2735_PAGE_REG8(0x01, 0x3f)
#define OV2735_REG_BINNING_DAC_CODE_MODE	OV2735_PAGE_REG8(0x01, 0x30)
#define OV2735_REG_FRAME_LENGTH			OV2735_PAGE_REG16(0x01, 0x0e)
#define OV2735_FRAME_LENGTH_MAX			0x0fff
#define OV2735_REG_FRAME_EXP_SEPERATE_EN	OV2735_PAGE_REG8(0x01, 0x0d)
#define OV2735_FRAME_EXP_SEPERATE_EN		0x10
#define OV2735_REG_FRAME_SYNC			OV2735_PAGE_REG8(0x01, 0x01)

#define OV2735_REG_HBLANK			OV2735_PAGE_REG16(0x01, 0x09)

#define OV2735_REG_HS_MIPI			OV2735_PAGE_REG8(0x01, 0xb1)
#define OV2735_REG_MIPI_CTRL1			OV2735_PAGE_REG8(0x01, 0x92)
#define OV2735_REG_MIPI_CTRL2			OV2735_PAGE_REG8(0x01, 0x94)
#define OV2735_REG_MIPI_CTRL3			OV2735_PAGE_REG8(0x01, 0xa1)
#define OV2735_REG_MIPI_CTRL4			OV2735_PAGE_REG8(0x01, 0xb2)
#define OV2735_REG_MIPI_CTRL5			OV2735_PAGE_REG8(0x01, 0xb3)
#define OV2735_REG_MIPI_CTRL6			OV2735_PAGE_REG8(0x01, 0xb4)
#define OV2735_REG_MIPI_CTRL7			OV2735_PAGE_REG8(0x01, 0xb5)
#define OV2735_REG_HIGH_SPEED			OV2735_PAGE_REG8(0x01, 0x9d)
#define OV2735_REG_PREPARE			OV2735_PAGE_REG8(0x01, 0x95)
#define OV2735_REG_R_HS_ZERO			OV2735_PAGE_REG8(0x01, 0x96)
#define OV2735_REG_TRAIL			OV2735_PAGE_REG8(0x01, 0x98)
#define OV2735_REG_R_CLK_ZERO			OV2735_PAGE_REG8(0x01, 0x9c)
#define OV2735_REG_MIPI_COLOMN_NUMBER		OV2735_PAGE_REG16(0x01, 0x8e)
#define OV2735_REG_MIPI_LINE_NUMBER		OV2735_PAGE_REG16(0x01, 0x90)

/* Timing control registers */
#define OV2735_REG_TIMING_CTRL2			OV2735_PAGE_REG8(0x01, 0x1a)
#define OV2735_REG_TIMING_CTRL3			OV2735_PAGE_REG8(0x01, 0x1c)
#define OV2735_REG_TIMING_CTRL1			OV2735_PAGE_REG8(0x01, 0x16)
#define OV2735_REG_RST_NUM			OV2735_PAGE_REG16(0x01, 0x10)
#define OV2735_REG_RST_NUM2			OV2735_PAGE_REG16(0x01, 0x32)
#define OV2735_REG_BOOST_EN			OV2735_PAGE_REG8(0x01, 0xd0)
#define OV2735_REG_B2_NUM			OV2735_PAGE_REG16(0x01, 0xd1)
#define OV2735_REG_B4_NUM			OV2735_PAGE_REG16(0x01, 0xd3)
#define OV2735_REG_PIXEL_CYCLE_P0		OV2735_PAGE_REG8(0x01, 0x50)
#define OV2735_REG_PIXEL_CYCLE_P1		OV2735_PAGE_REG8(0x01, 0x51)
#define OV2735_REG_PIXEL_CYCLE_P2		OV2735_PAGE_REG8(0x01, 0x52)
#define OV2735_REG_PIXEL_CYCLE_P3		OV2735_PAGE_REG8(0x01, 0x53)
#define OV2735_REG_PIXEL_CYCLE_P5		OV2735_PAGE_REG8(0x01, 0x55)
#define OV2735_REG_PIXEL_CYCLE_P7		OV2735_PAGE_REG16(0x01, 0x57)
#define OV2735_REG_PIXEL_CYCLE_P9		OV2735_PAGE_REG8(0x01, 0x5a)
#define OV2735_REG_PIXEL_CYCLE_P10		OV2735_PAGE_REG8(0x01, 0x5b)
#define OV2735_REG_PIXEL_CYCLE_P12		OV2735_PAGE_REG8(0x01, 0x5d)
#define OV2735_REG_PIXEL_CYCLE_P18		OV2735_PAGE_REG8(0x01, 0x64)
#define OV2735_REG_PIXEL_CYCLE_P20		OV2735_PAGE_REG8(0x01, 0x66)
#define OV2735_REG_PIXEL_CYCLE_P22		OV2735_PAGE_REG8(0x01, 0x68)
#define OV2735_REG_PIXEL_CYCLE_P33		OV2735_PAGE_REG16(0x01, 0x74)
#define OV2735_REG_PIXEL_CYCLE_P34		OV2735_PAGE_REG8(0x01, 0x76)
#define OV2735_REG_PIXEL_CYCLE_P35_P36		OV2735_PAGE_REG8(0x01, 0x77)
#define OV2735_REG_PIXEL_CYCLE_P37_P38		OV2735_PAGE_REG8(0x01, 0x78)
#define OV2735_REG_PIXEL_CYCLE_P31		OV2735_PAGE_REG8(0x01, 0x72)
#define OV2735_REG_PIXEL_CYCLE_P32		OV2735_PAGE_REG8(0x01, 0x73)
#define OV2735_REG_PIXEL_CYCLE_P44		OV2735_PAGE_REG8(0x01, 0x7d)
#define OV2735_REG_PIXEL_CYCLE_P45		OV2735_PAGE_REG8(0x01, 0x7e)
#define OV2735_REG_PIXEL_BIAS_CTRL_RH_RL	OV2735_PAGE_REG8(0x01, 0x8a)
#define OV2735_REG_PIXEL_BIAS_CTRL_SH_SL	OV2735_PAGE_REG8(0x01, 0x8b)

/* Analog Control registers */
#define OV2735_REG_ICOMP			OV2735_PAGE_REG8(0x01, 0x19)
#define OV2735_REG_PCP_RST_SEL			OV2735_PAGE_REG8(0x01, 0x21)
#define OV2735_REG_VNCP				OV2735_PAGE_REG8(0x01, 0x20)
#define OV2735_REG_ANALOG_CTRL3			OV2735_PAGE_REG8(0x01, 0x25)
#define OV2735_REG_ANALOG_CTRL4			OV2735_PAGE_REG8(0x01, 0x26)
#define OV2735_REG_ANALOG_CTRL5			OV2735_PAGE_REG8(0x01, 0x29)
#define OV2735_REG_ANALOG_CTRL6			OV2735_PAGE_REG8(0x01, 0x2a)
#define OV2735_REG_ANALOG_CTRL8			OV2735_PAGE_REG8(0x01, 0x2c)

/* BLC registers */
#define OV2735_REG_BLC_GAIN_BLUE		OV2735_PAGE_REG8(0x01, 0x86)
#define OV2735_REG_BLC_GAIN_RED			OV2735_PAGE_REG8(0x01, 0x87)
#define OV2735_REG_BLC_GAIN_GR			OV2735_PAGE_REG8(0x01, 0x88)
#define OV2735_REG_BLC_GAIN_GB			OV2735_PAGE_REG8(0x01, 0x89)
#define OV2735_REG_GB_SUBOFFSET			OV2735_PAGE_REG8(0x01, 0xf0)
#define OV2735_REG_BLUE_SUBOFFSET		OV2735_PAGE_REG8(0x01, 0xf1)
#define OV2735_REG_RED_SUBOFFSET		OV2735_PAGE_REG8(0x01, 0xf2)
#define OV2735_REG_GR_SUBOFFSET			OV2735_PAGE_REG8(0x01, 0xf3)
#define OV2735_REG_BLC_BPC_TH_P			OV2735_PAGE_REG8(0x01, 0xfc)
#define OV2735_REG_BLC_BPC_TH_N			OV2735_PAGE_REG8(0x01, 0xfe)
#define OV2735_REG_ABL				OV2735_PAGE_REG8(0x01, 0xfb)

#define OV2735_REG_TEST_PATTERN			OV2735_PAGE_REG8(0x01, 0xb2)
#define OV2735_TEST_PATTERN_ENABLE		0x01
#define OV2735_TEST_PATTERN_DISABLE		0xfe

#define OV2735_REG_LONG_EXPOSURE		OV2735_PAGE_REG16(0x01, 0x03)
#define OV2735_EXPOSURE_MIN			4
#define OV2735_EXPOSURE_STEP			1
#define OV2735_EXPOSURE_MARGIN			4

#define OV2735_REG_ANALOG_GAIN			OV2735_PAGE_REG8(0x01, 0x24)
#define OV2735_ANALOG_GAIN_MIN			0x10
#define OV2735_ANALOG_GAIN_MAX			0xff
#define OV2735_ANALOG_GAIN_STEP			1
#define OV2735_ANALOG_GAIN_DEFAULT		0x10

/* Page 2 */
#define OV2735_REG_V_START			OV2735_PAGE_REG16(0x02, 0xa0)
#define OV2735_REG_V_SIZE			OV2735_PAGE_REG16(0x02, 0xa2)
#define OV2735_REG_H_START			OV2735_PAGE_REG16(0x02, 0xa4)
#define OV2735_REG_H_SIZE			OV2735_PAGE_REG16(0x02, 0xa6)

#define OV2735_LINK_FREQ_420MHZ			(420 * HZ_PER_MHZ)
#define OV2735_PIXEL_RATE			(168 * HZ_PER_MHZ)

/* OV2735 native and active pixel array size */
static const struct v4l2_rect ov2735_native_area = {
	.top = 0,
	.left = 0,
	.width = 1936,
	.height = 1096,
};

static const struct v4l2_rect ov2735_active_area = {
	.top = 8,
	.left = 8,
	.width = 1920,
	.height = 1080,
};

static const char * const ov2735_supply_name[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

/* PLL_OUT = [PLL_IN * (pll_nc +3)] / [(pll_mc + 1) * (pll_outdiv + 1)] */
struct ov2735_pll_parameters {
	u8 pll_nc;
	u8 pll_mc;
	u8 pll_outdiv;
};

struct ov2735 {
	struct device *dev;
	struct regmap *cci;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov2735_supply_name)];

	/* V4L2 Controls */
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *test_pattern;

	u32 link_freq_index;

	u8 current_page;
	struct mutex page_lock;
};

struct ov2735_mode {
	u32 width;
	u32 height;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	struct v4l2_rect crop;
};

static const struct cci_reg_sequence ov2735_common_regs[] = {
	{ OV2735_REG_CLK_MODE,			0x15 },
	{ OV2735_REG_CLOCK_REG1,		0x01 },
	{ OV2735_REG_CLOCK_REG2,		0x20 },
	{ OV2735_REG_BINNING_DAC_CODE_MODE,	0x00 },
	{ OV2735_REG_ABL,			0x73 },
	{ OV2735_REG_FRAME_SYNC,		0x01 },

	/* Timing ctrl */
	{ OV2735_REG_TIMING_CTRL2,		0x6b },
	{ OV2735_REG_TIMING_CTRL3,		0xea },
	{ OV2735_REG_TIMING_CTRL1,		0x0c },
	{ OV2735_REG_RST_NUM,			0x0063 },
	{ OV2735_REG_RST_NUM2,			0x006f },
	{ OV2735_REG_BOOST_EN,			0x02 },
	{ OV2735_REG_B2_NUM,			0x0120 },
	{ OV2735_REG_B4_NUM,			0x042a },
	{ OV2735_REG_PIXEL_CYCLE_P0,		0x00 },
	{ OV2735_REG_PIXEL_CYCLE_P1,		0x2c },
	{ OV2735_REG_PIXEL_CYCLE_P2,		0x29 },
	{ OV2735_REG_PIXEL_CYCLE_P3,		0x00 },
	{ OV2735_REG_PIXEL_CYCLE_P5,		0x44 },
	{ OV2735_REG_PIXEL_CYCLE_P7,		0x0029 },
	{ OV2735_REG_PIXEL_CYCLE_P9,		0x00 },
	{ OV2735_REG_PIXEL_CYCLE_P10,		0x00 },
	{ OV2735_REG_PIXEL_CYCLE_P12,		0x00 },
	{ OV2735_REG_PIXEL_CYCLE_P18,		0x2f },
	{ OV2735_REG_PIXEL_CYCLE_P20,		0x62 },
	{ OV2735_REG_PIXEL_CYCLE_P22,		0x5b },
	{ OV2735_REG_PIXEL_CYCLE_P33,		0x0046 },
	{ OV2735_REG_PIXEL_CYCLE_P34,		0x36 },
	{ OV2735_REG_PIXEL_CYCLE_P35_P36,	0x4f },
	{ OV2735_REG_PIXEL_CYCLE_P37_P38,	0xef },
	{ OV2735_REG_PIXEL_CYCLE_P31,		0xcf },
	{ OV2735_REG_PIXEL_CYCLE_P32,		0x36 },
	{ OV2735_REG_PIXEL_CYCLE_P44,		0x0d },
	{ OV2735_REG_PIXEL_CYCLE_P45,		0x0d },
	{ OV2735_REG_PIXEL_BIAS_CTRL_RH_RL,	0x77 },
	{ OV2735_REG_PIXEL_BIAS_CTRL_SH_SL,	0x77 },

	/* Analog ctrl */
	{ OV2735_REG_ANALOG_CTRL4,		0x5a },
	{ OV2735_REG_ANALOG_CTRL5,		0x01 },
	{ OV2735_REG_ANALOG_CTRL6,		0xd2 },
	{ OV2735_REG_ANALOG_CTRL8,		0x40 },
	{ OV2735_REG_PCP_RST_SEL,		0x00 },
	{ OV2735_REG_ICOMP,			0xc3 },

	{ OV2735_REG_HS_MIPI,			0x83 },
	{ OV2735_REG_MIPI_CTRL5,		0x0b },
	{ OV2735_REG_MIPI_CTRL6,		0x14 },
	{ OV2735_REG_HIGH_SPEED,		0x40 },
	{ OV2735_REG_MIPI_CTRL3,		0x05 },
	{ OV2735_REG_MIPI_CTRL2,		0x44 },
	{ OV2735_REG_PREPARE,			0x33 },
	{ OV2735_REG_R_HS_ZERO,			0x1f },
	{ OV2735_REG_TRAIL,			0x45 },
	{ OV2735_REG_R_CLK_ZERO,		0x10 },
	{ OV2735_REG_MIPI_CTRL7,		0x70 },
	{ OV2735_REG_ANALOG_CTRL3,		0xe0 },
	{ OV2735_REG_VNCP,			0x7b },

	/* BLC */
	{ OV2735_REG_BLC_GAIN_BLUE,		0x77 },
	{ OV2735_REG_BLC_GAIN_GB,		0x77 },
	{ OV2735_REG_BLC_GAIN_RED,		0x74 },
	{ OV2735_REG_BLC_GAIN_GR,		0x74 },
	{ OV2735_REG_BLC_BPC_TH_P,		0xe0 },
	{ OV2735_REG_BLC_BPC_TH_N,		0xe0 },
	{ OV2735_REG_GB_SUBOFFSET,		0x40 },
	{ OV2735_REG_BLUE_SUBOFFSET,		0x40 },
	{ OV2735_REG_RED_SUBOFFSET,		0x40 },
	{ OV2735_REG_GR_SUBOFFSET,		0x40 },
};

static const struct ov2735_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.exp_def = 399,
		.hts_def = 2200,
		.vts_def = 2545,
		.crop = {
			.top = 8,
			.left = 8,
			.width = 1920,
			.height = 1080,
		},
	},
};

static const s64 link_freq_menu_items[] = {
	OV2735_LINK_FREQ_420MHZ,
};

static const struct ov2735_pll_parameters pll_configs[] = {
	/* For 420MHz pll_configs */
	{
		.pll_nc = 4,
		.pll_mc = 0,
		.pll_outdiv = 1,
	},
};

static const char * const ov2735_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color",
};

static int ov2735_page_access(struct ov2735 *ov2735, u32 reg, int *err)
{
	u8 page = reg >> CCI_REG_PRIVATE_SHIFT;
	int ret = 0;

	if (err && *err)
		return *err;

	guard(mutex)(&ov2735->page_lock);

	/* Perform page access before read/write */
	if (ov2735->current_page == page)
		return ret;

	ret = cci_write(ov2735->cci, OV2735_REG_PAGE_SELECT, page, err);
	if (!ret)
		ov2735->current_page = page;

	return ret;
}

static int ov2735_read(struct ov2735 *ov2735, u32 reg, u64 *val, int *err)
{
	u32 addr = reg & ~CCI_REG_PRIVATE_MASK;
	int ret;

	ret = ov2735_page_access(ov2735, reg, err);
	if (ret)
		return ret;

	return cci_read(ov2735->cci, addr, val, err);
}

static int ov2735_write(struct ov2735 *ov2735, u32 reg, u64 val, int *err)
{
	u32 addr = reg & ~CCI_REG_PRIVATE_MASK;
	int ret;

	ret = ov2735_page_access(ov2735, reg, err);
	if (ret)
		return ret;

	return cci_write(ov2735->cci, addr, val, err);
}

static int ov2735_multi_reg_write(struct ov2735 *ov2735,
				  const struct cci_reg_sequence *regs,
				  unsigned int num_regs, int *err)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_regs; i++) {
		ret = ov2735_write(ov2735, regs[i].reg, regs[i].val, err);
		if (ret)
			return ret;
	}

	return 0;
}

static inline struct ov2735 *to_ov2735(struct v4l2_subdev *_sd)
{
	return container_of_const(_sd, struct ov2735, sd);
}

static int ov2735_enable_test_pattern(struct ov2735 *ov2735, u32 pattern)
{
	int ret;
	u64 val;

	ret = ov2735_read(ov2735, OV2735_REG_TEST_PATTERN, &val, NULL);
	if (ret)
		return ret;

	switch (pattern) {
	case 0:
		val &= ~OV2735_TEST_PATTERN_ENABLE;
		break;
	case 1:
		val |= OV2735_TEST_PATTERN_ENABLE;
		break;
	}

	return ov2735_write(ov2735, OV2735_REG_TEST_PATTERN, val, NULL);
}

static int ov2735_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2735 *ov2735 =
		container_of_const(ctrl->handler, struct ov2735, handler);
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_state *state;
	u64 vts;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&ov2735->sd);
	fmt = v4l2_subdev_state_get_format(state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Honour the VBLANK limits when setting exposure */
		s64 max = fmt->height + ctrl->val - OV2735_EXPOSURE_MARGIN;

		ret = __v4l2_ctrl_modify_range(ov2735->exposure,
					       ov2735->exposure->minimum, max,
					       ov2735->exposure->step,
					       ov2735->exposure->default_value);
		if (ret)
			return ret;
	}

	if (pm_runtime_get_if_in_use(ov2735->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ov2735_write(ov2735, OV2735_REG_LONG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ov2735_write(ov2735, OV2735_REG_ANALOG_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_HBLANK:
		ov2735_write(ov2735, OV2735_REG_HBLANK, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + fmt->height;
		ov2735_write(ov2735, OV2735_REG_FRAME_EXP_SEPERATE_EN,
			     OV2735_FRAME_EXP_SEPERATE_EN, &ret);
		ov2735_write(ov2735, OV2735_REG_FRAME_LENGTH, vts, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov2735_enable_test_pattern(ov2735, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	ov2735_write(ov2735, OV2735_REG_FRAME_SYNC, 0x01, &ret);

	pm_runtime_put(ov2735->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov2735_ctrl_ops = {
	.s_ctrl = ov2735_set_ctrl,
};

static int ov2735_init_controls(struct ov2735 *ov2735)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	const struct ov2735_mode *mode = &supported_modes[0];
	u64 hblank_def, vblank_def, exp_max;
	int ret;

	ctrl_hdlr = &ov2735->handler;
	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	ov2735->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov2735_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 0,
					       OV2735_PIXEL_RATE, 1,
					       OV2735_PIXEL_RATE);

	ov2735->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov2735_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ov2735->link_freq_index,
						   0, link_freq_menu_items);
	if (ov2735->link_freq)
		ov2735->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	hblank_def = mode->hts_def - mode->width;
	ov2735->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov2735_ctrl_ops,
					   V4L2_CID_HBLANK, hblank_def,
					   hblank_def, 1, hblank_def);

	vblank_def = mode->vts_def - mode->height;
	ov2735->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov2735_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   OV2735_FRAME_LENGTH_MAX - mode->height,
					   1, vblank_def);

	exp_max = mode->vts_def - OV2735_EXPOSURE_MARGIN;
	ov2735->exposure =
		v4l2_ctrl_new_std(ctrl_hdlr, &ov2735_ctrl_ops,
				  V4L2_CID_EXPOSURE,
				  OV2735_EXPOSURE_MIN, exp_max,
				  OV2735_EXPOSURE_STEP, mode->exp_def);

	ov2735->gain =
		v4l2_ctrl_new_std(ctrl_hdlr, &ov2735_ctrl_ops,
				  V4L2_CID_ANALOGUE_GAIN, OV2735_ANALOG_GAIN_MIN,
				  OV2735_ANALOG_GAIN_MAX, OV2735_ANALOG_GAIN_STEP,
				  OV2735_ANALOG_GAIN_DEFAULT);

	ov2735->test_pattern =
		v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov2735_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(ov2735_test_pattern_menu) - 1,
					     0, 0, ov2735_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(ov2735->dev, "control init failed (%d)\n", ret);
		goto err_handler_free;
	}

	ret = v4l2_fwnode_device_parse(ov2735->dev, &props);
	if (ret)
		goto err_handler_free;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr,
					      &ov2735_ctrl_ops, &props);
	if (ret)
		goto err_handler_free;

	ov2735->sd.ctrl_handler = ctrl_hdlr;

	return 0;

err_handler_free:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int ov2735_set_pll_ctrl(struct ov2735 *ov2735)
{
	const struct ov2735_pll_parameters *pll_parameters;
	u8 pll_ctrl;
	u8 pll_outdiv;
	int ret = 0;

	pll_parameters = &pll_configs[ov2735->link_freq_index];

	/* BIT[7]: pll_clk_sel, BIT[6:2]: pll_nc, BIT[1:0]: pll_mc */
	pll_ctrl = ((pll_parameters->pll_nc << 2) | (pll_parameters->pll_mc << 0)) &
		    OV2735_PLL_CTRL_ENABLE;

	pll_outdiv = pll_parameters->pll_outdiv;

	ov2735_write(ov2735, OV2735_REG_PLL_CTRL, pll_ctrl, &ret);
	ov2735_write(ov2735, OV2735_REG_PLL_OUTDIV, pll_outdiv, &ret);

	return ret;
}

static int ov2735_set_framefmt(struct ov2735 *ov2735,
			       struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *crop;
	int ret = 0;

	format = v4l2_subdev_state_get_format(state, 0);
	crop = v4l2_subdev_state_get_crop(state, 0);

	ov2735_write(ov2735, OV2735_REG_V_START, crop->top, &ret);
	ov2735_write(ov2735, OV2735_REG_V_SIZE, format->height, &ret);
	ov2735_write(ov2735, OV2735_REG_MIPI_LINE_NUMBER, format->height, &ret);
	ov2735_write(ov2735, OV2735_REG_H_START, crop->left, &ret);
	/* OV2735_REG_H_SIZE: Image half horizontal size */
	ov2735_write(ov2735, OV2735_REG_H_SIZE, (format->width / 2), &ret);
	ov2735_write(ov2735, OV2735_REG_MIPI_COLOMN_NUMBER, format->width, &ret);

	return ret;
}

static int ov2735_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	int ret;

	ret = pm_runtime_resume_and_get(ov2735->dev);
	if (ret < 0)
		return ret;

	/* Apply pll settings */
	ret = ov2735_set_pll_ctrl(ov2735);
	if (ret) {
		dev_err(ov2735->dev, "failed to set frame format: %d\n", ret);
		goto err_rpm_put;
	}

	ret = ov2735_multi_reg_write(ov2735, ov2735_common_regs,
				     ARRAY_SIZE(ov2735_common_regs), NULL);
	if (ret) {
		dev_err(ov2735->dev, "failed to write common registers\n");
		goto err_rpm_put;
	}

	/* Apply format settings */
	ret = ov2735_set_framefmt(ov2735, state);
	if (ret) {
		dev_err(ov2735->dev, "failed to set frame format: %d\n", ret);
		goto err_rpm_put;
	}

	/* Apply customized values from user */
	ret = __v4l2_ctrl_handler_setup(ov2735->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	ret = ov2735_write(ov2735, OV2735_REG_STREAM_CTRL,
			   OV2735_STREAM_CTRL_ON, NULL);
	if (ret)
		goto err_rpm_put;

	return 0;

err_rpm_put:
	pm_runtime_put(ov2735->dev);
	return ret;
}

static int ov2735_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct ov2735 *ov2735 = to_ov2735(sd);
	int ret;

	ret = ov2735_write(ov2735, OV2735_REG_STREAM_CTRL,
			   OV2735_STREAM_CTRL_OFF, NULL);
	if (ret)
		dev_err(ov2735->dev, "%s failed to set stream\n", __func__);

	pm_runtime_put(ov2735->dev);

	return ret;
}

static int ov2735_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, 0);
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r = ov2735_native_area;
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = ov2735_active_area;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ov2735_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov2735_enum_frame_size(struct v4l2_subdev *sd,
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

static int ov2735_set_framing_limits(struct ov2735 *ov2735,
				     const struct ov2735_mode *mode)
{
	u32 hblank, vblank_def;
	int ret;

	hblank = mode->hts_def - mode->width;
	ret = __v4l2_ctrl_modify_range(ov2735->hblank, hblank, hblank, 1,
				       hblank);
	if (ret)
		return ret;

	vblank_def = mode->vts_def - mode->height;
	return __v4l2_ctrl_modify_range(ov2735->vblank, vblank_def,
					OV2735_FRAME_LENGTH_MAX - mode->height,
					1, vblank_def);
}

static int ov2735_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	const struct ov2735_mode *mode;
	struct v4l2_rect *crop;
	struct ov2735 *ov2735 = to_ov2735(sd);
	int ret;

	format = v4l2_subdev_state_get_format(sd_state, 0);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = ov2735_set_framing_limits(ov2735, mode);
		if (ret)
			return ret;
	}

	*format = fmt->format;

	/* Initialize crop rectangle */
	crop = v4l2_subdev_state_get_crop(sd_state, 0);
	*crop = mode->crop;

	return 0;
}

static int ov2735_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.code = MEDIA_BUS_FMT_SGRBG10_1X10,
			.width = supported_modes[0].width,
			.height = supported_modes[0].height,
		},
	};

	ov2735_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops ov2735_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops ov2735_pad_ops = {
	.enum_mbus_code = ov2735_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov2735_set_pad_format,
	.get_selection = ov2735_get_selection,
	.enum_frame_size = ov2735_enum_frame_size,
	.enable_streams = ov2735_enable_streams,
	.disable_streams = ov2735_disable_streams,
};

static const struct v4l2_subdev_ops ov2735_subdev_ops = {
	.video = &ov2735_video_ops,
	.pad = &ov2735_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov2735_internal_ops = {
	.init_state = ov2735_init_state,
};

static int ov2735_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2735 *ov2735 = to_ov2735(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ov2735_supply_name),
				    ov2735->supplies);
	if (ret) {
		dev_err(ov2735->dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(ov2735->enable_gpio, 1);
	/* T4: delay from PWDN pulling low to RSTB pulling high */
	fsleep(4 * USEC_PER_MSEC);

	ret = clk_prepare_enable(ov2735->xclk);
	if (ret) {
		dev_err(ov2735->dev, "failed to enable clock\n");
		goto err_regulator_off;
	}

	gpiod_set_value_cansleep(ov2735->reset_gpio, 0);
	/* T5: delay from RSTB pulling high to first I2C command */
	fsleep(5 * USEC_PER_MSEC);

	return 0;

err_regulator_off:
	regulator_bulk_disable(ARRAY_SIZE(ov2735_supply_name), ov2735->supplies);
	return ret;
}

static int ov2735_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2735 *ov2735 = to_ov2735(sd);

	gpiod_set_value_cansleep(ov2735->enable_gpio, 0);
	clk_disable_unprepare(ov2735->xclk);
	gpiod_set_value_cansleep(ov2735->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ov2735_supply_name), ov2735->supplies);

	return 0;
}

static int ov2735_get_regulators(struct ov2735 *ov2735)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov2735_supply_name); i++)
		ov2735->supplies[i].supply = ov2735_supply_name[i];

	return devm_regulator_bulk_get(ov2735->dev,
				       ARRAY_SIZE(ov2735_supply_name),
				       ov2735->supplies);
}

static int ov2735_identify_module(struct ov2735 *ov2735)
{
	u64 chip_id;
	int ret;

	ret = ov2735_read(ov2735, OV2735_REG_CHIPID, &chip_id, NULL);
	if (ret)
		return dev_err_probe(ov2735->dev, ret,
				     "failed to read chip id %x\n",
				     OV2735_CHIPID);

	if (chip_id != OV2735_CHIPID)
		return dev_err_probe(ov2735->dev, -EIO,
				     "chip id mismatch: %x!=%llx\n",
				     OV2735_CHIPID, chip_id);

	return 0;
}

static int ov2735_parse_endpoint(struct ov2735 *ov2735)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	unsigned long link_freq_bitmap;
	int ret;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(ov2735->dev), NULL);
	if (!ep)
		return dev_err_probe(ov2735->dev, -ENXIO,
				     "Failed to get next endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		ret = dev_err_probe(ov2735->dev, -EINVAL,
				    "only 2 data lanes are supported\n");
		goto error_out;
	}

	ret = v4l2_link_freq_to_bitmap(ov2735->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &link_freq_bitmap);
	if (ret) {
		ret = dev_err_probe(ov2735->dev, -EINVAL,
				    "only 420MHz frequency is available\n");
		goto error_out;
	}

	ov2735->link_freq_index = __ffs(link_freq_bitmap);

error_out:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
};

static int ov2735_probe(struct i2c_client *client)
{
	struct ov2735 *ov2735;
	unsigned int xclk_freq;
	int ret;

	ov2735 = devm_kzalloc(&client->dev, sizeof(*ov2735), GFP_KERNEL);
	if (!ov2735)
		return -ENOMEM;

	ov2735->dev = &client->dev;

	v4l2_i2c_subdev_init(&ov2735->sd, client, &ov2735_subdev_ops);
	ov2735->sd.internal_ops = &ov2735_internal_ops;

	ov2735->cci = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(ov2735->cci))
		return dev_err_probe(ov2735->dev, PTR_ERR(ov2735->cci),
				     "failed to initialize CCI\n");

	/* Set Current page to 0 */
	ov2735->current_page = 0;

	ret = devm_mutex_init(ov2735->dev, &ov2735->page_lock);
	if (ret)
		return dev_err_probe(ov2735->dev, ret,
				     "Failed to initialize lock\n");

	/* Get system clock (xvclk) */
	ov2735->xclk = devm_v4l2_sensor_clk_get(ov2735->dev, NULL);
	if (IS_ERR(ov2735->xclk))
		return dev_err_probe(ov2735->dev, PTR_ERR(ov2735->xclk),
				     "failed to get xclk\n");

	xclk_freq = clk_get_rate(ov2735->xclk);
	if (xclk_freq != OV2735_XCLK_FREQ)
		return dev_err_probe(ov2735->dev, -EINVAL,
				     "xclk frequency not supported: %u Hz\n",
				     xclk_freq);

	ret = ov2735_get_regulators(ov2735);
	if (ret)
		return dev_err_probe(ov2735->dev, ret,
				     "failed to get regulators\n");

	ret = ov2735_parse_endpoint(ov2735);
	if (ret)
		return dev_err_probe(ov2735->dev, ret,
				     "failed to parse endpoint configuration\n");

	ov2735->reset_gpio = devm_gpiod_get_optional(ov2735->dev,
						     "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov2735->reset_gpio))
		return dev_err_probe(ov2735->dev, PTR_ERR(ov2735->reset_gpio),
				     "failed to get reset GPIO\n");

	ov2735->enable_gpio = devm_gpiod_get_optional(ov2735->dev,
						      "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ov2735->enable_gpio))
		return dev_err_probe(ov2735->dev, PTR_ERR(ov2735->enable_gpio),
				     "failed to get enable GPIO\n");

	ret = ov2735_power_on(ov2735->dev);
	if (ret)
		return ret;

	ret = ov2735_identify_module(ov2735);
	if (ret)
		goto error_power_off;

	ret = ov2735_init_controls(ov2735);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	ov2735->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov2735->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov2735->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov2735->sd.entity, 1, &ov2735->pad);
	if (ret) {
		dev_err_probe(ov2735->dev, ret, "failed to init entity pads\n");
		goto error_handler_free;
	}

	ov2735->sd.state_lock = ov2735->handler.lock;
	ret = v4l2_subdev_init_finalize(&ov2735->sd);
	if (ret) {
		dev_err_probe(ov2735->dev, ret, "subdev init error\n");
		goto error_media_entity;
	}

	ret = devm_pm_runtime_get_noresume(ov2735->dev);
	if (ret) {
		dev_err_probe(ov2735->dev, ret,
			      "failed to get runtime PM noresume\n");
		goto error_subdev_cleanup;
	}

	ret = devm_pm_runtime_set_active_enabled(ov2735->dev);
	if (ret) {
		dev_err_probe(ov2735->dev, ret,
			      "failed to set runtime PM active+enabled\n");
		goto error_subdev_cleanup;
	}

	ret = v4l2_async_register_subdev_sensor(&ov2735->sd);
	if (ret) {
		dev_err_probe(ov2735->dev, ret,
			      "failed to register ov2735 sub-device\n");
		goto error_subdev_cleanup;
	}

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&ov2735->sd);

error_media_entity:
	media_entity_cleanup(&ov2735->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(ov2735->sd.ctrl_handler);

error_power_off:
	ov2735_power_off(ov2735->dev);

	return ret;
}

static void ov2735_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2735 *ov2735 = to_ov2735(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(&ov2735->sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(ov2735->sd.ctrl_handler);
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov2735_pm_ops,
				 ov2735_power_off, ov2735_power_on, NULL);

static const struct of_device_id ov2735_id[] = {
	{ .compatible = "ovti,ov2735" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov2735_id);

static struct i2c_driver ov2735_driver = {
	.driver = {
		.name = "ov2735",
		.pm = pm_ptr(&ov2735_pm_ops),
		.of_match_table = ov2735_id,
	},
	.probe = ov2735_probe,
	.remove = ov2735_remove,
};
module_i2c_driver(ov2735_driver);

MODULE_DESCRIPTION("OV2735 Camera Sensor Driver");
MODULE_AUTHOR("Hardevsinh Palaniya <hardevsinh.palaniya@siliconsignals.io>");
MODULE_AUTHOR("Himanshu Bhavani <himanshu.bhavani@siliconsignals.io>");
MODULE_LICENSE("GPL");
