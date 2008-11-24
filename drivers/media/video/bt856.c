/*
 * bt856 - BT856A Digital Video Encoder (Rockwell Part)
 *
 * Copyright (C) 1999 Mike Bernson <mike@mlb.org>
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * Modifications for LML33/DC10plus unified driver
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * This code was modify/ported from the saa7111 driver written
 * by Dave Perks.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *   - moved over to linux>=2.4.x i2c protocol (9/9/2002)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

MODULE_DESCRIPTION("Brooktree-856A video encoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

#define BT856_REG_OFFSET	0xDA
#define BT856_NR_REG		6

struct bt856 {
	unsigned char reg[BT856_NR_REG];

	int norm;
	int enable;
};

/* ----------------------------------------------------------------------- */

static inline int bt856_write(struct i2c_client *client, u8 reg, u8 value)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	encoder->reg[reg - BT856_REG_OFFSET] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int bt856_setbit(struct i2c_client *client, u8 reg, u8 bit, u8 value)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	return bt856_write(client, reg,
		(encoder->reg[reg - BT856_REG_OFFSET] & ~(1 << bit)) |
				(value ? (1 << bit) : 0));
}

static void bt856_dump(struct i2c_client *client)
{
	int i;
	struct bt856 *encoder = i2c_get_clientdata(client);

	v4l_info(client, "register dump:\n");
	for (i = 0; i < BT856_NR_REG; i += 2)
		printk(KERN_CONT " %02x", encoder->reg[i]);
	printk(KERN_CONT "\n");
}

/* ----------------------------------------------------------------------- */

static int bt856_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	switch (cmd) {
	case 0:
		/* This is just for testing!!! */
		v4l_dbg(1, debug, client, "init\n");
		bt856_write(client, 0xdc, 0x18);
		bt856_write(client, 0xda, 0);
		bt856_write(client, 0xde, 0);

		bt856_setbit(client, 0xdc, 3, 1);
		//bt856_setbit(client, 0xdc, 6, 0);
		bt856_setbit(client, 0xdc, 4, 1);

		switch (encoder->norm) {
		case VIDEO_MODE_NTSC:
			bt856_setbit(client, 0xdc, 2, 0);
			break;

		case VIDEO_MODE_PAL:
			bt856_setbit(client, 0xdc, 2, 1);
			break;
		}

		bt856_setbit(client, 0xdc, 1, 1);
		bt856_setbit(client, 0xde, 4, 0);
		bt856_setbit(client, 0xde, 3, 1);
		if (debug != 0)
			bt856_dump(client);
		break;

	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		v4l_dbg(1, debug, client, "get capabilities\n");

		cap->flags = VIDEO_ENCODER_PAL |
			     VIDEO_ENCODER_NTSC |
			     VIDEO_ENCODER_CCIR;
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
			bt856_setbit(client, 0xdc, 2, 0);
			break;

		case VIDEO_MODE_PAL:
			bt856_setbit(client, 0xdc, 2, 1);
			bt856_setbit(client, 0xda, 0, 0);
			//bt856_setbit(client, 0xda, 0, 1);
			break;

		default:
			return -EINVAL;
		}
		encoder->norm = *iarg;
		if (debug != 0)
			bt856_dump(client);
		break;
	}

	case ENCODER_SET_INPUT:
	{
		int *iarg = arg;

		v4l_dbg(1, debug, client, "set input %d\n", *iarg);

		/* We only have video bus.
		 * iarg = 0: input is from bt819
		 * iarg = 1: input is from ZR36060 */
		switch (*iarg) {
		case 0:
			bt856_setbit(client, 0xde, 4, 0);
			bt856_setbit(client, 0xde, 3, 1);
			bt856_setbit(client, 0xdc, 3, 1);
			bt856_setbit(client, 0xdc, 6, 0);
			break;
		case 1:
			bt856_setbit(client, 0xde, 4, 0);
			bt856_setbit(client, 0xde, 3, 1);
			bt856_setbit(client, 0xdc, 3, 1);
			bt856_setbit(client, 0xdc, 6, 1);
			break;
		case 2:	// Color bar
			bt856_setbit(client, 0xdc, 3, 0);
			bt856_setbit(client, 0xde, 4, 1);
			break;
		default:
			return -EINVAL;
		}

		if (debug != 0)
			bt856_dump(client);
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

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static unsigned short normal_i2c[] = { 0x88 >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int bt856_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bt856 *encoder;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	encoder = kzalloc(sizeof(struct bt856), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;
	encoder->norm = VIDEO_MODE_NTSC;
	encoder->enable = 1;
	i2c_set_clientdata(client, encoder);

	bt856_write(client, 0xdc, 0x18);
	bt856_write(client, 0xda, 0);
	bt856_write(client, 0xde, 0);

	bt856_setbit(client, 0xdc, 3, 1);
	//bt856_setbit(client, 0xdc, 6, 0);
	bt856_setbit(client, 0xdc, 4, 1);

	switch (encoder->norm) {

	case VIDEO_MODE_NTSC:
		bt856_setbit(client, 0xdc, 2, 0);
		break;

	case VIDEO_MODE_PAL:
		bt856_setbit(client, 0xdc, 2, 1);
		break;
	}

	bt856_setbit(client, 0xdc, 1, 1);
	bt856_setbit(client, 0xde, 4, 0);
	bt856_setbit(client, 0xde, 3, 1);

	if (debug != 0)
		bt856_dump(client);
	return 0;
}

static int bt856_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id bt856_id[] = {
	{ "bt856", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bt856_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "bt856",
	.driverid = I2C_DRIVERID_BT856,
	.command = bt856_command,
	.probe = bt856_probe,
	.remove = bt856_remove,
	.id_table = bt856_id,
};
