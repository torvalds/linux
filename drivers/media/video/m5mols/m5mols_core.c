/*
 * Driver for M5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/m5mols.h>

#include "m5mols.h"
#include "m5mols_reg.h"

int m5mols_debug;
module_param(m5mols_debug, int, 0644);

#define MOD_NAME		"M5MOLS"
#define M5MOLS_I2C_CHECK_RETRY 50
#define DEBUG
#define DEFAULT_SENSOR_WIDTH	800
#define DEFAULT_SENSOR_HEIGHT	480

#define m5_err	\
	do { printk("%s : %d : ret : %d\n", __func__, __LINE__, ret);\
	} while(0)
/* M5MOLS mode */
static u8 m5mols_reg_mode[] = {
	[MODE_SYSINIT]		= 0x00,
	[MODE_PARMSET]		= 0x01,
	[MODE_MONITOR]		= 0x02,
	[MODE_CAPTURE]		= 0x03,
	[MODE_UNKNOWN]		= 0xff,
};

/* M5MOLS status */
static u8 m5mols_reg_status[] = {
	[STATUS_SYSINIT]	= 0x00,
	[STATUS_PARMSET]	= 0x01,
	[STATUS_MONITOR]	= 0x02,
	[STATUS_AUTO_FOCUS]	= 0x03,
	[STATUS_FACE_DETECTION]	= 0x04,
	[STATUS_DUAL_CAPTURE]	= 0x05,
	[STATUS_SINGLE_CAPTURE]	= 0x06,
	[STATUS_PREVIEW]	= 0x07,
	[STATUS_UNKNOWN]	= 0xff,
};

/* M5MOLS regulator consumer names */
/* The DEFAULT names of power are referenced with M5MO datasheet. */
static struct regulator_bulk_data supplies[] = {
	{
		/* core power - 1.2v, generally at the M5MOLS */
		.supply		= "core",
	}, {
		.supply		= "dig_18",	/* digital power 1 - 1.8v */
	}, {
		.supply		= "d_sensor",	/* sensor power 1 - 1.8v */
	}, {
		.supply		= "dig_28",	/* digital power 2 - 2.8v */
	}, {
		.supply		= "a_sensor",	/* analog power */
	}, {
		.supply		= "dig_12",	/* digital power 3 - 1.2v */
	},
};

/* M5MOLS default format (codes, sizes, preset values) */
static struct v4l2_mbus_framefmt default_fmt[M5MOLS_RES_MAX] = {
	[M5MOLS_RES_MON] = {
		.width		= DEFAULT_SENSOR_WIDTH,
		.height		= DEFAULT_SENSOR_HEIGHT,
		.code		= V4L2_MBUS_FMT_YUYV8_2X8,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	[M5MOLS_RES_CAPTURE] = {
		.width		= 1920,
		.height		= 1080,
		.code		= V4L2_MBUS_FMT_JPEG_1X8,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

#define SIZE_DEFAULT_FFMT	ARRAY_SIZE(default_fmt)

static const struct m5mols_format m5mols_formats[] = {
	[M5MOLS_RES_MON] = {
		.code		= V4L2_MBUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	[M5MOLS_RES_CAPTURE] = {
		.code		= V4L2_MBUS_FMT_JPEG_1X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

static const struct m5mols_resolution m5mols_resolutions[] = {
	/* monitor size */
	{ 0x01, M5MOLS_RES_MON, 128, 96 },	/* SUB-QCIF */
	{ 0x03, M5MOLS_RES_MON, 160, 120 },	/* QQVGA */
	{ 0x05, M5MOLS_RES_MON, 176, 144 },	/* QCIF */
	{ 0x06, M5MOLS_RES_MON, 176, 176 },	/* 176*176 */
	{ 0x08, M5MOLS_RES_MON, 240, 320 },	/* 1 QVGA */
	{ 0x09, M5MOLS_RES_MON, 320, 240 },	/* QVGA */
	{ 0x0c, M5MOLS_RES_MON, 240, 400 },	/* l WQVGA */
	{ 0x0d, M5MOLS_RES_MON, 400, 240 },	/* WQVGA */
	{ 0x0e, M5MOLS_RES_MON, 352, 288 },	/* CIF */
	{ 0x13, M5MOLS_RES_MON, 480, 360 },	/* 480*360 */
	{ 0x15, M5MOLS_RES_MON, 640, 360 },	/* qHD */
	{ 0x17, M5MOLS_RES_MON, 640, 480 },	/* VGA */
	{ 0x18, M5MOLS_RES_MON, 720, 480 },	/* 720x480 */
	{ 0x1a, M5MOLS_RES_MON, 800, 480 },	/* WVGA */
	{ 0x1f, M5MOLS_RES_MON, 800, 600 },	/* SVGA */
	{ 0x21, M5MOLS_RES_MON, 1280, 720 },	/* HD */
	{ 0x25, M5MOLS_RES_MON, 1920, 1080 },	/* 1080p */
	{ 0x29, M5MOLS_RES_MON, 3264, 2448 },	/* 8M (2.63fps@3264*2448) */
	{ 0x30, M5MOLS_RES_MON, 320, 240 },	/* 60fps for slow motion */
	{ 0x31, M5MOLS_RES_MON, 320, 240 },	/* 120fps for slow motion */
	{ 0x39, M5MOLS_RES_MON, 800, 602 },	/* AHS_MON debug */

	/* capture(JPEG or Bayer RAW or YUV Raw) size */
	{ 0x02, M5MOLS_RES_CAPTURE, 320, 240 },		/* QVGA */
	{ 0x04, M5MOLS_RES_CAPTURE, 400, 240 },		/* WQVGA */
	{ 0x07, M5MOLS_RES_CAPTURE, 480, 360 },		/* 480 x 360 */
	{ 0x08, M5MOLS_RES_CAPTURE, 640, 360 },		/* qHD */
	{ 0x09, M5MOLS_RES_CAPTURE, 640, 480 },		/* VGA */
	{ 0x0a, M5MOLS_RES_CAPTURE, 800, 480 },		/* WVGA */
	{ 0x10, M5MOLS_RES_CAPTURE, 1280, 720 },	/* HD */
	{ 0x14, M5MOLS_RES_CAPTURE, 1280,  960 },	/* 1M */
	{ 0x17, M5MOLS_RES_CAPTURE, 1600, 1200 },	/* 2M */
	{ 0x19, M5MOLS_RES_CAPTURE, 1920, 1080 },	/* Full-HD */
	{ 0x1a, M5MOLS_RES_CAPTURE, 2048, 1152 },	/* 3M */
	{ 0x1b, M5MOLS_RES_CAPTURE, 2048, 1536 },	/* 3M */
	{ 0x1c, M5MOLS_RES_CAPTURE, 2560, 1440 },	/* 4M */
	{ 0x1d, M5MOLS_RES_CAPTURE, 2560, 1536 },	/* 4M */
	{ 0x1f, M5MOLS_RES_CAPTURE, 2560, 1920 },	/* 5M */
	{ 0x21, M5MOLS_RES_CAPTURE, 3264, 1836 },	/* 6M */
	{ 0x22, M5MOLS_RES_CAPTURE, 3264, 1960 },	/* 6M */
	{ 0x25, M5MOLS_RES_CAPTURE, 3264, 2448 },	/* 8M */
#ifdef M5MO_THUMB_SUPPORT
	/* capture thumb(JPEG) size */
	{ 0x00, M5MOLS_RES_THUMB, 160, 90 },	/* 160 x 90 */
	{ 0x02, M5MOLS_RES_THUMB, 160, 120 },	/* QQVGA */
	{ 0x04, M5MOLS_RES_THUMB, 320, 240 },	/* QVGA */
	{ 0x06, M5MOLS_RES_THUMB, 400, 240 },	/* WQVGA */
	{ 0x09, M5MOLS_RES_THUMB, 480, 360 },	/* 480 x 360 */
	{ 0x0a, M5MOLS_RES_THUMB, 640, 360 },	/* qHD */
	{ 0x0b, M5MOLS_RES_THUMB, 640, 480 },	/* VGA */
	{ 0x0c, M5MOLS_RES_THUMB, 800, 480 },	/* WVGA */
#endif
};

/* M5MOLS default FPS */
static const struct v4l2_fract default_fps = {
	.numerator		= 1,
	.denominator		= M5MOLS_FPS_AUTO,
};

static u8 m5mols_reg_fps[] = {
	[M5MOLS_FPS_AUTO]	= 0x01,
	[M5MOLS_FPS_10]		= 0x05,
	[M5MOLS_FPS_12]		= 0x04,
	[M5MOLS_FPS_15]		= 0x03,
	[M5MOLS_FPS_20]		= 0x08,
	[M5MOLS_FPS_21]		= 0x09,
	[M5MOLS_FPS_22]		= 0x0a,
	[M5MOLS_FPS_23]		= 0x0b,
	[M5MOLS_FPS_24]		= 0x07,
	[M5MOLS_FPS_30]		= 0x02,
};

static u32 m5mols_swap_byte(u8 *data, enum m5mols_i2c_size size)
{
	if (size == I2C_8BIT)
		return *data;
	else if (size == I2C_16BIT)
		return be16_to_cpu(*((u16 *)data));
	else
		return be32_to_cpu(*((u32 *)data));
}

/*
 * m5mols_read_reg/m5mols_write_reg - handle sensor's I2C communications.
 *
 * The I2C command packet of M5MOLS is made up 3 kinds of I2C bytes(category,
 * command, bytes). Reference m5mols.h.
 *
 * The packet is needed 2, when M5MOLS is read through I2C.
 * The packet is needed 1, when M5MOLS is written through I2C.
 *
 * I2C packet common order(including both reading/writing)
 *   1st : size (data size + 4)
 *   2nd : READ/WRITE (R - 0x01, W - 0x02)
 *   3rd : Category
 *   4th : Command
 *
 * I2C packet order for READING operation
 *   5th : data real size for reading
 *   And, read another I2C packet again, until data size.
 *
 * I2C packet order for WRITING operation
 *   5th to 8th: an actual data to write
 */

#define M5MOLS_BYTE_READ	0x01
#define M5MOLS_BYTE_WRITE	0x02

int m5mols_read_reg(struct v4l2_subdev *sd,
		enum m5mols_i2c_size size,
		u8 category, u8 cmd, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[2];
	u8 wbuf[5], rbuf[I2C_MAX + 1];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	if (size != I2C_8BIT && size != I2C_16BIT && size != I2C_32BIT)
		return -EINVAL;

	/* 1st I2C operation for writing category & command. */
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 5;		/* 1(cmd size per bytes) + 4 */
	msg[0].buf = wbuf;
	wbuf[0] = 5;		/* same right above this */
	wbuf[1] = M5MOLS_BYTE_READ;
	wbuf[2] = category;
	wbuf[3] = cmd;
	wbuf[4] = size;

	/* 2nd I2C operation for reading data. */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = size + 1;
	msg[1].buf = rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		m5_err;
		dev_err(&client->dev, "failed READ[%d] at "
				"cat[%02x] cmd[%02x]\n",
				size, category, cmd);
		return ret;
	}

	*val = m5mols_swap_byte(&rbuf[1], size);

	usleep_range(15000, 20000);	/* must be for stabilization */

	return 0;
}

int m5mols_write_reg(struct v4l2_subdev *sd,
		enum m5mols_i2c_size size,
		u8 category, u8 cmd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *cdev = &client->dev;
	struct i2c_msg msg[1];
	u8 wbuf[I2C_MAX + 4];
	u32 *buf = (u32 *)&wbuf[4];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	if (size != I2C_8BIT && size != I2C_16BIT && size != I2C_32BIT) {
		dev_err(cdev, "Wrong data size\n");
		return -EINVAL;
	}

	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = size + 4;
	msg->buf = wbuf;
	wbuf[0] = size + 4;
	wbuf[1] = M5MOLS_BYTE_WRITE;
	wbuf[2] = category;
	wbuf[3] = cmd;

	*buf = m5mols_swap_byte((u8 *)&val, size);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
		m5_err;
		dev_err(&client->dev, "failed WRITE[%d] at "
				"cat[%02x] cmd[%02x], ret %d\n",
				size, msg->buf[2], msg->buf[3], ret);
		return ret;
	}

	usleep_range(15000, 20000);	/* must be for stabilization */

	return 0;
}

int m5mols_check_busy(struct v4l2_subdev *sd, u8 category, u8 cmd, u32 value)
{
	u32 busy, i;
	int ret;

	for (i = 0; i < M5MOLS_I2C_CHECK_RETRY; i++) {
		ret = m5mols_read_reg(sd, I2C_8BIT, category, cmd, &busy);
		if (ret < 0)
			return ret;

		if (busy == value)	/* bingo */
			return 0;

		/* must be for stabilization */
		usleep_range(10000, 10000);
	}
	return -EBUSY;
}

/*
 * m5mols_set_mode - change and set mode of M5MOLS.
 *
 * This driver supports now only 3 modes(System, Monitor, Parameter).
 */
int m5mols_set_mode(struct v4l2_subdev *sd, enum m5mols_mode mode)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *cdev = &client->dev;
	const char *m5mols_str_mode[] = {
		"System initialization",
		"Parameter setting",
		"Monitor setting",
		"Capture setting",
		"Unknown",
	};
	int ret = 0;

	if (mode < MODE_SYSINIT || mode > MODE_UNKNOWN)
		return -EINVAL;

	ret = i2c_w8_system(sd, CAT0_SYSMODE, m5mols_reg_mode[mode]);
	if (!ret) {
		/* bug detect, capture status is  not 0x3 but 0x6 */
		if (mode == MODE_CAPTURE)
			mode = STATUS_SINGLE_CAPTURE;
		ret = m5mols_check_busy(sd, CAT_SYSTEM, CAT0_STATUS,
				m5mols_reg_status[mode]);
		if (ret)
			m5_err;
	}
	if (ret < 0)
		return ret;

	info->mode = m5mols_reg_mode[mode];
	dev_dbg(cdev, " mode: %s\n", m5mols_str_mode[mode]);

	return ret;
}

/*
 * m5mols_get_status - get status of M5MOLS.
 */
enum m5mols_status m5mols_get_status(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *cdev = &client->dev;
	const char *m5mols_str_status[] = {
		"System initialization",
		"Parameter setting",
		"Monitor setting",
		"Auto Focus",
		"Face Detection",
		"Multi/Dual Capture",
		"Single Capture",
		"Preview (Data transfer)", /* It means recording, not preview. */
		"Unknown",
	};
	u32 reg;
	int ret = 0;

	ret = i2c_r8_system(sd, CAT0_STATUS, &reg);
	if (ret)
		return ret;

	if (reg < STATUS_SYSINIT || reg >= STATUS_UNKNOWN)
		return -EINVAL;

	info->status = m5mols_reg_status[reg];
	dev_dbg(cdev, " status: %s\n", m5mols_str_status[reg]);

	return ret;
}

/*
 * get_version - get M5MOLS sensor versions.
 */
static int get_version(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	union {
		struct m5mols_version	ver;
		u8			bytes[10];
	} value;
	int ret, i;

	for (i = CAT0_CUSTOMER_CODE; i <= CAT0_VERSION_AWB_L; i++) {
		ret = i2c_r8_system(sd, i, (u32 *)&value.bytes[i]);
		if (ret)
			return ret;
	}

	info->ver = value.ver;

	info->ver.fw = be16_to_cpu(info->ver.fw);
	info->ver.hw = be16_to_cpu(info->ver.hw);
	info->ver.parm = be16_to_cpu(info->ver.parm);
	info->ver.awb = be16_to_cpu(info->ver.awb);

	return ret;
}

static void m5mols_show_version(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	struct m5mols_info *info = to_m5mols(sd);

	dev_info(dev, "customer code\t0x%02x\n", info->ver.ctm_code);
	dev_info(dev, "project code\t0x%02x\n", info->ver.pj_code);
	dev_info(dev, "firmware version\t0x%04x\n", info->ver.fw);
	dev_info(dev, "hardware version\t0x%04x\n", info->ver.hw);
	dev_info(dev, "parameter version\t0x%04x\n", info->ver.parm);
	dev_info(dev, "AWB version\t0x%04x\n", info->ver.awb);
}

/*
 * get_res_preset - find out M5MOLS register value from requested resolution.
 *
 * @width: requested width
 * @height: requested height
 * @type: requested type of each modes. It supports only monitor mode now.
 */
static int get_res_preset(struct v4l2_subdev *sd, u16 width, u16 height,
		enum m5mols_res_type type)
{
	struct m5mols_info *info = to_m5mols(sd);
	int i;

	for (i = 0; i < ARRAY_SIZE(m5mols_resolutions); i++) {
		if ((m5mols_resolutions[i].type == type) &&
			(m5mols_resolutions[i].width == width) &&
			(m5mols_resolutions[i].height == height))
			break;
	}

	if (i >= ARRAY_SIZE(m5mols_resolutions)) {
		v4l2msg("no matching resolution\n");
		return -EINVAL;
	}

	return m5mols_resolutions[i].value;
}

/*
 * get_fps - calc & check FPS from v4l2_captureparm, if FPS is adequate, set.
 *
 * In M5MOLS case, the denominator means FPS. The each value of numerator and
 * denominator should not be minus. If numerator is 0, it sets AUTO FPS. If
 * numerator is not 1, it recalculates denominator. After it checks, the
 * denominator is set to timeperframe.denominator, and used by FPS.
 */
static int get_fps(struct v4l2_subdev *sd,
		struct v4l2_captureparm *parm)
{
	int numerator = parm->timeperframe.numerator;
	int denominator = parm->timeperframe.denominator;

	/* The denominator should be +, except 0. The numerator shoud be +. */
	if (numerator < 0 || denominator <= 0)
		return -EINVAL;

	/* The numerator is 0, return auto fps. */
	if (numerator == 0) {
		parm->timeperframe.denominator = M5MOLS_FPS_AUTO;
		return 0;
	}

	/* calc FPS(not time per frame) per 1 numerator */
	denominator = denominator / numerator;

	if (denominator < M5MOLS_FPS_AUTO || denominator > M5MOLS_FPS_MAX)
		return -EINVAL;

	if (!m5mols_reg_fps[denominator])
		return -EINVAL;

	return 0;
}

/*
 * to_code - return pixelcode of M5MOLS according to resolution type.
 */
static enum v4l2_mbus_pixelcode to_code(enum m5mols_res_type res_type)
{
	return m5mols_formats[res_type].code;
}

/*
 * to_res_type - return resolution type of M5MOLS according to pixelcode.
 */
static enum m5mols_res_type to_res_type(struct v4l2_subdev *sd,
		enum v4l2_mbus_pixelcode code)
{
	int i = ARRAY_SIZE(m5mols_formats);

	while (i--)
		if (code == m5mols_formats[i].code)
			break;
	if (i < 0)
		return M5MOLS_RES_MAX;

	if (code == m5mols_formats[M5MOLS_RES_MON].code)
		return M5MOLS_RES_MON;
	else
		return M5MOLS_RES_CAPTURE;
}

static int m5mols_g_mbus_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *ffmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	enum m5mols_res_type res_type;

	res_type = to_res_type(sd, ffmt->code);
	if (res_type == M5MOLS_RES_MAX)
		return -EINVAL;

	*ffmt = info->fmt[res_type];
	info->code = ffmt->code;

	return 0;
}

static int m5mols_into_monitor(struct v4l2_subdev *sd, int res_size)
{
	int ret;

	ret = m5mols_set_mode(sd, MODE_PARMSET);
	if (!ret)
		ret = i2c_w8_param(sd, CAT1_MONITOR_SIZE, (u8)res_size);
	if (!ret)
		ret = m5mols_set_mode(sd, MODE_PARMSET);

	return ret;
}

static int m5mols_into_capture(struct v4l2_subdev *sd, int res_size)
{
	struct m5mols_info *info = to_m5mols(sd);
	u32 reg;
	int ret, timeout = 1;
	u32 temp = 0;

	info->captured = false;

	/*
	 * The sequence of preparing Capture mode.
	 * 1. Clear Interrupt bit (for dummy)
	 * 2. Enable Capture bit at Interrupt
	 * 3. Lock AE/AWB
	 * 4. Enter Still Capture mode
	 */

	ret = m5mols_set_mode(sd, MODE_MONITOR);
	if (!ret)
		/* FIXME: setting capture exposure at the middle of a amount. */
		ret = i2c_w16_ae(sd, CAT3_MANUAL_GAIN_CAP, 0x90);
	if (!ret)
		ret = m5mols_set_ae_lock(info, true);
	if (!ret)
		ret = m5mols_set_awb_lock(info, true);
	if (!ret)
		ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
	if (!ret)
		ret = i2c_w8_system(sd, CAT0_INT_ENABLE, 1 << INT_BIT_CAPTURE);
	if (!ret)
		ret = m5mols_set_mode(sd, MODE_CAPTURE);
	if (!ret)
		timeout = wait_event_interruptible_timeout(info->cap_wait,
				info->captured, msecs_to_jiffies(2000));

	/* disable all interrupt & clear interrupt */
	ret = i2c_w8_system(sd, CAT0_INT_ENABLE, 0x0);
	if (!ret)
		ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
	if (ret)
		return -EPERM;

	/* If all timeout exhausted, return error. */
	if (!timeout)
		return -ETIMEDOUT;

	ret = i2c_r32_capt_ctrl(sd, CATC_CAP_IMAGE_SIZE, &temp);
	info->captured = false;
	return ret;
}

static int m5mols_s_mbus_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *ffmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	enum m5mols_res_type res_type;
	int size;
	int ret = -EINVAL;

	res_type = to_res_type(sd, ffmt->code);
	if (res_type == M5MOLS_RES_MAX)
		return -EINVAL;

	/* If user set portrait for preview, it is substitued width width height
	 * unless get_res_preset will fail that M5MOLS did not support
	 * reverse WVGA */
	if (ffmt->width < ffmt->height) {
		int temp;
		temp  = ffmt->width;
		ffmt->width = ffmt->height;
		ffmt->height = temp;
	}
	size = get_res_preset(sd, ffmt->width, ffmt->height, res_type);
	if (size < 0)
		return -EINVAL;

	if (ffmt->code == m5mols_formats[M5MOLS_RES_MON].code)
		ret = m5mols_into_monitor(sd, size);
	else
		ret = m5mols_into_capture(sd, 0);

	info->fmt[res_type]		= default_fmt[res_type];
	info->fmt[res_type].width	= ffmt->width;
	info->fmt[res_type].height	= ffmt->height;

	*ffmt = info->fmt[res_type];
	info->code = ffmt->code;

	return ret;
}

static int m5mols_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
			      enum v4l2_mbus_pixelcode *code)
{
	if (!code || index >= ARRAY_SIZE(m5mols_formats))
		return -EINVAL;

	*code = m5mols_formats[index].code;

	return 0;
}

static int m5mols_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_captureparm *cp = &parms->parm.capture;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe = info->tpf;

	return 0;
}

static int m5mols_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_captureparm *cp = &parms->parm.capture;
	int ret = -EINVAL;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	ret = m5mols_set_mode_backup(sd, MODE_PARMSET);
	if (!ret)
		ret = get_fps(sd, cp);	/* set right FPS to denominator. */
	if (!ret)
		ret = i2c_w8_param(sd, CAT1_MONITOR_FPS,
				m5mols_reg_fps[cp->timeperframe.denominator]);
	if (!ret)
		ret = m5mols_set_mode_restore(sd);
	if (!ret) {
		cp->capability = V4L2_CAP_TIMEPERFRAME;
		info->tpf = cp->timeperframe;
	}

	v4l2msg("denominator: %d / numerator: %d.\n",
		cp->timeperframe.denominator, cp->timeperframe.numerator);

	return ret;
}

static int m5mols_get_info_capture(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct m5mols_exif *exif = &info->cap.exif;
	int denominator, numerator;
	int ret = 0;

	ret = i2c_r32_exif(sd, CAT7_INFO_EXPTIME_NU, &numerator);
	if (!ret)
		ret = i2c_r32_exif(sd, CAT7_INFO_EXPTIME_DE, &denominator);
	if (!ret)
		exif->exposure_time = (u32)(numerator / denominator);
	if (ret)
		return ret;

	ret = i2c_r32_exif(sd, CAT7_INFO_TV_NU, &numerator);
	if (!ret)
		ret = i2c_r32_exif(sd, CAT7_INFO_TV_DE, &denominator);
	if (!ret)
		exif->shutter_speed = (u32)(numerator / denominator);
	if (ret)
		return ret;

	ret = i2c_r32_exif(sd, CAT7_INFO_AV_NU, &numerator);
	if (!ret)
		ret = i2c_r32_exif(sd, CAT7_INFO_AV_DE, &denominator);
	if (!ret)
		exif->aperture = (u32)(numerator / denominator);
	if (ret)
		return ret;

	ret = i2c_r32_exif(sd, CAT7_INFO_BV_NU, &numerator);
	if (!ret)
		ret = i2c_r32_exif(sd, CAT7_INFO_BV_DE, &denominator);
	if (!ret)
		exif->brightness = (u32)(numerator / denominator);
	if (ret)
		return ret;

	ret = i2c_r32_exif(sd, CAT7_INFO_EBV_NU, &numerator);
	if (!ret)
		ret = i2c_r32_exif(sd, CAT7_INFO_EBV_DE, &denominator);
	if (!ret)
		exif->exposure_bias = (u32)(numerator / denominator);
	if (ret)
		return ret;

	ret = i2c_r16_exif(sd, CAT7_INFO_ISO, (u32 *)&exif->iso_speed);
	if (!ret)
		ret = i2c_r16_exif(sd, CAT7_INFO_FLASH, (u32 *)&exif->flash);
	if (!ret)
		ret = i2c_r16_exif(sd, CAT7_INFO_SDR, (u32 *)&exif->sdr);
	if (!ret)
		ret = i2c_r16_exif(sd, CAT7_INFO_QVAL, (u32 *)&exif->qval);
	if (ret)
		return ret;
	if (!ret)
		ret = i2c_r32_capt_ctrl(sd, CATC_CAP_IMAGE_SIZE,
				&info->cap.main);
	if (!ret)
		ret = i2c_r32_capt_ctrl(sd, CATC_CAP_THUMB_SIZE,
				&info->cap.thumb);

	info->cap.total = info->cap.main + info->cap.thumb;

	v4l2_info(sd, "%s: capture total size %d\n", __func__, info->cap.total);
	v4l2_info(sd, "%s: capture main size %d\n", __func__, info->cap.main);
	v4l2_info(sd, "%s: capture thumb size %d\n", __func__, info->cap.thumb);
	v4l2_info(sd, "%s: exposure_time %d\n", __func__, exif->exposure_time);
	v4l2_info(sd, "%s: shutter_speed %d\n", __func__, exif->shutter_speed);
	v4l2_info(sd, "%s: aperture %d\n", __func__, exif->aperture);
	v4l2_info(sd, "%s: brightness %d\n", __func__, exif->brightness);
	v4l2_info(sd, "%s: exposure_bias %d\n", __func__, exif->exposure_bias);
	v4l2_info(sd, "%s: iso_speed %d\n", __func__, exif->iso_speed);
	v4l2_info(sd, "%s: flash %d\n", __func__, exif->flash);
	v4l2_info(sd, "%s: sdr %d\n", __func__, exif->sdr);
	v4l2_info(sd, "%s: qval %d\n", __func__, exif->qval);

	return ret;
}

/* TODO: not verified. */
static int m5mols_start_capture(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	u32 reg, size;
	int ret, timeout;

	u8 reg_capt_fmt[] = {
		0x10,	/* JPEG with header + Thumbnail JPEG(YUV422@QVGA) */
	}; /* YUV422, JPEG(422), JPEG(420) */
	info->captured = false;

	size = get_res_preset(sd,
			info->fmt[M5MOLS_RES_CAPTURE].width,
			info->fmt[M5MOLS_RES_CAPTURE].height,
			M5MOLS_RES_CAPTURE);
	if (size < 0)
		return -EINVAL;
	ret = 0;
	/*
	 * The sequence of Starting Capture mode.
	 * 1. Select capture Single or Multi
	 * 2. Select format (YUV422, JPEG(YUV420, YUV422))
	 * 3. Set image size preset of Capture
	 * 4. Read Interrupt bit (for dummy)
	 * 5. Enable Capture bit at Interrupt
	 * 6. Start Capture
	 * 7. Check interrupt and register value
	 * 8. Get Image & Thumb size
	 */
	ret = i2c_w8_capt_ctrl(sd, CATC_CAP_SEL_FRAME, true);	/* single capture */
	if (!ret)
		ret = i2c_w8_capt_parm(sd, CATB_YUVOUT_MAIN, reg_capt_fmt[0]);
	if (!ret)
		ret = i2c_w8_capt_parm(sd, CATB_MAIN_IMAGE_SIZE, size);
	if (!ret)
		ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
	if (!ret)
		ret = i2c_w8_system(sd, CAT0_INT_ENABLE, 1 << INT_BIT_CAPTURE);
	if (!ret)
		ret = i2c_w8_capt_ctrl(sd, CATC_CAP_START, true);
	if (!ret) {
		timeout = wait_event_interruptible_timeout(info->cap_wait,
				info->captured, msecs_to_jiffies(2000));

		if (info->captured) {
			ret = m5mols_get_info_capture(sd);

			if (!ret)
				v4l2_subdev_notify(sd, info->cap.total, NULL);
			else
				return ret;
		}

		/* disable all interrupt & clear interrupt */
		ret = i2c_w8_system(sd, CAT0_INT_ENABLE, 0x0);
		if (!ret)
			ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
		if (ret)
			return -EPERM;

		/* If all timeout exhausted, return error. */
		if (!timeout)
			return -ETIMEDOUT;

		info->captured = false;

		ret = 0;
	}

	/* TODO: complete capture. */

	return ret;
}

static int m5mols_start_monitor(struct v4l2_subdev *sd)
{
	return m5mols_set_mode(sd, MODE_MONITOR);
}

static int m5mols_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct m5mols_info *info = to_m5mols(sd);

	if (enable) {
		if (info->code == to_code(M5MOLS_RES_MON)) {
			v4l2_info(sd, "%s : monitor mode\n", __func__);
			return m5mols_start_monitor(sd);
		}
		if (info->code == to_code(M5MOLS_RES_CAPTURE)) {
			v4l2_info(sd, "%s : capture mode\n", __func__);
			return  m5mols_start_capture(sd);
		}
		return -EINVAL;
	} else {
		if (is_streaming(sd))
			return m5mols_set_mode(sd, MODE_PARMSET);
		return -EINVAL;
	}
}

static const struct v4l2_subdev_video_ops m5mols_video_ops = {
	.g_mbus_fmt		= m5mols_g_mbus_fmt,
	.s_mbus_fmt		= m5mols_s_mbus_fmt,
	.enum_mbus_fmt		= m5mols_enum_mbus_fmt,
	.g_parm			= m5mols_g_parm,
	.s_parm			= m5mols_s_parm,
	.s_stream		= m5mols_s_stream,
};

static int m5mols_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	int ret;

	ret = m5mols_set_mode_backup(sd, MODE_PARMSET);
	if (!ret)
		ret = m5mols_set_ctrl(ctrl);
	if (!ret)
		ret = m5mols_set_mode_restore(sd);

	return ret;
}

static int m5mols_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct m5mols_info *info = to_m5mols(sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAM_JPEG_ENCODEDSIZE:
		ctrl->cur.val = info->cap.total;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct v4l2_ctrl_ops m5mols_ctrl_ops = {
	.s_ctrl = m5mols_s_ctrl,
	.g_volatile_ctrl = m5mols_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config ctrl_private[] = {
	{
		.ops = &m5mols_ctrl_ops,
		.id = V4L2_CID_CAM_JPEG_MEMSIZE,
		.name = "Jpeg memory size",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = M5MO_JPEG_MEMSIZE,
		.step = 1,
		.min = 0,
		.def = M5MO_JPEG_MEMSIZE,
		.is_private = 1,
	}, {
		.ops = &m5mols_ctrl_ops,
		.id = V4L2_CID_CAM_JPEG_ENCODEDSIZE,
		.name = "Jpeg encoded size",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = M5MO_JPEG_MEMSIZE,
		.step = 1,
		.min = 0,
		.def = 0,
		.is_private = 1,
		.is_volatile = 1,
	},
};
/*
 * m5mols_sensor_power - handle sensor power up/down.
 *
 * @enable: If it is true, power up. If is not, power down.
 */
static int m5mols_sensor_power(struct m5mols_info *info, bool enable)
{
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int ret;

	if (enable) {
		if (is_powerup(sd))
			return 0;

		/* power-on additional power */
		if (info->set_power) {
			ret = info->set_power(&c->dev, 1);
			if (ret)
				return ret;
		}

		ret = regulator_bulk_enable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			return ret;

		gpio_set_value(info->pdata->gpio_rst, info->pdata->enable_rst);
		usleep_range(1000, 1000);

		info->power = true;
	} else {
		if (!is_powerup(sd))
			return 0;

		ret = regulator_bulk_disable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			return ret;

		/* power-off additional power */
		if (info->set_power) {
			ret = info->set_power(&c->dev, 0);
			if (ret)
				return ret;
		}

		info->power = false;

		gpio_set_value(info->pdata->gpio_rst, !info->pdata->enable_rst);
		usleep_range(1000, 1000);
	}

	return ret;
}

static void m5mols_irq_work(struct work_struct *work)
{
	struct m5mols_info *info = container_of(work, struct m5mols_info, work);
	struct v4l2_subdev *sd = &info->sd;
	u32 reg;
	int ret;
	if (is_powerup(sd)) {
		ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);
		if (!ret) {
			switch (reg & 0x0f) {
			case (1 << INT_BIT_AF):
				/* Except returning zero at just that upper
				 * statments, not entering in this parenthesis.
				 * The return value is below:
				 * 0x0 : AF Fail
				 * 0x2 : AF Success
				 * 0x4 : Idle Status
				 * 0x5 : Busy Status */
				ret = i2c_r8_lens(sd, CATA_AF_STATUS, &reg);
				if (!ret && (reg == 0x02))
					info->is_focus = true;
				else
					info->is_focus = false;
				printk("%s = AF %02x, focus %d\n",
						__func__, reg, info->is_focus);
				break;
			case (1 << INT_BIT_CAPTURE):
				printk("%s = CAPTURE\n", __func__);
				if (!info->captured) {
					wake_up_interruptible(&info->cap_wait);
					info->captured = true;
				}
				break;
			case (1 << INT_BIT_ZOOM):
			case (1 << INT_BIT_FRAME_SYNC):
			case (1 << INT_BIT_FD):
			case (1 << INT_BIT_LENS_INIT):
			case (1 << INT_BIT_SOUND):
				printk("%s = Nothing : 0x%08x\n", __func__, reg);
				break;
			case (1 << INT_BIT_MODE):
			default:
				break;
			}
		}
	}
}

static irqreturn_t m5mols_irq_handler(int irq, void *data)
{
	struct v4l2_subdev *sd = data;
	struct m5mols_info *info = to_m5mols(sd);

	v4l2_info(sd, "%s\n", __func__);

	schedule_work(&info->work);

	return IRQ_HANDLED;
}

/*
 * m5mols_sensor_armboot - booting M5MOLS internal ARM core-controller.
 *
 * It makes to ready M5MOLS for I2C & MIPI interface. After it's powered up,
 * it activates if it gets armboot command for I2C interface. After getting
 * cmd, it must wait about least 500ms referenced by M5MOLS datasheet.
 */
static int m5mols_sensor_armboot(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct m5mols_info *info = to_m5mols(sd);
	static u8 m5mols_mipi_value = 0x02;
	u32 reg;
	int ret;

	/* 1. ARM booting */
	ret = i2c_w8_flash(sd, CATC_CAM_START, true);
	if (ret < 0)
		return ret;

	msleep(500);
	dev_dbg(&client->dev, "Success ARM Booting\n");

	/* after ARM booting, the M5MOLS state changed Parameter mode. */
	info->mode = MODE_PARMSET;

	ret = i2c_r8_system(sd, CAT0_INT_FACTOR, &reg);		/* clear intterupt */
	if (!ret)
		ret = i2c_w8_system(sd, CAT0_INT_ENABLE, 0x0);	/* all disable */
	if (!ret)
		ret = get_version(sd);
	if (!ret)
		ret = i2c_w8_param(sd, CAT1_DATA_INTERFACE, m5mols_mipi_value);

	m5mols_show_version(sd);

	return ret;
}

/*
 * m5mols_init_controls - initialization using v4l2_ctrl.
 */
static int m5mols_init_controls(struct m5mols_info *info)
{
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 max_ex_mon;
	int ret;

	/* check minimum & maximum of M5MOLS controls */
	ret = i2c_r16_ae(sd, CAT3_MAX_GAIN_MON, (u32 *)&max_ex_mon);
	if (ret)
		return ret;

	/* set the controls using v4l2 control frameworks */
	v4l2_ctrl_handler_init(&info->handle, 9);

	info->colorfx = v4l2_ctrl_new_std_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_COLORFX,
			9, 1, V4L2_COLORFX_NONE);
	info->autoexposure = v4l2_ctrl_new_std_menu(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_EXPOSURE_AUTO,
			1, 0, V4L2_EXPOSURE_AUTO);
	info->exposure = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_EXPOSURE,
			0, max_ex_mon, 1, (int)max_ex_mon/2);
	info->autofocus = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_FOCUS_AUTO,
			0, 1, 1, 0);
	info->autowb = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_AUTO_WHITE_BALANCE,
			0, 1, 1, 1);
	info->saturation = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_SATURATION,
			0, 6, 1, 3);
	info->zoom = v4l2_ctrl_new_std(&info->handle,
			&m5mols_ctrl_ops, V4L2_CID_ZOOM_ABSOLUTE,
			0, 70, 1, 0);
	info->jpeg_size = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[0],
			NULL);
	info->encoded_size = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[1],
			NULL);

	sd->ctrl_handler = &info->handle;

	if (info->handle.error) {
		dev_err(&client->dev, "Failed to init controls, %d\n", ret);
		v4l2_ctrl_handler_free(&info->handle);
		return info->handle.error;
	}

	v4l2_ctrl_cluster(2, &info->autoexposure);
	/* If above ctrl value is not good image, so it is better that not set */
	v4l2_ctrl_handler_setup(&info->handle);

	return 0;
}

/*
 * m5mols_setup_default - set default size & fps in the monitor mode.
 */
static int m5mols_setup_default(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	int value;
	int ret = -EINVAL;

	value = get_res_preset(sd,
			default_fmt[M5MOLS_RES_MON].width,
			default_fmt[M5MOLS_RES_MON].height,
			M5MOLS_RES_MON);
	if (value >= 0)
		ret = i2c_w8_param(sd, CAT1_MONITOR_SIZE, (u8)value);
	if (!ret)
		ret = i2c_w8_param(sd, CAT1_MONITOR_FPS,
			m5mols_reg_fps[default_fps.denominator]);
	if (!ret)
		ret = m5mols_init_controls(info);
	if (!ret)
		ret = m5mols_set_ae_lock(info, false);
	if (!ret)
		ret = m5mols_set_awb_lock(info, false);
	if (!ret) {
		info->fmt[M5MOLS_RES_MON] = default_fmt[M5MOLS_RES_MON];
		info->tpf = default_fps;

		ret = 0;
	}

	return ret;
}

static int m5mols_s_power(struct v4l2_subdev *sd, int on)
{
	struct m5mols_info *info = to_m5mols(sd);
	int ret;

	if (on) {
		ret = m5mols_sensor_power(info, true);
		if (!ret)
			ret = m5mols_sensor_armboot(sd);
		if (!ret)
			ret = m5mols_setup_default(sd);
	} else {
		ret = m5mols_sensor_power(info, false);
	}

	return ret;
}

static int m5mols_log_status(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);

	v4l2_ctrl_handler_log_status(&info->handle, sd->name);

	return 0;
}

static const struct v4l2_subdev_core_ops m5mols_core_ops = {
	.s_power		= m5mols_s_power,
	.g_ctrl			= v4l2_subdev_g_ctrl,
	.s_ctrl			= v4l2_subdev_s_ctrl,
	.queryctrl		= v4l2_subdev_queryctrl,
	.querymenu		= v4l2_subdev_querymenu,
	.g_ext_ctrls		= v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls		= v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls		= v4l2_subdev_s_ext_ctrls,
	.log_status		= m5mols_log_status,
};

/**
 * __find_restype - Lookup M-5MOLS resolution type according to pixel code
 * @code: pixel code
 */
static enum m5mols_restype __find_restype(enum v4l2_mbus_pixelcode code)
{
	enum m5mols_restype type = M5MOLS_RESTYPE_MONITOR;

	do {
		if (code == default_fmt[type].code)
			return type;
	} while (type++ != SIZE_DEFAULT_FFMT);

	return 0;
}

/**
 * __find_resolution - Lookup preset and type of M-5MOLS's resolution
 * @mf: pixel format to find/negotiate the resolution preset for
 * @type: M-5MOLS resolution type
 * @resolution:	M-5MOLS resolution preset register value
 *
 * Find nearest resolution matching resolution preset and adjust mf
 * to supported values.
 */
static int __find_resolution(struct v4l2_subdev *sd,
			     struct v4l2_mbus_framefmt *mf,
			     enum m5mols_restype *type,
			     u32 *resolution)
{
	const struct m5mols_resolution *fsize = &m5mols_resolutions[0];
	const struct m5mols_resolution *match = NULL;
	enum m5mols_restype stype = __find_restype(mf->code);
	int i = ARRAY_SIZE(m5mols_resolutions);
	unsigned int min_err = ~0;

	while (i--) {
		int err;
		if (stype == fsize->type) {
			err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);

			if (err < min_err) {
				min_err = err;
				match = fsize;
			}
		}
		fsize++;
	}
	if (match) {
		mf->width  = match->width;
		mf->height = match->height;
		*resolution = match->value;
		*type = stype;
		return 0;
	}

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *__find_format(struct m5mols_info *info,
				struct v4l2_subdev_fh *fh,
				enum v4l2_subdev_format_whence which,
				enum m5mols_restype type)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;

	return &info->fmt[type];
}

static int m5mols_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_mbus_framefmt *format;

	if (fmt->pad != 0)
		return -EINVAL;

	format = __find_format(info, fh, fmt->which, info->res_type);
	if (!format)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

static int m5mols_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	struct v4l2_mbus_framefmt *sfmt;
	enum m5mols_restype type;
	u32 resolution = 0;
	int ret;

	if (fmt->pad != 0)
		return -EINVAL;

	ret = __find_resolution(sd, format, &type, &resolution);
	if (ret < 0)
		return ret;

	sfmt = __find_format(info, fh, fmt->which, type);
	if (!sfmt)
		return 0;

	sfmt		= &default_fmt[type];
	sfmt->width	= format->width;
	sfmt->height	= format->height;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		info->resolution = resolution;
		info->code = format->code;
		info->res_type = type;
	}

	return 0;
}

static int m5mols_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (!code || code->index >= SIZE_DEFAULT_FFMT)
		return -EINVAL;

	code->code = default_fmt[code->index].code;

	return 0;
}

static struct v4l2_subdev_pad_ops m5mols_pad_ops = {
	.enum_mbus_code	= m5mols_enum_mbus_code,
	.get_fmt	= m5mols_get_fmt,
	.set_fmt	= m5mols_set_fmt,
};

static const struct v4l2_subdev_ops m5mols_ops = {
	.core	= &m5mols_core_ops,
	.pad	= &m5mols_pad_ops,
	.video	= &m5mols_video_ops,
};

static int m5mols_link_setup(struct media_entity *entity,
			    const struct media_pad *local,
			    const struct media_pad *remote, u32 flags)
{
	printk("%s\n", __func__);
	return 0;
}
static const struct media_entity_operations m5mols_media_ops = {
	.link_setup = m5mols_link_setup,
};

static int m5mols_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = 0;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = m5mols_formats[M5MOLS_RES_MON].code;
	format.format.width = DEFAULT_SENSOR_WIDTH;
	format.format.height = DEFAULT_SENSOR_HEIGHT;

	m5mols_set_fmt(sd, fh, &format);

	return 0;
}

static int m5mols_subdev_close(struct v4l2_subdev *sd,
			      struct v4l2_subdev_fh *fh)
{
	v4l2_dbg(1, m5mols_debug, sd, "%s", __func__);
	return 0;
}

static int m5mols_subdev_registered(struct v4l2_subdev *sd)
{
	v4l2_dbg(1, m5mols_debug, sd, "%s", __func__);
	return 0;
}

static void m5mols_subdev_unregistered(struct v4l2_subdev *sd)
{
	v4l2_dbg(1, m5mols_debug, sd, "%s", __func__);
}

static const struct v4l2_subdev_internal_ops m5mols_v4l2_internal_ops = {
	.open = m5mols_init_formats,
	.close = m5mols_subdev_close,
	.registered = m5mols_subdev_registered,
	.unregistered = m5mols_subdev_unregistered,
};

static int m5mols_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	const struct m5mols_platform_data *pdata =
		client->dev.platform_data;
	struct m5mols_info *info;
	struct v4l2_subdev *sd;
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->gpio_rst)) {
		dev_err(&client->dev, "No valid nRST gpio pin.\n");
		return -EINVAL;
	}

	if (!pdata->irq) {
		dev_err(&client->dev, "Interrupt not assigned.\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct m5mols_info), GFP_KERNEL);
	if (info == NULL) {
		dev_err(&client->dev, "Failed to allocate info\n");
		return -ENOMEM;
	}

	info->pdata	= pdata;
	if (info->pdata->set_power)	/* for additional power if needed. */
		info->set_power = pdata->set_power;

	if (info->pdata->irq) {
		INIT_WORK(&info->work, m5mols_irq_work);
		ret = request_irq(info->pdata->irq, m5mols_irq_handler,
				  IRQF_TRIGGER_RISING, MOD_NAME, &info->sd);
		if (ret) {
			dev_err(&client->dev, "Failed to request irq: %d\n", ret);
			return ret;
		}
	}

	ret = gpio_request(info->pdata->gpio_rst, "M5MOLS nRST");
	if (ret) {
		dev_err(&client->dev, "Failed to set gpio, %d\n", ret);
		goto out_gpio;
	}

	gpio_direction_output(info->pdata->gpio_rst, !info->pdata->enable_rst);

	ret = regulator_bulk_get(&client->dev, ARRAY_SIZE(supplies), supplies);
	if (ret) {
		dev_err(&client->dev, "Failed to get regulators, %d\n", ret);
		goto out_reg;
	}

	sd = &info->sd;
	strlcpy(sd->name, MOD_NAME, sizeof(sd->name));

	init_waitqueue_head(&info->cap_wait);

	v4l2_i2c_subdev_init(sd, client, &m5mols_ops);
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, 1, &info->pad, 0);
	if (ret < 0)
		goto out_reg;

	m5mols_init_formats(sd, NULL);

	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &m5mols_v4l2_internal_ops;
	sd->entity.ops = &m5mols_media_ops;

	info->res_type = M5MOLS_RESTYPE_MONITOR;

	v4l2_info(sd, "%s : m5mols driver probed success\n", __func__);

	return 0;

out_reg:
	regulator_bulk_free(ARRAY_SIZE(supplies), supplies);
out_gpio:
	gpio_free(info->pdata->gpio_rst);
	kfree(info);

	return ret;
}

static int m5mols_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct m5mols_info *info = to_m5mols(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->handle);
	free_irq(info->pdata->irq, sd);
	regulator_bulk_free(ARRAY_SIZE(supplies), supplies);
	gpio_free(info->pdata->gpio_rst);
	media_entity_cleanup(&sd->entity);
	kfree(info);

	return 0;
}

static const struct i2c_device_id m5mols_id[] = {
	{ MOD_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, m5mols_id);

static struct i2c_driver m5mols_i2c_driver = {
	.driver = {
		.name	= MOD_NAME,
	},
	.probe		= m5mols_probe,
	.remove		= m5mols_remove,
	.id_table	= m5mols_id,
};

static int __init m5mols_mod_init(void)
{
	return i2c_add_driver(&m5mols_i2c_driver);
}

static void __exit m5mols_mod_exit(void)
{
	i2c_del_driver(&m5mols_i2c_driver);
}

module_init(m5mols_mod_init);
module_exit(m5mols_mod_exit);

MODULE_AUTHOR("HeungJun Kim <riverful.kim@samsung.com>");
MODULE_AUTHOR("Dongsoo Kim <dongsoo45.kim@samsung.com>");
MODULE_DESCRIPTION("Fujitsu M5MOLS 8M Pixel camera sensor with ISP driver");
MODULE_LICENSE("GPL");
