// SPDX-License-Identifier: GPL-2.0
/*
 * imx471.c - imx471 sensor driver
 *
 * Copyright (C) 2025 Intel Corporation
 * Copyright (C) 2026 Kate Hsuan <hpa@redhat.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>

#define IMX471_REG_MODE_SELECT			CCI_REG8(0x0100)
#define IMX471_MODE_STANDBY			0x00
#define IMX471_MODE_STREAMING			0x01

/* Chip ID */
#define IMX471_REG_CHIP_ID			CCI_REG16(0x0016)
#define IMX471_CHIP_ID				0x0471

/* V_TIMING internal */
#define IMX471_REG_FLL				CCI_REG16(0x0340)
#define IMX471_FLL_MAX				0xffff

/* Exposure control */
#define IMX471_REG_EXPOSURE			CCI_REG16(0x0202)
#define IMX471_EXPOSURE_MIN			1
#define IMX471_EXPOSURE_STEP			1
#define IMX471_EXPOSURE_DEFAULT			1270

/* Default exposure margin */
#define IMX471_EXPOSURE_MARGIN			18

/* Analog gain control */
#define IMX471_REG_ANALOG_GAIN			CCI_REG16(0x0204)
#define IMX471_ANA_GAIN_MIN			0
#define IMX471_ANA_GAIN_MAX			800
#define IMX471_ANA_GAIN_STEP			1
#define IMX471_ANA_GAIN_DEFAULT			0

/* Digital gain control */
#define IMX471_REG_DPGA_USE_GLOBAL_GAIN		CCI_REG16(0x3ff9)
#define IMX471_REG_DIG_GAIN_GLOBAL		CCI_REG16(0x020e)
#define IMX471_DGTL_GAIN_MIN			256
#define IMX471_DGTL_GAIN_MAX			4095
#define IMX471_DGTL_GAIN_STEP			1
#define IMX471_DGTL_GAIN_DEFAULT		256

/* HFLIP and VFLIP control */
#define IMX471_REG_ORIENTATION			CCI_REG8(0x0101)

/* Test Pattern Control */
#define IMX471_REG_TEST_PATTERN			CCI_REG8(0x0600)

/* default link frequency and external clock */
#define IMX471_LINK_FREQ_DEFAULT		200000000LL
#define IMX471_EXT_CLK				19200000

/* PLL */
#define IMX471_REG_VTPXCK_DIV			CCI_REG8(0x0301)
#define IMX471_REG_VTSYCK_DIV			CCI_REG8(0x0303)
#define IMX471_REG_PREPLLCK_VT_DIV		CCI_REG8(0x0305)
#define IMX471_REG_PLL_VT_MPY			CCI_REG16(0x0306)
#define IMX471_REG_OPPXCK_DIV			CCI_REG8(0x0309)
#define IMX471_REG_OPSYCK_DIV			CCI_REG8(0x030b)
#define IMX471_REG_PLL_MULT_DRIV		CCI_REG8(0x0310)
#define IMX471_PLL_SINGLE			0
#define IMX471_PLL_DUAL				1

/* IMX471 native and active pixel array size */
#define IMX471_NATIVE_WIDTH			4672
#define IMX471_NATIVE_HEIGHT			3512
#define IMX471_PIXEL_ARRAY_LEFT			8
#define IMX471_PIXEL_ARRAY_TOP			8
#define IMX471_PIXEL_ARRAY_WIDTH		4656
#define IMX471_PIXEL_ARRAY_HEIGHT		3496

#define IMX471_REG_EXCK_FREQ			CCI_REG16(0x0136)

#define IMX471_REG_CSI_DATA_FORMAT		CCI_REG16(0x0112)
#define IMX471_CSI_DATA_FORMAT_RAW10		0x0a0a

#define IMX471_REG_CSI_LANE_MODE		CCI_REG8(0x0114)
#define IMX471_CSI_2_LANE_MODE			1
#define IMX471_CSI_4_LANE_MODE			3

#define IMX471_REG_X_ADD_STA			CCI_REG16(0x0344)
#define IMX471_REG_Y_ADD_STA			CCI_REG16(0x0346)
#define IMX471_REG_X_ADD_END			CCI_REG16(0x0348)
#define IMX471_REG_Y_ADD_END			CCI_REG16(0x034a)
#define IMX471_REG_X_OUTPUT_SIZE		CCI_REG16(0x034c)
#define IMX471_REG_Y_OUTPUT_SIZE		CCI_REG16(0x034e)
#define IMX471_REG_X_EVEN_INC			CCI_REG8(0x0381)
#define IMX471_REG_X_ODD_INC			CCI_REG8(0x0383)
#define IMX471_REG_Y_EVEN_INC			CCI_REG8(0x0385)
#define IMX471_REG_Y_ODD_INC			CCI_REG8(0x0387)

#define IMX471_REG_DIG_CROP_X_OFFSET		CCI_REG16(0x0408)
#define IMX471_REG_DIG_CROP_Y_OFFSET		CCI_REG16(0x040a)
#define IMX471_REG_DIG_CROP_WIDTH		CCI_REG16(0x040c)
#define IMX471_REG_DIG_CROP_HEIGHT		CCI_REG16(0x040e)

/* Binning mode */
#define IMX471_REG_BINNING_MODE			CCI_REG8(0x0900)
#define IMX471_BINNING_NONE			0
#define IMX471_BINNING_ENABLE			1
#define IMX471_REG_BINNING_TYPE			CCI_REG8(0x0901)
#define IMX471_REG_BINNING_WEIGHTING		CCI_REG8(0x0902)

#define to_imx471(_sd) container_of_const(_sd, struct imx471, sd)

static const char * const imx471_supply_name[] = {
	"vana",
};

struct imx471_mode {
	u32 width;
	u32 height;

	/* V-timing */
	u32 fll_def;
	u32 fll_min;

	/* H-timing */
	u32 llp;

	const struct cci_reg_sequence *default_mode_regs;
	unsigned int default_mode_regs_length;
};

struct imx471 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *exposure;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx471_supply_name)];
	struct clk *img_clk;

	struct device *dev;
	struct regmap *regmap;
};

static const struct cci_reg_sequence imx471_global_regs[] = {
	{ IMX471_REG_EXCK_FREQ, 0x1333 },
	{ CCI_REG8(0x3c7e), 0x08 },
	{ CCI_REG8(0x3c7f), 0x05 },
	{ CCI_REG8(0x3e35), 0x00 },
	{ CCI_REG8(0x3e36), 0x00 },
	{ CCI_REG8(0x3e37), 0x00 },
	{ CCI_REG8(0x3f7f), 0x01 },
	{ CCI_REG8(0x4431), 0x04 },
	{ CCI_REG8(0x531c), 0x01 },
	{ CCI_REG8(0x531d), 0x02 },
	{ CCI_REG8(0x531e), 0x04 },
	{ CCI_REG8(0x5928), 0x00 },
	{ CCI_REG8(0x5929), 0x2f },
	{ CCI_REG8(0x592a), 0x00 },
	{ CCI_REG8(0x592b), 0x85 },
	{ CCI_REG8(0x592c), 0x00 },
	{ CCI_REG8(0x592d), 0x32 },
	{ CCI_REG8(0x592e), 0x00 },
	{ CCI_REG8(0x592f), 0x88 },
	{ CCI_REG8(0x5930), 0x00 },
	{ CCI_REG8(0x5931), 0x3d },
	{ CCI_REG8(0x5932), 0x00 },
	{ CCI_REG8(0x5933), 0x93 },
	{ CCI_REG8(0x5938), 0x00 },
	{ CCI_REG8(0x5939), 0x24 },
	{ CCI_REG8(0x593a), 0x00 },
	{ CCI_REG8(0x593b), 0x7a },
	{ CCI_REG8(0x593c), 0x00 },
	{ CCI_REG8(0x593d), 0x24 },
	{ CCI_REG8(0x593e), 0x00 },
	{ CCI_REG8(0x593f), 0x7a },
	{ CCI_REG8(0x5940), 0x00 },
	{ CCI_REG8(0x5941), 0x2f },
	{ CCI_REG8(0x5942), 0x00 },
	{ CCI_REG8(0x5943), 0x85 },
	{ CCI_REG8(0x5f0e), 0x6e },
	{ CCI_REG8(0x5f11), 0xc6 },
	{ CCI_REG8(0x5f17), 0x5e },
	{ CCI_REG8(0x7990), 0x01 },
	{ CCI_REG8(0x7993), 0x5d },
	{ CCI_REG8(0x7994), 0x5d },
	{ CCI_REG8(0x7995), 0xa1 },
	{ CCI_REG8(0x799a), 0x01 },
	{ CCI_REG8(0x799d), 0x00 },
	{ CCI_REG8(0x8169), 0x01 },
	{ CCI_REG8(0x8359), 0x01 },
	{ CCI_REG8(0x9302), 0x1e },
	{ CCI_REG8(0x9306), 0x1f },
	{ CCI_REG8(0x930a), 0x26 },
	{ CCI_REG8(0x930e), 0x23 },
	{ CCI_REG8(0x9312), 0x23 },
	{ CCI_REG8(0x9316), 0x2c },
	{ CCI_REG8(0x9317), 0x19 },
	{ CCI_REG8(0xb046), 0x01 },
	{ CCI_REG8(0xb048), 0x01 },
};

static const struct cci_reg_sequence mode_1928x1088_regs[] = {
	{ IMX471_REG_CSI_DATA_FORMAT, IMX471_CSI_DATA_FORMAT_RAW10 },
	{ IMX471_REG_CSI_LANE_MODE, IMX471_CSI_4_LANE_MODE },
	{ IMX471_REG_X_ADD_STA, 8 },
	{ IMX471_REG_Y_ADD_STA, 408 },
	{ IMX471_REG_X_ADD_END, 4647 },
	{ IMX471_REG_Y_ADD_END, 3051 },
	{ IMX471_REG_X_EVEN_INC, 1 },
	{ IMX471_REG_X_ODD_INC, 1 },
	{ IMX471_REG_Y_EVEN_INC, 1 },
	{ IMX471_REG_Y_ODD_INC, 1 },
	{ IMX471_REG_BINNING_MODE, IMX471_BINNING_ENABLE },
	{ IMX471_REG_BINNING_TYPE, 0x22 },
	{ IMX471_REG_BINNING_WEIGHTING, 0x08 },
	{ IMX471_REG_DIG_CROP_X_OFFSET, 208 },
	{ IMX471_REG_DIG_CROP_Y_OFFSET, 108 },
	{ IMX471_REG_DIG_CROP_WIDTH, 1928 },
	{ IMX471_REG_DIG_CROP_HEIGHT, 1088 },
	{ IMX471_REG_X_OUTPUT_SIZE, 1928 },
	{ IMX471_REG_Y_OUTPUT_SIZE, 1088 },
	{ IMX471_REG_VTPXCK_DIV, 0x06 },
	{ IMX471_REG_VTSYCK_DIV, 0x02 },
	{ IMX471_REG_PREPLLCK_VT_DIV, 0x02 },
	{ IMX471_REG_PLL_VT_MPY, 0x0079 },
	{ IMX471_REG_OPSYCK_DIV, 0x01 },
	{ CCI_REG8(0x030d), 0x02 },
	{ CCI_REG8(0x030e), 0x00 },
	{ CCI_REG8(0x030f), 0x53 },
	{ IMX471_REG_PLL_MULT_DRIV, IMX471_PLL_DUAL },
	{ CCI_REG8(0x3f4c), 0x81 },
	{ CCI_REG8(0x3f4d), 0x81 },
	{ CCI_REG8(0x3f78), 0x01 },
	{ CCI_REG8(0x3f79), 0x31 },
	{ CCI_REG8(0x3ffe), 0x00 },
	{ CCI_REG8(0x3fff), 0x8a },
	{ CCI_REG8(0x5f0a), 0xb6 },
};

static const char * const imx471_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

static const s64 link_freq_menu_items[] = {
	IMX471_LINK_FREQ_DEFAULT,
};

/*
 * The Bayer formats for the flipping.
 * - no flip
 * - h flip
 * - v flip
 * - h and v flips
 */
static const u32 imx471_hv_flips_bayer_order[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

static const struct imx471_mode imx471_modes[] = {
	{
		.width = 1928,
		.height = 1088,
		.fll_def = 1308,
		.fll_min = 1308,
		.llp = 2328,
		.default_mode_regs = mode_1928x1088_regs,
		.default_mode_regs_length = ARRAY_SIZE(mode_1928x1088_regs),
	},
};

static int imx471_get_regulators(struct device *dev, struct imx471 *sensor)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(imx471_supply_name); i++)
		sensor->supplies[i].supply = imx471_supply_name[i];

	return devm_regulator_bulk_get(dev, ARRAY_SIZE(imx471_supply_name),
				       sensor->supplies);
}

static int imx471_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx471 *sensor = container_of_const(ctrl->handler,
						   struct imx471,
						   ctrl_handler);
	struct v4l2_subdev_state *state =
			v4l2_subdev_get_locked_active_state(&sensor->sd);
	const struct v4l2_mbus_framefmt *format =
			v4l2_subdev_state_get_format(state, 0);
	int ret;

	if (ctrl->id == V4L2_CID_VBLANK) {
		s64 exposure_max = format->height + ctrl->val -
				   IMX471_EXPOSURE_MARGIN;
		ret = __v4l2_ctrl_modify_range(sensor->exposure,
					       sensor->exposure->minimum,
					       exposure_max,
					       sensor->exposure->step,
					       exposure_max);
		if (ret)
			return ret;
	}

	if (!pm_runtime_get_if_in_use(sensor->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(sensor->regmap, IMX471_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(sensor->regmap, IMX471_REG_DIG_GAIN_GLOBAL,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(sensor->regmap, IMX471_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = cci_write(sensor->regmap, IMX471_REG_FLL,
				format->height + ctrl->val, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(sensor->regmap, IMX471_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = cci_write(sensor->regmap, IMX471_REG_ORIENTATION,
				sensor->hflip->val | sensor->vflip->val << 1,
				NULL);
		break;
	default:
		ret = -EINVAL;
		dev_err(sensor->dev, "ctrl(id:0x%x,val:0x%x) is not handled\n",
			ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(sensor->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx471_ctrl_ops = {
	.s_ctrl = imx471_set_ctrl,
};

static u32 imx471_get_format_code(struct imx471 *sensor)
{
	unsigned int i;

	i = (sensor->vflip->val ? 2 : 0) | (sensor->hflip->val ? 1 : 0);

	return imx471_hv_flips_bayer_order[i];
}

static int imx471_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx471 *sensor = to_imx471(sd);

	if (code->index >= (ARRAY_SIZE(imx471_hv_flips_bayer_order) / 4))
		return -EINVAL;

	code->code = imx471_get_format_code(sensor);

	return 0;
}

static int imx471_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(imx471_modes))
		return -EINVAL;

	fse->min_width = imx471_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = imx471_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void imx471_update_pad_format(struct imx471 *sensor,
				     const struct imx471_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.code = imx471_get_format_code(sensor);
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx471_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx471 *sensor = to_imx471(sd);
	const struct imx471_mode *mode;
	int h_blank, ret;

	mode = v4l2_find_nearest_size(imx471_modes, ARRAY_SIZE(imx471_modes),
				      width, height, fmt->format.width,
				      fmt->format.height);

	imx471_update_pad_format(sensor, mode, fmt);

	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (media_entity_is_streaming(&sensor->sd.entity))
		return -EBUSY;

	ret = __v4l2_ctrl_modify_range(sensor->vblank,
				       mode->fll_min - mode->height,
				       IMX471_FLL_MAX - mode->height,
				       1,
				       mode->fll_def - mode->height);
	if (ret)
		return ret;

	h_blank = mode->llp - mode->width;
	/*
	 * Currently hblank is not changeable.
	 * So FPS control is done only by vblank.
	 */
	return __v4l2_ctrl_modify_range(sensor->hblank, h_blank,
					h_blank, 1, h_blank);
}

static int imx471_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		break;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = IMX471_NATIVE_WIDTH;
		sel->r.height = IMX471_NATIVE_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = IMX471_PIXEL_ARRAY_TOP;
		sel->r.left = IMX471_PIXEL_ARRAY_LEFT;
		sel->r.width = IMX471_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX471_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int imx471_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.code = MEDIA_BUS_FMT_SRGGB10_1X10,
			.width = imx471_modes[0].width,
			.height = imx471_modes[0].height,
		},
	};

	return imx471_set_pad_format(sd, sd_state, &fmt);
}

static int imx471_identify_module(struct imx471 *sensor)
{
	int ret;
	u64 val;

	ret = cci_read(sensor->regmap, IMX471_REG_CHIP_ID, &val, NULL);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to read chip id\n");

	if (val != IMX471_CHIP_ID)
		return dev_err_probe(sensor->dev, -EIO,
				     "chip id mismatch: %x!=%llx\n",
				     IMX471_CHIP_ID, val);

	return 0;
}

static int imx471_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx471 *sensor = to_imx471(sd);

	clk_disable_unprepare(sensor->img_clk);
	gpiod_set_value_cansleep(sensor->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx471_supply_name),
			       sensor->supplies);

	return 0;
}

static int imx471_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx471 *sensor = to_imx471(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx471_supply_name),
				    sensor->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(sensor->img_clk);
	if (ret < 0) {
		regulator_bulk_disable(ARRAY_SIZE(imx471_supply_name),
				       sensor->supplies);
		dev_err(dev, "failed to enable imaging clock: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);

	usleep_range(10000, 15000);

	return 0;
}

static int imx471_enable_stream(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				u32 pad, u64 streams_mask)
{
	struct imx471 *sensor = to_imx471(sd);
	const struct imx471_mode *mode;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	ret = pm_runtime_resume_and_get(sensor->dev);
	if (ret)
		return ret;

	ret = imx471_identify_module(sensor);
	if (ret)
		goto error_powerdown;

	ret = cci_multi_reg_write(sensor->regmap, imx471_global_regs,
				  ARRAY_SIZE(imx471_global_regs), NULL);
	if (ret) {
		dev_err(sensor->dev, "failed to set global settings: %d\n",
			ret);
		goto error_powerdown;
	}

	fmt = v4l2_subdev_state_get_format(state, 0);
	mode = v4l2_find_nearest_size(imx471_modes, ARRAY_SIZE(imx471_modes),
				      width, height, fmt->width, fmt->height);

	ret = cci_multi_reg_write(sensor->regmap, mode->default_mode_regs,
				  mode->default_mode_regs_length, NULL);
	if (ret) {
		dev_err(sensor->dev, "failed to set mode: %d\n", ret);
		goto error_powerdown;
	}

	ret = cci_write(sensor->regmap, IMX471_REG_DPGA_USE_GLOBAL_GAIN, 1,
			NULL);
	if (ret)
		goto error_powerdown;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (ret)
		goto error_powerdown;

	ret = cci_write(sensor->regmap, IMX471_REG_MODE_SELECT,
			IMX471_MODE_STREAMING, NULL);
	if (ret)
		goto error_powerdown;

	__v4l2_ctrl_grab(sensor->vflip, true);
	__v4l2_ctrl_grab(sensor->hflip, true);

	return ret;

error_powerdown:
	pm_runtime_put(sensor->dev);

	return ret;
}

static int imx471_disable_stream(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	struct imx471 *sensor = to_imx471(sd);
	int ret;

	ret = cci_write(sensor->regmap, IMX471_REG_MODE_SELECT,
			IMX471_MODE_STANDBY, NULL);
	pm_runtime_put(sensor->dev);

	if (ret)
		dev_err(sensor->dev,
			"failed to disable stream with return value: %d\n",
			ret);

	__v4l2_ctrl_grab(sensor->vflip, false);
	__v4l2_ctrl_grab(sensor->hflip, false);

	return 0;
}

static const struct v4l2_subdev_video_ops imx471_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx471_pad_ops = {
	.enum_mbus_code = imx471_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx471_set_pad_format,
	.get_selection = imx471_get_selection,
	.enum_frame_size = imx471_enum_frame_size,
	.enable_streams = imx471_enable_stream,
	.disable_streams = imx471_disable_stream,
};

static const struct v4l2_subdev_ops imx471_subdev_ops = {
	.video = &imx471_video_ops,
	.pad = &imx471_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx471_internal_ops = {
	.init_state = imx471_init_state,
};

static int imx471_init_controls(struct imx471 *sensor)
{
	const struct imx471_mode *mode = &imx471_modes[0];
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *link_freq;
	s64 exposure_max, hblank;
	u64 pixel_rate;
	int ret;

	ret = v4l2_fwnode_device_parse(sensor->dev, &props);
	if (ret) {
		dev_err(sensor->dev, "failed to parse fwnode: %d\n", ret);
		return ret;
	}

	ctrl_hdlr = &sensor->ctrl_handler;
	v4l2_ctrl_handler_init(ctrl_hdlr, 12);

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx471_ctrl_ops, &props);

	link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
					   &imx471_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE(link_freq_menu_items) - 1,
					   0,
					   link_freq_menu_items);

	/* pixel_rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = div_u64(IMX471_LINK_FREQ_DEFAULT * 2 * 4, 10);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops,
			  V4L2_CID_PIXEL_RATE, pixel_rate,
			  pixel_rate, 1, pixel_rate);

	sensor->vblank = v4l2_ctrl_new_std(ctrl_hdlr,
					   &imx471_ctrl_ops,
					   V4L2_CID_VBLANK,
					   mode->fll_min - mode->height,
					   IMX471_FLL_MAX - mode->height,
					   1,
					   mode->fll_def - mode->height);

	hblank = mode->llp - mode->width;
	sensor->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank,
					   1, hblank);

	/* fll >= exposure time + adjust parameter (default value is 18) */
	exposure_max = mode->fll_def - IMX471_EXPOSURE_MARGIN;
	sensor->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX471_EXPOSURE_MIN, exposure_max,
					     IMX471_EXPOSURE_STEP,
					     IMX471_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX471_ANA_GAIN_MIN, IMX471_ANA_GAIN_MAX,
			  IMX471_ANA_GAIN_STEP, IMX471_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX471_DGTL_GAIN_MIN, IMX471_DGTL_GAIN_MAX,
			  IMX471_DGTL_GAIN_STEP, IMX471_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx471_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx471_test_pattern_menu) - 1,
				     0, 0, imx471_test_pattern_menu);

	sensor->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);

	sensor->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx471_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (ctrl_hdlr->error) {
		dev_err(sensor->dev, "%s control init failed: %d\n",
			__func__, ctrl_hdlr->error);
		goto error;
	}

	link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	sensor->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ctrl_hdlr->error;
}

static int imx471_check_hwcfg(struct imx471 *sensor)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep, *fwnode = dev_fwnode(sensor->dev);
	unsigned long link_freq_bitmap;
	struct clk *clk;
	int ret;

	clk = devm_v4l2_sensor_clk_get(sensor->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(sensor->dev, PTR_ERR(clk),
				     "can't get clock frequency\n");

	if (clk_get_rate(clk) != IMX471_EXT_CLK)
		return dev_err_probe(sensor->dev, -EINVAL,
				     "external clock %lu is not supported\n",
				     clk_get_rate(clk));

	ep = fwnode_graph_get_endpoint_by_id(fwnode, 0, 0, 0);
	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "parsing endpoint failed\n");

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "number of CSI2 data lanes %u is not supported\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);
		goto done_endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(sensor->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &link_freq_bitmap);

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int imx471_probe(struct i2c_client *client)
{
	struct imx471 *sensor;
	int ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return dev_err_probe(&client->dev, -ENOMEM,
				     "failed to allocate memory\n");

	sensor->dev = &client->dev;

	ret = imx471_check_hwcfg(sensor);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to check hwcfg: %d\n", ret);

	ret = imx471_get_regulators(sensor->dev, sensor);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to get regulators\n");

	sensor->reset_gpio = devm_gpiod_get_optional(sensor->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->reset_gpio),
				     "failed to get reset gpio\n");

	sensor->img_clk = devm_v4l2_sensor_clk_get(sensor->dev, NULL);
	if (IS_ERR(sensor->img_clk))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->img_clk),
				     "failed to get imaging clock\n");

	v4l2_i2c_subdev_init(&sensor->sd, client, &imx471_subdev_ops);

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->regmap),
				     "failed to initialize CCI\n");

	ret = imx471_power_on(sensor->dev);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to power on\n");

	ret = imx471_identify_module(sensor);
	if (ret) {
		dev_err_probe(sensor->dev, ret, "failed to find sensor: %d\n",
			      ret);
		goto error_power_off;
	}

	ret = imx471_init_controls(sensor);
	if (ret) {
		dev_err_probe(sensor->dev, ret, "failed to init controls: %d\n",
			      ret);
		goto error_power_off;
	}

	sensor->sd.internal_ops = &imx471_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		dev_err_probe(sensor->dev, ret,
			      "failed to init entity pads: %d\n", ret);
		goto error_v4l2_ctrl_handler_free;
	}

	sensor->sd.state_lock = sensor->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret < 0) {
		dev_err_probe(sensor->dev, ret, "failed to init subdev: %d\n",
			      ret);
		goto error_media_entity_pm;
	}

	pm_runtime_set_active(sensor->dev);
	pm_runtime_enable(sensor->dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret < 0)
		goto error_v4l2_subdev_cleanup;

	pm_runtime_idle(sensor->dev);

	return 0;

error_v4l2_subdev_cleanup:
	pm_runtime_disable(sensor->dev);
	pm_runtime_set_suspended(sensor->dev);
	v4l2_subdev_cleanup(&sensor->sd);

error_media_entity_pm:
	media_entity_cleanup(&sensor->sd.entity);

error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(sensor->sd.ctrl_handler);

error_power_off:
	imx471_power_off(sensor->dev);

	return ret;
}

static void imx471_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);

	if (!pm_runtime_status_suspended(&client->dev)) {
		imx471_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx471_pm_ops, imx471_power_off,
				 imx471_power_on, NULL);

static const struct acpi_device_id imx471_acpi_ids[] __maybe_unused = {
	{ "SONY471A" },
	{ "TBE20A0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, imx471_acpi_ids);

static struct i2c_driver imx471_i2c_driver = {
	.driver = {
		.name = "imx471",
		.acpi_match_table = ACPI_PTR(imx471_acpi_ids),
		.pm = pm_sleep_ptr(&imx471_pm_ops),
	},
	.probe = imx471_probe,
	.remove = imx471_remove,
};
module_i2c_driver(imx471_i2c_driver);

MODULE_AUTHOR("Jimmy Su <jimmy.su@intel.com>");
MODULE_AUTHOR("Serin Yeh <serin.yeh@intel.com>");
MODULE_AUTHOR("Kate Hsuan <hpa@redhat.com>");
MODULE_DESCRIPTION("Sony imx471 sensor driver");
MODULE_LICENSE("GPL");
