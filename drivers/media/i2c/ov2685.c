/*
 * ov2685 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define CHIP_ID				0x2685
#define OV2685_REG_CHIP_ID		0x300a

#define REG_SC_CTRL_MODE		0x0100
#define     SC_CTRL_MODE_STANDBY	0x0
#define     SC_CTRL_MODE_STREAMING	BIT(0)

#define OV2685_REG_EXPOSURE		0x3500
#define	OV2685_EXPOSURE_MIN		4
#define	OV2685_EXPOSURE_STEP		1

#define OV2685_REG_VTS			0x380e
#define OV2685_VTS_MAX			0x7fff

#define OV2685_REG_GAIN			0x350a
#define OV2685_GAIN_MIN			0
#define OV2685_GAIN_MAX			0x07ff
#define OV2685_GAIN_STEP		0x1
#define OV2685_GAIN_DEFAULT		0x0036

#define OV2685_REG_TEST_PATTERN		0x5080
#define OV2685_TEST_PATTERN_DISABLED		0x00
#define OV2685_TEST_PATTERN_COLOR_BAR		0x80
#define OV2685_TEST_PATTERN_RND			0x81
#define OV2685_TEST_PATTERN_COLOR_BAR_FADE	0x88
#define OV2685_TEST_PATTERN_BW_SQUARE		0x92
#define OV2685_TEST_PATTERN_COLOR_SQUARE	0x82

#define REG_NULL			0xFFFF

#define OV2685_REG_VALUE_08BIT		1
#define OV2685_REG_VALUE_16BIT		2
#define OV2685_REG_VALUE_24BIT		3

#define OV2685_LANES			1
#define OV2685_BITS_PER_SAMPLE		10

struct regval {
	u16 addr;
	u8 val;
};

struct ov2685_mode {
	u32 width;
	u32 height;
	u32 exp_def;
	u32 hts_def;
	u32 vts_def;
	const struct regval *reg_list;
};

struct ov2685 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct regulator	*avdd_regulator;	/* Analog power */
	struct regulator	*dovdd_regulator;	/* Digital I/O power */
				/* use internal DVDD power */
	struct gpio_desc	*reset_gpio;

	bool			streaming;
	struct mutex		mutex;
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl_handler ctrl_handler;

	const struct ov2685_mode *cur_mode;
};
#define to_ov2685(sd) container_of(sd, struct ov2685, subdev)

/* PLL settings bases on 24M xvclk */
static struct regval ov2685_1600x1200_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x3002, 0x00},
	{0x3016, 0x1c},
	{0x3018, 0x44},
	{0x301d, 0xf0},
	{0x3020, 0x00},
	{0x3082, 0x37},
	{0x3083, 0x03},
	{0x3084, 0x09},
	{0x3085, 0x04},
	{0x3086, 0x00},
	{0x3087, 0x00},
	{0x3501, 0x4e},
	{0x3502, 0xe0},
	{0x3503, 0x07},
	{0x350b, 0x36},
	{0x3600, 0xb4},
	{0x3603, 0x35},
	{0x3604, 0x24},
	{0x3605, 0x00},
	{0x3620, 0x24},
	{0x3621, 0x34},
	{0x3622, 0x03},
	{0x3628, 0x10},
	{0x3705, 0x3c},
	{0x370a, 0x21},
	{0x370c, 0x50},
	{0x370d, 0xc0},
	{0x3717, 0x58},
	{0x3718, 0x80},
	{0x3720, 0x00},
	{0x3721, 0x09},
	{0x3722, 0x06},
	{0x3723, 0x59},
	{0x3738, 0x99},
	{0x3781, 0x80},
	{0x3784, 0x0c},
	{0x3789, 0x60},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x06},
	{0x3805, 0x4f},
	{0x3806, 0x04},
	{0x3807, 0xbf},
	{0x3808, 0x06},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xb0},
	{0x380c, 0x06},
	{0x380d, 0xa4},
	{0x380e, 0x05},
	{0x380f, 0x0e},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3819, 0x04},
	{0x3820, 0xc0},
	{0x3821, 0x00},
	{0x3a06, 0x01},
	{0x3a07, 0x84},
	{0x3a08, 0x01},
	{0x3a09, 0x43},
	{0x3a0a, 0x24},
	{0x3a0b, 0x60},
	{0x3a0c, 0x28},
	{0x3a0d, 0x60},
	{0x3a0e, 0x04},
	{0x3a0f, 0x8c},
	{0x3a10, 0x05},
	{0x3a11, 0x0c},
	{0x4000, 0x81},
	{0x4001, 0x40},
	{0x4008, 0x02},
	{0x4009, 0x09},
	{0x4300, 0x00},
	{0x430e, 0x00},
	{0x4602, 0x02},
	{0x481b, 0x40},
	{0x481f, 0x40},
	{0x4837, 0x18},
	{0x5000, 0x1f},
	{0x5001, 0x05},
	{0x5002, 0x30},
	{0x5003, 0x04},
	{0x5004, 0x00},
	{0x5005, 0x0c},
	{0x5280, 0x15},
	{0x5281, 0x06},
	{0x5282, 0x06},
	{0x5283, 0x08},
	{0x5284, 0x1c},
	{0x5285, 0x1c},
	{0x5286, 0x20},
	{0x5287, 0x10},
	{REG_NULL, 0x00}
};

#define OV2685_LINK_FREQ_330MHZ		330000000
static const s64 link_freq_menu_items[] = {
	OV2685_LINK_FREQ_330MHZ
};

static const char * const ov2685_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
	"RND PATTERN",
	"Color Bar FADE",
	"BW SQUARE",
	"COLOR SQUARE"
};

static const int ov2685_test_pattern_val[] = {
	OV2685_TEST_PATTERN_DISABLED,
	OV2685_TEST_PATTERN_COLOR_BAR,
	OV2685_TEST_PATTERN_RND,
	OV2685_TEST_PATTERN_COLOR_BAR_FADE,
	OV2685_TEST_PATTERN_BW_SQUARE,
	OV2685_TEST_PATTERN_COLOR_SQUARE,
};

static const struct ov2685_mode supported_modes[] = {
	{
		.width = 1600,
		.height = 1200,
		.exp_def = 0x04ee,
		.hts_def = 0x06a4,
		.vts_def = 0x050e,
		.reg_list = ov2685_1600x1200_regs,
	},
};

/* Write registers up to 4 at a time */
static int ov2685_write_reg(struct i2c_client *client, u16 reg,
			    unsigned int len, u32 val)
{
	int buf_i;
	int val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int ov2685_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov2685_write_reg(client, regs[i].addr,
				       OV2685_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov2685_read_reg(struct i2c_client *client, u16 reg,
			   unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static void ov2685_fill_fmt(struct ov2685 *ov2685,
			    struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->width = ov2685->cur_mode->width;
	fmt->height = ov2685->cur_mode->height;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov2685_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2685 *ov2685 = to_ov2685(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	ov2685_fill_fmt(ov2685, mbus_fmt);

	return 0;
}

static int ov2685_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2685 *ov2685 = to_ov2685(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	ov2685_fill_fmt(ov2685, mbus_fmt);

	return 0;
}

static int ov2685_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int ov2685_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fse->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	fse->min_width  = supported_modes[index].width;
	fse->max_width  = supported_modes[index].width;
	fse->max_height = supported_modes[index].height;
	fse->min_height = supported_modes[index].height;

	return 0;
}

static inline void ov2685_set_exposure(struct ov2685 *ov2685, s32 val)
{
	ov2685_write_reg(ov2685->client, OV2685_REG_EXPOSURE,
			 OV2685_REG_VALUE_24BIT, val << 4);
}

static inline void ov2685_set_gain(struct ov2685 *ov2685, s32 val)
{
	ov2685_write_reg(ov2685->client, OV2685_REG_GAIN,
			 OV2685_REG_VALUE_16BIT, val & OV2685_GAIN_MAX);
}

static inline void ov2685_set_vts(struct ov2685 *ov2685, s32 val)
{
	val += ov2685->cur_mode->height;
	ov2685_write_reg(ov2685->client, OV2685_REG_VTS,
			 OV2685_REG_VALUE_16BIT, val);
}

static inline void ov2685_enable_test_pattern(struct ov2685 *ov2685, u32 pat)
{
	ov2685_write_reg(ov2685->client, OV2685_REG_TEST_PATTERN,
			 OV2685_REG_VALUE_08BIT, ov2685_test_pattern_val[pat]);
}

static int __ov2685_power_on(struct ov2685 *ov2685)
{
	int ret;
	struct device *dev = &ov2685->client->dev;

	ret = clk_prepare_enable(ov2685->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	clk_set_rate(ov2685->xvclk, 24000000);

	gpiod_set_value_cansleep(ov2685->reset_gpio, 1);
	/* AVDD and DOVDD may rise in any order */
	ret = regulator_enable(ov2685->avdd_regulator);
	if (ret < 0) {
		dev_err(dev, "Failed to enable AVDD regulator\n");
		goto disable_xvclk;
	}
	ret = regulator_enable(ov2685->dovdd_regulator);
	if (ret < 0) {
		dev_err(dev, "Failed to enable DOVDD regulator\n");
		goto disable_avdd;
	}
	/* The minimum delay between AVDD and reset rising can be 0 */
	gpiod_set_value_cansleep(ov2685->reset_gpio, 0);
	/* 8192 xvclk cycles prior to the first SCCB transaction.
	 * NOTE: An additional 1ms must be added to wait for
	 *       SCCB to become stable when using internal DVDD.
	 */
	usleep_range(1350, 1500);

	/* HACK: ov2685 would output messy data after reset(R0103),
	 * writing register before .s_stream() as a workaround
	 */
	ret = ov2685_write_array(ov2685->client, ov2685->cur_mode->reg_list);

	return ret;
disable_avdd:
	regulator_disable(ov2685->avdd_regulator);
disable_xvclk:
	clk_disable_unprepare(ov2685->xvclk);

	return ret;
}

static void __ov2685_power_off(struct ov2685 *ov2685)
{
	/* 512 xvclk cycles after the last SCCB transaction or MIPI frame end */
	usleep_range(30, 50);
	clk_disable_unprepare(ov2685->xvclk);
	gpiod_set_value_cansleep(ov2685->reset_gpio, 1);
	regulator_disable(ov2685->dovdd_regulator);
	regulator_disable(ov2685->avdd_regulator);
}

static int ov2685_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov2685 *ov2685 = to_ov2685(sd);
	int ret = 0;

	mutex_lock(&ov2685->mutex);

	if (on)
		ret = pm_runtime_get_sync(&ov2685->client->dev);
	else
		ret = pm_runtime_put(&ov2685->client->dev);

	mutex_unlock(&ov2685->mutex);

	return ret;
}

static int ov2685_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov2685 *ov2685 = to_ov2685(sd);
	struct i2c_client *client = ov2685->client;
	int ret = 0;

	mutex_lock(&ov2685->mutex);

	on = !!on;
	if (on == ov2685->streaming)
		goto unlock_and_return;

	if (on) {
		/* In case these controls are set before streaming */
		ov2685_set_exposure(ov2685, ov2685->exposure->val);
		ov2685_set_gain(ov2685, ov2685->anal_gain->val);
		ov2685_set_vts(ov2685, ov2685->vblank->val);
		ov2685_enable_test_pattern(ov2685, ov2685->test_pattern->val);

		ret = ov2685_write_reg(client, REG_SC_CTRL_MODE,
				OV2685_REG_VALUE_08BIT, SC_CTRL_MODE_STREAMING);
		if (ret)
			goto unlock_and_return;
	} else {
		ret = ov2685_write_reg(client, REG_SC_CTRL_MODE,
				OV2685_REG_VALUE_08BIT, SC_CTRL_MODE_STANDBY);
		if (ret)
			goto unlock_and_return;
	}

	ov2685->streaming = on;

unlock_and_return:
	mutex_unlock(&ov2685->mutex);
	return ret;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov2685_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov2685 *ov2685 = to_ov2685(sd);
	struct v4l2_mbus_framefmt *try_fmt;

	mutex_lock(&ov2685->mutex);

	try_fmt = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	/* Initialize try_fmt */
	ov2685_fill_fmt(ov2685, try_fmt);

	mutex_unlock(&ov2685->mutex);

	return 0;
}
#endif

static int ov2685_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2685 *ov2685 = to_ov2685(sd);
	int ret;

	ret = __ov2685_power_on(ov2685);
	if (ret)
		return ret;

	if (ov2685->streaming) {
		ret = ov2685_s_stream(sd, 1);
		if (ret)
			__ov2685_power_off(ov2685);
	}

	return ret;
}

static int ov2685_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2685 *ov2685 = to_ov2685(sd);

	__ov2685_power_off(ov2685);

	return 0;
}

static const struct dev_pm_ops ov2685_pm_ops = {
	SET_RUNTIME_PM_OPS(ov2685_runtime_suspend,
			   ov2685_runtime_resume, NULL)
};

static int ov2685_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2685 *ov2685 = container_of(ctrl->handler,
					     struct ov2685, ctrl_handler);
	struct i2c_client *client = ov2685->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov2685->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov2685->exposure,
					 ov2685->exposure->minimum, max,
					 ov2685->exposure->step,
					 ov2685->exposure->default_value);
		break;
	}

	pm_runtime_get_sync(&client->dev);
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ov2685_set_exposure(ov2685, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ov2685_set_gain(ov2685, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ov2685_set_vts(ov2685, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ov2685_enable_test_pattern(ov2685, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	};
	pm_runtime_put(&client->dev);

	return ret;
}

static struct v4l2_subdev_core_ops ov2685_core_ops = {
	.s_power = ov2685_s_power,
};

static struct v4l2_subdev_video_ops ov2685_video_ops = {
	.s_stream = ov2685_s_stream,
};

static struct v4l2_subdev_pad_ops ov2685_pad_ops = {
	.enum_mbus_code = ov2685_enum_mbus_code,
	.enum_frame_size = ov2685_enum_frame_sizes,
	.get_fmt = ov2685_get_fmt,
	.set_fmt = ov2685_set_fmt,
};

static struct v4l2_subdev_ops ov2685_subdev_ops = {
	.core	= &ov2685_core_ops,
	.video	= &ov2685_video_ops,
	.pad	= &ov2685_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov2685_internal_ops = {
	.open = ov2685_open,
};
#endif

static const struct v4l2_ctrl_ops ov2685_ctrl_ops = {
	.s_ctrl = ov2685_set_ctrl,
};

static int ov2685_initialize_controls(struct ov2685 *ov2685)
{
	const struct ov2685_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	u64 exposure_max;
	u32 pixel_rate, h_blank;
	int ret;

	handler = &ov2685->ctrl_handler;
	mode = ov2685->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	handler->lock = &ov2685->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = (link_freq_menu_items[0] * 2 * OV2685_LANES) /
		     OV2685_BITS_PER_SAMPLE;
	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	ov2685->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov2685->hblank)
		ov2685->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov2685->vblank = v4l2_ctrl_new_std(handler, &ov2685_ctrl_ops,
				V4L2_CID_VBLANK, mode->vts_def - mode->height,
				OV2685_VTS_MAX - mode->height, 1,
				mode->vts_def - mode->height);

	exposure_max = mode->vts_def - 4;
	ov2685->exposure = v4l2_ctrl_new_std(handler, &ov2685_ctrl_ops,
				V4L2_CID_EXPOSURE, OV2685_EXPOSURE_MIN,
				exposure_max, OV2685_EXPOSURE_STEP,
				mode->exp_def);

	ov2685->anal_gain = v4l2_ctrl_new_std(handler, &ov2685_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV2685_GAIN_MIN,
				OV2685_GAIN_MAX, OV2685_GAIN_STEP,
				OV2685_GAIN_DEFAULT);

	ov2685->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov2685_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov2685_test_pattern_menu) - 1,
				0, 0, ov2685_test_pattern_menu);

	if (handler->error) {
		v4l2_ctrl_handler_free(handler);
		return handler->error;
	}

	ov2685->subdev.ctrl_handler = handler;

	return 0;
}

static int ov2685_check_sensor_id(struct ov2685 *ov2685,
				  struct i2c_client *client)
{
	struct device *dev = &ov2685->client->dev;
	int id, ret;

	ret = __ov2685_power_on(ov2685);
	if (ret)
		return ret;
	ov2685_read_reg(client, OV2685_REG_CHIP_ID,
			OV2685_REG_VALUE_16BIT, &id);
	__ov2685_power_off(ov2685);

	if (id != CHIP_ID) {
		dev_err(dev, "Wrong camera sensor id(%04x)\n", id);
		return -EINVAL;
	}

	dev_info(dev, "Detected OV%04x sensor\n", CHIP_ID);

	return 0;
}

static int ov2685_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov2685 *ov2685;
	int ret;

	ov2685 = devm_kzalloc(dev, sizeof(*ov2685), GFP_KERNEL);
	if (!ov2685)
		return -ENOMEM;

	ov2685->client = client;
	ov2685->cur_mode = &supported_modes[0];

	ov2685->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov2685->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov2685->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov2685->reset_gpio)) {
		dev_err(dev, "Failed to get reset-gpios\n");
		return -EINVAL;
	}

	ov2685->avdd_regulator = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ov2685->avdd_regulator)) {
		dev_err(dev, "Failed to get avdd-supply\n");
		return -EINVAL;
	}
	ov2685->dovdd_regulator = devm_regulator_get(dev, "dovdd");
	if (IS_ERR(ov2685->dovdd_regulator)) {
		dev_err(dev, "Failed to get dovdd-supply\n");
		return -EINVAL;
	}

	mutex_init(&ov2685->mutex);
	v4l2_i2c_subdev_init(&ov2685->subdev, client, &ov2685_subdev_ops);
	ret = ov2685_initialize_controls(ov2685);
	if (ret)
		goto destroy_mutex;

	ret = ov2685_check_sensor_id(ov2685, client);
	if (ret)
		return ret;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	ov2685->subdev.internal_ops = &ov2685_internal_ops;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov2685->pad.flags = MEDIA_PAD_FL_SOURCE;
	ov2685->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov2685->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&ov2685->subdev.entity, 1, &ov2685->pad, 0);
	if (ret < 0)
		goto free_ctrl_handler;
#endif

	ret = v4l2_async_register_subdev(&ov2685->subdev);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto clean_entity;
	}

	pm_runtime_enable(dev);
	return 0;

clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&ov2685->subdev.entity);
#endif
free_ctrl_handler:
	v4l2_ctrl_handler_free(&ov2685->ctrl_handler);
destroy_mutex:
	mutex_destroy(&ov2685->mutex);

	return ret;
}

static int ov2685_remove(struct i2c_client *client)
{
	struct ov2685 *ov2685 = i2c_get_clientdata(client);

	__ov2685_power_off(ov2685);
	v4l2_async_unregister_subdev(&ov2685->subdev);
	media_entity_cleanup(&ov2685->subdev.entity);
	v4l2_ctrl_handler_free(&ov2685->ctrl_handler);
	mutex_destroy(&ov2685->mutex);

	return 0;
}

static const struct of_device_id ov2685_of_match[] = {
	{ .compatible = "ovti,ov2685" },
	{},
};

static struct i2c_driver ov2685_i2c_driver = {
	.driver = {
		.name = "ov2685",
		.owner = THIS_MODULE,
		.pm = &ov2685_pm_ops,
		.of_match_table = ov2685_of_match
	},
	.probe		= &ov2685_probe,
	.remove		= &ov2685_remove,
};

module_i2c_driver(ov2685_i2c_driver);

MODULE_DESCRIPTION("OmniVision ov2685 sensor driver");
MODULE_LICENSE("GPL v2");
