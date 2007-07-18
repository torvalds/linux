/*
 * adv7170 - adv7170, adv7171 video encoder driver version 0.0.1
 *
 * Copyright (C) 2002 Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Based on adv7176 driver by:
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (1/1/2003)
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
#include <linux/types.h>
#include <linux/i2c.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include <linux/videodev.h>
#include <linux/video_encoder.h>

MODULE_DESCRIPTION("Analog Devices ADV7170 video encoder driver");
MODULE_AUTHOR("Maxim Yevtyushkin");
MODULE_LICENSE("GPL");


#define I2C_NAME(x) (x)->name


static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

/* ----------------------------------------------------------------------- */

struct adv7170 {
	unsigned char reg[128];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

#define   I2C_ADV7170        0xd4
#define   I2C_ADV7171        0x54

static char adv7170_name[] = "adv7170";
static char adv7171_name[] = "adv7171";

static char *inputs[] = { "pass_through", "play_back" };
static char *norms[] = { "PAL", "NTSC" };

/* ----------------------------------------------------------------------- */

static inline int
adv7170_write (struct i2c_client *client,
	       u8                 reg,
	       u8                 value)
{
	struct adv7170 *encoder = i2c_get_clientdata(client);

	encoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int
adv7170_read (struct i2c_client *client,
	      u8                 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int
adv7170_write_block (struct i2c_client *client,
		     const u8          *data,
		     unsigned int       len)
{
	int ret = -1;
	u8 reg;

	/* the adv7170 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		struct adv7170 *encoder = i2c_get_clientdata(client);
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] =
				    encoder->reg[reg++] = data[1];
				len -= 2;
				data += 2;
			} while (len >= 2 && data[0] == reg &&
				 block_len < 32);
			if ((ret = i2c_master_send(client, block_data,
						   block_len)) < 0)
				break;
		}
	} else {
		/* do some slow I2C emulation kind of thing */
		while (len >= 2) {
			reg = *data++;
			if ((ret = adv7170_write(client, reg,
						 *data++)) < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

/* ----------------------------------------------------------------------- */
// Output filter:  S-Video  Composite

#define MR050       0x11	//0x09
#define MR060       0x14	//0x0c

//---------------------------------------------------------------------------

#define TR0MODE     0x4c
#define TR0RST	    0x80

#define TR1CAPT	    0x00
#define TR1PLAY	    0x00


static const unsigned char init_NTSC[] = {
	0x00, 0x10,		// MR0
	0x01, 0x20,		// MR1
	0x02, 0x0e,		// MR2 RTC control: bits 2 and 1
	0x03, 0x80,		// MR3
	0x04, 0x30,		// MR4
	0x05, 0x00,		// Reserved
	0x06, 0x00,		// Reserved
	0x07, TR0MODE,		// TM0
	0x08, TR1CAPT,		// TM1
	0x09, 0x16,		// Fsc0
	0x0a, 0x7c,		// Fsc1
	0x0b, 0xf0,		// Fsc2
	0x0c, 0x21,		// Fsc3
	0x0d, 0x00,		// Subcarrier Phase
	0x0e, 0x00,		// Closed Capt. Ext 0
	0x0f, 0x00,		// Closed Capt. Ext 1
	0x10, 0x00,		// Closed Capt. 0
	0x11, 0x00,		// Closed Capt. 1
	0x12, 0x00,		// Pedestal Ctl 0
	0x13, 0x00,		// Pedestal Ctl 1
	0x14, 0x00,		// Pedestal Ctl 2
	0x15, 0x00,		// Pedestal Ctl 3
	0x16, 0x00,		// CGMS_WSS_0
	0x17, 0x00,		// CGMS_WSS_1
	0x18, 0x00,		// CGMS_WSS_2
	0x19, 0x00,		// Teletext Ctl
};

static const unsigned char init_PAL[] = {
	0x00, 0x71,		// MR0
	0x01, 0x20,		// MR1
	0x02, 0x0e,		// MR2 RTC control: bits 2 and 1
	0x03, 0x80,		// MR3
	0x04, 0x30,		// MR4
	0x05, 0x00,		// Reserved
	0x06, 0x00,		// Reserved
	0x07, TR0MODE,		// TM0
	0x08, TR1CAPT,		// TM1
	0x09, 0xcb,		// Fsc0
	0x0a, 0x8a,		// Fsc1
	0x0b, 0x09,		// Fsc2
	0x0c, 0x2a,		// Fsc3
	0x0d, 0x00,		// Subcarrier Phase
	0x0e, 0x00,		// Closed Capt. Ext 0
	0x0f, 0x00,		// Closed Capt. Ext 1
	0x10, 0x00,		// Closed Capt. 0
	0x11, 0x00,		// Closed Capt. 1
	0x12, 0x00,		// Pedestal Ctl 0
	0x13, 0x00,		// Pedestal Ctl 1
	0x14, 0x00,		// Pedestal Ctl 2
	0x15, 0x00,		// Pedestal Ctl 3
	0x16, 0x00,		// CGMS_WSS_0
	0x17, 0x00,		// CGMS_WSS_1
	0x18, 0x00,		// CGMS_WSS_2
	0x19, 0x00,		// Teletext Ctl
};


static int
adv7170_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void *             arg)
{
	struct adv7170 *encoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
#if 0
		/* This is just for testing!!! */
		adv7170_write_block(client, init_common,
				    sizeof(init_common));
		adv7170_write(client, 0x07, TR0MODE | TR0RST);
		adv7170_write(client, 0x07, TR0MODE);
#endif
		break;

	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		cap->flags = VIDEO_ENCODER_PAL |
			     VIDEO_ENCODER_NTSC;
		cap->inputs = 2;
		cap->outputs = 1;
	}
		break;

	case ENCODER_SET_NORM:
	{
		int iarg = *(int *) arg;

		dprintk(1, KERN_DEBUG "%s_command: set norm %d",
			I2C_NAME(client), iarg);

		switch (iarg) {

		case VIDEO_MODE_NTSC:
			adv7170_write_block(client, init_NTSC,
					    sizeof(init_NTSC));
			if (encoder->input == 0)
				adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			break;

		case VIDEO_MODE_PAL:
			adv7170_write_block(client, init_PAL,
					    sizeof(init_PAL));
			if (encoder->input == 0)
				adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			break;

		default:
			dprintk(1, KERN_ERR "%s: illegal norm: %d\n",
			       I2C_NAME(client), iarg);
			return -EINVAL;

		}
		dprintk(1, KERN_DEBUG "%s: switched to %s\n", I2C_NAME(client),
			norms[iarg]);
		encoder->norm = iarg;
	}
		break;

	case ENCODER_SET_INPUT:
	{
		int iarg = *(int *) arg;

		/* RJ: *iarg = 0: input is from decoder
		 *iarg = 1: input is from ZR36060
		 *iarg = 2: color bar */

		dprintk(1, KERN_DEBUG "%s_command: set input from %s\n",
			I2C_NAME(client),
			iarg == 0 ? "decoder" : "ZR36060");

		switch (iarg) {

		case 0:
			adv7170_write(client, 0x01, 0x20);
			adv7170_write(client, 0x08, TR1CAPT);	/* TR1 */
			adv7170_write(client, 0x02, 0x0e);	// Enable genlock
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			//udelay(10);
			break;

		case 1:
			adv7170_write(client, 0x01, 0x00);
			adv7170_write(client, 0x08, TR1PLAY);	/* TR1 */
			adv7170_write(client, 0x02, 0x08);
			adv7170_write(client, 0x07, TR0MODE | TR0RST);
			adv7170_write(client, 0x07, TR0MODE);
			//udelay(10);
			break;

		default:
			dprintk(1, KERN_ERR "%s: illegal input: %d\n",
				I2C_NAME(client), iarg);
			return -EINVAL;

		}
		dprintk(1, KERN_DEBUG "%s: switched to %s\n", I2C_NAME(client),
			inputs[iarg]);
		encoder->input = iarg;
	}
		break;

	case ENCODER_SET_OUTPUT:
	{
		int *iarg = arg;

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
static unsigned short normal_i2c[] =
    { I2C_ADV7170 >> 1, (I2C_ADV7170 >> 1) + 1,
	I2C_ADV7171 >> 1, (I2C_ADV7171 >> 1) + 1,
	I2C_CLIENT_END
};

static unsigned short ignore = I2C_CLIENT_END;

static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver i2c_driver_adv7170;

static int
adv7170_detect_client (struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	int i;
	struct i2c_client *client;
	struct adv7170 *encoder;
	char *dname;

	dprintk(1,
		KERN_INFO
		"adv7170.c: detecting adv7170 client on address 0x%x\n",
		address << 1);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_adv7170;
	if ((client->addr == I2C_ADV7170 >> 1) ||
	    (client->addr == (I2C_ADV7170 >> 1) + 1)) {
		dname = adv7170_name;
	} else if ((client->addr == I2C_ADV7171 >> 1) ||
		   (client->addr == (I2C_ADV7171 >> 1) + 1)) {
		dname = adv7171_name;
	} else {
		/* We should never get here!!! */
		kfree(client);
		return 0;
	}
	strlcpy(I2C_NAME(client), dname, sizeof(I2C_NAME(client)));

	encoder = kzalloc(sizeof(struct adv7170), GFP_KERNEL);
	if (encoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	encoder->norm = VIDEO_MODE_NTSC;
	encoder->input = 0;
	encoder->enable = 1;
	i2c_set_clientdata(client, encoder);

	i = i2c_attach_client(client);
	if (i) {
		kfree(client);
		kfree(encoder);
		return i;
	}

	i = adv7170_write_block(client, init_NTSC, sizeof(init_NTSC));
	if (i >= 0) {
		i = adv7170_write(client, 0x07, TR0MODE | TR0RST);
		i = adv7170_write(client, 0x07, TR0MODE);
		i = adv7170_read(client, 0x12);
		dprintk(1, KERN_INFO "%s_attach: rev. %d at 0x%02x\n",
			I2C_NAME(client), i & 1, client->addr << 1);
	}
	if (i < 0) {
		dprintk(1, KERN_ERR "%s_attach: init error 0x%x\n",
		       I2C_NAME(client), i);
	}

	return 0;
}

static int
adv7170_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"adv7170.c: starting probe for adapter %s (0x%x)\n",
		I2C_NAME(adapter), adapter->id);
	return i2c_probe(adapter, &addr_data, &adv7170_detect_client);
}

static int
adv7170_detach_client (struct i2c_client *client)
{
	struct adv7170 *encoder = i2c_get_clientdata(client);
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

static struct i2c_driver i2c_driver_adv7170 = {
	.driver = {
		.name = "adv7170",	/* name */
	},

	.id = I2C_DRIVERID_ADV7170,

	.attach_adapter = adv7170_attach_adapter,
	.detach_client = adv7170_detach_client,
	.command = adv7170_command,
};

static int __init
adv7170_init (void)
{
	return i2c_add_driver(&i2c_driver_adv7170);
}

static void __exit
adv7170_exit (void)
{
	i2c_del_driver(&i2c_driver_adv7170);
}

module_init(adv7170_init);
module_exit(adv7170_exit);
