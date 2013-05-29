/*
 * Driver for MT9V022 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/module.h>

#include <media/mt9v022.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

/*
 * mt9v022 i2c address 0x48, 0x4c, 0x58, 0x5c
 * The platform has to define struct i2c_board_info objects and link to them
 * from struct soc_camera_host_desc
 */

static char *sensor_type;
module_param(sensor_type, charp, S_IRUGO);
MODULE_PARM_DESC(sensor_type, "Sensor type: \"colour\" or \"monochrome\"");

/* mt9v022 selected register addresses */
#define MT9V022_CHIP_VERSION		0x00
#define MT9V022_COLUMN_START		0x01
#define MT9V022_ROW_START		0x02
#define MT9V022_WINDOW_HEIGHT		0x03
#define MT9V022_WINDOW_WIDTH		0x04
#define MT9V022_HORIZONTAL_BLANKING	0x05
#define MT9V022_VERTICAL_BLANKING	0x06
#define MT9V022_CHIP_CONTROL		0x07
#define MT9V022_SHUTTER_WIDTH1		0x08
#define MT9V022_SHUTTER_WIDTH2		0x09
#define MT9V022_SHUTTER_WIDTH_CTRL	0x0a
#define MT9V022_TOTAL_SHUTTER_WIDTH	0x0b
#define MT9V022_RESET			0x0c
#define MT9V022_READ_MODE		0x0d
#define MT9V022_MONITOR_MODE		0x0e
#define MT9V022_PIXEL_OPERATION_MODE	0x0f
#define MT9V022_LED_OUT_CONTROL		0x1b
#define MT9V022_ADC_MODE_CONTROL	0x1c
#define MT9V022_REG32			0x20
#define MT9V022_ANALOG_GAIN		0x35
#define MT9V022_BLACK_LEVEL_CALIB_CTRL	0x47
#define MT9V022_PIXCLK_FV_LV		0x74
#define MT9V022_DIGITAL_TEST_PATTERN	0x7f
#define MT9V022_AEC_AGC_ENABLE		0xAF
#define MT9V022_MAX_TOTAL_SHUTTER_WIDTH	0xBD

/* mt9v024 partial list register addresses changes with respect to mt9v022 */
#define MT9V024_PIXCLK_FV_LV		0x72
#define MT9V024_MAX_TOTAL_SHUTTER_WIDTH	0xAD

/* Progressive scan, master, defaults */
#define MT9V022_CHIP_CONTROL_DEFAULT	0x188

#define MT9V022_MAX_WIDTH		752
#define MT9V022_MAX_HEIGHT		480
#define MT9V022_MIN_WIDTH		48
#define MT9V022_MIN_HEIGHT		32
#define MT9V022_COLUMN_SKIP		1
#define MT9V022_ROW_SKIP		4

#define MT9V022_HORIZONTAL_BLANKING_MIN	43
#define MT9V022_HORIZONTAL_BLANKING_MAX	1023
#define MT9V022_HORIZONTAL_BLANKING_DEF	94
#define MT9V022_VERTICAL_BLANKING_MIN	2
#define MT9V022_VERTICAL_BLANKING_MAX	3000
#define MT9V022_VERTICAL_BLANKING_DEF	45

#define is_mt9v022_rev3(id)	(id == 0x1313)
#define is_mt9v024(id)		(id == 0x1324)

/* MT9V022 has only one fixed colorspace per pixelcode */
struct mt9v022_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct mt9v022_datafmt *mt9v022_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct mt9v022_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct mt9v022_datafmt mt9v022_colour_fmts[] = {
	/*
	 * Order important: first natively supported,
	 * second supported with a GPIO extender
	 */
	{V4L2_MBUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
};

static const struct mt9v022_datafmt mt9v022_monochrome_fmts[] = {
	/* Order important - see above */
	{V4L2_MBUS_FMT_Y10_1X10, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_Y8_1X8, V4L2_COLORSPACE_JPEG},
};

/* only registers with different addresses on different mt9v02x sensors */
struct mt9v02x_register {
	u8	max_total_shutter_width;
	u8	pixclk_fv_lv;
};

static const struct mt9v02x_register mt9v022_register = {
	.max_total_shutter_width	= MT9V022_MAX_TOTAL_SHUTTER_WIDTH,
	.pixclk_fv_lv			= MT9V022_PIXCLK_FV_LV,
};

static const struct mt9v02x_register mt9v024_register = {
	.max_total_shutter_width	= MT9V024_MAX_TOTAL_SHUTTER_WIDTH,
	.pixclk_fv_lv			= MT9V024_PIXCLK_FV_LV,
};

enum mt9v022_model {
	MT9V022IX7ATM,
	MT9V022IX7ATC,
};

struct mt9v022 {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct {
		/* exposure/auto-exposure cluster */
		struct v4l2_ctrl *autoexposure;
		struct v4l2_ctrl *exposure;
	};
	struct {
		/* gain/auto-gain cluster */
		struct v4l2_ctrl *autogain;
		struct v4l2_ctrl *gain;
	};
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_rect rect;	/* Sensor window */
	const struct mt9v022_datafmt *fmt;
	const struct mt9v022_datafmt *fmts;
	const struct mt9v02x_register *reg;
	int num_fmts;
	enum mt9v022_model model;
	u16 chip_control;
	u16 chip_version;
	unsigned short y_skip_top;	/* Lines to skip at the top */
};

static struct mt9v022 *to_mt9v022(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mt9v022, subdev);
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

static int mt9v022_init(struct i2c_client *client)
{
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	int ret;

	/*
	 * Almost the default mode: master, parallel, simultaneous, and an
	 * undocumented bit 0x200, which is present in table 7, but not in 8,
	 * plus snapshot mode to disable scan for now
	 */
	mt9v022->chip_control |= 0x10;
	ret = reg_write(client, MT9V022_CHIP_CONTROL, mt9v022->chip_control);
	if (!ret)
		ret = reg_write(client, MT9V022_READ_MODE, 0x300);

	/* All defaults */
	if (!ret)
		/* AEC, AGC on */
		ret = reg_set(client, MT9V022_AEC_AGC_ENABLE, 0x3);
	if (!ret)
		ret = reg_write(client, MT9V022_ANALOG_GAIN, 16);
	if (!ret)
		ret = reg_write(client, MT9V022_TOTAL_SHUTTER_WIDTH, 480);
	if (!ret)
		ret = reg_write(client, mt9v022->reg->max_total_shutter_width, 480);
	if (!ret)
		/* default - auto */
		ret = reg_clear(client, MT9V022_BLACK_LEVEL_CALIB_CTRL, 1);
	if (!ret)
		ret = reg_write(client, MT9V022_DIGITAL_TEST_PATTERN, 0);
	if (!ret)
		return v4l2_ctrl_handler_setup(&mt9v022->hdl);

	return ret;
}

static int mt9v022_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);

	if (enable) {
		/* Switch to master "normal" mode */
		mt9v022->chip_control &= ~0x10;
		if (is_mt9v022_rev3(mt9v022->chip_version) ||
		    is_mt9v024(mt9v022->chip_version)) {
			/*
			 * Unset snapshot mode specific settings: clear bit 9
			 * and bit 2 in reg. 0x20 when in normal mode.
			 */
			if (reg_clear(client, MT9V022_REG32, 0x204))
				return -EIO;
		}
	} else {
		/* Switch to snapshot mode */
		mt9v022->chip_control |= 0x10;
		if (is_mt9v022_rev3(mt9v022->chip_version) ||
		    is_mt9v024(mt9v022->chip_version)) {
			/*
			 * Required settings for snapshot mode: set bit 9
			 * (RST enable) and bit 2 (CR enable) in reg. 0x20
			 * See TechNote TN0960 or TN-09-225.
			 */
			if (reg_set(client, MT9V022_REG32, 0x204))
				return -EIO;
		}
	}

	if (reg_write(client, MT9V022_CHIP_CONTROL, mt9v022->chip_control) < 0)
		return -EIO;
	return 0;
}

static int mt9v022_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	struct v4l2_rect rect = a->c;
	int min_row, min_blank;
	int ret;

	/* Bayer format - even size lengths */
	if (mt9v022->fmts == mt9v022_colour_fmts) {
		rect.width	= ALIGN(rect.width, 2);
		rect.height	= ALIGN(rect.height, 2);
		/* Let the user play with the starting pixel */
	}

	soc_camera_limit_side(&rect.left, &rect.width,
		     MT9V022_COLUMN_SKIP, MT9V022_MIN_WIDTH, MT9V022_MAX_WIDTH);

	soc_camera_limit_side(&rect.top, &rect.height,
		     MT9V022_ROW_SKIP, MT9V022_MIN_HEIGHT, MT9V022_MAX_HEIGHT);

	/* Like in example app. Contradicts the datasheet though */
	ret = reg_read(client, MT9V022_AEC_AGC_ENABLE);
	if (ret >= 0) {
		if (ret & 1) /* Autoexposure */
			ret = reg_write(client, mt9v022->reg->max_total_shutter_width,
					rect.height + mt9v022->y_skip_top + 43);
		/*
		 * If autoexposure is off, there is no need to set
		 * MT9V022_TOTAL_SHUTTER_WIDTH here. Autoexposure can be off
		 * only if the user has set exposure manually, using the
		 * V4L2_CID_EXPOSURE_AUTO with the value V4L2_EXPOSURE_MANUAL.
		 * In this case the register MT9V022_TOTAL_SHUTTER_WIDTH
		 * already contains the correct value.
		 */
	}
	/* Setup frame format: defaults apart from width and height */
	if (!ret)
		ret = reg_write(client, MT9V022_COLUMN_START, rect.left);
	if (!ret)
		ret = reg_write(client, MT9V022_ROW_START, rect.top);
	/*
	 * mt9v022: min total row time is 660 columns, min blanking is 43
	 * mt9v024: min total row time is 690 columns, min blanking is 61
	 */
	if (is_mt9v024(mt9v022->chip_version)) {
		min_row = 690;
		min_blank = 61;
	} else {
		min_row = 660;
		min_blank = 43;
	}
	if (!ret)
		ret = v4l2_ctrl_s_ctrl(mt9v022->hblank,
				rect.width > min_row - min_blank ?
				min_blank : min_row - rect.width);
	if (!ret)
		ret = v4l2_ctrl_s_ctrl(mt9v022->vblank, 45);
	if (!ret)
		ret = reg_write(client, MT9V022_WINDOW_WIDTH, rect.width);
	if (!ret)
		ret = reg_write(client, MT9V022_WINDOW_HEIGHT,
				rect.height + mt9v022->y_skip_top);

	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "Frame %dx%d pixel\n", rect.width, rect.height);

	mt9v022->rect = rect;

	return 0;
}

static int mt9v022_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);

	a->c	= mt9v022->rect;
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9v022_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= MT9V022_COLUMN_SKIP;
	a->bounds.top			= MT9V022_ROW_SKIP;
	a->bounds.width			= MT9V022_MAX_WIDTH;
	a->bounds.height		= MT9V022_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9v022_g_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);

	mf->width	= mt9v022->rect.width;
	mf->height	= mt9v022->rect.height;
	mf->code	= mt9v022->fmt->code;
	mf->colorspace	= mt9v022->fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int mt9v022_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	struct v4l2_crop a = {
		.c = {
			.left	= mt9v022->rect.left,
			.top	= mt9v022->rect.top,
			.width	= mf->width,
			.height	= mf->height,
		},
	};
	int ret;

	/*
	 * The caller provides a supported format, as verified per call to
	 * .try_mbus_fmt(), datawidth is from our supported format list
	 */
	switch (mf->code) {
	case V4L2_MBUS_FMT_Y8_1X8:
	case V4L2_MBUS_FMT_Y10_1X10:
		if (mt9v022->model != MT9V022IX7ATM)
			return -EINVAL;
		break;
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SBGGR10_1X10:
		if (mt9v022->model != MT9V022IX7ATC)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* No support for scaling on this camera, just crop. */
	ret = mt9v022_s_crop(sd, &a);
	if (!ret) {
		mf->width	= mt9v022->rect.width;
		mf->height	= mt9v022->rect.height;
		mt9v022->fmt	= mt9v022_find_datafmt(mf->code,
					mt9v022->fmts, mt9v022->num_fmts);
		mf->colorspace	= mt9v022->fmt->colorspace;
	}

	return ret;
}

static int mt9v022_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	const struct mt9v022_datafmt *fmt;
	int align = mf->code == V4L2_MBUS_FMT_SBGGR8_1X8 ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_1X10;

	v4l_bound_align_image(&mf->width, MT9V022_MIN_WIDTH,
		MT9V022_MAX_WIDTH, align,
		&mf->height, MT9V022_MIN_HEIGHT + mt9v022->y_skip_top,
		MT9V022_MAX_HEIGHT + mt9v022->y_skip_top, align, 0);

	fmt = mt9v022_find_datafmt(mf->code, mt9v022->fmts,
				   mt9v022->num_fmts);
	if (!fmt) {
		fmt = mt9v022->fmt;
		mf->code = fmt->code;
	}

	mf->colorspace	= fmt->colorspace;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9v022_g_register(struct v4l2_subdev *sd,
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

static int mt9v022_s_register(struct v4l2_subdev *sd,
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

static int mt9v022_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	return soc_camera_set_power(&client->dev, ssdd, on);
}

static int mt9v022_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9v022 *mt9v022 = container_of(ctrl->handler,
					       struct mt9v022, hdl);
	struct v4l2_subdev *sd = &mt9v022->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_ctrl *gain = mt9v022->gain;
	struct v4l2_ctrl *exp = mt9v022->exposure;
	unsigned long range;
	int data;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		data = reg_read(client, MT9V022_ANALOG_GAIN);
		if (data < 0)
			return -EIO;

		range = gain->maximum - gain->minimum;
		gain->val = ((data - 16) * range + 24) / 48 + gain->minimum;
		return 0;
	case V4L2_CID_EXPOSURE_AUTO:
		data = reg_read(client, MT9V022_TOTAL_SHUTTER_WIDTH);
		if (data < 0)
			return -EIO;

		range = exp->maximum - exp->minimum;
		exp->val = ((data - 1) * range + 239) / 479 + exp->minimum;
		return 0;
	case V4L2_CID_HBLANK:
		data = reg_read(client, MT9V022_HORIZONTAL_BLANKING);
		if (data < 0)
			return -EIO;
		ctrl->val = data;
		return 0;
	case V4L2_CID_VBLANK:
		data = reg_read(client, MT9V022_VERTICAL_BLANKING);
		if (data < 0)
			return -EIO;
		ctrl->val = data;
		return 0;
	}
	return -EINVAL;
}

static int mt9v022_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9v022 *mt9v022 = container_of(ctrl->handler,
					       struct mt9v022, hdl);
	struct v4l2_subdev *sd = &mt9v022->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			data = reg_set(client, MT9V022_READ_MODE, 0x10);
		else
			data = reg_clear(client, MT9V022_READ_MODE, 0x10);
		if (data < 0)
			return -EIO;
		return 0;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			data = reg_set(client, MT9V022_READ_MODE, 0x20);
		else
			data = reg_clear(client, MT9V022_READ_MODE, 0x20);
		if (data < 0)
			return -EIO;
		return 0;
	case V4L2_CID_AUTOGAIN:
		if (ctrl->val) {
			if (reg_set(client, MT9V022_AEC_AGC_ENABLE, 0x2) < 0)
				return -EIO;
		} else {
			struct v4l2_ctrl *gain = mt9v022->gain;
			/* mt9v022 has minimum == default */
			unsigned long range = gain->maximum - gain->minimum;
			/* Valid values 16 to 64, 32 to 64 must be even. */
			unsigned long gain_val = ((gain->val - gain->minimum) *
					      48 + range / 2) / range + 16;

			if (gain_val >= 32)
				gain_val &= ~1;

			/*
			 * The user wants to set gain manually, hope, she
			 * knows, what she's doing... Switch AGC off.
			 */
			if (reg_clear(client, MT9V022_AEC_AGC_ENABLE, 0x2) < 0)
				return -EIO;

			dev_dbg(&client->dev, "Setting gain from %d to %lu\n",
				reg_read(client, MT9V022_ANALOG_GAIN), gain_val);
			if (reg_write(client, MT9V022_ANALOG_GAIN, gain_val) < 0)
				return -EIO;
		}
		return 0;
	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->val == V4L2_EXPOSURE_AUTO) {
			data = reg_set(client, MT9V022_AEC_AGC_ENABLE, 0x1);
		} else {
			struct v4l2_ctrl *exp = mt9v022->exposure;
			unsigned long range = exp->maximum - exp->minimum;
			unsigned long shutter = ((exp->val - exp->minimum) *
					479 + range / 2) / range + 1;

			/*
			 * The user wants to set shutter width manually, hope,
			 * she knows, what she's doing... Switch AEC off.
			 */
			data = reg_clear(client, MT9V022_AEC_AGC_ENABLE, 0x1);
			if (data < 0)
				return -EIO;
			dev_dbg(&client->dev, "Shutter width from %d to %lu\n",
					reg_read(client, MT9V022_TOTAL_SHUTTER_WIDTH),
					shutter);
			if (reg_write(client, MT9V022_TOTAL_SHUTTER_WIDTH,
						shutter) < 0)
				return -EIO;
		}
		return 0;
	case V4L2_CID_HBLANK:
		if (reg_write(client, MT9V022_HORIZONTAL_BLANKING,
				ctrl->val) < 0)
			return -EIO;
		return 0;
	case V4L2_CID_VBLANK:
		if (reg_write(client, MT9V022_VERTICAL_BLANKING,
				ctrl->val) < 0)
			return -EIO;
		return 0;
	}
	return -EINVAL;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9v022_video_probe(struct i2c_client *client)
{
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	s32 data;
	int ret;
	unsigned long flags;

	ret = mt9v022_s_power(&mt9v022->subdev, 1);
	if (ret < 0)
		return ret;

	/* Read out the chip version register */
	data = reg_read(client, MT9V022_CHIP_VERSION);

	/* must be 0x1311, 0x1313 or 0x1324 */
	if (data != 0x1311 && data != 0x1313 && data != 0x1324) {
		ret = -ENODEV;
		dev_info(&client->dev, "No MT9V022 found, ID register 0x%x\n",
			 data);
		goto ei2c;
	}

	mt9v022->chip_version = data;

	mt9v022->reg = is_mt9v024(data) ? &mt9v024_register :
			&mt9v022_register;

	/* Soft reset */
	ret = reg_write(client, MT9V022_RESET, 1);
	if (ret < 0)
		goto ei2c;
	/* 15 clock cycles */
	udelay(200);
	if (reg_read(client, MT9V022_RESET)) {
		dev_err(&client->dev, "Resetting MT9V022 failed!\n");
		if (ret > 0)
			ret = -EIO;
		goto ei2c;
	}

	/* Set monochrome or colour sensor type */
	if (sensor_type && (!strcmp("colour", sensor_type) ||
			    !strcmp("color", sensor_type))) {
		ret = reg_write(client, MT9V022_PIXEL_OPERATION_MODE, 4 | 0x11);
		mt9v022->model = MT9V022IX7ATC;
		mt9v022->fmts = mt9v022_colour_fmts;
	} else {
		ret = reg_write(client, MT9V022_PIXEL_OPERATION_MODE, 0x11);
		mt9v022->model = MT9V022IX7ATM;
		mt9v022->fmts = mt9v022_monochrome_fmts;
	}

	if (ret < 0)
		goto ei2c;

	mt9v022->num_fmts = 0;

	/*
	 * This is a 10bit sensor, so by default we only allow 10bit.
	 * The platform may support different bus widths due to
	 * different routing of the data lines.
	 */
	if (ssdd->query_bus_param)
		flags = ssdd->query_bus_param(ssdd);
	else
		flags = SOCAM_DATAWIDTH_10;

	if (flags & SOCAM_DATAWIDTH_10)
		mt9v022->num_fmts++;
	else
		mt9v022->fmts++;

	if (flags & SOCAM_DATAWIDTH_8)
		mt9v022->num_fmts++;

	mt9v022->fmt = &mt9v022->fmts[0];

	dev_info(&client->dev, "Detected a MT9V022 chip ID %x, %s sensor\n",
		 data, mt9v022->model == MT9V022IX7ATM ?
		 "monochrome" : "colour");

	ret = mt9v022_init(client);
	if (ret < 0)
		dev_err(&client->dev, "Failed to initialise the camera\n");

ei2c:
	mt9v022_s_power(&mt9v022->subdev, 0);
	return ret;
}

static int mt9v022_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);

	*lines = mt9v022->y_skip_top;

	return 0;
}

static const struct v4l2_ctrl_ops mt9v022_ctrl_ops = {
	.g_volatile_ctrl = mt9v022_g_volatile_ctrl,
	.s_ctrl = mt9v022_s_ctrl,
};

static struct v4l2_subdev_core_ops mt9v022_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= mt9v022_g_register,
	.s_register	= mt9v022_s_register,
#endif
	.s_power	= mt9v022_s_power,
};

static int mt9v022_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v022 *mt9v022 = to_mt9v022(client);

	if (index >= mt9v022->num_fmts)
		return -EINVAL;

	*code = mt9v022->fmts[index].code;
	return 0;
}

static int mt9v022_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	cfg->flags = V4L2_MBUS_MASTER | V4L2_MBUS_SLAVE |
		V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING |
		V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_LOW |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_LOW |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = soc_camera_apply_board_flags(ssdd, cfg);

	return 0;
}

static int mt9v022_s_mbus_config(struct v4l2_subdev *sd,
				 const struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	unsigned long flags = soc_camera_apply_board_flags(ssdd, cfg);
	unsigned int bps = soc_mbus_get_fmtdesc(mt9v022->fmt->code)->bits_per_sample;
	int ret;
	u16 pixclk = 0;

	if (ssdd->set_bus_param) {
		ret = ssdd->set_bus_param(ssdd, 1 << (bps - 1));
		if (ret)
			return ret;
	} else if (bps != 10) {
		/*
		 * Without board specific bus width settings we only support the
		 * sensors native bus width
		 */
		return -EINVAL;
	}

	if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		pixclk |= 0x10;

	if (!(flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH))
		pixclk |= 0x1;

	if (!(flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH))
		pixclk |= 0x2;

	ret = reg_write(client, mt9v022->reg->pixclk_fv_lv, pixclk);
	if (ret < 0)
		return ret;

	if (!(flags & V4L2_MBUS_MASTER))
		mt9v022->chip_control &= ~0x8;

	ret = reg_write(client, MT9V022_CHIP_CONTROL, mt9v022->chip_control);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "Calculated pixclk 0x%x, chip control 0x%x\n",
		pixclk, mt9v022->chip_control);

	return 0;
}

static struct v4l2_subdev_video_ops mt9v022_subdev_video_ops = {
	.s_stream	= mt9v022_s_stream,
	.s_mbus_fmt	= mt9v022_s_fmt,
	.g_mbus_fmt	= mt9v022_g_fmt,
	.try_mbus_fmt	= mt9v022_try_fmt,
	.s_crop		= mt9v022_s_crop,
	.g_crop		= mt9v022_g_crop,
	.cropcap	= mt9v022_cropcap,
	.enum_mbus_fmt	= mt9v022_enum_fmt,
	.g_mbus_config	= mt9v022_g_mbus_config,
	.s_mbus_config	= mt9v022_s_mbus_config,
};

static struct v4l2_subdev_sensor_ops mt9v022_subdev_sensor_ops = {
	.g_skip_top_lines	= mt9v022_g_skip_top_lines,
};

static struct v4l2_subdev_ops mt9v022_subdev_ops = {
	.core	= &mt9v022_subdev_core_ops,
	.video	= &mt9v022_subdev_video_ops,
	.sensor	= &mt9v022_subdev_sensor_ops,
};

static int mt9v022_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9v022 *mt9v022;
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mt9v022_platform_data *pdata;
	int ret;

	if (!ssdd) {
		dev_err(&client->dev, "MT9V022 driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9v022 = devm_kzalloc(&client->dev, sizeof(struct mt9v022), GFP_KERNEL);
	if (!mt9v022)
		return -ENOMEM;

	pdata = ssdd->drv_priv;
	v4l2_i2c_subdev_init(&mt9v022->subdev, client, &mt9v022_subdev_ops);
	v4l2_ctrl_handler_init(&mt9v022->hdl, 6);
	v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	mt9v022->autogain = v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	mt9v022->gain = v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_GAIN, 0, 127, 1, 64);

	/*
	 * Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width
	 */
	mt9v022->autoexposure = v4l2_ctrl_new_std_menu(&mt9v022->hdl,
			&mt9v022_ctrl_ops, V4L2_CID_EXPOSURE_AUTO, 1, 0,
			V4L2_EXPOSURE_AUTO);
	mt9v022->exposure = v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 255, 1, 255);

	mt9v022->hblank = v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_HBLANK, MT9V022_HORIZONTAL_BLANKING_MIN,
			MT9V022_HORIZONTAL_BLANKING_MAX, 1,
			MT9V022_HORIZONTAL_BLANKING_DEF);

	mt9v022->vblank = v4l2_ctrl_new_std(&mt9v022->hdl, &mt9v022_ctrl_ops,
			V4L2_CID_VBLANK, MT9V022_VERTICAL_BLANKING_MIN,
			MT9V022_VERTICAL_BLANKING_MAX, 1,
			MT9V022_VERTICAL_BLANKING_DEF);

	mt9v022->subdev.ctrl_handler = &mt9v022->hdl;
	if (mt9v022->hdl.error) {
		int err = mt9v022->hdl.error;

		dev_err(&client->dev, "control initialisation err %d\n", err);
		return err;
	}
	v4l2_ctrl_auto_cluster(2, &mt9v022->autoexposure,
				V4L2_EXPOSURE_MANUAL, true);
	v4l2_ctrl_auto_cluster(2, &mt9v022->autogain, 0, true);

	mt9v022->chip_control = MT9V022_CHIP_CONTROL_DEFAULT;

	/*
	 * On some platforms the first read out line is corrupted.
	 * Workaround it by skipping if indicated by platform data.
	 */
	mt9v022->y_skip_top	= pdata ? pdata->y_skip_top : 0;
	mt9v022->rect.left	= MT9V022_COLUMN_SKIP;
	mt9v022->rect.top	= MT9V022_ROW_SKIP;
	mt9v022->rect.width	= MT9V022_MAX_WIDTH;
	mt9v022->rect.height	= MT9V022_MAX_HEIGHT;

	ret = mt9v022_video_probe(client);
	if (ret)
		v4l2_ctrl_handler_free(&mt9v022->hdl);

	return ret;
}

static int mt9v022_remove(struct i2c_client *client)
{
	struct mt9v022 *mt9v022 = to_mt9v022(client);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	v4l2_device_unregister_subdev(&mt9v022->subdev);
	if (ssdd->free_bus)
		ssdd->free_bus(ssdd);
	v4l2_ctrl_handler_free(&mt9v022->hdl);

	return 0;
}
static const struct i2c_device_id mt9v022_id[] = {
	{ "mt9v022", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9v022_id);

static struct i2c_driver mt9v022_i2c_driver = {
	.driver = {
		.name = "mt9v022",
	},
	.probe		= mt9v022_probe,
	.remove		= mt9v022_remove,
	.id_table	= mt9v022_id,
};

module_i2c_driver(mt9v022_i2c_driver);

MODULE_DESCRIPTION("Micron MT9V022 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
