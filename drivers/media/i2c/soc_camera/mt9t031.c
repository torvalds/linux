/*
 * Driver for MT9T031 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski, DENX Software Engineering <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/soc_camera.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

/*
 * ATTENTION: this driver still cannot be used outside of the soc-camera
 * framework because of its PM implementation, using the video_device node.
 * If hardware becomes available for testing, alternative PM approaches shall
 * be considered and tested.
 */

/*
 * mt9t031 i2c address 0x5d
 * The platform has to define struct i2c_board_info objects and link to them
 * from struct soc_camera_host_desc
 */

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
#define MT9T031_MIN_WIDTH		18
#define MT9T031_HORIZONTAL_BLANK	142
#define MT9T031_VERTICAL_BLANK		25
#define MT9T031_COLUMN_SKIP		32
#define MT9T031_ROW_SKIP		20

struct mt9t031 {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct {
		/* exposure/auto-exposure cluster */
		struct v4l2_ctrl *autoexposure;
		struct v4l2_ctrl *exposure;
	};
	struct v4l2_rect rect;	/* Sensor window */
	struct v4l2_clk *clk;
	u16 xskip;
	u16 yskip;
	unsigned int total_h;
	unsigned short y_skip_top;	/* Lines to skip at the top */
};

static struct mt9t031 *to_mt9t031(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mt9t031, subdev);
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

static int mt9t031_idle(struct i2c_client *client)
{
	int ret;

	/* Disable chip output, synchronous option update */
	ret = reg_write(client, MT9T031_RESET, 1);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_RESET, 0);
	if (ret >= 0)
		ret = reg_clear(client, MT9T031_OUTPUT_CONTROL, 2);

	return ret >= 0 ? 0 : -EIO;
}

static int mt9t031_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (enable)
		/* Switch to master "normal" mode */
		ret = reg_set(client, MT9T031_OUTPUT_CONTROL, 2);
	else
		/* Stop sensor readout */
		ret = reg_clear(client, MT9T031_OUTPUT_CONTROL, 2);

	if (ret < 0)
		return -EIO;

	return 0;
}

/* target must be _even_ */
static u16 mt9t031_skip(s32 *source, s32 target, s32 max)
{
	unsigned int skip;

	if (*source < target + target / 2) {
		*source = target;
		return 1;
	}

	skip = min(max, *source + target / 2) / target;
	if (skip > 8)
		skip = 8;
	*source = target * skip;

	return skip;
}

/* rect is the sensor rectangle, the caller guarantees parameter validity */
static int mt9t031_set_params(struct i2c_client *client,
			      struct v4l2_rect *rect, u16 xskip, u16 yskip)
{
	struct mt9t031 *mt9t031 = to_mt9t031(client);
	int ret;
	u16 xbin, ybin;
	const u16 hblank = MT9T031_HORIZONTAL_BLANK,
		vblank = MT9T031_VERTICAL_BLANK;

	xbin = min(xskip, (u16)3);
	ybin = min(yskip, (u16)3);

	/*
	 * Could just do roundup(rect->left, [xy]bin * 2); but this is cheaper.
	 * There is always a valid suitably aligned value. The worst case is
	 * xbin = 3, width = 2048. Then we will start at 36, the last read out
	 * pixel will be 2083, which is < 2085 - first black pixel.
	 *
	 * MT9T031 datasheet imposes window left border alignment, depending on
	 * the selected xskip. Failing to conform to this requirement produces
	 * dark horizontal stripes in the image. However, even obeying to this
	 * requirement doesn't eliminate the stripes in all configurations. They
	 * appear "locally reproducibly," but can differ between tests under
	 * different lighting conditions.
	 */
	switch (xbin) {
	case 1:
		rect->left &= ~1;
		break;
	case 2:
		rect->left &= ~3;
		break;
	case 3:
		rect->left = rect->left > roundup(MT9T031_COLUMN_SKIP, 6) ?
			(rect->left / 6) * 6 : roundup(MT9T031_COLUMN_SKIP, 6);
	}

	rect->top &= ~1;

	dev_dbg(&client->dev, "skip %u:%u, rect %ux%u@%u:%u\n",
		xskip, yskip, rect->width, rect->height, rect->left, rect->top);

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
	dev_dbg(&client->dev, "new physical left %u, top %u\n",
		rect->left, rect->top);

	/*
	 * The caller provides a supported format, as guaranteed by
	 * .try_mbus_fmt(), soc_camera_s_crop() and soc_camera_cropcap()
	 */
	if (ret >= 0)
		ret = reg_write(client, MT9T031_COLUMN_START, rect->left);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_ROW_START, rect->top);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_WINDOW_WIDTH, rect->width - 1);
	if (ret >= 0)
		ret = reg_write(client, MT9T031_WINDOW_HEIGHT,
				rect->height + mt9t031->y_skip_top - 1);
	if (ret >= 0 && v4l2_ctrl_g_ctrl(mt9t031->autoexposure) == V4L2_EXPOSURE_AUTO) {
		mt9t031->total_h = rect->height + mt9t031->y_skip_top + vblank;

		ret = set_shutter(client, mt9t031->total_h);
	}

	/* Re-enable register update, commit all changes */
	if (ret >= 0)
		ret = reg_clear(client, MT9T031_OUTPUT_CONTROL, 1);

	if (ret >= 0) {
		mt9t031->rect = *rect;
		mt9t031->xskip = xskip;
		mt9t031->yskip = yskip;
	}

	return ret < 0 ? ret : 0;
}

static int mt9t031_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *a)
{
	struct v4l2_rect rect = a->c;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	rect.width = ALIGN(rect.width, 2);
	rect.height = ALIGN(rect.height, 2);

	soc_camera_limit_side(&rect.left, &rect.width,
		     MT9T031_COLUMN_SKIP, MT9T031_MIN_WIDTH, MT9T031_MAX_WIDTH);

	soc_camera_limit_side(&rect.top, &rect.height,
		     MT9T031_ROW_SKIP, MT9T031_MIN_HEIGHT, MT9T031_MAX_HEIGHT);

	return mt9t031_set_params(client, &rect, mt9t031->xskip, mt9t031->yskip);
}

static int mt9t031_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	a->c	= mt9t031->rect;
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9t031_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= MT9T031_COLUMN_SKIP;
	a->bounds.top			= MT9T031_ROW_SKIP;
	a->bounds.width			= MT9T031_MAX_WIDTH;
	a->bounds.height		= MT9T031_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9t031_g_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	mf->width	= mt9t031->rect.width / mt9t031->xskip;
	mf->height	= mt9t031->rect.height / mt9t031->yskip;
	mf->code	= MEDIA_BUS_FMT_SBGGR10_1X10;
	mf->colorspace	= V4L2_COLORSPACE_SRGB;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int mt9t031_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);
	u16 xskip, yskip;
	struct v4l2_rect rect = mt9t031->rect;

	/*
	 * try_fmt has put width and height within limits.
	 * S_FMT: use binning and skipping for scaling
	 */
	xskip = mt9t031_skip(&rect.width, mf->width, MT9T031_MAX_WIDTH);
	yskip = mt9t031_skip(&rect.height, mf->height, MT9T031_MAX_HEIGHT);

	mf->code	= MEDIA_BUS_FMT_SBGGR10_1X10;
	mf->colorspace	= V4L2_COLORSPACE_SRGB;

	/* mt9t031_set_params() doesn't change width and height */
	return mt9t031_set_params(client, &rect, xskip, yskip);
}

/*
 * If a user window larger than sensor window is requested, we'll increase the
 * sensor window.
 */
static int mt9t031_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	v4l_bound_align_image(
		&mf->width, MT9T031_MIN_WIDTH, MT9T031_MAX_WIDTH, 1,
		&mf->height, MT9T031_MIN_HEIGHT, MT9T031_MAX_HEIGHT, 1, 0);

	mf->code	= MEDIA_BUS_FMT_SBGGR10_1X10;
	mf->colorspace	= V4L2_COLORSPACE_SRGB;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9t031_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff)
		return -EINVAL;

	reg->size = 1;
	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9t031_s_register(struct v4l2_subdev *sd,
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

static int mt9t031_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9t031 *mt9t031 = container_of(ctrl->handler,
					       struct mt9t031, hdl);
	const u32 shutter_max = MT9T031_MAX_HEIGHT + MT9T031_VERTICAL_BLANK;
	s32 min, max;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_AUTO:
		min = mt9t031->exposure->minimum;
		max = mt9t031->exposure->maximum;
		mt9t031->exposure->val =
			(shutter_max / 2 + (mt9t031->total_h - 1) * (max - min))
				/ shutter_max + min;
		break;
	}
	return 0;
}

static int mt9t031_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9t031 *mt9t031 = container_of(ctrl->handler,
					       struct mt9t031, hdl);
	struct v4l2_subdev *sd = &mt9t031->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_ctrl *exp = mt9t031->exposure;
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			data = reg_set(client, MT9T031_READ_MODE_2, 0x8000);
		else
			data = reg_clear(client, MT9T031_READ_MODE_2, 0x8000);
		if (data < 0)
			return -EIO;
		return 0;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			data = reg_set(client, MT9T031_READ_MODE_2, 0x4000);
		else
			data = reg_clear(client, MT9T031_READ_MODE_2, 0x4000);
		if (data < 0)
			return -EIO;
		return 0;
	case V4L2_CID_GAIN:
		/* See Datasheet Table 7, Gain settings. */
		if (ctrl->val <= ctrl->default_value) {
			/* Pack it into 0..1 step 0.125, register values 0..8 */
			unsigned long range = ctrl->default_value - ctrl->minimum;
			data = ((ctrl->val - (s32)ctrl->minimum) * 8 + range / 2) / range;

			dev_dbg(&client->dev, "Setting gain %d\n", data);
			data = reg_write(client, MT9T031_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		} else {
			/* Pack it into 1.125..128 variable step, register values 9..0x7860 */
			/* We assume qctrl->maximum - qctrl->default_value - 1 > 0 */
			unsigned long range = ctrl->maximum - ctrl->default_value - 1;
			/* calculated gain: map 65..127 to 9..1024 step 0.125 */
			unsigned long gain = ((ctrl->val - (s32)ctrl->default_value - 1) *
					       1015 + range / 2) / range + 9;

			if (gain <= 32)		/* calculated gain 9..32 -> 9..32 */
				data = gain;
			else if (gain <= 64)	/* calculated gain 33..64 -> 0x51..0x60 */
				data = ((gain - 32) * 16 + 16) / 32 + 80;
			else
				/* calculated gain 65..1024 -> (1..120) << 8 + 0x60 */
				data = (((gain - 64 + 7) * 32) & 0xff00) | 0x60;

			dev_dbg(&client->dev, "Set gain from 0x%x to 0x%x\n",
				reg_read(client, MT9T031_GLOBAL_GAIN), data);
			data = reg_write(client, MT9T031_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		}
		return 0;

	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->val == V4L2_EXPOSURE_MANUAL) {
			unsigned int range = exp->maximum - exp->minimum;
			unsigned int shutter = ((exp->val - (s32)exp->minimum) * 1048 +
						 range / 2) / range + 1;
			u32 old;

			get_shutter(client, &old);
			dev_dbg(&client->dev, "Set shutter from %u to %u\n",
				old, shutter);
			if (set_shutter(client, shutter) < 0)
				return -EIO;
		} else {
			const u16 vblank = MT9T031_VERTICAL_BLANK;
			mt9t031->total_h = mt9t031->rect.height +
				mt9t031->y_skip_top + vblank;

			if (set_shutter(client, mt9t031->total_h) < 0)
				return -EIO;
		}
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Power Management:
 * This function does nothing for now but must be present for pm to work
 */
static int mt9t031_runtime_suspend(struct device *dev)
{
	return 0;
}

/*
 * Power Management:
 * COLUMN_ADDRESS_MODE and ROW_ADDRESS_MODE are not rewritten if unchanged
 * they are however changed at reset if the platform hook is present
 * thus we rewrite them with the values stored by the driver
 */
static int mt9t031_runtime_resume(struct device *dev)
{
	struct video_device *vdev = to_video_device(dev);
	struct v4l2_subdev *sd = soc_camera_vdev_to_subdev(vdev);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	int ret;
	u16 xbin, ybin;

	xbin = min(mt9t031->xskip, (u16)3);
	ybin = min(mt9t031->yskip, (u16)3);

	ret = reg_write(client, MT9T031_COLUMN_ADDRESS_MODE,
		((xbin - 1) << 4) | (mt9t031->xskip - 1));
	if (ret < 0)
		return ret;

	ret = reg_write(client, MT9T031_ROW_ADDRESS_MODE,
		((ybin - 1) << 4) | (mt9t031->yskip - 1));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct dev_pm_ops mt9t031_dev_pm_ops = {
	.runtime_suspend	= mt9t031_runtime_suspend,
	.runtime_resume		= mt9t031_runtime_resume,
};

static struct device_type mt9t031_dev_type = {
	.name	= "MT9T031",
	.pm	= &mt9t031_dev_pm_ops,
};

static int mt9t031_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct video_device *vdev = soc_camera_i2c_to_vdev(client);
	struct mt9t031 *mt9t031 = to_mt9t031(client);
	int ret;

	if (on) {
		ret = soc_camera_power_on(&client->dev, ssdd, mt9t031->clk);
		if (ret < 0)
			return ret;
		if (vdev)
			/* Not needed during probing, when vdev isn't available yet */
			vdev->dev.type = &mt9t031_dev_type;
	} else {
		if (vdev)
			vdev->dev.type = NULL;
		soc_camera_power_off(&client->dev, ssdd, mt9t031->clk);
	}

	return 0;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9t031_video_probe(struct i2c_client *client)
{
	struct mt9t031 *mt9t031 = to_mt9t031(client);
	s32 data;
	int ret;

	ret = mt9t031_s_power(&mt9t031->subdev, 1);
	if (ret < 0)
		return ret;

	ret = mt9t031_idle(client);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to initialise the camera\n");
		goto done;
	}

	/* Read out the chip version register */
	data = reg_read(client, MT9T031_CHIP_VERSION);

	switch (data) {
	case 0x1621:
		break;
	default:
		dev_err(&client->dev,
			"No MT9T031 chip detected, register read %x\n", data);
		ret = -ENODEV;
		goto done;
	}

	dev_info(&client->dev, "Detected a MT9T031 chip ID %x\n", data);

	ret = v4l2_ctrl_handler_setup(&mt9t031->hdl);

done:
	mt9t031_s_power(&mt9t031->subdev, 0);

	return ret;
}

static int mt9t031_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	*lines = mt9t031->y_skip_top;

	return 0;
}

static const struct v4l2_ctrl_ops mt9t031_ctrl_ops = {
	.g_volatile_ctrl = mt9t031_g_volatile_ctrl,
	.s_ctrl = mt9t031_s_ctrl,
};

static struct v4l2_subdev_core_ops mt9t031_subdev_core_ops = {
	.s_power	= mt9t031_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= mt9t031_g_register,
	.s_register	= mt9t031_s_register,
#endif
};

static int mt9t031_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    u32 *code)
{
	if (index)
		return -EINVAL;

	*code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int mt9t031_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	cfg->flags = V4L2_MBUS_MASTER | V4L2_MBUS_PCLK_SAMPLE_RISING |
		V4L2_MBUS_PCLK_SAMPLE_FALLING | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = soc_camera_apply_board_flags(ssdd, cfg);

	return 0;
}

static int mt9t031_s_mbus_config(struct v4l2_subdev *sd,
				const struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	if (soc_camera_apply_board_flags(ssdd, cfg) &
	    V4L2_MBUS_PCLK_SAMPLE_FALLING)
		return reg_clear(client, MT9T031_PIXEL_CLOCK_CONTROL, 0x8000);
	else
		return reg_set(client, MT9T031_PIXEL_CLOCK_CONTROL, 0x8000);
}

static struct v4l2_subdev_video_ops mt9t031_subdev_video_ops = {
	.s_stream	= mt9t031_s_stream,
	.s_mbus_fmt	= mt9t031_s_fmt,
	.g_mbus_fmt	= mt9t031_g_fmt,
	.try_mbus_fmt	= mt9t031_try_fmt,
	.s_crop		= mt9t031_s_crop,
	.g_crop		= mt9t031_g_crop,
	.cropcap	= mt9t031_cropcap,
	.enum_mbus_fmt	= mt9t031_enum_fmt,
	.g_mbus_config	= mt9t031_g_mbus_config,
	.s_mbus_config	= mt9t031_s_mbus_config,
};

static struct v4l2_subdev_sensor_ops mt9t031_subdev_sensor_ops = {
	.g_skip_top_lines	= mt9t031_g_skip_top_lines,
};

static struct v4l2_subdev_ops mt9t031_subdev_ops = {
	.core	= &mt9t031_subdev_core_ops,
	.video	= &mt9t031_subdev_video_ops,
	.sensor	= &mt9t031_subdev_sensor_ops,
};

static int mt9t031_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9t031 *mt9t031;
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret;

	if (!ssdd) {
		dev_err(&client->dev, "MT9T031 driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9t031 = devm_kzalloc(&client->dev, sizeof(struct mt9t031), GFP_KERNEL);
	if (!mt9t031)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&mt9t031->subdev, client, &mt9t031_subdev_ops);
	v4l2_ctrl_handler_init(&mt9t031->hdl, 5);
	v4l2_ctrl_new_std(&mt9t031->hdl, &mt9t031_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&mt9t031->hdl, &mt9t031_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&mt9t031->hdl, &mt9t031_ctrl_ops,
			V4L2_CID_GAIN, 0, 127, 1, 64);

	/*
	 * Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width
	 */
	mt9t031->autoexposure = v4l2_ctrl_new_std_menu(&mt9t031->hdl,
			&mt9t031_ctrl_ops, V4L2_CID_EXPOSURE_AUTO, 1, 0,
			V4L2_EXPOSURE_AUTO);
	mt9t031->exposure = v4l2_ctrl_new_std(&mt9t031->hdl, &mt9t031_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 255, 1, 255);

	mt9t031->subdev.ctrl_handler = &mt9t031->hdl;
	if (mt9t031->hdl.error)
		return mt9t031->hdl.error;

	v4l2_ctrl_auto_cluster(2, &mt9t031->autoexposure,
				V4L2_EXPOSURE_MANUAL, true);

	mt9t031->y_skip_top	= 0;
	mt9t031->rect.left	= MT9T031_COLUMN_SKIP;
	mt9t031->rect.top	= MT9T031_ROW_SKIP;
	mt9t031->rect.width	= MT9T031_MAX_WIDTH;
	mt9t031->rect.height	= MT9T031_MAX_HEIGHT;

	mt9t031->xskip = 1;
	mt9t031->yskip = 1;

	mt9t031->clk = v4l2_clk_get(&client->dev, "mclk");
	if (IS_ERR(mt9t031->clk)) {
		ret = PTR_ERR(mt9t031->clk);
		goto eclkget;
	}

	ret = mt9t031_video_probe(client);
	if (ret) {
		v4l2_clk_put(mt9t031->clk);
eclkget:
		v4l2_ctrl_handler_free(&mt9t031->hdl);
	}

	return ret;
}

static int mt9t031_remove(struct i2c_client *client)
{
	struct mt9t031 *mt9t031 = to_mt9t031(client);

	v4l2_clk_put(mt9t031->clk);
	v4l2_device_unregister_subdev(&mt9t031->subdev);
	v4l2_ctrl_handler_free(&mt9t031->hdl);

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

module_i2c_driver(mt9t031_i2c_driver);

MODULE_DESCRIPTION("Micron MT9T031 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <lg@denx.de>");
MODULE_LICENSE("GPL v2");
