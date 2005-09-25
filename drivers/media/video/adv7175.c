/* 
 *  adv7175 - adv7175a video encoder driver version 0.0.3
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (9/9/2002)
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
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <linux/videodev.h>
#include <asm/uaccess.h>

MODULE_DESCRIPTION("Analog Devices ADV7175 video encoder driver");
MODULE_AUTHOR("Dave Perks");
MODULE_LICENSE("GPL");

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

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

struct adv7175 {
	unsigned char reg[128];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

#define   I2C_ADV7175        0xd4
#define   I2C_ADV7176        0x54

static char adv7175_name[] = "adv7175";
static char adv7176_name[] = "adv7176";

static char *inputs[] = { "pass_through", "play_back", "color_bar" };
static char *norms[] = { "PAL", "NTSC", "SECAM->PAL (may not work!)" };

/* ----------------------------------------------------------------------- */

static inline int
adv7175_write (struct i2c_client *client,
	       u8                 reg,
	       u8                 value)
{
	struct adv7175 *encoder = i2c_get_clientdata(client);

	encoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int
adv7175_read (struct i2c_client *client,
	      u8                 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int
adv7175_write_block (struct i2c_client *client,
		     const u8          *data,
		     unsigned int       len)
{
	int ret = -1;
	u8 reg;

	/* the adv7175 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		struct adv7175 *encoder = i2c_get_clientdata(client);
		struct i2c_msg msg;
		u8 block_data[32];

		msg.addr = client->addr;
		msg.flags = 0;
		while (len >= 2) {
			msg.buf = (char *) block_data;
			msg.len = 0;
			block_data[msg.len++] = reg = data[0];
			do {
				block_data[msg.len++] =
				    encoder->reg[reg++] = data[1];
				len -= 2;
				data += 2;
			} while (len >= 2 && data[0] == reg &&
				 msg.len < 32);
			if ((ret = i2c_transfer(client->adapter,
						&msg, 1)) < 0)
				break;
		}
	} else {
		/* do some slow I2C emulation kind of thing */
		while (len >= 2) {
			reg = *data++;
			if ((ret = adv7175_write(client, reg,
						 *data++)) < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static void
set_subcarrier_freq (struct i2c_client *client,
		     int                pass_through)
{
	/* for some reason pass_through NTSC needs
	 * a different sub-carrier freq to remain stable. */
	if(pass_through)
		adv7175_write(client, 0x02, 0x00);
	else
		adv7175_write(client, 0x02, 0x55);

	adv7175_write(client, 0x03, 0x55);
	adv7175_write(client, 0x04, 0x55);
	adv7175_write(client, 0x05, 0x25);
}

#ifdef ENCODER_DUMP
static void
dump (struct i2c_client *client)
{
	struct adv7175 *encoder = i2c_get_clientdata(client);
	int i, j;

	printk(KERN_INFO "%s: registry dump\n", I2C_NAME(client));
	for (i = 0; i < 182 / 8; i++) {
		printk("%s: 0x%02x -", I2C_NAME(client), i * 8);
		for (j = 0; j < 8; j++) {
			printk(" 0x%02x", encoder->reg[i * 8 + j]);
		}
		printk("\n");
	}
}
#endif

/* ----------------------------------------------------------------------- */
// Output filter:  S-Video  Composite

#define MR050       0x11	//0x09
#define MR060       0x14	//0x0c

//---------------------------------------------------------------------------

#define TR0MODE     0x46
#define TR0RST	    0x80

#define TR1CAPT	    0x80
#define TR1PLAY	    0x00

static const unsigned char init_common[] = {

	0x00, MR050,		/* MR0, PAL enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x0c,		/* subc. freq. */
	0x03, 0x8c,		/* subc. freq. */
	0x04, 0x79,		/* subc. freq. */
	0x05, 0x26,		/* subc. freq. */
	0x06, 0x40,		/* subc. phase */

	0x07, TR0MODE,		/* TR0, 16bit */
	0x08, 0x21,		/*  */
	0x09, 0x00,		/*  */
	0x0a, 0x00,		/*  */
	0x0b, 0x00,		/*  */
	0x0c, TR1CAPT,		/* TR1 */
	0x0d, 0x4f,		/* MR2 */
	0x0e, 0x00,		/*  */
	0x0f, 0x00,		/*  */
	0x10, 0x00,		/*  */
	0x11, 0x00,		/*  */
};

static const unsigned char init_pal[] = {
	0x00, MR050,		/* MR0, PAL enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x0c,		/* subc. freq. */
	0x03, 0x8c,		/* subc. freq. */
	0x04, 0x79,		/* subc. freq. */
	0x05, 0x26,		/* subc. freq. */
	0x06, 0x40,		/* subc. phase */
};

static const unsigned char init_ntsc[] = {
	0x00, MR060,		/* MR0, NTSC enabled */
	0x01, 0x00,		/* MR1 */
	0x02, 0x55,		/* subc. freq. */
	0x03, 0x55,		/* subc. freq. */
	0x04, 0x55,		/* subc. freq. */
	0x05, 0x25,		/* subc. freq. */
	0x06, 0x1a,		/* subc. phase */
};

static int
adv7175_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct adv7175 *encoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
		/* This is just for testing!!! */
		adv7175_write_block(client, init_common,
				    sizeof(init_common));
		adv7175_write(client, 0x07, TR0MODE | TR0RST);
		adv7175_write(client, 0x07, TR0MODE);
	        break;

	case ENCODER_GET_CAPABILITIES:
	{
		struct video_encoder_capability *cap = arg;

		cap->flags = VIDEO_ENCODER_PAL |
			     VIDEO_ENCODER_NTSC |
			     VIDEO_ENCODER_SECAM; /* well, hacky */
		cap->inputs = 2;
		cap->outputs = 1;
	}
		break;

	case ENCODER_SET_NORM:
	{
		int iarg = *(int *) arg;

		switch (iarg) {

		case VIDEO_MODE_NTSC:
			adv7175_write_block(client, init_ntsc,
					    sizeof(init_ntsc));
			if (encoder->input == 0)
				adv7175_write(client, 0x0d, 0x4f);	// Enable genlock
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			break;

		case VIDEO_MODE_PAL:
			adv7175_write_block(client, init_pal,
					    sizeof(init_pal));
			if (encoder->input == 0)
				adv7175_write(client, 0x0d, 0x4f);	// Enable genlock
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			break;

		case VIDEO_MODE_SECAM:	// WARNING! ADV7176 does not support SECAM.
			/* This is an attempt to convert
			 * SECAM->PAL (typically it does not work
			 * due to genlock: when decoder is in SECAM
			 * and encoder in in PAL the subcarrier can
			 * not be syncronized with horizontal
			 * quency) */
			adv7175_write_block(client, init_pal,
					    sizeof(init_pal));
			if (encoder->input == 0)
				adv7175_write(client, 0x0d, 0x49);	// Disable genlock
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			break;
		default:
			dprintk(1, KERN_ERR "%s: illegal norm: %d\n",
				I2C_NAME(client), iarg);
			return -EINVAL;

		}
		dprintk(1, KERN_INFO "%s: switched to %s\n", I2C_NAME(client),
			norms[iarg]);
		encoder->norm = iarg;
	}
		break;

	case ENCODER_SET_INPUT:
	{
		int iarg = *(int *) arg;

		/* RJ: *iarg = 0: input is from SAA7110
		 *iarg = 1: input is from ZR36060
		 *iarg = 2: color bar */

		switch (iarg) {

		case 0:
			adv7175_write(client, 0x01, 0x00);

			if (encoder->norm == VIDEO_MODE_NTSC)
				set_subcarrier_freq(client, 1);

			adv7175_write(client, 0x0c, TR1CAPT);	/* TR1 */
			if (encoder->norm == VIDEO_MODE_SECAM)
				adv7175_write(client, 0x0d, 0x49);	// Disable genlock
			else
				adv7175_write(client, 0x0d, 0x4f);	// Enable genlock
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			//udelay(10);
			break;

		case 1:
			adv7175_write(client, 0x01, 0x00);

			if (encoder->norm == VIDEO_MODE_NTSC)
				set_subcarrier_freq(client, 0);

			adv7175_write(client, 0x0c, TR1PLAY);	/* TR1 */
			adv7175_write(client, 0x0d, 0x49);
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			//udelay(10);
			break;

		case 2:
			adv7175_write(client, 0x01, 0x80);

			if (encoder->norm == VIDEO_MODE_NTSC)
				set_subcarrier_freq(client, 0);

			adv7175_write(client, 0x0d, 0x49);
			adv7175_write(client, 0x07, TR0MODE | TR0RST);
			adv7175_write(client, 0x07, TR0MODE);
			//udelay(10);
			break;

		default:
			dprintk(1, KERN_ERR "%s: illegal input: %d\n",
				I2C_NAME(client), iarg);
			return -EINVAL;

		}
		dprintk(1, KERN_INFO "%s: switched to %s\n", I2C_NAME(client),
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

#ifdef ENCODER_DUMP
	case ENCODER_DUMP:
	{
		dump(client);
	}
		break;
#endif

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
    { I2C_ADV7175 >> 1, (I2C_ADV7175 >> 1) + 1,
	I2C_ADV7176 >> 1, (I2C_ADV7176 >> 1) + 1,
	I2C_CLIENT_END
};

static unsigned short ignore = I2C_CLIENT_END;
                                                                                
static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver i2c_driver_adv7175;

static int
adv7175_detect_client (struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	int i;
	struct i2c_client *client;
	struct adv7175 *encoder;
	char *dname;

	dprintk(1,
		KERN_INFO
		"adv7175.c: detecting adv7175 client on address 0x%x\n",
		address << 1);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	memset(client, 0, sizeof(struct i2c_client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_adv7175;
	client->flags = I2C_CLIENT_ALLOW_USE;
	if ((client->addr == I2C_ADV7175 >> 1) ||
	    (client->addr == (I2C_ADV7175 >> 1) + 1)) {
		dname = adv7175_name;
	} else if ((client->addr == I2C_ADV7176 >> 1) ||
		   (client->addr == (I2C_ADV7176 >> 1) + 1)) {
		dname = adv7176_name;
	} else {
		/* We should never get here!!! */
		kfree(client);
		return 0;
	}
	strlcpy(I2C_NAME(client), dname, sizeof(I2C_NAME(client)));

	encoder = kmalloc(sizeof(struct adv7175), GFP_KERNEL);
	if (encoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	memset(encoder, 0, sizeof(struct adv7175));
	encoder->norm = VIDEO_MODE_PAL;
	encoder->input = 0;
	encoder->enable = 1;
	i2c_set_clientdata(client, encoder);

	i = i2c_attach_client(client);
	if (i) {
		kfree(client);
		kfree(encoder);
		return i;
	}

	i = adv7175_write_block(client, init_common, sizeof(init_common));
	if (i >= 0) {
		i = adv7175_write(client, 0x07, TR0MODE | TR0RST);
		i = adv7175_write(client, 0x07, TR0MODE);
		i = adv7175_read(client, 0x12);
		dprintk(1, KERN_INFO "%s_attach: rev. %d at 0x%x\n",
			I2C_NAME(client), i & 1, client->addr << 1);
	}
	if (i < 0) {
		dprintk(1, KERN_ERR "%s_attach: init error 0x%x\n",
			I2C_NAME(client), i);
	}

	return 0;
}

static int
adv7175_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"adv7175.c: starting probe for adapter %s (0x%x)\n",
		I2C_NAME(adapter), adapter->id);
	return i2c_probe(adapter, &addr_data, &adv7175_detect_client);
}

static int
adv7175_detach_client (struct i2c_client *client)
{
	struct adv7175 *encoder = i2c_get_clientdata(client);
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

static struct i2c_driver i2c_driver_adv7175 = {
	.owner = THIS_MODULE,
	.name = "adv7175",	/* name */

	.id = I2C_DRIVERID_ADV7175,
	.flags = I2C_DF_NOTIFY,

	.attach_adapter = adv7175_attach_adapter,
	.detach_client = adv7175_detach_client,
	.command = adv7175_command,
};

static int __init
adv7175_init (void)
{
	return i2c_add_driver(&i2c_driver_adv7175);
}

static void __exit
adv7175_exit (void)
{
	i2c_del_driver(&i2c_driver_adv7175);
}

module_init(adv7175_init);
module_exit(adv7175_exit);
