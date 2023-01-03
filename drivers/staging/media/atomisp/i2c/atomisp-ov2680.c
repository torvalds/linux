// SPDX-License-Identifier: GPL-2.0
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

#include <asm/unaligned.h>

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
#include <media/ov_16bit_addr_reg_helpers.h>
#include <media/v4l2-device.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include "../include/linux/atomisp_gmin_platform.h"

#include "ov2680.h"

static enum atomisp_bayer_order ov2680_bayer_order_mapping[] = {
	atomisp_bayer_order_bggr,
	atomisp_bayer_order_grbg,
	atomisp_bayer_order_gbrg,
	atomisp_bayer_order_rggb,
};

static int ov2680_write_reg_array(struct i2c_client *client,
				  const struct ov2680_reg *reglist)
{
	const struct ov2680_reg *next = reglist;
	int ret;

	for (; next->reg != 0; next++) {
		ret = ov_write_reg8(client, next->reg, next->val);
		if (ret)
			return ret;
	}

	return 0;
}

static long __ov2680_set_exposure(struct v4l2_subdev *sd, int coarse_itg,
				  int gain, int digitgain)

{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	u16 vts;
	int ret, exp_val;

	dev_dbg(&client->dev,
		"+++++++__ov2680_set_exposure coarse_itg %d, gain %d, digitgain %d++\n",
		coarse_itg, gain, digitgain);

	vts = dev->res->lines_per_frame;

	/* group hold */
	ret = ov_write_reg8(client, OV2680_GROUP_ACCESS, 0x00);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_GROUP_ACCESS);
		return ret;
	}

	/* Increase the VTS to match exposure + MARGIN */
	if (coarse_itg > vts - OV2680_INTEGRATION_TIME_MARGIN)
		vts = (u16)coarse_itg + OV2680_INTEGRATION_TIME_MARGIN;

	ret = ov_write_reg16(client, OV2680_TIMING_VTS_H, vts);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_TIMING_VTS_H);
		return ret;
	}

	/* set exposure */

	/* Lower four bit should be 0*/
	exp_val = coarse_itg << 4;
	ret = ov_write_reg8(client, OV2680_EXPOSURE_L, exp_val & 0xFF);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_EXPOSURE_L);
		return ret;
	}

	ret = ov_write_reg8(client, OV2680_EXPOSURE_M, (exp_val >> 8) & 0xFF);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_EXPOSURE_M);
		return ret;
	}

	ret = ov_write_reg8(client, OV2680_EXPOSURE_H, (exp_val >> 16) & 0x0F);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_EXPOSURE_H);
		return ret;
	}

	/* Analog gain */
	ret = ov_write_reg16(client, OV2680_AGC_H, gain);
	if (ret) {
		dev_err(&client->dev, "%s: write 0x%02x: error, aborted\n",
			__func__, OV2680_AGC_H);
		return ret;
	}
	/* Digital gain */
	if (digitgain) {
		ret = ov_write_reg16(client, OV2680_MWB_RED_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev,
				"%s: write 0x%02x: error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}

		ret = ov_write_reg16(client, OV2680_MWB_GREEN_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev,
				"%s: write 0x%02x: error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}

		ret = ov_write_reg16(client, OV2680_MWB_BLUE_GAIN_H, digitgain);
		if (ret) {
			dev_err(&client->dev,
				"%s: write 0x%02x: error, aborted\n",
				__func__, OV2680_MWB_RED_GAIN_H);
			return ret;
		}
	}

	/* End group */
	ret = ov_write_reg8(client, OV2680_GROUP_ACCESS, 0x10);
	if (ret)
		return ret;

	/* Delay launch group */
	ret = ov_write_reg8(client, OV2680_GROUP_ACCESS, 0xa0);
	if (ret)
		return ret;
	return ret;
}

static int ov2680_set_exposure(struct v4l2_subdev *sd, int exposure,
			       int gain, int digitgain)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	int ret = 0;

	mutex_lock(&dev->input_lock);

	dev->exposure = exposure;
	dev->gain = gain;
	dev->digitgain = digitgain;

	if (dev->power_on)
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

/*
 * This returns the exposure time being used. This should only be used
 * for filling in EXIF data, not for actual image processing.
 */
static int ov2680_q_exposure(struct v4l2_subdev *sd, s32 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 reg_val;
	int ret;

	/* get exposure */
	ret = ov_read_reg24(client, OV2680_EXPOSURE_H, &reg_val);
	if (ret)
		return ret;

	/* Lower four bits are not part of the exposure val (always 0) */
	*value = reg_val >> 4;
	return 0;
}

static void ov2680_set_bayer_order(struct ov2680_device *sensor, struct v4l2_mbus_framefmt *fmt)
{
	static const int ov2680_hv_flip_bayer_order[] = {
		MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
	};
	struct camera_mipi_info *ov2680_info;
	int hv_flip = 0;

	if (sensor->ctrls.vflip->val)
		hv_flip += 1;

	if (sensor->ctrls.hflip->val)
		hv_flip += 2;

	fmt->code = ov2680_hv_flip_bayer_order[hv_flip];

	/* TODO atomisp specific custom API, should be removed */
	ov2680_info = v4l2_get_subdev_hostdata(&sensor->sd);
	if (ov2680_info)
		ov2680_info->raw_bayer_order = ov2680_bayer_order_mapping[hv_flip];
}

static int ov2680_set_vflip(struct ov2680_device *sensor, s32 val)
{
	int ret;

	if (sensor->is_streaming)
		return -EBUSY;

	ret = ov_update_reg(sensor->client, OV2680_REG_FORMAT1, BIT(2), val ? BIT(2) : 0);
	if (ret < 0)
		return ret;

	ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
	return 0;
}

static int ov2680_set_hflip(struct ov2680_device *sensor, s32 val)
{
	int ret;

	if (sensor->is_streaming)
		return -EBUSY;

	ret = ov_update_reg(sensor->client, OV2680_REG_FORMAT2, BIT(2), val ? BIT(2) : 0);
	if (ret < 0)
		return ret;

	ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
	return 0;
}

static int ov2680_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov2680_device *sensor = to_ov2680_sensor(sd);
	int ret;

	if (!sensor->power_on) {
		ov2680_set_bayer_order(sensor, &sensor->mode.fmt);
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		ret = ov2680_set_vflip(sensor, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov2680_set_hflip(sensor, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int ov2680_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = ov2680_q_exposure(sd, &ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov2680_ctrl_ops = {
	.s_ctrl = ov2680_s_ctrl,
	.g_volatile_ctrl = ov2680_g_volatile_ctrl
};

static int ov2680_init_registers(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = ov_write_reg8(client, OV2680_SW_RESET, 0x01);
	ret |= ov2680_write_reg_array(client, ov2680_global_setting);

	return ret;
}

static int power_ctrl(struct v4l2_subdev *sd, bool flag)
{
	int ret = 0;
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!dev || !dev->platform_data)
		return -ENODEV;

	dev_dbg(&client->dev, "%s: %s", __func__, flag ? "on" : "off");

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

	/*
	 * The OV2680 documents only one GPIO input (#XSHUTDN), but
	 * existing integrations often wire two (reset/power_down)
	 * because that is the way other sensors work.  There is no
	 * way to tell how it is wired internally, so existing
	 * firmwares expose both and we drive them symmetrically.
	 */
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

	if (dev->power_on)
		return 0; /* Already on */

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

	ret = ov2680_init_registers(sd);
	if (ret)
		goto fail_init_registers;

	ret = __ov2680_set_exposure(sd, dev->exposure, dev->gain, dev->digitgain);
	if (ret)
		goto fail_init_registers;

	dev->power_on = true;
	return 0;

fail_init_registers:
	dev->platform_data->flisclk_ctrl(sd, 0);
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

	if (!dev->platform_data) {
		dev_err(&client->dev,
			"no camera_sensor_platform_data");
		return -ENODEV;
	}

	if (!dev->power_on)
		return 0; /* Already off */

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
	if (ret) {
		dev_err(&client->dev, "vprog failed.\n");
		return ret;
	}

	dev->power_on = false;
	return 0;
}

static int ov2680_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	int ret;

	mutex_lock(&dev->input_lock);

	if (on == 0) {
		ret = power_down(sd);
	} else {
		ret = power_up(sd);
	}

	mutex_unlock(&dev->input_lock);

	return ret;
}

static struct v4l2_mbus_framefmt *
__ov2680_get_pad_format(struct ov2680_device *sensor,
			struct v4l2_subdev_state *state,
			unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&sensor->sd, state, pad);

	return &sensor->mode.fmt;
}

static void ov2680_fill_format(struct ov2680_device *sensor,
			       struct v4l2_mbus_framefmt *fmt,
			       unsigned int width, unsigned int height)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = width;
	fmt->height = height;
	fmt->field = V4L2_FIELD_NONE;
	ov2680_set_bayer_order(sensor, fmt);
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;
	struct ov2680_resolution *res;
	int vts, ret = 0;

	res = v4l2_find_nearest_size(ov2680_res_preview, ARRAY_SIZE(ov2680_res_preview),
				     width, height,
				     format->format.width, format->format.height);
	if (!res)
		res = &ov2680_res_preview[N_RES_PREVIEW - 1];

	fmt = __ov2680_get_pad_format(dev, sd_state, format->pad, format->which);
	ov2680_fill_format(dev, fmt, res->width, res->height);

	format->format = *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	dev_dbg(&client->dev, "%s: %dx%d\n",
		__func__, fmt->width, fmt->height);

	mutex_lock(&dev->input_lock);

	/* s_power has not been called yet for std v4l2 clients (camorama) */
	power_up(sd);
	ret = ov2680_write_reg_array(client, dev->res->regs);
	if (ret) {
		dev_err(&client->dev,
			"ov2680 write resolution register err: %d\n", ret);
		goto err;
	}

	vts = dev->res->lines_per_frame;

	/* If necessary increase the VTS to match exposure + MARGIN */
	if (dev->exposure > vts - OV2680_INTEGRATION_TIME_MARGIN)
		vts = dev->exposure + OV2680_INTEGRATION_TIME_MARGIN;

	ret = ov_write_reg16(client, OV2680_TIMING_VTS_H, vts);
	if (ret) {
		dev_err(&client->dev, "ov2680 write vts err: %d\n", ret);
		goto err;
	}

	/*
	 * recall flip functions to avoid flip registers
	 * were overridden by default setting
	 */
	ret = __v4l2_ctrl_handler_setup(&dev->ctrls.handler);
	if (ret < 0)
		goto err;

	dev->res = res;
err:
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct v4l2_mbus_framefmt *fmt;

	fmt = __ov2680_get_pad_format(dev, sd_state, format->pad, format->which);
	format->format = *fmt;
	return 0;
}

static int ov2680_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	u32 high, low;
	int ret;
	u16 id;
	u8 revision;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = ov_read_reg8(client, OV2680_SC_CMMN_CHIP_ID_H, &high);
	if (ret) {
		dev_err(&client->dev, "sensor_id_high = 0x%x\n", high);
		return -ENODEV;
	}
	ret = ov_read_reg8(client, OV2680_SC_CMMN_CHIP_ID_L, &low);
	id = ((((u16)high) << 8) | (u16)low);

	if (id != OV2680_ID) {
		dev_err(&client->dev, "sensor ID error 0x%x\n", id);
		return -ENODEV;
	}

	ret = ov_read_reg8(client, OV2680_SC_CMMN_SUB_ID, &high);
	revision = (u8)high & 0x0f;

	dev_info(&client->dev, "sensor_revision id = 0x%x, rev= %d\n",
		 id, revision);

	return 0;
}

static int ov2680_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	if (enable)
		dev_dbg(&client->dev, "ov2680_s_stream one\n");
	else
		dev_dbg(&client->dev, "ov2680_s_stream off\n");

	ret = ov_write_reg8(client, OV2680_SW_STREAM,
				enable ? OV2680_START_STREAMING : OV2680_STOP_STREAMING);
	if (ret == 0) {
		dev->is_streaming = enable;
		v4l2_ctrl_activate(dev->ctrls.vflip, !enable);
		v4l2_ctrl_activate(dev->ctrls.hflip, !enable);
	}

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
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int ov2680_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	interval->interval.numerator = 1;
	interval->interval.denominator = dev->res->fps;

	return 0;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= MAX_FMTS)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int ov2680_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;

	if (index >= N_RES_PREVIEW)
		return -EINVAL;

	fse->min_width = ov2680_res_preview[index].width;
	fse->min_height = ov2680_res_preview[index].height;
	fse->max_width = ov2680_res_preview[index].width;
	fse->max_height = ov2680_res_preview[index].height;

	return 0;
}

static int ov2680_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct v4l2_fract fract;

	if (fie->index >= N_RES_PREVIEW ||
	    fie->width > ov2680_res_preview[0].width ||
	    fie->height > ov2680_res_preview[0].height ||
	    fie->which > V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	fract.denominator = ov2680_res_preview[fie->index].fps;
	fract.numerator = 1;

	fie->interval = fract;

	return 0;
}

static int ov2680_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	mutex_lock(&dev->input_lock);
	*frames = dev->res->skip_frames;
	mutex_unlock(&dev->input_lock);

	return 0;
}

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.s_stream = ov2680_s_stream,
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
	.enum_frame_interval = ov2680_enum_frame_interval,
	.get_fmt = ov2680_get_fmt,
	.set_fmt = ov2680_set_fmt,
};

static const struct v4l2_subdev_ops ov2680_ops = {
	.core = &ov2680_core_ops,
	.video = &ov2680_video_ops,
	.pad = &ov2680_pad_ops,
	.sensor = &ov2680_sensor_ops,
};

static int ov2680_init_controls(struct ov2680_device *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ov2680_ctrl_ops;
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;

	v4l2_ctrl_handler_init(hdl, 2);

	hdl->lock = &sensor->input_lock;

	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	ctrls->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	ctrls->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static void ov2680_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680_device *dev = to_ov2680_sensor(sd);

	dev_dbg(&client->dev, "ov2680_remove...\n");

	dev->platform_data->csi_cfg(sd, 0);

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrls.handler);
	kfree(dev);
}

static int ov2680_probe(struct i2c_client *client)
{
	struct ov2680_device *dev;
	int ret;
	void *pdata;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->input_lock);

	dev->client = client;
	dev->res = &ov2680_res_preview[0];
	dev->exposure = dev->res->lines_per_frame - OV2680_INTEGRATION_TIME_MARGIN;
	dev->gain = 250; /* 0-2047 */
	v4l2_i2c_subdev_init(&dev->sd, client, &ov2680_ops);

	pdata = gmin_camera_platform_data(&dev->sd,
					  ATOMISP_INPUT_FORMAT_RAW_10,
					  atomisp_bayer_order_bggr);
	if (!pdata) {
		ret = -EINVAL;
		goto out_free;
	}

	ret = ov2680_s_config(&dev->sd, client->irq, pdata);
	if (ret)
		goto out_free;

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = ov2680_init_controls(dev);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	ov2680_fill_format(dev, &dev->mode.fmt, OV2680_NATIVE_WIDTH, OV2680_NATIVE_HEIGHT);

	ret = atomisp_register_i2c_module(&dev->sd, pdata, RAW_CAMERA);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	return 0;
out_free:
	dev_dbg(&client->dev, "+++ out free\n");
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

static struct i2c_driver ov2680_driver = {
	.driver = {
		.name = "ov2680",
		.acpi_match_table = ov2680_acpi_match,
	},
	.probe_new = ov2680_probe,
	.remove = ov2680_remove,
};
module_i2c_driver(ov2680_driver);

MODULE_AUTHOR("Jacky Wang <Jacky_wang@ovt.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision 2680 sensors");
MODULE_LICENSE("GPL");
