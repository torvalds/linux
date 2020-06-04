// SPDX-License-Identifier: GPL-2.0
/*
 * Support for GalaxyCore GC0310 VGA camera sensor.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/moduleparam.h>
#include <media/v4l2-device.h>
#include <linux/io.h>
#include "../include/linux/atomisp_gmin_platform.h"

#include "gc0310.h"

/* i2c read/write stuff */
static int gc0310_read_reg(struct i2c_client *client,
			   u16 data_length, u8 reg, u8 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[1];

	if (!client->adapter) {
		dev_err(&client->dev, "%s error, no client->adapter\n",
			__func__);
		return -ENODEV;
	}

	if (data_length != GC0310_8BIT) {
		dev_err(&client->dev, "%s error, invalid data length\n",
			__func__);
		return -EINVAL;
	}

	memset(msg, 0, sizeof(msg));

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = I2C_MSG_LENGTH;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8)(reg & 0xff);

	msg[1].addr = client->addr;
	msg[1].len = data_length;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err != 2) {
		if (err >= 0)
			err = -EIO;
		dev_err(&client->dev,
			"read from offset 0x%x error %d", reg, err);
		return err;
	}

	*val = 0;
	/* high byte comes first */
	if (data_length == GC0310_8BIT)
		*val = (u8)data[0];

	return 0;
}

static int gc0310_i2c_write(struct i2c_client *client, u16 len, u8 *data)
{
	struct i2c_msg msg;
	const int num_msg = 1;
	int ret;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = data;
	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret == num_msg ? 0 : -EIO;
}

static int gc0310_write_reg(struct i2c_client *client, u16 data_length,
			    u8 reg, u8 val)
{
	int ret;
	unsigned char data[2] = {0};
	u8 *wreg = (u8 *)data;
	const u16 len = data_length + sizeof(u8); /* 8-bit address + data */

	if (data_length != GC0310_8BIT) {
		dev_err(&client->dev,
			"%s error, invalid data_length\n", __func__);
		return -EINVAL;
	}

	/* high byte goes out first */
	*wreg = (u8)(reg & 0xff);

	if (data_length == GC0310_8BIT)
		data[1] = (u8)(val);

	ret = gc0310_i2c_write(client, len, data);
	if (ret)
		dev_err(&client->dev,
			"write error: wrote 0x%x to offset 0x%x error %d",
			val, reg, ret);

	return ret;
}

/*
 * gc0310_write_reg_array - Initializes a list of GC0310 registers
 * @client: i2c driver client structure
 * @reglist: list of registers to be written
 *
 * This function initializes a list of registers. When consecutive addresses
 * are found in a row on the list, this function creates a buffer and sends
 * consecutive data in a single i2c_transfer().
 *
 * __gc0310_flush_reg_array, __gc0310_buf_reg_array() and
 * __gc0310_write_reg_is_consecutive() are internal functions to
 * gc0310_write_reg_array_fast() and should be not used anywhere else.
 *
 */

static int __gc0310_flush_reg_array(struct i2c_client *client,
				    struct gc0310_write_ctrl *ctrl)
{
	u16 size;

	if (ctrl->index == 0)
		return 0;

	size = sizeof(u8) + ctrl->index; /* 8-bit address + data */
	ctrl->buffer.addr = (u8)(ctrl->buffer.addr);
	ctrl->index = 0;

	return gc0310_i2c_write(client, size, (u8 *)&ctrl->buffer);
}

static int __gc0310_buf_reg_array(struct i2c_client *client,
				  struct gc0310_write_ctrl *ctrl,
				  const struct gc0310_reg *next)
{
	int size;

	switch (next->type) {
	case GC0310_8BIT:
		size = 1;
		ctrl->buffer.data[ctrl->index] = (u8)next->val;
		break;
	default:
		return -EINVAL;
	}

	/* When first item is added, we need to store its starting address */
	if (ctrl->index == 0)
		ctrl->buffer.addr = next->reg;

	ctrl->index += size;

	/*
	 * Buffer cannot guarantee free space for u32? Better flush it to avoid
	 * possible lack of memory for next item.
	 */
	if (ctrl->index + sizeof(u8) >= GC0310_MAX_WRITE_BUF_SIZE)
		return __gc0310_flush_reg_array(client, ctrl);

	return 0;
}

static int __gc0310_write_reg_is_consecutive(struct i2c_client *client,
	struct gc0310_write_ctrl *ctrl,
	const struct gc0310_reg *next)
{
	if (ctrl->index == 0)
		return 1;

	return ctrl->buffer.addr + ctrl->index == next->reg;
}

static int gc0310_write_reg_array(struct i2c_client *client,
				  const struct gc0310_reg *reglist)
{
	const struct gc0310_reg *next = reglist;
	struct gc0310_write_ctrl ctrl;
	int err;

	ctrl.index = 0;
	for (; next->type != GC0310_TOK_TERM; next++) {
		switch (next->type & GC0310_TOK_MASK) {
		case GC0310_TOK_DELAY:
			err = __gc0310_flush_reg_array(client, &ctrl);
			if (err)
				return err;
			msleep(next->val);
			break;
		default:
			/*
			 * If next address is not consecutive, data needs to be
			 * flushed before proceed.
			 */
			if (!__gc0310_write_reg_is_consecutive(client, &ctrl,
							       next)) {
				err = __gc0310_flush_reg_array(client, &ctrl);
				if (err)
					return err;
			}
			err = __gc0310_buf_reg_array(client, &ctrl, next);
			if (err) {
				dev_err(&client->dev, "%s: write error, aborted\n",
					__func__);
				return err;
			}
			break;
		}
	}

	return __gc0310_flush_reg_array(client, &ctrl);
}

static int gc0310_g_focal(struct v4l2_subdev *sd, s32 *val)
{
	*val = (GC0310_FOCAL_LENGTH_NUM << 16) | GC0310_FOCAL_LENGTH_DEM;
	return 0;
}

static int gc0310_g_fnumber(struct v4l2_subdev *sd, s32 *val)
{
	/*const f number for imx*/
	*val = (GC0310_F_NUMBER_DEFAULT_NUM << 16) | GC0310_F_NUMBER_DEM;
	return 0;
}

static int gc0310_g_fnumber_range(struct v4l2_subdev *sd, s32 *val)
{
	*val = (GC0310_F_NUMBER_DEFAULT_NUM << 24) |
	       (GC0310_F_NUMBER_DEM << 16) |
	       (GC0310_F_NUMBER_DEFAULT_NUM << 8) | GC0310_F_NUMBER_DEM;
	return 0;
}

static int gc0310_g_bin_factor_x(struct v4l2_subdev *sd, s32 *val)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	*val = gc0310_res[dev->fmt_idx].bin_factor_x;

	return 0;
}

static int gc0310_g_bin_factor_y(struct v4l2_subdev *sd, s32 *val)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	*val = gc0310_res[dev->fmt_idx].bin_factor_y;

	return 0;
}

static int gc0310_get_intg_factor(struct i2c_client *client,
				  struct camera_mipi_info *info,
				  const struct gc0310_resolution *res)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct atomisp_sensor_mode_data *buf = &info->data;
	u16 val;
	u8 reg_val;
	int ret;
	unsigned int hori_blanking;
	unsigned int vert_blanking;
	unsigned int sh_delay;

	if (!info)
		return -EINVAL;

	/* pixel clock calculattion */
	dev->vt_pix_clk_freq_mhz = 14400000; // 16.8MHz
	buf->vt_pix_clk_freq_mhz = dev->vt_pix_clk_freq_mhz;
	pr_info("vt_pix_clk_freq_mhz=%d\n", buf->vt_pix_clk_freq_mhz);

	/* get integration time */
	buf->coarse_integration_time_min = GC0310_COARSE_INTG_TIME_MIN;
	buf->coarse_integration_time_max_margin =
	    GC0310_COARSE_INTG_TIME_MAX_MARGIN;

	buf->fine_integration_time_min = GC0310_FINE_INTG_TIME_MIN;
	buf->fine_integration_time_max_margin =
	    GC0310_FINE_INTG_TIME_MAX_MARGIN;

	buf->fine_integration_time_def = GC0310_FINE_INTG_TIME_MIN;
	buf->read_mode = res->bin_mode;

	/* get the cropping and output resolution to ISP for this mode. */
	/* Getting crop_horizontal_start */
	ret =  gc0310_read_reg(client, GC0310_8BIT,
			       GC0310_H_CROP_START_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret =  gc0310_read_reg(client, GC0310_8BIT,
			       GC0310_H_CROP_START_L, &reg_val);
	if (ret)
		return ret;
	buf->crop_horizontal_start = val | (reg_val & 0xFF);
	pr_info("crop_horizontal_start=%d\n", buf->crop_horizontal_start);

	/* Getting crop_vertical_start */
	ret =  gc0310_read_reg(client, GC0310_8BIT,
			       GC0310_V_CROP_START_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret =  gc0310_read_reg(client, GC0310_8BIT,
			       GC0310_V_CROP_START_L, &reg_val);
	if (ret)
		return ret;
	buf->crop_vertical_start = val | (reg_val & 0xFF);
	pr_info("crop_vertical_start=%d\n", buf->crop_vertical_start);

	/* Getting output_width */
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_H_OUTSIZE_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_H_OUTSIZE_L, &reg_val);
	if (ret)
		return ret;
	buf->output_width = val | (reg_val & 0xFF);
	pr_info("output_width=%d\n", buf->output_width);

	/* Getting output_height */
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_V_OUTSIZE_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_V_OUTSIZE_L, &reg_val);
	if (ret)
		return ret;
	buf->output_height = val | (reg_val & 0xFF);
	pr_info("output_height=%d\n", buf->output_height);

	buf->crop_horizontal_end = buf->crop_horizontal_start + buf->output_width - 1;
	buf->crop_vertical_end = buf->crop_vertical_start + buf->output_height - 1;
	pr_info("crop_horizontal_end=%d\n", buf->crop_horizontal_end);
	pr_info("crop_vertical_end=%d\n", buf->crop_vertical_end);

	/* Getting line_length_pck */
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_H_BLANKING_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_H_BLANKING_L, &reg_val);
	if (ret)
		return ret;
	hori_blanking = val | (reg_val & 0xFF);
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_SH_DELAY, &reg_val);
	if (ret)
		return ret;
	sh_delay = reg_val;
	buf->line_length_pck = buf->output_width + hori_blanking + sh_delay + 4;
	pr_info("hori_blanking=%d sh_delay=%d line_length_pck=%d\n", hori_blanking,
		sh_delay, buf->line_length_pck);

	/* Getting frame_length_lines */
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_V_BLANKING_H, &reg_val);
	if (ret)
		return ret;
	val = (reg_val & 0xFF) << 8;
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_V_BLANKING_L, &reg_val);
	if (ret)
		return ret;
	vert_blanking = val | (reg_val & 0xFF);
	buf->frame_length_lines = buf->output_height + vert_blanking;
	pr_info("vert_blanking=%d frame_length_lines=%d\n", vert_blanking,
		buf->frame_length_lines);

	buf->binning_factor_x = res->bin_factor_x ?
				res->bin_factor_x : 1;
	buf->binning_factor_y = res->bin_factor_y ?
				res->bin_factor_y : 1;
	return 0;
}

static int gc0310_set_gain(struct v4l2_subdev *sd, int gain)

{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 again, dgain;

	if (gain < 0x20)
		gain = 0x20;
	if (gain > 0x80)
		gain = 0x80;

	if (gain >= 0x20 && gain < 0x40) {
		again = 0x0; /* sqrt(2) */
		dgain = gain;
	} else {
		again = 0x2; /* 2 * sqrt(2) */
		dgain = gain / 2;
	}

	pr_info("gain=0x%x again=0x%x dgain=0x%x\n", gain, again, dgain);

	/* set analog gain */
	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_AGC_ADJ, again);
	if (ret)
		return ret;

	/* set digital gain */
	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_DGC_ADJ, dgain);
	if (ret)
		return ret;

	return 0;
}

static int __gc0310_set_exposure(struct v4l2_subdev *sd, int coarse_itg,
				 int gain, int digitgain)

{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	pr_info("coarse_itg=%d gain=%d digitgain=%d\n", coarse_itg, gain, digitgain);

	/* set exposure */
	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_AEC_PK_EXPO_L,
			       coarse_itg & 0xff);
	if (ret)
		return ret;

	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_AEC_PK_EXPO_H,
			       (coarse_itg >> 8) & 0x0f);
	if (ret)
		return ret;

	ret = gc0310_set_gain(sd, gain);
	if (ret)
		return ret;

	return ret;
}

static int gc0310_set_exposure(struct v4l2_subdev *sd, int exposure,
			       int gain, int digitgain)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	ret = __gc0310_set_exposure(sd, exposure, gain, digitgain);
	mutex_unlock(&dev->input_lock);

	return ret;
}

static long gc0310_s_exposure(struct v4l2_subdev *sd,
			      struct atomisp_exposure *exposure)
{
	int exp = exposure->integration_time[0];
	int gain = exposure->gain[0];
	int digitgain = exposure->gain[1];

	/* we should not accept the invalid value below. */
	if (gain == 0) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);

		v4l2_err(client, "%s: invalid value\n", __func__);
		return -EINVAL;
	}

	return gc0310_set_exposure(sd, exp, gain, digitgain);
}

/* TO DO */
static int gc0310_v_flip(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

/* TO DO */
static int gc0310_h_flip(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

static long gc0310_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ATOMISP_IOC_S_EXPOSURE:
		return gc0310_s_exposure(sd, arg);
	default:
		return -EINVAL;
	}
	return 0;
}

/* This returns the exposure time being used. This should only be used
 * for filling in EXIF data, not for actual image processing.
 */
static int gc0310_q_exposure(struct v4l2_subdev *sd, s32 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 reg_v;
	int ret;

	/* get exposure */
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_AEC_PK_EXPO_L,
			      &reg_v);
	if (ret)
		goto err;

	*value = reg_v;
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_AEC_PK_EXPO_H,
			      &reg_v);
	if (ret)
		goto err;

	*value = *value + (reg_v << 8);
err:
	return ret;
}

static int gc0310_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0310_device *dev =
	    container_of(ctrl->handler, struct gc0310_device, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		dev_dbg(&client->dev, "%s: CID_VFLIP:%d.\n",
			__func__, ctrl->val);
		ret = gc0310_v_flip(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&client->dev, "%s: CID_HFLIP:%d.\n",
			__func__, ctrl->val);
		ret = gc0310_h_flip(&dev->sd, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int gc0310_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0310_device *dev =
	    container_of(ctrl->handler, struct gc0310_device, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = gc0310_q_exposure(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FOCAL_ABSOLUTE:
		ret = gc0310_g_focal(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_ABSOLUTE:
		ret = gc0310_g_fnumber(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_RANGE:
		ret = gc0310_g_fnumber_range(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_HORZ:
		ret = gc0310_g_bin_factor_x(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_VERT:
		ret = gc0310_g_bin_factor_y(&dev->sd, &ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = gc0310_s_ctrl,
	.g_volatile_ctrl = gc0310_g_volatile_ctrl
};

static const struct v4l2_ctrl_config gc0310_controls[] = {
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_EXPOSURE_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "exposure",
		.min = 0x0,
		.max = 0xffff,
		.step = 0x01,
		.def = 0x00,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Flip",
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_HFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Mirror",
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FOCAL_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "focal length",
		.min = GC0310_FOCAL_LENGTH_DEFAULT,
		.max = GC0310_FOCAL_LENGTH_DEFAULT,
		.step = 0x01,
		.def = GC0310_FOCAL_LENGTH_DEFAULT,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FNUMBER_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "f-number",
		.min = GC0310_F_NUMBER_DEFAULT,
		.max = GC0310_F_NUMBER_DEFAULT,
		.step = 0x01,
		.def = GC0310_F_NUMBER_DEFAULT,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FNUMBER_RANGE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "f-number range",
		.min = GC0310_F_NUMBER_RANGE,
		.max = GC0310_F_NUMBER_RANGE,
		.step = 0x01,
		.def = GC0310_F_NUMBER_RANGE,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_BIN_FACTOR_HORZ,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "horizontal binning factor",
		.min = 0,
		.max = GC0310_BIN_FACTOR_MAX,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_BIN_FACTOR_VERT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "vertical binning factor",
		.min = 0,
		.max = GC0310_BIN_FACTOR_MAX,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
};

static int gc0310_init(struct v4l2_subdev *sd)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	pr_info("%s S\n", __func__);
	mutex_lock(&dev->input_lock);

	/* set initial registers */
	ret  = gc0310_write_reg_array(client, gc0310_reset_register);

	/* restore settings */
	gc0310_res = gc0310_res_preview;
	N_RES = N_RES_PREVIEW;

	mutex_unlock(&dev->input_lock);

	pr_info("%s E\n", __func__);
	return ret;
}

static int power_ctrl(struct v4l2_subdev *sd, bool flag)
{
	int ret = 0;
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	if (!dev || !dev->platform_data)
		return -ENODEV;

	if (flag) {
		/* The upstream module driver (written to Crystal
		 * Cove) had this logic to pulse the rails low first.
		 * This appears to break things on the MRD7 with the
		 * X-Powers PMIC...
		 *
		 *     ret = dev->platform_data->v1p8_ctrl(sd, 0);
		 *     ret |= dev->platform_data->v2p8_ctrl(sd, 0);
		 *     mdelay(50);
		 */
		ret |= dev->platform_data->v1p8_ctrl(sd, 1);
		ret |= dev->platform_data->v2p8_ctrl(sd, 1);
		usleep_range(10000, 15000);
	}

	if (!flag || ret) {
		ret |= dev->platform_data->v1p8_ctrl(sd, 0);
		ret |= dev->platform_data->v2p8_ctrl(sd, 0);
	}
	return ret;
}

static int gpio_ctrl(struct v4l2_subdev *sd, bool flag)
{
	int ret;
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	if (!dev || !dev->platform_data)
		return -ENODEV;

	/* GPIO0 == "reset" (active low), GPIO1 == "power down" */
	if (flag) {
		/* Pulse reset, then release power down */
		ret = dev->platform_data->gpio0_ctrl(sd, 0);
		usleep_range(5000, 10000);
		ret |= dev->platform_data->gpio0_ctrl(sd, 1);
		usleep_range(10000, 15000);
		ret |= dev->platform_data->gpio1_ctrl(sd, 0);
		usleep_range(10000, 15000);
	} else {
		ret = dev->platform_data->gpio1_ctrl(sd, 1);
		ret |= dev->platform_data->gpio0_ctrl(sd, 0);
	}
	return ret;
}

static int power_down(struct v4l2_subdev *sd);

static int power_up(struct v4l2_subdev *sd)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	pr_info("%s S\n", __func__);
	if (!dev->platform_data) {
		dev_err(&client->dev,
			"no camera_sensor_platform_data");
		return -ENODEV;
	}

	/* power control */
	ret = power_ctrl(sd, 1);
	if (ret)
		goto fail_power;

	/* flis clock control */
	ret = dev->platform_data->flisclk_ctrl(sd, 1);
	if (ret)
		goto fail_clk;

	/* gpio ctrl */
	ret = gpio_ctrl(sd, 1);
	if (ret) {
		ret = gpio_ctrl(sd, 1);
		if (ret)
			goto fail_gpio;
	}

	msleep(100);

	pr_info("%s E\n", __func__);
	return 0;

fail_gpio:
	dev->platform_data->flisclk_ctrl(sd, 0);
fail_clk:
	power_ctrl(sd, 0);
fail_power:
	dev_err(&client->dev, "sensor power-up failed\n");

	return ret;
}

static int power_down(struct v4l2_subdev *sd)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (!dev->platform_data) {
		dev_err(&client->dev,
			"no camera_sensor_platform_data");
		return -ENODEV;
	}

	/* gpio ctrl */
	ret = gpio_ctrl(sd, 0);
	if (ret) {
		ret = gpio_ctrl(sd, 0);
		if (ret)
			dev_err(&client->dev, "gpio failed 2\n");
	}

	ret = dev->platform_data->flisclk_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "flisclk failed\n");

	/* power control */
	ret = power_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "vprog failed.\n");

	return ret;
}

static int gc0310_s_power(struct v4l2_subdev *sd, int on)
{
	int ret;

	if (on == 0)
		return power_down(sd);
	else {
		ret = power_up(sd);
		if (!ret)
			return gc0310_init(sd);
	}
	return ret;
}

/*
 * distance - calculate the distance
 * @res: resolution
 * @w: width
 * @h: height
 *
 * Get the gap between resolution and w/h.
 * res->width/height smaller than w/h wouldn't be considered.
 * Returns the value of gap or -1 if fail.
 */
#define LARGEST_ALLOWED_RATIO_MISMATCH 800
static int distance(struct gc0310_resolution *res, u32 w, u32 h)
{
	unsigned int w_ratio = (res->width << 13) / w;
	unsigned int h_ratio;
	int match;

	if (h == 0)
		return -1;
	h_ratio = (res->height << 13) / h;
	if (h_ratio == 0)
		return -1;
	match   = abs(((w_ratio << 13) / h_ratio) - 8192);

	if ((w_ratio < 8192) || (h_ratio < 8192)  ||
	    (match > LARGEST_ALLOWED_RATIO_MISMATCH))
		return -1;

	return w_ratio + h_ratio;
}

/* Return the nearest higher resolution index */
static int nearest_resolution_index(int w, int h)
{
	int i;
	int idx = -1;
	int dist;
	int min_dist = INT_MAX;
	struct gc0310_resolution *tmp_res = NULL;

	for (i = 0; i < N_RES; i++) {
		tmp_res = &gc0310_res[i];
		dist = distance(tmp_res, w, h);
		if (dist == -1)
			continue;
		if (dist < min_dist) {
			min_dist = dist;
			idx = i;
		}
	}

	return idx;
}

static int get_resolution_index(int w, int h)
{
	int i;

	for (i = 0; i < N_RES; i++) {
		if (w != gc0310_res[i].width)
			continue;
		if (h != gc0310_res[i].height)
			continue;

		return i;
	}

	return -1;
}

/* TODO: remove it. */
static int startup(struct v4l2_subdev *sd)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	pr_info("%s S\n", __func__);

	ret = gc0310_write_reg_array(client, gc0310_res[dev->fmt_idx].regs);
	if (ret) {
		dev_err(&client->dev, "gc0310 write register err.\n");
		return ret;
	}

	pr_info("%s E\n", __func__);
	return ret;
}

static int gc0310_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *gc0310_info = NULL;
	int ret = 0;
	int idx = 0;

	pr_info("%s S\n", __func__);

	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	gc0310_info = v4l2_get_subdev_hostdata(sd);
	if (!gc0310_info)
		return -EINVAL;

	mutex_lock(&dev->input_lock);

	idx = nearest_resolution_index(fmt->width, fmt->height);
	if (idx == -1) {
		/* return the largest resolution */
		fmt->width = gc0310_res[N_RES - 1].width;
		fmt->height = gc0310_res[N_RES - 1].height;
	} else {
		fmt->width = gc0310_res[idx].width;
		fmt->height = gc0310_res[idx].height;
	}
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *fmt;
		mutex_unlock(&dev->input_lock);
		return 0;
	}

	dev->fmt_idx = get_resolution_index(fmt->width, fmt->height);
	if (dev->fmt_idx == -1) {
		dev_err(&client->dev, "get resolution fail\n");
		mutex_unlock(&dev->input_lock);
		return -EINVAL;
	}

	printk("%s: before gc0310_write_reg_array %s\n", __func__,
	       gc0310_res[dev->fmt_idx].desc);
	ret = startup(sd);
	if (ret) {
		dev_err(&client->dev, "gc0310 startup err\n");
		goto err;
	}

	ret = gc0310_get_intg_factor(client, gc0310_info,
				     &gc0310_res[dev->fmt_idx]);
	if (ret) {
		dev_err(&client->dev, "failed to get integration_factor\n");
		goto err;
	}

	pr_info("%s E\n", __func__);
err:
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int gc0310_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	fmt->width = gc0310_res[dev->fmt_idx].width;
	fmt->height = gc0310_res[dev->fmt_idx].height;
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;

	return 0;
}

static int gc0310_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	u8 high, low;
	int ret;
	u16 id;

	pr_info("%s S\n", __func__);
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_SC_CMMN_CHIP_ID_H, &high);
	if (ret) {
		dev_err(&client->dev, "read sensor_id_high failed\n");
		return -ENODEV;
	}
	ret = gc0310_read_reg(client, GC0310_8BIT,
			      GC0310_SC_CMMN_CHIP_ID_L, &low);
	if (ret) {
		dev_err(&client->dev, "read sensor_id_low failed\n");
		return -ENODEV;
	}
	id = ((((u16)high) << 8) | (u16)low);
	pr_info("sensor ID = 0x%x\n", id);

	if (id != GC0310_ID) {
		dev_err(&client->dev, "sensor ID error, read id = 0x%x, target id = 0x%x\n", id,
			GC0310_ID);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "detect gc0310 success\n");

	pr_info("%s E\n", __func__);

	return 0;
}

static int gc0310_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	pr_info("%s S enable=%d\n", __func__, enable);
	mutex_lock(&dev->input_lock);

	if (enable) {
		/* enable per frame MIPI and sensor ctrl reset  */
		ret = gc0310_write_reg(client, GC0310_8BIT,
				       0xFE, 0x30);
		if (ret) {
			mutex_unlock(&dev->input_lock);
			return ret;
		}
	}

	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_3);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = gc0310_write_reg(client, GC0310_8BIT, GC0310_SW_STREAM,
			       enable ? GC0310_START_STREAMING :
			       GC0310_STOP_STREAMING);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = gc0310_write_reg(client, GC0310_8BIT,
			       GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_0);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	mutex_unlock(&dev->input_lock);
	pr_info("%s E\n", __func__);
	return ret;
}

static int gc0310_s_config(struct v4l2_subdev *sd,
			   int irq, void *platform_data)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	pr_info("%s S\n", __func__);
	if (!platform_data)
		return -ENODEV;

	dev->platform_data =
	    (struct camera_sensor_platform_data *)platform_data;

	mutex_lock(&dev->input_lock);
	/* power off the module, then power on it in future
	 * as first power on by board may not fulfill the
	 * power on sequqence needed by the module
	 */
	ret = power_down(sd);
	if (ret) {
		dev_err(&client->dev, "gc0310 power-off err.\n");
		goto fail_power_off;
	}

	ret = power_up(sd);
	if (ret) {
		dev_err(&client->dev, "gc0310 power-up err.\n");
		goto fail_power_on;
	}

	ret = dev->platform_data->csi_cfg(sd, 1);
	if (ret)
		goto fail_csi_cfg;

	/* config & detect sensor */
	ret = gc0310_detect(client);
	if (ret) {
		dev_err(&client->dev, "gc0310_detect err s_config.\n");
		goto fail_csi_cfg;
	}

	/* turn off sensor, after probed */
	ret = power_down(sd);
	if (ret) {
		dev_err(&client->dev, "gc0310 power-off err.\n");
		goto fail_csi_cfg;
	}
	mutex_unlock(&dev->input_lock);

	pr_info("%s E\n", __func__);
	return 0;

fail_csi_cfg:
	dev->platform_data->csi_cfg(sd, 0);
fail_power_on:
	power_down(sd);
	dev_err(&client->dev, "sensor power-gating failed\n");
fail_power_off:
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int gc0310_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	interval->interval.numerator = 1;
	interval->interval.denominator = gc0310_res[dev->fmt_idx].fps;

	return 0;
}

static int gc0310_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= MAX_FMTS)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	return 0;
}

static int gc0310_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= N_RES)
		return -EINVAL;

	fse->min_width = gc0310_res[index].width;
	fse->min_height = gc0310_res[index].height;
	fse->max_width = gc0310_res[index].width;
	fse->max_height = gc0310_res[index].height;

	return 0;
}

static int gc0310_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	mutex_lock(&dev->input_lock);
	*frames = gc0310_res[dev->fmt_idx].skip_frames;
	mutex_unlock(&dev->input_lock);

	return 0;
}

static const struct v4l2_subdev_sensor_ops gc0310_sensor_ops = {
	.g_skip_frames	= gc0310_g_skip_frames,
};

static const struct v4l2_subdev_video_ops gc0310_video_ops = {
	.s_stream = gc0310_s_stream,
	.g_frame_interval = gc0310_g_frame_interval,
};

static const struct v4l2_subdev_core_ops gc0310_core_ops = {
	.s_power = gc0310_s_power,
	.ioctl = gc0310_ioctl,
};

static const struct v4l2_subdev_pad_ops gc0310_pad_ops = {
	.enum_mbus_code = gc0310_enum_mbus_code,
	.enum_frame_size = gc0310_enum_frame_size,
	.get_fmt = gc0310_get_fmt,
	.set_fmt = gc0310_set_fmt,
};

static const struct v4l2_subdev_ops gc0310_ops = {
	.core = &gc0310_core_ops,
	.video = &gc0310_video_ops,
	.pad = &gc0310_pad_ops,
	.sensor = &gc0310_sensor_ops,
};

static int gc0310_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	dev_dbg(&client->dev, "gc0310_remove...\n");

	dev->platform_data->csi_cfg(sd, 0);

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	kfree(dev);

	return 0;
}

static int gc0310_probe(struct i2c_client *client)
{
	struct gc0310_device *dev;
	int ret;
	void *pdata;
	unsigned int i;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->input_lock);

	dev->fmt_idx = 0;
	v4l2_i2c_subdev_init(&dev->sd, client, &gc0310_ops);

	pdata = gmin_camera_platform_data(&dev->sd,
					  ATOMISP_INPUT_FORMAT_RAW_8,
					  atomisp_bayer_order_grbg);
	if (!pdata) {
		ret = -EINVAL;
		goto out_free;
	}

	ret = gc0310_s_config(&dev->sd, client->irq, pdata);
	if (ret)
		goto out_free;

	ret = atomisp_register_i2c_module(&dev->sd, pdata, RAW_CAMERA);
	if (ret)
		goto out_free;

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->format.code = MEDIA_BUS_FMT_SGRBG8_1X8;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret =
	    v4l2_ctrl_handler_init(&dev->ctrl_handler,
				   ARRAY_SIZE(gc0310_controls));
	if (ret) {
		gc0310_remove(client);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(gc0310_controls); i++)
		v4l2_ctrl_new_custom(&dev->ctrl_handler, &gc0310_controls[i],
				     NULL);

	if (dev->ctrl_handler.error) {
		gc0310_remove(client);
		return dev->ctrl_handler.error;
	}

	/* Use same lock for controls as for everything else. */
	dev->ctrl_handler.lock = &dev->input_lock;
	dev->sd.ctrl_handler = &dev->ctrl_handler;

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret)
		gc0310_remove(client);

	pr_info("%s E\n", __func__);
	return ret;
out_free:
	v4l2_device_unregister_subdev(&dev->sd);
	kfree(dev);
	return ret;
}

static const struct acpi_device_id gc0310_acpi_match[] = {
	{"XXGC0310"},
	{"INT0310"},
	{},
};
MODULE_DEVICE_TABLE(acpi, gc0310_acpi_match);

static struct i2c_driver gc0310_driver = {
	.driver = {
		.name = "gc0310",
		.acpi_match_table = gc0310_acpi_match,
	},
	.probe_new = gc0310_probe,
	.remove = gc0310_remove,
};
module_i2c_driver(gc0310_driver);

MODULE_AUTHOR("Lai, Angie <angie.lai@intel.com>");
MODULE_DESCRIPTION("A low-level driver for GalaxyCore GC0310 sensors");
MODULE_LICENSE("GPL");
