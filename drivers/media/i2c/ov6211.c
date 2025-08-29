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

#define OV6211_LINK_FREQ_480MHZ		(480 * HZ_PER_MHZ)
#define OV6211_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)

#define OV6211_REG_CHIP_ID		CCI_REG16(0x300a)
#define OV6211_CHIP_ID			0x6710

#define OV6211_REG_MODE_SELECT		CCI_REG8(0x0100)
#define OV6211_MODE_STANDBY		0x00
#define OV6211_MODE_STREAMING		BIT(0)

#define OV6211_REG_SOFTWARE_RST		CCI_REG8(0x0103)
#define OV6211_SOFTWARE_RST		BIT(0)

/* Exposure controls from sensor */
#define OV6211_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV6211_EXPOSURE_MIN		1
#define OV6211_EXPOSURE_MAX_MARGIN	4
#define OV6211_EXPOSURE_STEP		1
#define OV6211_EXPOSURE_DEFAULT		210

/* Analogue gain controls from sensor */
#define OV6211_REG_ANALOGUE_GAIN	CCI_REG16(0x350a)
#define OV6211_ANALOGUE_GAIN_MIN	1
#define OV6211_ANALOGUE_GAIN_MAX	0x3ff
#define OV6211_ANALOGUE_GAIN_STEP	1
#define OV6211_ANALOGUE_GAIN_DEFAULT	160

/* Test pattern */
#define OV6211_REG_PRE_ISP		CCI_REG8(0x5e00)
#define OV6211_TEST_PATTERN_ENABLE	BIT(7)

#define to_ov6211(_sd)			container_of(_sd, struct ov6211, sd)

static const s64 ov6211_link_freq_menu[] = {
	OV6211_LINK_FREQ_480MHZ,
};

struct ov6211_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct ov6211_mode {
	u32 width;	/* Frame width in pixels */
	u32 height;	/* Frame height in pixels */
	u32 hts;	/* Horizontal timing size */
	u32 vts;	/* Default vertical timing size */
	u32 bpp;	/* Bits per pixel */

	const struct ov6211_reg_list reg_list;	/* Sensor register setting */
};

static const char * const ov6211_test_pattern_menu[] = {
	"Disabled",
	"Vertical Colour Bars",
};

static const char * const ov6211_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV6211_NUM_SUPPLIES	ARRAY_SIZE(ov6211_supply_names)

struct ov6211 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[OV6211_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;

	/* Saved register values */
	u64 pre_isp;
};

static const struct cci_reg_sequence ov6211_400x400_120fps_mode[] = {
	{ CCI_REG8(0x3005), 0x00 },
	{ CCI_REG8(0x3013), 0x12 },
	{ CCI_REG8(0x3014), 0x04 },
	{ CCI_REG8(0x3016), 0x10 },
	{ CCI_REG8(0x3017), 0x00 },
	{ CCI_REG8(0x3018), 0x00 },
	{ CCI_REG8(0x301a), 0x00 },
	{ CCI_REG8(0x301b), 0x00 },
	{ CCI_REG8(0x301c), 0x00 },
	{ CCI_REG8(0x3037), 0xf0 },
	{ CCI_REG8(0x3080), 0x01 },
	{ CCI_REG8(0x3081), 0x00 },
	{ CCI_REG8(0x3082), 0x01 },
	{ CCI_REG8(0x3098), 0x04 },
	{ CCI_REG8(0x3099), 0x28 },
	{ CCI_REG8(0x309a), 0x06 },
	{ CCI_REG8(0x309b), 0x04 },
	{ CCI_REG8(0x309c), 0x00 },
	{ CCI_REG8(0x309d), 0x00 },
	{ CCI_REG8(0x309e), 0x01 },
	{ CCI_REG8(0x309f), 0x00 },
	{ CCI_REG8(0x30b0), 0x08 },
	{ CCI_REG8(0x30b1), 0x02 },
	{ CCI_REG8(0x30b2), 0x00 },
	{ CCI_REG8(0x30b3), 0x28 },
	{ CCI_REG8(0x30b4), 0x02 },
	{ CCI_REG8(0x30b5), 0x00 },
	{ CCI_REG8(0x3106), 0xd9 },
	{ CCI_REG8(0x3503), 0x07 },
	{ CCI_REG8(0x3509), 0x10 },
	{ CCI_REG8(0x3600), 0xfc },
	{ CCI_REG8(0x3620), 0xb7 },
	{ CCI_REG8(0x3621), 0x05 },
	{ CCI_REG8(0x3626), 0x31 },
	{ CCI_REG8(0x3627), 0x40 },
	{ CCI_REG8(0x3632), 0xa3 },
	{ CCI_REG8(0x3633), 0x34 },
	{ CCI_REG8(0x3634), 0x40 },
	{ CCI_REG8(0x3636), 0x00 },
	{ CCI_REG8(0x3660), 0x80 },
	{ CCI_REG8(0x3662), 0x03 },
	{ CCI_REG8(0x3664), 0xf0 },
	{ CCI_REG8(0x366a), 0x10 },
	{ CCI_REG8(0x366b), 0x06 },
	{ CCI_REG8(0x3680), 0xf4 },
	{ CCI_REG8(0x3681), 0x50 },
	{ CCI_REG8(0x3682), 0x00 },
	{ CCI_REG8(0x3708), 0x20 },
	{ CCI_REG8(0x3709), 0x40 },
	{ CCI_REG8(0x370d), 0x03 },
	{ CCI_REG8(0x373b), 0x02 },
	{ CCI_REG8(0x373c), 0x08 },
	{ CCI_REG8(0x3742), 0x00 },
	{ CCI_REG8(0x3744), 0x16 },
	{ CCI_REG8(0x3745), 0x08 },
	{ CCI_REG8(0x3781), 0xfc },
	{ CCI_REG8(0x3788), 0x00 },
	{ CCI_REG8(0x3800), 0x00 },
	{ CCI_REG8(0x3801), 0x04 },
	{ CCI_REG8(0x3802), 0x00 },
	{ CCI_REG8(0x3803), 0x04 },
	{ CCI_REG8(0x3804), 0x01 },
	{ CCI_REG8(0x3805), 0x9b },
	{ CCI_REG8(0x3806), 0x01 },
	{ CCI_REG8(0x3807), 0x9b },
	{ CCI_REG8(0x3808), 0x01 },	/* output width */
	{ CCI_REG8(0x3809), 0x90 },
	{ CCI_REG8(0x380a), 0x01 },	/* output height */
	{ CCI_REG8(0x380b), 0x90 },
	{ CCI_REG8(0x380c), 0x05 },	/* horizontal timing size */
	{ CCI_REG8(0x380d), 0xf2 },
	{ CCI_REG8(0x380e), 0x01 },	/* vertical timing size */
	{ CCI_REG8(0x380f), 0xb6 },
	{ CCI_REG8(0x3810), 0x00 },
	{ CCI_REG8(0x3811), 0x04 },
	{ CCI_REG8(0x3812), 0x00 },
	{ CCI_REG8(0x3813), 0x04 },
	{ CCI_REG8(0x3814), 0x11 },
	{ CCI_REG8(0x3815), 0x11 },
	{ CCI_REG8(0x3820), 0x00 },
	{ CCI_REG8(0x3821), 0x00 },
	{ CCI_REG8(0x382b), 0xfa },
	{ CCI_REG8(0x382f), 0x04 },
	{ CCI_REG8(0x3832), 0x00 },
	{ CCI_REG8(0x3833), 0x05 },
	{ CCI_REG8(0x3834), 0x00 },
	{ CCI_REG8(0x3835), 0x05 },
	{ CCI_REG8(0x3882), 0x04 },
	{ CCI_REG8(0x3883), 0x00 },
	{ CCI_REG8(0x38a4), 0x10 },
	{ CCI_REG8(0x38a5), 0x00 },
	{ CCI_REG8(0x38b1), 0x03 },
	{ CCI_REG8(0x3b80), 0x00 },
	{ CCI_REG8(0x3b81), 0xff },
	{ CCI_REG8(0x3b82), 0x10 },
	{ CCI_REG8(0x3b83), 0x00 },
	{ CCI_REG8(0x3b84), 0x08 },
	{ CCI_REG8(0x3b85), 0x00 },
	{ CCI_REG8(0x3b86), 0x01 },
	{ CCI_REG8(0x3b87), 0x00 },
	{ CCI_REG8(0x3b88), 0x00 },
	{ CCI_REG8(0x3b89), 0x00 },
	{ CCI_REG8(0x3b8a), 0x00 },
	{ CCI_REG8(0x3b8b), 0x05 },
	{ CCI_REG8(0x3b8c), 0x00 },
	{ CCI_REG8(0x3b8d), 0x00 },
	{ CCI_REG8(0x3b8e), 0x01 },
	{ CCI_REG8(0x3b8f), 0xb2 },
	{ CCI_REG8(0x3b94), 0x05 },
	{ CCI_REG8(0x3b95), 0xf2 },
	{ CCI_REG8(0x3b96), 0xc0 },
	{ CCI_REG8(0x4004), 0x04 },
	{ CCI_REG8(0x404e), 0x01 },
	{ CCI_REG8(0x4801), 0x0f },
	{ CCI_REG8(0x4806), 0x0f },
	{ CCI_REG8(0x4837), 0x43 },
	{ CCI_REG8(0x5a08), 0x00 },
	{ CCI_REG8(0x5a01), 0x00 },
	{ CCI_REG8(0x5a03), 0x00 },
	{ CCI_REG8(0x5a04), 0x10 },
	{ CCI_REG8(0x5a05), 0xa0 },
	{ CCI_REG8(0x5a06), 0x0c },
	{ CCI_REG8(0x5a07), 0x78 },
};

static const struct ov6211_mode supported_modes[] = {
	{
		.width = 400,
		.height = 400,
		.hts = 1522,
		.vts = 438,
		.bpp = 8,
		.reg_list = {
			.regs = ov6211_400x400_120fps_mode,
			.num_regs = ARRAY_SIZE(ov6211_400x400_120fps_mode),
		},
	},
};

static int ov6211_set_test_pattern(struct ov6211 *ov6211, u32 pattern)
{
	u64 val = ov6211->pre_isp;

	if (pattern)
		val |= OV6211_TEST_PATTERN_ENABLE;
	else
		val &= ~OV6211_TEST_PATTERN_ENABLE;

	return cci_write(ov6211->regmap, OV6211_REG_PRE_ISP, val, NULL);
}

static int ov6211_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov6211 *ov6211 = container_of(ctrl->handler, struct ov6211,
					     ctrl_handler);
	int ret;

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(ov6211->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(ov6211->regmap, OV6211_REG_ANALOGUE_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(ov6211->regmap, OV6211_REG_EXPOSURE,
				ctrl->val << 4, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov6211_set_test_pattern(ov6211, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(ov6211->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov6211_ctrl_ops = {
	.s_ctrl = ov6211_set_ctrl,
};

static int ov6211_init_controls(struct ov6211 *ov6211)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &ov6211->ctrl_handler;
	const struct ov6211_mode *mode = &supported_modes[0];
	struct v4l2_fwnode_device_properties props;
	s64 exposure_max, pixel_rate, h_blank;
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov6211_ctrl_ops,
				      V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(ov6211_link_freq_menu) - 1,
				      0, ov6211_link_freq_menu);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = ov6211_link_freq_menu[0] / mode->bpp;
	v4l2_ctrl_new_std(ctrl_hdlr, &ov6211_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &ov6211_ctrl_ops, V4L2_CID_HBLANK,
				 h_blank, h_blank, 1, h_blank);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(ctrl_hdlr, &ov6211_ctrl_ops, V4L2_CID_VBLANK,
				 mode->vts - mode->height,
				 mode->vts - mode->height, 1,
				 mode->vts - mode->height);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov6211_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV6211_ANALOGUE_GAIN_MIN, OV6211_ANALOGUE_GAIN_MAX,
			  OV6211_ANALOGUE_GAIN_STEP,
			  OV6211_ANALOGUE_GAIN_DEFAULT);

	exposure_max = (mode->vts - OV6211_EXPOSURE_MAX_MARGIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov6211_ctrl_ops,
			  V4L2_CID_EXPOSURE,
			  OV6211_EXPOSURE_MIN, exposure_max,
			  OV6211_EXPOSURE_STEP,
			  OV6211_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov6211_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov6211_test_pattern_menu) - 1,
				     0, 0, ov6211_test_pattern_menu);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ret = v4l2_fwnode_device_parse(ov6211->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov6211_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	ov6211->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void ov6211_update_pad_format(const struct ov6211_mode *mode,
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

static int ov6211_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	const struct ov6211_reg_list *reg_list = &supported_modes[0].reg_list;
	struct ov6211 *ov6211 = to_ov6211(sd);
	int ret;

	ret = pm_runtime_resume_and_get(ov6211->dev);
	if (ret)
		return ret;

	/* Skip a step of explicit entering into the standby mode */
	ret = cci_write(ov6211->regmap, OV6211_REG_SOFTWARE_RST,
			OV6211_SOFTWARE_RST, NULL);
	if (ret) {
		dev_err(ov6211->dev, "failed to software reset: %d\n", ret);
		goto error;
	}

	ret = cci_multi_reg_write(ov6211->regmap, reg_list->regs,
				  reg_list->num_regs, NULL);
	if (ret) {
		dev_err(ov6211->dev, "failed to set mode: %d\n", ret);
		goto error;
	}

	ret = __v4l2_ctrl_handler_setup(ov6211->sd.ctrl_handler);
	if (ret)
		goto error;

	ret = cci_write(ov6211->regmap, OV6211_REG_MODE_SELECT,
			OV6211_MODE_STREAMING, NULL);
	if (ret) {
		dev_err(ov6211->dev, "failed to start streaming: %d\n", ret);
		goto error;
	}

	return 0;

error:
	pm_runtime_put_autosuspend(ov6211->dev);

	return ret;
}

static int ov6211_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct ov6211 *ov6211 = to_ov6211(sd);
	int ret;

	ret = cci_write(ov6211->regmap, OV6211_REG_MODE_SELECT,
			OV6211_MODE_STANDBY, NULL);
	if (ret)
		dev_err(ov6211->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(ov6211->dev);

	return ret;
}

static int ov6211_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	const struct ov6211_mode *mode;

	format = v4l2_subdev_state_get_format(state, 0);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width,
				      fmt->format.height);

	ov6211_update_pad_format(mode, &fmt->format);
	*format = fmt->format;

	return 0;
}

static int ov6211_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y8_1X8;

	return 0;
}

static int ov6211_enum_frame_size(struct v4l2_subdev *sd,
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

static int ov6211_init_state(struct v4l2_subdev *sd,
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

	ov6211_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops ov6211_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops ov6211_pad_ops = {
	.set_fmt = ov6211_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = ov6211_enum_mbus_code,
	.enum_frame_size = ov6211_enum_frame_size,
	.enable_streams = ov6211_enable_streams,
	.disable_streams = ov6211_disable_streams,
};

static const struct v4l2_subdev_ops ov6211_subdev_ops = {
	.video = &ov6211_video_ops,
	.pad = &ov6211_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov6211_internal_ops = {
	.init_state = ov6211_init_state,
};

static const struct media_entity_operations ov6211_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ov6211_identify_sensor(struct ov6211 *ov6211)
{
	u64 val;
	int ret;

	ret = cci_read(ov6211->regmap, OV6211_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(ov6211->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != OV6211_CHIP_ID) {
		dev_err(ov6211->dev, "chip id mismatch: %x!=%llx\n",
			OV6211_CHIP_ID, val);
		return -ENODEV;
	}

	ret = cci_read(ov6211->regmap, OV6211_REG_PRE_ISP,
		       &ov6211->pre_isp, NULL);
	if (ret)
		dev_err(ov6211->dev, "failed to read pre_isp: %d\n", ret);

	return ret;
}

static int ov6211_check_hwcfg(struct ov6211 *ov6211)
{
	struct fwnode_handle *fwnode = dev_fwnode(ov6211->dev), *ep;
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

	ret = v4l2_link_freq_to_bitmap(ov6211->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       ov6211_link_freq_menu,
				       ARRAY_SIZE(ov6211_link_freq_menu),
				       &freq_bitmap);

	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov6211_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov6211 *ov6211 = to_ov6211(sd);
	int ret;

	ret = regulator_bulk_enable(OV6211_NUM_SUPPLIES, ov6211->supplies);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(ov6211->reset_gpio, 0);
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	ret = clk_prepare_enable(ov6211->xvclk);
	if (ret)
		goto reset_gpio;

	return 0;

reset_gpio:
	gpiod_set_value_cansleep(ov6211->reset_gpio, 1);

	regulator_bulk_disable(OV6211_NUM_SUPPLIES, ov6211->supplies);

	return ret;
}

static int ov6211_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov6211 *ov6211 = to_ov6211(sd);

	clk_disable_unprepare(ov6211->xvclk);

	gpiod_set_value_cansleep(ov6211->reset_gpio, 1);

	regulator_bulk_disable(OV6211_NUM_SUPPLIES, ov6211->supplies);

	return 0;
}

static int ov6211_probe(struct i2c_client *client)
{
	struct ov6211 *ov6211;
	unsigned long freq;
	unsigned int i;
	int ret;

	ov6211 = devm_kzalloc(&client->dev, sizeof(*ov6211), GFP_KERNEL);
	if (!ov6211)
		return -ENOMEM;

	ov6211->dev = &client->dev;

	v4l2_i2c_subdev_init(&ov6211->sd, client, &ov6211_subdev_ops);

	ov6211->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ov6211->regmap))
		return dev_err_probe(ov6211->dev, PTR_ERR(ov6211->regmap),
				     "failed to init CCI\n");

	ov6211->xvclk = devm_v4l2_sensor_clk_get(ov6211->dev, NULL);
	if (IS_ERR(ov6211->xvclk))
		return dev_err_probe(ov6211->dev, PTR_ERR(ov6211->xvclk),
				     "failed to get XVCLK clock\n");

	freq = clk_get_rate(ov6211->xvclk);
	if (freq && freq != OV6211_MCLK_FREQ_24MHZ)
		return dev_err_probe(ov6211->dev, -EINVAL,
				     "XVCLK clock frequency %lu is not supported\n",
				     freq);

	ret = ov6211_check_hwcfg(ov6211);
	if (ret)
		return dev_err_probe(ov6211->dev, ret,
				     "failed to check HW configuration\n");

	ov6211->reset_gpio = devm_gpiod_get_optional(ov6211->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(ov6211->reset_gpio))
		return dev_err_probe(ov6211->dev, PTR_ERR(ov6211->reset_gpio),
				     "cannot get reset GPIO\n");

	for (i = 0; i < OV6211_NUM_SUPPLIES; i++)
		ov6211->supplies[i].supply = ov6211_supply_names[i];

	ret = devm_regulator_bulk_get(ov6211->dev, OV6211_NUM_SUPPLIES,
				      ov6211->supplies);
	if (ret)
		return dev_err_probe(ov6211->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = ov6211_power_on(ov6211->dev);
	if (ret)
		return ret;

	ret = ov6211_identify_sensor(ov6211);
	if (ret) {
		dev_err_probe(ov6211->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	ret = ov6211_init_controls(ov6211);
	if (ret) {
		dev_err_probe(ov6211->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	ov6211->sd.state_lock = ov6211->ctrl_handler.lock;
	ov6211->sd.internal_ops = &ov6211_internal_ops;
	ov6211->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov6211->sd.entity.ops = &ov6211_subdev_entity_ops;
	ov6211->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov6211->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov6211->sd.entity, 1, &ov6211->pad);
	if (ret) {
		dev_err_probe(ov6211->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&ov6211->sd);
	if (ret < 0) {
		dev_err_probe(ov6211->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(ov6211->dev);
	pm_runtime_enable(ov6211->dev);

	ret = v4l2_async_register_subdev_sensor(&ov6211->sd);
	if (ret < 0) {
		dev_err_probe(ov6211->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_idle(ov6211->dev);
	pm_runtime_set_autosuspend_delay(ov6211->dev, 1000);
	pm_runtime_use_autosuspend(ov6211->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&ov6211->sd);
	pm_runtime_disable(ov6211->dev);
	pm_runtime_set_suspended(ov6211->dev);

media_entity_cleanup:
	media_entity_cleanup(&ov6211->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov6211->sd.ctrl_handler);

power_off:
	ov6211_power_off(ov6211->dev);

	return ret;
}

static void ov6211_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov6211 *ov6211 = to_ov6211(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(ov6211->dev);

	if (!pm_runtime_status_suspended(ov6211->dev)) {
		ov6211_power_off(ov6211->dev);
		pm_runtime_set_suspended(ov6211->dev);
	}
}

static const struct dev_pm_ops ov6211_pm_ops = {
	SET_RUNTIME_PM_OPS(ov6211_power_off, ov6211_power_on, NULL)
};

static const struct of_device_id ov6211_of_match[] = {
	{ .compatible = "ovti,ov6211" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov6211_of_match);

static struct i2c_driver ov6211_i2c_driver = {
	.driver = {
		.name = "ov6211",
		.pm = &ov6211_pm_ops,
		.of_match_table = ov6211_of_match,
	},
	.probe = ov6211_probe,
	.remove = ov6211_remove,
};

module_i2c_driver(ov6211_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("OmniVision OV6211 sensor driver");
MODULE_LICENSE("GPL");
