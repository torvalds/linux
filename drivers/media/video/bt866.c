/*
    bt866 - BT866 Digital Video Encoder (Rockwell Part)

    Copyright (C) 1999 Mike Bernson <mike@mlb.org>
    Copyright (C) 1998 Dave Perks <dperks@ibm.net>

    Modifications for LML33/DC10plus unified driver
    Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>

    This code was modify/ported from the saa7111 driver written
    by Dave Perks.

    This code was adapted for the bt866 by Christer Weinigel and ported
    to 2.6 by Martin Samuelsson.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev.h>
#include <linux/video_encoder.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("Brooktree-866 video encoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

struct bt866 {
	u8 reg[256];

	int norm;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static int bt866_write(struct i2c_client *client, u8 subaddr, u8 data)
{
	struct bt866 *encoder = i2c_get_clientdata(client);
	u8 buffer[2];
	int err;

	buffer[0] = subaddr;
	buffer[1] = data;

	encoder->reg[subaddr] = data;

	v4l_dbg(1, debug, client, "write 0x%02x = 0x%02x\n", subaddr, data);

	for (err = 0; err < 3;) {
		if (i2c_master_send(client, buffer, 2) == 2)
			break;
		err++;
		v4l_warn(client, "error #%d writing to 0x%02x\n",
				err, subaddr);
		schedule_timeout_interruptible(msecs_to_jiffies(100));
	}
	if (err == 3) {
		v4l_warn(client, "giving up\n");
		return -1;
	}

	return 0;
}

static int bt866_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct bt866 *encoder = i2c_get_clientdata(client);

	switch (cmd) {
	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		v4l_dbg(1, debug, client, "get capabilities\n");

		cap->flags
			= VIDEO_ENCODER_PAL
			| VIDEO_ENCODER_NTSC
			| VIDEO_ENCODER_CCIR;
		cap->inputs = 2;
		cap->outputs = 1;
		break;
	}

	case ENCODER_SET_NORM:
	{
		int *iarg = arg;

		v4l_dbg(1, debug, client, "set norm %d\n", *iarg);

		switch (*iarg) {
		case VIDEO_MODE_NTSC:
			break;

		case VIDEO_MODE_PAL:
			break;

		default:
			return -EINVAL;
		}
		encoder->norm = *iarg;
		break;
	}

	case ENCODER_SET_INPUT:
	{
		int *iarg = arg;
		static const __u8 init[] = {
			0xc8, 0xcc, /* CRSCALE */
			0xca, 0x91, /* CBSCALE */
			0xcc, 0x24, /* YC16 | OSDNUM */
			0xda, 0x00, /*  */
			0xdc, 0x24, /* SETMODE | PAL */
			0xde, 0x02, /* EACTIVE */

			/* overlay colors */
			0x70, 0xEB, 0x90, 0x80, 0xB0, 0x80, /* white */
			0x72, 0xA2, 0x92, 0x8E, 0xB2, 0x2C, /* yellow */
			0x74, 0x83, 0x94, 0x2C, 0xB4, 0x9C, /* cyan */
			0x76, 0x70, 0x96, 0x3A, 0xB6, 0x48, /* green */
			0x78, 0x54, 0x98, 0xC6, 0xB8, 0xB8, /* magenta */
			0x7A, 0x41, 0x9A, 0xD4, 0xBA, 0x64, /* red */
			0x7C, 0x23, 0x9C, 0x72, 0xBC, 0xD4, /* blue */
			0x7E, 0x10, 0x9E, 0x80, 0xBE, 0x80, /* black */

			0x60, 0xEB, 0x80, 0x80, 0xc0, 0x80, /* white */
			0x62, 0xA2, 0x82, 0x8E, 0xc2, 0x2C, /* yellow */
			0x64, 0x83, 0x84, 0x2C, 0xc4, 0x9C, /* cyan */
			0x66, 0x70, 0x86, 0x3A, 0xc6, 0x48, /* green */
			0x68, 0x54, 0x88, 0xC6, 0xc8, 0xB8, /* magenta */
			0x6A, 0x41, 0x8A, 0xD4, 0xcA, 0x64, /* red */
			0x6C, 0x23, 0x8C, 0x72, 0xcC, 0xD4, /* blue */
			0x6E, 0x10, 0x8E, 0x80, 0xcE, 0x80, /* black */
		};
		int i;
		u8 val;

		for (i = 0; i < ARRAY_SIZE(init) / 2; i += 2)
			bt866_write(client, init[i], init[i+1]);

		val = encoder->reg[0xdc];

		if (*iarg == 0)
			val |= 0x40; /* CBSWAP */
		else
			val &= ~0x40; /* !CBSWAP */

		bt866_write(client, 0xdc, val);

		val = encoder->reg[0xcc];
		if (*iarg == 2)
			val |= 0x01; /* OSDBAR */
		else
			val &= ~0x01; /* !OSDBAR */
		bt866_write(client, 0xcc, val);

		v4l_dbg(1, debug, client, "set input %d\n", *iarg);

		switch (*iarg) {
		case 0:
			break;
		case 1:
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	case ENCODER_SET_OUTPUT:
	{
		int *iarg = arg;

		v4l_dbg(1, debug, client, "set output %d\n", *iarg);

		/* not much choice of outputs */
		if (*iarg != 0)
			return -EINVAL;
		break;
	}

	case ENCODER_ENABLE_OUTPUT:
	{
		int *iarg = arg;
		encoder->enable = !!*iarg;

		v4l_dbg(1, debug, client, "enable output %d\n", encoder->enable);
		break;
	}

	case 4711:
	{
		int *iarg = arg;
		__u8 val;

		v4l_dbg(1, debug, client, "square %d\n", *iarg);

		val = encoder->reg[0xdc];
		if (*iarg)
			val |= 1; /* SQUARE */
		else
			val &= ~1; /* !SQUARE */
		bt866_write(client, 0xdc, val);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned short normal_i2c[] = { 0x88 >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int bt866_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bt866 *encoder;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = kzalloc(sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, encoder);
	return 0;
}

static int bt866_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static int bt866_legacy_probe(struct i2c_adapter *adapter)
{
	return adapter->id == I2C_HW_B_ZR36067;
}

static const struct i2c_device_id bt866_id[] = {
	{ "bt866", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bt866_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "bt866",
	.driverid = I2C_DRIVERID_BT866,
	.command = bt866_command,
	.probe = bt866_probe,
	.remove = bt866_remove,
	.legacy_probe = bt866_legacy_probe,
	.id_table = bt866_id,
};
