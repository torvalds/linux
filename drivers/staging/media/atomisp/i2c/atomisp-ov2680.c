// SPDX-License-Identifier: GPL-2.0
/*
 * Support for OmniVision OV2680 1080p HD camera sensor.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 * Copyright (c) 2023 Hans de Goede <hdegoede@redhat.com>
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

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#include <media/ov_16bit_addr_reg_helpers.h>
#include <media/v4l2-device.h>

#include "ov2680.h"

static const struct v4l2_rect ov2680_default_crop = {
	.left = OV2680_ACTIVE_START_LEFT,
	.top = OV2680_ACTIVE_START_TOP,
	.width = OV2680_ACTIVE_WIDTH,
	.height = OV2680_ACTIVE_HEIGHT,
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

static void ov2680_set_bayer_order(struct ov2680_dev *sensor, struct v4l2_mbus_framefmt *fmt)
{
	static const int ov2680_hv_flip_bayer_order[] = {
		MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
	};
	int hv_flip = 0;

	if (sensor->ctrls.vflip->val)
		hv_flip += 1;

	if (sensor->ctrls.hflip->val)
		hv_flip += 2;

	fmt->code = ov2680_hv_flip_bayer_order[hv_flip];
}

static int ov2680_set_vflip(struct ov2680_dev *sensor, s32 val)
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

static int ov2680_set_hflip(struct ov2680_dev *sensor, s32 val)
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

static int ov2680_exposure_set(struct ov2680_dev *sensor, u32 exp)
{
	return ov_write_reg24(sensor->client, OV2680_REG_EXPOSURE_PK_HIGH, exp << 4);
}

static int ov2680_gain_set(struct ov2680_dev *sensor, u32 gain)
{
	return ov_write_reg16(sensor->client, OV2680_REG_GAIN_PK, gain);
}

static int ov2680_test_pattern_set(struct ov2680_dev *sensor, int value)
{
	int ret;

	if (!value)
		return ov_update_reg(sensor->client, OV2680_REG_ISP_CTRL00, BIT(7), 0);

	ret = ov_update_reg(sensor->client, OV2680_REG_ISP_CTRL00, 0x03, value - 1);
	if (ret < 0)
		return ret;

	ret = ov_update_reg(sensor->client, OV2680_REG_ISP_CTRL00, BIT(7), BIT(7));
	if (ret < 0)
		return ret;

	return 0;
}

static int ov2680_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);
	int ret;

	/* Only apply changes to the controls if the device is powered up */
	if (!pm_runtime_get_if_in_use(sensor->sd.dev)) {
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
	case V4L2_CID_EXPOSURE:
		ret = ov2680_exposure_set(sensor, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		ret = ov2680_gain_set(sensor, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov2680_test_pattern_set(sensor, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_put(sensor->sd.dev);
	return ret;
}

static const struct v4l2_ctrl_ops ov2680_ctrl_ops = {
	.s_ctrl = ov2680_s_ctrl,
};

static int ov2680_init_registers(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = ov_write_reg8(client, OV2680_SW_RESET, 0x01);

	/* Wait for sensor reset */
	usleep_range(1000, 2000);

	ret |= ov2680_write_reg_array(client, ov2680_global_setting);

	return ret;
}

static struct v4l2_mbus_framefmt *
__ov2680_get_pad_format(struct ov2680_dev *sensor, struct v4l2_subdev_state *state,
			unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&sensor->sd, state, pad);

	return &sensor->mode.fmt;
}

static struct v4l2_rect *
__ov2680_get_pad_crop(struct ov2680_dev *sensor, struct v4l2_subdev_state *state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&sensor->sd, state, pad);

	return &sensor->mode.crop;
}

static void ov2680_fill_format(struct ov2680_dev *sensor,
			       struct v4l2_mbus_framefmt *fmt,
			       unsigned int width, unsigned int height)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = width;
	fmt->height = height;
	fmt->field = V4L2_FIELD_NONE;
	ov2680_set_bayer_order(sensor, fmt);
}

static void ov2680_calc_mode(struct ov2680_dev *sensor)
{
	int width = sensor->mode.fmt.width;
	int height = sensor->mode.fmt.height;
	int orig_width = width;
	int orig_height = height;

	if (width  <= (sensor->mode.crop.width / 2) &&
	    height <= (sensor->mode.crop.height / 2)) {
		sensor->mode.binning = true;
		width *= 2;
		height *= 2;
	} else {
		sensor->mode.binning = false;
	}

	sensor->mode.h_start =
		(sensor->mode.crop.left + (sensor->mode.crop.width - width) / 2) & ~1;
	sensor->mode.v_start =
		(sensor->mode.crop.top + (sensor->mode.crop.height - height) / 2) & ~1;
	sensor->mode.h_end = min(sensor->mode.h_start + width + OV2680_END_MARGIN - 1,
				 OV2680_NATIVE_WIDTH - 1);
	sensor->mode.v_end = min(sensor->mode.v_start + height + OV2680_END_MARGIN - 1,
				 OV2680_NATIVE_HEIGHT - 1);
	sensor->mode.h_output_size = orig_width;
	sensor->mode.v_output_size = orig_height;
	sensor->mode.hts = OV2680_PIXELS_PER_LINE;
	sensor->mode.vts = OV2680_LINES_PER_FRAME;
}

static int ov2680_set_mode(struct ov2680_dev *sensor)
{
	struct i2c_client *client = sensor->client;
	u8 sensor_ctrl_0a, inc, fmt1, fmt2;
	int ret;

	if (sensor->mode.binning) {
		sensor_ctrl_0a = 0x23;
		inc = 0x31;
		fmt1 = 0xc2;
		fmt2 = 0x01;
	} else {
		sensor_ctrl_0a = 0x21;
		inc = 0x11;
		fmt1 = 0xc0;
		fmt2 = 0x00;
	}

	ret = ov_write_reg8(client, OV2680_REG_SENSOR_CTRL_0A, sensor_ctrl_0a);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_HORIZONTAL_START_H, sensor->mode.h_start);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_VERTICAL_START_H, sensor->mode.v_start);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_HORIZONTAL_END_H, sensor->mode.h_end);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_VERTICAL_END_H, sensor->mode.v_end);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_HORIZONTAL_OUTPUT_SIZE_H,
				 sensor->mode.h_output_size);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_VERTICAL_OUTPUT_SIZE_H,
				 sensor->mode.v_output_size);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_HTS, sensor->mode.hts);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_VTS, sensor->mode.vts);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_ISP_X_WIN, 0);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_ISP_Y_WIN, 0);
	if (ret)
		return ret;

	ret = ov_write_reg8(client, OV2680_X_INC, inc);
	if (ret)
		return ret;

	ret = ov_write_reg8(client, OV2680_Y_INC, inc);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_X_WIN, sensor->mode.h_output_size);
	if (ret)
		return ret;

	ret = ov_write_reg16(client, OV2680_Y_WIN, sensor->mode.v_output_size);
	if (ret)
		return ret;

	ret = ov_write_reg8(client, OV2680_REG_FORMAT1, fmt1);
	if (ret)
		return ret;

	ret = ov_write_reg8(client, OV2680_REG_FORMAT2, fmt2);
	if (ret)
		return ret;

	return 0;
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);
	struct v4l2_mbus_framefmt *fmt;
	const struct v4l2_rect *crop;
	unsigned int width, height;

	crop = __ov2680_get_pad_crop(sensor, sd_state, format->pad, format->which);

	/* Limit set_fmt max size to crop width / height */
	width = clamp_t(unsigned int, ALIGN(format->format.width, 2),
			OV2680_MIN_CROP_WIDTH, crop->width);
	height = clamp_t(unsigned int, ALIGN(format->format.height, 2),
			 OV2680_MIN_CROP_HEIGHT, crop->height);

	fmt = __ov2680_get_pad_format(sensor, sd_state, format->pad, format->which);
	ov2680_fill_format(sensor, fmt, width, height);

	format->format = *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	mutex_lock(&sensor->lock);
	ov2680_calc_mode(sensor);
	mutex_unlock(&sensor->lock);
	return 0;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);
	struct v4l2_mbus_framefmt *fmt;

	fmt = __ov2680_get_pad_format(sensor, sd_state, format->pad, format->which);
	format->format = *fmt;
	return 0;
}

static int ov2680_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&sensor->lock);
		sel->r = *__ov2680_get_pad_crop(sensor, state, sel->pad, sel->which);
		mutex_unlock(&sensor->lock);
		break;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV2680_NATIVE_WIDTH;
		sel->r.height = OV2680_NATIVE_HEIGHT;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = OV2680_ACTIVE_START_TOP;
		sel->r.left = OV2680_ACTIVE_START_LEFT;
		sel->r.width = OV2680_ACTIVE_WIDTH;
		sel->r.height = OV2680_ACTIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ov2680_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	/*
	 * Clamp the boundaries of the crop rectangle to the size of the sensor
	 * pixel array. Align to multiples of 2 to ensure Bayer pattern isn't
	 * disrupted.
	 */
	rect.left = clamp(ALIGN(sel->r.left, 2), OV2680_NATIVE_START_LEFT,
			  OV2680_NATIVE_WIDTH);
	rect.top = clamp(ALIGN(sel->r.top, 2), OV2680_NATIVE_START_TOP,
			 OV2680_NATIVE_HEIGHT);
	rect.width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
			     OV2680_MIN_CROP_WIDTH, OV2680_NATIVE_WIDTH);
	rect.height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
			      OV2680_MIN_CROP_HEIGHT, OV2680_NATIVE_HEIGHT);

	/* Make sure the crop rectangle isn't outside the bounds of the array */
	rect.width = min_t(unsigned int, rect.width,
			   OV2680_NATIVE_WIDTH - rect.left);
	rect.height = min_t(unsigned int, rect.height,
			    OV2680_NATIVE_HEIGHT - rect.top);

	__crop = __ov2680_get_pad_crop(sensor, state, sel->pad, sel->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/*
		 * Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		format = __ov2680_get_pad_format(sensor, state, sel->pad, sel->which);
		format->width = rect.width;
		format->height = rect.height;
	}

	*__crop = rect;
	sel->r = rect;

	return 0;
}

static int ov2680_init_cfg(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY
		: V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.width = 800,
			.height = 600,
		},
	};

	sd_state->pads[0].try_crop = ov2680_default_crop;

	return ov2680_set_fmt(sd, sd_state, &fmt);
}

static int ov2680_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	u32 high = 0, low = 0;
	int ret;
	u16 id;
	u8 revision;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = ov_read_reg8(client, OV2680_SC_CMMN_CHIP_ID_H, &high);
	if (ret) {
		dev_err(&client->dev, "sensor_id_high read failed (%d)\n", ret);
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
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->is_streaming == enable) {
		dev_warn(&client->dev, "stream already %s\n", enable ? "started" : "stopped");
		goto error_unlock;
	}

	if (enable) {
		ret = pm_runtime_get_sync(sensor->sd.dev);
		if (ret < 0)
			goto error_power_down;

		ret = ov2680_set_mode(sensor);
		if (ret)
			goto error_power_down;

		/* Restore value of all ctrls */
		ret = __v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
		if (ret)
			goto error_power_down;

		ret = ov_write_reg8(client, OV2680_SW_STREAM, OV2680_START_STREAMING);
		if (ret)
			goto error_power_down;
	} else {
		ov_write_reg8(client, OV2680_SW_STREAM, OV2680_STOP_STREAMING);
		pm_runtime_put(sensor->sd.dev);
	}

	sensor->is_streaming = enable;
	v4l2_ctrl_activate(sensor->ctrls.vflip, !enable);
	v4l2_ctrl_activate(sensor->ctrls.hflip, !enable);

	mutex_unlock(&sensor->lock);
	return 0;

error_power_down:
	pm_runtime_put(sensor->sd.dev);
	sensor->is_streaming = false;
error_unlock:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov2680_s_config(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = pm_runtime_get_sync(&client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "ov2680 power-up err.\n");
		goto fail_power_on;
	}

	/* config & detect sensor */
	ret = ov2680_detect(client);
	if (ret)
		dev_err(&client->dev, "ov2680_detect err s_config.\n");

fail_power_on:
	pm_runtime_put(&client->dev);
	return ret;
}

static int ov2680_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	interval->interval.numerator = 1;
	interval->interval.denominator = OV2680_FPS;
	return 0;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	/* We support only a single format */
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int ov2680_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	static const struct v4l2_frmsize_discrete ov2680_frame_sizes[] = {
		{ 1616, 1216 },
		{ 1616, 1096 },
		{ 1616,  916 },
		{ 1456, 1096 },
		{ 1296,  976 },
		{ 1296,  736 },
		{  784,  592 },
		{  656,  496 },
	};
	int index = fse->index;

	if (index >= ARRAY_SIZE(ov2680_frame_sizes))
		return -EINVAL;

	fse->min_width = ov2680_frame_sizes[index].width;
	fse->min_height = ov2680_frame_sizes[index].height;
	fse->max_width = ov2680_frame_sizes[index].width;
	fse->max_height = ov2680_frame_sizes[index].height;

	return 0;
}

static int ov2680_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	/* Only 1 framerate */
	if (fie->index)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = OV2680_FPS;
	return 0;
}

static int ov2680_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	*frames = OV2680_SKIP_FRAMES;
	return 0;
}

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.s_stream = ov2680_s_stream,
	.g_frame_interval = ov2680_g_frame_interval,
};

static const struct v4l2_subdev_sensor_ops ov2680_sensor_ops = {
	.g_skip_frames	= ov2680_g_skip_frames,
};

static const struct v4l2_subdev_pad_ops ov2680_pad_ops = {
	.init_cfg = ov2680_init_cfg,
	.enum_mbus_code = ov2680_enum_mbus_code,
	.enum_frame_size = ov2680_enum_frame_size,
	.enum_frame_interval = ov2680_enum_frame_interval,
	.get_fmt = ov2680_get_fmt,
	.set_fmt = ov2680_set_fmt,
	.get_selection = ov2680_get_selection,
	.set_selection = ov2680_set_selection,
};

static const struct v4l2_subdev_ops ov2680_ops = {
	.video = &ov2680_video_ops,
	.pad = &ov2680_pad_ops,
	.sensor = &ov2680_sensor_ops,
};

static int ov2680_init_controls(struct ov2680_dev *sensor)
{
	static const char * const test_pattern_menu[] = {
		"Disabled",
		"Color Bars",
		"Random Data",
		"Square",
		"Black Image",
	};
	const struct v4l2_ctrl_ops *ops = &ov2680_ctrl_ops;
	struct ov2680_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int exp_max = OV2680_LINES_PER_FRAME - OV2680_INTEGRATION_TIME_MARGIN;

	v4l2_ctrl_handler_init(hdl, 4);

	hdl->lock = &sensor->lock;

	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					    0, exp_max, 1, exp_max);
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN, 0, 1023, 1, 250);
	ctrls->test_pattern =
		v4l2_ctrl_new_std_menu_items(hdl,
					     &ov2680_ctrl_ops, V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(test_pattern_menu) - 1,
					     0, 0, test_pattern_menu);

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
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);

	dev_dbg(&client->dev, "ov2680_remove...\n");

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	mutex_destroy(&sensor->lock);
	fwnode_handle_put(sensor->ep_fwnode);
	pm_runtime_disable(&client->dev);
}

static int ov2680_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov2680_dev *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	mutex_init(&sensor->lock);

	sensor->client = client;
	v4l2_i2c_subdev_init(&sensor->sd, client, &ov2680_ops);

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver.
	 * Bridge drivers doing this may also add GPIO mappings, wait for this.
	 */
	sensor->ep_fwnode = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!sensor->ep_fwnode)
		return dev_err_probe(dev, -EPROBE_DEFER, "waiting for fwnode graph endpoint\n");

	sensor->powerdown = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->powerdown)) {
		fwnode_handle_put(sensor->ep_fwnode);
		return dev_err_probe(dev, PTR_ERR(sensor->powerdown), "getting powerdown GPIO\n");
	}

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = ov2680_s_config(&sensor->sd);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->sd.fwnode = sensor->ep_fwnode;

	ret = ov2680_init_controls(sensor);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	sensor->mode.crop = ov2680_default_crop;
	ov2680_fill_format(sensor, &sensor->mode.fmt, OV2680_NATIVE_WIDTH, OV2680_NATIVE_HEIGHT);
	ov2680_calc_mode(sensor);

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret) {
		ov2680_remove(client);
		return ret;
	}

	return 0;
}

static int ov2680_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);

	gpiod_set_value_cansleep(sensor->powerdown, 1);
	return 0;
}

static int ov2680_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2680_dev *sensor = to_ov2680_sensor(sd);

	/* according to DS, at least 5ms is needed after DOVDD (enabled by ACPI) */
	usleep_range(5000, 6000);

	gpiod_set_value_cansleep(sensor->powerdown, 0);

	/* according to DS, 20ms is needed between PWDN and i2c access */
	msleep(20);

	ov2680_init_registers(sd);
	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov2680_pm_ops, ov2680_suspend, ov2680_resume, NULL);

static const struct acpi_device_id ov2680_acpi_match[] = {
	{"XXOV2680"},
	{"OVTI2680"},
	{},
};
MODULE_DEVICE_TABLE(acpi, ov2680_acpi_match);

static struct i2c_driver ov2680_driver = {
	.driver = {
		.name = "ov2680",
		.pm = pm_sleep_ptr(&ov2680_pm_ops),
		.acpi_match_table = ov2680_acpi_match,
	},
	.probe = ov2680_probe,
	.remove = ov2680_remove,
};
module_i2c_driver(ov2680_driver);

MODULE_AUTHOR("Jacky Wang <Jacky_wang@ovt.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision 2680 sensors");
MODULE_LICENSE("GPL");
