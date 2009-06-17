/*
 * Driver for MT9T031 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski, DENX Software Engineering <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

/* mt9t031 i2c address 0x5d
 * The platform has to define i2c_board_info
 * and call i2c_register_board_info() */

/* mt9t031 selected register addresses */
#define MT9T031_CHIP_VERSION		0x00
#define MT9T031_ROW_START		0x01
#define MT9T031_COLUMN_START		0x02
#define MT9T031_WINDOW_HEIGHT		0x03
#define MT9T031_WINDOW_WIDTH		0x04
#define MT9T031_HORIZONTAL_BLANKING	0x05
#define MT9T031_VERTICAL_BLANKING	0x06
#define MT9T031_OUTPUT_CONTROL		0x07
#define MT9T031_SHUTTER_WIDTH_UPPER	0x08
#define MT9T031_SHUTTER_WIDTH		0x09
#define MT9T031_PIXEL_CLOCK_CONTROL	0x0a
#define MT9T031_FRAME_RESTART		0x0b
#define MT9T031_SHUTTER_DELAY		0x0c
#define MT9T031_RESET			0x0d
#define MT9T031_READ_MODE_1		0x1e
#define MT9T031_READ_MODE_2		0x20
#define MT9T031_READ_MODE_3		0x21
#define MT9T031_ROW_ADDRESS_MODE	0x22
#define MT9T031_COLUMN_ADDRESS_MODE	0x23
#define MT9T031_GLOBAL_GAIN		0x35
#define MT9T031_CHIP_ENABLE		0xF8

#define MT9T031_MAX_HEIGHT		1536
#define MT9T031_MAX_WIDTH		2048
#define MT9T031_MIN_HEIGHT		2
#define MT9T031_MIN_WIDTH		2
#define MT9T031_HORIZONTAL_BLANK	142
#define MT9T031_VERTICAL_BLANK		25
#define MT9T031_COLUMN_SKIP		32
#define MT9T031_ROW_SKIP		20

#define MT9T031_BUS_PARAM	(SOCAM_PCLK_SAMPLE_RISING |	\
	SOCAM_PCLK_SAMPLE_FALLING | SOCAM_HSYNC_ACTIVE_HIGH |	\
	SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_DATA_ACTIVE_HIGH |	\
	SOCAM_MASTER | SOCAM_DATAWIDTH_10)

static const struct soc_camera_data_format mt9t031_colour_formats[] = {
	{
		.name		= "Bayer (sRGB) 10 bit",
		.depth		= 10,
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}
};

struct mt9t031 {
	struct i2c_client *client;
	struct soc_camera_device icd;
	int model;	/* V4L2_IDENT_MT9T031* codes from v4l2-chip-ident.h */
	unsigned char autoexposure;
	u16 xskip;
	u16 yskip;
};

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

static int set_shutter(struct i2c_client *client, const u32 data)
{
	int ret;

	ret = reg_write(client, MT9T031_SHUTTER_WIDTH_UPPER, data >> 16);

	if (ret >= 0)
		ret = reg_write(client, MT9T031_SHUTTER_WIDTH, data & 0xffff);

	return ret;
}

static int get_shutter(struct i2c_client *client, u32 *data)
{
	int ret;

	ret = reg_read(client, MT9T031_SHUTTER_WIDTH_UPPER);
	*data = ret << 16;

	if (ret >= 0)
		ret = reg_read(client, MT9T031_SHUTTER_WIDTH);
	*data |= ret & 0xffff;

	return ret < 0 ? ret : 0;
}

static int mt9t031_init(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret;

	if (icl->power) {
		ret = icl->power(&client->dev, 1);
		if (ret < 0) {
			dev_err(icd->vdev->parent,
				"Platform failed to power-on the camera.\n");
			return ret;
		}
	}

	/* Disable chip output, synchronous option update */
	ret = reg_write(client, MT9T031_RESET, 1);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_RESET, 0);
	if (ret >= 0)
		ret = reg_clear(client, MT9T031_OUTPUT_CONTROL, 2);

	if (ret < 0 && icl->power)
		icl->power(&client->dev, 0);

	return ret >= 0 ? 0 : -EIO;
}

static int mt9t031_release(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct soc_camera_link *icl = client->dev.platform_data;

	/* Disable the chip */
	reg_clear(client, MT9T031_OUTPUT_CONTROL, 2);

	if (icl->power)
		icl->power(&client->dev, 0);

	return 0;
}

static int mt9t031_start_capture(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(icd->control);

	/* Switch to master "normal" mode */
	if (reg_set(client, MT9T031_OUTPUT_CONTROL, 2) < 0)
		return -EIO;
	return 0;
}

static int mt9t031_stop_capture(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(icd->control);

	/* Stop sensor readout */
	if (reg_clear(client, MT9T031_OUTPUT_CONTROL, 2) < 0)
		return -EIO;
	return 0;
}

static int mt9t031_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	struct i2c_client *client = to_i2c_client(icd->control);

	/* The caller should have queried our parameters, check anyway */
	if (flags & ~MT9T031_BUS_PARAM)
		return -EINVAL;

	if (flags & SOCAM_PCLK_SAMPLE_FALLING)
		reg_clear(client, MT9T031_PIXEL_CLOCK_CONTROL, 0x8000);
	else
		reg_set(client, MT9T031_PIXEL_CLOCK_CONTROL, 0x8000);

	return 0;
}

static unsigned long mt9t031_query_bus_param(struct soc_camera_device *icd)
{
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	struct soc_camera_link *icl = mt9t031->client->dev.platform_data;

	return soc_camera_apply_sensor_flags(icl, MT9T031_BUS_PARAM);
}

/* Round up minima and round down maxima */
static void recalculate_limits(struct soc_camera_device *icd,
			       u16 xskip, u16 yskip)
{
	icd->x_min = (MT9T031_COLUMN_SKIP + xskip - 1) / xskip;
	icd->y_min = (MT9T031_ROW_SKIP + yskip - 1) / yskip;
	icd->width_min = (MT9T031_MIN_WIDTH + xskip - 1) / xskip;
	icd->height_min = (MT9T031_MIN_HEIGHT + yskip - 1) / yskip;
	icd->width_max = MT9T031_MAX_WIDTH / xskip;
	icd->height_max = MT9T031_MAX_HEIGHT / yskip;
}

static int mt9t031_set_params(struct soc_camera_device *icd,
			      struct v4l2_rect *rect, u16 xskip, u16 yskip)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	int ret;
	u16 xbin, ybin, width, height, left, top;
	const u16 hblank = MT9T031_HORIZONTAL_BLANK,
		vblank = MT9T031_VERTICAL_BLANK;

	/* Make sure we don't exceed sensor limits */
	if (rect->left + rect->width > icd->width_max)
		rect->left = (icd->width_max - rect->width) / 2 + icd->x_min;

	if (rect->top + rect->height > icd->height_max)
		rect->top = (icd->height_max - rect->height) / 2 + icd->y_min;

	width = rect->width * xskip;
	height = rect->height * yskip;
	left = rect->left * xskip;
	top = rect->top * yskip;

	xbin = min(xskip, (u16)3);
	ybin = min(yskip, (u16)3);

	dev_dbg(&icd->dev, "xskip %u, width %u/%u, yskip %u, height %u/%u\n",
		xskip, width, rect->width, yskip, height, rect->height);

	/* Could just do roundup(rect->left, [xy]bin * 2); but this is cheaper */
	switch (xbin) {
	case 2:
		left = (left + 3) & ~3;
		break;
	case 3:
		left = roundup(left, 6);
	}

	switch (ybin) {
	case 2:
		top = (top + 3) & ~3;
		break;
	case 3:
		top = roundup(top, 6);
	}

	/* Disable register update, reconfigure atomically */
	ret = reg_set(client, MT9T031_OUTPUT_CONTROL, 1);
	if (ret < 0)
		return ret;

	/* Blanking and start values - default... */
	ret = reg_write(client, MT9T031_HORIZONTAL_BLANKING, hblank);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_VERTICAL_BLANKING, vblank);

	if (yskip != mt9t031->yskip || xskip != mt9t031->xskip) {
		/* Binning, skipping */
		if (ret >= 0)
			ret = reg_write(client, MT9T031_COLUMN_ADDRESS_MODE,
					((xbin - 1) << 4) | (xskip - 1));
		if (ret >= 0)
			ret = reg_write(client, MT9T031_ROW_ADDRESS_MODE,
					((ybin - 1) << 4) | (yskip - 1));
	}
	dev_dbg(&icd->dev, "new physical left %u, top %u\n", left, top);

	/* The caller provides a supported format, as guaranteed by
	 * icd->try_fmt_cap(), soc_camera_s_crop() and soc_camera_cropcap() */
	if (ret >= 0)
		ret = reg_write(client, MT9T031_COLUMN_START, left);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_ROW_START, top);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_WINDOW_WIDTH, width - 1);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_WINDOW_HEIGHT,
				height + icd->y_skip_top - 1);
	if (ret >= 0 && mt9t031->autoexposure) {
		ret = set_shutter(client, height + icd->y_skip_top + vblank);
		if (ret >= 0) {
			const u32 shutter_max = MT9T031_MAX_HEIGHT + vblank;
			const struct v4l2_queryctrl *qctrl =
				soc_camera_find_qctrl(icd->ops,
						      V4L2_CID_EXPOSURE);
			icd->exposure = (shutter_max / 2 + (height +
					 icd->y_skip_top + vblank - 1) *
					 (qctrl->maximum - qctrl->minimum)) /
				shutter_max + qctrl->minimum;
		}
	}

	/* Re-enable register update, commit all changes */
	if (ret >= 0)
		ret = reg_clear(client, MT9T031_OUTPUT_CONTROL, 1);

	return ret < 0 ? ret : 0;
}

static int mt9t031_set_crop(struct soc_camera_device *icd,
			    struct v4l2_rect *rect)
{
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);

	/* CROP - no change in scaling, or in limits */
	return mt9t031_set_params(icd, rect, mt9t031->xskip, mt9t031->yskip);
}

static int mt9t031_set_fmt(struct soc_camera_device *icd,
			   struct v4l2_format *f)
{
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	int ret;
	u16 xskip, yskip;
	struct v4l2_rect rect = {
		.left	= icd->x_current,
		.top	= icd->y_current,
		.width	= f->fmt.pix.width,
		.height	= f->fmt.pix.height,
	};

	/*
	 * try_fmt has put rectangle within limits.
	 * S_FMT - use binning and skipping for scaling, recalculate
	 * limits, used for cropping
	 */
	/* Is this more optimal than just a division? */
	for (xskip = 8; xskip > 1; xskip--)
		if (rect.width * xskip <= MT9T031_MAX_WIDTH)
			break;

	for (yskip = 8; yskip > 1; yskip--)
		if (rect.height * yskip <= MT9T031_MAX_HEIGHT)
			break;

	recalculate_limits(icd, xskip, yskip);

	ret = mt9t031_set_params(icd, &rect, xskip, yskip);
	if (!ret) {
		mt9t031->xskip = xskip;
		mt9t031->yskip = yskip;
	}

	return ret;
}

static int mt9t031_try_fmt(struct soc_camera_device *icd,
			   struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->height < MT9T031_MIN_HEIGHT)
		pix->height = MT9T031_MIN_HEIGHT;
	if (pix->height > MT9T031_MAX_HEIGHT)
		pix->height = MT9T031_MAX_HEIGHT;
	if (pix->width < MT9T031_MIN_WIDTH)
		pix->width = MT9T031_MIN_WIDTH;
	if (pix->width > MT9T031_MAX_WIDTH)
		pix->width = MT9T031_MAX_WIDTH;

	pix->width &= ~0x01; /* has to be even */
	pix->height &= ~0x01; /* has to be even */

	return 0;
}

static int mt9t031_get_chip_id(struct soc_camera_device *icd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != mt9t031->client->addr)
		return -ENODEV;

	id->ident	= mt9t031->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9t031_get_register(struct soc_camera_device *icd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = to_i2c_client(icd->control);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9t031_set_register(struct soc_camera_device *icd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = to_i2c_client(icd->control);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static const struct v4l2_queryctrl mt9t031_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontally",
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

static int mt9t031_video_probe(struct soc_camera_device *);
static void mt9t031_video_remove(struct soc_camera_device *);
static int mt9t031_get_control(struct soc_camera_device *, struct v4l2_control *);
static int mt9t031_set_control(struct soc_camera_device *, struct v4l2_control *);

static struct soc_camera_ops mt9t031_ops = {
	.owner			= THIS_MODULE,
	.probe			= mt9t031_video_probe,
	.remove			= mt9t031_video_remove,
	.init			= mt9t031_init,
	.release		= mt9t031_release,
	.start_capture		= mt9t031_start_capture,
	.stop_capture		= mt9t031_stop_capture,
	.set_crop		= mt9t031_set_crop,
	.set_fmt		= mt9t031_set_fmt,
	.try_fmt		= mt9t031_try_fmt,
	.set_bus_param		= mt9t031_set_bus_param,
	.query_bus_param	= mt9t031_query_bus_param,
	.controls		= mt9t031_controls,
	.num_controls		= ARRAY_SIZE(mt9t031_controls),
	.get_control		= mt9t031_get_control,
	.set_control		= mt9t031_set_control,
	.get_chip_id		= mt9t031_get_chip_id,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.get_register		= mt9t031_get_register,
	.set_register		= mt9t031_set_register,
#endif
};

static int mt9t031_get_control(struct soc_camera_device *icd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		data = reg_read(client, MT9T031_READ_MODE_2);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x8000);
		break;
	case V4L2_CID_HFLIP:
		data = reg_read(client, MT9T031_READ_MODE_2);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x4000);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = mt9t031->autoexposure;
		break;
	}
	return 0;
}

static int mt9t031_set_control(struct soc_camera_device *icd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	const struct v4l2_queryctrl *qctrl;
	int data;

	qctrl = soc_camera_find_qctrl(&mt9t031_ops, ctrl->id);

	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			data = reg_set(client, MT9T031_READ_MODE_2, 0x8000);
		else
			data = reg_clear(client, MT9T031_READ_MODE_2, 0x8000);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->value)
			data = reg_set(client, MT9T031_READ_MODE_2, 0x4000);
		else
			data = reg_clear(client, MT9T031_READ_MODE_2, 0x4000);
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

			dev_dbg(&icd->dev, "Setting gain %d\n", data);
			data = reg_write(client, MT9T031_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		} else {
			/* Pack it into 1.125..128 variable step, register values 9..0x7860 */
			/* We assume qctrl->maximum - qctrl->default_value - 1 > 0 */
			unsigned long range = qctrl->maximum - qctrl->default_value - 1;
			/* calculated gain: map 65..127 to 9..1024 step 0.125 */
			unsigned long gain = ((ctrl->value - qctrl->default_value - 1) *
					       1015 + range / 2) / range + 9;

			if (gain <= 32)		/* calculated gain 9..32 -> 9..32 */
				data = gain;
			else if (gain <= 64)	/* calculated gain 33..64 -> 0x51..0x60 */
				data = ((gain - 32) * 16 + 16) / 32 + 80;
			else
				/* calculated gain 65..1024 -> (1..120) << 8 + 0x60 */
				data = (((gain - 64 + 7) * 32) & 0xff00) | 0x60;

			dev_dbg(&icd->dev, "Setting gain from 0x%x to 0x%x\n",
				reg_read(client, MT9T031_GLOBAL_GAIN), data);
			data = reg_write(client, MT9T031_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		}

		/* Success */
		icd->gain = ctrl->value;
		break;
	case V4L2_CID_EXPOSURE:
		/* mt9t031 has maximum == default */
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		else {
			const unsigned long range = qctrl->maximum - qctrl->minimum;
			const u32 shutter = ((ctrl->value - qctrl->minimum) * 1048 +
					     range / 2) / range + 1;
			u32 old;

			get_shutter(client, &old);
			dev_dbg(&icd->dev, "Setting shutter width from %u to %u\n",
				old, shutter);
			if (set_shutter(client, shutter) < 0)
				return -EIO;
			icd->exposure = ctrl->value;
			mt9t031->autoexposure = 0;
		}
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->value) {
			const u16 vblank = MT9T031_VERTICAL_BLANK;
			const u32 shutter_max = MT9T031_MAX_HEIGHT + vblank;
			if (set_shutter(client, icd->height +
					icd->y_skip_top + vblank) < 0)
				return -EIO;
			qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_EXPOSURE);
			icd->exposure = (shutter_max / 2 + (icd->height +
					 icd->y_skip_top + vblank - 1) *
					 (qctrl->maximum - qctrl->minimum)) /
				shutter_max + qctrl->minimum;
			mt9t031->autoexposure = 1;
		} else
			mt9t031->autoexposure = 0;
		break;
	}
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int mt9t031_video_probe(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(icd->control);
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);
	s32 data;
	int ret;

	/* We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant. */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	/* Enable the chip */
	data = reg_write(client, MT9T031_CHIP_ENABLE, 1);
	dev_dbg(&icd->dev, "write: %d\n", data);

	/* Read out the chip version register */
	data = reg_read(client, MT9T031_CHIP_VERSION);

	switch (data) {
	case 0x1621:
		mt9t031->model = V4L2_IDENT_MT9T031;
		icd->formats = mt9t031_colour_formats;
		icd->num_formats = ARRAY_SIZE(mt9t031_colour_formats);
		break;
	default:
		ret = -ENODEV;
		dev_err(&icd->dev,
			"No MT9T031 chip detected, register read %x\n", data);
		goto ei2c;
	}

	dev_info(&icd->dev, "Detected a MT9T031 chip ID %x\n", data);

	/* Now that we know the model, we can start video */
	ret = soc_camera_video_start(icd);
	if (ret)
		goto evstart;

	return 0;

evstart:
ei2c:
	return ret;
}

static void mt9t031_video_remove(struct soc_camera_device *icd)
{
	struct mt9t031 *mt9t031 = container_of(icd, struct mt9t031, icd);

	dev_dbg(&icd->dev, "Video %x removed: %p, %p\n", mt9t031->client->addr,
		icd->dev.parent, icd->vdev);
	soc_camera_video_stop(icd);
}

static int mt9t031_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9t031 *mt9t031;
	struct soc_camera_device *icd;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret;

	if (!icl) {
		dev_err(&client->dev, "MT9T031 driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9t031 = kzalloc(sizeof(struct mt9t031), GFP_KERNEL);
	if (!mt9t031)
		return -ENOMEM;

	mt9t031->client = client;
	i2c_set_clientdata(client, mt9t031);

	/* Second stage probe - when a capture adapter is there */
	icd = &mt9t031->icd;
	icd->ops	= &mt9t031_ops;
	icd->control	= &client->dev;
	icd->x_min	= MT9T031_COLUMN_SKIP;
	icd->y_min	= MT9T031_ROW_SKIP;
	icd->x_current	= icd->x_min;
	icd->y_current	= icd->y_min;
	icd->width_min	= MT9T031_MIN_WIDTH;
	icd->width_max	= MT9T031_MAX_WIDTH;
	icd->height_min	= MT9T031_MIN_HEIGHT;
	icd->height_max	= MT9T031_MAX_HEIGHT;
	icd->y_skip_top	= 0;
	icd->iface	= icl->bus_id;
	/* Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width */
	mt9t031->autoexposure = 1;

	mt9t031->xskip = 1;
	mt9t031->yskip = 1;

	ret = soc_camera_device_register(icd);
	if (ret)
		goto eisdr;

	return 0;

eisdr:
	i2c_set_clientdata(client, NULL);
	kfree(mt9t031);
	return ret;
}

static int mt9t031_remove(struct i2c_client *client)
{
	struct mt9t031 *mt9t031 = i2c_get_clientdata(client);

	soc_camera_device_unregister(&mt9t031->icd);
	i2c_set_clientdata(client, NULL);
	kfree(mt9t031);

	return 0;
}

static const struct i2c_device_id mt9t031_id[] = {
	{ "mt9t031", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9t031_id);

static struct i2c_driver mt9t031_i2c_driver = {
	.driver = {
		.name = "mt9t031",
	},
	.probe		= mt9t031_probe,
	.remove		= mt9t031_remove,
	.id_table	= mt9t031_id,
};

static int __init mt9t031_mod_init(void)
{
	return i2c_add_driver(&mt9t031_i2c_driver);
}

static void __exit mt9t031_mod_exit(void)
{
	i2c_del_driver(&mt9t031_i2c_driver);
}

module_init(mt9t031_mod_init);
module_exit(mt9t031_mod_exit);

MODULE_DESCRIPTION("Micron MT9T031 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <lg@denx.de>");
MODULE_LICENSE("GPL v2");
