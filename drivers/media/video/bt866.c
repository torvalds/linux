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
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/i2c.h>

#include <linux/videodev.h>
#include <asm/uaccess.h>

#include <linux/video_encoder.h>

MODULE_LICENSE("GPL");

#define	BT866_DEVNAME	"bt866"
#define I2C_BT866	0x88

MODULE_LICENSE("GPL");

#define DEBUG(x)		/* Debug driver */

/* ----------------------------------------------------------------------- */

struct bt866 {
	struct i2c_client *i2c;
	int addr;
	unsigned char reg[256];

	int norm;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static int bt866_write(struct bt866 *dev,
			unsigned char subaddr, unsigned char data);

static int bt866_do_command(struct bt866 *encoder,
			unsigned int cmd, void *arg)
{
	switch (cmd) {
	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		DEBUG(printk
		      (KERN_INFO "%s: get capabilities\n",
		       encoder->i2c->name));

		cap->flags
			= VIDEO_ENCODER_PAL
			| VIDEO_ENCODER_NTSC
			| VIDEO_ENCODER_CCIR;
		cap->inputs = 2;
		cap->outputs = 1;
	}
	break;

	case ENCODER_SET_NORM:
	{
		int *iarg = arg;

		DEBUG(printk(KERN_INFO "%s: set norm %d\n",
			     encoder->i2c->name, *iarg));

		switch (*iarg) {

		case VIDEO_MODE_NTSC:
			break;

		case VIDEO_MODE_PAL:
			break;

		default:
			return -EINVAL;

		}
		encoder->norm = *iarg;
	}
	break;

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
			bt866_write(encoder, init[i], init[i+1]);

		val = encoder->reg[0xdc];

		if (*iarg == 0)
			val |= 0x40; /* CBSWAP */
		else
			val &= ~0x40; /* !CBSWAP */

		bt866_write(encoder, 0xdc, val);

		val = encoder->reg[0xcc];
		if (*iarg == 2)
			val |= 0x01; /* OSDBAR */
		else
			val &= ~0x01; /* !OSDBAR */
		bt866_write(encoder, 0xcc, val);

		DEBUG(printk(KERN_INFO "%s: set input %d\n",
			     encoder->i2c->name, *iarg));

		switch (*iarg) {
		case 0:
			break;
		case 1:
			break;
		default:
			return -EINVAL;

		}
	}
	break;

	case ENCODER_SET_OUTPUT:
	{
		int *iarg = arg;

		DEBUG(printk(KERN_INFO "%s: set output %d\n",
			     encoder->i2c->name, *iarg));

		/* not much choice of outputs */
		if (*iarg != 0)
			return -EINVAL;
	}
	break;

	case ENCODER_ENABLE_OUTPUT:
	{
		int *iarg = arg;
		encoder->enable = !!*iarg;

		DEBUG(printk
		      (KERN_INFO "%s: enable output %d\n",
		       encoder->i2c->name, encoder->enable));
	}
	break;

	case 4711:
	{
		int *iarg = arg;
		__u8 val;

		printk("bt866: square = %d\n", *iarg);

		val = encoder->reg[0xdc];
		if (*iarg)
			val |= 1; /* SQUARE */
		else
			val &= ~1; /* !SQUARE */
		bt866_write(encoder, 0xdc, val);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

static int bt866_write(struct bt866 *encoder,
			unsigned char subaddr, unsigned char data)
{
	unsigned char buffer[2];
	int err;

	buffer[0] = subaddr;
	buffer[1] = data;

	encoder->reg[subaddr] = data;

	DEBUG(printk
	      ("%s: write 0x%02X = 0x%02X\n",
	       encoder->i2c->name, subaddr, data));

	for (err = 0; err < 3;) {
		if (i2c_master_send(encoder->i2c, buffer, 2) == 2)
			break;
		err++;
		printk(KERN_WARNING "%s: I/O error #%d "
		       "(write 0x%02x/0x%02x)\n",
		       encoder->i2c->name, err, encoder->addr, subaddr);
		schedule_timeout_interruptible(msecs_to_jiffies(100));
	}
	if (err == 3) {
		printk(KERN_WARNING "%s: giving up\n",
		       encoder->i2c->name);
		return -1;
	}

	return 0;
}

static int bt866_attach(struct i2c_adapter *adapter);
static int bt866_detach(struct i2c_client *client);
static int bt866_command(struct i2c_client *client,
			 unsigned int cmd, void *arg);


/* Addresses to scan */
static unsigned short normal_i2c[]	= {I2C_BT866>>1, I2C_CLIENT_END};
static unsigned short probe[2]		= {I2C_CLIENT_END, I2C_CLIENT_END};
static unsigned short ignore[2]		= {I2C_CLIENT_END, I2C_CLIENT_END};

static struct i2c_client_address_data addr_data = {
	normal_i2c,
	probe,
	ignore,
};

static struct i2c_driver i2c_driver_bt866 = {
	.driver.name = BT866_DEVNAME,
	.id = I2C_DRIVERID_BT866,
	.attach_adapter = bt866_attach,
	.detach_client = bt866_detach,
	.command = bt866_command
};


static struct i2c_client bt866_client_tmpl =
{
	.name = "(nil)",
	.addr = 0,
	.adapter = NULL,
	.driver = &i2c_driver_bt866,
	.usage_count = 0
};

static int bt866_found_proc(struct i2c_adapter *adapter,
			    int addr, int kind)
{
	struct bt866 *encoder;
	struct i2c_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memcpy(client, &bt866_client_tmpl, sizeof(*client));

	encoder = kzalloc(sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}

	i2c_set_clientdata(client, encoder);
	client->adapter = adapter;
	client->addr = addr;
	sprintf(client->name, "%s-%02x", BT866_DEVNAME, adapter->id);

	encoder->i2c = client;
	encoder->addr = addr;
	//encoder->encoder_type = ENCODER_TYPE_UNKNOWN;

	/* initialize */

	i2c_attach_client(client);

	return 0;
}

static int bt866_attach(struct i2c_adapter *adapter)
{
	if (adapter->id == I2C_HW_B_ZR36067)
		return i2c_probe(adapter, &addr_data, bt866_found_proc);
	return 0;
}

static int bt866_detach(struct i2c_client *client)
{
	struct bt866 *encoder = i2c_get_clientdata(client);

	i2c_detach_client(client);
	kfree(encoder);
	kfree(client);

	return 0;
}

static int bt866_command(struct i2c_client *client,
			 unsigned int cmd, void *arg)
{
	struct bt866 *encoder = i2c_get_clientdata(client);
	return bt866_do_command(encoder, cmd, arg);
}

static int __devinit bt866_init(void)
{
	i2c_add_driver(&i2c_driver_bt866);
	return 0;
}

static void __devexit bt866_exit(void)
{
	i2c_del_driver(&i2c_driver_bt866);
}

module_init(bt866_init);
module_exit(bt866_exit);
