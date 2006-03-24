/* 
 * saa7111 - Philips SAA7111A video decoder driver version 0.0.3
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * Slight changes for video timing and attachment output by
 * Wolfgang Scherr <scherr@net4you.net>
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (1/1/2003)
 *
 * Changes by Michael Hunold <michael@mihu.de>
 *    - implemented DECODER_SET_GPIO, DECODER_INIT, DECODER_SET_VBI_BYPASS
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

MODULE_DESCRIPTION("Philips SAA7111 video decoder driver");
MODULE_AUTHOR("Dave Perks");
MODULE_LICENSE("GPL");

#include <linux/i2c.h>

#define I2C_NAME(s) (s)->name

#include <linux/video_decoder.h>

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

/* ----------------------------------------------------------------------- */

#define SAA7111_NR_REG		0x18

struct saa7111 {
	unsigned char reg[SAA7111_NR_REG];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

#define   I2C_SAA7111        0x48

/* ----------------------------------------------------------------------- */

static inline int
saa7111_write (struct i2c_client *client,
	       u8                 reg,
	       u8                 value)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int
saa7111_write_block (struct i2c_client *client,
		     const u8          *data,
		     unsigned int       len)
{
	int ret = -1;
	u8 reg;

	/* the saa7111 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		struct saa7111 *decoder = i2c_get_clientdata(client);
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] =
				    decoder->reg[reg++] = data[1];
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
			if ((ret = saa7111_write(client, reg,
						 *data++)) < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static int
saa7111_init_decoder (struct i2c_client *client,
	      struct video_decoder_init *init)
{
	return saa7111_write_block(client, init->data, init->len);
}

static inline int
saa7111_read (struct i2c_client *client,
	      u8                 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

static const unsigned char saa7111_i2c_init[] = {
	0x00, 0x00,		/* 00 - ID byte */
	0x01, 0x00,		/* 01 - reserved */

	/*front end */
	0x02, 0xd0,		/* 02 - FUSE=3, GUDL=2, MODE=0 */
	0x03, 0x23,		/* 03 - HLNRS=0, VBSL=1, WPOFF=0,
				 * HOLDG=0, GAFIX=0, GAI1=256, GAI2=256 */
	0x04, 0x00,		/* 04 - GAI1=256 */
	0x05, 0x00,		/* 05 - GAI2=256 */

	/* decoder */
	0x06, 0xf3,		/* 06 - HSB at  13(50Hz) /  17(60Hz)
				 * pixels after end of last line */
	/*0x07, 0x13,     * 07 - HSS at 113(50Hz) / 117(60Hz) pixels
				 * after end of last line */
	0x07, 0xe8,		/* 07 - HSS seems to be needed to
				 * work with NTSC, too */
	0x08, 0xc8,		/* 08 - AUFD=1, FSEL=1, EXFIL=0,
				 * VTRC=1, HPLL=0, VNOI=0 */
	0x09, 0x01,		/* 09 - BYPS=0, PREF=0, BPSS=0,
				 * VBLB=0, UPTCV=0, APER=1 */
	0x0a, 0x80,		/* 0a - BRIG=128 */
	0x0b, 0x47,		/* 0b - CONT=1.109 */
	0x0c, 0x40,		/* 0c - SATN=1.0 */
	0x0d, 0x00,		/* 0d - HUE=0 */
	0x0e, 0x01,		/* 0e - CDTO=0, CSTD=0, DCCF=0,
				 * FCTC=0, CHBW=1 */
	0x0f, 0x00,		/* 0f - reserved */
	0x10, 0x48,		/* 10 - OFTS=1, HDEL=0, VRLN=1, YDEL=0 */
	0x11, 0x1c,		/* 11 - GPSW=0, CM99=0, FECO=0, COMPO=1,
				 * OEYC=1, OEHV=1, VIPB=0, COLO=0 */
	0x12, 0x00,		/* 12 - output control 2 */
	0x13, 0x00,		/* 13 - output control 3 */
	0x14, 0x00,		/* 14 - reserved */
	0x15, 0x00,		/* 15 - VBI */
	0x16, 0x00,		/* 16 - VBI */
	0x17, 0x00,		/* 17 - VBI */
};

static int
saa7111_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
		break;
	case DECODER_INIT:
	{
		struct video_decoder_init *init = arg;
		if (NULL != init)
			return saa7111_init_decoder(client, init);
		else {
			struct video_decoder_init vdi;
			vdi.data = saa7111_i2c_init;
			vdi.len = sizeof(saa7111_i2c_init);
			return saa7111_init_decoder(client, &vdi);
		}
	}

	case DECODER_DUMP:
	{
		int i;

		for (i = 0; i < SAA7111_NR_REG; i += 16) {
			int j;

			printk(KERN_DEBUG "%s: %03x", I2C_NAME(client), i);
			for (j = 0; j < 16 && i + j < SAA7111_NR_REG; ++j) {
				printk(" %02x",
				       saa7111_read(client, i + j));
			}
			printk("\n");
		}
	}
		break;

	case DECODER_GET_CAPABILITIES:
	{
		struct video_decoder_capability *cap = arg;

		cap->flags = VIDEO_DECODER_PAL |
			     VIDEO_DECODER_NTSC |
			     VIDEO_DECODER_SECAM |
			     VIDEO_DECODER_AUTO |
			     VIDEO_DECODER_CCIR;
		cap->inputs = 8;
		cap->outputs = 1;
	}
		break;

	case DECODER_GET_STATUS:
	{
		int *iarg = arg;
		int status;
		int res;

		status = saa7111_read(client, 0x1f);
		dprintk(1, KERN_DEBUG "%s status: 0x%02x\n", I2C_NAME(client),
			status);
		res = 0;
		if ((status & (1 << 6)) == 0) {
			res |= DECODER_STATUS_GOOD;
		}
		switch (decoder->norm) {
		case VIDEO_MODE_NTSC:
			res |= DECODER_STATUS_NTSC;
			break;
		case VIDEO_MODE_PAL:
			res |= DECODER_STATUS_PAL;
			break;
		case VIDEO_MODE_SECAM:
			res |= DECODER_STATUS_SECAM;
			break;
		default:
		case VIDEO_MODE_AUTO:
			if ((status & (1 << 5)) != 0) {
				res |= DECODER_STATUS_NTSC;
			} else {
				res |= DECODER_STATUS_PAL;
			}
			break;
		}
		if ((status & (1 << 0)) != 0) {
			res |= DECODER_STATUS_COLOR;
		}
		*iarg = res;
	}
		break;

	case DECODER_SET_GPIO:
	{
		int *iarg = arg;
		if (0 != *iarg) {
			saa7111_write(client, 0x11,
				(decoder->reg[0x11] | 0x80));
		} else {
			saa7111_write(client, 0x11,
				(decoder->reg[0x11] & 0x7f));
		}
		break;
	}

	case DECODER_SET_VBI_BYPASS:
	{
		int *iarg = arg;
		if (0 != *iarg) {
			saa7111_write(client, 0x13,
				(decoder->reg[0x13] & 0xf0) | 0x0a);
		} else {
			saa7111_write(client, 0x13,
				(decoder->reg[0x13] & 0xf0));
		}
		break;
	}

	case DECODER_SET_NORM:
	{
		int *iarg = arg;

		switch (*iarg) {

		case VIDEO_MODE_NTSC:
			saa7111_write(client, 0x08,
				      (decoder->reg[0x08] & 0x3f) | 0x40);
			saa7111_write(client, 0x0e,
				      (decoder->reg[0x0e] & 0x8f));
			break;

		case VIDEO_MODE_PAL:
			saa7111_write(client, 0x08,
				      (decoder->reg[0x08] & 0x3f) | 0x00);
			saa7111_write(client, 0x0e,
				      (decoder->reg[0x0e] & 0x8f));
			break;

		case VIDEO_MODE_SECAM:
			saa7111_write(client, 0x08,
				      (decoder->reg[0x08] & 0x3f) | 0x00);
			saa7111_write(client, 0x0e,
				      (decoder->reg[0x0e] & 0x8f) | 0x50);
			break;

		case VIDEO_MODE_AUTO:
			saa7111_write(client, 0x08,
				      (decoder->reg[0x08] & 0x3f) | 0x80);
			saa7111_write(client, 0x0e,
				      (decoder->reg[0x0e] & 0x8f));
			break;

		default:
			return -EINVAL;

		}
		decoder->norm = *iarg;
	}
		break;

	case DECODER_SET_INPUT:
	{
		int *iarg = arg;

		if (*iarg < 0 || *iarg > 7) {
			return -EINVAL;
		}

		if (decoder->input != *iarg) {
			decoder->input = *iarg;
			/* select mode */
			saa7111_write(client, 0x02,
				      (decoder->
				       reg[0x02] & 0xf8) | decoder->input);
			/* bypass chrominance trap for modes 4..7 */
			saa7111_write(client, 0x09,
				      (decoder->
				       reg[0x09] & 0x7f) | ((decoder->
							     input >
							     3) ? 0x80 :
							    0));
		}
	}
		break;

	case DECODER_SET_OUTPUT:
	{
		int *iarg = arg;

		/* not much choice of outputs */
		if (*iarg != 0) {
			return -EINVAL;
		}
	}
		break;

	case DECODER_ENABLE_OUTPUT:
	{
		int *iarg = arg;
		int enable = (*iarg != 0);

		if (decoder->enable != enable) {
			decoder->enable = enable;

			/* RJ: If output should be disabled (for
			 * playing videos), we also need a open PLL.
			 * The input is set to 0 (where no input
			 * source is connected), although this
			 * is not necessary.
			 *
			 * If output should be enabled, we have to
			 * reverse the above.
			 */

			if (decoder->enable) {
				saa7111_write(client, 0x02,
					      (decoder->
					       reg[0x02] & 0xf8) |
					      decoder->input);
				saa7111_write(client, 0x08,
					      (decoder->reg[0x08] & 0xfb));
				saa7111_write(client, 0x11,
					      (decoder->
					       reg[0x11] & 0xf3) | 0x0c);
			} else {
				saa7111_write(client, 0x02,
					      (decoder->reg[0x02] & 0xf8));
				saa7111_write(client, 0x08,
					      (decoder->
					       reg[0x08] & 0xfb) | 0x04);
				saa7111_write(client, 0x11,
					      (decoder->reg[0x11] & 0xf3));
			}
		}
	}
		break;

	case DECODER_SET_PICTURE:
	{
		struct video_picture *pic = arg;

		if (decoder->bright != pic->brightness) {
			/* We want 0 to 255 we get 0-65535 */
			decoder->bright = pic->brightness;
			saa7111_write(client, 0x0a, decoder->bright >> 8);
		}
		if (decoder->contrast != pic->contrast) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->contrast = pic->contrast;
			saa7111_write(client, 0x0b,
				      decoder->contrast >> 9);
		}
		if (decoder->sat != pic->colour) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->sat = pic->colour;
			saa7111_write(client, 0x0c, decoder->sat >> 9);
		}
		if (decoder->hue != pic->hue) {
			/* We want -128 to 127 we get 0-65535 */
			decoder->hue = pic->hue;
			saa7111_write(client, 0x0d,
				      (decoder->hue - 32768) >> 8);
		}
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
static unsigned short normal_i2c[] = { I2C_SAA7111 >> 1, I2C_CLIENT_END };

static unsigned short ignore = I2C_CLIENT_END;
                                                                                
static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver i2c_driver_saa7111;

static int
saa7111_detect_client (struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	int i;
	struct i2c_client *client;
	struct saa7111 *decoder;
	struct video_decoder_init vdi;

	dprintk(1,
		KERN_INFO
		"saa7111.c: detecting saa7111 client on address 0x%x\n",
		address << 1);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_saa7111;
	strlcpy(I2C_NAME(client), "saa7111", sizeof(I2C_NAME(client)));

	decoder = kzalloc(sizeof(struct saa7111), GFP_KERNEL);
	if (decoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	decoder->norm = VIDEO_MODE_NTSC;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;
	i2c_set_clientdata(client, decoder);

	i = i2c_attach_client(client);
	if (i) {
		kfree(client);
		kfree(decoder);
		return i;
	}

	vdi.data = saa7111_i2c_init;
	vdi.len = sizeof(saa7111_i2c_init);
	i = saa7111_init_decoder(client, &vdi);
	if (i < 0) {
		dprintk(1, KERN_ERR "%s_attach error: init status %d\n",
			I2C_NAME(client), i);
	} else {
		dprintk(1,
			KERN_INFO
			"%s_attach: chip version %x at address 0x%x\n",
			I2C_NAME(client), saa7111_read(client, 0x00) >> 4,
			client->addr << 1);
	}

	return 0;
}

static int
saa7111_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"saa7111.c: starting probe for adapter %s (0x%x)\n",
		I2C_NAME(adapter), adapter->id);
	return i2c_probe(adapter, &addr_data, &saa7111_detect_client);
}

static int
saa7111_detach_client (struct i2c_client *client)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(decoder);
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_saa7111 = {
	.driver = {
		.name = "saa7111",
	},

	.id = I2C_DRIVERID_SAA7111A,

	.attach_adapter = saa7111_attach_adapter,
	.detach_client = saa7111_detach_client,
	.command = saa7111_command,
};

static int __init
saa7111_init (void)
{
	return i2c_add_driver(&i2c_driver_saa7111);
}

static void __exit
saa7111_exit (void)
{
	i2c_del_driver(&i2c_driver_saa7111);
}

module_init(saa7111_init);
module_exit(saa7111_exit);
