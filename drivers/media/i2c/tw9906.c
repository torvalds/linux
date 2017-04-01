/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

MODULE_DESCRIPTION("TW9906 I2C subdev driver");
MODULE_LICENSE("GPL v2");

struct tw9906 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	v4l2_std_id norm;
};

static inline struct tw9906 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tw9906, sd);
}

static const u8 initial_registers[] = {
	0x02, 0x40, /* input 0, composite */
	0x03, 0xa2, /* correct digital format */
	0x05, 0x81, /* or 0x01 for PAL */
	0x07, 0x02, /* window */
	0x08, 0x14, /* window */
	0x09, 0xf0, /* window */
	0x0a, 0x10, /* window */
	0x0b, 0xd0, /* window */
	0x0d, 0x00, /* scaling */
	0x0e, 0x11, /* scaling */
	0x0f, 0x00, /* scaling */
	0x10, 0x00, /* brightness */
	0x11, 0x60, /* contrast */
	0x12, 0x11, /* sharpness */
	0x13, 0x7e, /* U gain */
	0x14, 0x7e, /* V gain */
	0x15, 0x00, /* hue */
	0x19, 0x57, /* vbi */
	0x1a, 0x0f,
	0x1b, 0x40,
	0x29, 0x03,
	0x55, 0x00,
	0x6b, 0x26,
	0x6c, 0x36,
	0x6d, 0xf0,
	0x6e, 0x41,
	0x6f, 0x13,
	0xad, 0x70,
	0x00, 0x00, /* Terminator (reg 0x00 is read-only) */
};

static int write_reg(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_write_byte_data(client, reg, value);
}

static int write_regs(struct v4l2_subdev *sd, const u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0x00; i += 2)
		if (write_reg(sd, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

static int tw9906_s_video_routing(struct v4l2_subdev *sd, u32 input,
				      u32 output, u32 config)
{
	write_reg(sd, 0x02, 0x40 | (input << 1));
	return 0;
}

static int tw9906_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct tw9906 *dec = to_state(sd);
	bool is_60hz = norm & V4L2_STD_525_60;
	static const u8 config_60hz[] = {
		0x05, 0x81,
		0x07, 0x02,
		0x08, 0x14,
		0x09, 0xf0,
		0,    0,
	};
	static const u8 config_50hz[] = {
		0x05, 0x01,
		0x07, 0x12,
		0x08, 0x18,
		0x09, 0x20,
		0,    0,
	};

	write_regs(sd, is_60hz ? config_60hz : config_50hz);
	dec->norm = norm;
	return 0;
}

static int tw9906_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw9906 *dec = container_of(ctrl->handler, struct tw9906, hdl);
	struct v4l2_subdev *sd = &dec->sd;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		write_reg(sd, 0x10, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		write_reg(sd, 0x11, ctrl->val);
		break;
	case V4L2_CID_HUE:
		write_reg(sd, 0x15, ctrl->val);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tw9906_log_status(struct v4l2_subdev *sd)
{
	struct tw9906 *dec = to_state(sd);
	bool is_60hz = dec->norm & V4L2_STD_525_60;

	v4l2_info(sd, "Standard: %d Hz\n", is_60hz ? 60 : 50);
	v4l2_ctrl_subdev_log_status(sd);
	return 0;
}

/* --------------------------------------------------------------------------*/

static const struct v4l2_ctrl_ops tw9906_ctrl_ops = {
	.s_ctrl = tw9906_s_ctrl,
};

static const struct v4l2_subdev_core_ops tw9906_core_ops = {
	.log_status = tw9906_log_status,
};

static const struct v4l2_subdev_video_ops tw9906_video_ops = {
	.s_std = tw9906_s_std,
	.s_routing = tw9906_s_video_routing,
};

static const struct v4l2_subdev_ops tw9906_ops = {
	.core = &tw9906_core_ops,
	.video = &tw9906_video_ops,
};

static int tw9906_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct tw9906 *dec;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	dec = devm_kzalloc(&client->dev, sizeof(*dec), GFP_KERNEL);
	if (dec == NULL)
		return -ENOMEM;
	sd = &dec->sd;
	v4l2_i2c_subdev_init(sd, client, &tw9906_ops);
	hdl = &dec->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &tw9906_ctrl_ops,
		V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw9906_ctrl_ops,
		V4L2_CID_CONTRAST, 0, 255, 1, 0x60);
	v4l2_ctrl_new_std(hdl, &tw9906_ctrl_ops,
		V4L2_CID_HUE, -128, 127, 1, 0);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}

	/* Initialize tw9906 */
	dec->norm = V4L2_STD_NTSC;

	if (write_regs(sd, initial_registers) < 0) {
		v4l2_err(client, "error initializing TW9906\n");
		return -EINVAL;
	}

	return 0;
}

static int tw9906_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&to_state(sd)->hdl);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id tw9906_id[] = {
	{ "tw9906", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tw9906_id);

static struct i2c_driver tw9906_driver = {
	.driver = {
		.name	= "tw9906",
	},
	.probe = tw9906_probe,
	.remove = tw9906_remove,
	.id_table = tw9906_id,
};
module_i2c_driver(tw9906_driver);
