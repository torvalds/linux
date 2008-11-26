/*
 * saa7110 - Philips SAA7110(A) video decoder driver
 *
 * Copyright (C) 1998 Pauline Middelink <middelin@polyware.nl>
 *
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
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>

MODULE_DESCRIPTION("Philips SAA7110 video decoder driver");
MODULE_AUTHOR("Pauline Middelink");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define SAA7110_MAX_INPUT	9	/* 6 CVBS, 3 SVHS */
#define SAA7110_MAX_OUTPUT	1	/* 1 YUV */

#define SAA7110_NR_REG		0x35

struct saa7110 {
	u8 reg[SAA7110_NR_REG];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;

	wait_queue_head_t wq;
};

/* ----------------------------------------------------------------------- */
/* I2C support functions						   */
/* ----------------------------------------------------------------------- */

static int saa7110_write(struct i2c_client *client, u8 reg, u8 value)
{
	struct saa7110 *decoder = i2c_get_clientdata(client);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int saa7110_write_block(struct i2c_client *client, const u8 *data, unsigned int len)
{
	int ret = -1;
	u8 reg = *data;		/* first register to write to */

	/* Sanity check */
	if (reg + (len - 1) > SAA7110_NR_REG)
		return ret;

	/* the saa7110 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		struct saa7110 *decoder = i2c_get_clientdata(client);

		ret = i2c_master_send(client, data, len);

		/* Cache the written data */
		memcpy(decoder->reg + reg, data + 1, len - 1);
	} else {
		for (++data, --len; len; len--) {
			ret = saa7110_write(client, reg++, *data++);
			if (ret < 0)
				break;
		}
	}

	return ret;
}

static inline int saa7110_read(struct i2c_client *client)
{
	return i2c_smbus_read_byte(client);
}

/* ----------------------------------------------------------------------- */
/* SAA7110 functions							   */
/* ----------------------------------------------------------------------- */

#define FRESP_06H_COMPST 0x03	//0x13
#define FRESP_06H_SVIDEO 0x83	//0xC0


static int saa7110_selmux(struct i2c_client *client, int chan)
{
	static const unsigned char modes[9][8] = {
		/* mode 0 */
		{FRESP_06H_COMPST, 0xD9, 0x17, 0x40, 0x03,
			      0x44, 0x75, 0x16},
		/* mode 1 */
		{FRESP_06H_COMPST, 0xD8, 0x17, 0x40, 0x03,
			      0x44, 0x75, 0x16},
		/* mode 2 */
		{FRESP_06H_COMPST, 0xBA, 0x07, 0x91, 0x03,
			      0x60, 0xB5, 0x05},
		/* mode 3 */
		{FRESP_06H_COMPST, 0xB8, 0x07, 0x91, 0x03,
			      0x60, 0xB5, 0x05},
		/* mode 4 */
		{FRESP_06H_COMPST, 0x7C, 0x07, 0xD2, 0x83,
			      0x60, 0xB5, 0x03},
		/* mode 5 */
		{FRESP_06H_COMPST, 0x78, 0x07, 0xD2, 0x83,
			      0x60, 0xB5, 0x03},
		/* mode 6 */
		{FRESP_06H_SVIDEO, 0x59, 0x17, 0x42, 0xA3,
			      0x44, 0x75, 0x12},
		/* mode 7 */
		{FRESP_06H_SVIDEO, 0x9A, 0x17, 0xB1, 0x13,
			      0x60, 0xB5, 0x14},
		/* mode 8 */
		{FRESP_06H_SVIDEO, 0x3C, 0x27, 0xC1, 0x23,
			      0x44, 0x75, 0x21}
	};
	struct saa7110 *decoder = i2c_get_clientdata(client);
	const unsigned char *ptr = modes[chan];

	saa7110_write(client, 0x06, ptr[0]);	/* Luminance control    */
	saa7110_write(client, 0x20, ptr[1]);	/* Analog Control #1    */
	saa7110_write(client, 0x21, ptr[2]);	/* Analog Control #2    */
	saa7110_write(client, 0x22, ptr[3]);	/* Mixer Control #1     */
	saa7110_write(client, 0x2C, ptr[4]);	/* Mixer Control #2     */
	saa7110_write(client, 0x30, ptr[5]);	/* ADCs gain control    */
	saa7110_write(client, 0x31, ptr[6]);	/* Mixer Control #3     */
	saa7110_write(client, 0x21, ptr[7]);	/* Analog Control #2    */
	decoder->input = chan;

	return 0;
}

static const unsigned char initseq[1 + SAA7110_NR_REG] = {
	0, 0x4C, 0x3C, 0x0D, 0xEF, 0xBD, 0xF2, 0x03, 0x00,
	/* 0x08 */ 0xF8, 0xF8, 0x60, 0x60, 0x00, 0x86, 0x18, 0x90,
	/* 0x10 */ 0x00, 0x59, 0x40, 0x46, 0x42, 0x1A, 0xFF, 0xDA,
	/* 0x18 */ 0xF2, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0xD9, 0x16, 0x40, 0x41, 0x80, 0x41, 0x80, 0x4F,
	/* 0x28 */ 0xFE, 0x01, 0xCF, 0x0F, 0x03, 0x01, 0x03, 0x0C,
	/* 0x30 */ 0x44, 0x71, 0x02, 0x8C, 0x02
};

static int determine_norm(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct saa7110 *decoder = i2c_get_clientdata(client);
	int status;

	/* mode changed, start automatic detection */
	saa7110_write_block(client, initseq, sizeof(initseq));
	saa7110_selmux(client, decoder->input);
	prepare_to_wait(&decoder->wq, &wait, TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(250));
	finish_wait(&decoder->wq, &wait);
	status = saa7110_read(client);
	if (status & 0x40) {
		v4l_dbg(1, debug, client, "status=0x%02x (no signal)\n", status);
		return decoder->norm;	// no change
	}
	if ((status & 3) == 0) {
		saa7110_write(client, 0x06, 0x83);
		if (status & 0x20) {
			v4l_dbg(1, debug, client, "status=0x%02x (NTSC/no color)\n", status);
			//saa7110_write(client,0x2E,0x81);
			return VIDEO_MODE_NTSC;
		}
		v4l_dbg(1, debug, client, "status=0x%02x (PAL/no color)\n", status);
		//saa7110_write(client,0x2E,0x9A);
		return VIDEO_MODE_PAL;
	}
	//saa7110_write(client,0x06,0x03);
	if (status & 0x20) {	/* 60Hz */
		v4l_dbg(1, debug, client, "status=0x%02x (NTSC)\n", status);
		saa7110_write(client, 0x0D, 0x86);
		saa7110_write(client, 0x0F, 0x50);
		saa7110_write(client, 0x11, 0x2C);
		//saa7110_write(client,0x2E,0x81);
		return VIDEO_MODE_NTSC;
	}

	/* 50Hz -> PAL/SECAM */
	saa7110_write(client, 0x0D, 0x86);
	saa7110_write(client, 0x0F, 0x10);
	saa7110_write(client, 0x11, 0x59);
	//saa7110_write(client,0x2E,0x9A);

	prepare_to_wait(&decoder->wq, &wait, TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(250));
	finish_wait(&decoder->wq, &wait);

	status = saa7110_read(client);
	if ((status & 0x03) == 0x01) {
		v4l_dbg(1, debug, client, "status=0x%02x (SECAM)\n", status);
		saa7110_write(client, 0x0D, 0x87);
		return VIDEO_MODE_SECAM;
	}
	v4l_dbg(1, debug, client, "status=0x%02x (PAL)\n", status);
	return VIDEO_MODE_PAL;
}

static int
saa7110_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct saa7110 *decoder = i2c_get_clientdata(client);
	int v;

	switch (cmd) {
	case 0:
		//saa7110_write_block(client, initseq, sizeof(initseq));
		break;

	case DECODER_GET_CAPABILITIES:
	{
		struct video_decoder_capability *dc = arg;

		dc->flags =
		    VIDEO_DECODER_PAL | VIDEO_DECODER_NTSC |
		    VIDEO_DECODER_SECAM | VIDEO_DECODER_AUTO;
		dc->inputs = SAA7110_MAX_INPUT;
		dc->outputs = SAA7110_MAX_OUTPUT;
		break;
	}

	case DECODER_GET_STATUS:
	{
		int status;
		int res = 0;

		status = saa7110_read(client);
		v4l_dbg(1, debug, client, "status=0x%02x norm=%d\n",
			       status, decoder->norm);
		if (!(status & 0x40))
			res |= DECODER_STATUS_GOOD;
		if (status & 0x03)
			res |= DECODER_STATUS_COLOR;

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
		}
		*(int *) arg = res;
		break;
	}

	case DECODER_SET_NORM:
		v = *(int *) arg;
		if (decoder->norm != v) {
			decoder->norm = v;
			//saa7110_write(client, 0x06, 0x03);
			switch (v) {
			case VIDEO_MODE_NTSC:
				saa7110_write(client, 0x0D, 0x86);
				saa7110_write(client, 0x0F, 0x50);
				saa7110_write(client, 0x11, 0x2C);
				//saa7110_write(client, 0x2E, 0x81);
				v4l_dbg(1, debug, client, "switched to NTSC\n");
				break;
			case VIDEO_MODE_PAL:
				saa7110_write(client, 0x0D, 0x86);
				saa7110_write(client, 0x0F, 0x10);
				saa7110_write(client, 0x11, 0x59);
				//saa7110_write(client, 0x2E, 0x9A);
				v4l_dbg(1, debug, client, "switched to PAL\n");
				break;
			case VIDEO_MODE_SECAM:
				saa7110_write(client, 0x0D, 0x87);
				saa7110_write(client, 0x0F, 0x10);
				saa7110_write(client, 0x11, 0x59);
				//saa7110_write(client, 0x2E, 0x9A);
				v4l_dbg(1, debug, client, "switched to SECAM\n");
				break;
			case VIDEO_MODE_AUTO:
				v4l_dbg(1, debug, client, "switched to AUTO\n");
				decoder->norm = determine_norm(client);
				*(int *) arg = decoder->norm;
				break;
			default:
				return -EPERM;
			}
		}
		break;

	case DECODER_SET_INPUT:
		v = *(int *) arg;
		if (v < 0 || v >= SAA7110_MAX_INPUT) {
			v4l_dbg(1, debug, client, "input=%d not available\n", v);
			return -EINVAL;
		}
		if (decoder->input != v) {
			saa7110_selmux(client, v);
			v4l_dbg(1, debug, client, "switched to input=%d\n", v);
		}
		break;

	case DECODER_SET_OUTPUT:
		v = *(int *) arg;
		/* not much choice of outputs */
		if (v != 0)
			return -EINVAL;
		break;

	case DECODER_ENABLE_OUTPUT:
		v = *(int *) arg;
		if (decoder->enable != v) {
			decoder->enable = v;
			saa7110_write(client, 0x0E, v ? 0x18 : 0x80);
			v4l_dbg(1, debug, client, "YUV %s\n", v ? "on" : "off");
		}
		break;

	case DECODER_SET_PICTURE:
	{
		struct video_picture *pic = arg;

		if (decoder->bright != pic->brightness) {
			/* We want 0 to 255 we get 0-65535 */
			decoder->bright = pic->brightness;
			saa7110_write(client, 0x19, decoder->bright >> 8);
		}
		if (decoder->contrast != pic->contrast) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->contrast = pic->contrast;
			saa7110_write(client, 0x13,
				      decoder->contrast >> 9);
		}
		if (decoder->sat != pic->colour) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->sat = pic->colour;
			saa7110_write(client, 0x12, decoder->sat >> 9);
		}
		if (decoder->hue != pic->hue) {
			/* We want -128 to 127 we get 0-65535 */
			decoder->hue = pic->hue;
			saa7110_write(client, 0x07,
				      (decoder->hue >> 8) - 128);
		}
		break;
	}

	case DECODER_DUMP:
		if (!debug)
			break;
		for (v = 0; v < SAA7110_NR_REG; v += 16) {
			int j;
			v4l_dbg(1, debug, client, "%02x:", v);
			for (j = 0; j < 16 && v + j < SAA7110_NR_REG; j++)
				printk(KERN_CONT " %02x", decoder->reg[v + j]);
			printk(KERN_CONT "\n");
		}
		break;

	default:
		v4l_dbg(1, debug, client, "unknown command %08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

static unsigned short normal_i2c[] = { 0x9c >> 1, 0x9e >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int saa7110_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct saa7110 *decoder;
	int rv;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	decoder = kzalloc(sizeof(struct saa7110), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;
	decoder->norm = VIDEO_MODE_PAL;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;
	init_waitqueue_head(&decoder->wq);
	i2c_set_clientdata(client, decoder);

	rv = saa7110_write_block(client, initseq, sizeof(initseq));
	if (rv < 0) {
		v4l_dbg(1, debug, client, "init status %d\n", rv);
	} else {
		int ver, status;
		saa7110_write(client, 0x21, 0x10);
		saa7110_write(client, 0x0e, 0x18);
		saa7110_write(client, 0x0D, 0x04);
		ver = saa7110_read(client);
		saa7110_write(client, 0x0D, 0x06);
		//mdelay(150);
		status = saa7110_read(client);
		v4l_dbg(1, debug, client, "version %x, status=0x%02x\n",
			       ver, status);
		saa7110_write(client, 0x0D, 0x86);
		saa7110_write(client, 0x0F, 0x10);
		saa7110_write(client, 0x11, 0x59);
		//saa7110_write(client, 0x2E, 0x9A);
	}

	//saa7110_selmux(client,0);
	//determine_norm(client);
	/* setup and implicit mode 0 select has been performed */

	return 0;
}

static int saa7110_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa7110_id[] = {
	{ "saa7110", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa7110_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa7110",
	.driverid = I2C_DRIVERID_SAA7110,
	.command = saa7110_command,
	.probe = saa7110_probe,
	.remove = saa7110_remove,
	.id_table = saa7110_id,
};
