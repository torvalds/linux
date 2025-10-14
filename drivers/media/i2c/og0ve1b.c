// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024-2025 Linaro Ltd

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OG0VE1B_LINK_FREQ_500MHZ	(500 * HZ_PER_MHZ)
#define OG0VE1B_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)

#define OG0VE1B_REG_CHIP_ID		CCI_REG24(0x300a)
#define OG0VE1B_CHIP_ID			0xc75645

#define OG0VE1B_REG_MODE_SELECT		CCI_REG8(0x0100)
#define OG0VE1B_MODE_STANDBY		0x00
#define OG0VE1B_MODE_STREAMING		BIT(0)

#define OG0VE1B_REG_SOFTWARE_RST	CCI_REG8(0x0103)
#define OG0VE1B_SOFTWARE_RST		BIT(0)

/* Exposure controls from sensor */
#define OG0VE1B_REG_EXPOSURE		CCI_REG24(0x3500)
#define OG0VE1B_EXPOSURE_MIN		1
#define OG0VE1B_EXPOSURE_MAX_MARGIN	14
#define OG0VE1B_EXPOSURE_STEP		1
#define OG0VE1B_EXPOSURE_DEFAULT	554

/* Analogue gain controls from sensor */
#define OG0VE1B_REG_ANALOGUE_GAIN	CCI_REG16(0x350a)
#define OG0VE1B_ANALOGUE_GAIN_MIN	1
#define OG0VE1B_ANALOGUE_GAIN_MAX	0x1ff
#define OG0VE1B_ANALOGUE_GAIN_STEP	1
#define OG0VE1B_ANALOGUE_GAIN_DEFAULT	16

/* Test pattern */
#define OG0VE1B_REG_PRE_ISP		CCI_REG8(0x5e00)
#define OG0VE1B_TEST_PATTERN_ENABLE	BIT(7)

#define to_og0ve1b(_sd)			container_of(_sd, struct og0ve1b, sd)

static const s64 og0ve1b_link_freq_menu[] = {
	OG0VE1B_LINK_FREQ_500MHZ,
};

struct og0ve1b_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct og0ve1b_mode {
	u32 width;	/* Frame width in pixels */
	u32 height;	/* Frame height in pixels */
	u32 hts;	/* Horizontal timing size */
	u32 vts;	/* Default vertical timing size */
	u32 bpp;	/* Bits per pixel */

	const struct og0ve1b_reg_list reg_list;	/* Sensor register setting */
};

static const char * const og0ve1b_test_pattern_menu[] = {
	"Disabled",
	"Vertical Colour Bars",
};

static const char * const og0ve1b_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OG0VE1B_NUM_SUPPLIES	ARRAY_SIZE(og0ve1b_supply_names)

struct og0ve1b {
	struct device *dev;
	struct regmap *regmap;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[OG0VE1B_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;

	/* Saved register value */
	u64 pre_isp;
};

static const struct cci_reg_sequence og0ve1b_640x480_120fps_mode[] = {
	{ CCI_REG8(0x30a0), 0x02 },
	{ CCI_REG8(0x30a1), 0x00 },
	{ CCI_REG8(0x30a2), 0x48 },
	{ CCI_REG8(0x30a3), 0x34 },
	{ CCI_REG8(0x30a4), 0xf7 },
	{ CCI_REG8(0x30a5), 0x00 },
	{ CCI_REG8(0x3082), 0x32 },
	{ CCI_REG8(0x3083), 0x01 },
	{ CCI_REG8(0x301c), 0xf0 },
	{ CCI_REG8(0x301e), 0x0b },
	{ CCI_REG8(0x3106), 0x10 },
	{ CCI_REG8(0x3708), 0x77 },
	{ CCI_REG8(0x3709), 0xf8 },
	{ CCI_REG8(0x3717), 0x00 },
	{ CCI_REG8(0x3782), 0x00 },
	{ CCI_REG8(0x3783), 0x47 },
	{ CCI_REG8(0x37a2), 0x00 },
	{ CCI_REG8(0x3503), 0x07 },
	{ CCI_REG8(0x3509), 0x10 },
	{ CCI_REG8(0x3600), 0x83 },
	{ CCI_REG8(0x3601), 0x21 },
	{ CCI_REG8(0x3602), 0xf1 },
	{ CCI_REG8(0x360a), 0x18 },
	{ CCI_REG8(0x360e), 0xb3 },
	{ CCI_REG8(0x3613), 0x20 },
	{ CCI_REG8(0x366a), 0x78 },
	{ CCI_REG8(0x3706), 0x63 },
	{ CCI_REG8(0x3713), 0x00 },
	{ CCI_REG8(0x3716), 0xb0 },
	{ CCI_REG8(0x37a1), 0x38 },
	{ CCI_REG8(0x3800), 0x00 },
	{ CCI_REG8(0x3801), 0x04 },
	{ CCI_REG8(0x3802), 0x00 },
	{ CCI_REG8(0x3803), 0x04 },
	{ CCI_REG8(0x3804), 0x02 },
	{ CCI_REG8(0x3805), 0x8b },
	{ CCI_REG8(0x3806), 0x01 },
	{ CCI_REG8(0x3807), 0xeb },
	{ CCI_REG8(0x3808), 0x02 },	/* output width */
	{ CCI_REG8(0x3809), 0x80 },
	{ CCI_REG8(0x380a), 0x01 },	/* output height */
	{ CCI_REG8(0x380b), 0xe0 },
	{ CCI_REG8(0x380c), 0x03 },	/* horizontal timing size */
	{ CCI_REG8(0x380d), 0x18 },
	{ CCI_REG8(0x380e), 0x02 },	/* vertical timing size */
	{ CCI_REG8(0x380f), 0x38 },
	{ CCI_REG8(0x3811), 0x04 },
	{ CCI_REG8(0x3813), 0x04 },
	{ CCI_REG8(0x3814), 0x11 },
	{ CCI_REG8(0x3815), 0x11 },
	{ CCI_REG8(0x3820), 0x00 },
	{ CCI_REG8(0x3821), 0x00 },
	{ CCI_REG8(0x3823), 0x04 },
	{ CCI_REG8(0x382a), 0x00 },
	{ CCI_REG8(0x382b), 0x03 },
	{ CCI_REG8(0x3840), 0x00 },
	{ CCI_REG8(0x389e), 0x00 },
	{ CCI_REG8(0x3c05), 0x08 },
	{ CCI_REG8(0x3c26), 0x02 },
	{ CCI_REG8(0x3c27), 0xc0 },
	{ CCI_REG8(0x3c28), 0x00 },
	{ CCI_REG8(0x3c29), 0x40 },
	{ CCI_REG8(0x3c2c), 0x00 },
	{ CCI_REG8(0x3c2d), 0x50 },
	{ CCI_REG8(0x3c2e), 0x02 },
	{ CCI_REG8(0x3c2f), 0x66 },
	{ CCI_REG8(0x3c33), 0x08 },
	{ CCI_REG8(0x3c35), 0x00 },
	{ CCI_REG8(0x3c36), 0x00 },
	{ CCI_REG8(0x3c37), 0x00 },
	{ CCI_REG8(0x3f52), 0x9b },
	{ CCI_REG8(0x4001), 0x42 },
	{ CCI_REG8(0x4004), 0x08 },
	{ CCI_REG8(0x4005), 0x00 },
	{ CCI_REG8(0x4007), 0x28 },
	{ CCI_REG8(0x4009), 0x40 },
	{ CCI_REG8(0x4307), 0x30 },
	{ CCI_REG8(0x4500), 0x80 },
	{ CCI_REG8(0x4501), 0x02 },
	{ CCI_REG8(0x4502), 0x47 },
	{ CCI_REG8(0x4504), 0x7f },
	{ CCI_REG8(0x4601), 0x48 },
	{ CCI_REG8(0x4800), 0x64 },
	{ CCI_REG8(0x4801), 0x0f },
	{ CCI_REG8(0x4806), 0x2f },
	{ CCI_REG8(0x4819), 0xaa },
	{ CCI_REG8(0x4823), 0x3e },
	{ CCI_REG8(0x5000), 0x85 },
	{ CCI_REG8(0x5e00), 0x0c },
	{ CCI_REG8(0x3899), 0x09 },
	{ CCI_REG8(0x4f00), 0x64 },
	{ CCI_REG8(0x4f02), 0x0a },
	{ CCI_REG8(0x4f05), 0x0e },
	{ CCI_REG8(0x4f06), 0x11 },
	{ CCI_REG8(0x4f08), 0x0b },
	{ CCI_REG8(0x4f0a), 0xc4 },
	{ CCI_REG8(0x4f20), 0x1f },
	{ CCI_REG8(0x4f25), 0x10 },
	{ CCI_REG8(0x3016), 0x10 },
	{ CCI_REG8(0x3017), 0x00 },
	{ CCI_REG8(0x3018), 0x00 },
	{ CCI_REG8(0x3019), 0x00 },
	{ CCI_REG8(0x301a), 0x00 },
	{ CCI_REG8(0x301b), 0x00 },
	{ CCI_REG8(0x301c), 0x72 },
	{ CCI_REG8(0x3037), 0x40 },
	{ CCI_REG8(0x4f2c), 0x00 },
	{ CCI_REG8(0x4f21), 0x00 },
	{ CCI_REG8(0x4f23), 0x00 },
	{ CCI_REG8(0x4f2a), 0x00 },
	{ CCI_REG8(0x3665), 0xe7 },
	{ CCI_REG8(0x3668), 0x48 },
	{ CCI_REG8(0x3671), 0x3c },
	{ CCI_REG8(0x389a), 0x02 },
	{ CCI_REG8(0x389b), 0x00 },
	{ CCI_REG8(0x303c), 0xa0 },
	{ CCI_REG8(0x300f), 0xf0 },
	{ CCI_REG8(0x304b), 0x0f },
	{ CCI_REG8(0x3662), 0x24 },
	{ CCI_REG8(0x3006), 0x40 },
	{ CCI_REG8(0x4f26), 0x45 },
	{ CCI_REG8(0x3607), 0x34 },
	{ CCI_REG8(0x3608), 0x01 },
	{ CCI_REG8(0x360a), 0x0c },
	{ CCI_REG8(0x360b), 0x86 },
	{ CCI_REG8(0x360c), 0xcc },
	{ CCI_REG8(0x3013), 0x00 },
	{ CCI_REG8(0x3083), 0x02 },
	{ CCI_REG8(0x3084), 0x12 },
	{ CCI_REG8(0x4601), 0x38 },
	{ CCI_REG8(0x366f), 0x3a },
	{ CCI_REG8(0x3713), 0x19 },
	{ CCI_REG8(0x37a2), 0x00 },
	{ CCI_REG8(0x3f43), 0x27 },
	{ CCI_REG8(0x3f45), 0x27 },
	{ CCI_REG8(0x3f47), 0x32 },
	{ CCI_REG8(0x3f49), 0x3e },
	{ CCI_REG8(0x3f4b), 0x20 },
	{ CCI_REG8(0x3f4d), 0x30 },
	{ CCI_REG8(0x4300), 0x3f },
	{ CCI_REG8(0x4009), 0x10 },
	{ CCI_REG8(0x3f02), 0x68 },
	{ CCI_REG8(0x3700), 0x8c },
	{ CCI_REG8(0x370b), 0x7e },
	{ CCI_REG8(0x3f47), 0x35 },
};

static const struct og0ve1b_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.hts = 792,
		.vts = 568,
		.bpp = 8,
		.reg_list = {
			.regs = og0ve1b_640x480_120fps_mode,
			.num_regs = ARRAY_SIZE(og0ve1b_640x480_120fps_mode),
		},
	},
};

static int og0ve1b_enable_test_pattern(struct og0ve1b *og0ve1b, u32 pattern)
{
	u64 val = og0ve1b->pre_isp;

	if (pattern)
		val |= OG0VE1B_TEST_PATTERN_ENABLE;
	else
		val &= ~OG0VE1B_TEST_PATTERN_ENABLE;

	return cci_write(og0ve1b->regmap, OG0VE1B_REG_PRE_ISP, val, NULL);
}

static int og0ve1b_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct og0ve1b *og0ve1b = container_of(ctrl->handler, struct og0ve1b,
					       ctrl_handler);
	int ret;

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(og0ve1b->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(og0ve1b->regmap, OG0VE1B_REG_ANALOGUE_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(og0ve1b->regmap, OG0VE1B_REG_EXPOSURE,
				ctrl->val << 4, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = og0ve1b_enable_test_pattern(og0ve1b, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(og0ve1b->dev);

	return ret;
}

static const struct v4l2_ctrl_ops og0ve1b_ctrl_ops = {
	.s_ctrl = og0ve1b_set_ctrl,
};

static int og0ve1b_init_controls(struct og0ve1b *og0ve1b)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &og0ve1b->ctrl_handler;
	const struct og0ve1b_mode *mode = &supported_modes[0];
	struct v4l2_fwnode_device_properties props;
	s64 exposure_max, pixel_rate, h_blank;
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &og0ve1b_ctrl_ops,
				      V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(og0ve1b_link_freq_menu) - 1,
				      0, og0ve1b_link_freq_menu);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = og0ve1b_link_freq_menu[0] / mode->bpp;
	v4l2_ctrl_new_std(ctrl_hdlr, &og0ve1b_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &og0ve1b_ctrl_ops, V4L2_CID_HBLANK,
				 h_blank, h_blank, 1, h_blank);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &og0ve1b_ctrl_ops, V4L2_CID_VBLANK,
				 mode->vts - mode->height,
				 mode->vts - mode->height, 1,
				 mode->vts - mode->height);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &og0ve1b_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OG0VE1B_ANALOGUE_GAIN_MIN, OG0VE1B_ANALOGUE_GAIN_MAX,
			  OG0VE1B_ANALOGUE_GAIN_STEP,
			  OG0VE1B_ANALOGUE_GAIN_DEFAULT);

	exposure_max = (mode->vts - OG0VE1B_EXPOSURE_MAX_MARGIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &og0ve1b_ctrl_ops,
			  V4L2_CID_EXPOSURE,
			  OG0VE1B_EXPOSURE_MIN, exposure_max,
			  OG0VE1B_EXPOSURE_STEP,
			  OG0VE1B_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &og0ve1b_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(og0ve1b_test_pattern_menu) - 1,
				     0, 0, og0ve1b_test_pattern_menu);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ret = v4l2_fwnode_device_parse(og0ve1b->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &og0ve1b_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	og0ve1b->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void og0ve1b_update_pad_format(const struct og0ve1b_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_Y8_1X8;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int og0ve1b_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	const struct og0ve1b_reg_list *reg_list = &supported_modes[0].reg_list;
	struct og0ve1b *og0ve1b = to_og0ve1b(sd);
	int ret;

	ret = pm_runtime_resume_and_get(og0ve1b->dev);
	if (ret)
		return ret;

	/* Skip a step of explicit entering into the standby mode */
	ret = cci_write(og0ve1b->regmap, OG0VE1B_REG_SOFTWARE_RST,
			OG0VE1B_SOFTWARE_RST, NULL);
	if (ret) {
		dev_err(og0ve1b->dev, "failed to software reset: %d\n", ret);
		goto error;
	}

	ret = cci_multi_reg_write(og0ve1b->regmap, reg_list->regs,
				  reg_list->num_regs, NULL);
	if (ret) {
		dev_err(og0ve1b->dev, "failed to set mode: %d\n", ret);
		goto error;
	}

	ret = __v4l2_ctrl_handler_setup(og0ve1b->sd.ctrl_handler);
	if (ret)
		goto error;

	ret = cci_write(og0ve1b->regmap, OG0VE1B_REG_MODE_SELECT,
			OG0VE1B_MODE_STREAMING, NULL);
	if (ret) {
		dev_err(og0ve1b->dev, "failed to start streaming: %d\n", ret);
		goto error;
	}

	return 0;

error:
	pm_runtime_put_autosuspend(og0ve1b->dev);

	return ret;
}

static int og0ve1b_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state, u32 pad,
				   u64 streams_mask)
{
	struct og0ve1b *og0ve1b = to_og0ve1b(sd);
	int ret;

	ret = cci_write(og0ve1b->regmap, OG0VE1B_REG_MODE_SELECT,
			OG0VE1B_MODE_STANDBY, NULL);
	if (ret)
		dev_err(og0ve1b->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(og0ve1b->dev);

	return ret;
}

static int og0ve1b_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	const struct og0ve1b_mode *mode;

	format = v4l2_subdev_state_get_format(state, 0);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width,
				      fmt->format.height);

	og0ve1b_update_pad_format(mode, &fmt->format);
	*format = fmt->format;

	return 0;
}

static int og0ve1b_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y8_1X8;

	return 0;
}

static int og0ve1b_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_Y8_1X8)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int og0ve1b_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = MEDIA_BUS_FMT_Y8_1X8,
			.width = supported_modes[0].width,
			.height = supported_modes[0].height,
		},
	};

	og0ve1b_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops og0ve1b_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops og0ve1b_pad_ops = {
	.set_fmt = og0ve1b_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = og0ve1b_enum_mbus_code,
	.enum_frame_size = og0ve1b_enum_frame_size,
	.enable_streams = og0ve1b_enable_streams,
	.disable_streams = og0ve1b_disable_streams,
};

static const struct v4l2_subdev_ops og0ve1b_subdev_ops = {
	.video = &og0ve1b_video_ops,
	.pad = &og0ve1b_pad_ops,
};

static const struct v4l2_subdev_internal_ops og0ve1b_internal_ops = {
	.init_state = og0ve1b_init_state,
};

static const struct media_entity_operations og0ve1b_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int og0ve1b_identify_sensor(struct og0ve1b *og0ve1b)
{
	u64 val;
	int ret;

	ret = cci_read(og0ve1b->regmap, OG0VE1B_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(og0ve1b->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != OG0VE1B_CHIP_ID) {
		dev_err(og0ve1b->dev, "chip id mismatch: %x!=%llx\n",
			OG0VE1B_CHIP_ID, val);
		return -ENODEV;
	}

	ret = cci_read(og0ve1b->regmap, OG0VE1B_REG_PRE_ISP,
		       &og0ve1b->pre_isp, NULL);
	if (ret)
		dev_err(og0ve1b->dev, "failed to read pre_isp: %d\n", ret);

	return ret;
}

static int og0ve1b_check_hwcfg(struct og0ve1b *og0ve1b)
{
	struct fwnode_handle *fwnode = dev_fwnode(og0ve1b->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	unsigned long freq_bitmap;
	int ret;

	if (!fwnode)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	ret = v4l2_link_freq_to_bitmap(og0ve1b->dev,
				       bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       og0ve1b_link_freq_menu,
				       ARRAY_SIZE(og0ve1b_link_freq_menu),
				       &freq_bitmap);

	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int og0ve1b_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct og0ve1b *og0ve1b = to_og0ve1b(sd);
	int ret;

	ret = regulator_bulk_enable(OG0VE1B_NUM_SUPPLIES, og0ve1b->supplies);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(og0ve1b->reset_gpio, 0);
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	ret = clk_prepare_enable(og0ve1b->xvclk);
	if (ret)
		goto reset_gpio;

	return 0;

reset_gpio:
	gpiod_set_value_cansleep(og0ve1b->reset_gpio, 1);

	regulator_bulk_disable(OG0VE1B_NUM_SUPPLIES, og0ve1b->supplies);

	return ret;
}

static int og0ve1b_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct og0ve1b *og0ve1b = to_og0ve1b(sd);

	clk_disable_unprepare(og0ve1b->xvclk);

	gpiod_set_value_cansleep(og0ve1b->reset_gpio, 1);

	regulator_bulk_disable(OG0VE1B_NUM_SUPPLIES, og0ve1b->supplies);

	return 0;
}

static int og0ve1b_probe(struct i2c_client *client)
{
	struct og0ve1b *og0ve1b;
	unsigned long freq;
	unsigned int i;
	int ret;

	og0ve1b = devm_kzalloc(&client->dev, sizeof(*og0ve1b), GFP_KERNEL);
	if (!og0ve1b)
		return -ENOMEM;

	og0ve1b->dev = &client->dev;

	v4l2_i2c_subdev_init(&og0ve1b->sd, client, &og0ve1b_subdev_ops);

	og0ve1b->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(og0ve1b->regmap))
		return dev_err_probe(og0ve1b->dev, PTR_ERR(og0ve1b->regmap),
				     "failed to init CCI\n");

	og0ve1b->xvclk = devm_v4l2_sensor_clk_get(og0ve1b->dev, NULL);
	if (IS_ERR(og0ve1b->xvclk))
		return dev_err_probe(og0ve1b->dev, PTR_ERR(og0ve1b->xvclk),
				     "failed to get XVCLK clock\n");

	freq = clk_get_rate(og0ve1b->xvclk);
	if (freq && freq != OG0VE1B_MCLK_FREQ_24MHZ)
		return dev_err_probe(og0ve1b->dev, -EINVAL,
				     "XVCLK clock frequency %lu is not supported\n",
				     freq);

	ret = og0ve1b_check_hwcfg(og0ve1b);
	if (ret)
		return dev_err_probe(og0ve1b->dev, ret,
				     "failed to check HW configuration\n");

	og0ve1b->reset_gpio = devm_gpiod_get_optional(og0ve1b->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(og0ve1b->reset_gpio))
		return dev_err_probe(og0ve1b->dev, PTR_ERR(og0ve1b->reset_gpio),
				     "cannot get reset GPIO\n");

	for (i = 0; i < OG0VE1B_NUM_SUPPLIES; i++)
		og0ve1b->supplies[i].supply = og0ve1b_supply_names[i];

	ret = devm_regulator_bulk_get(og0ve1b->dev, OG0VE1B_NUM_SUPPLIES,
				      og0ve1b->supplies);
	if (ret)
		return dev_err_probe(og0ve1b->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = og0ve1b_power_on(og0ve1b->dev);
	if (ret)
		return ret;

	ret = og0ve1b_identify_sensor(og0ve1b);
	if (ret) {
		dev_err_probe(og0ve1b->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	ret = og0ve1b_init_controls(og0ve1b);
	if (ret) {
		dev_err_probe(og0ve1b->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	og0ve1b->sd.state_lock = og0ve1b->ctrl_handler.lock;
	og0ve1b->sd.internal_ops = &og0ve1b_internal_ops;
	og0ve1b->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	og0ve1b->sd.entity.ops = &og0ve1b_subdev_entity_ops;
	og0ve1b->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	og0ve1b->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&og0ve1b->sd.entity, 1, &og0ve1b->pad);
	if (ret) {
		dev_err_probe(og0ve1b->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&og0ve1b->sd);
	if (ret < 0) {
		dev_err_probe(og0ve1b->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(og0ve1b->dev);
	pm_runtime_enable(og0ve1b->dev);

	ret = v4l2_async_register_subdev_sensor(&og0ve1b->sd);
	if (ret < 0) {
		dev_err_probe(og0ve1b->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_idle(og0ve1b->dev);
	pm_runtime_set_autosuspend_delay(og0ve1b->dev, 1000);
	pm_runtime_use_autosuspend(og0ve1b->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&og0ve1b->sd);
	pm_runtime_disable(og0ve1b->dev);
	pm_runtime_set_suspended(og0ve1b->dev);

media_entity_cleanup:
	media_entity_cleanup(&og0ve1b->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(og0ve1b->sd.ctrl_handler);

power_off:
	og0ve1b_power_off(og0ve1b->dev);

	return ret;
}

static void og0ve1b_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og0ve1b *og0ve1b = to_og0ve1b(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(og0ve1b->dev);

	if (!pm_runtime_status_suspended(og0ve1b->dev)) {
		og0ve1b_power_off(og0ve1b->dev);
		pm_runtime_set_suspended(og0ve1b->dev);
	}
}

static const struct dev_pm_ops og0ve1b_pm_ops = {
	SET_RUNTIME_PM_OPS(og0ve1b_power_off, og0ve1b_power_on, NULL)
};

static const struct of_device_id og0ve1b_of_match[] = {
	{ .compatible = "ovti,og0ve1b" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, og0ve1b_of_match);

static struct i2c_driver og0ve1b_i2c_driver = {
	.driver = {
		.name = "og0ve1b",
		.pm = &og0ve1b_pm_ops,
		.of_match_table = og0ve1b_of_match,
	},
	.probe = og0ve1b_probe,
	.remove = og0ve1b_remove,
};

module_i2c_driver(og0ve1b_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("OmniVision OG0VE1B sensor driver");
MODULE_LICENSE("GPL");
