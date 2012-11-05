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

#include "wis-i2c.h"

struct wis_saa7115 {
	int norm;
	int brightness;
	int contrast;
	int saturation;
	int hue;
};

static u8 initial_registers[] = {
	0x01, 0x08,
	0x02, 0xc0,
	0x03, 0x20,
	0x04, 0x80,
	0x05, 0x80,
	0x06, 0xeb,
	0x07, 0xe0,
	0x08, 0xf0,	/* always toggle FID */
	0x09, 0x40,
	0x0a, 0x80,
	0x0b, 0x40,
	0x0c, 0x40,
	0x0d, 0x00,
	0x0e, 0x03,
	0x0f, 0x2a,
	0x10, 0x0e,
	0x11, 0x00,
	0x12, 0x8d,
	0x13, 0x00,
	0x14, 0x00,
	0x15, 0x11,
	0x16, 0x01,
	0x17, 0xda,
	0x18, 0x40,
	0x19, 0x80,
	0x1a, 0x00,
	0x1b, 0x42,
	0x1c, 0xa9,
	0x30, 0x66,
	0x31, 0x90,
	0x32, 0x01,
	0x34, 0x00,
	0x35, 0x00,
	0x36, 0x20,
	0x38, 0x03,
	0x39, 0x20,
	0x3a, 0x88,
	0x40, 0x00,
	0x41, 0xff,
	0x42, 0xff,
	0x43, 0xff,
	0x44, 0xff,
	0x45, 0xff,
	0x46, 0xff,
	0x47, 0xff,
	0x48, 0xff,
	0x49, 0xff,
	0x4a, 0xff,
	0x4b, 0xff,
	0x4c, 0xff,
	0x4d, 0xff,
	0x4e, 0xff,
	0x4f, 0xff,
	0x50, 0xff,
	0x51, 0xff,
	0x52, 0xff,
	0x53, 0xff,
	0x54, 0xf4 /*0xff*/,
	0x55, 0xff,
	0x56, 0xff,
	0x57, 0xff,
	0x58, 0x40,
	0x59, 0x47,
	0x5a, 0x06 /*0x03*/,
	0x5b, 0x83,
	0x5d, 0x06,
	0x5e, 0x00,
	0x80, 0x30, /* window defined scaler operation, task A and B enabled */
	0x81, 0x03, /* use scaler datapath generated V */
	0x83, 0x00,
	0x84, 0x00,
	0x85, 0x00,
	0x86, 0x45,
	0x87, 0x31,
	0x88, 0xc0,
	0x90, 0x02, /* task A process top field */
	0x91, 0x08,
	0x92, 0x09,
	0x93, 0x80,
	0x94, 0x06,
	0x95, 0x00,
	0x96, 0xc0,
	0x97, 0x02,
	0x98, 0x12,
	0x99, 0x00,
	0x9a, 0xf2,
	0x9b, 0x00,
	0x9c, 0xd0,
	0x9d, 0x02,
	0x9e, 0xf2,
	0x9f, 0x00,
	0xa0, 0x01,
	0xa1, 0x01,
	0xa2, 0x01,
	0xa4, 0x80,
	0xa5, 0x40,
	0xa6, 0x40,
	0xa8, 0x00,
	0xa9, 0x04,
	0xaa, 0x00,
	0xac, 0x00,
	0xad, 0x02,
	0xae, 0x00,
	0xb0, 0x00,
	0xb1, 0x04,
	0xb2, 0x00,
	0xb3, 0x04,
	0xb4, 0x00,
	0xb8, 0x00,
	0xbc, 0x00,
	0xc0, 0x03,	/* task B process bottom field */
	0xc1, 0x08,
	0xc2, 0x09,
	0xc3, 0x80,
	0xc4, 0x06,
	0xc5, 0x00,
	0xc6, 0xc0,
	0xc7, 0x02,
	0xc8, 0x12,
	0xc9, 0x00,
	0xca, 0xf2,
	0xcb, 0x00,
	0xcc, 0xd0,
	0xcd, 0x02,
	0xce, 0xf2,
	0xcf, 0x00,
	0xd0, 0x01,
	0xd1, 0x01,
	0xd2, 0x01,
	0xd4, 0x80,
	0xd5, 0x40,
	0xd6, 0x40,
	0xd8, 0x00,
	0xd9, 0x04,
	0xda, 0x00,
	0xdc, 0x00,
	0xdd, 0x02,
	0xde, 0x00,
	0xe0, 0x00,
	0xe1, 0x04,
	0xe2, 0x00,
	0xe3, 0x04,
	0xe4, 0x00,
	0xe8, 0x00,
	0x88, 0xf0, /* End of original static list */
	0x00, 0x00, /* Terminator (reg 0x00 is read-only) */
};

static int write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int write_regs(struct i2c_client *client, u8 *regs)
{
	int i;

	for (i = 0; regs[i] != 0x00; i += 2)
		if (i2c_smbus_write_byte_data(client, regs[i], regs[i + 1]) < 0)
			return -1;
	return 0;
}

static int wis_saa7115_command(struct i2c_client *client,
				unsigned int cmd, void *arg)
{
	struct wis_saa7115 *dec = i2c_get_clientdata(client);

	switch (cmd) {
	case VIDIOC_S_INPUT:
	{
		int *input = arg;

		i2c_smbus_write_byte_data(client, 0x02, 0xC0 | *input);
		i2c_smbus_write_byte_data(client, 0x09,
				*input < 6 ? 0x40 : 0xC0);
		break;
	}
	case DECODER_SET_RESOLUTION:
	{
		struct video_decoder_resolution *res = arg;
		/* Course-grained scaler */
		int h_integer_scaler = res->width < 704 ? 704 / res->width : 1;
		/* Fine-grained scaler to take care of remainder */
		int h_scaling_increment = (704 / h_integer_scaler) *
					1024 / res->width;
		/* Fine-grained scaler only */
		int v_scaling_increment = (dec->norm & V4L2_STD_NTSC ?
				240 : 288) * 1024 / res->height;
		u8 regs[] = {
			0x88,	0xc0,
			0x9c,	res->width & 0xff,
			0x9d,	res->width >> 8,
			0x9e,	res->height & 0xff,
			0x9f,	res->height >> 8,
			0xa0,	h_integer_scaler,
			0xa1,	1,
			0xa2,	1,
			0xa8,	h_scaling_increment & 0xff,
			0xa9,	h_scaling_increment >> 8,
			0xac,	(h_scaling_increment / 2) & 0xff,
			0xad,	(h_scaling_increment / 2) >> 8,
			0xb0,	v_scaling_increment & 0xff,
			0xb1,	v_scaling_increment >> 8,
			0xb2,	v_scaling_increment & 0xff,
			0xb3,	v_scaling_increment >> 8,
			0xcc,	res->width & 0xff,
			0xcd,	res->width >> 8,
			0xce,	res->height & 0xff,
			0xcf,	res->height >> 8,
			0xd0,	h_integer_scaler,
			0xd1,	1,
			0xd2,	1,
			0xd8,	h_scaling_increment & 0xff,
			0xd9,	h_scaling_increment >> 8,
			0xdc,	(h_scaling_increment / 2) & 0xff,
			0xdd,	(h_scaling_increment / 2) >> 8,
			0xe0,	v_scaling_increment & 0xff,
			0xe1,	v_scaling_increment >> 8,
			0xe2,	v_scaling_increment & 0xff,
			0xe3,	v_scaling_increment >> 8,
			0x88,	0xf0,
			0,	0,
		};
		write_regs(client, regs);
		break;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *input = arg;
		u8 regs[] = {
			0x88,	0xc0,
			0x98,	*input & V4L2_STD_NTSC ? 0x12 : 0x16,
			0x9a,	*input & V4L2_STD_NTSC ? 0xf2 : 0x20,
			0x9b,	*input & V4L2_STD_NTSC ? 0x00 : 0x01,
			0xc8,	*input & V4L2_STD_NTSC ? 0x12 : 0x16,
			0xca,	*input & V4L2_STD_NTSC ? 0xf2 : 0x20,
			0xcb,	*input & V4L2_STD_NTSC ? 0x00 : 0x01,
			0x88,	0xf0,
			0x30,	*input & V4L2_STD_NTSC ? 0x66 : 0x00,
			0x31,	*input & V4L2_STD_NTSC ? 0x90 : 0xe0,
			0,	0,
		};
		write_regs(client, regs);
		dec->norm = *input;
		break;
	}
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *ctrl = arg;

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Brightness", sizeof(ctrl->name));
			ctrl->minimum = 0;
			ctrl->maximum = 255;
			ctrl->step = 1;
			ctrl->default_value = 128;
			ctrl->flags = 0;
			break;
		case V4L2_CID_CONTRAST:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Contrast", sizeof(ctrl->name));
			ctrl->minimum = 0;
			ctrl->maximum = 127;
			ctrl->step = 1;
			ctrl->default_value = 64;
			ctrl->flags = 0;
			break;
		case V4L2_CID_SATURATION:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Saturation", sizeof(ctrl->name));
			ctrl->minimum = 0;
			ctrl->maximum = 127;
			ctrl->step = 1;
			ctrl->default_value = 64;
			ctrl->flags = 0;
			break;
		case V4L2_CID_HUE:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Hue", sizeof(ctrl->name));
			ctrl->minimum = -128;
			ctrl->maximum = 127;
			ctrl->step = 1;
			ctrl->default_value = 0;
			ctrl->flags = 0;
			break;
		}
		break;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			if (ctrl->value > 255)
				dec->brightness = 255;
			else if (ctrl->value < 0)
				dec->brightness = 0;
			else
				dec->brightness = ctrl->value;
			write_reg(client, 0x0a, dec->brightness);
			break;
		case V4L2_CID_CONTRAST:
			if (ctrl->value > 127)
				dec->contrast = 127;
			else if (ctrl->value < 0)
				dec->contrast = 0;
			else
				dec->contrast = ctrl->value;
			write_reg(client, 0x0b, dec->contrast);
			break;
		case V4L2_CID_SATURATION:
			if (ctrl->value > 127)
				dec->saturation = 127;
			else if (ctrl->value < 0)
				dec->saturation = 0;
			else
				dec->saturation = ctrl->value;
			write_reg(client, 0x0c, dec->saturation);
			break;
		case V4L2_CID_HUE:
			if (ctrl->value > 127)
				dec->hue = 127;
			else if (ctrl->value < -128)
				dec->hue = -128;
			else
				dec->hue = ctrl->value;
			write_reg(client, 0x0d, dec->hue);
			break;
		}
		break;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			ctrl->value = dec->brightness;
			break;
		case V4L2_CID_CONTRAST:
			ctrl->value = dec->contrast;
			break;
		case V4L2_CID_SATURATION:
			ctrl->value = dec->saturation;
			break;
		case V4L2_CID_HUE:
			ctrl->value = dec->hue;
			break;
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

static int wis_saa7115_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct wis_saa7115 *dec;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	dec = kmalloc(sizeof(struct wis_saa7115), GFP_KERNEL);
	if (dec == NULL)
		return -ENOMEM;

	dec->norm = V4L2_STD_NTSC;
	dec->brightness = 128;
	dec->contrast = 64;
	dec->saturation = 64;
	dec->hue = 0;
	i2c_set_clientdata(client, dec);

	printk(KERN_DEBUG
		"wis-saa7115: initializing SAA7115 at address %d on %s\n",
		client->addr, adapter->name);

	if (write_regs(client, initial_registers) < 0) {
		printk(KERN_ERR
			"wis-saa7115: error initializing SAA7115\n");
		kfree(dec);
		return -ENODEV;
	}

	return 0;
}

static int wis_saa7115_remove(struct i2c_client *client)
{
	struct wis_saa7115 *dec = i2c_get_clientdata(client);

	kfree(dec);
	return 0;
}

static const struct i2c_device_id wis_saa7115_id[] = {
	{ "wis_saa7115", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wis_saa7115_id);

static struct i2c_driver wis_saa7115_driver = {
	.driver = {
		.name	= "WIS SAA7115 I2C driver",
	},
	.probe		= wis_saa7115_probe,
	.remove		= wis_saa7115_remove,
	.command	= wis_saa7115_command,
	.id_table	= wis_saa7115_id,
};

module_i2c_driver(wis_saa7115_driver);

MODULE_LICENSE("GPL v2");
