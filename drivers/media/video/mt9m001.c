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

#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

#ifdef CONFIG_MT9M001_PCA9536_SWITCH
#include <asm/gpio.h>
#endif

/* mt9m001 i2c address 0x5d
 * The platform has to define i2c_board_info
 * and call i2c_register_board_info() */

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

static const struct soc_camera_data_format mt9m001_colour_formats[] = {
	/* Order important: first natively supported,
	 * second supported with a GPIO extender */
	{
		.name		= "Bayer (sRGB) 10 bit",
		.depth		= 10,
		.fourcc		= V4L2_PIX_FMT_SBGGR16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}, {
		.name		= "Bayer (sRGB) 8 bit",
		.depth		= 8,
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}
};

static const struct soc_camera_data_format mt9m001_monochrome_formats[] = {
	/* Order important - see above */
	{
		.name		= "Monochrome 10 bit",
		.depth		= 10,
		.fourcc		= V4L2_PIX_FMT_Y16,
	}, {
		.name		= "Monochrome 8 bit",
		.depth		= 8,
		.fourcc		= V4L2_PIX_FMT_GREY,
	},
};

struct mt9m001 {
	struct i2c_client *client;
	struct soc_camera_device icd;
	int model;	/* V4L2_IDENT_MT9M001* codes from v4l2-chip-ident.h */
	int switch_gpio;
	unsigned char autoexposure;
	unsigned char datawidth;
};

static int reg_read(struct soc_camera_device *icd, const u8 reg)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	struct i2c_client *client = mt9m001->client;
	s32 data = i2c_smbus_read_word_data(client, reg);
	return data < 0 ? data : swab16(data);
}

static int reg_write(struct soc_camera_device *icd, const u8 reg,
		     const u16 data)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	return i2c_smbus_write_word_data(mt9m001->client, reg, swab16(data));
}

static int reg_set(struct soc_camera_device *icd, const u8 reg,
		   const u16 data)
{
	int ret;

	ret = reg_read(icd, reg);
	if (ret < 0)
		return ret;
	return reg_write(icd, reg, ret | data);
}

static int reg_clear(struct soc_camera_device *icd, const u8 reg,
		     const u16 data)
{
	int ret;

	ret = reg_read(icd, reg);
	if (ret < 0)
		return ret;
	return reg_write(icd, reg, ret & ~data);
}

static int mt9m001_init(struct soc_camera_device *icd)
{
	int ret;

	/* Disable chip, synchronous option update */
	dev_dbg(icd->vdev->dev, "%s\n", __func__);

	ret = reg_write(icd, MT9M001_RESET, 1);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_RESET, 0);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_OUTPUT_CONTROL, 0);

	return ret >= 0 ? 0 : -EIO;
}

static int mt9m001_release(struct soc_camera_device *icd)
{
	/* Disable the chip */
	reg_write(icd, MT9M001_OUTPUT_CONTROL, 0);
	return 0;
}

static int mt9m001_start_capture(struct soc_camera_device *icd)
{
	/* Switch to master "normal" mode */
	if (reg_write(icd, MT9M001_OUTPUT_CONTROL, 2) < 0)
		return -EIO;
	return 0;
}

static int mt9m001_stop_capture(struct soc_camera_device *icd)
{
	/* Stop sensor readout */
	if (reg_write(icd, MT9M001_OUTPUT_CONTROL, 0) < 0)
		return -EIO;
	return 0;
}

static int bus_switch_request(struct mt9m001 *mt9m001,
			      struct soc_camera_link *icl)
{
#ifdef CONFIG_MT9M001_PCA9536_SWITCH
	int ret;
	unsigned int gpio = icl->gpio;

	if (gpio_is_valid(gpio)) {
		/* We have a data bus switch. */
		ret = gpio_request(gpio, "mt9m001");
		if (ret < 0) {
			dev_err(&mt9m001->client->dev, "Cannot get GPIO %u\n",
				gpio);
			return ret;
		}

		ret = gpio_direction_output(gpio, 0);
		if (ret < 0) {
			dev_err(&mt9m001->client->dev,
				"Cannot set GPIO %u to output\n", gpio);
			gpio_free(gpio);
			return ret;
		}
	}

	mt9m001->switch_gpio = gpio;
#else
	mt9m001->switch_gpio = -EINVAL;
#endif
	return 0;
}

static void bus_switch_release(struct mt9m001 *mt9m001)
{
#ifdef CONFIG_MT9M001_PCA9536_SWITCH
	if (gpio_is_valid(mt9m001->switch_gpio))
		gpio_free(mt9m001->switch_gpio);
#endif
}

static int bus_switch_act(struct mt9m001 *mt9m001, int go8bit)
{
#ifdef CONFIG_MT9M001_PCA9536_SWITCH
	if (!gpio_is_valid(mt9m001->switch_gpio))
		return -ENODEV;

	gpio_set_value_cansleep(mt9m001->switch_gpio, go8bit);
	return 0;
#else
	return -ENODEV;
#endif
}

static int bus_switch_possible(struct mt9m001 *mt9m001)
{
#ifdef CONFIG_MT9M001_PCA9536_SWITCH
	return gpio_is_valid(mt9m001->switch_gpio);
#else
	return 0;
#endif
}

static int mt9m001_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	unsigned int width_flag = flags & SOCAM_DATAWIDTH_MASK;
	int ret;

	/* Flags validity verified in test_bus_param */

	if ((mt9m001->datawidth != 10 && (width_flag == SOCAM_DATAWIDTH_10)) ||
	    (mt9m001->datawidth != 9  && (width_flag == SOCAM_DATAWIDTH_9)) ||
	    (mt9m001->datawidth != 8  && (width_flag == SOCAM_DATAWIDTH_8))) {
		/* Well, we actually only can do 10 or 8 bits... */
		if (width_flag == SOCAM_DATAWIDTH_9)
			return -EINVAL;
		ret = bus_switch_act(mt9m001,
				     width_flag == SOCAM_DATAWIDTH_8);
		if (ret < 0)
			return ret;

		mt9m001->datawidth = width_flag == SOCAM_DATAWIDTH_8 ? 8 : 10;
	}

	return 0;
}

static unsigned long mt9m001_query_bus_param(struct soc_camera_device *icd)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	unsigned int width_flag = SOCAM_DATAWIDTH_10;

	if (bus_switch_possible(mt9m001))
		width_flag |= SOCAM_DATAWIDTH_8;

	/* MT9M001 has all capture_format parameters fixed */
	return SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_MASTER |
		width_flag;
}

static int mt9m001_set_fmt_cap(struct soc_camera_device *icd,
		__u32 pixfmt, struct v4l2_rect *rect)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	int ret;
	const u16 hblank = 9, vblank = 25;

	/* Blanking and start values - default... */
	ret = reg_write(icd, MT9M001_HORIZONTAL_BLANKING, hblank);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_VERTICAL_BLANKING, vblank);

	/* The caller provides a supported format, as verified per
	 * call to icd->try_fmt_cap() */
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_COLUMN_START, rect->left);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_ROW_START, rect->top);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_WINDOW_WIDTH, rect->width - 1);
	if (ret >= 0)
		ret = reg_write(icd, MT9M001_WINDOW_HEIGHT,
				rect->height + icd->y_skip_top - 1);
	if (ret >= 0 && mt9m001->autoexposure) {
		ret = reg_write(icd, MT9M001_SHUTTER_WIDTH,
				rect->height + icd->y_skip_top + vblank);
		if (ret >= 0) {
			const struct v4l2_queryctrl *qctrl =
				soc_camera_find_qctrl(icd->ops,
						      V4L2_CID_EXPOSURE);
			icd->exposure = (524 + (rect->height + icd->y_skip_top +
						vblank - 1) *
					 (qctrl->maximum - qctrl->minimum)) /
				1048 + qctrl->minimum;
		}
	}

	return ret < 0 ? ret : 0;
}

static int mt9m001_try_fmt_cap(struct soc_camera_device *icd,
			       struct v4l2_format *f)
{
	if (f->fmt.pix.height < 32 + icd->y_skip_top)
		f->fmt.pix.height = 32 + icd->y_skip_top;
	if (f->fmt.pix.height > 1024 + icd->y_skip_top)
		f->fmt.pix.height = 1024 + icd->y_skip_top;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > 1280)
		f->fmt.pix.width = 1280;
	f->fmt.pix.width &= ~0x01; /* has to be even, unsure why was ~3 */

	return 0;
}

static int mt9m001_get_chip_id(struct soc_camera_device *icd,
			       struct v4l2_chip_ident *id)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);

	if (id->match_type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match_chip != mt9m001->client->addr)
		return -ENODEV;

	id->ident	= mt9m001->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m001_get_register(struct soc_camera_device *icd,
				struct v4l2_register *reg)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);

	if (reg->match_type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match_chip != mt9m001->client->addr)
		return -ENODEV;

	reg->val = reg_read(icd, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9m001_set_register(struct soc_camera_device *icd,
				struct v4l2_register *reg)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);

	if (reg->match_type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match_chip != mt9m001->client->addr)
		return -ENODEV;

	if (reg_write(icd, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

const struct v4l2_queryctrl mt9m001_controls[] = {
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

static int mt9m001_video_probe(struct soc_camera_device *);
static void mt9m001_video_remove(struct soc_camera_device *);
static int mt9m001_get_control(struct soc_camera_device *, struct v4l2_control *);
static int mt9m001_set_control(struct soc_camera_device *, struct v4l2_control *);

static struct soc_camera_ops mt9m001_ops = {
	.owner			= THIS_MODULE,
	.probe			= mt9m001_video_probe,
	.remove			= mt9m001_video_remove,
	.init			= mt9m001_init,
	.release		= mt9m001_release,
	.start_capture		= mt9m001_start_capture,
	.stop_capture		= mt9m001_stop_capture,
	.set_fmt_cap		= mt9m001_set_fmt_cap,
	.try_fmt_cap		= mt9m001_try_fmt_cap,
	.set_bus_param		= mt9m001_set_bus_param,
	.query_bus_param	= mt9m001_query_bus_param,
	.controls		= mt9m001_controls,
	.num_controls		= ARRAY_SIZE(mt9m001_controls),
	.get_control		= mt9m001_get_control,
	.set_control		= mt9m001_set_control,
	.get_chip_id		= mt9m001_get_chip_id,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.get_register		= mt9m001_get_register,
	.set_register		= mt9m001_set_register,
#endif
};

static int mt9m001_get_control(struct soc_camera_device *icd, struct v4l2_control *ctrl)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		data = reg_read(icd, MT9M001_READ_OPTIONS2);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x8000);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = mt9m001->autoexposure;
		break;
	}
	return 0;
}

static int mt9m001_set_control(struct soc_camera_device *icd, struct v4l2_control *ctrl)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	const struct v4l2_queryctrl *qctrl;
	int data;

	qctrl = soc_camera_find_qctrl(&mt9m001_ops, ctrl->id);

	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			data = reg_set(icd, MT9M001_READ_OPTIONS2, 0x8000);
		else
			data = reg_clear(icd, MT9M001_READ_OPTIONS2, 0x8000);
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
			data = reg_write(icd, MT9M001_GLOBAL_GAIN, data);
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

			dev_dbg(&icd->dev, "Setting gain from %d to %d\n",
				 reg_read(icd, MT9M001_GLOBAL_GAIN), data);
			data = reg_write(icd, MT9M001_GLOBAL_GAIN, data);
			if (data < 0)
				return -EIO;
		}

		/* Success */
		icd->gain = ctrl->value;
		break;
	case V4L2_CID_EXPOSURE:
		/* mt9m001 has maximum == default */
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		else {
			unsigned long range = qctrl->maximum - qctrl->minimum;
			unsigned long shutter = ((ctrl->value - qctrl->minimum) * 1048 +
						 range / 2) / range + 1;

			dev_dbg(&icd->dev, "Setting shutter width from %d to %lu\n",
				 reg_read(icd, MT9M001_SHUTTER_WIDTH), shutter);
			if (reg_write(icd, MT9M001_SHUTTER_WIDTH, shutter) < 0)
				return -EIO;
			icd->exposure = ctrl->value;
			mt9m001->autoexposure = 0;
		}
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->value) {
			const u16 vblank = 25;
			if (reg_write(icd, MT9M001_SHUTTER_WIDTH, icd->height +
				      icd->y_skip_top + vblank) < 0)
				return -EIO;
			qctrl = soc_camera_find_qctrl(icd->ops, V4L2_CID_EXPOSURE);
			icd->exposure = (524 + (icd->height + icd->y_skip_top + vblank - 1) *
					 (qctrl->maximum - qctrl->minimum)) /
				1048 + qctrl->minimum;
			mt9m001->autoexposure = 1;
		} else
			mt9m001->autoexposure = 0;
		break;
	}
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int mt9m001_video_probe(struct soc_camera_device *icd)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);
	s32 data;
	int ret;

	/* We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant. */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	/* Enable the chip */
	data = reg_write(&mt9m001->icd, MT9M001_CHIP_ENABLE, 1);
	dev_dbg(&icd->dev, "write: %d\n", data);

	/* Read out the chip version register */
	data = reg_read(icd, MT9M001_CHIP_VERSION);

	/* must be 0x8411 or 0x8421 for colour sensor and 8431 for bw */
	switch (data) {
	case 0x8411:
	case 0x8421:
		mt9m001->model = V4L2_IDENT_MT9M001C12ST;
		icd->formats = mt9m001_colour_formats;
		if (mt9m001->client->dev.platform_data)
			icd->num_formats = ARRAY_SIZE(mt9m001_colour_formats);
		else
			icd->num_formats = 1;
		break;
	case 0x8431:
		mt9m001->model = V4L2_IDENT_MT9M001C12STM;
		icd->formats = mt9m001_monochrome_formats;
		if (mt9m001->client->dev.platform_data)
			icd->num_formats = ARRAY_SIZE(mt9m001_monochrome_formats);
		else
			icd->num_formats = 1;
		break;
	default:
		ret = -ENODEV;
		dev_err(&icd->dev,
			"No MT9M001 chip detected, register read %x\n", data);
		goto ei2c;
	}

	dev_info(&icd->dev, "Detected a MT9M001 chip ID %x (%s)\n", data,
		 data == 0x8431 ? "C12STM" : "C12ST");

	/* Now that we know the model, we can start video */
	ret = soc_camera_video_start(icd);
	if (ret)
		goto eisis;

	return 0;

eisis:
ei2c:
	return ret;
}

static void mt9m001_video_remove(struct soc_camera_device *icd)
{
	struct mt9m001 *mt9m001 = container_of(icd, struct mt9m001, icd);

	dev_dbg(&icd->dev, "Video %x removed: %p, %p\n", mt9m001->client->addr,
		mt9m001->icd.dev.parent, mt9m001->icd.vdev);
	soc_camera_video_stop(&mt9m001->icd);
}

static int mt9m001_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9m001 *mt9m001;
	struct soc_camera_device *icd;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret;

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

	mt9m001->client = client;
	i2c_set_clientdata(client, mt9m001);

	/* Second stage probe - when a capture adapter is there */
	icd = &mt9m001->icd;
	icd->ops	= &mt9m001_ops;
	icd->control	= &client->dev;
	icd->x_min	= 20;
	icd->y_min	= 12;
	icd->x_current	= 20;
	icd->y_current	= 12;
	icd->width_min	= 48;
	icd->width_max	= 1280;
	icd->height_min	= 32;
	icd->height_max	= 1024;
	icd->y_skip_top	= 1;
	icd->iface	= icl->bus_id;
	/* Default datawidth - this is the only width this camera (normally)
	 * supports. It is only with extra logic that it can support
	 * other widths. Therefore it seems to be a sensible default. */
	mt9m001->datawidth = 10;
	/* Simulated autoexposure. If enabled, we calculate shutter width
	 * ourselves in the driver based on vertical blanking and frame width */
	mt9m001->autoexposure = 1;

	ret = bus_switch_request(mt9m001, icl);
	if (ret)
		goto eswinit;

	ret = soc_camera_device_register(icd);
	if (ret)
		goto eisdr;

	return 0;

eisdr:
	bus_switch_release(mt9m001);
eswinit:
	kfree(mt9m001);
	return ret;
}

static int mt9m001_remove(struct i2c_client *client)
{
	struct mt9m001 *mt9m001 = i2c_get_clientdata(client);

	soc_camera_device_unregister(&mt9m001->icd);
	bus_switch_release(mt9m001);
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
