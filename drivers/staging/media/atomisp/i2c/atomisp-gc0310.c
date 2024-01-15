// SPDX-License-Identifier: GPL-2.0
/*
 * Support for GalaxyCore GC0310 VGA camera sensor.
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

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define GC0310_NATIVE_WIDTH			656
#define GC0310_NATIVE_HEIGHT			496

#define GC0310_FPS				30
#define GC0310_SKIP_FRAMES			3

#define GC0310_FOCAL_LENGTH_NUM			278 /* 2.78mm */

#define GC0310_ID				0xa310

#define GC0310_RESET_RELATED			0xFE
#define GC0310_REGISTER_PAGE_0			0x0
#define GC0310_REGISTER_PAGE_3			0x3

/*
 * GC0310 System control registers
 */
#define GC0310_SW_STREAM			0x10

#define GC0310_SC_CMMN_CHIP_ID_H		0xf0
#define GC0310_SC_CMMN_CHIP_ID_L		0xf1

#define GC0310_AEC_PK_EXPO_H			0x03
#define GC0310_AEC_PK_EXPO_L			0x04
#define GC0310_AGC_ADJ				0x48
#define GC0310_DGC_ADJ				0x71
#define GC0310_GROUP_ACCESS			0x3208

#define GC0310_H_CROP_START_H			0x09
#define GC0310_H_CROP_START_L			0x0A
#define GC0310_V_CROP_START_H			0x0B
#define GC0310_V_CROP_START_L			0x0C
#define GC0310_H_OUTSIZE_H			0x0F
#define GC0310_H_OUTSIZE_L			0x10
#define GC0310_V_OUTSIZE_H			0x0D
#define GC0310_V_OUTSIZE_L			0x0E
#define GC0310_H_BLANKING_H			0x05
#define GC0310_H_BLANKING_L			0x06
#define GC0310_V_BLANKING_H			0x07
#define GC0310_V_BLANKING_L			0x08
#define GC0310_SH_DELAY				0x11

#define GC0310_START_STREAMING			0x94 /* 8-bit enable */
#define GC0310_STOP_STREAMING			0x0 /* 8-bit disable */

#define to_gc0310_sensor(x) container_of(x, struct gc0310_device, sd)

struct gc0310_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	/* Protect against concurrent changes to controls */
	struct mutex input_lock;
	bool is_streaming;

	struct gpio_desc *reset;
	struct gpio_desc *powerdown;

	struct gc0310_mode {
		struct v4l2_mbus_framefmt fmt;
	} mode;

	struct gc0310_ctrls {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	} ctrls;
};

struct gc0310_reg {
	u8 reg;
	u8 val;
};

static const struct gc0310_reg gc0310_reset_register[] = {
	/* System registers */
	{ 0xfe, 0xf0 },
	{ 0xfe, 0xf0 },
	{ 0xfe, 0x00 },

	{ 0xfc, 0x0e }, /* 4e */
	{ 0xfc, 0x0e }, /* 16//4e // [0]apwd [6]regf_clk_gate */
	{ 0xf2, 0x80 }, /* sync output */
	{ 0xf3, 0x00 }, /* 1f//01 data output */
	{ 0xf7, 0x33 }, /* f9 */
	{ 0xf8, 0x05 }, /* 00 */
	{ 0xf9, 0x0e }, /* 0x8e //0f */
	{ 0xfa, 0x11 },

	/* MIPI */
	{ 0xfe, 0x03 },
	{ 0x01, 0x03 }, /* mipi 1lane */
	{ 0x02, 0x22 }, /* 0x33 */
	{ 0x03, 0x94 },
	{ 0x04, 0x01 }, /* fifo_prog */
	{ 0x05, 0x00 }, /* fifo_prog */
	{ 0x06, 0x80 }, /* b0  //YUV ISP data */
	{ 0x11, 0x2a }, /* 1e //LDI set YUV422 */
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0] */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */
	{ 0x15, 0x12 }, /* 0x10 //DPHYY_MODE read_ready */
	{ 0x17, 0x01 },
	{ 0x40, 0x08 },
	{ 0x41, 0x00 },
	{ 0x42, 0x00 },
	{ 0x43, 0x00 },
	{ 0x21, 0x02 }, /* 0x01 */
	{ 0x22, 0x02 }, /* 0x01 */
	{ 0x23, 0x01 }, /* 0x05 //Nor:0x05 DOU:0x06 */
	{ 0x29, 0x00 },
	{ 0x2A, 0x25 }, /* 0x05 //data zero 0x7a de */
	{ 0x2B, 0x02 },

	{ 0xfe, 0x00 },

	/* CISCTL */
	{ 0x00, 0x2f }, /* 2f//0f//02//01 */
	{ 0x01, 0x0f }, /* 06 */
	{ 0x02, 0x04 },
	{ 0x4f, 0x00 }, /* AEC 0FF */
	{ 0x03, 0x01 }, /* 0x03 //04 */
	{ 0x04, 0xc0 }, /* 0xe8 //58 */
	{ 0x05, 0x00 },
	{ 0x06, 0xb2 }, /* 0x0a //HB */
	{ 0x07, 0x00 },
	{ 0x08, 0x0c }, /* 0x89 //VB */
	{ 0x09, 0x00 }, /* row start */
	{ 0x0a, 0x00 },
	{ 0x0b, 0x00 }, /* col start */
	{ 0x0c, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */
	{ 0x17, 0x14 },
	{ 0x18, 0x1a }, /* 0a//[4]double reset */
	{ 0x19, 0x14 }, /* AD pipeline */
	{ 0x1b, 0x48 },
	{ 0x1e, 0x6b }, /* 3b//col bias */
	{ 0x1f, 0x28 }, /* 20//00//08//txlow */
	{ 0x20, 0x89 }, /* 88//0c//[3:2]DA15 */
	{ 0x21, 0x49 }, /* 48//[3] txhigh */
	{ 0x22, 0xb0 },
	{ 0x23, 0x04 }, /* [1:0]vcm_r */
	{ 0x24, 0x16 }, /* 15 */
	{ 0x34, 0x20 }, /* [6:4] rsg high//range */

	/* BLK */
	{ 0x26, 0x23 }, /* [1]dark_current_en [0]offset_en */
	{ 0x28, 0xff }, /* BLK_limie_value */
	{ 0x29, 0x00 }, /* global offset */
	{ 0x33, 0x18 }, /* offset_ratio */
	{ 0x37, 0x20 }, /* dark_current_ratio */
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x00 },
	{ 0x2d, 0x00 },
	{ 0x2e, 0x00 },
	{ 0x2f, 0x00 },
	{ 0x30, 0x00 },
	{ 0x31, 0x00 },
	{ 0x47, 0x80 }, /* a7 */
	{ 0x4e, 0x66 }, /* select_row */
	{ 0xa8, 0x02 }, /* win_width_dark, same with crop_win_width */
	{ 0xa9, 0x80 },

	/* ISP */
	{ 0x40, 0x06 }, /* 0xff //ff //48 */
	{ 0x41, 0x00 }, /* 0x21 //00//[0]curve_en */
	{ 0x42, 0x04 }, /* 0xcf //0a//[1]awn_en */
	{ 0x44, 0x18 }, /* 0x18 //02 */
	{ 0x46, 0x02 }, /* 0x03 //sync */
	{ 0x49, 0x03 },
	{ 0x4c, 0x20 }, /* 00[5]pretect exp */
	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

	/* Gain */
	{ 0x70, 0x70 }, /* 70 //80//global gain */
	{ 0x71, 0x20 }, /* pregain gain */
	{ 0x72, 0x40 }, /* post gain */
	{ 0x5a, 0x84 }, /* 84//analog gain 0  */
	{ 0x5b, 0xc9 }, /* c9 */
	{ 0x5c, 0xed }, /* ed//not use pga gain highest level */
	{ 0x77, 0x40 }, /* R gain 0x74 //awb gain */
	{ 0x78, 0x40 }, /* G gain */
	{ 0x79, 0x40 }, /* B gain 0x5f */

	{ 0x48, 0x00 },
	{ 0xfe, 0x01 },
	{ 0x0a, 0x45 }, /* [7]col gain mode */

	{ 0x3e, 0x40 },
	{ 0x3f, 0x5c },
	{ 0x40, 0x7b },
	{ 0x41, 0xbd },
	{ 0x42, 0xf6 },
	{ 0x43, 0x63 },
	{ 0x03, 0x60 },
	{ 0x44, 0x03 },

	/* Dark / Sun mode related */
	{ 0xfe, 0x01 },
	{ 0x45, 0xa4 }, /* 0xf7 */
	{ 0x46, 0xf0 }, /* 0xff //f0//sun value th */
	{ 0x48, 0x03 }, /* sun mode */
	{ 0x4f, 0x60 }, /* sun_clamp */
	{ 0xfe, 0x00 },
};

static const struct gc0310_reg gc0310_VGA_30fps[] = {
	{ 0xfe, 0x00 },
	{ 0x0d, 0x01 }, /* height */
	{ 0x0e, 0xf2 }, /* 0xf7 //height */
	{ 0x0f, 0x02 }, /* width */
	{ 0x10, 0x94 }, /* 0xa0 //height */

	{ 0x50, 0x01 }, /* crop enable */
	{ 0x51, 0x00 },
	{ 0x52, 0x00 },
	{ 0x53, 0x00 },
	{ 0x54, 0x01 },
	{ 0x55, 0x01 }, /* crop window height */
	{ 0x56, 0xf0 },
	{ 0x57, 0x02 }, /* crop window width */
	{ 0x58, 0x90 },

	{ 0xfe, 0x03 },
	{ 0x12, 0x90 }, /* 00 //04 //00 //04//00 //LWC[7:0]  */
	{ 0x13, 0x02 }, /* 05 //05 //LWC[15:8] */

	{ 0xfe, 0x00 },
};

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

static int gc0310_exposure_set(struct gc0310_device *dev, u32 exp)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

	return i2c_smbus_write_word_swapped(client, GC0310_AEC_PK_EXPO_H, exp);
}

static int gc0310_gain_set(struct gc0310_device *dev, u32 gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	u8 again, dgain;
	int ret;

	/* Taken from original driver, this never sets dgain lower then 32? */

	/* Change 0 - 95 to 32 - 127 */
	gain += 32;

	if (gain < 64) {
		again = 0x0; /* sqrt(2) */
		dgain = gain;
	} else {
		again = 0x2; /* 2 * sqrt(2) */
		dgain = gain / 2;
	}

	ret = i2c_smbus_write_byte_data(client, GC0310_AGC_ADJ, again);
	if (ret)
		return ret;

	return i2c_smbus_write_byte_data(client, GC0310_DGC_ADJ, dgain);
}

static int gc0310_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0310_device *dev =
		container_of(ctrl->handler, struct gc0310_device, ctrls.handler);
	int ret;

	/* Only apply changes to the controls if the device is powered up */
	if (!pm_runtime_get_if_in_use(dev->sd.dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = gc0310_exposure_set(dev, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		ret = gc0310_gain_set(dev, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(dev->sd.dev);
	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = gc0310_s_ctrl,
};

static struct v4l2_mbus_framefmt *
gc0310_get_pad_format(struct gc0310_device *dev,
		      struct v4l2_subdev_state *state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&dev->sd, state, pad);

	return &dev->mode.fmt;
}

/* The GC0310 currently only supports 1 fixed fmt */
static void gc0310_fill_format(struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = GC0310_NATIVE_WIDTH;
	fmt->height = GC0310_NATIVE_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
}

static int gc0310_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct v4l2_mbus_framefmt *fmt;

	fmt = gc0310_get_pad_format(dev, sd_state, format->pad, format->which);
	gc0310_fill_format(fmt);

	format->format = *fmt;
	return 0;
}

static int gc0310_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct gc0310_device *dev = to_gc0310_sensor(sd);
	struct v4l2_mbus_framefmt *fmt;

	fmt = gc0310_get_pad_format(dev, sd_state, format->pad, format->which);
	format->format = *fmt;
	return 0;
}

static int gc0310_detect(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	ret = pm_runtime_get_sync(&client->dev);
	if (ret >= 0)
		ret = i2c_smbus_read_word_swapped(client, GC0310_SC_CMMN_CHIP_ID_H);
	pm_runtime_put(&client->dev);
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
	int ret = 0;

	dev_dbg(&client->dev, "%s S enable=%d\n", __func__, enable);
	mutex_lock(&dev->input_lock);

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0)
			goto error_power_down;

		msleep(100);

		ret = gc0310_write_reg_array(client, gc0310_reset_register,
					     ARRAY_SIZE(gc0310_reset_register));
		if (ret)
			goto error_power_down;

		ret = gc0310_write_reg_array(client, gc0310_VGA_30fps,
					     ARRAY_SIZE(gc0310_VGA_30fps));
		if (ret)
			goto error_power_down;

		/* restore value of all ctrls */
		ret = __v4l2_ctrl_handler_setup(&dev->ctrls.handler);
		if (ret)
			goto error_power_down;

		/* enable per frame MIPI and sensor ctrl reset  */
		ret = i2c_smbus_write_byte_data(client, 0xFE, 0x30);
		if (ret)
			goto error_power_down;
	}

	ret = i2c_smbus_write_byte_data(client, GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_3);
	if (ret)
		goto error_power_down;

	ret = i2c_smbus_write_byte_data(client, GC0310_SW_STREAM,
					enable ? GC0310_START_STREAMING : GC0310_STOP_STREAMING);
	if (ret)
		goto error_power_down;

	ret = i2c_smbus_write_byte_data(client, GC0310_RESET_RELATED, GC0310_REGISTER_PAGE_0);
	if (ret)
		goto error_power_down;

	if (!enable)
		pm_runtime_put(&client->dev);

	dev->is_streaming = enable;
	mutex_unlock(&dev->input_lock);
	return 0;

error_power_down:
	pm_runtime_put(&client->dev);
	dev->is_streaming = false;
	mutex_unlock(&dev->input_lock);
	return ret;
}

static int gc0310_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *interval)
{
	interval->interval.numerator = 1;
	interval->interval.denominator = GC0310_FPS;

	return 0;
}

static int gc0310_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	/* We support only a single format */
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	return 0;
}

static int gc0310_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	/* We support only a single resolution */
	if (fse->index)
		return -EINVAL;

	fse->min_width = GC0310_NATIVE_WIDTH;
	fse->max_width = GC0310_NATIVE_WIDTH;
	fse->min_height = GC0310_NATIVE_HEIGHT;
	fse->max_height = GC0310_NATIVE_HEIGHT;

	return 0;
}

static int gc0310_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	*frames = GC0310_SKIP_FRAMES;
	return 0;
}

static const struct v4l2_subdev_sensor_ops gc0310_sensor_ops = {
	.g_skip_frames	= gc0310_g_skip_frames,
};

static const struct v4l2_subdev_video_ops gc0310_video_ops = {
	.s_stream = gc0310_s_stream,
	.g_frame_interval = gc0310_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc0310_pad_ops = {
	.enum_mbus_code = gc0310_enum_mbus_code,
	.enum_frame_size = gc0310_enum_frame_size,
	.get_fmt = gc0310_get_fmt,
	.set_fmt = gc0310_set_fmt,
};

static const struct v4l2_subdev_ops gc0310_ops = {
	.video = &gc0310_video_ops,
	.pad = &gc0310_pad_ops,
	.sensor = &gc0310_sensor_ops,
};

static int gc0310_init_controls(struct gc0310_device *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->ctrls.handler;

	v4l2_ctrl_handler_init(hdl, 2);

	/* Use the same lock for controls as for everything else */
	hdl->lock = &dev->input_lock;
	dev->sd.ctrl_handler = hdl;

	dev->ctrls.exposure =
		v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_EXPOSURE, 0, 4095, 1, 1023);

	/* 32 steps at base gain 1 + 64 half steps at base gain 2 */
	dev->ctrls.gain =
		v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_GAIN, 0, 95, 1, 31);

	return hdl->error;
}

static void gc0310_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc0310_device *dev = to_gc0310_sensor(sd);

	dev_dbg(&client->dev, "gc0310_remove...\n");

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrls.handler);
	mutex_destroy(&dev->input_lock);
	pm_runtime_disable(&client->dev);
}

static int gc0310_probe(struct i2c_client *client)
{
	struct fwnode_handle *ep_fwnode;
	struct gc0310_device *dev;
	int ret;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver.
	 * Bridge drivers doing this may also add GPIO mappings, wait for this.
	 */
	ep_fwnode = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep_fwnode)
		return dev_err_probe(&client->dev, -EPROBE_DEFER, "waiting for fwnode graph endpoint\n");

	fwnode_handle_put(ep_fwnode);

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dev->reset)) {
		return dev_err_probe(&client->dev, PTR_ERR(dev->reset),
				     "getting reset GPIO\n");
	}

	dev->powerdown = devm_gpiod_get(&client->dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(dev->powerdown)) {
		return dev_err_probe(&client->dev, PTR_ERR(dev->powerdown),
				     "getting powerdown GPIO\n");
	}

	mutex_init(&dev->input_lock);
	v4l2_i2c_subdev_init(&dev->sd, client, &gc0310_ops);
	gc0310_fill_format(&dev->mode.fmt);

	pm_runtime_set_suspended(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);

	ret = gc0310_detect(client);
	if (ret) {
		gc0310_remove(client);
		return ret;
	}

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = gc0310_init_controls(dev);
	if (ret) {
		gc0310_remove(client);
		return ret;
	}

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret) {
		gc0310_remove(client);
		return ret;
	}

	ret = v4l2_async_register_subdev_sensor(&dev->sd);
	if (ret) {
		gc0310_remove(client);
		return ret;
	}

	return 0;
}

static int gc0310_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc0310_device *gc0310_dev = to_gc0310_sensor(sd);

	gpiod_set_value_cansleep(gc0310_dev->powerdown, 1);
	gpiod_set_value_cansleep(gc0310_dev->reset, 1);
	return 0;
}

static int gc0310_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc0310_device *gc0310_dev = to_gc0310_sensor(sd);

	usleep_range(10000, 15000);
	gpiod_set_value_cansleep(gc0310_dev->reset, 0);
	usleep_range(10000, 15000);
	gpiod_set_value_cansleep(gc0310_dev->powerdown, 0);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(gc0310_pm_ops, gc0310_suspend, gc0310_resume, NULL);

static const struct acpi_device_id gc0310_acpi_match[] = {
	{"INT0310"},
	{},
};
MODULE_DEVICE_TABLE(acpi, gc0310_acpi_match);

static struct i2c_driver gc0310_driver = {
	.driver = {
		.name = "gc0310",
		.pm = pm_sleep_ptr(&gc0310_pm_ops),
		.acpi_match_table = gc0310_acpi_match,
	},
	.probe = gc0310_probe,
	.remove = gc0310_remove,
};
module_i2c_driver(gc0310_driver);

MODULE_AUTHOR("Lai, Angie <angie.lai@intel.com>");
MODULE_DESCRIPTION("A low-level driver for GalaxyCore GC0310 sensors");
MODULE_LICENSE("GPL");
