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
#include <linux/slab.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>

#include "wis-i2c.h"

struct wis_tw2804 {
	struct v4l2_subdev sd;
	u8 channel:2;
	u8 input:1;
	u8 update:1;
	u8 auto_gain:1;
	u8 ckil:1;
	int norm;
	u8 brightness;
	u8 contrast;
	u8 saturation;
	u8 hue;
	u8 gain;
	u8 cr_gain;
	u8 r_balance;
	u8 b_balance;
};

static u8 global_registers[] = {
	0x39, 0x00,
	0x3a, 0xff,
	0x3b, 0x84,
	0x3c, 0x80,
	0x3d, 0x80,
	0x3e, 0x82,
	0x3f, 0x82,
	0x78, 0x0f,
	0xff, 0xff, /* Terminator (reg 0xff does not exist) */
};

static u8 channel_registers[] = {
	0x01, 0xc4,
	0x02, 0xa5,
	0x03, 0x20,
	0x04, 0xd0,
	0x05, 0x20,
	0x06, 0xd0,
	0x07, 0x88,
	0x08, 0x20,
	0x09, 0x07,
	0x0a, 0xf0,
	0x0b, 0x07,
	0x0c, 0xf0,
	0x0d, 0x40,
	0x0e, 0xd2,
	0x0f, 0x80,
	0x10, 0x80,
	0x11, 0x80,
	0x12, 0x80,
	0x13, 0x1f,
	0x14, 0x00,
	0x15, 0x00,
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0xff,
	0x19, 0xff,
	0x1a, 0xff,
	0x1b, 0xff,
	0x1c, 0xff,
	0x1d, 0xff,
	0x1e, 0xff,
	0x1f, 0xff,
	0x20, 0x07,
	0x21, 0x07,
	0x22, 0x00,
	0x23, 0x91,
	0x24, 0x51,
	0x25, 0x03,
	0x26, 0x00,
	0x27, 0x00,
	0x28, 0x00,
	0x29, 0x00,
	0x2a, 0x00,
	0x2b, 0x00,
	0x2c, 0x00,
	0x2d, 0x00,
	0x2e, 0x00,
	0x2f, 0x00,
	0x30, 0x00,
	0x31, 0x00,
	0x32, 0x00,
	0x33, 0x00,
	0x34, 0x00,
	0x35, 0x00,
	0x36, 0x00,
	0x37, 0x00,
	0xff, 0xff, /* Terminator (reg 0xff does not exist) */
};

static s32 write_reg(struct i2c_client *client, u8 reg, u8 value, u8 channel)
{
	return i2c_smbus_write_byte_data(client, reg | (channel << 6), value);
}

static int write_regs(struct i2c_client *client, u8 *regs, u8 channel)
{
	int i;

	for (i = 0; regs[i] != 0xff; i += 2)
		if (i2c_smbus_write_byte_data(client,
				regs[i] | (channel << 6), regs[i + 1]) < 0)
			return -EINVAL;
	return 0;
}

static s32 read_reg(struct i2c_client *client, u8 reg, u8 channel)
{
	return i2c_smbus_read_byte_data(client, (reg) | (channel << 6));
}

static inline struct wis_tw2804 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct wis_tw2804, sd);
}

static int tw2804_log_status(struct v4l2_subdev *sd)
{
	struct wis_tw2804 *state = to_state(sd);
	v4l2_info(sd, "Standard: %s\n", state->norm == V4L2_STD_NTSC ? "NTSC" :
					state->norm == V4L2_STD_PAL ? "PAL" : "unknown");
	v4l2_info(sd, "Channel: %d\n", state->channel);
	v4l2_info(sd, "Input: %d\n", state->input);
	v4l2_info(sd, "Brightness: %d\n", state->brightness);
	v4l2_info(sd, "Contrast: %d\n", state->contrast);
	v4l2_info(sd, "Saturation: %d\n", state->saturation);
	v4l2_info(sd, "Hue: %d\n", state->hue);
	return 0;
}

static int tw2804_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *query)
{
	static const u32 user_ctrls[] = {
		V4L2_CID_USER_CLASS,
		V4L2_CID_BRIGHTNESS,
		V4L2_CID_CONTRAST,
		V4L2_CID_SATURATION,
		V4L2_CID_HUE,
		V4L2_CID_AUTOGAIN,
		V4L2_CID_COLOR_KILLER,
		V4L2_CID_GAIN,
		V4L2_CID_CHROMA_GAIN,
		V4L2_CID_BLUE_BALANCE,
		V4L2_CID_RED_BALANCE,
		0
	};

	static const u32 *ctrl_classes[] = {
		user_ctrls,
		NULL
	};

	query->id = v4l2_ctrl_next(ctrl_classes, query->id);

	switch (query->id) {
	case V4L2_CID_USER_CLASS:
		return v4l2_ctrl_query_fill(query, 0, 0, 0, 0);
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_AUTOGAIN:
		return v4l2_ctrl_query_fill(query, 0, 1, 1, 0);
	case V4L2_CID_COLOR_KILLER:
		return v4l2_ctrl_query_fill(query, 0, 1, 1, 0);
	case V4L2_CID_GAIN:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_CHROMA_GAIN:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 128);
	case V4L2_CID_BLUE_BALANCE:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 122);
	case V4L2_CID_RED_BALANCE:
		return v4l2_ctrl_query_fill(query, 0, 255, 1, 122);
	default:
		return -EINVAL;
	}
}

s32 get_ctrl_addr(int ctrl)
{
	switch (ctrl) {
	case V4L2_CID_BRIGHTNESS:
		return 0x12;
	case V4L2_CID_CONTRAST:
		return 0x11;
	case V4L2_CID_SATURATION:
		return 0x10;
	case V4L2_CID_HUE:
		return 0x0f;
	case V4L2_CID_AUTOGAIN:
		return 0x02;
	case V4L2_CID_COLOR_KILLER:
		return 0x14;
	case V4L2_CID_GAIN:
		return 0x3c;
	case V4L2_CID_CHROMA_GAIN:
		return 0x3d;
	case V4L2_CID_RED_BALANCE:
		return 0x3f;
	case V4L2_CID_BLUE_BALANCE:
		return 0x3e;
	default:
		return -EINVAL;
	}
}

static int tw2804_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct wis_tw2804 *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	s32 addr = get_ctrl_addr(ctrl->id);
	s32 val = 0;

	if (addr == -EINVAL)
		return -EINVAL;

	if (state->update) {
		val = read_reg(client, addr, ctrl->id == V4L2_CID_GAIN ||
					ctrl->id == V4L2_CID_CHROMA_GAIN ||
					ctrl->id == V4L2_CID_RED_BALANCE ||
					ctrl->id == V4L2_CID_BLUE_BALANCE ? 0 : state->channel);
		if (val < 0)
			return val;
	}

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (state->update)
			state->brightness = val;
		ctrl->value = state->brightness;
		break;
	case V4L2_CID_CONTRAST:
		if (state->update)
			state->contrast = val;
		ctrl->value = state->contrast;
		break;
	case V4L2_CID_SATURATION:
		if (state->update)
			state->saturation = val;
		ctrl->value = state->saturation;
		break;
	case V4L2_CID_HUE:
		if (state->update)
			state->hue = val;
		ctrl->value = state->hue;
		break;
	case V4L2_CID_AUTOGAIN:
		if (state->update)
			state->auto_gain = val & (1<<7) ? 1 : 0;
		ctrl->value = state->auto_gain;
		break;
	case V4L2_CID_COLOR_KILLER:
		if (state->update)
			state->ckil = (val & 0x03) == 0x03 ? 1 : 0;
		ctrl->value = state->ckil;
		break;
	case V4L2_CID_GAIN:
		if (state->update)
			state->gain = val;
		ctrl->value = state->gain;
		break;
	case V4L2_CID_CHROMA_GAIN:
		if (state->update)
			state->cr_gain = val;
		ctrl->value = state->cr_gain;
		break;
	case V4L2_CID_RED_BALANCE:
		if (state->update)
			state->r_balance = val;
		ctrl->value = state->r_balance;
		break;
	case V4L2_CID_BLUE_BALANCE:
		if (state->update)
			state->b_balance = val;
		ctrl->value = state->b_balance;
		break;
	default:
		return -EINVAL;
	}

	state->update = 0;
	return 0;
}

static int tw2804_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct wis_tw2804 *dec = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	s32 reg = 0;
	s32 addr = get_ctrl_addr(ctrl->id);

	if (addr == -EINVAL)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		reg = read_reg(client, addr, dec->channel);
		if (reg > 0) {
			if (ctrl->value == 0)
				ctrl->value = reg & ~(1<<7);
			else
				ctrl->value = reg | 1<<7;
		} else
			return reg;
		break;
	case V4L2_CID_COLOR_KILLER:
		reg = read_reg(client, addr, dec->channel);
		if (reg > 0)
			ctrl->value = (reg & ~(0x03)) | (ctrl->value == 0 ? 0x02 : 0x03);
		else
			return reg;
		break;
	default:
		break;
	}

	ctrl->value = ctrl->value > 255 ? 255 : (ctrl->value < 0 ? 0 : ctrl->value);
	reg = write_reg(client, addr, (u8)ctrl->value, ctrl->id == V4L2_CID_GAIN ||
						ctrl->id == V4L2_CID_CHROMA_GAIN ||
						ctrl->id == V4L2_CID_RED_BALANCE ||
						ctrl->id == V4L2_CID_BLUE_BALANCE ? 0 : dec->channel);

	if (reg < 0) {
		v4l2_err(&dec->sd, "Can`t set_ctrl value:id=%d;value=%d\n", ctrl->id, ctrl->value);
		return reg;
	}

	dec->update = 1;
	return tw2804_g_ctrl(sd, ctrl);
}

static int tw2804_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct wis_tw2804 *dec = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	u8 regs[] = {
		0x01, norm&V4L2_STD_NTSC ? 0xc4 : 0x84,
		0x09, norm&V4L2_STD_NTSC ? 0x07 : 0x04,
		0x0a, norm&V4L2_STD_NTSC ? 0xf0 : 0x20,
		0x0b, norm&V4L2_STD_NTSC ? 0x07 : 0x04,
		0x0c, norm&V4L2_STD_NTSC ? 0xf0 : 0x20,
		0x0d, norm&V4L2_STD_NTSC ? 0x40 : 0x4a,
		0x16, norm&V4L2_STD_NTSC ? 0x00 : 0x40,
		0x17, norm&V4L2_STD_NTSC ? 0x00 : 0x40,
		0x20, norm&V4L2_STD_NTSC ? 0x07 : 0x0f,
		0x21, norm&V4L2_STD_NTSC ? 0x07 : 0x0f,
		0xff, 0xff,
	};
	write_regs(client, regs, dec->channel);
	dec->norm = norm;
	return 0;
}

static const struct v4l2_subdev_core_ops tw2804_core_ops = {
	.log_status = tw2804_log_status,
	.g_ctrl = tw2804_g_ctrl,
	.s_ctrl = tw2804_s_ctrl,
	.queryctrl = tw2804_queryctrl,
	.s_std = tw2804_s_std,
};

static int tw2804_s_video_routing(struct v4l2_subdev *sd, u32 input, u32 output,
	u32 config)
{
	struct wis_tw2804 *dec = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	s32 reg = 0;

	if (0 > input || input > 1)
		return -EINVAL;

	if (input == dec->input && !dec->update)
		return 0;

	reg = read_reg(client, 0x22, dec->channel);

	if (reg >= 0) {
		if (input == 0)
			reg &= ~(1<<2);
		else
			reg |= 1<<2;
		reg = write_reg(client, 0x22, (u8)reg, dec->channel);
	}

	if (reg >= 0) {
		dec->input = input;
		dec->update = 0;
	} else
		return reg;
	return 0;
}

static int tw2804_s_mbus_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *fmt)
{
	/*TODO need select between 3fmt:
	 * bt_656,
	 * bt_601_8bit,
	 * bt_656_dual,
	 */
	return 0;
}

int tw2804_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct wis_tw2804 *dec = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 reg = read_reg(client, 0x78, 0);

	if (enable == 1)
		write_reg(client, 0x78, reg & ~(1<<dec->channel), 0);
	else
		write_reg(client, 0x78, reg | (1<<dec->channel), 0);

	return 0;
}

static const struct v4l2_subdev_video_ops tw2804_video_ops = {
	.s_routing = tw2804_s_video_routing,
	.s_mbus_fmt = tw2804_s_mbus_fmt,
	.s_stream = tw2804_s_stream,
};

static const struct v4l2_subdev_ops tw2804_ops = {
	.core = &tw2804_core_ops,
	.video = &tw2804_video_ops,
};

static int wis_tw2804_command(struct i2c_client *client,
				unsigned int cmd, void *arg)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct wis_tw2804 *dec = to_state(sd);
	int *input;

	if (cmd == DECODER_SET_CHANNEL) {
		input = arg;

		if (*input < 0 || *input > 3) {
			printk(KERN_ERR "wis-tw2804: channel %d is not "
					"between 0 and 3!\n", *input);
			return 0;
		}
		dec->channel = *input;
		printk(KERN_DEBUG "wis-tw2804: initializing TW2804 "
				"channel %d\n", dec->channel);
		if (dec->channel == 0 &&
				write_regs(client, global_registers, 0) < 0) {
			printk(KERN_ERR "wis-tw2804: error initializing "
					"TW2804 global registers\n");
			return 0;
		}
		if (write_regs(client, channel_registers, dec->channel) < 0) {
			printk(KERN_ERR "wis-tw2804: error initializing "
					"TW2804 channel %d\n", dec->channel);
			return 0;
		}
		return 0;
	}

	if (dec->channel < 0) {
		printk(KERN_DEBUG "wis-tw2804: ignoring command %08x until "
				"channel number is set\n", cmd);
		return 0;
	}
	return 0;
}

static int wis_tw2804_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct wis_tw2804 *dec;
	struct v4l2_subdev *sd;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	dec = kzalloc(sizeof(struct wis_tw2804), GFP_KERNEL);

	if (dec == NULL)
		return -ENOMEM;
	sd = &dec->sd;
	dec->update = 1;
	dec->channel = -1;
	dec->norm = V4L2_STD_NTSC;
	dec->brightness = 128;
	dec->contrast = 128;
	dec->saturation = 128;
	dec->hue = 128;
	dec->gain = 128;
	dec->cr_gain = 128;
	dec->b_balance = 122;
	dec->r_balance = 122;
	v4l2_i2c_subdev_init(sd, client, &tw2804_ops);

	printk(KERN_DEBUG "wis-tw2804: creating TW2804 at address %d on %s\n",
		client->addr, adapter->name);

	return 0;
}

static int wis_tw2804_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id wis_tw2804_id[] = {
	{ "wis_tw2804", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wis_tw2804_id);

static struct i2c_driver wis_tw2804_driver = {
	.driver = {
		.name	= "WIS TW2804 I2C driver",
	},
	.probe		= wis_tw2804_probe,
	.remove		= wis_tw2804_remove,
	.command	= wis_tw2804_command,
	.id_table	= wis_tw2804_id,
};

static int __init wis_tw2804_init(void)
{
	return i2c_add_driver(&wis_tw2804_driver);
}

static void __exit wis_tw2804_cleanup(void)
{
	i2c_del_driver(&wis_tw2804_driver);
}

module_init(wis_tw2804_init);
module_exit(wis_tw2804_cleanup);

MODULE_LICENSE("GPL v2");
