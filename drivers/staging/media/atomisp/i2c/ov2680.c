/*
 * Support for OmniVision OV2680 1080p HD camera sensor.
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
#include <linux/gpio.h>
#include <linux/moduleparam.h>
#include <media/v4l2-device.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include "../include/linux/atomisp_gmin_platform.h"

#include "ov2680.h"

static int h_flag = 0;
static int v_flag = 0;
static enum atomisp_bayer_order ov2680_bayer_order_mapping[] = {
	atomisp_bayer_order_bggr,
	atomisp_bayer_order_grbg,
	atomisp_bayer_order_gbrg,
	atomisp_bayer_order_rggb,
};

/* i2c read/write stuff */
static int ov2680_read_reg(struct i2c_client *client,
			   u16 data_length, u16 reg, u16 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[6];

	if (!client->adapter) {
		dev_err(&client->dev, "%s error, no client->adapter\n",
			__func__);
		return -ENODEV;
	}

	if (data_length != OV2680_8BIT && data_length != OV2680_16BIT
					&& data_length != OV2680_32BIT) {
		dev_err(&client->dev, "%s error, invalid data length\n",
			__func__);
		return -EINVAL;
	}

	memset(msg, 0 , sizeof(msg));

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = I2C_MSG_LENGTH;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8)(reg >> 8);
	data[1] = (u8)(reg & 0xff);

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
	if (data_length == OV2680_8BIT)
		*val = (u8)data[0];
	else if (data_length == OV2680_16BIT)
		*val = be16_to_cpu(*(u16 *)&data[0]);
	else
		*val = be32_to_cpu(*(u32 *)&data[0]);
	//dev_dbg(&client->dev,  "++++i2c read adr%x = %x\n", reg,*val);
	return 0;
}

static int ov2680_i2c_write(struct i2c_client *client, u16 len, u8 *data)
{
	struct i2c_msg msg;
	const int num_msg = 1;
	int ret;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = data;
	ret = i2c_transfer(client->adapter, &msg, 1);
	//dev_dbg(&client->dev,  "+++i2c write reg=%x->%x\n", data[0]*256 +data[1],data[2]);
	return ret == num_msg ? 0 : -EIO;
}

static int ov2680_write_reg(struct i2c_client *client, u16 data_length,
							u16 reg, u16 val)
{
	int ret;
	unsigned char data[4] = {0};
	u16 *wreg = (u16 *)data;
	const u16 len = data_length + sizeof(u16); /* 16-bit address + data */

	if (data_length != OV2680_8BIT && data_length != OV2680_16BIT) {
		dev_err(&client->dev,
			"%s error, invalid data_length\n", __func__);
		return -EINVAL;
	}

	/* high byte goes out first */
	*wreg = cpu_to_be16(reg);

	if (data_length == OV2680_8BIT) {
		data[2] = (u8)(val);
	} else {
		/* OV2680_16BIT */
		u16 *wdata = (u16 *)&data[2];
		*wdata = cpu_to_be16(val);
	}

	ret = ov2680_i2c_write(client, len, data);
	if (ret)
		dev_err(&client->dev,
			"write error: wrote 0x%x to offset 0x%x error %d",
			val, reg, ret);

	return ret;
}

/*
 * ov2680_write_reg_array - Initializes a list of OV2680 registers
 * @client: i2c driver client structure
 * @reglist: list of registers to be written
 *
 * This function initializes a list of registers. When consecutive addresses
 * are found in a row on the list, this function creates a buffer and sends
 * consecutive data in a single i2c_transfer().
 *
 * __ov2680_flush_reg_array, __ov2680_buf_reg_array() and
 * __ov2680_write_reg_is_consecutive() are internal functions to
 * ov2680_write_reg_array_fast() and should be not used anywhere else.
 *
 */

static int __ov2680_flush_reg_array(struct i2c_client *client,
				    struct ov2680_write_ctrl *ctrl)
{
	u16 size;

	if (ctrl->index == 0)
		return 0;

	size = sizeof(u16) + ctrl->index; /* 16-bit address + data */
	ctrl->buffer.addr = cpu_to_be16(ctrl->buffer.addr);
	ctrl->index = 0;

	return ov2680_i2c_write(client, size, (u8 *)&ctrl->buffer);
}

static int __ov2680_buf_reg_array(struct i2c_client *client,
				  struct ov2680_write_ctrl *ctrl,
				  const struct ov2680_reg *next)
{
	int size;
	u16 *data16;

	switch (next->type) {
	case OV2680_8BIT:
		size = 1;
		ctrl->buffer.data[ctrl->index] = (u8)next->val;
		break;
	case OV2680_16BIT:
		size = 2;
		data16 = (u16 *)&ctrl->buffer.data[ctrl->index];
		*data16 = cpu_to_be16((u16)next->val);
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
	if (ctrl->index + sizeof(u16) >= OV2680_MAX_WRITE_BUF_SIZE)
		return __ov2680_flush_reg_array(client, ctrl);

	return 0;
}

static int __ov2680_write_reg_is_consecutive(struct i2c_client *client,
					     struct ov2680_write_ctrl *ctrl,
					     const struct ov2680_reg *next)
{
	if (ctrl->index == 0)
		return 1;

	return ctrl->buffer.addr + ctrl->index == next->reg;
}

static int ov2680_write_reg_array(struct i2c_client *client,
				  const struct ov2680_reg *reglist)
{
	const struct ov2680_reg *next = reglist;
	struct ov2680_write_ctrl ctrl;
	int err;
	dev_dbg(&client->dev,  "++++write reg array\n");
	ctrl.index = 0;
	for (; next->type != OV2680_TOK_TERM; next++) {
		switch (next->type & OV2680_TOK_MASK) {
		case OV2680_TOK_DELAY:
			err = __ov2680_flush_reg_array(client, &ctrl);
			if (err)
				return err;
			msleep(next->val);
			break;
		default:
			/*
			 * If next address is not consecutive, data needs to be
			 * flushed before proceed.
			 */
			 dev_dbg(&client->dev,  "+++ov2680_write_reg_array reg=%x->%x\n", next->reg,next->val);
			if (!__ov2680_write_reg_is_consecutive(client, &ctrl,
								next)) {
				err = __ov2680_flush_reg_array(client, &ctrl);
			if (err)
				return err;
			}
			err = __ov2680_buf_reg_array(client, &ctrl, next);
			if (err) {
				dev_err(&client->dev, "%s: write error, aborted\n",
					 __func__);
				return err;
			}
			break;
		}
	}

	return __ov2680_flush_reg_array(client, &ctrl);
}
static int ov2680_g_focal(struct v4l2_subdev *sd, s32 *val)
{

	*val = (OV2680_FOCAL_LENGTH_NUM << 16) | OV2680_FOCAL_LENGTH_DEM;
	return 0;
}

static int ov2680_g_fnumber(struct v4l2_subdev *sd, s32 *val)
{
	/*const f number for ov2680*/

	*val = (OV2680_F_NUMBER_DEFAULT_NUM << 16) | OV2680_F_NUMBER_DEM;
	return 0;
}

static int ov2680_g_fnumber_range(struct v4l2_subdev *sd, s32 *val)
{
	
	*val = (OV2680_F_NUMBER_DEFAULT_NUM << 24) |
		(OV2680_F_NUMBER_DEM << 16) |
		(OV2680_F_NUMBER_DEFAULT_NUM << 8) | OV2680_F_NUMBER_DEM;
	return 0;
}

static int ov2680_g_bin_factor_x(struct v4l2_subdev *sd, s32 *val)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_dbg(&client->dev,  "++++ov2680_g_bin_factor_x\n");
	*val = ov2680_res[dev->fmt_idx].bin_factor_x;

	return 0;
}

static int ov2680_g_bin_factor_y(struct v4l2_subdev *sd, s32 *val)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	
	*val = ov2680_res[dev->fmt_idx].bin_factor_y;
	dev_dbg(&client->dev,  "++++ov2680_g_bin_factor_y\n");
	return 0;
}


static int ov2680_get_intg_factor(struct i2c_client *client,
				struct camera_mipi_info *info,
				const struct ov2680_resolution *res)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct atomisp_sensor_mode_data *buf = &info->data;
	unsigned int pix_clk_freq_hz;
	u16 reg_val;
	int ret;
	dev_dbg(&client->dev,  "++++ov2680_get_intg_factor\n");
	if (!info)
		return -EINVAL;

	/* pixel clock */
	pix_clk_freq_hz = res->pix_clk_freq * 1000000;

	dev->vt_pix_clk_freq_mhz = pix_clk_freq_hz;
	buf->vt_pix_clk_freq_mhz = pix_clk_freq_hz;

	/* get integration time */
	buf->coarse_integration_time_min = OV2680_COARSE_INTG_TIME_MIN;
	buf->coarse_integration_time_max_margin =
					OV2680_COARSE_INTG_TIME_MAX_MARGIN;

	buf->fine_integration_time_min = OV2680_FINE_INTG_TIME_MIN;
	buf->fine_integration_time_max_margin =
					OV2680_FINE_INTG_TIME_MAX_MARGIN;

	buf->fine_integration_time_def = OV2680_FINE_INTG_TIME_MIN;
	buf->frame_length_lines = res->lines_per_frame;
	buf->line_length_pck = res->pixels_per_line;
	buf->read_mode = res->bin_mode;

	/* get the cropping and output resolution to ISP for this mode. */
	ret =  ov2680_read_reg(client, OV2680_16BIT,
					OV2680_HORIZONTAL_START_H, &reg_val);
	if (ret)
		return ret;
	buf->crop_horizontal_start = reg_val;

	ret =  ov2680_read_reg(client, OV2680_16BIT,
					OV2680_VERTICAL_START_H, &reg_val);
	if (ret)
		return ret;
	buf->crop_vertical_start = reg_val;

	ret = ov2680_read_reg(client, OV2680_16BIT,
					OV2680_HORIZONTAL_END_H, &reg_val);
	if (ret)
		return ret;
	buf->crop_horizontal_end = reg_val;

	ret = ov2680_read_reg(client, OV2680_16BIT,
					OV2680_VERTICAL_END_H, &reg_val);
	if (ret)
		return ret;
	buf->crop_vertical_end = reg_val;

	ret = ov2680_read_reg(client, OV2680_16BIT,
					OV2680_HORIZONTAL_OUTPUT_SIZE_H, &reg_val);
	if (ret)
		return ret;
	buf->output_width = reg_val;

	ret = ov2680_read_reg(client, OV2680_16BIT,
					OV2680_VERTICAL_OUTPUT_SIZE_H, &reg_val);
	if (ret)
		return ret;
	buf->output_height = reg_val;

	buf->binning_factor_x = res->bin_factor_x ?
					(res->bin_factor_x * 2) : 1;
	buf->binning_factor_y = res->bin_factor_y ?
					(res->bin_factor_y * 2) : 1;
	return 0;
}

static long __ov2680_set_exposure(struct v4l2_subdev *sd, int coarse_itg,
				 int gain, int digitgain)

{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	u16 vts,hts;
	int ret,exp_val;
	
       dev_dbg(&client->dev, "+++++++__ov2680_set_exposure coarse_itg %d, gain %d, digitgain %d++\n",coarse_itg, gain, digitgain);

	hts = ov2680_res[dev->fmt_idx].pixels_per_line;
	vts = ov2680_res[dev->fmt_idx].lines_per_frame;

	/* group hold */
	ret = ov2680_write_reg(client, OV2680_8BIT,
                                       OV2680_GROUP_ACCESS, 0x00);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_GROUP_ACCESS);
		return ret;
	}

	/* Increase the VTS to match exposure + MARGIN */
	if (coarse_itg > vts - OV2680_INTEGRATION_TIME_MARGIN)
		vts = (u16) coarse_itg + OV2680_INTEGRATION_TIME_MARGIN;

	ret = ov2680_write_reg(client, OV2680_16BIT, OV2680_TIMING_VTS_H, vts);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_TIMING_VTS_H);
		return ret;
	}

	/* set exposure */

	/* Lower four bit should be 0*/
	exp_val = coarse_itg << 4;
	ret = ov2680_write_reg(client, OV2680_8BIT,
			       OV2680_EXPOSURE_L, exp_val & 0xFF);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_EXPOSURE_L);
		return ret;
	}

	ret = ov2680_write_reg(client, OV2680_8BIT,
			       OV2680_EXPOSURE_M, (exp_val >> 8) & 0xFF);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_EXPOSURE_M);
		return ret;
	}

	ret = ov2680_write_reg(client, OV2680_8BIT,
			       OV2680_EXPOSURE_H, (exp_val >> 16) & 0x0F);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_EXPOSURE_H);
		return ret;
	}

	/* Analog gain */
	ret = ov2680_write_reg(client, OV2680_16BIT, OV2680_AGC_H, gain);
	if (ret) {
		dev_err(&client->dev, "%s: write %x error, aborted\n",
			__func__, OV2680_AGC_H);
		return ret;
	}
	/* Digital gain */
	if (digitgain) {
		ret = ov2680_write_reg(client, OV2680_16BIT,
				OV2680_MWB_RED_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev, "%s: write %x error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}

		ret = ov2680_write_reg(client, OV2680_16BIT,
				OV2680_MWB_GREEN_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev, "%s: write %x error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}

		ret = ov2680_write_reg(client, OV2680_16BIT,
				OV2680_MWB_BLUE_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev, "%s: write %x error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}
	}

	/* End group */
	ret = ov2680_write_reg(client, OV2680_8BIT,
			       OV2680_GROUP_ACCESS, 0x10);
	if (ret)
		return ret;

	/* Delay launch group */
	ret = ov2680_write_reg(client, OV2680_8BIT,
					   OV2680_GROUP_ACCESS, 0xa0);
	if (ret)
		return ret;
	return ret;
}

static int ov2680_set_exposure(struct v4l2_subdev *sd, int exposure,
	int gain, int digitgain)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	ret = __ov2680_set_exposure(sd, exposure, gain, digitgain);
	mutex_unlock(&dev->input_lock);

	return ret;
}

static long ov2680_s_exposure(struct v4l2_subdev *sd,
			       struct atomisp_exposure *exposure)
{
	u16 coarse_itg = exposure->integration_time[0];
	u16 analog_gain = exposure->gain[0];
	u16 digital_gain = exposure->gain[1];

	/* we should not accept the invalid value below */
	if (analog_gain == 0) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l2_err(client, "%s: invalid value\n", __func__);
		return -EINVAL;
	}

	// EXPOSURE CONTROL DISABLED FOR INITIAL CHECKIN, TUNING DOESN'T WORK
	return ov2680_set_exposure(sd, coarse_itg, analog_gain, digital_gain);
}





static long ov2680_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{

	switch (cmd) {
	case ATOMISP_IOC_S_EXPOSURE:
		return ov2680_s_exposure(sd, arg);
	
	default:
		return -EINVAL;
	}
	return 0;
}

/* This returns the exposure time being used. This should only be used
 * for filling in EXIF data, not for actual image processing.
 */
static int ov2680_q_exposure(struct v4l2_subdev *sd, s32 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 reg_v, reg_v2;
	int ret;

	/* get exposure */
	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_EXPOSURE_L,
					&reg_v);
	if (ret)
		goto err;

	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_EXPOSURE_M,
					&reg_v2);
	if (ret)
		goto err;

	reg_v += reg_v2 << 8;
	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_EXPOSURE_H,
					&reg_v2);
	if (ret)
		goto err;

	*value = reg_v + (((u32)reg_v2 << 16));
err:
	return ret;
}

static u32 ov2680_translate_bayer_order(enum atomisp_bayer_order code)
{
	switch (code) {
	case atomisp_bayer_order_rggb:
		return MEDIA_BUS_FMT_SRGGB10_1X10;
	case atomisp_bayer_order_grbg:
		return MEDIA_BUS_FMT_SGRBG10_1X10;
	case atomisp_bayer_order_bggr:
		return MEDIA_BUS_FMT_SBGGR10_1X10;
	case atomisp_bayer_order_gbrg:
		return MEDIA_BUS_FMT_SGBRG10_1X10;
	}
	return 0;
}

static int ov2680_v_flip(struct v4l2_subdev *sd, s32 value)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct camera_mipi_info *ov2680_info = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val;
	u8 index;
	dev_dbg(&client->dev, "@%s: value:%d\n", __func__, value);
	ret = ov2680_read_reg(client, OV2680_8BIT, OV2680_FLIP_REG, &val);
	if (ret)
		return ret;
	if (value) {
		val |= OV2680_FLIP_MIRROR_BIT_ENABLE;
	} else {
		val &= ~OV2680_FLIP_MIRROR_BIT_ENABLE;
	}
	ret = ov2680_write_reg(client, OV2680_8BIT,
			OV2680_FLIP_REG, val);
	if (ret)
		return ret;
	index = (v_flag>0?OV2680_FLIP_BIT:0) | (h_flag>0?OV2680_MIRROR_BIT:0);
	ov2680_info = v4l2_get_subdev_hostdata(sd);
	if (ov2680_info) {
		ov2680_info->raw_bayer_order = ov2680_bayer_order_mapping[index];
		dev->format.code = ov2680_translate_bayer_order(
			ov2680_info->raw_bayer_order);
	}
	return ret;
}

static int ov2680_h_flip(struct v4l2_subdev *sd, s32 value)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct camera_mipi_info *ov2680_info = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val;
	u8 index;
	dev_dbg(&client->dev, "@%s: value:%d\n", __func__, value);

	ret = ov2680_read_reg(client, OV2680_8BIT, OV2680_MIRROR_REG, &val);
	if (ret)
		return ret;
	if (value) {
		val |= OV2680_FLIP_MIRROR_BIT_ENABLE;
	} else {
		val &= ~OV2680_FLIP_MIRROR_BIT_ENABLE;
	}
	ret = ov2680_write_reg(client, OV2680_8BIT,
			OV2680_MIRROR_REG, val);
	if (ret)
		return ret;
	index = (v_flag>0?OV2680_FLIP_BIT:0) | (h_flag>0?OV2680_MIRROR_BIT:0);
	ov2680_info = v4l2_get_subdev_hostdata(sd);
	if (ov2680_info) {
		ov2680_info->raw_bayer_order = ov2680_bayer_order_mapping[index];
		dev->format.code = ov2680_translate_bayer_order(
			ov2680_info->raw_bayer_order);
	}
	return ret;
}

static int ov2680_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2680_device *dev =
	    container_of(ctrl->handler, struct ov2680_device, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		dev_dbg(&client->dev, "%s: CID_VFLIP:%d.\n",
			__func__, ctrl->val);
		ret = ov2680_v_flip(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&client->dev, "%s: CID_HFLIP:%d.\n",
			__func__, ctrl->val);
		ret = ov2680_h_flip(&dev->sd, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int ov2680_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2680_device *dev =
	    container_of(ctrl->handler, struct ov2680_device, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = ov2680_q_exposure(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FOCAL_ABSOLUTE:
		ret = ov2680_g_focal(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_ABSOLUTE:
		ret = ov2680_g_fnumber(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_RANGE:
		ret = ov2680_g_fnumber_range(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_HORZ:
		ret = ov2680_g_bin_factor_x(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_VERT:
		ret = ov2680_g_bin_factor_y(&dev->sd, &ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = ov2680_s_ctrl,
	.g_volatile_ctrl = ov2680_g_volatile_ctrl
};

struct v4l2_ctrl_config ov2680_controls[] = {
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
	 .id = V4L2_CID_FOCAL_ABSOLUTE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "focal length",
	 .min = OV2680_FOCAL_LENGTH_DEFAULT,
	 .max = OV2680_FOCAL_LENGTH_DEFAULT,
	 .step = 0x01,
	 .def = OV2680_FOCAL_LENGTH_DEFAULT,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FNUMBER_ABSOLUTE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "f-number",
	 .min = OV2680_F_NUMBER_DEFAULT,
	 .max = OV2680_F_NUMBER_DEFAULT,
	 .step = 0x01,
	 .def = OV2680_F_NUMBER_DEFAULT,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FNUMBER_RANGE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "f-number range",
	 .min = OV2680_F_NUMBER_RANGE,
	 .max = OV2680_F_NUMBER_RANGE,
	 .step = 0x01,
	 .def = OV2680_F_NUMBER_RANGE,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_BIN_FACTOR_HORZ,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "horizontal binning factor",
	 .min = 0,
	 .max = OV2680_BIN_FACTOR_MAX,
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
	 .max = OV2680_BIN_FACTOR_MAX,
	 .step = 1,
	 .def = 0,
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
};

static int ov2680_init_registers(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = ov2680_write_reg(client, OV2680_8BIT, OV2680_SW_RESET, 0x01);
	ret |= ov2680_write_reg_array(client, ov2680_global_setting);

	return ret;
}

static int ov2680_init(struct v4l2_subdev *sd)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	int ret;

	mutex_lock(&dev->input_lock);

	/* restore settings */
	ov2680_res = ov2680_res_preview;
	N_RES = N_RES_PREVIEW;

	ret = ov2680_init_registers(sd);

	mutex_unlock(&dev->input_lock);

	return ret;
}

static int power_ctrl(struct v4l2_subdev *sd, bool flag)
{
	int ret = 0;
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	if (!dev || !dev->platform_data)
		return -ENODEV;

	/* Non-gmin platforms use the legacy callback */
	if (dev->platform_data->power_ctrl)
		return dev->platform_data->power_ctrl(sd, flag);

	if (flag) {
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
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	if (!dev || !dev->platform_data)
		return -ENODEV;

	/* Non-gmin platforms use the legacy callback */
	if (dev->platform_data->gpio_ctrl)
		return dev->platform_data->gpio_ctrl(sd, flag);

	/* The OV2680 documents only one GPIO input (#XSHUTDN), but
	 * existing integrations often wire two (reset/power_down)
	 * because that is the way other sensors work.  There is no
	 * way to tell how it is wired internally, so existing
	 * firmwares expose both and we drive them symmetrically. */
	if (flag) {
		ret = dev->platform_data->gpio0_ctrl(sd, 1);
		usleep_range(10000, 15000);
		/* Ignore return from second gpio, it may not be there */
		dev->platform_data->gpio1_ctrl(sd, 1);
		usleep_range(10000, 15000);
	} else {
		dev->platform_data->gpio1_ctrl(sd, 0);
		ret = dev->platform_data->gpio0_ctrl(sd, 0);
	}
	return ret;
}

static int power_up(struct v4l2_subdev *sd)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (!dev->platform_data) {
		dev_err(&client->dev,
			"no camera_sensor_platform_data");
		return -ENODEV;
	}

	/* power control */
	ret = power_ctrl(sd, 1);
	if (ret)
		goto fail_power;

	/* according to DS, at least 5ms is needed between DOVDD and PWDN */
	usleep_range(5000, 6000);

	/* gpio ctrl */
	ret = gpio_ctrl(sd, 1);
	if (ret) {
		ret = gpio_ctrl(sd, 1);
		if (ret)
			goto fail_power;
	}

	/* flis clock control */
	ret = dev->platform_data->flisclk_ctrl(sd, 1);
	if (ret)
		goto fail_clk;

	/* according to DS, 20ms is needed between PWDN and i2c access */
	msleep(20);

	return 0;

fail_clk:
	gpio_ctrl(sd, 0);
fail_power:
	power_ctrl(sd, 0);
	dev_err(&client->dev, "sensor power-up failed\n");

	return ret;
}

static int power_down(struct v4l2_subdev *sd)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	h_flag = 0;
	v_flag = 0;
	if (!dev->platform_data) {
		dev_err(&client->dev,
			"no camera_sensor_platform_data");
		return -ENODEV;
	}

	ret = dev->platform_data->flisclk_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "flisclk failed\n");

	/* gpio ctrl */
	ret = gpio_ctrl(sd, 0);
	if (ret) {
		ret = gpio_ctrl(sd, 0);
		if (ret)
			dev_err(&client->dev, "gpio failed 2\n");
	}

	/* power control */
	ret = power_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "vprog failed.\n");

	return ret;
}

static int ov2680_s_power(struct v4l2_subdev *sd, int on)
{
	int ret;

	if (on == 0){
		ret = power_down(sd);
	} else {
		ret = power_up(sd);	
		if (!ret)
			return ov2680_init(sd);
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
#define LARGEST_ALLOWED_RATIO_MISMATCH 600
static int distance(struct ov2680_resolution *res, u32 w, u32 h)
{
	unsigned int w_ratio = (res->width << 13) / w;
	unsigned int h_ratio;
	int match;

	if (h == 0)
		return -1;
	h_ratio = (res->height << 13) / h;
	if (h_ratio == 0)
		return -1;
	match   = abs(((w_ratio << 13) / h_ratio) - ((int)8192));


	if ((w_ratio < (int)8192) || (h_ratio < (int)8192)  ||
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
	struct ov2680_resolution *tmp_res = NULL;

	for (i = 0; i < N_RES; i++) {
		tmp_res = &ov2680_res[i];
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
		if (w != ov2680_res[i].width)
			continue;
		if (h != ov2680_res[i].height)
			continue;

		return i;
	}

	return -1;
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *ov2680_info = NULL;
	int ret = 0;
	int idx = 0;
	dev_dbg(&client->dev, "+++++ov2680_s_mbus_fmt+++++l\n");
	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	ov2680_info = v4l2_get_subdev_hostdata(sd);
	if (!ov2680_info)
		return -EINVAL;

	mutex_lock(&dev->input_lock);
	idx = nearest_resolution_index(fmt->width, fmt->height);
	if (idx == -1) {
		/* return the largest resolution */
		fmt->width = ov2680_res[N_RES - 1].width;
		fmt->height = ov2680_res[N_RES - 1].height;
	} else {
		fmt->width = ov2680_res[idx].width;
		fmt->height = ov2680_res[idx].height;
	}
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *fmt;
		mutex_unlock(&dev->input_lock);
		return 0;
		}
	dev->fmt_idx = get_resolution_index(fmt->width, fmt->height);
	dev_dbg(&client->dev, "+++++get_resolution_index=%d+++++l\n",
		     dev->fmt_idx);
	if (dev->fmt_idx == -1) {
		dev_err(&client->dev, "get resolution fail\n");
		mutex_unlock(&dev->input_lock);
		return -EINVAL;
	}
	v4l2_info(client, "__s_mbus_fmt i=%d, w=%d, h=%d\n", dev->fmt_idx,
		  fmt->width, fmt->height);
	dev_dbg(&client->dev, "__s_mbus_fmt i=%d, w=%d, h=%d\n",
		     dev->fmt_idx, fmt->width, fmt->height);

	ret = ov2680_write_reg_array(client, ov2680_res[dev->fmt_idx].regs);
	if (ret)
		dev_err(&client->dev, "ov2680 write resolution register err\n");

	ret = ov2680_get_intg_factor(client, ov2680_info,
				     &ov2680_res[dev->fmt_idx]);
	if (ret) {
		dev_err(&client->dev, "failed to get integration_factor\n");
		goto err;
	}

	/*recall flip functions to avoid flip registers
	 * were overridden by default setting
	 */
	if (h_flag)
		ov2680_h_flip(sd, h_flag);
	if (v_flag)
		ov2680_v_flip(sd, v_flag);

	v4l2_info(client, "\n%s idx %d \n", __func__, dev->fmt_idx);

	/*ret = startup(sd);
	 * if (ret)
	 * dev_err(&client->dev, "ov2680 startup err\n");
	 */
err:
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	fmt->width = ov2680_res[dev->fmt_idx].width;
	fmt->height = ov2680_res[dev->fmt_idx].height;
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov2680_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	u16 high, low;
	int ret;
	u16 id;
	u8 revision;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_SC_CMMN_CHIP_ID_H, &high);
	if (ret) {
		dev_err(&client->dev, "sensor_id_high = 0x%x\n", high);
		return -ENODEV;
	}
	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_SC_CMMN_CHIP_ID_L, &low);
	id = ((((u16) high) << 8) | (u16) low);

	if (id != OV2680_ID) {
		dev_err(&client->dev, "sensor ID error 0x%x\n", id);
		return -ENODEV;
	}

	ret = ov2680_read_reg(client, OV2680_8BIT,
					OV2680_SC_CMMN_SUB_ID, &high);
	revision = (u8) high & 0x0f;

	dev_info(&client->dev, "sensor_revision id = 0x%x\n", id);

	return 0;
}

static int ov2680_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	if(enable )
		dev_dbg(&client->dev, "ov2680_s_stream one \n");
	else
		dev_dbg(&client->dev, "ov2680_s_stream off \n");
	
	ret = ov2680_write_reg(client, OV2680_8BIT, OV2680_SW_STREAM,
				enable ? OV2680_START_STREAMING :
				OV2680_STOP_STREAMING);
#if 0
	/* restore settings */
	ov2680_res = ov2680_res_preview;
	N_RES = N_RES_PREVIEW;
#endif

	//otp valid at stream on state
	//if(!dev->otp_data)
	//	dev->otp_data = ov2680_otp_read(sd);

	mutex_unlock(&dev->input_lock);

	return ret;
}


static int ov2680_s_config(struct v4l2_subdev *sd,
			   int irq, void *platform_data)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

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
		dev_err(&client->dev, "ov2680 power-off err.\n");
		goto fail_power_off;
	}

	ret = power_up(sd);
	if (ret) {
		dev_err(&client->dev, "ov2680 power-up err.\n");
		goto fail_power_on;
	}

	ret = dev->platform_data->csi_cfg(sd, 1);
	if (ret)
		goto fail_csi_cfg;

	/* config & detect sensor */
	ret = ov2680_detect(client);
	if (ret) {
		dev_err(&client->dev, "ov2680_detect err s_config.\n");
		goto fail_csi_cfg;
	}
	
	/* turn off sensor, after probed */
	ret = power_down(sd);
	if (ret) {
		dev_err(&client->dev, "ov2680 power-off err.\n");
		goto fail_csi_cfg;
	}
	mutex_unlock(&dev->input_lock);

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

static int ov2680_g_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!param)
		return -EINVAL;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		dev_err(&client->dev,  "unsupported buffer type.\n");
		return -EINVAL;
	}

	memset(param, 0, sizeof(*param));
	param->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (dev->fmt_idx >= 0 && dev->fmt_idx < N_RES) {
		param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
		param->parm.capture.timeperframe.numerator = 1;
		param->parm.capture.capturemode = dev->run_mode;
		param->parm.capture.timeperframe.denominator =
			ov2680_res[dev->fmt_idx].fps;
	}
	return 0;
}

static int ov2680_s_parm(struct v4l2_subdev *sd,
			struct v4l2_streamparm *param)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev->run_mode = param->parm.capture.capturemode;

	v4l2_info(client, "\n%s:run_mode :%x\n", __func__, dev->run_mode);

	mutex_lock(&dev->input_lock);
	switch (dev->run_mode) {
	case CI_MODE_VIDEO:
		ov2680_res = ov2680_res_video;
		N_RES = N_RES_VIDEO;
		break;
	case CI_MODE_STILL_CAPTURE:
		ov2680_res = ov2680_res_still;
		N_RES = N_RES_STILL;
		break;
	default:
		ov2680_res = ov2680_res_preview;
		N_RES = N_RES_PREVIEW;
	}
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int ov2680_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	interval->interval.numerator = 1;
	interval->interval.denominator = ov2680_res[dev->fmt_idx].fps;

	return 0;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= MAX_FMTS)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int ov2680_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= N_RES)
		return -EINVAL;

	fse->min_width = ov2680_res[index].width;
	fse->min_height = ov2680_res[index].height;
	fse->max_width = ov2680_res[index].width;
	fse->max_height = ov2680_res[index].height;

	return 0;

}

static int ov2680_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	
	mutex_lock(&dev->input_lock);
	*frames = ov2680_res[dev->fmt_idx].skip_frames;
	mutex_unlock(&dev->input_lock);

	return 0;
}

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.s_stream = ov2680_s_stream,
	.g_parm = ov2680_g_parm,
	.s_parm = ov2680_s_parm,
	.g_frame_interval = ov2680_g_frame_interval,
};

static const struct v4l2_subdev_sensor_ops ov2680_sensor_ops = {
		.g_skip_frames	= ov2680_g_skip_frames,
};

static const struct v4l2_subdev_core_ops ov2680_core_ops = {
	.s_power = ov2680_s_power,
	.ioctl = ov2680_ioctl,
};

static const struct v4l2_subdev_pad_ops ov2680_pad_ops = {
	.enum_mbus_code = ov2680_enum_mbus_code,
	.enum_frame_size = ov2680_enum_frame_size,
	.get_fmt = ov2680_get_fmt,
	.set_fmt = ov2680_set_fmt,
};

static const struct v4l2_subdev_ops ov2680_ops = {
	.core = &ov2680_core_ops,
	.video = &ov2680_video_ops,
	.pad = &ov2680_pad_ops,
	.sensor = &ov2680_sensor_ops,
};

static int ov2680_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	dev_dbg(&client->dev, "ov2680_remove...\n");

	dev->platform_data->csi_cfg(sd, 0);

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	kfree(dev);

	return 0;
}

static int ov2680_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ov2680_device *dev;
	int ret;
	void *pdata;
	unsigned int i;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&client->dev, "out of memory\n");
		return -ENOMEM;
	}

	mutex_init(&dev->input_lock);

	dev->fmt_idx = 0;
	v4l2_i2c_subdev_init(&(dev->sd), client, &ov2680_ops);

	if (ACPI_COMPANION(&client->dev))
		pdata = gmin_camera_platform_data(&dev->sd,
						  ATOMISP_INPUT_FORMAT_RAW_10,
						  atomisp_bayer_order_bggr);
	else
		pdata = client->dev.platform_data;

	if (!pdata) {
		ret = -EINVAL;
		goto out_free;
        }

	ret = ov2680_s_config(&dev->sd, client->irq, pdata);
	if (ret)
		goto out_free;

	ret = atomisp_register_i2c_module(&dev->sd, pdata, RAW_CAMERA);
	if (ret)
		goto out_free;

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret =
	    v4l2_ctrl_handler_init(&dev->ctrl_handler,
				   ARRAY_SIZE(ov2680_controls));
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(ov2680_controls); i++)
		v4l2_ctrl_new_custom(&dev->ctrl_handler, &ov2680_controls[i],
				     NULL);

	if (dev->ctrl_handler.error) {
		ov2680_remove(client);
		return dev->ctrl_handler.error;
	}

	/* Use same lock for controls as for everything else. */
	dev->ctrl_handler.lock = &dev->input_lock;
	dev->sd.ctrl_handler = &dev->ctrl_handler;

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret)
	{
		ov2680_remove(client);
		dev_dbg(&client->dev, "+++ remove ov2680 \n");
	}
	return ret;
out_free:
	dev_dbg(&client->dev, "+++ out free \n");
	v4l2_device_unregister_subdev(&dev->sd);
	kfree(dev);
	return ret;
}

static const struct acpi_device_id ov2680_acpi_match[] = {
	{"XXOV2680"},
	{"OVTI2680"},
	{},
};
MODULE_DEVICE_TABLE(acpi, ov2680_acpi_match);


MODULE_DEVICE_TABLE(i2c, ov2680_id);
static struct i2c_driver ov2680_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = OV2680_NAME,
		.acpi_match_table = ACPI_PTR(ov2680_acpi_match),

	},
	.probe = ov2680_probe,
	.remove = ov2680_remove,
	.id_table = ov2680_id,
};

static int init_ov2680(void)
{
	return i2c_add_driver(&ov2680_driver);
}

static void exit_ov2680(void)
{

	i2c_del_driver(&ov2680_driver);
}

module_init(init_ov2680);
module_exit(exit_ov2680);

MODULE_AUTHOR("Jacky Wang <Jacky_wang@ovt.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision 2680 sensors");
MODULE_LICENSE("GPL");

