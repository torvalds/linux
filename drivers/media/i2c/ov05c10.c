// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024-2026 Intel Corporation. */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

/*
 * OV05C10 has paged 8-bit registers. Encode register addresses as 16-bit
 * logical addresses (<page><offset>) so regmap range handling can switch
 * pages via 0xfd automatically, making page handling transparent to driver.
 */
#define OV05C10_REG8(page, addr)	CCI_REG8(((page) << 8) | (addr))
#define OV05C10_REG16(page, addr)	CCI_REG16(((page) << 8) | (addr))
#define OV05C10_REG24(page, addr)	CCI_REG24(((page) << 8) | (addr))
#define OV05C10_REG32(page, addr)	CCI_REG32(((page) << 8) | (addr))

#define OV05C10_REG_PAGE_FLAG		0xfd
#define OV05C10_MAX_REGISTER		0x0fff

#define OV05C10_REG_CHIP_ID		OV05C10_REG32(0x00, 0x00)
#define OV05C10_CHIP_ID			0x43055610

#define OV05C10_REG_MIPI_EN		OV05C10_REG8(0x00, 0xa0)
#define OV05C10_MIPI_EN_ENABLE		0x01

#define OV05C10_REG_H_SIZE_MIPI		OV05C10_REG16(0x00, 0x8e)
#define OV05C10_REG_V_SIZE_MIPI		OV05C10_REG16(0x00, 0x90)

#define OV05C10_REG_TRIGGER		OV05C10_REG8(0x01, 0x01)
#define OV05C10_TRIGGER_APPLY		0x01
#define OV05C10_TRIGGER_LATCH		0x02

#define OV05C10_REG_EXPOSURE		OV05C10_REG24(0x01, 0x02)
#define OV05C10_EXPOSURE_MARGIN		33
#define OV05C10_EXPOSURE_MIN		0x6
#define OV05C10_EXPOSURE_STEP		0x1

#define OV05C10_REG_DUMMY_LINE		OV05C10_REG16(0x01, 0x05)

#define OV05C10_REG_DIGITAL_GAIN	OV05C10_REG16(0x01, 0x21)
#define OV05C10_MAX_DIG_GAIN		0x100	/* 4x */
#define OV05C10_MIN_DIG_GAIN		0x40	/* 1x */
#define OV05C10_DGTL_GAIN_STEP		0x01
#define OV05C10_DGTL_GAIN_DEFAULT	0x40

#define OV05C10_REG_ANALOG_GAIN		OV05C10_REG8(0x01, 0x24)
#define OV05C10_MAX_ANALOG_GAIN		0xf8	/* 15.5x */
#define OV05C10_MIN_ANALOG_GAIN		0x10	/* 1x */
#define OV05C10_ANALOG_GAIN_STEP	0x01
#define OV05C10_ANALOG_GAIN_DEFAULT	0x10

#define OV05C10_REG_TIMING_VTS		OV05C10_REG16(0x01, 0x35)
#define OV05C10_VTS_MAX			0xffff
#define OV05C10_PPL			3236
#define OV05C10_DEFAULT_VTS		1860

#define OV05C10_REG_TIMING_HTS		OV05C10_REG16(0x01, 0x37)

#define OV05C10_REG_COL_ANA_ADDR_START	OV05C10_REG16(0x01, 0x28)
#define OV05C10_REG_COL_ANA_ADDR_SIZE	OV05C10_REG16(0x01, 0x2a)
#define OV05C10_REG_ROW_ANA_ADDR_START	OV05C10_REG16(0x01, 0x2c)
#define OV05C10_REG_ROW_ANA_ADDR_SIZE	OV05C10_REG16(0x01, 0x2e)

#define OV05C10_REG_DEM_V_START		OV05C10_REG16(0x02, 0xa0)
#define OV05C10_REG_DEM_V_SIZE		OV05C10_REG16(0x02, 0xa2)
#define OV05C10_REG_DEM_H_START		OV05C10_REG16(0x02, 0xa4)
#define OV05C10_REG_DEM_H_SIZE		OV05C10_REG16(0x02, 0xa6)

#define OV05C10_REG_TEST_PATTERN_EN	OV05C10_REG8(0x04, 0x12)
#define OV05C10_TEST_PATTERN_DISABLE	0x00
#define OV05C10_TEST_PATTERN_ENABLE	0x01

#define OV05C10_REG_TEST_CLK_EN		OV05C10_REG8(0x04, 0xf3)
#define OV05C10_TEST_CLK_DISABLE	0x00
#define OV05C10_TEST_CLK_ENABLE		0x02

#define OV05C10_PIXEL_RATE		192000000ULL
#define OV05C10_NATIVE_WIDTH		2888
#define OV05C10_NATIVE_HEIGHT		1808

static const struct regmap_range_cfg ov05c10_range = {
	.range_min = 0x0000,
	.range_max = OV05C10_MAX_REGISTER,
	.selector_reg = OV05C10_REG_PAGE_FLAG,
	.selector_mask = 0x0f,
	.selector_shift = 0,
	.window_start = 0x00,
	.window_len = 0x100,
};

static const struct regmap_config ov05c10_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = OV05C10_MAX_REGISTER,
	.ranges = &ov05c10_range,
	.num_ranges = 1,
	.disable_locking = true,
};

#define to_ov05c10(_sd) container_of_const(_sd, struct ov05c10, sd)

static const char *const ov05c10_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
};

static const s64 ov05c10_link_freq_items[] = {
	480000000ULL,
};

struct ov05c10_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

struct ov05c10_mode {
	u16 left;
	u16 top;
	u32 width;
	u32 height;
	u16 hts;
	u16 vts_def;
	u16 vts_min;
	u16 lanes;
	u32 code;
	/* Sensor register settings for this mode */
	const struct ov05c10_reg_list reg_list;
};

/* 2800X1576_2lane_raw10_Mclk19.2M_pclk96M_30fps */
static const struct cci_reg_sequence mode_2800_1576_30fps[] = {
	{ OV05C10_REG8(0x00, 0x20), 0x00 },
	{ OV05C10_REG8(0x00, 0x20), 0x0b },
	{ OV05C10_REG8(0x00, 0xc1), 0x09 },
	{ OV05C10_REG8(0x00, 0x21), 0x06 },
	{ OV05C10_REG8(0x00, 0x11), 0x4e },
	{ OV05C10_REG8(0x00, 0x12), 0x13 },
	{ OV05C10_REG8(0x00, 0x14), 0x96 },
	{ OV05C10_REG8(0x00, 0x1b), 0x64 },
	{ OV05C10_REG8(0x00, 0x1d), 0x02 },
	{ OV05C10_REG8(0x00, 0x1e), 0x40 },
	{ OV05C10_REG8(0x00, 0xe7), 0x03 },
	{ OV05C10_REG8(0x00, 0xe7), 0x00 },
	{ OV05C10_REG8(0x00, 0x21), 0x00 },
	{ OV05C10_REG8(0x01, 0x03), 0x00 },
	{ OV05C10_REG8(0x01, 0x04), 0x06 },
	{ OV05C10_REG8(0x01, 0x06), 0x76 },
	{ OV05C10_REG8(0x01, 0x07), 0x08 },
	{ OV05C10_REG8(0x01, 0x1b), 0x01 },
	{ OV05C10_REG8(0x01, 0x42), 0x5d },
	{ OV05C10_REG8(0x01, 0x43), 0x08 },
	{ OV05C10_REG8(0x01, 0x44), 0x81 },
	{ OV05C10_REG8(0x01, 0x46), 0x5f },
	{ OV05C10_REG8(0x01, 0x48), 0x18 },
	{ OV05C10_REG8(0x01, 0x49), 0x04 },
	{ OV05C10_REG8(0x01, 0x5c), 0x18 },
	{ OV05C10_REG8(0x01, 0x5e), 0x13 },
	{ OV05C10_REG8(0x01, 0x70), 0x15 },
	{ OV05C10_REG8(0x01, 0x77), 0x35 },
	{ OV05C10_REG8(0x01, 0x79), 0xb2 },
	{ OV05C10_REG8(0x01, 0x7b), 0x08 },
	{ OV05C10_REG8(0x01, 0x7d), 0x08 },
	{ OV05C10_REG8(0x01, 0x7e), 0x08 },
	{ OV05C10_REG8(0x01, 0x7f), 0x08 },
	{ OV05C10_REG8(0x01, 0x90), 0x37 },
	{ OV05C10_REG8(0x01, 0x91), 0x05 },
	{ OV05C10_REG8(0x01, 0x92), 0x18 },
	{ OV05C10_REG8(0x01, 0x93), 0x27 },
	{ OV05C10_REG8(0x01, 0x94), 0x05 },
	{ OV05C10_REG8(0x01, 0x95), 0x38 },
	{ OV05C10_REG8(0x01, 0x9b), 0x00 },
	{ OV05C10_REG8(0x01, 0x9c), 0x06 },
	{ OV05C10_REG8(0x01, 0x9d), 0x28 },
	{ OV05C10_REG8(0x01, 0x9e), 0x06 },
	{ OV05C10_REG8(0x01, 0xb2), 0x0f },
	{ OV05C10_REG8(0x01, 0xb3), 0x29 },
	{ OV05C10_REG8(0x01, 0xbf), 0x3c },
	{ OV05C10_REG8(0x01, 0xc2), 0x04 },
	{ OV05C10_REG8(0x01, 0xc4), 0x00 },
	{ OV05C10_REG8(0x01, 0xca), 0x20 },
	{ OV05C10_REG8(0x01, 0xcb), 0x20 },
	{ OV05C10_REG8(0x01, 0xcc), 0x28 },
	{ OV05C10_REG8(0x01, 0xcd), 0x28 },
	{ OV05C10_REG8(0x01, 0xce), 0x20 },
	{ OV05C10_REG8(0x01, 0xcf), 0x20 },
	{ OV05C10_REG8(0x01, 0xd0), 0x2a },
	{ OV05C10_REG8(0x01, 0xd1), 0x2a },
	{ OV05C10_REG8(0x0f, 0x00), 0x00 },
	{ OV05C10_REG8(0x0f, 0x01), 0xa0 },
	{ OV05C10_REG8(0x0f, 0x02), 0x48 },
	{ OV05C10_REG8(0x0f, 0x07), 0x8e },
	{ OV05C10_REG8(0x0f, 0x08), 0x70 },
	{ OV05C10_REG8(0x0f, 0x09), 0x01 },
	{ OV05C10_REG8(0x0f, 0x0b), 0x40 },
	{ OV05C10_REG8(0x0f, 0x0d), 0x07 },
	{ OV05C10_REG8(0x0f, 0x11), 0x33 },
	{ OV05C10_REG8(0x0f, 0x12), 0x77 },
	{ OV05C10_REG8(0x0f, 0x13), 0x66 },
	{ OV05C10_REG8(0x0f, 0x14), 0x65 },
	{ OV05C10_REG8(0x0f, 0x15), 0x37 },
	{ OV05C10_REG8(0x0f, 0x16), 0xbf },
	{ OV05C10_REG8(0x0f, 0x17), 0xff },
	{ OV05C10_REG8(0x0f, 0x18), 0xff },
	{ OV05C10_REG8(0x0f, 0x19), 0x12 },
	{ OV05C10_REG8(0x0f, 0x1a), 0x10 },
	{ OV05C10_REG8(0x0f, 0x1c), 0x77 },
	{ OV05C10_REG8(0x0f, 0x1d), 0x77 },
	{ OV05C10_REG8(0x0f, 0x20), 0x0f },
	{ OV05C10_REG8(0x0f, 0x21), 0x0f },
	{ OV05C10_REG8(0x0f, 0x22), 0x0f },
	{ OV05C10_REG8(0x0f, 0x23), 0x0f },
	{ OV05C10_REG8(0x0f, 0x2b), 0x20 },
	{ OV05C10_REG8(0x0f, 0x2c), 0x20 },
	{ OV05C10_REG8(0x0f, 0x2d), 0x04 },
	{ OV05C10_REG8(0x03, 0x9d), 0x0f },
	{ OV05C10_REG8(0x03, 0x9f), 0x40 },
	{ OV05C10_REG8(0x00, 0x20), 0x1b },
	{ OV05C10_REG8(0x04, 0x19), 0x60 },
	{ OV05C10_REG8(0x02, 0x75), 0x04 },
	{ OV05C10_REG8(0x02, 0x7f), 0x06 },
	{ OV05C10_REG8(0x02, 0x9a), 0x03 },
	{ OV05C10_REG8(0x07, 0x42), 0x00 },
	{ OV05C10_REG8(0x07, 0x43), 0x80 },
	{ OV05C10_REG8(0x07, 0x44), 0x00 },
	{ OV05C10_REG8(0x07, 0x45), 0x80 },
	{ OV05C10_REG8(0x07, 0x46), 0x00 },
	{ OV05C10_REG8(0x07, 0x47), 0x80 },
	{ OV05C10_REG8(0x07, 0x48), 0x00 },
	{ OV05C10_REG8(0x07, 0x49), 0x80 },
	{ OV05C10_REG8(0x07, 0x00), 0xf7 },
	{ OV05C10_REG8(0x00, 0xe7), 0x03 },
	{ OV05C10_REG8(0x00, 0xe7), 0x00 },
	{ OV05C10_REG8(0x00, 0x93), 0x18 },
	{ OV05C10_REG8(0x00, 0x94), 0xff },
	{ OV05C10_REG8(0x00, 0x95), 0xbd },
	{ OV05C10_REG8(0x00, 0x96), 0x1a },
	{ OV05C10_REG8(0x00, 0x98), 0x04 },
	{ OV05C10_REG8(0x00, 0x99), 0x08 },
	{ OV05C10_REG8(0x00, 0x9b), 0x10 },
	{ OV05C10_REG8(0x00, 0x9c), 0x3f },
	{ OV05C10_REG8(0x00, 0xa1), 0x05 },
	{ OV05C10_REG8(0x00, 0xa4), 0x2f },
	{ OV05C10_REG8(0x00, 0xc0), 0x0c },
	{ OV05C10_REG8(0x00, 0xc1), 0x08 },
	{ OV05C10_REG8(0x00, 0xc2), 0x00 },
	{ OV05C10_REG8(0x00, 0xb6), 0x20 },
	{ OV05C10_REG8(0x00, 0xbb), 0x80 },
	{ OV05C10_REG_MIPI_EN, 0x00 },
	{ OV05C10_REG8(0x01, 0x33), 0x03 },
	{ OV05C10_REG8(0x01, 0x01), 0x02 },
	{ OV05C10_REG8(0x00, 0x20), 0x1f },
};

static const struct ov05c10_mode supported_modes[] = {
	{
		.left = 46,
		.top = 117,
		.width = 2800,
		.height = 1576,
		.hts = 758,
		.vts_def = 1978,
		.vts_min = 1978,
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2800_1576_30fps),
			.regs = mode_2800_1576_30fps,
		},
	},
};

struct ov05c10 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	struct regmap *regmap;
	unsigned long link_freq_bitmap;

	struct clk *img_clk;
	struct regulator *avdd;
	struct gpio_desc *reset;
	bool identified;
};

static int ov05c10_test_pattern(struct ov05c10 *ov05c10, u32 pattern)
{
	int ret = 0;

	if (pattern) {
		cci_write(ov05c10->regmap, OV05C10_REG_TEST_CLK_EN,
			  OV05C10_TEST_CLK_ENABLE, &ret);
		cci_write(ov05c10->regmap, OV05C10_REG_TEST_PATTERN_EN,
			  OV05C10_TEST_PATTERN_ENABLE, &ret);
	} else {
		cci_write(ov05c10->regmap, OV05C10_REG_TEST_CLK_EN,
			  OV05C10_TEST_CLK_DISABLE, &ret);
		cci_write(ov05c10->regmap, OV05C10_REG_TEST_PATTERN_EN,
			  OV05C10_TEST_PATTERN_DISABLE, &ret);
	}

	return ret;
}

static int ov05c10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov05c10 *ov05c10 =
		container_of_const(ctrl->handler, struct ov05c10, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	struct v4l2_subdev_state *state;
	const struct v4l2_mbus_framefmt *format;
	u64 vts;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&ov05c10->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		ret = __v4l2_ctrl_modify_range(ov05c10->exposure,
					       ov05c10->exposure->minimum,
					       format->height + ctrl->val -
					       OV05C10_EXPOSURE_MARGIN,
					       ov05c10->exposure->step,
					       format->height -
					       OV05C10_EXPOSURE_MARGIN);

		if (ret)
			return ret;
	}

	if (!pm_runtime_get_if_active(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(ov05c10->regmap, OV05C10_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(ov05c10->regmap, OV05C10_REG_DIGITAL_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(ov05c10->regmap, OV05C10_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		/*
		 * REG_TIMING_VTS is read-only and increased by writing to
		 * REG_DUMMY_LINE in ov05c10. The calculation formula is
		 * required VTS = dummyline + current VTS. Here get the
		 * current VTS and calculate the required dummyline.
		 */
		cci_read(ov05c10->regmap, OV05C10_REG_TIMING_VTS, &vts, &ret);
		if (ret)
			goto err;

		ret = cci_write(ov05c10->regmap, OV05C10_REG_DUMMY_LINE,
				max_t(int, ctrl->val + format->height - vts, 0),
				NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov05c10_test_pattern(ov05c10, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	cci_write(ov05c10->regmap, OV05C10_REG_TRIGGER,
		  OV05C10_TRIGGER_APPLY, &ret);
	if (ret) {
		dev_err(&client->dev, "failed to trigger write");
		goto err;
	}

err:
	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov05c10_ctrl_ops = {
	.s_ctrl = ov05c10_set_ctrl,
};

static int ov05c10_init_controls(struct ov05c10 *ov05c10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, vblank_max, vblank_min, vblank_def, hblank;
	const struct ov05c10_mode *mode = &supported_modes[0];
	struct v4l2_ctrl *link_freq;
	int ret;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	ctrl_hdlr = &ov05c10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov05c10_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE(ov05c10_link_freq_items) -
					   1, 0, ov05c10_link_freq_items);

	vblank_min = mode->vts_min - mode->height;
	vblank_max = OV05C10_VTS_MAX - mode->height;
	vblank_def = mode->vts_def - mode->height;
	ov05c10->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_min,
					    vblank_max, 1, vblank_def);

	hblank = OV05C10_PPL - mode->width;
	ov05c10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops,
					    V4L2_CID_HBLANK, hblank, hblank, 1,
					    hblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV05C10_MIN_ANALOG_GAIN, OV05C10_MAX_ANALOG_GAIN,
			  OV05C10_ANALOG_GAIN_STEP,
			  OV05C10_ANALOG_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV05C10_MIN_DIG_GAIN, OV05C10_MAX_DIG_GAIN,
			  OV05C10_DGTL_GAIN_STEP, OV05C10_DGTL_GAIN_DEFAULT);

	exposure_max = mode->vts_def - OV05C10_EXPOSURE_MARGIN;
	ov05c10->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OV05C10_EXPOSURE_MIN,
					      exposure_max,
					      OV05C10_EXPOSURE_STEP,
					      exposure_max);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov05c10_ctrl_ops,
			  V4L2_CID_PIXEL_RATE, OV05C10_PIXEL_RATE,
			  OV05C10_PIXEL_RATE, 1, OV05C10_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov05c10_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov05c10_test_pattern_menu) - 1,
				     0, 0, ov05c10_test_pattern_menu);

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov05c10_ctrl_ops, &props);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ov05c10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov05c10->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static int ov05c10_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams)
{
	struct ov05c10 *ov05c10 = to_ov05c10(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	struct v4l2_mbus_framefmt *format =
		v4l2_subdev_state_get_format(state, 0);
	const struct ov05c10_mode *mode =
		v4l2_find_nearest_size(supported_modes,
				       ARRAY_SIZE(supported_modes),
				       width, height,
				       format->width, format->height);
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	cci_multi_reg_write(ov05c10->regmap, mode->reg_list.regs,
			    mode->reg_list.num_of_regs, &ret);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		goto err_rpm_put;
	}

	cci_write(ov05c10->regmap, OV05C10_REG_DEM_V_START, mode->top, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_DEM_V_SIZE, mode->height, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_DEM_H_START, mode->left, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_DEM_H_SIZE, mode->width, &ret);

	cci_write(ov05c10->regmap, OV05C10_REG_H_SIZE_MIPI, mode->width, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_V_SIZE_MIPI, mode->height, &ret);

	ret = __v4l2_ctrl_handler_setup(ov05c10->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	cci_write(ov05c10->regmap, OV05C10_REG_MIPI_EN,
		  OV05C10_MIPI_EN_ENABLE, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_TRIGGER,
		  OV05C10_TRIGGER_LATCH, &ret);
	if (ret) {
		dev_err(&client->dev, "failed to start stream");
		goto err_rpm_put;
	}

	return 0;

err_rpm_put:
	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

static int ov05c10_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state, u32 pad,
				   u64 streams)
{
	struct ov05c10 *ov05c10 = to_ov05c10(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	int ret = 0;

	cci_write(ov05c10->regmap, OV05C10_REG_MIPI_EN, 0, &ret);
	cci_write(ov05c10->regmap, OV05C10_REG_TRIGGER,
		  OV05C10_TRIGGER_LATCH, &ret);
	if (ret < 0)
		dev_err(&client->dev, "failed to stop stream");

	pm_runtime_put_autosuspend(&client->dev);

	return ret;
}

static void ov05c10_pad_format_crop_from_mode(const struct ov05c10_mode *mode,
					      struct v4l2_mbus_framefmt *fmt,
					      struct v4l2_rect *crop)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = mode->code;
	fmt->field = V4L2_FIELD_NONE;

	crop->left = mode->left;
	crop->width = mode->width;
	crop->top = mode->top;
	crop->height = mode->height;
}

static int ov05c10_set_format(struct v4l2_subdev *sd,
			      const struct v4l2_subdev_client_info *ci,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct ov05c10 *ov05c10 = to_ov05c10(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(sd_state, fmt->pad);
	const struct ov05c10_mode *mode;
	s64 hblank, exposure_max;
	int ret;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	ov05c10_pad_format_crop_from_mode(mode, &fmt->format, crop);
	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	hblank = OV05C10_PPL - mode->width;
	ret = __v4l2_ctrl_modify_range(ov05c10->hblank, hblank, hblank,
				       1, hblank);
	if (ret) {
		dev_err(&client->dev, "HBLANK ctrl range update failed\n");
		return ret;
	}

	/* Update limits and set FPS to default */
	ret = __v4l2_ctrl_modify_range(ov05c10->vblank,
				       mode->vts_min - mode->height,
				       OV05C10_VTS_MAX - mode->height, 1,
				       mode->vts_def - mode->height);
	if (ret) {
		dev_err(&client->dev, "VBLANK ctrl range update failed\n");
		return ret;
	}

	ret = __v4l2_ctrl_s_ctrl(ov05c10->vblank,
				 mode->vts_def - mode->height);
	if (ret) {
		dev_err(&client->dev, "VBLANK ctrl set failed\n");
		return ret;
	}

	exposure_max = mode->vts_def - OV05C10_EXPOSURE_MARGIN;
	ret = __v4l2_ctrl_modify_range(ov05c10->exposure, OV05C10_EXPOSURE_MIN,
				       exposure_max, OV05C10_EXPOSURE_STEP,
				       exposure_max);
	if (ret)
		dev_err(&client->dev, "exposure ctrl range update failed\n");

	return ret;
}

static int ov05c10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov05c10_enum_frame_size(struct v4l2_subdev *sd,
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

static int ov05c10_get_selection(struct v4l2_subdev *sd,
				 const struct v4l2_subdev_client_info *ci,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		break;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV05C10_NATIVE_WIDTH;
		sel->r.height = OV05C10_NATIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ov05c10_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state)
{
	ov05c10_pad_format_crop_from_mode(&supported_modes[0],
					  v4l2_subdev_state_get_format(sd_state, 0),
					  v4l2_subdev_state_get_crop(sd_state, 0));

	return 0;
}

static const struct v4l2_subdev_pad_ops ov05c10_pad_ops = {
	.set_fmt = ov05c10_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = ov05c10_enum_mbus_code,
	.enum_frame_size = ov05c10_enum_frame_size,
	.get_selection = ov05c10_get_selection,
	.enable_streams = ov05c10_enable_streams,
	.disable_streams = ov05c10_disable_streams,
};

static const struct v4l2_subdev_ops ov05c10_subdev_ops = {
	.pad = &ov05c10_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov05c10_internal_ops = {
	.init_state = ov05c10_init_state,
};

static int ov05c10_parse_fwnode(struct ov05c10 *ov05c10, struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	int ret;

	endpoint = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
						   FWNODE_GRAPH_ENDPOINT_NEXT);
	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err_probe(dev, ret, "parsing endpoint node failed\n");
		goto out_err;
	}

	ret = v4l2_link_freq_to_bitmap(dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       ov05c10_link_freq_items,
				       ARRAY_SIZE(ov05c10_link_freq_items),
				       &ov05c10->link_freq_bitmap);

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov05c10_identify_module(struct ov05c10 *ov05c10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov05c10->sd);
	u64 val;
	int ret = 0;

	cci_read(ov05c10->regmap, OV05C10_REG_CHIP_ID, &val, &ret);
	if (ret)
		return dev_err_probe(&client->dev, ret, "chip id read err");

	if (val != OV05C10_CHIP_ID)
		return dev_err_probe(&client->dev, -ENXIO,
				     "chip id mismatch: %x!=%llu",
				     OV05C10_CHIP_ID, val);

	return 0;
}

/* This function tries to get power control resources */
static int ov05c10_get_pm_resources(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov05c10 *ov05c10 = to_ov05c10(sd);

	ov05c10->img_clk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(ov05c10->img_clk))
		return dev_err_probe(dev, PTR_ERR(ov05c10->img_clk),
				     "failed to get imaging clock\n");

	ov05c10->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ov05c10->avdd))
		return dev_err_probe(dev, PTR_ERR(ov05c10->avdd),
				     "failed to get avdd regulator\n");

	ov05c10->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ov05c10->reset))
		return dev_err_probe(dev, PTR_ERR(ov05c10->reset),
				     "failed to get reset gpio\n");

	return 0;
}

static int ov05c10_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov05c10 *ov05c10 = to_ov05c10(sd);

	gpiod_set_value_cansleep(ov05c10->reset, 1);
	regulator_disable(ov05c10->avdd);
	clk_disable_unprepare(ov05c10->img_clk);

	return 0;
}

static int ov05c10_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov05c10 *ov05c10 = to_ov05c10(sd);
	int ret;

	ret = clk_prepare_enable(ov05c10->img_clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable imaging clock: %d", ret);
		return ret;
	}

	ret = regulator_enable(ov05c10->avdd);
	if (ret < 0) {
		dev_err(dev, "failed to enable avdd: %d", ret);
		goto err_clk_disable_unprepare;
	}

	gpiod_set_value_cansleep(ov05c10->reset, 0);
	/* 5ms to wait ready after XSHUTDN assert */
	fsleep(5000);

	if (!ov05c10->identified) {
		ret = ov05c10_identify_module(ov05c10);
		if (ret) {
			dev_err_probe(dev, ret, "failed to find sensor: %d\n",
				      ret);
			goto err_power_off;
		}

		ov05c10->identified = true;
	}

	return 0;

err_power_off:
	gpiod_set_value_cansleep(ov05c10->reset, 1);
	regulator_disable(ov05c10->avdd);

err_clk_disable_unprepare:
	clk_disable_unprepare(ov05c10->img_clk);

	return ret;
}

static const struct dev_pm_ops ov05c10_pm_ops = {
	SET_RUNTIME_PM_OPS(ov05c10_power_off, ov05c10_power_on, NULL)
};

static int ov05c10_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov05c10 *ov05c10;
	bool full_power;
	int ret;

	ov05c10 = devm_kzalloc(&client->dev, sizeof(*ov05c10), GFP_KERNEL);
	if (!ov05c10)
		return -ENOMEM;

	ret = ov05c10_parse_fwnode(ov05c10, dev);
	if (ret)
		return ret;

	ov05c10->regmap = devm_regmap_init_i2c(client, &ov05c10_regmap_config);
	if (IS_ERR(ov05c10->regmap))
		return dev_err_probe(dev, PTR_ERR(ov05c10->regmap),
				     "failed to init regmap\n");

	v4l2_i2c_subdev_init(&ov05c10->sd, client, &ov05c10_subdev_ops);

	ret = ov05c10_get_pm_resources(dev);
	if (ret)
		return ret;

	full_power = acpi_dev_state_d0(dev);
	if (full_power) {
		ret = ov05c10_power_on(dev);
		if (ret)
			return ret;
	}

	ret = ov05c10_init_controls(ov05c10);
	if (ret) {
		dev_err_probe(&client->dev, ret, "failed to init controls\n");
		goto probe_error_power_off;
	}

	ov05c10->sd.internal_ops = &ov05c10_internal_ops;
	ov05c10->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov05c10->sd.entity.ops = NULL;
	ov05c10->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov05c10->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov05c10->sd.entity, 1, &ov05c10->pad);
	if (ret) {
		dev_err_probe(&client->dev, ret,
			      "failed to init entity pads\n");
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov05c10->sd.state_lock = ov05c10->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&ov05c10->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret, "v4l2 subdev init error\n");
		goto probe_error_media_entity_cleanup;
	}

	if (full_power)
		pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&ov05c10->sd);
	if (ret < 0) {
		dev_err_probe(&client->dev, ret,
			      "failed to register V4L2 subdev");
		goto probe_error_rpm;
	}
	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_rpm:
	if (full_power)
		pm_runtime_set_suspended(&client->dev);
	pm_runtime_disable(&client->dev);
	v4l2_subdev_cleanup(&ov05c10->sd);

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov05c10->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov05c10->sd.ctrl_handler);

probe_error_power_off:
	if (full_power)
		ov05c10_power_off(&client->dev);

	return ret;
}

static void ov05c10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov05c10 *ov05c10 = to_ov05c10(sd);

	v4l2_async_unregister_subdev(&ov05c10->sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&ov05c10->sd.entity);
	v4l2_ctrl_handler_free(&ov05c10->ctrl_handler);
	pm_runtime_dont_use_autosuspend(&client->dev);
	pm_runtime_disable(&client->dev);

	if (!pm_runtime_status_suspended(&client->dev)) {
		ov05c10_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

#if IS_BUILTIN(CONFIG_ACPI)
static const struct acpi_device_id ov05c10_acpi_ids[] = {
	{ .id = "OVTI05C1" },
	{ }
};

MODULE_DEVICE_TABLE(acpi, ov05c10_acpi_ids);
#endif

static struct i2c_driver ov05c10_i2c_driver = {
	.driver = {
		.name = "ov05c10",
		.pm = pm_ptr(&ov05c10_pm_ops),
		.acpi_match_table = ACPI_PTR(ov05c10_acpi_ids),
	},
	.probe = ov05c10_probe,
	.remove = ov05c10_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(ov05c10_i2c_driver);
MODULE_DESCRIPTION("OmniVision ov05c10 camera driver");
MODULE_AUTHOR("Dongcheng Yan <dongcheng.yan@intel.com>");
MODULE_LICENSE("GPL");
