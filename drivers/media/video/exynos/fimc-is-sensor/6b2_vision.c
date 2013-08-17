/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/exynos-clock.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/exynos_fimc_is_sensor.h>

#include "6b2_vision.h"

/* TODO : What is this purpose? */
int sensor_debug;
module_param(sensor_debug, int, 0644);

/* TODO : Have to change MOD_NAME */
#define MOD_NAME		"S5K6B2"
#define DEFAULT_SENSOR_WIDTH	184
#define DEFAULT_SENSOR_HEIGHT	104
#define SENSOR_MEMSIZE DEFAULT_SENSOR_WIDTH * DEFAULT_SENSOR_HEIGHT

#define FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW		(1 << 7)
#define FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND		(1 << 6)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART		(1 << 5)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMEND		(1 << 4)

#define SENSOR_REG_VIS_DURATION_MSB					(0x6026)
#define SENSOR_REG_VIS_DURATION_LSB					(0x6027)
#define SENSOR_REG_VIS_FRAME_LENGTH_LINE_ALV_MSB	(0x4340)
#define SENSOR_REG_VIS_FRAME_LENGTH_LINE_ALV_LSB	(0x4341)
#define SENSOR_REG_VIS_LINE_LENGTH_PCLK_ALV_MSB		(0x4342)
#define SENSOR_REG_VIS_LINE_LENGTH_PCLK_ALV_LSB		(0x4343)
#define SENSOR_REG_VIS_GAIN_RED						(0x6029)
#define SENSOR_REG_VIS_GAIN_GREEN					(0x602A)
#define SENSOR_REG_VIS_AE_TARGET					(0x600A)
#define SENSOR_REG_VIS_AE_SPEED						(0x5034)
#define SENSOR_REG_VIS_AE_NUMBER_OF_PIXEL_MSB		(0x5030)
#define SENSOR_REG_VIS_AE_NUMBER_OF_PIXEL_LSB		(0x5031)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x1_2		(0x6000)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x3_4		(0x6001)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x1_2		(0x6002)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x3_4		(0x6003)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x1_2		(0x6004)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x3_4		(0x6005)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x1_2		(0x6006)
#define SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x3_4		(0x6007)
#define SENSOR_REG_VIS_AE_MANUAL_EXP_MSB			(0x5039)
#define SENSOR_REG_VIS_AE_MANUAL_EXP_LSB			(0x503A)
#define SENSOR_REG_VIS_AE_MANUAL_ANG_MSB			(0x503B)
#define SENSOR_REG_VIS_AE_MANUAL_ANG_LSB			(0x503C)
#define SENSOR_REG_VIS_BIT_CONVERTING_MSB			(0x602B)
#define SENSOR_REG_VIS_BIT_CONVERTING_LSB			(0x7203)
#define SENSOR_REG_VIS_AE_OFF						(0x5000)

static struct v4l2_mbus_framefmt default_fmt = {
	.width		= DEFAULT_SENSOR_WIDTH,
	.height		= DEFAULT_SENSOR_HEIGHT,
	.code		= V4L2_MBUS_FMT_SGRBG8_1X8,
	.field		= V4L2_FIELD_NONE,
	.colorspace	= V4L2_COLORSPACE_SRGB,
};
#define SIZE_DEFAULT_FFMT 1

static const struct sensor_format sensor_formats = {
	.code		= V4L2_MBUS_FMT_SGRBG8_1X8,
	.colorspace	= V4L2_COLORSPACE_SRGB,
};

/*
 * vision_sensor_read_reg/vision_sensor_write_reg - handle sensor's I2C communications.
 *
 * The I2C command packet of sensor is made up 3 kinds of I2C bytes(category,
 * command, bytes). Reference exynos_sensor.h.
 *
 * The packet is needed 2, when read through I2C.
 * The packet is needed 1, when written through I2C.
 *
 * I2C packet common order(including both reading/writing)
 *   1st : Slave addr
 *   2nd : READ/WRITE (R - 0x01, W - 0x02)
 *   3rd : Size
 *   4th : Data
 */
int vision_sensor_read_reg(struct v4l2_subdev *sd,
		u32 addr, u8 *val, enum sensor_i2c_size size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[2];
	u8 *array = (u8*)&addr;
	u8 wbuf[2];
	int ret;

	if (!client->adapter) {
		sensor_err("Could not find adapter!\n");
		return -ENODEV;
	}

	if (size != I2C_8BIT && size != I2C_16BIT && size != I2C_32BIT) {
		sensor_err("Wrong data size\n");
		return -EINVAL;
	}

	/* 1. I2C operation for writing. */
	msg[0].addr = client->addr;
	msg[0].flags = 0; /* write : 0, read : 1 */
	msg[0].len = 2;
	msg[0].buf = wbuf;
	/* TODO : consider other size of buffer */
	wbuf[0] = array[1];
	wbuf[1] = array[0];

	/* 2. I2C operation for reading data. */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = val;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		sensor_err("i2c treansfer fail");
		return ret;
	}

	return 0;
}

int vision_sensor_write_reg(struct v4l2_subdev *sd,
		u32 addr, u8 val, enum sensor_i2c_size size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[1];
	u8 *array = (u8*)&addr;
	u8 wbuf[3];
	int ret;

	if (!client->adapter) {
		sensor_err("Could not find adapter!\n");
		return -ENODEV;
	}

	if (size != I2C_8BIT && size != I2C_16BIT && size != I2C_32BIT) {
		sensor_err("Wrong data size\n");
		return -EINVAL;
	}

	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 3;
	msg->buf = wbuf;
	wbuf[0] = array[1];
	wbuf[1] = array[0];
	wbuf[2] = val;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
		sensor_err("i2c treansfer fail");
		return ret;
	}

	return 0;
}

/*
 * sensor_straming - start and stop streaming.
 */
int sensor_streaming(struct v4l2_subdev *sd, enum sensor_status state)
{
	struct sensor_info *info = to_sensor(sd);
	int ret = 0;

	if (state == info->status) {
		sensor_err("sensor is already stream %s",
			state == STATUS_STREAMING ? "on" : "off");
		return ret;
	}

	ret = vision_sensor_write_reg(sd, 0x4100, (u8)state, I2C_8BIT);
	if (ret < 0) {
		sensor_err("file to write reg(ret = %d)", ret);
		return ret;
	}

	if (state == STATUS_STANDBY) {
		ret = sensor_drain_request(sd, true);
		if (ret < 0) {
			sensor_err("fail to drain request");
			return ret;
		}
	}

	info->status = state;

	return ret;
}

static int sensor_g_mbus_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *ffmt)
{
	struct sensor_info *info = to_sensor(sd);

	*ffmt = info->fmt;

	return 0;
}

static int sensor_s_mbus_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *ffmt)
{
	struct sensor_info *info = to_sensor(sd);
	int ret = -EINVAL;

	info->fmt		= default_fmt;
	info->fmt.width	= ffmt->width;
	info->fmt.height	= ffmt->height;

	info->code = ffmt->code;

	return ret;
}

static int sensor_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned int index,
				  enum v4l2_mbus_pixelcode *code)
{
	*code = sensor_formats.code;

	return 0;
}

static int sensor_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	/* TODO : If need */
	sensor_info("not supported");
	return 0;
}

static int sensor_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	/* TODO : If need */
	sensor_info("not supported");
	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	if (enable) {
		if (!is_streaming(sd)) {
			v4l2_info(sd, "%s : start streaming\n", __func__);
			return sensor_streaming(sd, STATUS_STREAMING);
		}
		return -EINVAL;
	} else {
		if (is_streaming(sd)) {
			v4l2_info(sd, "%s : stop streaming\n", __func__);
			return sensor_streaming(sd, STATUS_STANDBY);
		}
		return -EINVAL;
	}
}

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.g_mbus_fmt		= sensor_g_mbus_fmt,
	.s_mbus_fmt		= sensor_s_mbus_fmt,
	.enum_mbus_fmt		= sensor_enum_mbus_fmt,
	.g_parm			= sensor_g_parm,
	.s_parm			= sensor_s_parm,
	.s_stream		= sensor_s_stream,
};

static int sensor_add_request(struct v4l2_subdev *sd, u32 addr, u8 val)
{
	struct sensor_info *info = to_sensor(sd);

	if (info->req_head == NULL) {
		info->req_head = kzalloc(sizeof(struct sensor_req_list), GFP_ATOMIC);
		if (info->req_head == NULL) {
			sensor_err("Failed to allocate new requeset\n");
			return -ENOMEM;
		}
		info->req_tail = info->req_head;

		info->req_head->next = NULL;
		info->req_head->addr = addr;
		info->req_head->value = val;
	} else if (info->req_tail->next == NULL) {
		info->req_tail->next = kzalloc(sizeof(struct sensor_req_list), GFP_ATOMIC);
		if (info->req_tail->next == NULL) {
			sensor_err("Failed to allocate new requeset\n");
			return -ENOMEM;
		}
		info->req_tail = info->req_tail->next;

		info->req_tail->next = NULL;
		info->req_tail->addr = addr;
		info->req_tail->value = val;
	} else {
		sensor_err("Failed to add request, abnormal state!");
		return -1;
	}

	return 0;
}

struct sensor_req_list *sensor_get_request(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_sensor(sd);
	struct sensor_req_list *req = NULL;

	req = info->req_head;
	if (info->req_head != NULL)
		info->req_head = info->req_head->next;

	return req;
}

static int sensor_drain_request(struct v4l2_subdev *sd, bool isValid)
{
	struct sensor_info *info = to_sensor(sd);
	struct sensor_req_list *req = NULL;
	int ret = 0;

	req = sensor_get_request(&info->sd);
	while (req) {
		if (isValid)
			ret = vision_sensor_write_reg(&info->sd, req->addr, (u8)req->value, I2C_8BIT);

		kfree(req);
		req = NULL;
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			break;
		}

		req = sensor_get_request(&info->sd);
	}

	return ret;
}

static int sensor_ctrl_ae_target(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", value[0]);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_TARGET, value[0]);
		if (ret < 0)
			sensor_err("Failed to add request");
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_TARGET, (u8)value[0], I2C_8BIT);
		if (ret < 0)
			sensor_err("file to write reg(ret = %d)", ret);
	}

	return ret;
}

static int sensor_ctrl_ae_weight(struct v4l2_subdev *sd, int cmd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("cmd = %x val = %d", cmd, val);

	switch (cmd) {
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_1x1_2:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x1_2, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x1_2, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_1x3_4:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x3_4, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_1x3_4, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_2x1_2:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x1_2, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x1_2, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_2x3_4:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x3_4, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_2x3_4, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_3x1_2:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x1_2, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x1_2, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_3x3_4:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x3_4, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_3x3_4, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_4x1_2:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x1_2, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x1_2, (u8)value[0], I2C_8BIT);
		}
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_4x3_4:
		if (info->status == STATUS_STREAMING) {
			ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x3_4, value[0]);
		} else {
			ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_WINDOW_WEIGHT_4x3_4, (u8)value[0], I2C_8BIT);
		}
		break;
	default :
		sensor_err("Unknown CID");
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		sensor_err("Failed to set ae weight(CID:%x)", cmd);

	return ret;
}

static int sensor_ctrl_rg_weight(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_GAIN_RED, value[1]);
		if (ret < 0) {
			sensor_err("Failed to add request(RED)");
			goto ret;
		}
		ret = sensor_add_request(sd, SENSOR_REG_VIS_GAIN_GREEN, value[0]);
		if (ret < 0) {
			sensor_err("Failed to add request(GREEN)");
			goto ret;
		}
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_GAIN_RED, (u8)value[1], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_GAIN_GREEN, (u8)value[0], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
	}

ret:
	return ret;
}

static int sensor_ctrl_ae_speed(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_SPEED, value[0]);
		if (ret < 0)
			sensor_err("Failed to add request");
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_SPEED, (u8)value[0], I2C_8BIT);
		if (ret < 0)
			sensor_err("file to write reg(ret = %d)", ret);
	}

	return ret;
}

static int sensor_ctrl_shutter(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_MANUAL_EXP_MSB, value[1]);
		if (ret < 0) {
			sensor_err("Failed to add request(MSB)");
			goto ret;
		}
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_MANUAL_EXP_LSB, value[0]);
		if (ret < 0) {
			sensor_err("Failed to add request(LSB)");
			goto ret;
		}
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_MANUAL_EXP_MSB, (u8)value[1], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_MANUAL_EXP_LSB, (u8)value[0], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
	}

ret:
	return ret;
}

static int sensor_ctrl_gain(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_MANUAL_ANG_MSB, value[1]);
		if (ret < 0) {
			sensor_err("Failed to add request(MSB)");
			goto ret;
		}
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_MANUAL_ANG_LSB, value[0]);
		if (ret < 0) {
			sensor_err("Failed to add request(LSB)");
			goto ret;
		}
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_MANUAL_ANG_MSB, (u8)value[1], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_MANUAL_ANG_LSB, (u8)value[0], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
	}

ret:
	return ret;
}

static int sensor_ctrl_bit_converting(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_BIT_CONVERTING_MSB, value[1]);
		if (ret < 0) {
			sensor_err("Failed to add request(MSB)");
			goto ret;
		}
		ret = sensor_add_request(sd, SENSOR_REG_VIS_BIT_CONVERTING_LSB, value[0]);
		if (ret < 0) {
			sensor_err("Failed to add request(LSB)");
			goto ret;
		}
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_BIT_CONVERTING_MSB, (u8)value[1], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_BIT_CONVERTING_LSB, (u8)value[0], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
	}

ret:
	return ret;
}

static int sensor_ctrl_auto_exposure(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_AE_OFF, value[0]);
		if (ret < 0)
			sensor_err("Failed to add request");
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_AE_OFF, (u8)value[0], I2C_8BIT);
		if (ret < 0)
			sensor_err("file to write reg(ret = %d)", ret);
	}

	return ret;
}

static int sensor_ctrl_frame_rate(struct v4l2_subdev *sd, int val)
{
	struct sensor_info *info = to_sensor(sd);
	u8 *value = (u8 *)&val;
	int ret = 0;

	sensor_dbg("val = %d", val);

	if (info->status == STATUS_STREAMING) {
		ret = sensor_add_request(sd, SENSOR_REG_VIS_DURATION_MSB, value[1]);
		if (ret < 0) {
			sensor_err("Failed to add request(MSB)");
			goto ret;
		}
		ret = sensor_add_request(sd, SENSOR_REG_VIS_DURATION_LSB, value[0]);
		if (ret < 0) {
			sensor_err("Failed to add request(LSB)");
			goto ret;
		}
	} else {
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_DURATION_MSB, (u8)value[1], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
		ret = vision_sensor_write_reg(sd, SENSOR_REG_VIS_DURATION_LSB, (u8)value[0], I2C_8BIT);
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			goto ret;
		}
	}

ret:
	return ret;
}

static int sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	u32 cmd = ctrl->id;
	s32 value = ctrl->val;
	int ret = 0;

	switch (cmd) {
	case V4L2_CID_SENSOR_SET_AE_TARGET:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_ae_target(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_1x1_2:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_1x3_4:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_2x1_2:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_2x3_4:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_3x1_2:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_3x3_4:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_4x1_2:
	case V4L2_CID_SENSOR_SET_AE_WEIGHT_4x3_4:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_ae_weight(sd, cmd, value);
		break;
	case V4L2_CID_SENSOR_SET_RG_WEIGHT:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_rg_weight(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_AE_SPEED:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_ae_speed(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_SHUTTER:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_shutter(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_GAIN:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_gain(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_BIT_CONVERTING:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_bit_converting(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_AUTO_EXPOSURE:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_auto_exposure(sd, value);
		break;
	case V4L2_CID_SENSOR_SET_FRAME_RATE:
		sensor_dbg("ctrl.id = %x, val=%x", cmd, value);
		ret = sensor_ctrl_frame_rate(sd, value);
		break;
	default :
		sensor_err("Unknown CID(%x)", cmd);
		ret = -1;
		break;
	}

	return ret;
}

static int sensor_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	/* TODO : If need */
	sensor_info("not supported");
	return 0;
}

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.s_ctrl = sensor_s_ctrl,
	.g_volatile_ctrl = sensor_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config ctrl_private[] = {
	{
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_TARGET,
		.name = "Sensor set ae target",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0xFF,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_1x1_2,
		.name = "Sensor set ae weight_1x1_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_1x3_4,
		.name = "Sensor set ae weight_1x3_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_2x1_2,
		.name = "Sensor set ae weight_2x1_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_2x3_4,
		.name = "Sensor set ae weight_2x3_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_3x1_2,
		.name = "Sensor set ae weight_3x1_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_3x3_4,
		.name = "Sensor set ae weight_3x3_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_4x1_2,
		.name = "Sensor set ae weight_4x1_2",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_WEIGHT_4x3_4,
		.name = "Sensor set ae weight_4x3_4",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x44,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_RG_WEIGHT,
		.name = "Sensor set R/G weight",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0xFFFF,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AE_SPEED,
		.name = "Sensor set ae speed",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x3F,
		.step = 0x01,
		.min = 0x00,
		.def = 0x00,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_SHUTTER,
		.name = "Sensor set shutter control",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0xFFFF,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_GAIN,
		.name = "Sensor set analog gain",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x100,
		.step = 0x01,
		.min = 0x20,
		.def = 0x20,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_BIT_CONVERTING,
		.name = "Sensor set bit converting",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0xFFFF,
		.step = 0x01,
		.min = 0x01,
		.def = 0x01,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_AUTO_EXPOSURE,
		.name = "Sensor set auto exposure",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0x02,
		.step = 0x02,
		.min = 0x00,
		.def = 0x00,
	}, {
		.ops = &sensor_ctrl_ops,
		.id = V4L2_CID_SENSOR_SET_FRAME_RATE,
		.name = "Sensor set frame rate",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_UPDATE,
		.max = 0xFFFF,
		.step = 0x01,
		.min = 0x00,
		.def = 0x00,
	},
};

/*
 * sensor_power - handle sensor power up/down.
 *
 * @enable: If it is true, power up. If is not, power down.
 */
static int sensor_power(struct sensor_info *info, bool enable)
{
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *cdev = &client->dev;
	const struct exynos_fimc_is_sensor_platform_data *pdata = cdev->platform_data;
	int ret = 0;

	if (enable) {
		if (is_powerup(sd))
			return 0;

		if (pdata->clk_on) {
			pdata->clk_on(cdev, FLITE_ID_C);
		} else {
			sensor_err("failed to clk_on\n");
			return -1;
		}
		info->power = true;
	} else {
		if (!is_powerup(sd))
			return 0;

		if (pdata->clk_off) {
			pdata->clk_off(cdev, FLITE_ID_C);
		} else {
			sensor_err("failed to clk_off\n");
			return -1;
		}
		info->power = false;
	}

	return ret;
}

static void sensor_irq_work(struct work_struct *work)
{
	struct sensor_info *info = container_of(work, struct sensor_info, work);
	struct sensor_req_list *req = NULL;
	int ret = 0;

	req = sensor_get_request(&info->sd);
	while (req) {
		sensor_dbg("addr(%.8x), val(%x)", req->addr, req->value);
		ret = vision_sensor_write_reg(&info->sd, req->addr, (u8)req->value, I2C_8BIT);

		kfree(req);
		req = NULL;
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			break;
		}

		req = sensor_get_request(&info->sd);
	}
}

static void tasklet_func_flite_str(unsigned long data)
{
	/* TODO: handle flite frame start */
	sensor_info("not supported yet");
}

static void tasklet_func_flite_end(unsigned long data)
{
	struct sensor_info *info = (struct sensor_info *)data;
	struct sensor_req_list *req = NULL;
	int ret = 0;

	req = sensor_get_request(&info->sd);
	while (req) {
		sensor_dbg("addr(%.8x), val(%x)", req->addr, req->value);
		ret = vision_sensor_write_reg(&info->sd, req->addr, (u8)req->value, I2C_8BIT);

		kfree(req);
		req = NULL;
		if (ret < 0) {
			sensor_err("file to write reg(ret = %d)", ret);
			break;
		}

		req = sensor_get_request(&info->sd);
	}
}

static irqreturn_t sensor_irq_handler(int irq, void *data)
{
	struct v4l2_subdev *sd = data;
	struct sensor_info *info = to_sensor(sd);

	v4l2_info(sd, "%s\n", __func__);

	schedule_work(&info->work);

	return IRQ_HANDLED;
}

static void sensor_handler(void *data, int arg)
{
	struct v4l2_subdev *sd = data;
	struct sensor_info *info = to_sensor(sd);

	sensor_dbg("%s: irq(%d), info->status(%d)", __func__, arg, info->status);

	switch (arg) {
	case FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART:
		sensor_dbg("Flite frame start");
		tasklet_schedule(&info->tasklet_flite_str);
		break;
	case FLITE_REG_CISTATUS_IRQ_SRC_FRMEND:
		sensor_dbg("Flite frame end");
		schedule_work(&info->work);
		break;
	default :
		sensor_err("unknown irq(%d)", arg);
		break;
	}
}

/*
 * sensor_init_controls - initialization using v4l2_ctrl.
 */
static int sensor_init_controls(struct sensor_info *info)
{
	struct v4l2_subdev *sd = &info->sd;
	int num_of_ctl_hint = ARRAY_SIZE(ctrl_private);

	/* set the controls using v4l2 control frameworks */
	v4l2_ctrl_handler_init(&info->handle, num_of_ctl_hint);

	/* Vision mode only support simple AE */
	info->ae_target = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[0], NULL);
	info->ae_weight[0] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[1], NULL);
	info->ae_weight[1] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[2], NULL);
	info->ae_weight[2] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[3], NULL);
	info->ae_weight[3] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[4], NULL);
	info->ae_weight[4] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[5], NULL);
	info->ae_weight[5] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[6], NULL);
	info->ae_weight[6] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[7], NULL);
	info->ae_weight[7] = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[8], NULL);
	info->rg_weight = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[9], NULL);
	info->ae_speed = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[10], NULL);
	info->shutter = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[11], NULL);
	info->gain = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[12], NULL);
	info->bit_converting = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[13], NULL);
	info->autoexposure = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[14], NULL);
	info->framerate = v4l2_ctrl_new_custom(&info->handle,
			&ctrl_private[15], NULL);

	sd->ctrl_handler = &info->handle;

	if (info->handle.error) {
		sensor_err("Failed to init controls, %d\n", info->handle.error);
		v4l2_ctrl_handler_free(&info->handle);
		return info->handle.error;
	}

	return 0;
}

/*
 * sensor_setup_default - set default size & fps in the monitor mode.
 */
static int sensor_setup_default(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_sensor(sd);
	int ret = 0;

	/* TODO : Hear set sensor init */
	info->fmt = default_fmt;

	/* sensor init */
	/* 8 bit mode */
	vision_sensor_write_reg(sd, 0x7203, 0x40, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x602B, 0x02, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x702A, 0x3D, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x702B, 0xB0, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7030, 0x0E, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7031, 0x2F, I2C_8BIT);

	/* Analog Tuning */
	vision_sensor_write_reg(sd, 0x7067, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7073, 0xFF, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7074, 0x22, I2C_8BIT);

	/* Dark Tuning */
	vision_sensor_write_reg(sd, 0x7042, 0x1F, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7403, 0xC0, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7245, 0x04, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7205, 0xA1, I2C_8BIT);

	/* Remove Dark Band */
	vision_sensor_write_reg(sd, 0x7430, 0x07, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x705C, 0x7E, I2C_8BIT);

	/* Remove  Sun spot */
	vision_sensor_write_reg(sd, 0x702C, 0x3C, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7075, 0x3D, I2C_8BIT);

	/* Remove CFPN */
	vision_sensor_write_reg(sd, 0x7066, 0x0C, I2C_8BIT);

	/* AE setting */
	vision_sensor_write_reg(sd, 0x6000, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6001, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6002, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6003, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6004, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6005, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6006, 0x44, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x6007, 0x44, I2C_8BIT);

	/* AE target */
	vision_sensor_write_reg(sd, 0x600A, 0xB4, I2C_8BIT);

	/* speed */
	vision_sensor_write_reg(sd, 0x5034, 0x00, I2C_8BIT);

	/* Cintc_min */
	vision_sensor_write_reg(sd, 0x5017, 0x01, I2C_8BIT);

	/* Number of pixel */
	vision_sensor_write_reg(sd, 0x5030, 0x4A, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x5031, 0xC0, I2C_8BIT);

	/* G + R Setting */
	/* Vision Senser Data = 0.5*Gr + 0.5*R */
	vision_sensor_write_reg(sd, 0x6029, 0x02, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x602A, 0x02, I2C_8BIT);

	/* For Analog Gain 16x */
	vision_sensor_write_reg(sd, 0x7018, 0xCF, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7019, 0xDB, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x702A, 0x8D, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x702B, 0x60, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x5035, 0x02, I2C_8BIT);

	/* BIT_RATE_MBPS_alv */
	vision_sensor_write_reg(sd, 0x7351, 0x02, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7352, 0x48, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7353, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x7354, 0x00, I2C_8BIT);

	vision_sensor_write_reg(sd, 0x7339, 0x03, I2C_8BIT);

	/* Analog gain */
	vision_sensor_write_reg(sd, 0x4204, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4205, 0x32, I2C_8BIT);

    /* frame rate - default 10fps*/
	vision_sensor_write_reg(sd, SENSOR_REG_VIS_DURATION_MSB, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, SENSOR_REG_VIS_DURATION_LSB, 0x6A, I2C_8BIT);


/* TODO : clock control is need */
#if 0
	/* VT Pixel clock devider */
	vision_sensor_write_reg(sd, 0x4300, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4301, 0x08, I2C_8BIT);

	/* VT System clock devider */
	vision_sensor_write_reg(sd, 0x4302, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4303, 0x01, I2C_8BIT);

	/* Pre PLL clock devider */
	vision_sensor_write_reg(sd, 0x4304, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4305, 0x06, I2C_8BIT);

	/* PLL multiplier */
	vision_sensor_write_reg(sd, 0x4306, 0x01, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4307, 0x46, I2C_8BIT);

	/* Output Pixel clock devider */
	vision_sensor_write_reg(sd, 0x4308, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x4309, 0x08, I2C_8BIT);

	/* Output System clock devider */
	vision_sensor_write_reg(sd, 0x430A, 0x00, I2C_8BIT);
	vision_sensor_write_reg(sd, 0x430B, 0x01, I2C_8BIT);
#endif

	return ret;
}

static int sensor_s_power(struct v4l2_subdev *sd, int on)
{
	struct sensor_info *info = to_sensor(sd);
	int ret;

	if (on) {
		ret = sensor_power(info, true);
		if (!ret)
			ret = sensor_setup_default(sd);
	} else {
		ret = sensor_power(info, false);
	}

	return ret;
}

static int sensor_log_status(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_sensor(sd);

	v4l2_ctrl_handler_log_status(&info->handle, sd->name);

	return 0;
}

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.s_power		= sensor_s_power,
	.g_ctrl			= v4l2_subdev_g_ctrl,
	.s_ctrl			= v4l2_subdev_s_ctrl,
	.queryctrl		= v4l2_subdev_queryctrl,
	.querymenu		= v4l2_subdev_querymenu,
	.g_ext_ctrls		= v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls		= v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls		= v4l2_subdev_s_ext_ctrls,
	.log_status		= sensor_log_status,
};

static struct v4l2_mbus_framefmt *__find_format(struct sensor_info *info,
				struct v4l2_subdev_fh *fh,
				enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;

	return &info->fmt;
}

static int sensor_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor_info *info = to_sensor(sd);
	struct v4l2_mbus_framefmt *format;

	if (fmt->pad != 0)
		return -EINVAL;

	format = __find_format(info, fh, fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

static int sensor_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor_info *info = to_sensor(sd);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	struct v4l2_mbus_framefmt *sfmt;

	if (fmt->pad != 0)
		return -EINVAL;

	sfmt = __find_format(info, fh, fmt->which);
	if (!sfmt)
		return 0;

	sfmt		= &default_fmt;
	sfmt->width	= format->width;
	sfmt->height	= format->height;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		info->code = format->code;

	return 0;
}

static int sensor_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (!code || code->index >= SIZE_DEFAULT_FFMT)
		return -EINVAL;

	code->code = default_fmt.code;

	return 0;
}

static struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code	= sensor_enum_mbus_code,
	.get_fmt	= sensor_get_fmt,
	.set_fmt	= sensor_set_fmt,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core	= &sensor_core_ops,
	.pad	= &sensor_pad_ops,
	.video	= &sensor_video_ops,
};

static int sensor_link_setup(struct media_entity *entity,
				const struct media_pad *local,
				const struct media_pad *remote, u32 flags)
{
	/* TODO : If need */
	sensor_info("not supported yet");
	return 0;
}
static const struct media_entity_operations sensor_media_ops = {
	.link_setup = sensor_link_setup,
};

static int sensor_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = 0;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = sensor_formats.code;
	format.format.width = DEFAULT_SENSOR_WIDTH;
	format.format.height = DEFAULT_SENSOR_HEIGHT;

	sensor_set_fmt(sd, fh, &format);

	return 0;
}

static int sensor_set_clock(struct v4l2_subdev *sd, int sensor_id)
{
	char sensor_mclk[20];
	struct clk *sclk_mout_sensor = NULL;
	struct clk *sclk_sensor = NULL;
	unsigned long sensor;

	/* SENSOR */
	snprintf(sensor_mclk, sizeof(sensor_mclk), "sclk_isp_sensor%d", sensor_id);
	sclk_mout_sensor = clk_get(NULL, "sclk_mout_isp_sensor");
	if (IS_ERR(sclk_mout_sensor)) {
		pr_err("%s : clk_get(sclk_mout_sensor%d) failed\n", __func__, sensor_id);
		return PTR_ERR(sclk_mout_sensor);
	}

	sclk_sensor = clk_get(NULL, sensor_mclk);
	if (IS_ERR(sclk_sensor)) {
		pr_err("%s : clk_get(sclk_isp_sensor%d) failed\n", __func__, sensor_id);
		return PTR_ERR(sclk_sensor);
	}

	clk_set_parent(sclk_mout_sensor, clk_get(NULL, "mout_ipll"));
	clk_set_rate(sclk_sensor, 24 * 1000000);

	sensor = clk_get_rate(sclk_sensor);
	pr_debug("sensor : %ld\n", sensor);

	clk_put(sclk_mout_sensor);
	clk_put(sclk_sensor);

	return 0;
}

static int mipi_csis_callback(struct device *dev, void *p)
{
	struct platform_device **pd_list = p;
	struct v4l2_subdev *sd = NULL;

	sd = dev_get_drvdata(dev);

	if (sd) {
		struct platform_device *pdev = v4l2_get_subdevdata(sd);
		if (pdev) {
			v4l2_dbg(1, sensor_debug, sd, "pdev->id: %d", pdev->id);
			*(pd_list + pdev->id) = pdev;
		} else {
			return -1;
		}
	}

	return 0;
}

static struct platform_device *get_mipi_csis_pdev(int id)
{
	const char *module_name = "s5p-mipi-csis";
	struct device_driver *drv;
	struct platform_device *pdev[FLITE_ID_END];
	int ret;

	drv = driver_find(module_name, &platform_bus_type);
	if (!drv) {
		request_module(module_name);
		drv = driver_find(module_name, &platform_bus_type);
	}
	if (!drv)
		return ERR_PTR(-ENODEV);

	ret = driver_for_each_device(drv, NULL, &pdev[0], mipi_csis_callback);

	return ret ? NULL : pdev[id];
}

static int sensor_subdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sensor_info *info = to_sensor(sd);
	int ret = 0;
	struct platform_device *pdev;
	int irq = 0;

	v4l2_dbg(1, sensor_debug, sd, "%s", __func__);

	ret = sensor_set_clock(sd, FLITE_ID_C);
	if (ret < 0) {
		sensor_err("sensor set clock fail");
		return ret;
	}

	ret = sensor_init_controls(info);
	if (ret < 0) {
		sensor_err("sensor init contols fail");
		return ret;
	}

	fh->vfh.ctrl_handler = &info->handle;
	sensor_init_formats(sd, fh);

	pdev = get_mipi_csis_pdev(FLITE_ID_C);
	irq = platform_get_irq(pdev, 0);
	v4l2_dbg(1, sensor_debug, sd, "%s : mipi irq num : %d", __func__, irq);

	enable_irq(irq);

	return 0;
}

static int sensor_subdev_close(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh)
{
	struct sensor_info *info = to_sensor(sd);

	v4l2_dbg(1, sensor_debug, sd, "%s", __func__);

	v4l2_ctrl_handler_free(&info->handle);

	return 0;
}

static int sensor_subdev_registered(struct v4l2_subdev *sd)
{
	/* TODO : If need */
	v4l2_dbg(1, sensor_debug, sd, "%s", __func__);
	return 0;
}

static void sensor_subdev_unregistered(struct v4l2_subdev *sd)
{
	/* TODO : If need */
	v4l2_dbg(1, sensor_debug, sd, "%s", __func__);
}

static const struct v4l2_subdev_internal_ops sensor_v4l2_internal_ops = {
	.open = sensor_subdev_open,
	.close = sensor_subdev_close,
	.registered = sensor_subdev_registered,
	.unregistered = sensor_subdev_unregistered,
};

static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	const struct exynos_fimc_is_sensor_platform_data *pdata =
		client->dev.platform_data;
	struct sensor_info *info;
	struct v4l2_subdev *sd;
	int ret = 0;

	if (pdata == NULL) {
		sensor_err("No platform data\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->gpio_rst)) {
		sensor_err("No valid nRST gpio pin.\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL) {
		sensor_err("Failed to allocate info\n");
		return -ENOMEM;
	}

	info->pdata = pdata;

	tasklet_init(&info->tasklet_flite_str, tasklet_func_flite_str,
		(unsigned long)info);

	tasklet_init(&info->tasklet_flite_end, tasklet_func_flite_end,
		(unsigned long)info);

	INIT_WORK(&info->work, sensor_irq_work);
	if (info->pdata->irq) {
		ret = request_irq(info->pdata->irq, sensor_irq_handler,
				  IRQF_TRIGGER_RISING, MOD_NAME, &info->sd);
		if (ret) {
			sensor_err("Failed to request irq: %d\n", ret);
			return ret;
		}
	}

	ret = gpio_request(info->pdata->gpio_rst, "GPIO_CAM_VT_nRST");
	if (ret) {
		sensor_err("Failed to set gpio, %d\n", ret);
		goto out_gpio;
	}

/*
	s3c_gpio_setpull(info->pdata->gpio_rst, S3C_GPIO_PULL_NONE);

	gpio_direction_output(info->pdata->gpio_rst, 0);
	gpio_direction_output(info->pdata->gpio_rst, 1);
*/

	gpio_free(info->pdata->gpio_rst);

	sd = &info->sd;

	v4l2_i2c_subdev_init(sd, client, &sensor_ops);
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, 1, &info->pad, 0);
	if (ret < 0)
		goto out_reg;

	sensor_init_formats(sd, NULL);

	strlcpy(sd->name, MOD_NAME, sizeof(sd->name));
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &sensor_v4l2_internal_ops;
	sd->entity.ops = &sensor_media_ops;

	sd->host_priv = sensor_handler;

	info->req_head = NULL;
	info->req_tail = NULL;

	v4l2_info(sd, "%s : sensor driver probed success\n", __func__);

	return 0;

out_reg:
out_gpio:
	gpio_free(info->pdata->gpio_rst);
	kfree(info);

	return ret;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sensor_info *info = to_sensor(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->handle);
	free_irq(info->pdata->irq, sd);
	gpio_free(info->pdata->gpio_rst);
	media_entity_cleanup(&sd->entity);
	kfree(info);

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ MOD_NAME, 0 },
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name	= MOD_NAME,
	},
	.probe		= sensor_probe,
	.remove		= sensor_remove,
	.id_table	= sensor_id,
};

module_i2c_driver(sensor_i2c_driver);

MODULE_AUTHOR("Hyeonmyeong Choi <hyeon.choi@samsung.com>");
MODULE_DESCRIPTION("S5K6B2 2M Pixel camera vision sensor");
MODULE_LICENSE("GPL");
