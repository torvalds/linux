/*
 * Driver for M-5MOLS 8M Pixel camera sensor with ISP
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
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/module.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/i2c/m5mols.h>

#include "m5mols.h"
#include "m5mols_reg.h"

int m5mols_debug;
module_param(m5mols_debug, int, 0644);

#define MODULE_NAME		"M5MOLS"
#define M5MOLS_I2C_CHECK_RETRY	500

/* The regulator consumer names for external voltage regulators */
static struct regulator_bulk_data supplies[] = {
	{
		.supply = "core",	/* ARM core power, 1.2V */
	}, {
		.supply	= "dig_18",	/* digital power 1, 1.8V */
	}, {
		.supply	= "d_sensor",	/* sensor power 1, 1.8V */
	}, {
		.supply	= "dig_28",	/* digital power 2, 2.8V */
	}, {
		.supply	= "a_sensor",	/* analog power */
	}, {
		.supply	= "dig_12",	/* digital power 3, 1.2V */
	},
};

static struct v4l2_mbus_framefmt m5mols_default_ffmt[M5MOLS_RESTYPE_MAX] = {
	[M5MOLS_RESTYPE_MONITOR] = {
		.width		= 1920,
		.height		= 1080,
		.code		= MEDIA_BUS_FMT_VYUY8_2X8,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	[M5MOLS_RESTYPE_CAPTURE] = {
		.width		= 1920,
		.height		= 1080,
		.code		= MEDIA_BUS_FMT_JPEG_1X8,
		.field		= V4L2_FIELD_NONE,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};
#define SIZE_DEFAULT_FFMT	ARRAY_SIZE(m5mols_default_ffmt)

static const struct m5mols_resolution m5mols_reg_res[] = {
	{ 0x01, M5MOLS_RESTYPE_MONITOR, 128, 96 },	/* SUB-QCIF */
	{ 0x03, M5MOLS_RESTYPE_MONITOR, 160, 120 },	/* QQVGA */
	{ 0x05, M5MOLS_RESTYPE_MONITOR, 176, 144 },	/* QCIF */
	{ 0x06, M5MOLS_RESTYPE_MONITOR, 176, 176 },
	{ 0x08, M5MOLS_RESTYPE_MONITOR, 240, 320 },	/* QVGA */
	{ 0x09, M5MOLS_RESTYPE_MONITOR, 320, 240 },	/* QVGA */
	{ 0x0c, M5MOLS_RESTYPE_MONITOR, 240, 400 },	/* WQVGA */
	{ 0x0d, M5MOLS_RESTYPE_MONITOR, 400, 240 },	/* WQVGA */
	{ 0x0e, M5MOLS_RESTYPE_MONITOR, 352, 288 },	/* CIF */
	{ 0x13, M5MOLS_RESTYPE_MONITOR, 480, 360 },
	{ 0x15, M5MOLS_RESTYPE_MONITOR, 640, 360 },	/* qHD */
	{ 0x17, M5MOLS_RESTYPE_MONITOR, 640, 480 },	/* VGA */
	{ 0x18, M5MOLS_RESTYPE_MONITOR, 720, 480 },
	{ 0x1a, M5MOLS_RESTYPE_MONITOR, 800, 480 },	/* WVGA */
	{ 0x1f, M5MOLS_RESTYPE_MONITOR, 800, 600 },	/* SVGA */
	{ 0x21, M5MOLS_RESTYPE_MONITOR, 1280, 720 },	/* HD */
	{ 0x25, M5MOLS_RESTYPE_MONITOR, 1920, 1080 },	/* 1080p */
	{ 0x29, M5MOLS_RESTYPE_MONITOR, 3264, 2448 },	/* 2.63fps 8M */
	{ 0x39, M5MOLS_RESTYPE_MONITOR, 800, 602 },	/* AHS_MON debug */

	{ 0x02, M5MOLS_RESTYPE_CAPTURE, 320, 240 },	/* QVGA */
	{ 0x04, M5MOLS_RESTYPE_CAPTURE, 400, 240 },	/* WQVGA */
	{ 0x07, M5MOLS_RESTYPE_CAPTURE, 480, 360 },
	{ 0x08, M5MOLS_RESTYPE_CAPTURE, 640, 360 },	/* qHD */
	{ 0x09, M5MOLS_RESTYPE_CAPTURE, 640, 480 },	/* VGA */
	{ 0x0a, M5MOLS_RESTYPE_CAPTURE, 800, 480 },	/* WVGA */
	{ 0x10, M5MOLS_RESTYPE_CAPTURE, 1280, 720 },	/* HD */
	{ 0x14, M5MOLS_RESTYPE_CAPTURE, 1280, 960 },	/* 1M */
	{ 0x17, M5MOLS_RESTYPE_CAPTURE, 1600, 1200 },	/* 2M */
	{ 0x19, M5MOLS_RESTYPE_CAPTURE, 1920, 1080 },	/* Full-HD */
	{ 0x1a, M5MOLS_RESTYPE_CAPTURE, 2048, 1152 },	/* 3Mega */
	{ 0x1b, M5MOLS_RESTYPE_CAPTURE, 2048, 1536 },
	{ 0x1c, M5MOLS_RESTYPE_CAPTURE, 2560, 1440 },	/* 4Mega */
	{ 0x1d, M5MOLS_RESTYPE_CAPTURE, 2560, 1536 },
	{ 0x1f, M5MOLS_RESTYPE_CAPTURE, 2560, 1920 },	/* 5Mega */
	{ 0x21, M5MOLS_RESTYPE_CAPTURE, 3264, 1836 },	/* 6Mega */
	{ 0x22, M5MOLS_RESTYPE_CAPTURE, 3264, 1960 },
	{ 0x25, M5MOLS_RESTYPE_CAPTURE, 3264, 2448 },	/* 8Mega */
};

/**
 * m5mols_swap_byte - an byte array to integer conversion function
 * @data: byte array
 * @length: size in bytes of I2C packet defined in the M-5MOLS datasheet
 *
 * Convert I2C data byte array with performing any required byte
 * reordering to assure proper values for each data type, regardless
 * of the architecture endianness.
 */
static u32 m5mols_swap_byte(u8 *data, u8 length)
{
	if (length == 1)
		return *data;
	else if (length == 2)
		return be16_to_cpu(*((__be16 *)data));
	else
		return be32_to_cpu(*((__be32 *)data));
}

/**
 * m5mols_read -  I2C read function
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @size: desired size of I2C packet
 * @reg: combination of size, category and command for the I2C packet
 * @val: read value
 *
 * Returns 0 on success, or else negative errno.
 */
static int m5mols_read(struct v4l2_subdev *sd, u32 size, u32 reg, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct m5mols_info *info = to_m5mols(sd);
	u8 rbuf[M5MOLS_I2C_MAX_SIZE + 1];
	u8 category = I2C_CATEGORY(reg);
	u8 cmd = I2C_COMMAND(reg);
	struct i2c_msg msg[2];
	u8 wbuf[5];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 5;
	msg[0].buf = wbuf;
	wbuf[0] = 5;
	wbuf[1] = M5MOLS_BYTE_READ;
	wbuf[2] = category;
	wbuf[3] = cmd;
	wbuf[4] = size;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = size + 1;
	msg[1].buf = rbuf;

	/* minimum stabilization time */
	usleep_range(200, 300);

	ret = i2c_transfer(client->adapter, msg, 2);

	if (ret == 2) {
		*val = m5mols_swap_byte(&rbuf[1], size);
		return 0;
	}

	if (info->isp_ready)
		v4l2_err(sd, "read failed: size:%d cat:%02x cmd:%02x. %d\n",
			 size, category, cmd, ret);

	return ret < 0 ? ret : -EIO;
}

int m5mols_read_u8(struct v4l2_subdev *sd, u32 reg, u8 *val)
{
	u32 val_32;
	int ret;

	if (I2C_SIZE(reg) != 1) {
		v4l2_err(sd, "Wrong data size\n");
		return -EINVAL;
	}

	ret = m5mols_read(sd, I2C_SIZE(reg), reg, &val_32);
	if (ret)
		return ret;

	*val = (u8)val_32;
	return ret;
}

int m5mols_read_u16(struct v4l2_subdev *sd, u32 reg, u16 *val)
{
	u32 val_32;
	int ret;

	if (I2C_SIZE(reg) != 2) {
		v4l2_err(sd, "Wrong data size\n");
		return -EINVAL;
	}

	ret = m5mols_read(sd, I2C_SIZE(reg), reg, &val_32);
	if (ret)
		return ret;

	*val = (u16)val_32;
	return ret;
}

int m5mols_read_u32(struct v4l2_subdev *sd, u32 reg, u32 *val)
{
	if (I2C_SIZE(reg) != 4) {
		v4l2_err(sd, "Wrong data size\n");
		return -EINVAL;
	}

	return m5mols_read(sd, I2C_SIZE(reg), reg, val);
}

/**
 * m5mols_write - I2C command write function
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @reg: combination of size, category and command for the I2C packet
 * @val: value to write
 *
 * Returns 0 on success, or else negative errno.
 */
int m5mols_write(struct v4l2_subdev *sd, u32 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct m5mols_info *info = to_m5mols(sd);
	u8 wbuf[M5MOLS_I2C_MAX_SIZE + 4];
	u8 category = I2C_CATEGORY(reg);
	u8 cmd = I2C_COMMAND(reg);
	u8 size	= I2C_SIZE(reg);
	u32 *buf = (u32 *)&wbuf[4];
	struct i2c_msg msg[1];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	if (size != 1 && size != 2 && size != 4) {
		v4l2_err(sd, "Wrong data size\n");
		return -EINVAL;
	}

	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = (u16)size + 4;
	msg->buf = wbuf;
	wbuf[0] = size + 4;
	wbuf[1] = M5MOLS_BYTE_WRITE;
	wbuf[2] = category;
	wbuf[3] = cmd;

	*buf = m5mols_swap_byte((u8 *)&val, size);

	/* minimum stabilization time */
	usleep_range(200, 300);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret == 1)
		return 0;

	if (info->isp_ready)
		v4l2_err(sd, "write failed: cat:%02x cmd:%02x ret:%d\n",
			 category, cmd, ret);

	return ret < 0 ? ret : -EIO;
}

/**
 * m5mols_busy_wait - Busy waiting with I2C register polling
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @reg: the I2C_REG() address of an 8-bit status register to check
 * @value: expected status register value
 * @mask: bit mask for the read status register value
 * @timeout: timeout in miliseconds, or -1 for default timeout
 *
 * The @reg register value is ORed with @mask before comparing with @value.
 *
 * Return: 0 if the requested condition became true within less than
 *         @timeout ms, or else negative errno.
 */
int m5mols_busy_wait(struct v4l2_subdev *sd, u32 reg, u32 value, u32 mask,
		     int timeout)
{
	int ms = timeout < 0 ? M5MOLS_BUSY_WAIT_DEF_TIMEOUT : timeout;
	unsigned long end = jiffies + msecs_to_jiffies(ms);
	u8 status;

	do {
		int ret = m5mols_read_u8(sd, reg, &status);

		if (ret < 0 && !(mask & M5MOLS_I2C_RDY_WAIT_FL))
			return ret;
		if (!ret && (status & mask & 0xff) == (value & 0xff))
			return 0;
		usleep_range(100, 250);
	} while (ms > 0 && time_is_after_jiffies(end));

	return -EBUSY;
}

/**
 * m5mols_enable_interrupt - Clear interrupt pending bits and unmask interrupts
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @reg: combination of size, category and command for the I2C packet
 *
 * Before writing desired interrupt value the INT_FACTOR register should
 * be read to clear pending interrupts.
 */
int m5mols_enable_interrupt(struct v4l2_subdev *sd, u8 reg)
{
	struct m5mols_info *info = to_m5mols(sd);
	u8 mask = is_available_af(info) ? REG_INT_AF : 0;
	u8 dummy;
	int ret;

	ret = m5mols_read_u8(sd, SYSTEM_INT_FACTOR, &dummy);
	if (!ret)
		ret = m5mols_write(sd, SYSTEM_INT_ENABLE, reg & ~mask);
	return ret;
}

int m5mols_wait_interrupt(struct v4l2_subdev *sd, u8 irq_mask, u32 timeout)
{
	struct m5mols_info *info = to_m5mols(sd);

	int ret = wait_event_interruptible_timeout(info->irq_waitq,
				atomic_add_unless(&info->irq_done, -1, 0),
				msecs_to_jiffies(timeout));
	if (ret <= 0)
		return ret ? ret : -ETIMEDOUT;

	return m5mols_busy_wait(sd, SYSTEM_INT_FACTOR, irq_mask,
				M5MOLS_I2C_RDY_WAIT_FL | irq_mask, -1);
}

/**
 * m5mols_reg_mode - Write the mode and check busy status
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @mode: the required operation mode
 *
 * It always accompanies a little delay changing the M-5MOLS mode, so it is
 * needed checking current busy status to guarantee right mode.
 */
static int m5mols_reg_mode(struct v4l2_subdev *sd, u8 mode)
{
	int ret = m5mols_write(sd, SYSTEM_SYSMODE, mode);
	if (ret < 0)
		return ret;
	return m5mols_busy_wait(sd, SYSTEM_SYSMODE, mode, 0xff,
				M5MOLS_MODE_CHANGE_TIMEOUT);
}

/**
 * m5mols_set_mode - set the M-5MOLS controller mode
 * @info: M-5MOLS driver data structure
 * @mode: the required operation mode
 *
 * The commands of M-5MOLS are grouped into specific modes. Each functionality
 * can be guaranteed only when the sensor is operating in mode which a command
 * belongs to.
 */
int m5mols_set_mode(struct m5mols_info *info, u8 mode)
{
	struct v4l2_subdev *sd = &info->sd;
	int ret = -EINVAL;
	u8 reg;

	if (mode < REG_PARAMETER || mode > REG_CAPTURE)
		return ret;

	ret = m5mols_read_u8(sd, SYSTEM_SYSMODE, &reg);
	if (ret || reg == mode)
		return ret;

	switch (reg) {
	case REG_PARAMETER:
		ret = m5mols_reg_mode(sd, REG_MONITOR);
		if (mode == REG_MONITOR)
			break;
		if (!ret)
			ret = m5mols_reg_mode(sd, REG_CAPTURE);
		break;

	case REG_MONITOR:
		if (mode == REG_PARAMETER) {
			ret = m5mols_reg_mode(sd, REG_PARAMETER);
			break;
		}

		ret = m5mols_reg_mode(sd, REG_CAPTURE);
		break;

	case REG_CAPTURE:
		ret = m5mols_reg_mode(sd, REG_MONITOR);
		if (mode == REG_MONITOR)
			break;
		if (!ret)
			ret = m5mols_reg_mode(sd, REG_PARAMETER);
		break;

	default:
		v4l2_warn(sd, "Wrong mode: %d\n", mode);
	}

	if (!ret)
		info->mode = mode;

	return ret;
}

/**
 * m5mols_get_version - retrieve full revisions information of M-5MOLS
 * @sd: sub-device, as pointed by struct v4l2_subdev
 *
 * The version information includes revisions of hardware and firmware,
 * AutoFocus alghorithm version and the version string.
 */
static int m5mols_get_version(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct m5mols_version *ver = &info->ver;
	u8 *str = ver->str;
	int i;
	int ret;

	ret = m5mols_read_u8(sd, SYSTEM_VER_CUSTOMER, &ver->customer);
	if (!ret)
		ret = m5mols_read_u8(sd, SYSTEM_VER_PROJECT, &ver->project);
	if (!ret)
		ret = m5mols_read_u16(sd, SYSTEM_VER_FIRMWARE, &ver->fw);
	if (!ret)
		ret = m5mols_read_u16(sd, SYSTEM_VER_HARDWARE, &ver->hw);
	if (!ret)
		ret = m5mols_read_u16(sd, SYSTEM_VER_PARAMETER, &ver->param);
	if (!ret)
		ret = m5mols_read_u16(sd, SYSTEM_VER_AWB, &ver->awb);
	if (!ret)
		ret = m5mols_read_u8(sd, AF_VERSION, &ver->af);
	if (ret)
		return ret;

	for (i = 0; i < VERSION_STRING_SIZE; i++) {
		ret = m5mols_read_u8(sd, SYSTEM_VER_STRING, &str[i]);
		if (ret)
			return ret;
	}

	v4l2_info(sd, "Manufacturer\t[%s]\n",
			is_manufacturer(info, REG_SAMSUNG_ELECTRO) ?
			"Samsung Electro-Mechanics" :
			is_manufacturer(info, REG_SAMSUNG_OPTICS) ?
			"Samsung Fiber-Optics" :
			is_manufacturer(info, REG_SAMSUNG_TECHWIN) ?
			"Samsung Techwin" : "None");
	v4l2_info(sd, "Customer/Project\t[0x%02x/0x%02x]\n",
			info->ver.customer, info->ver.project);

	if (!is_available_af(info))
		v4l2_info(sd, "No support Auto Focus on this firmware\n");

	return ret;
}

/**
 * __find_restype - Lookup M-5MOLS resolution type according to pixel code
 * @code: pixel code
 */
static enum m5mols_restype __find_restype(u32 code)
{
	enum m5mols_restype type = M5MOLS_RESTYPE_MONITOR;

	do {
		if (code == m5mols_default_ffmt[type].code)
			return type;
	} while (type++ != SIZE_DEFAULT_FFMT);

	return 0;
}

/**
 * __find_resolution - Lookup preset and type of M-5MOLS's resolution
 * @sd: sub-device, as pointed by struct v4l2_subdev
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
	const struct m5mols_resolution *fsize = &m5mols_reg_res[0];
	const struct m5mols_resolution *match = NULL;
	enum m5mols_restype stype = __find_restype(mf->code);
	int i = ARRAY_SIZE(m5mols_reg_res);
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
		*resolution = match->reg;
		*type = stype;
		return 0;
	}

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *__find_format(struct m5mols_info *info,
				struct v4l2_subdev_pad_config *cfg,
				enum v4l2_subdev_format_whence which,
				enum m5mols_restype type)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return cfg ? v4l2_subdev_get_try_format(&info->sd, cfg, 0) : NULL;

	return &info->ffmt[type];
}

static int m5mols_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&info->lock);

	format = __find_format(info, cfg, fmt->which, info->res_type);
	if (format)
		fmt->format = *format;
	else
		ret = -EINVAL;

	mutex_unlock(&info->lock);
	return ret;
}

static int m5mols_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	struct v4l2_mbus_framefmt *sfmt;
	enum m5mols_restype type;
	u32 resolution = 0;
	int ret;

	ret = __find_resolution(sd, format, &type, &resolution);
	if (ret < 0)
		return ret;

	sfmt = __find_format(info, cfg, fmt->which, type);
	if (!sfmt)
		return 0;

	mutex_lock(&info->lock);

	format->code = m5mols_default_ffmt[type].code;
	format->colorspace = V4L2_COLORSPACE_JPEG;
	format->field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		*sfmt = *format;
		info->resolution = resolution;
		info->res_type = type;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int m5mols_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_frame_desc *fd)
{
	struct m5mols_info *info = to_m5mols(sd);

	if (pad != 0 || fd == NULL)
		return -EINVAL;

	mutex_lock(&info->lock);
	/*
	 * .get_frame_desc is only used for compressed formats,
	 * thus we always return the capture frame parameters here.
	 */
	fd->entry[0].length = info->cap.buf_size;
	fd->entry[0].pixelcode = info->ffmt[M5MOLS_RESTYPE_CAPTURE].code;
	mutex_unlock(&info->lock);

	fd->entry[0].flags = V4L2_MBUS_FRAME_DESC_FL_LEN_MAX;
	fd->num_entries = 1;

	return 0;
}

static int m5mols_set_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_frame_desc *fd)
{
	struct m5mols_info *info = to_m5mols(sd);
	struct v4l2_mbus_framefmt *mf = &info->ffmt[M5MOLS_RESTYPE_CAPTURE];

	if (pad != 0 || fd == NULL)
		return -EINVAL;

	fd->entry[0].flags = V4L2_MBUS_FRAME_DESC_FL_LEN_MAX;
	fd->num_entries = 1;
	fd->entry[0].length = clamp_t(u32, fd->entry[0].length,
				      mf->width * mf->height,
				      M5MOLS_MAIN_JPEG_SIZE_MAX);
	mutex_lock(&info->lock);
	info->cap.buf_size = fd->entry[0].length;
	mutex_unlock(&info->lock);

	return 0;
}


static int m5mols_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (!code || code->index >= SIZE_DEFAULT_FFMT)
		return -EINVAL;

	code->code = m5mols_default_ffmt[code->index].code;

	return 0;
}

static const struct v4l2_subdev_pad_ops m5mols_pad_ops = {
	.enum_mbus_code	= m5mols_enum_mbus_code,
	.get_fmt	= m5mols_get_fmt,
	.set_fmt	= m5mols_set_fmt,
	.get_frame_desc	= m5mols_get_frame_desc,
	.set_frame_desc	= m5mols_set_frame_desc,
};

/**
 * m5mols_restore_controls - Apply current control values to the registers
 * @info: M-5MOLS driver data structure
 *
 * m5mols_do_scenemode() handles all parameters for which there is yet no
 * individual control. It should be replaced at some point by setting each
 * control individually, in required register set up order.
 */
int m5mols_restore_controls(struct m5mols_info *info)
{
	int ret;

	if (info->ctrl_sync)
		return 0;

	ret = m5mols_do_scenemode(info, REG_SCENE_NORMAL);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_setup(&info->handle);
	info->ctrl_sync = !ret;

	return ret;
}

/**
 * m5mols_start_monitor - Start the monitor mode
 * @info: M-5MOLS driver data structure
 *
 * Before applying the controls setup the resolution and frame rate
 * in PARAMETER mode, and then switch over to MONITOR mode.
 */
static int m5mols_start_monitor(struct m5mols_info *info)
{
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	ret = m5mols_set_mode(info, REG_PARAMETER);
	if (!ret)
		ret = m5mols_write(sd, PARM_MON_SIZE, info->resolution);
	if (!ret)
		ret = m5mols_write(sd, PARM_MON_FPS, REG_FPS_30);
	if (!ret)
		ret = m5mols_set_mode(info, REG_MONITOR);
	if (!ret)
		ret = m5mols_restore_controls(info);

	return ret;
}

static int m5mols_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct m5mols_info *info = to_m5mols(sd);
	u32 code;
	int ret;

	mutex_lock(&info->lock);
	code = info->ffmt[info->res_type].code;

	if (enable) {
		if (is_code(code, M5MOLS_RESTYPE_MONITOR))
			ret = m5mols_start_monitor(info);
		else if (is_code(code, M5MOLS_RESTYPE_CAPTURE))
			ret = m5mols_start_capture(info);
		else
			ret = -EINVAL;
	} else {
		ret = m5mols_set_mode(info, REG_PARAMETER);
	}

	mutex_unlock(&info->lock);
	return ret;
}

static const struct v4l2_subdev_video_ops m5mols_video_ops = {
	.s_stream	= m5mols_s_stream,
};

static int m5mols_sensor_power(struct m5mols_info *info, bool enable)
{
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct m5mols_platform_data *pdata = info->pdata;
	int ret;

	if (info->power == enable)
		return 0;

	if (enable) {
		if (info->set_power) {
			ret = info->set_power(&client->dev, 1);
			if (ret)
				return ret;
		}

		ret = regulator_bulk_enable(ARRAY_SIZE(supplies), supplies);
		if (ret) {
			if (info->set_power)
				info->set_power(&client->dev, 0);
			return ret;
		}

		gpio_set_value(pdata->gpio_reset, !pdata->reset_polarity);
		info->power = 1;

		return ret;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(supplies), supplies);
	if (ret)
		return ret;

	if (info->set_power)
		info->set_power(&client->dev, 0);

	gpio_set_value(pdata->gpio_reset, pdata->reset_polarity);

	info->isp_ready = 0;
	info->power = 0;

	return ret;
}

/* m5mols_update_fw - optional firmware update routine */
int __attribute__ ((weak)) m5mols_update_fw(struct v4l2_subdev *sd,
		int (*set_power)(struct m5mols_info *, bool))
{
	return 0;
}

/**
 * m5mols_fw_start - M-5MOLS internal ARM controller initialization
 * @sd: sub-device, as pointed by struct v4l2_subdev
 *
 * Execute the M-5MOLS internal ARM controller initialization sequence.
 * This function should be called after the supply voltage has been
 * applied and before any requests to the device are made.
 */
static int m5mols_fw_start(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	int ret;

	atomic_set(&info->irq_done, 0);
	/* Wait until I2C slave is initialized in Flash Writer mode */
	ret = m5mols_busy_wait(sd, FLASH_CAM_START, REG_IN_FLASH_MODE,
			       M5MOLS_I2C_RDY_WAIT_FL | 0xff, -1);
	if (!ret)
		ret = m5mols_write(sd, FLASH_CAM_START, REG_START_ARM_BOOT);
	if (!ret)
		ret = m5mols_wait_interrupt(sd, REG_INT_MODE, 2000);
	if (ret < 0)
		return ret;

	info->isp_ready = 1;

	ret = m5mols_get_version(sd);
	if (!ret)
		ret = m5mols_update_fw(sd, m5mols_sensor_power);
	if (ret)
		return ret;

	v4l2_dbg(1, m5mols_debug, sd, "Success ARM Booting\n");

	ret = m5mols_write(sd, PARM_INTERFACE, REG_INTERFACE_MIPI);
	if (!ret)
		ret = m5mols_enable_interrupt(sd,
				REG_INT_AF | REG_INT_CAPTURE);

	return ret;
}

/* Execute the lens soft-landing algorithm */
static int m5mols_auto_focus_stop(struct m5mols_info *info)
{
	int ret;

	ret = m5mols_write(&info->sd, AF_EXECUTE, REG_AF_STOP);
	if (!ret)
		ret = m5mols_write(&info->sd, AF_MODE, REG_AF_POWEROFF);
	if (!ret)
		ret = m5mols_busy_wait(&info->sd, SYSTEM_STATUS, REG_AF_IDLE,
				       0xff, -1);
	return ret;
}

/**
 * m5mols_s_power - Main sensor power control function
 * @sd: sub-device, as pointed by struct v4l2_subdev
 * @on: if true, powers on the device; powers off otherwise.
 *
 * To prevent breaking the lens when the sensor is powered off the Soft-Landing
 * algorithm is called where available. The Soft-Landing algorithm availability
 * dependends on the firmware provider.
 */
static int m5mols_s_power(struct v4l2_subdev *sd, int on)
{
	struct m5mols_info *info = to_m5mols(sd);
	int ret;

	mutex_lock(&info->lock);

	if (on) {
		ret = m5mols_sensor_power(info, true);
		if (!ret)
			ret = m5mols_fw_start(sd);
	} else {
		if (is_manufacturer(info, REG_SAMSUNG_TECHWIN)) {
			ret = m5mols_set_mode(info, REG_MONITOR);
			if (!ret)
				ret = m5mols_auto_focus_stop(info);
			if (ret < 0)
				v4l2_warn(sd, "Soft landing lens failed\n");
		}
		ret = m5mols_sensor_power(info, false);

		info->ctrl_sync = 0;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int m5mols_log_status(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);

	v4l2_ctrl_handler_log_status(&info->handle, sd->name);

	return 0;
}

static const struct v4l2_subdev_core_ops m5mols_core_ops = {
	.s_power	= m5mols_s_power,
	.log_status	= m5mols_log_status,
};

/*
 * V4L2 subdev internal operations
 */
static int m5mols_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format = v4l2_subdev_get_try_format(sd, fh->pad, 0);

	*format = m5mols_default_ffmt[0];
	return 0;
}

static const struct v4l2_subdev_internal_ops m5mols_subdev_internal_ops = {
	.open		= m5mols_open,
};

static const struct v4l2_subdev_ops m5mols_ops = {
	.core		= &m5mols_core_ops,
	.pad		= &m5mols_pad_ops,
	.video		= &m5mols_video_ops,
};

static irqreturn_t m5mols_irq_handler(int irq, void *data)
{
	struct m5mols_info *info = to_m5mols(data);

	atomic_set(&info->irq_done, 1);
	wake_up_interruptible(&info->irq_waitq);

	return IRQ_HANDLED;
}

static int m5mols_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	const struct m5mols_platform_data *pdata = client->dev.platform_data;
	unsigned long gpio_flags;
	struct m5mols_info *info;
	struct v4l2_subdev *sd;
	int ret;

	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->gpio_reset)) {
		dev_err(&client->dev, "No valid RESET GPIO specified\n");
		return -EINVAL;
	}

	if (!client->irq) {
		dev_err(&client->dev, "Interrupt not assigned\n");
		return -EINVAL;
	}

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdata = pdata;
	info->set_power	= pdata->set_power;

	gpio_flags = pdata->reset_polarity
		   ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
	ret = devm_gpio_request_one(&client->dev, pdata->gpio_reset, gpio_flags,
				    "M5MOLS_NRST");
	if (ret) {
		dev_err(&client->dev, "Failed to request gpio: %d\n", ret);
		return ret;
	}

	ret = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(supplies),
				      supplies);
	if (ret) {
		dev_err(&client->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &m5mols_ops);
	strlcpy(sd->name, MODULE_NAME, sizeof(sd->name));
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sd->internal_ops = &m5mols_subdev_internal_ops;
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &info->pad);
	if (ret < 0)
		return ret;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	init_waitqueue_head(&info->irq_waitq);
	mutex_init(&info->lock);

	ret = devm_request_irq(&client->dev, client->irq, m5mols_irq_handler,
			       IRQF_TRIGGER_RISING, MODULE_NAME, sd);
	if (ret) {
		dev_err(&client->dev, "Interrupt request failed: %d\n", ret);
		goto error;
	}
	info->res_type = M5MOLS_RESTYPE_MONITOR;
	info->ffmt[0] = m5mols_default_ffmt[0];
	info->ffmt[1] =	m5mols_default_ffmt[1];

	ret = m5mols_sensor_power(info, true);
	if (ret)
		goto error;

	ret = m5mols_fw_start(sd);
	if (!ret)
		ret = m5mols_init_controls(sd);

	ret = m5mols_sensor_power(info, false);
	if (!ret)
		return 0;
error:
	media_entity_cleanup(&sd->entity);
	return ret;
}

static int m5mols_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct i2c_device_id m5mols_id[] = {
	{ MODULE_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, m5mols_id);

static struct i2c_driver m5mols_i2c_driver = {
	.driver = {
		.name	= MODULE_NAME,
	},
	.probe		= m5mols_probe,
	.remove		= m5mols_remove,
	.id_table	= m5mols_id,
};

module_i2c_driver(m5mols_i2c_driver);

MODULE_AUTHOR("HeungJun Kim <riverful.kim@samsung.com>");
MODULE_AUTHOR("Dongsoo Kim <dongsoo45.kim@samsung.com>");
MODULE_DESCRIPTION("Fujitsu M-5MOLS 8M Pixel camera driver");
MODULE_LICENSE("GPL");
