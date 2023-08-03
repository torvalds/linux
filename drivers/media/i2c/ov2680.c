// SPDX-License-Identifier: GPL-2.0
/*
 * Omnivision OV2680 CMOS Image Sensor driver
 *
 * Copyright (C) 2018 Linaro Ltd
 *
 * Based on OV5640 Sensor Driver
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014-2017 Mentor Graphics Inc.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define OV2680_CHIP_ID				0x2680

#define OV2680_REG_STREAM_CTRL			CCI_REG8(0x0100)
#define OV2680_REG_SOFT_RESET			CCI_REG8(0x0103)

#define OV2680_REG_CHIP_ID			CCI_REG16(0x300a)
#define OV2680_REG_PLL_MULTIPLIER		CCI_REG16(0x3081)

#define OV2680_REG_EXPOSURE_PK			CCI_REG24(0x3500)
#define OV2680_REG_R_MANUAL			CCI_REG8(0x3503)
#define OV2680_REG_GAIN_PK			CCI_REG16(0x350a)
#define OV2680_REG_TIMING_HTS			CCI_REG16(0x380c)
#define OV2680_REG_TIMING_VTS			CCI_REG16(0x380e)
#define OV2680_REG_FORMAT1			CCI_REG8(0x3820)
#define OV2680_REG_FORMAT2			CCI_REG8(0x3821)

#define OV2680_REG_ISP_CTRL00			CCI_REG8(0x5080)

#define OV2680_FRAME_RATE			30

#define OV2680_WIDTH_MAX			1600
#define OV2680_HEIGHT_MAX			1200

#define OV2680_DEFAULT_WIDTH			800
#define OV2680_DEFAULT_HEIGHT			600

enum ov2680_mode_id {
	OV2680_MODE_QUXGA_800_600,
	OV2680_MODE_720P_1280_720,
	OV2680_MODE_UXGA_1600_1200,
	OV2680_MODE_MAX,
};

static const char * const ov2680_supply_name[] = {
	"DOVDD",
	"DVDD",
	"AVDD",
};

#define OV2680_NUM_SUPPLIES ARRAY_SIZE(ov2680_supply_name)

enum {
	OV2680_19_2_MHZ,
	OV2680_24_MHZ,
};

static const unsigned long ov2680_xvclk_freqs[] = {
	[OV2680_19_2_MHZ] = 19200000,
	[OV2680_24_MHZ] = 24000000,
};

static const u8 ov2680_pll_multipliers[] = {
	[OV2680_19_2_MHZ] = 69,
	[OV2680_24_MHZ] = 55,
};

struct ov2680_mode_info {
	const char *name;
	enum ov2680_mode_id id;
	u32 width;
	u32 height;
	const struct reg_sequence *reg_data;
	u32 reg_data_size;
};

struct ov2680_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *test_pattern;
};

struct ov2680_mode {
	struct v4l2_mbus_framefmt	fmt;
	struct v4l2_fract		frame_interval;
};

struct ov2680_dev {
	struct device			*dev;
	struct regmap			*regmap;
	struct v4l2_subdev		sd;

	struct media_pad		pad;
	struct clk			*xvclk;
	u32				xvclk_freq;
	u8				pll_mult;
	struct regulator_bulk_data	supplies[OV2680_NUM_SUPPLIES];

	struct gpio_desc		*pwdn_gpio;
	struct mutex			lock; /* protect members */

	bool				is_streaming;

	struct ov2680_ctrls		ctrls;
	struct ov2680_mode		mode;

	const struct ov2680_mode_info	*current_mode;
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Random Data",
	"Square",
	"Black Image",
};

static const int ov2680_hv_flip_bayer_order[] = {
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

static const struct reg_sequence ov2680_setting_30fps_QUXGA_800_600[] = {
	/* Set PLL SP DIV to 1 for binning mode */
	{0x3086, 0x01},

	/* Sensor control register 0x0a to 0x23 for binning mode */
	{0x370a, 0x23},

	/* Set X and Y output size to 800x600 */
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},

	/* Set HTS + VTS to 1708x644 */
	{0x380c, 0x06},
	{0x380d, 0xac},
	{0x380e, 0x02},
	{0x380f, 0x84},

	/* Set ISP WIN X and Y start to 4x4 */
	{0x3811, 0x04},
	{0x3813, 0x04},

	/* Set X INC and Y INC for binning */
	{0x3814, 0x31},
	{0x3815, 0x31},

	/* Initialize FORMAT1 to default/reset value (vflip disabled) */
	{0x3820, 0xc0},

	/* Set black level compensation range to 0 - 3 (default 0 - 11) */
	{0x4008, 0x00},
	{0x4009, 0x03},

	/* Set MIPI pclk period to 0x1e (default/reset is 0x18) */
	{0x4837, 0x1e},

	/* Initialize exposure to 0x4ee (overridden by the ctrl, drop this */
	{0x3501, 0x4e},
	{0x3502, 0xe0},

	/* R MANUAL set exposure and gain to manual (hw does not do auto) */
	{0x3503, 0x03},
};

static const struct reg_sequence ov2680_setting_30fps_720P_1280_720[] = {
	/* Set PLL SP DIV to 0 for not binning mode */
	{0x3086, 0x00},

	/* Set X and Y output size to 1280x720 */
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},

	/* Set HTS + VTS to 1704x1294 */
	{0x380c, 0x06},
	{0x380d, 0xa8},
	{0x380e, 0x05},
	{0x380f, 0x0e},

	/* Set ISP WIN X and Y start to 8x6 */
	{0x3811, 0x08},
	{0x3813, 0x06},

	/* Set X INC and Y INC for non binning */
	{0x3814, 0x11},
	{0x3815, 0x11},

	/* Initialize FORMAT1 to default/reset value (vflip disabled) */
	{0x3820, 0xc0},

	/* Set backlight compensation range start to 0 */
	{0x4008, 0x00},
};

static const struct reg_sequence ov2680_setting_30fps_UXGA_1600_1200[] = {
	/* Set PLL SP DIV to 0 for not binning mode */
	{0x3086, 0x00},

	/* Initialize exposure to 0x4ee (overridden by the ctrl, drop this */
	{0x3501, 0x4e},
	{0x3502, 0xe0},

	/* Set X and Y output size to 1600x1200 */
	{0x3808, 0x06},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xb0},

	/* Set HTS + VTS to 1704x1294 */
	{0x380c, 0x06},
	{0x380d, 0xa8},
	{0x380e, 0x05},
	{0x380f, 0x0e},

	/* Set ISP WIN X and Y start to 0x0 */
	{0x3811, 0x00},
	{0x3813, 0x00},

	/* Set X INC and Y INC for non binning */
	{0x3814, 0x11},
	{0x3815, 0x11},

	/* Initialize FORMAT1 to default/reset value (vflip disabled) */
	{0x3820, 0xc0},

	/* Set backlight compensation range start to 0 */
	{0x4008, 0x00},

	/* Set MIPI pclk period to default/reset value of 0x18 */
	{0x4837, 0x18}
};

static const struct ov2680_mode_info ov2680_mode_init_data = {
	"mode_quxga_800_600", OV2680_MODE_QUXGA_800_600, 800, 600,
	ov2680_setting_30fps_QUXGA_800_600,
	ARRAY_SIZE(ov2680_setting_30fps_QUXGA_800_600),
};

static const struct ov2680_mode_info ov2680_mode_data[OV2680_MODE_MAX] = {
	{"mode_quxga_800_600", OV2680_MODE_QUXGA_800_600,
	 800, 600, ov2680_setting_30fps_QUXGA_800_600,
	 ARRAY_SIZE(ov2680_setting_30fps_QUXGA_800_600)},
	{"mode_720p_1280_720", OV2680_MODE_720P_1280_720,
	 1280, 720, ov2680_setting_30fps_720P_1280_720,
	 ARRAY_SIZE(ov2680_setting_30fps_720P_1280_720)},
	{"mode_uxga_1600_1200", OV2680_MODE_UXGA_1600_1200,
	 1600, 1200, ov2680_setting_30fps_UXGA_1600_1200,
	 ARRAY_SIZE(ov2680_setting_30fps_UXGA_1600_1200)},
};

static struct ov2680_dev *to_ov2680_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov2680_dev, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov2680_dev,
			     ctrls.handler)->sd;
}

static void ov2680_power_up(struct ov2680_dev *sensor)
{
	if (!sensor->pwdn_gpio)
		return;

	gpiod_set_value(sensor->pwdn_gpio, 0);
	usleep_range(5000, 10000);
}

static void ov2680_power_down(struct ov2680_dev *sensor)
{
	if (!sensor->pwdn_gpio)
		return;

	gpiod_set_value(sensor->pwdn_gpio, 1);
	usleep_range(5000, 10000);
}

static void ov2680_set_bayer_order(struct ov2680_dev *sensor,
				   struct v4l2_mbus_framefmt *fmt)
{
	int hv_flip = 0;

	if (sensor->ctrls.vflip && sensor->ctrls.vflip->val)
		hv_flip += 1;

	if (sensor->ctrls.hflip && sensor->ctrls.hflip->val)
		hv_flip += 2;

	fmt->code = ov2680_hv_flip_bayer_order[hv_flip];
}

static void ov2680_fill_format(struct ov2680_dev *sensor,
			       struct v4l2_mbus_framefmt *fmt,
			       unsigned int width, unsigned int height)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = width;
	fmt->height = height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	ov2680_set_bayer_order(sensor, fmt);
}

static int ov2680_set_vflip(struct ov2680_dev *sensor, s32 val)
{
	int ret;

	if (sensor->is_streaming)
		return -EBUSY;

	ret = cci_update_bits(sensor->regmap, OV2680_REG_FORMAT1,
			      BIT(2), val ? BIT(2) : 0, NULL);
	if (ret < 0)
		return ret;

	ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
	return 0;
}

static int ov2680_set_hflip(struct ov2680_dev *sensor, s32 val)
{
	int ret;

	if (sensor->is_streaming)
		return -EBUSY;

	ret = cci_update_bits(sensor->regmap, OV2680_REG_FORMAT2,
			      BIT(2), val ? BIT(2) : 0, NULL);
	if (ret < 0)
		return ret;

	ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
	return 0;
}

static int ov2680_test_pattern_set(struct ov2680_dev *sensor, int value)
{
	int ret = 0;

	if (!value)
		return cci_update_bits(sensor->regmap, OV2680_REG_ISP_CTRL00,
				       BIT(7), 0, NULL);

	cci_update_bits(sensor->regmap, OV2680_REG_ISP_CTRL00,
			0x03, value - 1, &ret);
	cci_update_bits(sensor->regmap, OV2680_REG_ISP_CTRL00,
			BIT(7), BIT(7), &ret);

	return ret;
}

static int ov2680_gain_set(struct ov2680_dev *sensor, u32 gain)
{
	return cci_write(sensor->regmap, OV2680_REG_GAIN_PK, gain, NULL);
}

static int ov2680_exposure_set(struct ov2680_dev *sensor, u32 exp)
{
	return cci_write(sensor->regmap, OV2680_REG_EXPOSURE_PK, exp << 4,
			 NULL);
}

static int ov2680_stream_enable(struct ov2680_dev *sensor)
{
	int ret;

	ret = cci_write(sensor->regmap, OV2680_REG_PLL_MULTIPLIER,
			sensor->pll_mult, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write(sensor->regmap,
				     ov2680_mode_init_data.reg_data,
				     ov2680_mode_init_data.reg_data_size);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write(sensor->regmap,
				     sensor->current_mode->reg_data,
				     sensor->current_mode->reg_data_size);
	if (ret < 0)
		return ret;

	/* Restore value of all ctrls */
	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
	if (ret < 0)
		return ret;

	return cci_write(sensor->regmap, OV2680_REG_STREAM_CTRL, 1, NULL);
}

static int ov2680_stream_disable(struct ov2680_dev *sensor)
{
	return cci_write(sensor->regmap, OV2680_REG_STREAM_CTRL, 0, NULL);
}

static int ov2680_power_off(struct ov2680_dev *sensor)
{
	clk_disable_unprepare(sensor->xvclk);
	ov2680_power_down(sensor);
	regulator_bulk_disable(OV2680_NUM_SUPPLIES, sensor->supplies);
	return 0;
}

static int ov2680_power_on(struct ov2680_dev *sensor)
{
	int ret;

	ret = regulator_bulk_enable(OV2680_NUM_SUPPLIES, sensor->supplies);
	if (ret < 0) {
		dev_err(sensor->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}

	if (!sensor->pwdn_gpio) {
		ret = cci_write(sensor->regmap, OV2680_REG_SOFT_RESET, 0x01,
				NULL);
		if (ret != 0) {
			dev_err(sensor->dev, "sensor soft reset failed\n");
			goto err_disable_regulators;
		}
		usleep_range(1000, 2000);
	} else {
		ov2680_power_down(sensor);
		ov2680_power_up(sensor);
	}

	ret = clk_prepare_enable(sensor->xvclk);
	if (ret < 0)
		goto err_disable_regulators;

	return 0;

err_disable_regulators:
	regulator_bulk_disable(OV2680_NUM_SUPPLIES, sensor->supplies);
	return ret;
}

static int ov2680_s_g_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->mode.frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov2680_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming == !!enable)
		goto unlock;

	if (enable) {
		ret = pm_runtime_resume_and_get(sensor->sd.dev);
		if (ret < 0)
			goto unlock;

		ret = ov2680_stream_enable(sensor);
		if (ret < 0) {
			pm_runtime_put(sensor->sd.dev);
			goto unlock;
		}
	} else {
		ret = ov2680_stream_disable(sensor);
		pm_runtime_put(sensor->sd.dev);
	}

	sensor->is_streaming = !!enable;

unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	if (code->pad != 0 || code->index != 0)
		return -EINVAL;

	code->code = sensor->mode.fmt.code;

	return 0;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct v4l2_mbus_framefmt *fmt = NULL;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt = v4l2_subdev_get_try_format(&sensor->sd, sd_state,
						 format->pad);
	} else {
		fmt = &sensor->mode.fmt;
	}

	format->format = *fmt;

	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	struct v4l2_mbus_framefmt *try_fmt;
	const struct ov2680_mode_info *mode;
	int ret = 0;

	if (format->pad != 0)
		return -EINVAL;

	mode = v4l2_find_nearest_size(ov2680_mode_data,
				      ARRAY_SIZE(ov2680_mode_data),
				      width, height,
				      format->format.width,
				      format->format.height);
	if (!mode)
		return -EINVAL;

	ov2680_fill_format(sensor, &format->format, mode->width, mode->height);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		try_fmt = v4l2_subdev_get_try_format(sd, sd_state, 0);
		*try_fmt = format->format;
		return 0;
	}

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	sensor->current_mode = mode;
	sensor->mode.fmt = format->format;

unlock:
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov2680_init_cfg(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	ov2680_fill_format(sensor, &sd_state->pads[0].try_fmt,
			   OV2680_DEFAULT_WIDTH, OV2680_DEFAULT_HEIGHT);
	return 0;
}

static int ov2680_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= OV2680_MODE_MAX || index < 0)
		return -EINVAL;

	fse->min_width = ov2680_mode_data[index].width;
	fse->min_height = ov2680_mode_data[index].height;
	fse->max_width = ov2680_mode_data[index].width;
	fse->max_height = ov2680_mode_data[index].height;

	return 0;
}

static bool ov2680_valid_frame_size(struct v4l2_subdev_frame_interval_enum *fie)
{
	int i;

	for (i = 0; i < OV2680_MODE_MAX; i++) {
		if (fie->width == ov2680_mode_data[i].width &&
		    fie->height == ov2680_mode_data[i].height)
			return true;
	}

	return false;
}

static int ov2680_enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	/* Only 1 framerate */
	if (fie->index || !ov2680_valid_frame_size(fie))
		return -EINVAL;

	fie->interval = sensor->mode.frame_interval;

	return 0;
}

static int ov2680_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret;

	/* Only apply changes to the controls if the device is powered up */
	if (!pm_runtime_get_if_in_use(sensor->sd.dev)) {
		ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ret = ov2680_gain_set(sensor, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov2680_exposure_set(sensor, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = ov2680_set_vflip(sensor, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov2680_set_hflip(sensor, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov2680_test_pattern_set(sensor, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(sensor->sd.dev);
	return ret;
}

static const struct v4l2_ctrl_ops ov2680_ctrl_ops = {
	.s_ctrl = ov2680_s_ctrl,
};

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.g_frame_interval	= ov2680_s_g_frame_interval,
	.s_frame_interval	= ov2680_s_g_frame_interval,
	.s_stream		= ov2680_s_stream,
};

static const struct v4l2_subdev_pad_ops ov2680_pad_ops = {
	.init_cfg		= ov2680_init_cfg,
	.enum_mbus_code		= ov2680_enum_mbus_code,
	.get_fmt		= ov2680_get_fmt,
	.set_fmt		= ov2680_set_fmt,
	.enum_frame_size	= ov2680_enum_frame_size,
	.enum_frame_interval	= ov2680_enum_frame_interval,
};

static const struct v4l2_subdev_ops ov2680_subdev_ops = {
	.video	= &ov2680_video_ops,
	.pad	= &ov2680_pad_ops,
};

static int ov2680_mode_init(struct ov2680_dev *sensor)
{
	const struct ov2680_mode_info *init_mode;

	/* set initial mode */
	ov2680_fill_format(sensor, &sensor->mode.fmt,
			   OV2680_DEFAULT_WIDTH, OV2680_DEFAULT_HEIGHT);

	sensor->mode.frame_interval.denominator = OV2680_FRAME_RATE;
	sensor->mode.frame_interval.numerator = 1;

	init_mode = &ov2680_mode_init_data;

	sensor->current_mode = init_mode;

	return 0;
}

static int ov2680_v4l2_register(struct ov2680_dev *sensor)
{
	struct i2c_client *client = to_i2c_client(sensor->dev);
	const struct v4l2_ctrl_ops *ops = &ov2680_ctrl_ops;
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret = 0;

	v4l2_i2c_subdev_init(&sensor->sd, client, &ov2680_subdev_ops);

	sensor->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(hdl, 5);

	hdl->lock = &sensor->lock;

	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);

	ctrls->test_pattern = v4l2_ctrl_new_std_menu_items(hdl,
					&ov2680_ctrl_ops, V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(test_pattern_menu) - 1,
					0, 0, test_pattern_menu);

	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					    0, 32767, 1, 0);

	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN, 0, 2047, 1, 0);

	if (hdl->error) {
		ret = hdl->error;
		goto cleanup_entity;
	}

	ctrls->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	ctrls->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->sd.ctrl_handler = hdl;

	ret = v4l2_async_register_subdev(&sensor->sd);
	if (ret < 0)
		goto cleanup_entity;

	return 0;

cleanup_entity:
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(hdl);

	return ret;
}

static int ov2680_get_regulators(struct ov2680_dev *sensor)
{
	int i;

	for (i = 0; i < OV2680_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = ov2680_supply_name[i];

	return devm_regulator_bulk_get(sensor->dev,
				       OV2680_NUM_SUPPLIES, sensor->supplies);
}

static int ov2680_check_id(struct ov2680_dev *sensor)
{
	u64 chip_id;
	int ret;

	ret = cci_read(sensor->regmap, OV2680_REG_CHIP_ID, &chip_id, NULL);
	if (ret < 0) {
		dev_err(sensor->dev, "failed to read chip id\n");
		return -ENODEV;
	}

	if (chip_id != OV2680_CHIP_ID) {
		dev_err(sensor->dev, "chip id: 0x%04llx does not match expected 0x%04x\n",
			chip_id, OV2680_CHIP_ID);
		return -ENODEV;
	}

	return 0;
}

static int ov2680_parse_dt(struct ov2680_dev *sensor)
{
	struct device *dev = sensor->dev;
	struct fwnode_handle *ep_fwnode;
	struct gpio_desc *gpio;
	unsigned int rate = 0;
	int i, ret;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver.
	 * Bridge drivers doing this may also add GPIO mappings, wait for this.
	 */
	ep_fwnode = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!ep_fwnode)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	fwnode_handle_put(ep_fwnode);

	/*
	 * The pin we want is named XSHUTDN in the datasheet. Linux sensor
	 * drivers have standardized on using "powerdown" as con-id name
	 * for powerdown or shutdown pins. Older DTB files use "reset",
	 * so fallback to that if there is no "powerdown" pin.
	 */
	gpio = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (!gpio)
		gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);

	ret = PTR_ERR_OR_ZERO(gpio);
	if (ret < 0) {
		dev_dbg(dev, "error while getting reset gpio: %d\n", ret);
		return ret;
	}

	sensor->pwdn_gpio = gpio;

	sensor->xvclk = devm_clk_get_optional(dev, "xvclk");
	if (IS_ERR(sensor->xvclk)) {
		dev_err(dev, "xvclk clock missing or invalid\n");
		return PTR_ERR(sensor->xvclk);
	}

	/*
	 * We could have either a 24MHz or 19.2MHz clock rate from either DT or
	 * ACPI... but we also need to support the weird IPU3 case which will
	 * have an external clock AND a clock-frequency property. Check for the
	 * clock-frequency property and if found, set that rate if we managed
	 * to acquire a clock. This should cover the ACPI case. If the system
	 * uses devicetree then the configured rate should already be set, so
	 * we can just read it.
	 */
	ret = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
				       &rate);
	if (ret && !sensor->xvclk)
		return dev_err_probe(dev, ret, "invalid clock config\n");

	if (!ret && sensor->xvclk) {
		ret = clk_set_rate(sensor->xvclk, rate);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set clock rate\n");
	}

	sensor->xvclk_freq = rate ?: clk_get_rate(sensor->xvclk);

	for (i = 0; i < ARRAY_SIZE(ov2680_xvclk_freqs); i++) {
		if (sensor->xvclk_freq == ov2680_xvclk_freqs[i])
			break;
	}

	if (i == ARRAY_SIZE(ov2680_xvclk_freqs))
		return dev_err_probe(dev, -EINVAL,
				     "unsupported xvclk frequency %d Hz\n",
				     sensor->xvclk_freq);

	sensor->pll_mult = ov2680_pll_multipliers[i];

	return 0;
}

static int ov2680_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov2680_dev *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = &client->dev;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	ret = ov2680_parse_dt(sensor);
	if (ret < 0)
		return ret;

	ret = ov2680_mode_init(sensor);
	if (ret < 0)
		return ret;

	ret = ov2680_get_regulators(sensor);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	mutex_init(&sensor->lock);

	/*
	 * Power up and verify the chip now, so that if runtime pm is
	 * disabled the chip is left on and streaming will work.
	 */
	ret = ov2680_power_on(sensor);
	if (ret < 0)
		goto lock_destroy;

	ret = ov2680_check_id(sensor);
	if (ret < 0)
		goto err_powerdown;

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = ov2680_v4l2_register(sensor);
	if (ret < 0)
		goto err_pm_runtime;

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	dev_info(dev, "ov2680 init correctly\n");

	return 0;

err_pm_runtime:
	pm_runtime_disable(&client->dev);
	pm_runtime_put_noidle(&client->dev);
err_powerdown:
	ov2680_power_off(sensor);
lock_destroy:
	dev_err(dev, "ov2680 init fail: %d\n", ret);
	mutex_destroy(&sensor->lock);

	return ret;
}

static void ov2680_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	mutex_destroy(&sensor->lock);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov2680_power_off(sensor);
	pm_runtime_set_suspended(&client->dev);
}

static int ov2680_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);

	if (sensor->is_streaming)
		ov2680_stream_disable(sensor);

	return ov2680_power_off(sensor);
}

static int ov2680_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_dev(sd);
	int ret;

	ret = ov2680_power_on(sensor);
	if (ret < 0)
		goto stream_disable;

	if (sensor->is_streaming) {
		ret = ov2680_stream_enable(sensor);
		if (ret < 0)
			goto stream_disable;
	}

	return 0;

stream_disable:
	ov2680_stream_disable(sensor);
	sensor->is_streaming = false;

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov2680_pm_ops, ov2680_suspend, ov2680_resume,
				 NULL);

static const struct of_device_id ov2680_dt_ids[] = {
	{ .compatible = "ovti,ov2680" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov2680_dt_ids);

static const struct acpi_device_id ov2680_acpi_ids[] = {
	{ "OVTI2680" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, ov2680_acpi_ids);

static struct i2c_driver ov2680_i2c_driver = {
	.driver = {
		.name  = "ov2680",
		.pm = pm_sleep_ptr(&ov2680_pm_ops),
		.of_match_table	= ov2680_dt_ids,
		.acpi_match_table = ov2680_acpi_ids,
	},
	.probe		= ov2680_probe,
	.remove		= ov2680_remove,
};
module_i2c_driver(ov2680_i2c_driver);

MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_DESCRIPTION("OV2680 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
