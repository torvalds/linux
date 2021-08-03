// SPDX-License-Identifier: GPL-2.0
/*
 * GC032A CMOS Image Sensor driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 init driver.
 * V0.0X01.0X02 add quick stream on/off
 * V0.0X01.0X03 set sensor in stream off state by default
 * to avoid sending abnormal data in the early stage.
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
#define DRIVER_NAME "gc032a"
#define GC032A_PIXEL_RATE		(96 * 1000 * 1000)
//#define GC032A_AUTO_FPS

/*
 * GC032A register definitions
 */
#define REG_SOFTWARE_STANDBY		0xf3

#define REG_SC_CHIP_ID_H		0xf0
#define REG_SC_CHIP_ID_L		0xf1

#define REG_NULL			0xFFFF	/* Array end token */

#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))
#define GC032A_ID			0x232a

struct sensor_register {
	u16 addr;
	u8 value;
};

struct gc032a_framesize {
	u16 width;
	u16 height;
	u16 max_exp_lines;
	struct v4l2_fract max_fps;
	const struct sensor_register *regs;
};

struct gc032a_pll_ctrl {
	u8 ctrl1;
	u8 ctrl2;
	u8 ctrl3;
};

struct gc032a_pixfmt {
	u32 code;
	/* Output format Register Value (REG_FORMAT_CTRL00) */
	struct sensor_register *format_ctrl_regs;
};

struct pll_ctrl_reg {
	unsigned int div;
	unsigned char reg;
};

static const char * const gc032a_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

#define GC032A_NUM_SUPPLIES ARRAY_SIZE(gc032a_supply_names)

struct gc032a {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int xvclk_frequency;
	struct clk *xvclk;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC032A_NUM_SUPPLIES];
	struct mutex lock;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_frequency;
	const struct gc032a_framesize *frame_size;
	int streaming;
	bool power_on;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

static const struct sensor_register gc032a_vga_regs[] = {
	/*System*/
	{0xf3, 0x00},
	{0xf5, 0x06},
	{0xf7, 0x01},
	{0xf8, 0x03},
	{0xf9, 0xce},
	{0xfa, 0x00},
	{0xfc, 0x02},
	{0xfe, 0x02},
	{0x81, 0x03},

	{0xfe, 0x00},
	{0x77, 0x64},
	{0x78, 0x40},
	{0x79, 0x60},

	/*ANALOG & CISCTL*/
	{0xfe, 0x00},
	{0x03, 0x01},
	{0x04, 0xce},
	{0x05, 0x01},
	{0x06, 0xad},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x0a, 0x00},
	{0x0c, 0x00},
	{0x0d, 0x01},
	{0x0e, 0xe8},
	{0x0f, 0x02},
	{0x10, 0x88},
	{0x17, 0x54},
	{0x19, 0x08},
	{0x1a, 0x0a},
	{0x1f, 0x40},
	{0x20, 0x30},
	{0x2e, 0x80},
	{0x2f, 0x2b},
	{0x30, 0x1a},
	{0xfe, 0x02},
	{0x03, 0x02},
	{0x05, 0xd7},
	{0x06, 0x60},
	{0x08, 0x80},
	{0x12, 0x89},

	/*blk*/
	{0xfe, 0x00},
	{0x18, 0x02},
	{0xfe, 0x02},
	{0x40, 0x22},
	{0x45, 0x00},
	{0x46, 0x00},
	{0x49, 0x20},
	{0x4b, 0x3c},
	{0x50, 0x20},
	{0x42, 0x10},

	/*isp*/
	{0xfe, 0x01},
	{0x0a, 0xc5},
	{0x45, 0x00},
	{0xfe, 0x00},
	{0x40, 0xff},
	{0x41, 0x25},
	{0x42, 0xcf},
	{0x43, 0x10},
	{0x44, 0x83},//Output_format //80
	{0x46, 0x22},//sync
	{0x49, 0x03},
	{0xfe, 0x02},
	{0x22, 0xf6},

	/*Shading*/
	{0xfe, 0x00},
	{0xfe, 0x01},
	{0xc1, 0x38},
	{0xc2, 0x4c},
	{0xc3, 0x00},
	{0xc4, 0x32},
	{0xc5, 0x24},
	{0xc6, 0x16},
	{0xc7, 0x08},
	{0xc8, 0x08},
	{0xc9, 0x00},
	{0xca, 0x20},
	{0xdc, 0x8a},
	{0xdd, 0xa0},
	{0xde, 0xa6},
	{0xdf, 0x75},

	/*AWB*//*20170110*/
	{0xfe, 0x01},
	{0x90, 0x00},
	{0x91, 0x00},
	{0x92, 0xe3},
	{0x93, 0xbe},
	{0x95, 0x0b},
	{0x96, 0xe3},
	{0x97, 0x2e},
	{0x98, 0x0b},
	{0x9a, 0x2d},
	{0x9b, 0x0b},
	{0x9c, 0x59},
	{0x9d, 0x2e},
	{0x9f, 0x67},
	{0xa0, 0x59},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x86, 0x00},
	{0x87, 0x00},
	{0x88, 0x00},
	{0x89, 0x00},
	{0xa4, 0x00},
	{0xa5, 0x00},
	{0xa6, 0xda},
	{0xa7, 0x97},
	{0xa9, 0xda},
	{0xaa, 0x9a},
	{0xab, 0xac},
	{0xac, 0x86},
	{0xae, 0xda},
	{0xaf, 0xac},
	{0xb0, 0xda},
	{0xb1, 0xac},
	{0xb3, 0xda},
	{0xb4, 0xac},
	{0xb5, 0x00},
	{0xb6, 0x00},
	{0x8b, 0x00},
	{0x8c, 0x00},
	{0x8d, 0x00},
	{0x8e, 0x00},
	{0x94, 0x50},
	{0x99, 0xa6},
	{0x9e, 0xaa},
	{0xa3, 0x0a},
	{0x8a, 0x00},
	{0xa8, 0x50},
	{0xad, 0x55},
	{0xb2, 0x55},
	{0xb7, 0x05},
	{0x8f, 0x00},
	{0xb8, 0xae},
	{0xb9, 0xbb},

	/*CC*/
	{0xfe, 0x01},
	{0xd0, 0x40},
	{0xd1, 0xf8},
	{0xd2, 0x00},
	{0xd3, 0xfa},
	{0xd4, 0x45},
	{0xd5, 0x02},
	{0xd6, 0x30},
	{0xd7, 0xfa},
	{0xd8, 0x08},
	{0xd9, 0x08},
	{0xda, 0x58},
	{0xdb, 0x02},
	{0xfe, 0x00},

	/*Gamma*/
	{0xfe, 0x00},
	{0xba, 0x00},
	{0xbb, 0x04},
	{0xbc, 0x0a},
	{0xbd, 0x0e},
	{0xbe, 0x22},
	{0xbf, 0x30},
	{0xc0, 0x3d},
	{0xc1, 0x4a},
	{0xc2, 0x5d},
	{0xc3, 0x6b},
	{0xc4, 0x7a},
	{0xc5, 0x85},
	{0xc6, 0x90},
	{0xc7, 0xa5},
	{0xc8, 0xb5},
	{0xc9, 0xc2},
	{0xca, 0xcc},
	{0xcb, 0xd5},
	{0xcc, 0xde},
	{0xcd, 0xea},
	{0xce, 0xf5},
	{0xcf, 0xff},

	/*Auto Gamma*/
	{0xfe, 0x00},
	{0x5a, 0x08},
	{0x5b, 0x0f},
	{0x5c, 0x15},
	{0x5d, 0x1c},
	{0x5e, 0x28},
	{0x5f, 0x36},
	{0x60, 0x45},
	{0x61, 0x51},
	{0x62, 0x6a},
	{0x63, 0x7d},
	{0x64, 0x8d},
	{0x65, 0x98},
	{0x66, 0xa2},
	{0x67, 0xb5},
	{0x68, 0xc3},
	{0x69, 0xcd},
	{0x6a, 0xd4},
	{0x6b, 0xdc},
	{0x6c, 0xe3},
	{0x6d, 0xf0},
	{0x6e, 0xf9},
	{0x6f, 0xff},

	/*Gain*/
	{0xfe, 0x00},
	{0x70, 0x50},

	/*AEC*/
	{0xfe, 0x00},
	{0x4f, 0x01},
	{0xfe, 0x01},
	{0x0d, 0x00},//08 add 20170110
	{0x12, 0xa0},
	{0x13, 0x3a},
	{0x44, 0x04},
	{0x1f, 0x30},
	{0x20, 0x40},
	{0x26, 0x9a},
	{0x3e, 0x20},
	{0x3f, 0x2d},
	{0x40, 0x40},
	{0x41, 0x5b},
	{0x42, 0x82},
	{0x43, 0xb7},
	{0x04, 0x0a},
	{0x02, 0x79},
	{0x03, 0xc0},

	/*measure window*/
	{0xfe, 0x01},
	{0xcc, 0x08},
	{0xcd, 0x08},
	{0xce, 0xa4},
	{0xcf, 0xec},

	/*DNDD*/
	{0xfe, 0x00},
	{0x81, 0xb8},
	{0x82, 0x15},//de_noise
	{0x83, 0x1a},//de_noise dark
	{0x84, 0x01},
	{0x86, 0x50},
	{0x87, 0x18},
	{0x88, 0x10},
	{0x89, 0x70},
	{0x8a, 0x20},
	{0x8b, 0x10},
	{0x8c, 0x08},
	{0x8d, 0x0a},

	/*Intpee*/
	{0xfe, 0x00},
	{0x8f, 0xaa},
	{0x90, 0x9c},
	{0x91, 0x52},
	{0x92, 0x03},
	{0x93, 0x03},
	{0x94, 0x08},
	{0x95, 0x65},//sharpness
	{0x97, 0x00},
	{0x98, 0x00},

	/*ASDE*/
	{0xfe, 0x00},
	{0xa1, 0x30},
	{0xa2, 0x41},
	{0xa4, 0x30},
	{0xa5, 0x20},
	{0xaa, 0x30},
	{0xac, 0x32},

	/*YCP*/
	{0xfe, 0x00},
	{0xd1, 0x37},//3a
	{0xd2, 0x37},//3a
	{0xd3, 0x40},//38
	{0xd6, 0xf4},
	{0xd7, 0x1d},
	{0xdd, 0x73},
	{0xde, 0x84},

#ifdef GC032A_AUTO_FPS
	/*Banding*/
	/* 7fps */
	{0xfe, 0x00},
	{0x05, 0x01},
	{0x06, 0xad},
	{0x07, 0x00},
	{0x08, 0x10},

	{0xfe, 0x01},
	{0x25, 0x00},
	{0x26, 0x9a},
	{0x27, 0x01},
	{0x28, 0xce},
	{0x29, 0x03},
	{0x2a, 0x02},
	{0x2b, 0x04},
	{0x2c, 0x36},
	{0x2d, 0x07},
	{0x2e, 0xd2},
	{0x2f, 0x0b},
	{0x30, 0x6e},
	{0x31, 0x0e},
	{0x32, 0x70},
	{0x33, 0x12},
	{0x34, 0x0c},
	{0x3c, 0x20},
#else
	/*Banding*/
	/* 30 fps */
	{0xfe, 0x00},
	{0x05, 0x01},
	{0x06, 0xad},
	{0x07, 0x00},
	{0x08, 0x10},

	{0xfe, 0x01},
	{0x25, 0x00},
	{0x26, 0x9a},
	{0x27, 0x01},
	{0x28, 0xce},
	{0x29, 0x01},
	{0x2a, 0xce},
	{0x2b, 0x01},
	{0x2c, 0xce},
	{0x2d, 0x01},
	{0x2e, 0xce},
	{0x2f, 0x01},
	{0x30, 0xce},
	{0x31, 0x01},
	{0x32, 0xce},
	{0x33, 0x01},
	{0x34, 0xce},
	{0x3c, 0x00},
#endif
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

static const struct gc032a_framesize gc032a_framesizes[] = {
	{ /* VGA */
		.width		= 640,
		.height		= 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.regs		= gc032a_vga_regs,
		.max_exp_lines	= 488,
	}
};

static const struct gc032a_pixfmt gc032a_formats[] = {
	{
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
	}
};

static inline struct gc032a *to_gc032a(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc032a, sd);
}

/* sensor register write */
static int gc032a_write(struct i2c_client *client, u8 reg, u8 val)
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
		"gc032a write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc032a_read(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc032a read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc032a_write_array(struct i2c_client *client,
			      const struct sensor_register *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = gc032a_write(client, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

static void gc032a_get_default_format(struct v4l2_mbus_framefmt *format)
{
	format->width = gc032a_framesizes[0].width;
	format->height = gc032a_framesizes[0].height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = gc032a_formats[0].code;
	format->field = V4L2_FIELD_NONE;
}

static void gc032a_set_streaming(struct gc032a *gc032a, int on)
{
	struct i2c_client *client = gc032a->client;
	int ret;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	ret = gc032a_write(client, REG_SOFTWARE_STANDBY, on);
	if (ret)
		dev_err(&client->dev, "gc032a soft standby failed\n");
}

/*
 * V4L2 subdev video and pad level operations
 */

static int gc032a_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (code->index >= ARRAY_SIZE(gc032a_formats))
		return -EINVAL;

	code->code = gc032a_formats[code->index].code;

	return 0;
}

static int gc032a_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(gc032a_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(gc032a_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == gc032a_formats[i].code)
			break;

	fse->code = gc032a_formats[i].code;

	fse->min_width  = gc032a_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = gc032a_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int gc032a_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc032a *gc032a = to_gc032a(sd);

	dev_dbg(&client->dev, "%s enter\n", __func__);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&gc032a->lock);
		fmt->format = *mf;
		mutex_unlock(&gc032a->lock);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&gc032a->lock);
	fmt->format = gc032a->format;
	mutex_unlock(&gc032a->lock);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		gc032a->format.code, gc032a->format.width,
		gc032a->format.height);

	return 0;
}

static void __gc032a_try_frame_size(struct v4l2_mbus_framefmt *mf,
				    const struct gc032a_framesize **size)
{
	const struct gc032a_framesize *fsize = &gc032a_framesizes[0];
	const struct gc032a_framesize *match = NULL;
	int i = ARRAY_SIZE(gc032a_framesizes);
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

	if (!match)
		match = &gc032a_framesizes[0];

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int gc032a_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int index = ARRAY_SIZE(gc032a_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct gc032a_framesize *size = NULL;
	struct gc032a *gc032a = to_gc032a(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s enter\n", __func__);

	__gc032a_try_frame_size(mf, &size);

	while (--index >= 0)
		if (gc032a_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = gc032a_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&gc032a->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (gc032a->streaming) {
			mutex_unlock(&gc032a->lock);
			return -EBUSY;
		}

		gc032a->frame_size = size;
		gc032a->format = fmt->format;
	}

	mutex_unlock(&gc032a->lock);
	return ret;
}

static void gc032a_get_module_inf(struct gc032a *gc032a,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, DRIVER_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc032a->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc032a->len_name, sizeof(inf->base.lens));
}

static long gc032a_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc032a *gc032a = to_gc032a(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc032a_get_module_inf(gc032a, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			gc032a_set_streaming(gc032a, 0xff);
		else
			gc032a_set_streaming(gc032a, 0x00);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc032a_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = gc032a_ioctl(sd, cmd, inf);
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
			ret = gc032a_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc032a_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int gc032a_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc032a *gc032a = to_gc032a(sd);

	dev_info(&client->dev, "%s: on: %d\n", __func__, on);

	mutex_lock(&gc032a->lock);
	on = !!on;
	if (gc032a->streaming == on)
		goto unlock;

	if (!on) {
		/* Stop Streaming Sequence */
		gc032a_set_streaming(gc032a, 0x00);
		gc032a->streaming = on;
		goto unlock;
	}

	gc032a_set_streaming(gc032a, 0xFF);
	gc032a->streaming = on;

unlock:
	mutex_unlock(&gc032a->lock);
	return 0;
}

static int gc032a_set_test_pattern(struct gc032a *gc032a, int value)
{
	return 0;
}

static int gc032a_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc032a *gc032a =
			container_of(ctrl->handler, struct gc032a, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return gc032a_set_test_pattern(gc032a, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops gc032a_ctrl_ops = {
	.s_ctrl = gc032a_s_ctrl,
};

static const char * const gc032a_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc032a_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);

	dev_dbg(&client->dev, "%s:\n", __func__);

	gc032a_get_default_format(format);

	return 0;
}
#endif

static int gc032a_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_LOW |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int gc032a_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	struct gc032a *gc032a = to_gc032a(sd);
	struct i2c_client *client = gc032a->client;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	if (on) {
		if (!IS_ERR(gc032a->pwdn_gpio)) {
			gpiod_set_value_cansleep(gc032a->pwdn_gpio, 0);
			usleep_range(2000, 5000);
		}
		ret = gc032a_write_array(client, gc032a->frame_size->regs);
		if (ret)
			dev_err(&client->dev, "init error\n");
		gc032a->power_on = true;
	} else {
		if (!IS_ERR(gc032a->pwdn_gpio)) {
			gpiod_set_value_cansleep(gc032a->pwdn_gpio, 1);
			usleep_range(2000, 5000);
		}
		gc032a->power_on = false;
	}
	return 0;
}

static int gc032a_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(gc032a_framesizes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_YUYV8_2X8)
		return -EINVAL;

	fie->width = gc032a_framesizes[fie->index].width;
	fie->height = gc032a_framesizes[fie->index].height;
	fie->interval = gc032a_framesizes[fie->index].max_fps;
	return 0;
}

static const struct v4l2_subdev_core_ops gc032a_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = gc032a_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc032a_compat_ioctl32,
#endif
	.s_power = gc032a_power,
};

static const struct v4l2_subdev_video_ops gc032a_subdev_video_ops = {
	.s_stream = gc032a_s_stream,
	.g_mbus_config = gc032a_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops gc032a_subdev_pad_ops = {
	.enum_mbus_code = gc032a_enum_mbus_code,
	.enum_frame_size = gc032a_enum_frame_sizes,
	.enum_frame_interval = gc032a_enum_frame_interval,
	.get_fmt = gc032a_get_fmt,
	.set_fmt = gc032a_set_fmt,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_ops gc032a_subdev_ops = {
	.core  = &gc032a_subdev_core_ops,
	.video = &gc032a_subdev_video_ops,
	.pad   = &gc032a_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc032a_subdev_internal_ops = {
	.open = gc032a_open,
};
#endif

static int gc032a_detect(struct gc032a *gc032a)
{
	struct i2c_client *client = gc032a->client;
	u8 pid, ver;
	int ret;

	dev_dbg(&client->dev, "%s:\n", __func__);

	/* Check sensor revision */
	ret = gc032a_read(client, REG_SC_CHIP_ID_H, &pid);
	if (!ret)
		ret = gc032a_read(client, REG_SC_CHIP_ID_L, &ver);

	if (!ret) {
		unsigned short id;

		id = SENSOR_ID(pid, ver);
		if (id != GC032A_ID) {
			ret = -1;
			dev_err(&client->dev,
				"Sensor detection failed (%04X, %d)\n",
				id, ret);
		} else {
			dev_info(&client->dev, "Found GC%04X sensor\n", id);
			if (!IS_ERR(gc032a->pwdn_gpio))
				gpiod_set_value_cansleep(gc032a->pwdn_gpio, 1);
		}
	}

	return ret;
}

static int __gc032a_power_on(struct gc032a *gc032a)
{
	int ret;
	struct device *dev = &gc032a->client->dev;

	if (!IS_ERR(gc032a->xvclk)) {
		ret = clk_set_rate(gc032a->xvclk, 24000000);
		if (ret < 0)
			dev_info(dev, "Failed to set xvclk rate (24MHz)\n");
	}

	if (!IS_ERR(gc032a->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc032a->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc032a->supplies)) {
		ret = regulator_bulk_enable(GC032A_NUM_SUPPLIES,
			gc032a->supplies);
		if (ret < 0)
			dev_info(dev, "Failed to enable regulators\n");

		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc032a->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc032a->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc032a->xvclk)) {
		ret = clk_prepare_enable(gc032a->xvclk);
		if (ret < 0)
			dev_info(dev, "Failed to enable xvclk\n");
	}

	usleep_range(7000, 10000);
	gc032a->power_on = true;
	return 0;
}

static void __gc032a_power_off(struct gc032a *gc032a)
{
	if (!IS_ERR(gc032a->xvclk))
		clk_disable_unprepare(gc032a->xvclk);
	if (!IS_ERR(gc032a->supplies))
		regulator_bulk_disable(GC032A_NUM_SUPPLIES, gc032a->supplies);
	if (!IS_ERR(gc032a->pwdn_gpio))
		gpiod_set_value_cansleep(gc032a->pwdn_gpio, 1);
	gc032a->power_on = false;
}

static int gc032a_configure_regulators(struct gc032a *gc032a)
{
	unsigned int i;

	for (i = 0; i < GC032A_NUM_SUPPLIES; i++)
		gc032a->supplies[i].supply = gc032a_supply_names[i];

	return devm_regulator_bulk_get(&gc032a->client->dev,
				       GC032A_NUM_SUPPLIES,
				       gc032a->supplies);
}

static int gc032a_parse_of(struct gc032a *gc032a)
{
	struct device *dev = &gc032a->client->dev;
	int ret;

	gc032a->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc032a->pwdn_gpio))
		dev_info(dev, "Failed to get pwdn-gpios, maybe no used\n");

	ret = gc032a_configure_regulators(gc032a);
	if (ret)
		dev_info(dev, "Failed to get power regulators\n");

	return __gc032a_power_on(gc032a);
}

static int gc032a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	struct gc032a *gc032a;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc032a = devm_kzalloc(&client->dev, sizeof(*gc032a), GFP_KERNEL);
	if (!gc032a)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc032a->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc032a->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc032a->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc032a->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc032a->client = client;
	gc032a->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(gc032a->xvclk)) {
		dev_err(&client->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc032a_parse_of(gc032a);

	gc032a->xvclk_frequency = clk_get_rate(gc032a->xvclk);
	if (gc032a->xvclk_frequency < 6000000 ||
	    gc032a->xvclk_frequency > 27000000)
		return -EINVAL;

	v4l2_ctrl_handler_init(&gc032a->ctrls, 2);
	gc032a->link_frequency =
			v4l2_ctrl_new_std(&gc032a->ctrls, &gc032a_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  GC032A_PIXEL_RATE, 1,
					  GC032A_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&gc032a->ctrls, &gc032a_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc032a_test_pattern_menu) - 1,
				     0, 0, gc032a_test_pattern_menu);
	gc032a->sd.ctrl_handler = &gc032a->ctrls;

	if (gc032a->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, gc032a->ctrls.error);
		return  gc032a->ctrls.error;
	}

	sd = &gc032a->sd;
	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &gc032a_subdev_ops);

	sd->internal_ops = &gc032a_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	gc032a->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc032a->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&gc032a->ctrls);
		return ret;
	}
#endif

	mutex_init(&gc032a->lock);

	gc032a_get_default_format(&gc032a->format);
	gc032a->frame_size = &gc032a_framesizes[0];

	ret = gc032a_detect(gc032a);
	if (ret < 0)
		goto error;

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc032a->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc032a->module_index, facing,
		 DRIVER_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret)
		goto error;

	dev_info(&client->dev, "%s sensor driver registered !!\n", sd->name);
	gc032a->power_on = false;
	return 0;

error:
	v4l2_ctrl_handler_free(&gc032a->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc032a->lock);
	__gc032a_power_off(gc032a);
	return ret;
}

static int gc032a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc032a *gc032a = to_gc032a(sd);

	v4l2_ctrl_handler_free(&gc032a->ctrls);
	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc032a->lock);

	__gc032a_power_off(gc032a);

	return 0;
}

static const struct i2c_device_id gc032a_id[] = {
	{ "gc032a", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, gc032a_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc032a_of_match[] = {
	{ .compatible = "galaxycore,gc032a", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, gc032a_of_match);
#endif

static struct i2c_driver gc032a_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(gc032a_of_match),
	},
	.probe		= gc032a_probe,
	.remove		= gc032a_remove,
	.id_table	= gc032a_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc032a_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc032a_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("randy.wang <randy.wang@rock-chips.com>");
MODULE_DESCRIPTION("GC032A CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
