// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for MT9M001 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

/*
 * mt9m001 i2c address 0x5d
 */

/* mt9m001 selected register addresses */
#define MT9M001_CHIP_VERSION		0x00
#define MT9M001_ROW_START		0x01
#define MT9M001_COLUMN_START		0x02
#define MT9M001_WINDOW_HEIGHT		0x03
#define MT9M001_WINDOW_WIDTH		0x04
#define MT9M001_HORIZONTAL_BLANKING	0x05
#define MT9M001_VERTICAL_BLANKING	0x06
#define MT9M001_OUTPUT_CONTROL		0x07
#define MT9M001_SHUTTER_WIDTH		0x09
#define MT9M001_FRAME_RESTART		0x0b
#define MT9M001_SHUTTER_DELAY		0x0c
#define MT9M001_RESET			0x0d
#define MT9M001_READ_OPTIONS1		0x1e
#define MT9M001_READ_OPTIONS2		0x20
#define MT9M001_GLOBAL_GAIN		0x35
#define MT9M001_CHIP_ENABLE		0xF1

#define MT9M001_MAX_WIDTH		1280
#define MT9M001_MAX_HEIGHT		1024
#define MT9M001_MIN_WIDTH		48
#define MT9M001_MIN_HEIGHT		32
#define MT9M001_COLUMN_SKIP		20
#define MT9M001_ROW_SKIP		12
#define MT9M001_DEFAULT_HBLANK		9
#define MT9M001_DEFAULT_VBLANK		25

/* MT9M001 has only one fixed colorspace per pixelcode */
struct mt9m001_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct mt9m001_datafmt *mt9m001_find_datafmt(
	u32 code, const struct mt9m001_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct mt9m001_datafmt mt9m001_colour_fmts[] = {
	/*
	 * Order important: first natively supported,
	 * second supported with a GPIO extender
	 */
	{MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB},
	{MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
};

static const struct mt9m001_datafmt mt9m001_monochrome_fmts[] = {
	/* Order important - see above */
	{MEDIA_BUS_FMT_Y10_1X10, V4L2_COLORSPACE_JPEG},
	{MEDIA_BUS_FMT_Y8_1X8, V4L2_COLORSPACE_JPEG},
};

struct mt9m001 {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct {
		/* exposure/auto-exposure cluster */
		struct v4l2_ctrl *autoexposure;
		struct v4l2_ctrl *exposure;
	};
	bool streaming;
	struct mutex mutex;
	struct v4l2_rect rect;	/* Sensor window */
	struct clk *clk;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *reset_gpio;
	const struct mt9m001_datafmt *fmt;
	const struct mt9m001_datafmt *fmts;
	int num_fmts;
	unsigned int total_h;
	unsigned short y_skip_top;	/* Lines to skip at the top */
	struct media_pad pad;
};

static struct mt9m001 *to_mt9m001(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mt9m001, subdev);
}

static int reg_read(struct i2c_client *client, const u8 reg)
{
	return i2c_smbus_read_word_swapped(client, reg);
}

static int reg_write(struct i2c_client *client, const u8 reg,
		     const u16 data)
{
	return i2c_smbus_write_word_swapped(client, reg, data);
}

static int reg_set(struct i2c_client *client, const u8 reg,
		   const u16 data)
{
	int ret;

	ret = reg_read(client, reg);
	if (ret < 0)
		return ret;
	return reg_write(client, reg, ret | data);
}

static int reg_clear(struct i2c_client *client, const u8 reg,
		     const u16 data)
{
	int ret;

	ret = reg_read(client, reg);
	if (ret < 0)
		return ret;
	return reg_write(client, reg, ret & ~data);
}

struct mt9m001_reg {
	u8 reg;
	u16 data;
};

static int multi_reg_write(struct i2c_client *client,
			   const struct mt9m001_reg *regs, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		int ret = reg_write(client, regs[i].reg, regs[i].data);

		if (ret)
			return ret;
	}

	return 0;
}

static int mt9m001_init(struct i2c_client *client)
{
	static const struct mt9m001_reg init_regs[] = {
		/*
		 * Issue a soft reset. This returns all registers to their
		 * default values.
		 */
		{ MT9M001_RESET, 1 },
		{ MT9M001_RESET, 0 },
		/* Disable chip, synchronous option update */
		{ MT9M001_OUTPUT_CONTROL, 0 }
	};

	dev_dbg(&client->dev, "%s\n", __func__);

	return multi_reg_write(client, init_regs, ARRAY_SIZE(init_regs));
}

static int mt9m001_apply_selection(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	const struct mt9m001_reg regs[] = {
		/* Blanking and start values - default... */
		{ MT9M001_HORIZONTAL_BLANKING, MT9M001_DEFAULT_HBLANK },
		{ MT9M001_VERTICAL_BLANKING, MT9M001_DEFAULT_VBLANK },
		/*
		 * The caller provides a supported format, as verified per
		 * call to .set_fmt(FORMAT_TRY).
		 */
		{ MT9M001_COLUMN_START, mt9m001->rect.left },
		{ MT9M001_ROW_START, mt9m001->rect.top },
		{ MT9M001_WINDOW_WIDTH, mt9m001->rect.width - 1 },
		{ MT9M001_WINDOW_HEIGHT,
			mt9m001->rect.height + mt9m001->y_skip_top - 1 },
	};

	return multi_reg_write(client, regs, ARRAY_SIZE(regs));
}

static int mt9m001_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	int ret = 0;

	mutex_lock(&mt9m001->mutex);

	if (mt9m001->streaming == enable)
		goto done;

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0)
			goto put_unlock;

		ret = mt9m001_apply_selection(sd);
		if (ret)
			goto put_unlock;

		ret = __v4l2_ctrl_handler_setup(&mt9m001->hdl);
		if (ret)
			goto put_unlock;

		/* Switch to master "normal" mode */
		ret = reg_write(client, MT9M001_OUTPUT_CONTROL, 2);
		if (ret < 0)
			goto put_unlock;
	} else {
		/* Switch to master stop sensor readout */
		reg_write(client, MT9M001_OUTPUT_CONTROL, 0);
		pm_runtime_put(&client->dev);
	}

	mt9m001->streaming = enable;
done:
	mutex_unlock(&mt9m001->mutex);

	return 0;

put_unlock:
	pm_runtime_put(&client->dev);
	mutex_unlock(&mt9m001->mutex);

	return ret;
}

static int mt9m001_set_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_rect rect = sel->r;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (mt9m001->fmts == mt9m001_colour_fmts)
		/*
		 * Bayer format - even number of rows for simplicity,
		 * but let the user play with the top row.
		 */
		rect.height = ALIGN(rect.height, 2);

	/* Datasheet requirement: see register description */
	rect.width = ALIGN(rect.width, 2);
	rect.left = ALIGN(rect.left, 2);

	rect.width = clamp_t(u32, rect.width, MT9M001_MIN_WIDTH,
			MT9M001_MAX_WIDTH);
	rect.left = clamp_t(u32, rect.left, MT9M001_COLUMN_SKIP,
			MT9M001_COLUMN_SKIP + MT9M001_MAX_WIDTH - rect.width);

	rect.height = clamp_t(u32, rect.height, MT9M001_MIN_HEIGHT,
			MT9M001_MAX_HEIGHT);
	rect.top = clamp_t(u32, rect.top, MT9M001_ROW_SKIP,
			MT9M001_ROW_SKIP + MT9M001_MAX_HEIGHT - rect.height);

	mt9m001->total_h = rect.height + mt9m001->y_skip_top +
			   MT9M001_DEFAULT_VBLANK;

	mt9m001->rect = rect;

	return 0;
}

static int mt9m001_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = MT9M001_COLUMN_SKIP;
		sel->r.top = MT9M001_ROW_SKIP;
		sel->r.width = MT9M001_MAX_WIDTH;
		sel->r.height = MT9M001_MAX_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r = mt9m001->rect;
		return 0;
	default:
		return -EINVAL;
	}
}

static int mt9m001_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		format->format = *mf;
		return 0;
	}

	mf->width	= mt9m001->rect.width;
	mf->height	= mt9m001->rect.height;
	mf->code	= mt9m001->fmt->code;
	mf->colorspace	= mt9m001->fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;
	mf->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	mf->quantization = V4L2_QUANTIZATION_DEFAULT;
	mf->xfer_func	= V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mt9m001_s_fmt(struct v4l2_subdev *sd,
			 const struct mt9m001_datafmt *fmt,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_subdev_selection sel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.target = V4L2_SEL_TGT_CROP,
		.r.left = mt9m001->rect.left,
		.r.top = mt9m001->rect.top,
		.r.width = mf->width,
		.r.height = mf->height,
	};
	int ret;

	/* No support for scaling so far, just crop. TODO: use skipping */
	ret = mt9m001_set_selection(sd, NULL, &sel);
	if (!ret) {
		mf->width	= mt9m001->rect.width;
		mf->height	= mt9m001->rect.height;
		mt9m001->fmt	= fmt;
		mf->colorspace	= fmt->colorspace;
	}

	return ret;
}

static int mt9m001_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	const struct mt9m001_datafmt *fmt;

	if (format->pad)
		return -EINVAL;

	v4l_bound_align_image(&mf->width, MT9M001_MIN_WIDTH,
		MT9M001_MAX_WIDTH, 1,
		&mf->height, MT9M001_MIN_HEIGHT + mt9m001->y_skip_top,
		MT9M001_MAX_HEIGHT + mt9m001->y_skip_top, 0, 0);

	if (mt9m001->fmts == mt9m001_colour_fmts)
		mf->height = ALIGN(mf->height - 1, 2);

	fmt = mt9m001_find_datafmt(mf->code, mt9m001->fmts,
				   mt9m001->num_fmts);
	if (!fmt) {
		fmt = mt9m001->fmt;
		mf->code = fmt->code;
	}

	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;
	mf->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	mf->quantization = V4L2_QUANTIZATION_DEFAULT;
	mf->xfer_func	= V4L2_XFER_FUNC_DEFAULT;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return mt9m001_s_fmt(sd, fmt, mf);
	cfg->try_fmt = *mf;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m001_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff)
		return -EINVAL;

	reg->size = 2;
	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9m001_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff)
		return -EINVAL;

	if (reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static int mt9m001_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	int ret;

	ret = clk_prepare_enable(mt9m001->clk);
	if (ret)
		return ret;

	if (mt9m001->standby_gpio) {
		gpiod_set_value_cansleep(mt9m001->standby_gpio, 0);
		usleep_range(1000, 2000);
	}

	if (mt9m001->reset_gpio) {
		gpiod_set_value_cansleep(mt9m001->reset_gpio, 1);
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(mt9m001->reset_gpio, 0);
		usleep_range(1000, 2000);
	}

	return 0;
}

static int mt9m001_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	gpiod_set_value_cansleep(mt9m001->standby_gpio, 1);
	clk_disable_unprepare(mt9m001->clk);

	return 0;
}

static int mt9m001_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m001 *mt9m001 = container_of(ctrl->handler,
					       struct mt9m001, hdl);
	s32 min, max;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_AUTO:
		min = mt9m001->exposure->minimum;
		max = mt9m001->exposure->maximum;
		mt9m001->exposure->val =
			(524 + (mt9m001->total_h - 1) * (max - min)) / 1048 + min;
		break;
	}
	return 0;
}

static int mt9m001_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m001 *mt9m001 = container_of(ctrl->handler,
					       struct mt9m001, hdl);
	struct v4l2_subdev *sd = &mt9m001->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_ctrl *exp = mt9m001->exposure;
	int data;
	int ret;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ret = reg_set(client, MT9M001_READ_OPTIONS2, 0x8000);
		else
			ret = reg_clear(client, MT9M001_READ_OPTIONS2, 0x8000);
		break;

	case V4L2_CID_GAIN:
		/* See Datasheet Table 7, Gain settings. */
		if (ctrl->val <= ctrl->default_value) {
			/* Pack it into 0..1 step 0.125, register values 0..8 */
			unsigned long range = ctrl->default_value - ctrl->minimum;
			data = ((ctrl->val - (s32)ctrl->minimum) * 8 + range / 2) / range;

			dev_dbg(&client->dev, "Setting gain %d\n", data);
			ret = reg_write(client, MT9M001_GLOBAL_GAIN, data);
		} else {
			/* Pack it into 1.125..15 variable step, register values 9..67 */
			/* We assume qctrl->maximum - qctrl->default_value - 1 > 0 */
			unsigned long range = ctrl->maximum - ctrl->default_value - 1;
			unsigned long gain = ((ctrl->val - (s32)ctrl->default_value - 1) *
					       111 + range / 2) / range + 9;

			if (gain <= 32)
				data = gain;
			else if (gain <= 64)
				data = ((gain - 32) * 16 + 16) / 32 + 80;
			else
				data = ((gain - 64) * 7 + 28) / 56 + 96;

			dev_dbg(&client->dev, "Setting gain from %d to %d\n",
				 reg_read(client, MT9M001_GLOBAL_GAIN), data);
			ret = reg_write(client, MT9M001_GLOBAL_GAIN, data);
		}
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->val == V4L2_EXPOSURE_MANUAL) {
			unsigned long range = exp->maximum - exp->minimum;
			unsigned long shutter = ((exp->val - (s32)exp->minimum) * 1048 +
						 range / 2) / range + 1;

			dev_dbg(&client->dev,
				"Setting shutter width from %d to %lu\n",
				reg_read(client, MT9M001_SHUTTER_WIDTH), shutter);
			ret = reg_write(client, MT9M001_SHUTTER_WIDTH, shutter);
		} else {
			mt9m001->total_h = mt9m001->rect.height +
				mt9m001->y_skip_top + MT9M001_DEFAULT_VBLANK;
			ret = reg_write(client, MT9M001_SHUTTER_WIDTH,
					mt9m001->total_h);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9m001_video_probe(struct i2c_client *client)
{
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	s32 data;
	int ret;

	/* Enable the chip */
	data = reg_write(client, MT9M001_CHIP_ENABLE, 1);
	dev_dbg(&client->dev, "write: %d\n", data);

	/* Read out the chip version register */
	data = reg_read(client, MT9M001_CHIP_VERSION);

	/* must be 0x8411 or 0x8421 for colour sensor and 8431 for bw */
	switch (data) {
	case 0x8411:
	case 0x8421:
		mt9m001->fmts = mt9m001_colour_fmts;
		mt9m001->num_fmts = ARRAY_SIZE(mt9m001_colour_fmts);
		break;
	case 0x8431:
		mt9m001->fmts = mt9m001_monochrome_fmts;
		mt9m001->num_fmts = ARRAY_SIZE(mt9m001_monochrome_fmts);
		break;
	default:
		dev_err(&client->dev,
			"No MT9M001 chip detected, register read %x\n", data);
		ret = -ENODEV;
		goto done;
	}

	mt9m001->fmt = &mt9m001->fmts[0];

	dev_info(&client->dev, "Detected a MT9M001 chip ID %x (%s)\n", data,
		 data == 0x8431 ? "C12STM" : "C12ST");

	ret = mt9m001_init(client);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to initialise the camera\n");
		goto done;
	}

	/* mt9m001_init() has reset the chip, returning registers to defaults */
	ret = v4l2_ctrl_handler_setup(&mt9m001->hdl);

done:
	return ret;
}

static int mt9m001_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	*lines = mt9m001->y_skip_top;

	return 0;
}

static const struct v4l2_ctrl_ops mt9m001_ctrl_ops = {
	.g_volatile_ctrl = mt9m001_g_volatile_ctrl,
	.s_ctrl = mt9m001_s_ctrl,
};

static const struct v4l2_subdev_core_ops mt9m001_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= mt9m001_g_register,
	.s_register	= mt9m001_s_register,
#endif
};

static int mt9m001_init_cfg(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, cfg, 0);

	try_fmt->width		= MT9M001_MAX_WIDTH;
	try_fmt->height		= MT9M001_MAX_HEIGHT;
	try_fmt->code		= mt9m001->fmts[0].code;
	try_fmt->colorspace	= mt9m001->fmts[0].colorspace;
	try_fmt->field		= V4L2_FIELD_NONE;
	try_fmt->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	try_fmt->quantization	= V4L2_QUANTIZATION_DEFAULT;
	try_fmt->xfer_func	= V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mt9m001_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	if (code->pad || code->index >= mt9m001->num_fmts)
		return -EINVAL;

	code->code = mt9m001->fmts[code->index].code;
	return 0;
}

static int mt9m001_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	/* MT9M001 has all capture_format parameters fixed */
	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_FALLING |
		V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_HIGH | V4L2_MBUS_MASTER;
	cfg->type = V4L2_MBUS_PARALLEL;

	return 0;
}

static const struct v4l2_subdev_video_ops mt9m001_subdev_video_ops = {
	.s_stream	= mt9m001_s_stream,
	.g_mbus_config	= mt9m001_g_mbus_config,
};

static const struct v4l2_subdev_sensor_ops mt9m001_subdev_sensor_ops = {
	.g_skip_top_lines	= mt9m001_g_skip_top_lines,
};

static const struct v4l2_subdev_pad_ops mt9m001_subdev_pad_ops = {
	.init_cfg	= mt9m001_init_cfg,
	.enum_mbus_code = mt9m001_enum_mbus_code,
	.get_selection	= mt9m001_get_selection,
	.set_selection	= mt9m001_set_selection,
	.get_fmt	= mt9m001_get_fmt,
	.set_fmt	= mt9m001_set_fmt,
};

static const struct v4l2_subdev_ops mt9m001_subdev_ops = {
	.core	= &mt9m001_subdev_core_ops,
	.video	= &mt9m001_subdev_video_ops,
	.sensor	= &mt9m001_subdev_sensor_ops,
	.pad	= &mt9m001_subdev_pad_ops,
};

static int mt9m001_probe(struct i2c_client *client)
{
	struct mt9m001 *mt9m001;
	struct i2c_adapter *adapter = client->adapter;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9m001 = devm_kzalloc(&client->dev, sizeof(*mt9m001), GFP_KERNEL);
	if (!mt9m001)
		return -ENOMEM;

	mt9m001->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(mt9m001->clk))
		return PTR_ERR(mt9m001->clk);

	mt9m001->standby_gpio = devm_gpiod_get_optional(&client->dev, "standby",
							GPIOD_OUT_LOW);
	if (IS_ERR(mt9m001->standby_gpio))
		return PTR_ERR(mt9m001->standby_gpio);

	mt9m001->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(mt9m001->reset_gpio))
		return PTR_ERR(mt9m001->reset_gpio);

	v4l2_i2c_subdev_init(&mt9m001->subdev, client, &mt9m001_subdev_ops);
	mt9m001->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				 V4L2_SUBDEV_FL_HAS_EVENTS;
	v4l2_ctrl_handler_init(&mt9m001->hdl, 4);
	v4l2_ctrl_new_std(&mt9m001->hdl, &mt9m001_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&mt9m001->hdl, &mt9m001_ctrl_ops,
			V4L2_CID_GAIN, 0, 127, 1, 64);
	mt9m001->exposure = v4l2_ctrl_new_std(&mt9m001->hdl, &mt9m001_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 255, 1, 255);
	/*
	 * Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width
	 */
	mt9m001->autoexposure = v4l2_ctrl_new_std_menu(&mt9m001->hdl,
			&mt9m001_ctrl_ops, V4L2_CID_EXPOSURE_AUTO, 1, 0,
			V4L2_EXPOSURE_AUTO);
	mt9m001->subdev.ctrl_handler = &mt9m001->hdl;
	if (mt9m001->hdl.error)
		return mt9m001->hdl.error;

	v4l2_ctrl_auto_cluster(2, &mt9m001->autoexposure,
					V4L2_EXPOSURE_MANUAL, true);

	mutex_init(&mt9m001->mutex);
	mt9m001->hdl.lock = &mt9m001->mutex;

	/* Second stage probe - when a capture adapter is there */
	mt9m001->y_skip_top	= 0;
	mt9m001->rect.left	= MT9M001_COLUMN_SKIP;
	mt9m001->rect.top	= MT9M001_ROW_SKIP;
	mt9m001->rect.width	= MT9M001_MAX_WIDTH;
	mt9m001->rect.height	= MT9M001_MAX_HEIGHT;

	ret = mt9m001_power_on(&client->dev);
	if (ret)
		goto error_hdl_free;

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = mt9m001_video_probe(client);
	if (ret)
		goto error_power_off;

	mt9m001->pad.flags = MEDIA_PAD_FL_SOURCE;
	mt9m001->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&mt9m001->subdev.entity, 1, &mt9m001->pad);
	if (ret)
		goto error_power_off;

	ret = v4l2_async_register_subdev(&mt9m001->subdev);
	if (ret)
		goto error_entity_cleanup;

	pm_runtime_idle(&client->dev);

	return 0;

error_entity_cleanup:
	media_entity_cleanup(&mt9m001->subdev.entity);
error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	mt9m001_power_off(&client->dev);

error_hdl_free:
	v4l2_ctrl_handler_free(&mt9m001->hdl);
	mutex_destroy(&mt9m001->mutex);

	return ret;
}

static int mt9m001_remove(struct i2c_client *client)
{
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	pm_runtime_get_sync(&client->dev);

	v4l2_async_unregister_subdev(&mt9m001->subdev);
	media_entity_cleanup(&mt9m001->subdev.entity);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	mt9m001_power_off(&client->dev);

	v4l2_ctrl_handler_free(&mt9m001->hdl);
	mutex_destroy(&mt9m001->mutex);

	return 0;
}

static const struct i2c_device_id mt9m001_id[] = {
	{ "mt9m001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9m001_id);

static const struct dev_pm_ops mt9m001_pm_ops = {
	SET_RUNTIME_PM_OPS(mt9m001_power_off, mt9m001_power_on, NULL)
};

static const struct of_device_id mt9m001_of_match[] = {
	{ .compatible = "onnn,mt9m001", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt9m001_of_match);

static struct i2c_driver mt9m001_i2c_driver = {
	.driver = {
		.name = "mt9m001",
		.pm = &mt9m001_pm_ops,
		.of_match_table = mt9m001_of_match,
	},
	.probe_new	= mt9m001_probe,
	.remove		= mt9m001_remove,
	.id_table	= mt9m001_id,
};

module_i2c_driver(mt9m001_i2c_driver);

MODULE_DESCRIPTION("Micron MT9M001 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL v2");
