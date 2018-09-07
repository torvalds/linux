// SPDX-License-Identifier: GPL-2.0
/*
 * GC0312 CMOS Image Sensor driver
 *
 * Copyright (C) 2015 Texas Instruments, Inc.
 *
 * Benoit Parrot <bparrot@ti.com>
 * Lad, Prabhakar <prabhakar.csengg@gmail.com>
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define DRIVER_NAME "gc0312"
#define GC0312_PIXEL_RATE		(96 * 1000 * 1000)

/*
 * GC0312 register definitions
 */
#define REG_SOFTWARE_STANDBY		0xf3

#define REG_SC_CHIP_ID_H		0xf0
#define REG_SC_CHIP_ID_L		0xf1

#define REG_NULL			0xFFFF	/* Array end token */

#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))
#define GC0312_ID			0xb310

struct sensor_register {
	u16 addr;
	u8 value;
};

struct gc0312_framesize {
	u16 width;
	u16 height;
	u16 max_exp_lines;
	const struct sensor_register *regs;
};

struct gc0312_pll_ctrl {
	u8 ctrl1;
	u8 ctrl2;
	u8 ctrl3;
};

struct gc0312_pixfmt {
	u32 code;
	/* Output format Register Value (REG_FORMAT_CTRL00) */
	struct sensor_register *format_ctrl_regs;
};

struct pll_ctrl_reg {
	unsigned int div;
	unsigned char reg;
};

static const char * const gc0312_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

#define GC0312_NUM_SUPPLIES ARRAY_SIZE(gc0312_supply_names)

struct gc0312 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int xvclk_frequency;
	struct clk *xvclk;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[GC0312_NUM_SUPPLIES];
	struct mutex lock;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_frequency;
	const struct gc0312_framesize *frame_size;
	int streaming;
};

static const struct sensor_register gc0312_vga_regs[] = {
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfe, 0x00},
	{0xfc, 0x0e},
	{0xfc, 0x0e},
	{0xf2, 0x07},
	/*output_disable*/
	{0xf3, 0x00},
	{0xf7, 0x1b},
	{0xf8, 0x04},
	{0xf9, 0x0e},
	{0xfa, 0x11},

	/*CISCTL reg*/
	{0x00, 0x2f},
	{0x01, 0x0f},
	{0x02, 0x04},
	{0x03, 0x03},
	{0x04, 0x50},
	{0x09, 0x00},
	{0x0a, 0x00},
	{0x0b, 0x00},
	{0x0c, 0x04},
	{0x0d, 0x01},
	{0x0e, 0xe8},
	{0x0f, 0x02},
	{0x10, 0x88},
	{0x16, 0x00},
	{0x17, 0x17},
	{0x18, 0x1a},
	{0x19, 0x14},
	{0x1b, 0x48},
	/*1c travis 20140929  update for lag*/
	{0x1c, 0x1c},
	{0x1e, 0x6b},
	{0x1f, 0x28},
	/*0x89 travis20140801*/
	{0x20, 0x8b},
	{0x21, 0x49},
	/*b0 travis 20140929 update for lag*/
	{0x22, 0xb0},
	{0x23, 0x04},
	{0x24, 0x16},
	{0x34, 0x20},

	/*BLK*/
	{0x26, 0x23},
	{0x28, 0xff},
	{0x29, 0x00},
	{0x32, 0x00},
	{0x33, 0x10},
	{0x37, 0x20},
	{0x38, 0x10},
	{0x47, 0x80},
	{0x4e, 0x66},
	{0xa8, 0x02},
	{0xa9, 0x80},

	/*ISP reg*/
	{0x40, 0xff},
	{0x41, 0x21},
	{0x42, 0xcf},
	{0x44, 0x02},
	{0x45, 0xa8},
	/*sync 02*/
	{0x46, 0x02},
	{0x4a, 0x11},
	{0x4b, 0x01},
	{0x4c, 0x20},
	{0x4d, 0x05},
	{0x4f, 0x01},
	{0x50, 0x01},
	{0x55, 0x01},
	{0x56, 0xe0},
	{0x57, 0x02},
	{0x58, 0x80},

	/*GAIN*/
	{0x70, 0x70},
	{0x5a, 0x84},
	{0x5b, 0xc9},
	{0x5c, 0xed},
	{0x77, 0x74},
	{0x78, 0x40},
	{0x79, 0x5f},

	/*DNDD*/
	{0x82, 0x14},
	{0x83, 0x0b},
	{0x89, 0xf0},

	/*EEINTP*/
	{0x8f, 0xaa},
	{0x90, 0x8c},
	{0x91, 0x90},
	{0x92, 0x03},
	{0x93, 0x03},
	{0x94, 0x05},
	{0x95, 0x65},
	{0x96, 0xf0},

	/*ASDE*/
	{0xfe, 0x00},

	{0x9a, 0x20},
	{0x9b, 0x80},
	{0x9c, 0x40},
	{0x9d, 0x80},

	{0xa1, 0x30},
	{0xa2, 0x32},
	{0xa4, 0x80},
	{0xa5, 0x28},
	{0xaa, 0x30},
	{0xac, 0x22},

	/*GAMMA*/
	{0xfe, 0x00},
	{0xbf, 0x08},
	{0xc0, 0x16},
	{0xc1, 0x28},
	{0xc2, 0x41},
	{0xc3, 0x5a},
	{0xc4, 0x6c},
	{0xc5, 0x7a},
	{0xc6, 0x96},
	{0xc7, 0xac},
	{0xc8, 0xbc},
	{0xc9, 0xc9},
	{0xca, 0xd3},
	{0xcb, 0xdd},
	{0xcc, 0xe5},
	{0xcd, 0xf1},
	{0xce, 0xfa},
	{0xcf, 0xff},

	/*YCP*/
	{0xd0, 0x40},
	{0xd1, 0x34},
	{0xd2, 0x34},
	{0xd3, 0x40},
	{0xd6, 0xf2},
	{0xd7, 0x1b},
	{0xd8, 0x18},
	{0xdd, 0x03},

	/*AEC*/
	{0xfe, 0x01},
	{0x05, 0x30},
	{0x06, 0x75},
	{0x07, 0x40},
	{0x08, 0xb0},
	{0x0a, 0xc5},
	{0x0b, 0x11},
	{0x0c, 0x00},
	{0x12, 0x52},
	{0x13, 0x38},
	{0x18, 0x95},
	{0x19, 0x96},
	{0x1f, 0x20},
	{0x20, 0xc0},
	{0x3e, 0x40},
	{0x3f, 0x57},
	{0x40, 0x7d},
	{0x03, 0x60},
	{0x44, 0x02},

	/*AWB*/
	{0xfe, 0x01},
	{0x1c, 0x91},
	{0x21, 0x15},
	{0x50, 0x80},
	{0x56, 0x04},
	{0x59, 0x08},
	{0x5b, 0x02},
	{0x61, 0x8d},
	{0x62, 0xa7},
	{0x63, 0xd0},
	{0x65, 0x06},
	{0x66, 0x06},
	{0x67, 0x84},
	{0x69, 0x08},
	{0x6a, 0x25},
	{0x6b, 0x01},
	{0x6c, 0x00},
	{0x6d, 0x02},
	{0x6e, 0xf0},
	{0x6f, 0x80},
	{0x76, 0x80},
	{0x78, 0xaf},
	{0x79, 0x75},
	{0x7a, 0x40},
	{0x7b, 0x50},
	{0x7c, 0x0c},

	{0x90, 0xc9},
	{0x91, 0xbe},
	{0x92, 0xe2},
	{0x93, 0xc9},
	{0x95, 0x1b},
	{0x96, 0xe2},
	{0x97, 0x49},
	{0x98, 0x1b},
	{0x9a, 0x49},
	{0x9b, 0x1b},
	{0x9c, 0xc3},
	{0x9d, 0x49},
	{0x9f, 0xc7},
	{0xa0, 0xc8},
	{0xa1, 0x00},
	{0xa2, 0x00},
	{0x86, 0x00},
	{0x87, 0x00},
	{0x88, 0x00},
	{0x89, 0x00},
	{0xa4, 0xb9},
	{0xa5, 0xa0},
	{0xa6, 0xba},
	{0xa7, 0x92},
	{0xa9, 0xba},
	{0xaa, 0x80},
	{0xab, 0x9d},
	{0xac, 0x7f},
	{0xae, 0xbb},
	{0xaf, 0x9d},
	{0xb0, 0xc8},
	{0xb1, 0x97},
	{0xb3, 0xb7},
	{0xb4, 0x7f},
	{0xb5, 0x00},
	{0xb6, 0x00},
	{0x8b, 0x00},
	{0x8c, 0x00},
	{0x8d, 0x00},
	{0x8e, 0x00},
	{0x94, 0x55},
	{0x99, 0xa6},
	{0x9e, 0xaa},
	{0xa3, 0x0a},
	{0x8a, 0x00},
	{0xa8, 0x55},
	{0xad, 0x55},
	{0xb2, 0x55},
	{0xb7, 0x05},
	{0x8f, 0x00},
	{0xb8, 0xcb},
	{0xb9, 0x9b},
	/*CC*/
	{0xfe, 0x01},
	/*skin white*/
	{0xd0, 0x38},
	{0xd1, 0x00},
	{0xd2, 0x02},
	{0xd3, 0x04},
	{0xd4, 0x38},
	{0xd5, 0x12},

	{0xd6, 0x30},
	{0xd7, 0x00},
	{0xd8, 0x0a},
	{0xd9, 0x16},
	{0xda, 0x39},
	{0xdb, 0xf8},

	/*LSC*/
	{0xfe, 0x01},
	{0xc1, 0x3c},
	{0xc2, 0x50},
	{0xc3, 0x00},
	{0xc4, 0x40},
	{0xc5, 0x30},
	{0xc6, 0x30},
	{0xc7, 0x10},
	{0xc8, 0x00},
	{0xc9, 0x00},
	{0xdc, 0x20},
	{0xdd, 0x10},
	{0xdf, 0x00},
	{0xde, 0x00},

	/*Histogram*/
	{0x01, 0x10},
	{0x0b, 0x31},
	{0x0e, 0x50},
	{0x0f, 0x0f},
	{0x10, 0x6e},
	{0x12, 0xa0},
	{0x15, 0x60},
	{0x16, 0x60},
	{0x17, 0xe0},

	/*Measure Window*/
	{0xcc, 0x0c},
	{0xcd, 0x10},
	{0xce, 0xa0},
	{0xcf, 0xe6},

	/*dark sun*/
	{0x45, 0xf7},
	{0x46, 0xff},
	{0x47, 0x15},
	{0x48, 0x03},
	{0x4f, 0x60},

	/*banding*/
	{0xfe, 0x00},
	{0x05, 0x00},
	/*HB*/
	{0x06, 0x90},
	{0x07, 0x00},
	/*VB*/
	{0x08, 0x64},

	{0xfe, 0x01},
	/*anti-flicker step [11:8]*/
	{0x25, 0x00},
	/*anti-flicker step [7:0]*/
	{0x26, 0xb3},

	/*exp level 0 */
	{0x27, 0x02},
	{0x28, 0x19},
	/*exp level 1 */
	{0x29, 0x02},
	{0x2a, 0x19},
	/*7.14fps*/
	{0x2b, 0x02},
	{0x2c, 0x19},
	/*exp level 3 */
	{0x2d, 0x02},
	{0x2e, 0x19},
	{0x3c, 0x20},
	{0xfe, 0x00},

	/*DVP*/
	{0xfe, 0x03},
	{0x01, 0x00},
	{0x02, 0x00},
	{0x10, 0x00},
	{0x15, 0x00},
	{0xfe, 0x00},
	{REG_NULL, 0x00},
};

static const struct gc0312_framesize gc0312_framesizes[] = {
	{ /* VGA */
		.width		= 640,
		.height		= 480,
		.regs		= gc0312_vga_regs,
		.max_exp_lines	= 488,
	}
};

static const struct gc0312_pixfmt gc0312_formats[] = {
	{
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
	}
};

static inline struct gc0312 *to_gc0312(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc0312, sd);
}

/* sensor register write */
static int gc0312_write(struct i2c_client *client, u8 reg, u8 val)
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
		"gc0312 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc0312_read(struct i2c_client *client, u8 reg, u8 *val)
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
		"gc0312 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc0312_write_array(struct i2c_client *client,
			      const struct sensor_register *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = gc0312_write(client, regs[i].addr, regs[i].value);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}

		i++;
	}

	return ret;
}

static void gc0312_get_default_format(struct v4l2_mbus_framefmt *format)
{
	format->width = gc0312_framesizes[0].width;
	format->height = gc0312_framesizes[0].height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = gc0312_formats[0].code;
	format->field = V4L2_FIELD_NONE;
}

static void gc0312_set_streaming(struct gc0312 *gc0312, int on)
{
	struct i2c_client *client = gc0312->client;
	int ret;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	ret = gc0312_write(client, REG_SOFTWARE_STANDBY, on);
	if (ret)
		dev_err(&client->dev, "gc0312 soft standby failed\n");
}

/*
 * V4L2 subdev video and pad level operations
 */

static int gc0312_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (code->index >= ARRAY_SIZE(gc0312_formats))
		return -EINVAL;

	code->code = gc0312_formats[code->index].code;

	return 0;
}

static int gc0312_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(gc0312_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(gc0312_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == gc0312_formats[i].code)
			break;

	fse->code = gc0312_formats[i].code;

	fse->min_width  = gc0312_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = gc0312_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int gc0312_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0312 *gc0312 = to_gc0312(sd);

	dev_dbg(&client->dev, "%s enter\n", __func__);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&gc0312->lock);
		fmt->format = *mf;
		mutex_unlock(&gc0312->lock);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&gc0312->lock);
	fmt->format = gc0312->format;
	mutex_unlock(&gc0312->lock);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		gc0312->format.code, gc0312->format.width,
		gc0312->format.height);

	return 0;
}

static void __gc0312_try_frame_size(struct v4l2_mbus_framefmt *mf,
				    const struct gc0312_framesize **size)
{
	const struct gc0312_framesize *fsize = &gc0312_framesizes[0];
	const struct gc0312_framesize *match = NULL;
	int i = ARRAY_SIZE(gc0312_framesizes);
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
		match = &gc0312_framesizes[0];

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int gc0312_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int index = ARRAY_SIZE(gc0312_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct gc0312_framesize *size = NULL;
	struct gc0312 *gc0312 = to_gc0312(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s enter\n", __func__);

	__gc0312_try_frame_size(mf, &size);

	while (--index >= 0)
		if (gc0312_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = gc0312_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&gc0312->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (gc0312->streaming) {
			mutex_unlock(&gc0312->lock);
			return -EBUSY;
		}

		gc0312->frame_size = size;
		gc0312->format = fmt->format;
	}

	mutex_unlock(&gc0312->lock);
	return ret;
}

static int gc0312_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0312 *gc0312 = to_gc0312(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s: on: %d\n", __func__, on);

	mutex_lock(&gc0312->lock);

	on = !!on;

	if (gc0312->streaming == on)
		goto unlock;

	if (!on) {
		/* Stop Streaming Sequence */
		gc0312_set_streaming(gc0312, 0x00);
		gc0312->streaming = on;
		if (!IS_ERR(gc0312->pwdn_gpio)) {
			gpiod_set_value_cansleep(gc0312->pwdn_gpio, 1);
			usleep_range(2000, 5000);
		}
		goto unlock;
	}

	if (!IS_ERR(gc0312->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0312->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	ret = gc0312_write_array(client, gc0312->frame_size->regs);
	if (ret)
		goto unlock;

	gc0312_set_streaming(gc0312, 0xFF);
	gc0312->streaming = on;

unlock:
	mutex_unlock(&gc0312->lock);
	return ret;
}

static int gc0312_set_test_pattern(struct gc0312 *gc0312, int value)
{
	return 0;
}

static int gc0312_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0312 *gc0312 =
			container_of(ctrl->handler, struct gc0312, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return gc0312_set_test_pattern(gc0312, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops gc0312_ctrl_ops = {
	.s_ctrl = gc0312_s_ctrl,
};

static const char * const gc0312_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc0312_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);

	dev_dbg(&client->dev, "%s:\n", __func__);

	gc0312_get_default_format(format);

	return 0;
}
#endif

static int gc0312_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static const struct v4l2_subdev_core_ops gc0312_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops gc0312_subdev_video_ops = {
	.s_stream = gc0312_s_stream,
	.g_mbus_config = gc0312_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops gc0312_subdev_pad_ops = {
	.enum_mbus_code = gc0312_enum_mbus_code,
	.enum_frame_size = gc0312_enum_frame_sizes,
	.get_fmt = gc0312_get_fmt,
	.set_fmt = gc0312_set_fmt,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_ops gc0312_subdev_ops = {
	.core  = &gc0312_subdev_core_ops,
	.video = &gc0312_subdev_video_ops,
	.pad   = &gc0312_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc0312_subdev_internal_ops = {
	.open = gc0312_open,
};
#endif

static int gc0312_detect(struct gc0312 *gc0312)
{
	struct i2c_client *client = gc0312->client;
	u8 pid, ver;
	int ret;

	dev_dbg(&client->dev, "%s:\n", __func__);

	/* Check sensor revision */
	ret = gc0312_read(client, REG_SC_CHIP_ID_H, &pid);
	if (!ret)
		ret = gc0312_read(client, REG_SC_CHIP_ID_L, &ver);

	if (!ret) {
		unsigned short id;

		id = SENSOR_ID(pid, ver);
		if (id != GC0312_ID) {
			ret = -1;
			dev_err(&client->dev,
				"Sensor detection failed (%04X, %d)\n",
				id, ret);
		} else {
			dev_info(&client->dev, "Found GC%04X sensor\n", id);
			if (!IS_ERR(gc0312->pwdn_gpio))
				gpiod_set_value_cansleep(gc0312->pwdn_gpio, 1);
		}
	}

	return ret;
}

static int __gc0312_power_on(struct gc0312 *gc0312)
{
	int ret;
	struct device *dev = &gc0312->client->dev;

	if (!IS_ERR(gc0312->xvclk)) {
		ret = clk_set_rate(gc0312->xvclk, 24000000);
		if (ret < 0)
			dev_info(dev, "Failed to set xvclk rate (24MHz)\n");
	}

	if (!IS_ERR(gc0312->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0312->pwdn_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0312->supplies)) {
		ret = regulator_bulk_enable(GC0312_NUM_SUPPLIES,
			gc0312->supplies);
		if (ret < 0)
			dev_info(dev, "Failed to enable regulators\n");

		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0312->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc0312->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc0312->xvclk)) {
		ret = clk_prepare_enable(gc0312->xvclk);
		if (ret < 0)
			dev_info(dev, "Failed to enable xvclk\n");
	}

	usleep_range(7000, 10000);

	return 0;
}

static void __gc0312_power_off(struct gc0312 *gc0312)
{
	if (!IS_ERR(gc0312->xvclk))
		clk_disable_unprepare(gc0312->xvclk);
	if (!IS_ERR(gc0312->supplies))
		regulator_bulk_disable(GC0312_NUM_SUPPLIES, gc0312->supplies);
	if (!IS_ERR(gc0312->pwdn_gpio))
		gpiod_set_value_cansleep(gc0312->pwdn_gpio, 1);
}

static int gc0312_configure_regulators(struct gc0312 *gc0312)
{
	unsigned int i;

	for (i = 0; i < GC0312_NUM_SUPPLIES; i++)
		gc0312->supplies[i].supply = gc0312_supply_names[i];

	return devm_regulator_bulk_get(&gc0312->client->dev,
				       GC0312_NUM_SUPPLIES,
				       gc0312->supplies);
}

static int gc0312_parse_of(struct gc0312 *gc0312)
{
	struct device *dev = &gc0312->client->dev;
	int ret;

	gc0312->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc0312->pwdn_gpio))
		dev_info(dev, "Failed to get pwdn-gpios, maybe no used\n");

	ret = gc0312_configure_regulators(gc0312);
	if (ret)
		dev_info(dev, "Failed to get power regulators\n");

	return __gc0312_power_on(gc0312);
}

static int gc0312_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct gc0312 *gc0312;
	int ret;

	gc0312 = devm_kzalloc(&client->dev, sizeof(*gc0312), GFP_KERNEL);
	if (!gc0312)
		return -ENOMEM;

	gc0312->client = client;
	gc0312->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(gc0312->xvclk)) {
		dev_err(&client->dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc0312_parse_of(gc0312);

	gc0312->xvclk_frequency = clk_get_rate(gc0312->xvclk);
	if (gc0312->xvclk_frequency < 6000000 ||
	    gc0312->xvclk_frequency > 27000000)
		return -EINVAL;

	v4l2_ctrl_handler_init(&gc0312->ctrls, 2);
	gc0312->link_frequency =
			v4l2_ctrl_new_std(&gc0312->ctrls, &gc0312_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  GC0312_PIXEL_RATE, 1,
					  GC0312_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&gc0312->ctrls, &gc0312_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc0312_test_pattern_menu) - 1,
				     0, 0, gc0312_test_pattern_menu);
	gc0312->sd.ctrl_handler = &gc0312->ctrls;

	if (gc0312->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, gc0312->ctrls.error);
		return  gc0312->ctrls.error;
	}

	sd = &gc0312->sd;
	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &gc0312_subdev_ops);

	sd->internal_ops = &gc0312_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	gc0312->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &gc0312->pad, 0);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&gc0312->ctrls);
		return ret;
	}
#endif

	mutex_init(&gc0312->lock);

	gc0312_get_default_format(&gc0312->format);
	gc0312->frame_size = &gc0312_framesizes[0];

	ret = gc0312_detect(gc0312);
	if (ret < 0)
		goto error;

	ret = v4l2_async_register_subdev(&gc0312->sd);
	if (ret)
		goto error;

	dev_info(&client->dev, "%s sensor driver registered !!\n", sd->name);

	return 0;

error:
	v4l2_ctrl_handler_free(&gc0312->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc0312->lock);
	__gc0312_power_off(gc0312);
	return ret;
}

static int gc0312_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0312 *gc0312 = to_gc0312(sd);

	v4l2_ctrl_handler_free(&gc0312->ctrls);
	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc0312->lock);

	__gc0312_power_off(gc0312);

	return 0;
}

static const struct i2c_device_id gc0312_id[] = {
	{ "gc0312", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, gc0312_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc0312_of_match[] = {
	{ .compatible = "galaxycore,gc0312", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, gc0312_of_match);
#endif

static struct i2c_driver gc0312_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(gc0312_of_match),
	},
	.probe		= gc0312_probe,
	.remove		= gc0312_remove,
	.id_table	= gc0312_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc0312_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc0312_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Benoit Parrot <bparrot@ti.com>");
MODULE_DESCRIPTION("GC0312 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
