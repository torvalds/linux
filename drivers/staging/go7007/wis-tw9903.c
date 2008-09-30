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
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/ioctl.h>

#include "wis-i2c.h"

struct wis_tw9903 {
	int norm;
	int brightness;
	int contrast;
	int hue;
};

static u8 initial_registers[] =
{
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

static int wis_tw9903_command(struct i2c_client *client,
				unsigned int cmd, void *arg)
{
	struct wis_tw9903 *dec = i2c_get_clientdata(client);

	switch (cmd) {
	case VIDIOC_S_INPUT:
	{
		int *input = arg;

		i2c_smbus_write_byte_data(client, 0x02, 0x40 | (*input << 1));
		break;
	}
#if 0   /* The scaler on this thing seems to be horribly broken */
	case DECODER_SET_RESOLUTION:
	{
		struct video_decoder_resolution *res = arg;
		/*int hscale = 256 * 720 / res->width;*/
		int hscale = 256 * 720 / (res->width - (res->width > 704 ? 0 : 8));
		int vscale = 256 * (dec->norm & V4L2_STD_NTSC ?  240 : 288)
				/ res->height;
		u8 regs[] = {
			0x0d, vscale & 0xff,
			0x0f, hscale & 0xff,
			0x0e, ((vscale & 0xf00) >> 4) | ((hscale & 0xf00) >> 8),
			0x06, 0xc0, /* reset device */
			0,	0,
		};
		printk(KERN_DEBUG "vscale is %04x, hscale is %04x\n",
				vscale, hscale);
		/*write_regs(client, regs);*/
		break;
	}
#endif
	case VIDIOC_S_STD:
	{
		v4l2_std_id *input = arg;
		u8 regs[] = {
			0x05, *input & V4L2_STD_NTSC ? 0x80 : 0x00,
			0x07, *input & V4L2_STD_NTSC ? 0x02 : 0x12,
			0x08, *input & V4L2_STD_NTSC ? 0x14 : 0x18,
			0x09, *input & V4L2_STD_NTSC ? 0xf0 : 0x20,
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
			ctrl->minimum = -128;
			ctrl->maximum = 127;
			ctrl->step = 1;
			ctrl->default_value = 0x00;
			ctrl->flags = 0;
			break;
		case V4L2_CID_CONTRAST:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Contrast", sizeof(ctrl->name));
			ctrl->minimum = 0;
			ctrl->maximum = 255;
			ctrl->step = 1;
			ctrl->default_value = 0x60;
			ctrl->flags = 0;
			break;
#if 0
		/* I don't understand how the Chroma Gain registers work... */
		case V4L2_CID_SATURATION:
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strncpy(ctrl->name, "Saturation", sizeof(ctrl->name));
			ctrl->minimum = 0;
			ctrl->maximum = 127;
			ctrl->step = 1;
			ctrl->default_value = 64;
			ctrl->flags = 0;
			break;
#endif
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
			if (ctrl->value > 127)
				dec->brightness = 127;
			else if (ctrl->value < -128)
				dec->brightness = -128;
			else
				dec->brightness = ctrl->value;
			write_reg(client, 0x10, dec->brightness);
			break;
		case V4L2_CID_CONTRAST:
			if (ctrl->value > 255)
				dec->contrast = 255;
			else if (ctrl->value < 0)
				dec->contrast = 0;
			else
				dec->contrast = ctrl->value;
			write_reg(client, 0x11, dec->contrast);
			break;
#if 0
		case V4L2_CID_SATURATION:
			if (ctrl->value > 127)
				dec->saturation = 127;
			else if (ctrl->value < 0)
				dec->saturation = 0;
			else
				dec->saturation = ctrl->value;
			/*write_reg(client, 0x0c, dec->saturation);*/
			break;
#endif
		case V4L2_CID_HUE:
			if (ctrl->value > 127)
				dec->hue = 127;
			else if (ctrl->value < -128)
				dec->hue = -128;
			else
				dec->hue = ctrl->value;
			write_reg(client, 0x15, dec->hue);
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
#if 0
		case V4L2_CID_SATURATION:
			ctrl->value = dec->saturation;
			break;
#endif
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

static struct i2c_driver wis_tw9903_driver;

static struct i2c_client wis_tw9903_client_templ = {
	.name		= "TW9903 (WIS)",
	.driver		= &wis_tw9903_driver,
};

static int wis_tw9903_detect(struct i2c_adapter *adapter, int addr, int kind)
{
	struct i2c_client *client;
	struct wis_tw9903 *dec;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memcpy(client, &wis_tw9903_client_templ,
			sizeof(wis_tw9903_client_templ));
	client->adapter = adapter;
	client->addr = addr;

	dec = kmalloc(sizeof(struct wis_tw9903), GFP_KERNEL);
	if (dec == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	dec->norm = V4L2_STD_NTSC;
	dec->brightness = 0;
	dec->contrast = 0x60;
	dec->hue = 0;
	i2c_set_clientdata(client, dec);

	printk(KERN_DEBUG
		"wis-tw9903: initializing TW9903 at address %d on %s\n",
		addr, adapter->name);

	if (write_regs(client, initial_registers) < 0) {
		printk(KERN_ERR "wis-tw9903: error initializing TW9903\n");
		kfree(client);
		kfree(dec);
		return 0;
	}

	i2c_attach_client(client);
	return 0;
}

static int wis_tw9903_detach(struct i2c_client *client)
{
	struct wis_tw9903 *dec = i2c_get_clientdata(client);
	int r;

	r = i2c_detach_client(client);
	if (r < 0)
		return r;

	kfree(client);
	kfree(dec);
	return 0;
}

static struct i2c_driver wis_tw9903_driver = {
	.driver = {
		.name	= "WIS TW9903 I2C driver",
	},
	.id		= I2C_DRIVERID_WIS_TW9903,
	.detach_client	= wis_tw9903_detach,
	.command	= wis_tw9903_command,
};

static int __init wis_tw9903_init(void)
{
	int r;

	r = i2c_add_driver(&wis_tw9903_driver);
	if (r < 0)
		return r;
	return wis_i2c_add_driver(wis_tw9903_driver.id, wis_tw9903_detect);
}

static void __exit wis_tw9903_cleanup(void)
{
	wis_i2c_del_driver(wis_tw9903_detect);
	i2c_del_driver(&wis_tw9903_driver);
}

module_init(wis_tw9903_init);
module_exit(wis_tw9903_cleanup);

MODULE_LICENSE("GPL v2");
