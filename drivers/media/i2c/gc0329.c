// SPDX-License-Identifier: GPL-2.0
/*
 * gc0329 sensor driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 add enum_frame_interval function.
 * V0.0X01.0X02 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x2)
#define DRIVER_NAME "gc0329"
#define GC0329_PIXEL_RATE		(24 * 1000 * 1000)

/*
 * GC0329 register definitions
 */

#define REG_SC_CHIP_ID			0x00
#define GC0329_ID			0xc0
#define REG_NULL			0xFFFF	/* Array end token */

struct sensor_register {
	u16 addr;
	u8 value;
};

struct gc0329_framesize {
	u16 width;
	u16 height;
	struct v4l2_fract max_fps;
	const struct sensor_register *regs;
};

struct gc0329_pll_ctrl {
	u8 ctrl1;
	u8 ctrl2;
	u8 ctrl3;
};

struct gc0329_pixfmt {
	u32 code;
	/* Output format Register Value (REG_FORMAT_CTRL00) */
	struct sensor_register *format_ctrl_regs;
};

struct pll_ctrl_reg {
	unsigned int div;
	unsigned char reg;
};

static const char * const gc0329_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

#define GC0329_NUM_SUPPLIES ARRAY_SIZE(gc0329_supply_names)

struct gc0329 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int fps;
	unsigned int xvclk_frequency;
	struct clk *xvclk;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC0329_NUM_SUPPLIES];
	struct mutex lock; /* Protects streaming, format, interval */
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_frequency;
	const struct gc0329_framesize *frame_size;
	int streaming;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct sensor_register gc0329_vga_regs[] = {
	{0xfe, 0x80},
	{0xfc, 0x16},
	{0xfc, 0x16},
	{0xfe, 0x00},

	{0x73, 0x90},
	{0x74, 0x80},
	{0x75, 0x80},
	{0x76, 0x94},
	/* analog */
	{0xfc, 0x16},
	{0x0a, 0x00},
	{0x0c, 0x00},
	{0x17, 0x14},
	{0x19, 0x05},
	{0x1b, 0x24},
	{0x1c, 0x04},
	{0x1e, 0x00},
	{0x1f, 0xc0},
	{0x20, 0x00},
	{0x21, 0x48},
	{0x23, 0x22},
	{0x24, 0x16},
	/*   blk  */
	{0x26, 0xf7},
	{0x32, 0x04},
	{0x33, 0x20},
	{0x34, 0x20},
	{0x35, 0x20},
	{0x36, 0x20},
	/*   ISP  */
	{0x40, 0xff},
	{0x41, 0x00},
	{0x42, 0xfe},
	{0x46, 0x03},
	{0x4b, 0xcb},
	{0x4d, 0x01},
	{0x4f, 0x01},
	{0x70, 0x48},
	/*  DNDD  */
	{0x80, 0xe7},
	{0x82, 0x55},
	{0x87, 0x4a},
	/*  ASDE  */
	{0xfe, 0x01},
	{0x18, 0x22},
	{0xfe, 0x00},
	{0x9c, 0x0a},
	{0xa4, 0x50},
	{0xa5, 0x21},
	{0xa7, 0x35},
	{0xdd, 0x54},
	{0x95, 0x35},
	/*  gamma */
	{0xfe, 0x00},
	{0xbf, 0x06},
	{0xc0, 0x14},
	{0xc1, 0x27},
	{0xc2, 0x3b},
	{0xc3, 0x4f},
	{0xc4, 0x62},
	{0xc5, 0x72},
	{0xc6, 0x8d},
	{0xc7, 0xa4},
	{0xc8, 0xb8},
	{0xc9, 0xc9},
	{0xca, 0xd6},
	{0xcb, 0xe0},
	{0xcc, 0xe8},
	{0xcd, 0xf4},
	{0xce, 0xfc},
	{0xcf, 0xff},
	/*   CC   */
	{0xfe, 0x00},
	{0xb3, 0x44},
	{0xb4, 0xfd},
	{0xb5, 0x02},
	{0xb6, 0xfa},
	{0xb7, 0x48},
	{0xb8, 0xf0},
	/*  crop  */
	{0x50, 0x01},
	{0x19, 0x05},
	{0x20, 0x01},
	{0x22, 0xba},
	{0x21, 0x48},
	/*   YCP  */
	{0xfe, 0x00},
	{0xd1, 0x34},
	{0xd2, 0x34},
	/*   AEC  */
	{0xfe, 0x01},
	{0x10, 0x40},
	{0x11, 0x21},
	{0x12, 0x07},
	{0x13, 0x50},
	{0x17, 0x88},
	{0x21, 0xb0},
	{0x22, 0x48},
	{0x3c, 0x95},
	{0x3d, 0x50},
	{0x3e, 0x48},
	/*   AWB  */
	{0xfe, 0x01},
	{0x06, 0x08},
	{0x07, 0x06},
	{0x08, 0xa6},
	{0x09, 0xee},
	{0x50, 0xfc},
	{0x51, 0x28},
	{0x52, 0x10},
	{0x53, 0x08},
	{0x54, 0x12},
	{0x55, 0x10},
	{0x56, 0x10},
	{0x58, 0x80},
	{0x59, 0x08},
	{0x5a, 0x02},
	{0x5b, 0x63},
	{0x5c, 0x34},
	{0x5d, 0x73},
	{0x5e, 0x29},
	{0x5f, 0x40},
	{0x60, 0x40},
	{0x61, 0xc8},
	{0x62, 0xa0},
	{0x63, 0x40},
	{0x64, 0x38},
	{0x65, 0x98},
	{0x66, 0xfa},
	{0x67, 0x80},
	{0x68, 0x60},
	{0x69, 0x90},
	{0x6a, 0x40},
	{0x6b, 0x39},
	{0x6c, 0x28},
	{0x6d, 0x28},
	{0x6e, 0x41},
	{0x70, 0x10},
	{0x71, 0x00},
	{0x72, 0x08},
	{0x73, 0x40},
	{0x80, 0x70},
	{0x81, 0x58},
	{0x82, 0x42},
	{0x83, 0x40},
	{0x84, 0x40},
	{0x85, 0x40},
	/* CC-AWB */
	{0xd0, 0x00},
	{0xd2, 0x2c},
	{0xd3, 0x80},
	/*   ABS  */
	{0x9c, 0x02},
	{0x9d, 0x10},
	/*   LSC  */
	{0xfe, 0x01},
	{0xa0, 0x00},
	{0xa1, 0x3c},
	{0xa2, 0x50},
	{0xa3, 0x00},
	{0xa8, 0x0f},
	{0xa9, 0x08},
	{0xaa, 0x00},
	{0xab, 0x04},
	{0xac, 0x00},
	{0xad, 0x07},
	{0xae, 0x0e},
	{0xaf, 0x00},
	{0xb0, 0x00},
	{0xb1, 0x09},
	{0xb2, 0x00},
	{0xb3, 0x00},
	{0xb4, 0x31},
	{0xb5, 0x19},
	{0xb6, 0x24},
	{0xba, 0x3a},
	{0xbb, 0x24},
	{0xbc, 0x2a},
	{0xc0, 0x17},
	{0xc1, 0x13},
	{0xc2, 0x17},
	{0xc6, 0x21},
	{0xc7, 0x1c},
	{0xc8, 0x1c},
	{0xb7, 0x00},
	{0xb8, 0x00},
	{0xb9, 0x00},
	{0xbd, 0x00},
	{0xbe, 0x00},
	{0xbf, 0x00},
	{0xc3, 0x00},
	{0xc4, 0x00},
	{0xc5, 0x00},
	{0xc9, 0x00},
	{0xca, 0x00},
	{0xcb, 0x00},
	{0xa4, 0x00},
	{0xa5, 0x00},
	{0xa6, 0x00},
	{0xa7, 0x00},
	/*  asde  */
	{0xfe, 0x00},
	{0xa0, 0xaf},
	{0xa2, 0xff},
	{0x44, 0xa2},
	{REG_NULL, 0x00},
};

static const struct sensor_register gc0329_vga_regs_14fps[] = {
	/* flicker 14.2fps */
	{0xfe, 0x00},
	{0x05, 0x02},
	{0x06, 0x2c},
	{0x07, 0x00},
	{0x08, 0xb8},
	{0xfe, 0x01},
	{0x29, 0x00},
	{0x2a, 0x60},
	{0x2b, 0x02},
	{0x2c, 0xa0},
	{0x2d, 0x02},
	{0x2e, 0xa0},
	{0x2f, 0x02},
	{0x30, 0xa0},
	{0x31, 0x02},
	{0x32, 0xa0},
	{0x33, 0x20},
	{REG_NULL, 0x00},
};

static const struct sensor_register gc0329_vga_regs_30fps[] = {
	/* flicker 30fps */
	{0xfe, 0x00},
	{0x05, 0x00},
	{0x06, 0x56},
	{0x07, 0x00},
	{0x08, 0x10},
	{0xfe, 0x01},
	{0x29, 0x00},
	{0x2a, 0xa0},
	{0x2b, 0x01},
	{0x2c, 0xe0},
	{0x2d, 0x01},
	{0x2e, 0xe0},
	{0x2f, 0x01},
	{0x30, 0xe0},
	{0x31, 0x01},
	{0x32, 0xe0},
	{0x33, 0x20},
	{REG_NULL, 0x00},
};

static const struct gc0329_framesize gc0329_framesizes[] = {
	{
		.width		= 640,
		.height		= 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 140000,
		},
		.regs		= gc0329_vga_regs_14fps,
	},
	{
		.width		= 640,
		.height		= 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.regs		= gc0329_vga_regs_30fps,
	}
};

static const struct gc0329_pixfmt gc0329_formats[] = {
	{
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
	}
};

static inline struct gc0329 *to_gc0329(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc0329, sd);
}

/* sensor register write */
static int gc0329_write(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc0329 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc0329_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc0329 read reg:0x%x failed!\n", reg);

	return ret;
}

static int gc0329_write_array(struct i2c_client *client,
			      const struct sensor_register *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = gc0329_write(client, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

static void gc0329_get_default_format(struct v4l2_mbus_framefmt *format)
{
	format->width = gc0329_framesizes[0].width;
	format->height = gc0329_framesizes[0].height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = gc0329_formats[0].code;
	format->field = V4L2_FIELD_NONE;
}

static void gc0329_set_streaming(struct gc0329 *gc0329, int on)
{
	struct i2c_client *client = gc0329->client;
	int ret;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	ret = gc0329_write(client, 0xfe, 0x00);
	if (!on) {
		ret |= gc0329_write(client, 0xfc, 0x17);
		ret |= gc0329_write(client, 0xf0, 0x00);
		ret |= gc0329_write(client, 0xf1, 0x00);
	} else {
		ret |= gc0329_write(client, 0xfc, 0x16);
		ret |= gc0329_write(client, 0xf0, 0x07);
		ret |= gc0329_write(client, 0xf1, 0x01);
	}
	if (ret)
		dev_err(&client->dev, "gc0329 soft standby failed\n");
}

/*
 * V4L2 subdev video and pad level operations
 */

static int gc0329_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (code->index >= ARRAY_SIZE(gc0329_formats))
		return -EINVAL;

	code->code = gc0329_formats[code->index].code;

	return 0;
}

static int gc0329_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(gc0329_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(gc0329_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == gc0329_formats[i].code)
			break;

	fse->code = gc0329_formats[i].code;

	fse->min_width  = gc0329_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = gc0329_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int gc0329_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0329 *gc0329 = to_gc0329(sd);

	dev_dbg(&client->dev, "%s enter\n", __func__);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&gc0329->lock);
		fmt->format = *mf;
		mutex_unlock(&gc0329->lock);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&gc0329->lock);
	fmt->format = gc0329->format;
	mutex_unlock(&gc0329->lock);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		gc0329->format.code, gc0329->format.width,
		gc0329->format.height);

	return 0;
}

static void __gc0329_try_frame_size_fps(struct v4l2_mbus_framefmt *mf,
				    const struct gc0329_framesize **size,
				    unsigned int fps)
{
	const struct gc0329_framesize *fsize = &gc0329_framesizes[0];
	const struct gc0329_framesize *match = NULL;
	unsigned int i = ARRAY_SIZE(gc0329_framesizes);
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err && fsize->regs[0].addr) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match) {
		match = &gc0329_framesizes[0];
	} else {
		fsize = &gc0329_framesizes[0];
		for (i = 0; i < ARRAY_SIZE(gc0329_framesizes); i++) {
			if (fsize->width == match->width &&
				fsize->height == match->height &&
				fps >= DIV_ROUND_CLOSEST(fsize->max_fps.denominator,
				fsize->max_fps.numerator))
				match = fsize;

			fsize++;
		}
	}

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int gc0329_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int index = ARRAY_SIZE(gc0329_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct gc0329_framesize *size = NULL;
	struct gc0329 *gc0329 = to_gc0329(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s enter\n", __func__);

	__gc0329_try_frame_size_fps(mf, &size, gc0329->fps);

	while (--index >= 0)
		if (gc0329_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = gc0329_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&gc0329->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (gc0329->streaming) {
			mutex_unlock(&gc0329->lock);
			return -EBUSY;
		}

		gc0329->frame_size = size;
		gc0329->format = fmt->format;
	}

	mutex_unlock(&gc0329->lock);
	return ret;
}

static void gc0329_get_module_inf(struct gc0329 *gc0329,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, DRIVER_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc0329->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc0329->len_name, sizeof(inf->base.lens));
}

static long gc0329_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc0329 *gc0329 = to_gc0329(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc0329_get_module_inf(gc0329, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		gc0329_set_streaming(gc0329, !!stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc0329_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc0329_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc0329_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc0329_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int gc0329_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0329 *gc0329 = to_gc0329(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	mutex_lock(&gc0329->lock);

	on = !!on;

	if (gc0329->streaming == on)
		goto unlock;

	if (!on) {
		/* Stop Streaming Sequence */
		gc0329_set_streaming(gc0329, on);
		gc0329->streaming = on;
		if (!IS_ERR(gc0329->pwdn_gpio)) {
			gpiod_set_value_cansleep(gc0329->pwdn_gpio, 1);
			usleep_range(2000, 5000);
		}
		goto unlock;
	}
	if (!IS_ERR(gc0329->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0329->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	ret = gc0329_write_array(client, gc0329_vga_regs);
	if (ret)
		goto unlock;

	ret = gc0329_write_array(client, gc0329->frame_size->regs);
	if (ret)
		goto unlock;

	gc0329_set_streaming(gc0329, on);
	gc0329->streaming = on;

unlock:
	mutex_unlock(&gc0329->lock);
	return ret;
}

static int gc0329_set_test_pattern(struct gc0329 *gc0329, int value)
{
	return 0;
}

static int gc0329_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0329 *gc0329 =
			container_of(ctrl->handler, struct gc0329, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return gc0329_set_test_pattern(gc0329, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops gc0329_ctrl_ops = {
	.s_ctrl = gc0329_s_ctrl,
};

static const char * const gc0329_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc0329_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);

	dev_dbg(&client->dev, "%s:\n", __func__);

	gc0329_get_default_format(format);

	return 0;
}
#endif

static int gc0329_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int gc0329_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc0329 *gc0329 = to_gc0329(sd);

	mutex_lock(&gc0329->lock);
	fi->interval = gc0329->frame_size->max_fps;
	mutex_unlock(&gc0329->lock);

	return 0;
}

static int gc0329_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0329 *gc0329 = to_gc0329(sd);
	const struct gc0329_framesize *size = NULL;
	struct v4l2_mbus_framefmt mf;
	unsigned int fps;
	int ret = 0;

	dev_dbg(&client->dev, "Setting %d/%d frame interval\n",
		fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&gc0329->lock);
	fps = DIV_ROUND_CLOSEST(fi->interval.denominator,
		fi->interval.numerator);
	mf = gc0329->format;
	__gc0329_try_frame_size_fps(&mf, &size, fps);
	if (gc0329->frame_size != size) {
		ret = gc0329_write_array(client, size->regs);
		if (ret)
			goto unlock;
		gc0329->frame_size = size;
		gc0329->fps = fps;
	}
unlock:
	mutex_unlock(&gc0329->lock);

	return ret;
}

static int gc0329_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(gc0329_framesizes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_YUYV8_2X8)
		return -EINVAL;

	fie->width = gc0329_framesizes[fie->index].width;
	fie->height = gc0329_framesizes[fie->index].height;
	fie->interval = gc0329_framesizes[fie->index].max_fps;
	return 0;
}

static const struct v4l2_subdev_core_ops gc0329_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = gc0329_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc0329_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc0329_subdev_video_ops = {
	.s_stream = gc0329_s_stream,
	.g_mbus_config = gc0329_g_mbus_config,
	.g_frame_interval = gc0329_g_frame_interval,
	.s_frame_interval = gc0329_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc0329_subdev_pad_ops = {
	.enum_mbus_code = gc0329_enum_mbus_code,
	.enum_frame_size = gc0329_enum_frame_sizes,
	.enum_frame_interval = gc0329_enum_frame_interval,
	.get_fmt = gc0329_get_fmt,
	.set_fmt = gc0329_set_fmt,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_ops gc0329_subdev_ops = {
	.core  = &gc0329_subdev_core_ops,
	.video = &gc0329_subdev_video_ops,
	.pad   = &gc0329_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc0329_subdev_internal_ops = {
	.open = gc0329_open,
};
#endif

static int gc0329_detect(struct gc0329 *gc0329)
{
	struct i2c_client *client = gc0329->client;
	u8 pid = 0;
	int ret;

	dev_dbg(&client->dev, "%s:\n", __func__);

	/* Check sensor revision */
	ret = gc0329_write(client, 0xfc, 0x16);
	msleep(20);
	ret |= gc0329_read(client, REG_SC_CHIP_ID, &pid);
	if (!ret) {
		if (pid != GC0329_ID) {
			ret = -1;
			dev_err(&client->dev,
				"Sensor detection failed (%X, %d)\n",
				pid, ret);
		} else {
			dev_info(&client->dev,
				"Found GC0329 id:%X sensor\n", pid);
			if (!IS_ERR(gc0329->pwdn_gpio))
				gpiod_set_value_cansleep(gc0329->pwdn_gpio, 1);
		}
	}

	return ret;
}

static int __gc0329_power_on(struct gc0329 *gc0329)
{
	int ret;
	struct device *dev = &gc0329->client->dev;

	if (!IS_ERR(gc0329->xvclk)) {
		ret = clk_set_rate(gc0329->xvclk, 24000000);
		if (ret < 0)
			dev_info(dev, "Failed to set xvclk rate (24MHz)\n");
	}

	if (!IS_ERR(gc0329->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0329->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0329->supplies)) {
		ret = regulator_bulk_enable(GC0329_NUM_SUPPLIES,
			gc0329->supplies);
		if (ret < 0)
			dev_info(dev, "Failed to enable regulators\n");

		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0329->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0329->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0329->xvclk)) {
		ret = clk_prepare_enable(gc0329->xvclk);
		if (ret < 0)
			dev_info(dev, "Failed to enable xvclk\n");
	}

	usleep_range(7000, 10000);

	return 0;
}

static void __gc0329_power_off(struct gc0329 *gc0329)
{
	if (!IS_ERR(gc0329->xvclk))
		clk_disable_unprepare(gc0329->xvclk);
	if (!IS_ERR(gc0329->supplies))
		regulator_bulk_disable(GC0329_NUM_SUPPLIES, gc0329->supplies);
	if (!IS_ERR(gc0329->pwdn_gpio))
		gpiod_set_value_cansleep(gc0329->pwdn_gpio, 1);
}

static int gc0329_configure_regulators(struct gc0329 *gc0329)
{
	unsigned int i;

	for (i = 0; i < GC0329_NUM_SUPPLIES; i++)
		gc0329->supplies[i].supply = gc0329_supply_names[i];

	return devm_regulator_bulk_get(&gc0329->client->dev,
				       GC0329_NUM_SUPPLIES,
				       gc0329->supplies);
}

static int gc0329_parse_of(struct gc0329 *gc0329)
{
	struct device *dev = &gc0329->client->dev;
	int ret;

	gc0329->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc0329->pwdn_gpio))
		dev_info(dev, "Failed to get pwdn-gpios, maybe no used\n");

	ret = gc0329_configure_regulators(gc0329);
	if (ret)
		dev_info(dev, "Failed to get power regulators\n");

	return __gc0329_power_on(gc0329);
}

static int gc0329_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	struct gc0329 *gc0329;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc0329 = devm_kzalloc(&client->dev, sizeof(*gc0329), GFP_KERNEL);
	if (!gc0329)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc0329->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc0329->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc0329->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc0329->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc0329->client = client;
	gc0329->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(gc0329->xvclk)) {
		dev_err(&client->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc0329_parse_of(gc0329);

	gc0329->xvclk_frequency = clk_get_rate(gc0329->xvclk);
	if (gc0329->xvclk_frequency < 6000000 ||
	    gc0329->xvclk_frequency > 27000000)
		return -EINVAL;

	v4l2_ctrl_handler_init(&gc0329->ctrls, 2);
	gc0329->link_frequency =
			v4l2_ctrl_new_std(&gc0329->ctrls, &gc0329_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  GC0329_PIXEL_RATE, 1,
					  GC0329_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&gc0329->ctrls, &gc0329_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc0329_test_pattern_menu) - 1,
				     0, 0, gc0329_test_pattern_menu);
	gc0329->sd.ctrl_handler = &gc0329->ctrls;

	if (gc0329->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, gc0329->ctrls.error);
		return  gc0329->ctrls.error;
	}

	sd = &gc0329->sd;
	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &gc0329_subdev_ops);

	sd->internal_ops = &gc0329_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	gc0329->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc0329->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&gc0329->ctrls);
		return ret;
	}
#endif

	mutex_init(&gc0329->lock);

	gc0329_get_default_format(&gc0329->format);
	gc0329->frame_size = &gc0329_framesizes[0];
	gc0329->format.width = gc0329_framesizes[0].width;
	gc0329->format.height = gc0329_framesizes[0].height;
	gc0329->fps = DIV_ROUND_CLOSEST(gc0329_framesizes[0].max_fps.denominator,
				gc0329_framesizes[0].max_fps.numerator);

	ret = gc0329_detect(gc0329);
	if (ret < 0)
		goto error;

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc0329->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc0329->module_index, facing,
		 DRIVER_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret)
		goto error;

	dev_info(&client->dev, "%s sensor driver registered !!\n", sd->name);

	return 0;

error:
	v4l2_ctrl_handler_free(&gc0329->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc0329->lock);
	__gc0329_power_off(gc0329);
	return ret;
}

static int gc0329_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0329 *gc0329 = to_gc0329(sd);

	v4l2_ctrl_handler_free(&gc0329->ctrls);
	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc0329->lock);

	__gc0329_power_off(gc0329);

	return 0;
}

static const struct i2c_device_id gc0329_id[] = {
	{ "gc0329", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, gc0329_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc0329_of_match[] = {
	{ .compatible = "galaxycore,gc0329", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, gc0329_of_match);
#endif

static struct i2c_driver gc0329_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(gc0329_of_match),
	},
	.probe		= gc0329_probe,
	.remove		= gc0329_remove,
	.id_table	= gc0329_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc0329_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc0329_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GC0329 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
