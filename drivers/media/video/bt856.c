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
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/types.h>

#include <linux/videodev.h>
#include <asm/uaccess.h>

MODULE_DESCRIPTION("Brooktree-856A video encoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

#include <linux/i2c.h>

#define I2C_NAME(s) (s)->name

#include <linux/video_encoder.h>

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

/* ----------------------------------------------------------------------- */

#define REG_OFFSET	0xDA
#define BT856_NR_REG	6

struct bt856 {
	unsigned char reg[BT856_NR_REG];

	int norm;
	int enable;
};

#define   I2C_BT856        0x88

/* ----------------------------------------------------------------------- */

static inline int
bt856_write (struct i2c_client *client,
	     u8                 reg,
	     u8                 value)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	encoder->reg[reg - REG_OFFSET] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int
bt856_setbit (struct i2c_client *client,
	      u8                 reg,
	      u8                 bit,
	      u8                 value)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	return bt856_write(client, reg,
			   (encoder->
			    reg[reg - REG_OFFSET] & ~(1 << bit)) |
			    (value ? (1 << bit) : 0));
}

static void
bt856_dump (struct i2c_client *client)
{
	int i;
	struct bt856 *encoder = i2c_get_clientdata(client);

	printk(KERN_INFO "%s: register dump:", I2C_NAME(client));
	for (i = 0; i < BT856_NR_REG; i += 2)
		printk(" %02x", encoder->reg[i]);
	printk("\n");
}

/* ----------------------------------------------------------------------- */

static int
bt856_command (struct i2c_client *client,
	       unsigned int       cmd,
	       void              *arg)
{
	struct bt856 *encoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
		/* This is just for testing!!! */
		dprintk(1, KERN_INFO "bt856: init\n");
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

		dprintk(1, KERN_INFO "%s: get capabilities\n",
			I2C_NAME(client));

		cap->flags = VIDEO_ENCODER_PAL |
			     VIDEO_ENCODER_NTSC |
			     VIDEO_ENCODER_CCIR;
		cap->inputs = 2;
		cap->outputs = 1;
	}
		break;

	case ENCODER_SET_NORM:
	{
		int *iarg = arg;

		dprintk(1, KERN_INFO "%s: set norm %d\n", I2C_NAME(client),
			*iarg);

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
	}
		break;

	case ENCODER_SET_INPUT:
	{
		int *iarg = arg;

		dprintk(1, KERN_INFO "%s: set input %d\n", I2C_NAME(client),
			*iarg);

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
	}
		break;

	case ENCODER_SET_OUTPUT:
	{
		int *iarg = arg;

		dprintk(1, KERN_INFO "%s: set output %d\n", I2C_NAME(client),
			*iarg);

		/* not much choice of outputs */
		if (*iarg != 0) {
			return -EINVAL;
		}
	}
		break;

	case ENCODER_ENABLE_OUTPUT:
	{
		int *iarg = arg;

		encoder->enable = !!*iarg;

		dprintk(1, KERN_INFO "%s: enable output %d\n",
			I2C_NAME(client), encoder->enable);
	}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */
static unsigned short normal_i2c[] = { I2C_BT856 >> 1, I2C_CLIENT_END };

static unsigned short ignore = I2C_CLIENT_END;

static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver i2c_driver_bt856;

static int
bt856_detect_client (struct i2c_adapter *adapter,
		     int                 address,
		     int                 kind)
{
	int i;
	struct i2c_client *client;
	struct bt856 *encoder;

	dprintk(1,
		KERN_INFO
		"bt856.c: detecting bt856 client on address 0x%x\n",
		address << 1);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_bt856;
	strlcpy(I2C_NAME(client), "bt856", sizeof(I2C_NAME(client)));

	encoder = kzalloc(sizeof(struct bt856), GFP_KERNEL);
	if (encoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	encoder->norm = VIDEO_MODE_NTSC;
	encoder->enable = 1;
	i2c_set_clientdata(client, encoder);

	i = i2c_attach_client(client);
	if (i) {
		kfree(client);
		kfree(encoder);
		return i;
	}

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

	dprintk(1, KERN_INFO "%s_attach: at address 0x%x\n", I2C_NAME(client),
		client->addr << 1);

	return 0;
}

static int
bt856_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"bt856.c: starting probe for adapter %s (0x%x)\n",
		I2C_NAME(adapter), adapter->id);
	return i2c_probe(adapter, &addr_data, &bt856_detect_client);
}

static int
bt856_detach_client (struct i2c_client *client)
{
	struct bt856 *encoder = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(encoder);
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_bt856 = {
	.driver = {
		.name = "bt856",
	},

	.id = I2C_DRIVERID_BT856,

	.attach_adapter = bt856_attach_adapter,
	.detach_client = bt856_detach_client,
	.command = bt856_command,
};

static int __init
bt856_init (void)
{
	return i2c_add_driver(&i2c_driver_bt856);
}

static void __exit
bt856_exit (void)
{
	i2c_del_driver(&i2c_driver_bt856);
}

module_init(bt856_init);
module_exit(bt856_exit);
