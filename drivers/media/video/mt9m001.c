/*
 * Driver for MT9M001 CMOS Image Sensor from Micron
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
#include <linux/log2.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

/*
 * mt9m001 i2c address 0x5d
 * The platform has to define ctruct i2c_board_info objects and link to them
 * from struct soc_camera_link
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

/* MT9M001 has only one fixed colorspace per pixelcode */
struct mt9m001_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct mt9m001_datafmt *mt9m001_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct mt9m001_datafmt *fmt,
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
	{V4L2_MBUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
};

static const struct mt9m001_datafmt mt9m001_monochrome_fmts[] = {
	/* Order important - see above */
	{V4L2_MBUS_FMT_Y10_1X10, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_Y8_1X8, V4L2_COLORSPACE_JPEG},
};

struct mt9m001 {
	struct v4l2_subdev subdev;
	struct v4l2_rect rect;	/* Sensor window */
	const struct mt9m001_datafmt *fmt;
	const struct mt9m001_datafmt *fmts;
	int num_fmts;
	int model;	/* V4L2_IDENT_MT9M001* codes from v4l2-chip-ident.h */
	unsigned int gain;
	unsigned int exposure;
	unsigned short y_skip_top;	/* Lines to skip at the top */
	unsigned char autoexposure;
};

static struct mt9m001 *to_mt9m001(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mt9m001, subdev);
}

static int reg_read(struct i2c_client *client, const u8 reg)
{
	s32 data = i2c_smbus_read_word_data(client, reg);
	return data < 0 ? data : swab16(data);
}

static int reg_write(struct i2c_client *client, const u8 reg,
		     const u16 data)
{
	return i2c_smbus_write_word_data(client, reg, swab16(data));
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

static int mt9m001_init(struct i2c_client *client)
{
	int ret;

	dev_dbg(&client->dev, "%s\n", __func__);

	/*
	 * We don't know, whether platform provides reset, issue a soft reset
	 * too. This returns all registers to their default values.
	 */
	ret = reg_write(client, MT9M001_RESET, 1);
	if (!ret)
		ret = reg_write(client, MT9M001_RESET, 0);

	/* Disable chip, synchronous option update */
	if (!ret)
		ret = reg_write(client, MT9M001_OUTPUT_CONTROL, 0);

	return ret;
}

static int mt9m001_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* Switch to master "normal" mode or stop sensor readout */
	if (reg_write(client, MT9M001_OUTPUT_CONTROL, enable ? 2 : 0) < 0)
		return -EIO;
	return 0;
}

static int mt9m001_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	unsigned long width_flag = flags & SOCAM_DATAWIDTH_MASK;

	/* Only one width bit may be set */
	if (!is_power_of_2(width_flag))
		return -EINVAL;

	if (icl->set_bus_param)
		return icl->set_bus_param(icl, width_flag);

	/*
	 * Without board specific bus width settings we only support the
	 * sensors native bus width
	 */
	if (width_flag == SOCAM_DATAWIDTH_10)
		return 0;

	return -EINVAL;
}

static unsigned long mt9m001_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	/* MT9M001 has all capture_format parameters fixed */
	unsigned long flags = SOCAM_PCLK_SAMPLE_FALLING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_MASTER;

	if (icl->query_bus_param)
		flags |= icl->query_bus_param(icl) & SOCAM_DATAWIDTH_MASK;
	else
		flags |= SOCAM_DATAWIDTH_10;

	return soc_camera_apply_sensor_flags(icl, flags);
}

static int mt9m001_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_rect rect = a->c;
	struct soc_camera_device *icd = client->dev.platform_data;
	int ret;
	const u16 hblank = 9, vblank = 25;
	unsigned int total_h;

	if (mt9m001->fmts == mt9m001_colour_fmts)
		/*
		 * Bayer format - even number of rows for simplicity,
		 * but let the user play with the top row.
		 */
		rect.height = ALIGN(rect.height, 2);

	/* Datasheet requirement: see register description */
	rect.width = ALIGN(rect.width, 2);
	rect.left = ALIGN(rect.left, 2);

	soc_camera_limit_side(&rect.left, &rect.width,
		     MT9M001_COLUMN_SKIP, MT9M001_MIN_WIDTH, MT9M001_MAX_WIDTH);

	soc_camera_limit_side(&rect.top, &rect.height,
		     MT9M001_ROW_SKIP, MT9M001_MIN_HEIGHT, MT9M001_MAX_HEIGHT);

	total_h = rect.height + mt9m001->y_skip_top + vblank;

	/* Blanking and start values - default... */
	ret = reg_write(client, MT9M001_HORIZONTAL_BLANKING, hblank);
	if (!ret)
		ret = reg_write(client, MT9M001_VERTICAL_BLANKING, vblank);

	/*
	 * The caller provides a supported format, as verified per
	 * call to icd->try_fmt()
	 */
	if (!ret)
		ret = reg_write(client, MT9M001_COLUMN_START, rect.left);
	if (!ret)
		ret = reg_write(client, MT9M001_ROW_START, rect.top);
	if (!ret)
		ret = reg_write(client, MT9M001_WINDOW_WIDTH, rect.width - 1);
	if (!ret)
		ret = reg_write(client, MT9M001_WINDOW_HEIGHT,
				rect.height + mt9m001->y_skip_top - 1);
	if (!ret && mt9m001->autoexposure) {
		ret = reg_write(client, MT9M001_SHUTTER_WIDTH, total_h);
		if (!ret) {
			const struct v4l2_queryctrl *qctrl =
				soc_camera_find_qctrl(icd->ops,
						      V4L2_CID_EXPOSURE);
			mt9m001->exposure = (524 + (total_h - 1) *
				 (qctrl->maximum - qctrl->minimum)) /
				1048 + qctrl->minimum;
		}
	}

	if (!ret)
		mt9m001->rect = rect;

	return ret;
}

static int mt9m001_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	a->c	= mt9m001->rect;
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9m001_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= MT9M001_COLUMN_SKIP;
	a->bounds.top			= MT9M001_ROW_SKIP;
	a->bounds.width			= MT9M001_MAX_WIDTH;
	a->bounds.height		= MT9M001_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9m001_g_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	mf->width	= mt9m001->rect.width;
	mf->height	= mt9m001->rect.height;
	mf->code	= mt9m001->fmt->code;
	mf->colorspace	= mt9m001->fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int mt9m001_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct v4l2_crop a = {
		.c = {
			.left	= mt9m001->rect.left,
			.top	= mt9m001->rect.top,
			.width	= mf->width,
			.height	= mf->height,
		},
	};
	int ret;

	/* No support for scaling so far, just crop. TODO: use skipping */
	ret = mt9m001_s_crop(sd, &a);
	if (!ret) {
		mf->width	= mt9m001->rect.width;
		mf->height	= mt9m001->rect.height;
		mt9m001->fmt	= mt9m001_find_datafmt(mf->code,
					mt9m001->fmts, mt9m001->num_fmts);
		mf->colorspace	= mt9m001->fmt->colorspace;
	}

	return ret;
}

static int mt9m001_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	const struct mt9m001_datafmt *fmt;

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

	return 0;
}

static int mt9m001_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident	= mt9m001->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m001_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	reg->size = 2;
	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9m001_s_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static const struct v4l2_queryctrl mt9m001_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Gain",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 64,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure",
		.minimum	= 1,
		.maximum	= 255,
		.step		= 1,
		.default_value	= 255,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id		= V4L2_CID_EXPOSURE_AUTO,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Automatic Exposure",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	}
};

static struct soc_camera_ops mt9m001_ops = {
	.set_bus_param		= mt9m001_set_bus_param,
	.query_bus_param	= mt9m001_query_bus_param,
	.controls		= mt9m001_controls,
	.num_controls		= ARRAY_SIZE(mt9m001_controls),
};

static int mt9m001_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		data = reg_read(client, MT9M001_READ_OPTIONS2);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x8000);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = mt9m001->autoexposure;
		break;
	case V4L2_CID_GAIN:
		ctrl->value = mt9m001->gain;
		break;
	case V4L2_CID_EXPOSURE:
		ctrl->value = mt9m001->exposure;
		break;
	}
	return 0;
}

static int mt9m001_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct soc_camera_device *icd = client->dev.platform_data;
	const struct v4l2_queryctrl *qctrl;
	int data;

	qctrl = soc_camera_find_qctrl(&mt9m001_ops, ctrl->id);

	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			data = reg_set(client, MT9M001_READ_OPTIONS2, 0x8000);
		else
			data = reg_clear(client, MT9M001_READ_OPTIONS2, 0x8000);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_GAIN:
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		/* See Datasheet Table 7, Gain settings. */
		if (ctrl->value <= qctrl->default_value) {
			/* Pack it into 0..1 step 0.125, register values 0..8 */
			unsigned long range = qctrl->default_value - qctrl->minimum;
			data = ((ctrl->value - qctrl->minimum) * 8 + range / 2) / range;

			dev_dbg(&client->dev, "Setting gain %d\n", data);
			data = reg_write(client, MT9M001_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		} else {
			/* Pack it into 1.125..15 variable step, register values 9..67 */
			/* We assume qctrl->maximum - qctrl->default_value - 1 > 0 */
			unsigned long range = qctrl->maximum - qctrl->default_value - 1;
			unsigned long gain = ((ctrl->value - qctrl->default_value - 1) *
					       111 + range / 2) / range + 9;

			if (gain <= 32)
				data = gain;
			else if (gain <= 64)
				data = ((gain - 32) * 16 + 16) / 32 + 80;
			else
				data = ((gain - 64) * 7 + 28) / 56 + 96;

			dev_dbg(&client->dev, "Setting gain from %d to %d\n",
				 reg_read(client, MT9M001_GLOBAL_GAIN), data);
			data = reg_write(client, MT9M001_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		}

		/* Success */
		mt9m001->gain = ctrl->value;
		break;
	case V4L2_CID_EXPOSURE:
		/* mt9m001 has maximum == default */
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		else {
			unsigned long range = qctrl->maximum - qctrl->minimum;
			unsigned long shutter = ((ctrl->value - qctrl->minimum) * 1048 +
						 range / 2) / range + 1;

			dev_dbg(&client->dev,
				"Setting shutter width from %d to %lu\n",
				reg_read(client, MT9M001_SHUTTER_WIDTH),
				shutter);
			if (reg_write(client, MT9M001_SHUTTER_WIDTH, shutter) < 0)
				return -EIO;
			mt9m001->exposure = ctrl->value;
			mt9m001->autoexposure = 0;
		}
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->value) {
			const u16 vblank = 25;
			unsigned int total_h = mt9m001->rect.height +
				mt9m001->y_skip_top + vblank;
			if (reg_write(client, MT9M001_SHUTTER_WIDTH,
				      total_h) < 0)
				return -EIO;
			qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_EXPOSURE);
			mt9m001->exposure = (524 + (total_h - 1) *
				 (qctrl->maximum - qctrl->minimum)) /
				1048 + qctrl->minimum;
			mt9m001->autoexposure = 1;
		} else
			mt9m001->autoexposure = 0;
		break;
	}
	return 0;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9m001_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	s32 data;
	unsigned long flags;
	int ret;

	/*
	 * We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant.
	 */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	/* Enable the chip */
	data = reg_write(client, MT9M001_CHIP_ENABLE, 1);
	dev_dbg(&client->dev, "write: %d\n", data);

	/* Read out the chip version register */
	data = reg_read(client, MT9M001_CHIP_VERSION);

	/* must be 0x8411 or 0x8421 for colour sensor and 8431 for bw */
	switch (data) {
	case 0x8411:
	case 0x8421:
		mt9m001->model = V4L2_IDENT_MT9M001C12ST;
		mt9m001->fmts = mt9m001_colour_fmts;
		break;
	case 0x8431:
		mt9m001->model = V4L2_IDENT_MT9M001C12STM;
		mt9m001->fmts = mt9m001_monochrome_fmts;
		break;
	default:
		dev_err(&client->dev,
			"No MT9M001 chip detected, register read %x\n", data);
		return -ENODEV;
	}

	mt9m001->num_fmts = 0;

	/*
	 * This is a 10bit sensor, so by default we only allow 10bit.
	 * The platform may support different bus widths due to
	 * different routing of the data lines.
	 */
	if (icl->query_bus_param)
		flags = icl->query_bus_param(icl);
	else
		flags = SOCAM_DATAWIDTH_10;

	if (flags & SOCAM_DATAWIDTH_10)
		mt9m001->num_fmts++;
	else
		mt9m001->fmts++;

	if (flags & SOCAM_DATAWIDTH_8)
		mt9m001->num_fmts++;

	mt9m001->fmt = &mt9m001->fmts[0];

	dev_info(&client->dev, "Detected a MT9M001 chip ID %x (%s)\n", data,
		 data == 0x8431 ? "C12STM" : "C12ST");

	ret = mt9m001_init(client);
	if (ret < 0)
		dev_err(&client->dev, "Failed to initialise the camera\n");

	/* mt9m001_init() has reset the chip, returning registers to defaults */
	mt9m001->gain = 64;
	mt9m001->exposure = 255;

	return ret;
}

static void mt9m001_video_remove(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);

	dev_dbg(&icd->dev, "Video removed: %p, %p\n",
		icd->dev.parent, icd->vdev);
	if (icl->free_bus)
		icl->free_bus(icl);
}

static int mt9m001_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	*lines = mt9m001->y_skip_top;

	return 0;
}

static struct v4l2_subdev_core_ops mt9m001_subdev_core_ops = {
	.g_ctrl		= mt9m001_g_ctrl,
	.s_ctrl		= mt9m001_s_ctrl,
	.g_chip_ident	= mt9m001_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= mt9m001_g_register,
	.s_register	= mt9m001_s_register,
#endif
};

static int mt9m001_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m001 *mt9m001 = to_mt9m001(client);

	if (index >= mt9m001->num_fmts)
		return -EINVAL;

	*code = mt9m001->fmts[index].code;
	return 0;
}

static struct v4l2_subdev_video_ops mt9m001_subdev_video_ops = {
	.s_stream	= mt9m001_s_stream,
	.s_mbus_fmt	= mt9m001_s_fmt,
	.g_mbus_fmt	= mt9m001_g_fmt,
	.try_mbus_fmt	= mt9m001_try_fmt,
	.s_crop		= mt9m001_s_crop,
	.g_crop		= mt9m001_g_crop,
	.cropcap	= mt9m001_cropcap,
	.enum_mbus_fmt	= mt9m001_enum_fmt,
};

static struct v4l2_subdev_sensor_ops mt9m001_subdev_sensor_ops = {
	.g_skip_top_lines	= mt9m001_g_skip_top_lines,
};

static struct v4l2_subdev_ops mt9m001_subdev_ops = {
	.core	= &mt9m001_subdev_core_ops,
	.video	= &mt9m001_subdev_video_ops,
	.sensor	= &mt9m001_subdev_sensor_ops,
};

static int mt9m001_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9m001 *mt9m001;
	struct soc_camera_device *icd = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl;
	int ret;

	if (!icd) {
		dev_err(&client->dev, "MT9M001: missing soc-camera data!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl) {
		dev_err(&client->dev, "MT9M001 driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9m001 = kzalloc(sizeof(struct mt9m001), GFP_KERNEL);
	if (!mt9m001)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&mt9m001->subdev, client, &mt9m001_subdev_ops);

	/* Second stage probe - when a capture adapter is there */
	icd->ops		= &mt9m001_ops;

	mt9m001->y_skip_top	= 0;
	mt9m001->rect.left	= MT9M001_COLUMN_SKIP;
	mt9m001->rect.top	= MT9M001_ROW_SKIP;
	mt9m001->rect.width	= MT9M001_MAX_WIDTH;
	mt9m001->rect.height	= MT9M001_MAX_HEIGHT;

	/*
	 * Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width
	 */
	mt9m001->autoexposure = 1;

	ret = mt9m001_video_probe(icd, client);
	if (ret) {
		icd->ops = NULL;
		kfree(mt9m001);
	}

	return ret;
}

static int mt9m001_remove(struct i2c_client *client)
{
	struct mt9m001 *mt9m001 = to_mt9m001(client);
	struct soc_camera_device *icd = client->dev.platform_data;

	icd->ops = NULL;
	mt9m001_video_remove(icd);
	kfree(mt9m001);

	return 0;
}

static const struct i2c_device_id mt9m001_id[] = {
	{ "mt9m001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9m001_id);

static struct i2c_driver mt9m001_i2c_driver = {
	.driver = {
		.name = "mt9m001",
	},
	.probe		= mt9m001_probe,
	.remove		= mt9m001_remove,
	.id_table	= mt9m001_id,
};

static int __init mt9m001_mod_init(void)
{
	return i2c_add_driver(&mt9m001_i2c_driver);
}

static void __exit mt9m001_mod_exit(void)
{
	i2c_del_driver(&mt9m001_i2c_driver);
}

module_init(mt9m001_mod_init);
module_exit(mt9m001_mod_exit);

MODULE_DESCRIPTION("Micron MT9M001 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
