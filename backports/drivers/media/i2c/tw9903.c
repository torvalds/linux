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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("TW9903 I2C subdev driver");
MODULE_LICENSE("GPL v2");

/*
 * This driver is based on the wis-tw9903.c source that was in
 * drivers/staging/media/go7007. That source had commented out code for
 * saturation and scaling (neither seemed to work). If anyone ever gets
 * hardware to test this driver, then that code might be useful to look at.
 * You need to get the kernel sources of, say, kernel 3.8 where that
 * wis-tw9903 driver is still present.
 */

struct tw9903 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	v4l2_std_id norm;
};

static inline struct tw9903 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tw9903, sd);
}

static const u8 initial_registers[] = {
	0x02, 0x44, /* input 1, composite */
	0x03, 0x92, /* correct digital format */
	0x04, 0x00,
	0x05, 0x80, /* or 0x00 for PAL */
	0x06, 0x40, /* second internal current reference */
	0x07, 0x02, /* window */
	0x08, 0x14, /* window */
	0x09, 0xf0, /* window */
	0x0a, 0x81, /* window */
	0x0b, 0xd0, /* window */
	0x0c, 0x8c,
	0x0d, 0x00, /* scaling */
	0x0e, 0x11, /* scaling */
	0x0f, 0x00, /* scaling */
	0x10, 0x00, /* brightness */
	0x11, 0x60, /* contrast */
	0x12, 0x01, /* sharpness */
	0x13, 0x7f, /* U gain */
	0x14, 0x5a, /* V gain */
	0x15, 0x00, /* hue */
	0x16, 0xc3, /* sharpness */
	0x18, 0x00,
	0x19, 0x58, /* vbi */
	0x1a, 0x80,
	0x1c, 0x0f, /* video norm */
	0x1d, 0x7f, /* video norm */
	0x20, 0xa0, /* clamping gain (working 0x50) */
	0x21, 0x22,
	0x22, 0xf0,
	0x23, 0xfe,
	0x24, 0x3c,
	0x25, 0x38,
	0x26, 0x44,
	0x27, 0x20,
	0x28, 0x00,
	0x29, 0x15,
	0x2a, 0xa0,
	0x2b, 0x44,
	0x2c, 0x37,
	0x2d, 0x00,
	0x2e, 0xa5, /* burst PLL control (working: a9) */
	0x2f, 0xe0, /* 0xea is blue test frame -- 0xe0 for normal */
	0x31, 0x00,
	0x33, 0x22,
	0x34, 0x11,
	0x35, 0x35,
	0x3b, 0x05,
	0x06, 0xc0, /* reset device */
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

static int tw9903_s_video_routing(struct v4l2_subdev *sd, u32 input,
				      u32 output, u32 config)
{
	write_reg(sd, 0x02, 0x40 | (input << 1));
	return 0;
}

static int tw9903_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct tw9903 *dec = to_state(sd);
	bool is_60hz = norm & V4L2_STD_525_60;
	static const u8 config_60hz[] = {
		0x05, 0x80,
		0x07, 0x02,
		0x08, 0x14,
		0x09, 0xf0,
		0,    0,
	};
	static const u8 config_50hz[] = {
		0x05, 0x00,
		0x07, 0x12,
		0x08, 0x18,
		0x09, 0x20,
		0,    0,
	};

	write_regs(sd, is_60hz ? config_60hz : config_50hz);
	dec->norm = norm;
	return 0;
}


static int tw9903_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw9903 *dec = container_of(ctrl->handler, struct tw9903, hdl);
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

static int tw9903_log_status(struct v4l2_subdev *sd)
{
	struct tw9903 *dec = to_state(sd);
	bool is_60hz = dec->norm & V4L2_STD_525_60;

	v4l2_info(sd, "Standard: %d Hz\n", is_60hz ? 60 : 50);
	v4l2_ctrl_subdev_log_status(sd);
	return 0;
}

/* --------------------------------------------------------------------------*/

static const struct v4l2_ctrl_ops tw9903_ctrl_ops = {
	.s_ctrl = tw9903_s_ctrl,
};

static const struct v4l2_subdev_core_ops tw9903_core_ops = {
	.log_status = tw9903_log_status,
};

static const struct v4l2_subdev_video_ops tw9903_video_ops = {
	.s_std = tw9903_s_std,
	.s_routing = tw9903_s_video_routing,
};

static const struct v4l2_subdev_ops tw9903_ops = {
	.core = &tw9903_core_ops,
	.video = &tw9903_video_ops,
};

/* --------------------------------------------------------------------------*/

static int tw9903_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct tw9903 *dec;
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
	v4l2_i2c_subdev_init(sd, client, &tw9903_ops);
	hdl = &dec->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &tw9903_ctrl_ops,
		V4L2_CID_BRIGHTNESS, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw9903_ctrl_ops,
		V4L2_CID_CONTRAST, 0, 255, 1, 0x60);
	v4l2_ctrl_new_std(hdl, &tw9903_ctrl_ops,
		V4L2_CID_HUE, -128, 127, 1, 0);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}

	/* Initialize tw9903 */
	dec->norm = V4L2_STD_NTSC;

	if (write_regs(sd, initial_registers) < 0) {
		v4l2_err(client, "error initializing TW9903\n");
		return -EINVAL;
	}

	return 0;
}

static int tw9903_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&to_state(sd)->hdl);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id tw9903_id[] = {
	{ "tw9903", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tw9903_id);

static struct i2c_driver tw9903_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "tw9903",
	},
	.probe = tw9903_probe,
	.remove = tw9903_remove,
	.id_table = tw9903_id,
};
module_i2c_driver(tw9903_driver);
