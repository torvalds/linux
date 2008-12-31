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
#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/videodev.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("Philips SAA7111 video decoder driver");
MODULE_AUTHOR("Dave Perks");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* ----------------------------------------------------------------------- */

#define SAA7111_NR_REG		0x18

struct saa7111 {
	unsigned char reg[SAA7111_NR_REG];

	int norm;
	int input;
	int enable;
};

/* ----------------------------------------------------------------------- */

static inline int saa7111_write(struct i2c_client *client, u8 reg, u8 value)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline void saa7111_write_if_changed(struct i2c_client *client, u8 reg, u8 value)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);

	if (decoder->reg[reg] != value) {
		decoder->reg[reg] = value;
		i2c_smbus_write_byte_data(client, reg, value);
	}
}

static int saa7111_write_block(struct i2c_client *client, const u8 *data, unsigned int len)
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
			} while (len >= 2 && data[0] == reg && block_len < 32);
			ret = i2c_master_send(client, block_data, block_len);
			if (ret < 0)
				break;
		}
	} else {
		/* do some slow I2C emulation kind of thing */
		while (len >= 2) {
			reg = *data++;
			ret = saa7111_write(client, reg, *data++);
			if (ret < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static int saa7111_init_decoder(struct i2c_client *client,
		struct video_decoder_init *init)
{
	return saa7111_write_block(client, init->data, init->len);
}

static inline int saa7111_read(struct i2c_client *client, u8 reg)
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

static int saa7111_command(struct i2c_client *client, unsigned cmd, void *arg)
{
	struct saa7111 *decoder = i2c_get_clientdata(client);

	switch (cmd) {
	case 0:
		break;
	case DECODER_INIT:
	{
		struct video_decoder_init *init = arg;
		struct video_decoder_init vdi;

		if (NULL != init)
			return saa7111_init_decoder(client, init);
		vdi.data = saa7111_i2c_init;
		vdi.len = sizeof(saa7111_i2c_init);
		return saa7111_init_decoder(client, &vdi);
	}

	case DECODER_DUMP:
	{
		int i;

		for (i = 0; i < SAA7111_NR_REG; i += 16) {
			int j;

			v4l_info(client, "%03x", i);
			for (j = 0; j < 16 && i + j < SAA7111_NR_REG; ++j) {
				printk(KERN_CONT " %02x",
				       saa7111_read(client, i + j));
			}
			printk(KERN_CONT "\n");
		}
		break;
	}

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
		break;
	}

	case DECODER_GET_STATUS:
	{
		int *iarg = arg;
		int status;
		int res;

		status = saa7111_read(client, 0x1f);
		v4l_dbg(1, debug, client, "status: 0x%02x\n", status);
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
		break;
	}

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
		break;
	}

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
		break;
	}

	case DECODER_SET_OUTPUT:
	{
		int *iarg = arg;

		/* not much choice of outputs */
		if (*iarg != 0) {
			return -EINVAL;
		}
		break;
	}

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
		break;
	}

	case DECODER_SET_PICTURE:
	{
		struct video_picture *pic = arg;

		/* We want 0 to 255 we get 0-65535 */
		saa7111_write_if_changed(client, 0x0a, pic->brightness >> 8);
		/* We want 0 to 127 we get 0-65535 */
		saa7111_write(client, 0x0b, pic->contrast >> 9);
		/* We want 0 to 127 we get 0-65535 */
		saa7111_write(client, 0x0c, pic->colour >> 9);
		/* We want -128 to 127 we get 0-65535 */
		saa7111_write(client, 0x0d, (pic->hue - 32768) >> 8);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static unsigned short normal_i2c[] = { 0x48 >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int saa7111_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i;
	struct saa7111 *decoder;
	struct video_decoder_init vdi;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	decoder = kzalloc(sizeof(struct saa7111), GFP_KERNEL);
	if (decoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	decoder->norm = VIDEO_MODE_NTSC;
	decoder->input = 0;
	decoder->enable = 1;
	i2c_set_clientdata(client, decoder);

	vdi.data = saa7111_i2c_init;
	vdi.len = sizeof(saa7111_i2c_init);
	i = saa7111_init_decoder(client, &vdi);
	if (i < 0) {
		v4l_dbg(1, debug, client, "init status %d\n", i);
	} else {
		v4l_dbg(1, debug, client, "revision %x\n",
			saa7111_read(client, 0x00) >> 4);
	}
	return 0;
}

static int saa7111_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa7111_id[] = {
	{ "saa7111_old", 0 },	/* "saa7111" maps to the saa7115 driver */
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa7111_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa7111",
	.driverid = I2C_DRIVERID_SAA7111A,
	.command = saa7111_command,
	.probe = saa7111_probe,
	.remove = saa7111_remove,
	.id_table = saa7111_id,
};
