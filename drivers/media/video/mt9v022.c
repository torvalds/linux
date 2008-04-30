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

#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

#ifdef CONFIG_MT9M001_PCA9536_SWITCH
#include <asm/gpio.h>
#endif

/* mt9v022 i2c address 0x48, 0x4c, 0x58, 0x5c
 * The platform has to define i2c_board_info
 * and call i2c_register_board_info() */

static char *sensor_type;
module_param(sensor_type, charp, S_IRUGO);
MODULE_PARM_DESC(sensor_type, "Sensor type: \"colour\" or \"monochrome\"\n");

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
#define MT9V022_ANALOG_GAIN		0x34
#define MT9V022_BLACK_LEVEL_CALIB_CTRL	0x47
#define MT9V022_PIXCLK_FV_LV		0x74
#define MT9V022_DIGITAL_TEST_PATTERN	0x7f
#define MT9V022_AEC_AGC_ENABLE		0xAF
#define MT9V022_MAX_TOTAL_SHUTTER_WIDTH	0xBD

/* Progressive scan, master, defaults */
#define MT9V022_CHIP_CONTROL_DEFAULT	0x188

static const struct soc_camera_data_format mt9v022_colour_formats[] = {
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

static const struct soc_camera_data_format mt9v022_monochrome_formats[] = {
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

struct mt9v022 {
	struct i2c_client *client;
	struct soc_camera_device icd;
	int model;	/* V4L2_IDENT_MT9M001* codes from v4l2-chip-ident.h */
	int switch_gpio;
	u16 chip_control;
	unsigned char datawidth;
};

static int reg_read(struct soc_camera_device *icd, const u8 reg)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	struct i2c_client *client = mt9v022->client;
	s32 data = i2c_smbus_read_word_data(client, reg);
	return data < 0 ? data : swab16(data);
}

static int reg_write(struct soc_camera_device *icd, const u8 reg,
		     const u16 data)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	return i2c_smbus_write_word_data(mt9v022->client, reg, swab16(data));
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

static int mt9v022_init(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	int ret;

	/* Almost the default mode: master, parallel, simultaneous, and an
	 * undocumented bit 0x200, which is present in table 7, but not in 8,
	 * plus snapshot mode to disable scan for now */
	mt9v022->chip_control |= 0x10;
	ret = reg_write(icd, MT9V022_CHIP_CONTROL, mt9v022->chip_control);
	if (ret >= 0)
		reg_write(icd, MT9V022_READ_MODE, 0x300);

	/* All defaults */
	if (ret >= 0)
		/* AEC, AGC on */
		ret = reg_set(icd, MT9V022_AEC_AGC_ENABLE, 0x3);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_MAX_TOTAL_SHUTTER_WIDTH, 480);
	if (ret >= 0)
		/* default - auto */
		ret = reg_clear(icd, MT9V022_BLACK_LEVEL_CALIB_CTRL, 1);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_DIGITAL_TEST_PATTERN, 0);

	return ret >= 0 ? 0 : -EIO;
}

static int mt9v022_release(struct soc_camera_device *icd)
{
	/* Nothing? */
	return 0;
}

static int mt9v022_start_capture(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	/* Switch to master "normal" mode */
	mt9v022->chip_control &= ~0x10;
	if (reg_write(icd, MT9V022_CHIP_CONTROL,
		      mt9v022->chip_control) < 0)
		return -EIO;
	return 0;
}

static int mt9v022_stop_capture(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	/* Switch to snapshot mode */
	mt9v022->chip_control |= 0x10;
	if (reg_write(icd, MT9V022_CHIP_CONTROL,
		      mt9v022->chip_control) < 0)
		return -EIO;
	return 0;
}

static int bus_switch_request(struct mt9v022 *mt9v022, struct soc_camera_link *icl)
{
#ifdef CONFIG_MT9V022_PCA9536_SWITCH
	int ret;
	unsigned int gpio = icl->gpio;

	if (gpio_is_valid(gpio)) {
		/* We have a data bus switch. */
		ret = gpio_request(gpio, "mt9v022");
		if (ret < 0) {
			dev_err(&mt9v022->client->dev, "Cannot get GPIO %u\n", gpio);
			return ret;
		}

		ret = gpio_direction_output(gpio, 0);
		if (ret < 0) {
			dev_err(&mt9v022->client->dev,
				"Cannot set GPIO %u to output\n", gpio);
			gpio_free(gpio);
			return ret;
		}
	}

	mt9v022->switch_gpio = gpio;
#else
	mt9v022->switch_gpio = -EINVAL;
#endif
	return 0;
}

static void bus_switch_release(struct mt9v022 *mt9v022)
{
#ifdef CONFIG_MT9V022_PCA9536_SWITCH
	if (gpio_is_valid(mt9v022->switch_gpio))
		gpio_free(mt9v022->switch_gpio);
#endif
}

static int bus_switch_act(struct mt9v022 *mt9v022, int go8bit)
{
#ifdef CONFIG_MT9V022_PCA9536_SWITCH
	if (!gpio_is_valid(mt9v022->switch_gpio))
		return -ENODEV;

	gpio_set_value_cansleep(mt9v022->switch_gpio, go8bit);
	return 0;
#else
	return -ENODEV;
#endif
}

static int bus_switch_possible(struct mt9v022 *mt9v022)
{
#ifdef CONFIG_MT9V022_PCA9536_SWITCH
	return gpio_is_valid(mt9v022->switch_gpio);
#else
	return 0;
#endif
}

static int mt9v022_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	unsigned int width_flag = flags & SOCAM_DATAWIDTH_MASK;
	int ret;
	u16 pixclk = 0;

	/* Only one width bit may be set */
	if (!is_power_of_2(width_flag))
		return -EINVAL;

	if ((mt9v022->datawidth != 10 && (width_flag == SOCAM_DATAWIDTH_10)) ||
	    (mt9v022->datawidth != 9  && (width_flag == SOCAM_DATAWIDTH_9)) ||
	    (mt9v022->datawidth != 8  && (width_flag == SOCAM_DATAWIDTH_8))) {
		/* Well, we actually only can do 10 or 8 bits... */
		if (width_flag == SOCAM_DATAWIDTH_9)
			return -EINVAL;

		ret = bus_switch_act(mt9v022,
				     width_flag == SOCAM_DATAWIDTH_8);
		if (ret < 0)
			return ret;

		mt9v022->datawidth = width_flag == SOCAM_DATAWIDTH_8 ? 8 : 10;
	}

	if (flags & SOCAM_PCLK_SAMPLE_RISING)
		pixclk |= 0x10;

	if (!(flags & SOCAM_HSYNC_ACTIVE_HIGH))
		pixclk |= 0x1;

	if (!(flags & SOCAM_VSYNC_ACTIVE_HIGH))
		pixclk |= 0x2;

	ret = reg_write(icd, MT9V022_PIXCLK_FV_LV, pixclk);
	if (ret < 0)
		return ret;

	if (!(flags & SOCAM_MASTER))
		mt9v022->chip_control &= ~0x8;

	ret = reg_write(icd, MT9V022_CHIP_CONTROL, mt9v022->chip_control);
	if (ret < 0)
		return ret;

	dev_dbg(&icd->dev, "Calculated pixclk 0x%x, chip control 0x%x\n",
		pixclk, mt9v022->chip_control);

	return 0;
}

static unsigned long mt9v022_query_bus_param(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	unsigned int width_flag = SOCAM_DATAWIDTH_10;

	if (bus_switch_possible(mt9v022))
		width_flag |= SOCAM_DATAWIDTH_8;

	return SOCAM_PCLK_SAMPLE_RISING | SOCAM_PCLK_SAMPLE_FALLING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_LOW |
		SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW |
		SOCAM_MASTER | SOCAM_SLAVE |
		width_flag;
}

static int mt9v022_set_fmt_cap(struct soc_camera_device *icd,
		__u32 pixfmt, struct v4l2_rect *rect)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	int ret;

	/* The caller provides a supported format, as verified per call to
	 * icd->try_fmt_cap(), datawidth is from our supported format list */
	switch (pixfmt) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y16:
		if (mt9v022->model != V4L2_IDENT_MT9V022IX7ATM)
			return -EINVAL;
		break;
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SBGGR16:
		if (mt9v022->model != V4L2_IDENT_MT9V022IX7ATC)
			return -EINVAL;
		break;
	case 0:
		/* No format change, only geometry */
		break;
	default:
		return -EINVAL;
	}

	/* Like in example app. Contradicts the datasheet though */
	ret = reg_read(icd, MT9V022_AEC_AGC_ENABLE);
	if (ret >= 0) {
		if (ret & 1) /* Autoexposure */
			ret = reg_write(icd, MT9V022_MAX_TOTAL_SHUTTER_WIDTH,
					rect->height + icd->y_skip_top + 43);
		else
			ret = reg_write(icd, MT9V022_TOTAL_SHUTTER_WIDTH,
					rect->height + icd->y_skip_top + 43);
	}
	/* Setup frame format: defaults apart from width and height */
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_COLUMN_START, rect->left);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_ROW_START, rect->top);
	if (ret >= 0)
		/* Default 94, Phytec driver says:
		 * "width + horizontal blank >= 660" */
		ret = reg_write(icd, MT9V022_HORIZONTAL_BLANKING,
				rect->width > 660 - 43 ? 43 :
				660 - rect->width);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_VERTICAL_BLANKING, 45);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_WINDOW_WIDTH, rect->width);
	if (ret >= 0)
		ret = reg_write(icd, MT9V022_WINDOW_HEIGHT,
				rect->height + icd->y_skip_top);

	if (ret < 0)
		return ret;

	dev_dbg(&icd->dev, "Frame %ux%u pixel\n", rect->width, rect->height);

	return 0;
}

static int mt9v022_try_fmt_cap(struct soc_camera_device *icd,
			       struct v4l2_format *f)
{
	if (f->fmt.pix.height < 32 + icd->y_skip_top)
		f->fmt.pix.height = 32 + icd->y_skip_top;
	if (f->fmt.pix.height > 480 + icd->y_skip_top)
		f->fmt.pix.height = 480 + icd->y_skip_top;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > 752)
		f->fmt.pix.width = 752;
	f->fmt.pix.width &= ~0x03; /* ? */

	return 0;
}

static int mt9v022_get_chip_id(struct soc_camera_device *icd,
			       struct v4l2_chip_ident *id)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);

	if (id->match_type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match_chip != mt9v022->client->addr)
		return -ENODEV;

	id->ident	= mt9v022->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9v022_get_register(struct soc_camera_device *icd,
				struct v4l2_register *reg)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);

	if (reg->match_type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match_chip != mt9v022->client->addr)
		return -ENODEV;

	reg->val = reg_read(icd, reg->reg);

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9v022_set_register(struct soc_camera_device *icd,
				struct v4l2_register *reg)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);

	if (reg->match_type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0xff)
		return -EINVAL;

	if (reg->match_chip != mt9v022->client->addr)
		return -ENODEV;

	if (reg_write(icd, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

const struct v4l2_queryctrl mt9v022_controls[] = {
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
		.name		= "Analog Gain",
		.minimum	= 64,
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
		.id		= V4L2_CID_AUTOGAIN,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Automatic Gain",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
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

static int mt9v022_video_probe(struct soc_camera_device *);
static void mt9v022_video_remove(struct soc_camera_device *);
static int mt9v022_get_control(struct soc_camera_device *, struct v4l2_control *);
static int mt9v022_set_control(struct soc_camera_device *, struct v4l2_control *);

static struct soc_camera_ops mt9v022_ops = {
	.owner			= THIS_MODULE,
	.probe			= mt9v022_video_probe,
	.remove			= mt9v022_video_remove,
	.init			= mt9v022_init,
	.release		= mt9v022_release,
	.start_capture		= mt9v022_start_capture,
	.stop_capture		= mt9v022_stop_capture,
	.set_fmt_cap		= mt9v022_set_fmt_cap,
	.try_fmt_cap		= mt9v022_try_fmt_cap,
	.set_bus_param		= mt9v022_set_bus_param,
	.query_bus_param	= mt9v022_query_bus_param,
	.controls		= mt9v022_controls,
	.num_controls		= ARRAY_SIZE(mt9v022_controls),
	.get_control		= mt9v022_get_control,
	.set_control		= mt9v022_set_control,
	.get_chip_id		= mt9v022_get_chip_id,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.get_register		= mt9v022_get_register,
	.set_register		= mt9v022_set_register,
#endif
};

static int mt9v022_get_control(struct soc_camera_device *icd,
			       struct v4l2_control *ctrl)
{
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		data = reg_read(icd, MT9V022_READ_MODE);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x10);
		break;
	case V4L2_CID_HFLIP:
		data = reg_read(icd, MT9V022_READ_MODE);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x20);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		data = reg_read(icd, MT9V022_AEC_AGC_ENABLE);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x1);
		break;
	case V4L2_CID_AUTOGAIN:
		data = reg_read(icd, MT9V022_AEC_AGC_ENABLE);
		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & 0x2);
		break;
	}
	return 0;
}

static int mt9v022_set_control(struct soc_camera_device *icd,
			       struct v4l2_control *ctrl)
{
	int data;
	const struct v4l2_queryctrl *qctrl;

	qctrl = soc_camera_find_qctrl(&mt9v022_ops, ctrl->id);

	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			data = reg_set(icd, MT9V022_READ_MODE, 0x10);
		else
			data = reg_clear(icd, MT9V022_READ_MODE, 0x10);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->value)
			data = reg_set(icd, MT9V022_READ_MODE, 0x20);
		else
			data = reg_clear(icd, MT9V022_READ_MODE, 0x20);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_GAIN:
		/* mt9v022 has minimum == default */
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		else {
			unsigned long range = qctrl->maximum - qctrl->minimum;
			/* Datasheet says 16 to 64. autogain only works properly
			 * after setting gain to maximum 14. Larger values
			 * produce "white fly" noise effect. On the whole,
			 * manually setting analog gain does no good. */
			unsigned long gain = ((ctrl->value - qctrl->minimum) *
					      10 + range / 2) / range + 4;
			if (gain >= 32)
				gain &= ~1;
			/* The user wants to set gain manually, hope, she
			 * knows, what she's doing... Switch AGC off. */

			if (reg_clear(icd, MT9V022_AEC_AGC_ENABLE, 0x2) < 0)
				return -EIO;

			dev_info(&icd->dev, "Setting gain from %d to %lu\n",
				 reg_read(icd, MT9V022_ANALOG_GAIN), gain);
			if (reg_write(icd, MT9V022_ANALOG_GAIN, gain) < 0)
				return -EIO;
			icd->gain = ctrl->value;
		}
		break;
	case V4L2_CID_EXPOSURE:
		/* mt9v022 has maximum == default */
		if (ctrl->value > qctrl->maximum || ctrl->value < qctrl->minimum)
			return -EINVAL;
		else {
			unsigned long range = qctrl->maximum - qctrl->minimum;
			unsigned long shutter = ((ctrl->value - qctrl->minimum) *
						 479 + range / 2) / range + 1;
			/* The user wants to set shutter width manually, hope,
			 * she knows, what she's doing... Switch AEC off. */

			if (reg_clear(icd, MT9V022_AEC_AGC_ENABLE, 0x1) < 0)
				return -EIO;

			dev_dbg(&icd->dev, "Shutter width from %d to %lu\n",
				reg_read(icd, MT9V022_TOTAL_SHUTTER_WIDTH),
				shutter);
			if (reg_write(icd, MT9V022_TOTAL_SHUTTER_WIDTH,
				      shutter) < 0)
				return -EIO;
			icd->exposure = ctrl->value;
		}
		break;
	case V4L2_CID_AUTOGAIN:
		if (ctrl->value)
			data = reg_set(icd, MT9V022_AEC_AGC_ENABLE, 0x2);
		else
			data = reg_clear(icd, MT9V022_AEC_AGC_ENABLE, 0x2);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->value)
			data = reg_set(icd, MT9V022_AEC_AGC_ENABLE, 0x1);
		else
			data = reg_clear(icd, MT9V022_AEC_AGC_ENABLE, 0x1);
		if (data < 0)
			return -EIO;
		break;
	}
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int mt9v022_video_probe(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);
	s32 data;
	int ret;

	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	/* Read out the chip version register */
	data = reg_read(icd, MT9V022_CHIP_VERSION);

	/* must be 0x1311 or 0x1313 */
	if (data != 0x1311 && data != 0x1313) {
		ret = -ENODEV;
		dev_info(&icd->dev, "No MT9V022 detected, ID register 0x%x\n",
			 data);
		goto ei2c;
	}

	/* Soft reset */
	ret = reg_write(icd, MT9V022_RESET, 1);
	if (ret < 0)
		goto ei2c;
	/* 15 clock cycles */
	udelay(200);
	if (reg_read(icd, MT9V022_RESET)) {
		dev_err(&icd->dev, "Resetting MT9V022 failed!\n");
		goto ei2c;
	}

	/* Set monochrome or colour sensor type */
	if (sensor_type && (!strcmp("colour", sensor_type) ||
			    !strcmp("color", sensor_type))) {
		ret = reg_write(icd, MT9V022_PIXEL_OPERATION_MODE, 4 | 0x11);
		mt9v022->model = V4L2_IDENT_MT9V022IX7ATC;
		icd->formats = mt9v022_colour_formats;
		if (mt9v022->client->dev.platform_data)
			icd->num_formats = ARRAY_SIZE(mt9v022_colour_formats);
		else
			icd->num_formats = 1;
	} else {
		ret = reg_write(icd, MT9V022_PIXEL_OPERATION_MODE, 0x11);
		mt9v022->model = V4L2_IDENT_MT9V022IX7ATM;
		icd->formats = mt9v022_monochrome_formats;
		if (mt9v022->client->dev.platform_data)
			icd->num_formats = ARRAY_SIZE(mt9v022_monochrome_formats);
		else
			icd->num_formats = 1;
	}

	if (ret >= 0)
		ret = soc_camera_video_start(icd);
	if (ret < 0)
		goto eisis;

	dev_info(&icd->dev, "Detected a MT9V022 chip ID %x, %s sensor\n",
		 data, mt9v022->model == V4L2_IDENT_MT9V022IX7ATM ?
		 "monochrome" : "colour");

	return 0;

eisis:
ei2c:
	return ret;
}

static void mt9v022_video_remove(struct soc_camera_device *icd)
{
	struct mt9v022 *mt9v022 = container_of(icd, struct mt9v022, icd);

	dev_dbg(&icd->dev, "Video %x removed: %p, %p\n", mt9v022->client->addr,
		mt9v022->icd.dev.parent, mt9v022->icd.vdev);
	soc_camera_video_stop(&mt9v022->icd);
}

static int mt9v022_probe(struct i2c_client *client)
{
	struct mt9v022 *mt9v022;
	struct soc_camera_device *icd;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret;

	if (!icl) {
		dev_err(&client->dev, "MT9V022 driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9v022 = kzalloc(sizeof(struct mt9v022), GFP_KERNEL);
	if (!mt9v022)
		return -ENOMEM;

	mt9v022->chip_control = MT9V022_CHIP_CONTROL_DEFAULT;
	mt9v022->client = client;
	i2c_set_clientdata(client, mt9v022);

	icd = &mt9v022->icd;
	icd->ops	= &mt9v022_ops;
	icd->control	= &client->dev;
	icd->x_min	= 1;
	icd->y_min	= 4;
	icd->x_current	= 1;
	icd->y_current	= 4;
	icd->width_min	= 48;
	icd->width_max	= 752;
	icd->height_min	= 32;
	icd->height_max	= 480;
	icd->y_skip_top	= 1;
	icd->iface	= icl->bus_id;
	/* Default datawidth - this is the only width this camera (normally)
	 * supports. It is only with extra logic that it can support
	 * other widths. Therefore it seems to be a sensible default. */
	mt9v022->datawidth = 10;

	ret = bus_switch_request(mt9v022, icl);
	if (ret)
		goto eswinit;

	ret = soc_camera_device_register(icd);
	if (ret)
		goto eisdr;

	return 0;

eisdr:
	bus_switch_release(mt9v022);
eswinit:
	kfree(mt9v022);
	return ret;
}

static int mt9v022_remove(struct i2c_client *client)
{
	struct mt9v022 *mt9v022 = i2c_get_clientdata(client);

	soc_camera_device_unregister(&mt9v022->icd);
	bus_switch_release(mt9v022);
	kfree(mt9v022);

	return 0;
}

static struct i2c_driver mt9v022_i2c_driver = {
	.driver = {
		.name = "mt9v022",
	},
	.probe		= mt9v022_probe,
	.remove		= mt9v022_remove,
};

static int __init mt9v022_mod_init(void)
{
	return i2c_add_driver(&mt9v022_i2c_driver);
}

static void __exit mt9v022_mod_exit(void)
{
	i2c_del_driver(&mt9v022_i2c_driver);
}

module_init(mt9v022_mod_init);
module_exit(mt9v022_mod_exit);

MODULE_DESCRIPTION("Micron MT9V022 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
