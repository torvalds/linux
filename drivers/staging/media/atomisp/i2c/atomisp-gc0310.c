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

/*
 * gc0310_write_reg_array - Initializes a list of GC0310 registers
 * @client: i2c driver client structure
 * @reglist: list of registers to be written
 * @count: number of register, value pairs in the list
 */
static int gc0310_write_reg_array(struct i2c_client *client,
				  const struct gc0310_reg *reglist, int count)
{
	int i, err;

	for (i = 0; i < count; i++) {
		err = i2c_smbus_write_byte_data(client, reglist[i].reg, reglist[i].val);
		if (err) {
			dev_err(&client->dev, "write error: wrote 0x%x to offset 0x%x error %d",
				reglist[i].val, reglist[i].reg, err);
			return err;
		}
	}

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

	dev_dbg(&client->dev, "gain=0x%x again=0x%x dgain=0x%x\n", gain, again, dgain);

	/* set analog gain */
	ret = i2c_smbus_write_byte_data(client, GC0310_AGC_ADJ, again);
	if (ret)
		return ret;

	/* set digital gain */
	ret = i2c_smbus_write_byte_data(client, GC0310_DGC_ADJ, dgain);
	if (ret)
		return ret;

	return 0;
}

static int __gc0310_set_exposure(struct v4l2_subdev *sd, int coarse_itg,
				 int gain, int digitgain)

{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	dev_dbg(&client->dev, "coarse_itg=%d gain=%d digitgain=%d\n", coarse_itg, gain, digitgain);

	/* set exposure */
	ret = i2c_smbus_write_word_swapped(client, GC0310_AEC_PK_EXPO_H, coarse_itg);
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
	int ret;

	/* get exposure */
	ret = i2c_smbus_read_word_swapped(client, GC0310_AEC_PK_EXPO_H);
	if (ret < 0)
		return ret;

	*value = ret;
	return 0;
}

static int gc0310_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	switch (ctrl->id) {
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
};

static int gc0310_init(struct v4l2_subdev *sd)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	mutex_lock(&dev->input_lock);

	/* set initial registers */
	ret = gc0310_write_reg_array(client, gc0310_reset_register,
				     ARRAY_SIZE(gc0310_reset_register));

	/* restore settings */
	gc0310_res = gc0310_res_preview;
	N_RES = N_RES_PREVIEW;

	mutex_unlock(&dev->input_lock);

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

static int power_up(struct v4l2_subdev *sd)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
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

	dev->power_on = true;
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

	if (!dev->power_on)
		return 0; /* Already off */

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

	dev->power_on = false;
	return ret;
}

static int gc0310_s_power(struct v4l2_subdev *sd, int on)
{
	int ret;

	if (on == 0)
		return power_down(sd);

	ret = power_up(sd);
	if (ret)
		return ret;

	return gc0310_init(sd);
}

/* TODO: remove it. */
static int startup(struct v4l2_subdev *sd)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = gc0310_write_reg_array(client, dev->res->regs, dev->res->reg_count);
	if (ret) {
		dev_err(&client->dev, "gc0310 write register err.\n");
		return ret;
	}

	return ret;
}

static int gc0310_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *gc0310_info = NULL;
	struct gc0310_resolution *res;
	int ret = 0;

	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	gc0310_info = v4l2_get_subdev_hostdata(sd);
	if (!gc0310_info)
		return -EINVAL;

	mutex_lock(&dev->input_lock);

	res = v4l2_find_nearest_size(gc0310_res_preview,
				     ARRAY_SIZE(gc0310_res_preview), width,
				     height, fmt->width, fmt->height);
	if (!res)
		res = &gc0310_res_preview[N_RES - 1];

	fmt->width = res->width;
	fmt->height = res->height;
	dev->res = res;

	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		sd_state->pads->try_fmt = *fmt;
		mutex_unlock(&dev->input_lock);
		return 0;
	}

	/* s_power has not been called yet for std v4l2 clients (camorama) */
	power_up(sd);

	dev_dbg(&client->dev, "%s: before gc0310_write_reg_array %s\n",
		__func__, dev->res->desc);
	ret = startup(sd);
	if (ret) {
		dev_err(&client->dev, "gc0310 startup err\n");
		goto err;
	}

err:
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int gc0310_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	if (format->pad)
		return -EINVAL;

	if (!fmt)
		return -EINVAL;

	fmt->width = dev->res->width;
	fmt->height = dev->res->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;

	return 0;
}

static int gc0310_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = i2c_smbus_read_word_swapped(client, GC0310_SC_CMMN_CHIP_ID_H);
	if (ret < 0) {
		dev_err(&client->dev, "read sensor_id failed: %d\n", ret);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "sensor ID = 0x%x\n", ret);

	if (ret != GC0310_ID) {
		dev_err(&client->dev, "sensor ID error, read id = 0x%x, target id = 0x%x\n",
			ret, GC0310_ID);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "detect gc0310 success\n");

	return 0;
}

static int gc0310_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	dev_dbg(&client->dev, "%s S enable=%d\n", __func__, enable);
	mutex_lock(&dev->input_lock);

	if (enable) {
		/* enable per frame MIPI and sensor ctrl reset  */
		ret = i2c_smbus_write_byte_data(client, 0xFE, 0x30);
		if (ret) {
			mutex_unlock(&dev->input_lock);
			return ret;
		}
	}

	ret = i2c_smbus_write_byte_data(client, GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_3);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = i2c_smbus_write_byte_data(client, GC0310_SW_STREAM,
					enable ? GC0310_START_STREAMING : GC0310_STOP_STREAMING);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = i2c_smbus_write_byte_data(client, GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_0);
	if (ret) {
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	mutex_unlock(&dev->input_lock);
	return ret;
}

static int gc0310_s_config(struct v4l2_subdev *sd,
			   int irq, void *platform_data)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
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
	dev->power_on = true; /* force power_down() to run */
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
	interval->interval.denominator = dev->res->fps;

	return 0;
}

static int gc0310_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= MAX_FMTS)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	return 0;
}

static int gc0310_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
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
	*frames = dev->res->skip_frames;
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

static void gc0310_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	dev_dbg(&client->dev, "gc0310_remove...\n");

	dev->platform_data->csi_cfg(sd, 0);

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	kfree(dev);
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

	dev->res = &gc0310_res_preview[0];
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
