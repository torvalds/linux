/*
 * saa7114 - Philips SAA7114H video decoder driver version 0.0.1
 *
 * Copyright (C) 2002 Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Based on saa7111 driver by Dave Perks
 *
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * Slight changes for video timing and attachment output by
 * Wolfgang Scherr <scherr@net4you.net>
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
#include <linux/video_decoder.h>

MODULE_DESCRIPTION("Philips SAA7114H video decoder driver");
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

struct saa7114 {
	unsigned char reg[0xf0 * 2];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
	int playback;
};

#define   I2C_SAA7114        0x42
#define   I2C_SAA7114A       0x40

#define   I2C_DELAY   10


//#define SAA_7114_NTSC_HSYNC_START       (-3)
//#define SAA_7114_NTSC_HSYNC_STOP        (-18)

#define SAA_7114_NTSC_HSYNC_START  (-17)
#define SAA_7114_NTSC_HSYNC_STOP   (-32)

//#define SAA_7114_NTSC_HOFFSET           (5)
#define SAA_7114_NTSC_HOFFSET		(6)
#define SAA_7114_NTSC_VOFFSET           (10)
#define SAA_7114_NTSC_WIDTH             (720)
#define SAA_7114_NTSC_HEIGHT            (250)

#define SAA_7114_SECAM_HSYNC_START      (-17)
#define SAA_7114_SECAM_HSYNC_STOP       (-32)

#define SAA_7114_SECAM_HOFFSET          (2)
#define SAA_7114_SECAM_VOFFSET          (10)
#define SAA_7114_SECAM_WIDTH            (720)
#define SAA_7114_SECAM_HEIGHT           (300)

#define SAA_7114_PAL_HSYNC_START        (-17)
#define SAA_7114_PAL_HSYNC_STOP         (-32)

#define SAA_7114_PAL_HOFFSET            (2)
#define SAA_7114_PAL_VOFFSET            (10)
#define SAA_7114_PAL_WIDTH              (720)
#define SAA_7114_PAL_HEIGHT             (300)



#define SAA_7114_VERTICAL_CHROMA_OFFSET         0	//0x50504040
#define SAA_7114_VERTICAL_LUMA_OFFSET           0

#define REG_ADDR(x) (((x) << 1) + 1)
#define LOBYTE(x) ((unsigned char)((x) & 0xff))
#define HIBYTE(x) ((unsigned char)(((x) >> 8) & 0xff))
#define LOWORD(x) ((unsigned short int)((x) & 0xffff))
#define HIWORD(x) ((unsigned short int)(((x) >> 16) & 0xffff))


/* ----------------------------------------------------------------------- */

static inline int
saa7114_write (struct i2c_client *client,
	       u8                 reg,
	       u8                 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int
saa7114_write_block (struct i2c_client *client,
		     const u8          *data,
		     unsigned int       len)
{
	int ret = -1;
	u8 reg;

	/* the saa7114 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		/* do raw I2C, not smbus compatible */
		u8 block_data[32];
		int block_len;

		while (len >= 2) {
			block_len = 0;
			block_data[block_len++] = reg = data[0];
			do {
				block_data[block_len++] = data[1];
				reg++;
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
			if ((ret = saa7114_write(client, reg,
						 *data++)) < 0)
				break;
			len -= 2;
		}
	}

	return ret;
}

static inline int
saa7114_read (struct i2c_client *client,
	      u8                 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

// initially set NTSC, composite


static const unsigned char init[] = {
	0x00, 0x00,		/* 00 - ID byte , chip version,
				 * read only */
	0x01, 0x08,		/* 01 - X,X,X,X, IDEL3 to IDEL0 -
				 * horizontal increment delay,
				 * recommended position */
	0x02, 0x00,		/* 02 - FUSE=3, GUDL=2, MODE=0 ;
				 * input control */
	0x03, 0x10,		/* 03 - HLNRS=0, VBSL=1, WPOFF=0,
				 * HOLDG=0, GAFIX=0, GAI1=256, GAI2=256 */
	0x04, 0x90,		/* 04 - GAI1=256 */
	0x05, 0x90,		/* 05 - GAI2=256 */
	0x06, SAA_7114_NTSC_HSYNC_START,	/* 06 - HSB: hsync start,
				 * depends on the video standard */
	0x07, SAA_7114_NTSC_HSYNC_STOP,	/* 07 - HSS: hsync stop, depends
				 *on the video standard */
	0x08, 0xb8,		/* 08 - AUFD=1, FSEL=1, EXFIL=0, VTRC=1,
				 * HPLL: free running in playback, locked
				 * in capture, VNOI=0 */
	0x09, 0x80,		/* 09 - BYPS=0, PREF=0, BPSS=0, VBLB=0,
				 * UPTCV=0, APER=1; depends from input */
	0x0a, 0x80,		/* 0a - BRIG=128 */
	0x0b, 0x44,		/* 0b - CONT=1.109 */
	0x0c, 0x40,		/* 0c - SATN=1.0 */
	0x0d, 0x00,		/* 0d - HUE=0 */
	0x0e, 0x84,		/* 0e - CDTO, CSTD2 to 0, DCVF, FCTC,
				 * CCOMB; depends from video standard */
	0x0f, 0x24,		/* 0f - ACGC,CGAIN6 to CGAIN0; depends
				 * from video standard */
	0x10, 0x03,		/* 10 - OFFU1 to 0, OFFV1 to 0, CHBW,
				 * LCBW2 to 0 */
	0x11, 0x59,		/* 11 - COLO, RTP1, HEDL1 to 0, RTP0,
				 * YDEL2 to 0 */
	0x12, 0xc9,		/* 12 - RT signal control RTSE13 to 10
				 * and 03 to 00 */
	0x13, 0x80,		/* 13 - RT/X port output control  */
	0x14, 0x00,		/* 14 - analog, ADC, compatibility control */
	0x15, 0x00,		/* 15 - VGATE start FID change  */
	0x16, 0xfe,		/* 16 - VGATE stop */
	0x17, 0x00,		/* 17 - Misc., VGATE MSBs */
	0x18, 0x40,		/* RAWG */
	0x19, 0x80,		/* RAWO */
	0x1a, 0x00,
	0x1b, 0x00,
	0x1c, 0x00,
	0x1d, 0x00,
	0x1e, 0x00,
	0x1f, 0x00,		/* status byte, read only */
	0x20, 0x00,		/* video decoder reserved part */
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x00,
	0x25, 0x00,
	0x26, 0x00,
	0x27, 0x00,
	0x28, 0x00,
	0x29, 0x00,
	0x2a, 0x00,
	0x2b, 0x00,
	0x2c, 0x00,
	0x2d, 0x00,
	0x2e, 0x00,
	0x2f, 0x00,
	0x30, 0xbc,		/* audio clock generator */
	0x31, 0xdf,
	0x32, 0x02,
	0x33, 0x00,
	0x34, 0xcd,
	0x35, 0xcc,
	0x36, 0x3a,
	0x37, 0x00,
	0x38, 0x03,
	0x39, 0x10,
	0x3a, 0x00,
	0x3b, 0x00,
	0x3c, 0x00,
	0x3d, 0x00,
	0x3e, 0x00,
	0x3f, 0x00,
	0x40, 0x00,		/* VBI data slicer */
	0x41, 0xff,
	0x42, 0xff,
	0x43, 0xff,
	0x44, 0xff,
	0x45, 0xff,
	0x46, 0xff,
	0x47, 0xff,
	0x48, 0xff,
	0x49, 0xff,
	0x4a, 0xff,
	0x4b, 0xff,
	0x4c, 0xff,
	0x4d, 0xff,
	0x4e, 0xff,
	0x4f, 0xff,
	0x50, 0xff,
	0x51, 0xff,
	0x52, 0xff,
	0x53, 0xff,
	0x54, 0xff,
	0x55, 0xff,
	0x56, 0xff,
	0x57, 0xff,
	0x58, 0x40,		// framing code
	0x59, 0x47,		// horizontal offset
	0x5a, 0x06,		// vertical offset
	0x5b, 0x83,		// field offset
	0x5c, 0x00,		// reserved
	0x5d, 0x3e,		// header and data
	0x5e, 0x00,		// sliced data
	0x5f, 0x00,		// reserved
	0x60, 0x00,		/* video decoder reserved part */
	0x61, 0x00,
	0x62, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x00,
	0x6b, 0x00,
	0x6c, 0x00,
	0x6d, 0x00,
	0x6e, 0x00,
	0x6f, 0x00,
	0x70, 0x00,		/* video decoder reserved part */
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x00,
	0x75, 0x00,
	0x76, 0x00,
	0x77, 0x00,
	0x78, 0x00,
	0x79, 0x00,
	0x7a, 0x00,
	0x7b, 0x00,
	0x7c, 0x00,
	0x7d, 0x00,
	0x7e, 0x00,
	0x7f, 0x00,
	0x80, 0x00,		/* X-port, I-port and scaler */
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0xc5,
	0x85, 0x0d,		// hsync and vsync ?
	0x86, 0x40,
	0x87, 0x01,
	0x88, 0x00,
	0x89, 0x00,
	0x8a, 0x00,
	0x8b, 0x00,
	0x8c, 0x00,
	0x8d, 0x00,
	0x8e, 0x00,
	0x8f, 0x00,
	0x90, 0x03,		/* Task A definition           */
	0x91, 0x08,
	0x92, 0x00,
	0x93, 0x40,
	0x94, 0x00,		// window settings
	0x95, 0x00,
	0x96, 0x00,
	0x97, 0x00,
	0x98, 0x00,
	0x99, 0x00,
	0x9a, 0x00,
	0x9b, 0x00,
	0x9c, 0x00,
	0x9d, 0x00,
	0x9e, 0x00,
	0x9f, 0x00,
	0xa0, 0x01,		/* horizontal integer prescaling ratio */
	0xa1, 0x00,		/* horizontal prescaler accumulation
				 * sequence length */
	0xa2, 0x00,		/* UV FIR filter, Y FIR filter, prescaler
				 * DC gain */
	0xa3, 0x00,
	0xa4, 0x80,		// luminance brightness
	0xa5, 0x40,		// luminance gain
	0xa6, 0x40,		// chrominance saturation
	0xa7, 0x00,
	0xa8, 0x00,		// horizontal luminance scaling increment
	0xa9, 0x04,
	0xaa, 0x00,		// horizontal luminance phase offset
	0xab, 0x00,
	0xac, 0x00,		// horizontal chrominance scaling increment
	0xad, 0x02,
	0xae, 0x00,		// horizontal chrominance phase offset
	0xaf, 0x00,
	0xb0, 0x00,		// vertical luminance scaling increment
	0xb1, 0x04,
	0xb2, 0x00,		// vertical chrominance scaling increment
	0xb3, 0x04,
	0xb4, 0x00,
	0xb5, 0x00,
	0xb6, 0x00,
	0xb7, 0x00,
	0xb8, 0x00,
	0xb9, 0x00,
	0xba, 0x00,
	0xbb, 0x00,
	0xbc, 0x00,
	0xbd, 0x00,
	0xbe, 0x00,
	0xbf, 0x00,
	0xc0, 0x02,		// Task B definition
	0xc1, 0x08,
	0xc2, 0x00,
	0xc3, 0x40,
	0xc4, 0x00,		// window settings
	0xc5, 0x00,
	0xc6, 0x00,
	0xc7, 0x00,
	0xc8, 0x00,
	0xc9, 0x00,
	0xca, 0x00,
	0xcb, 0x00,
	0xcc, 0x00,
	0xcd, 0x00,
	0xce, 0x00,
	0xcf, 0x00,
	0xd0, 0x01,		// horizontal integer prescaling ratio
	0xd1, 0x00,		// horizontal prescaler accumulation sequence length
	0xd2, 0x00,		// UV FIR filter, Y FIR filter, prescaler DC gain
	0xd3, 0x00,
	0xd4, 0x80,		// luminance brightness
	0xd5, 0x40,		// luminance gain
	0xd6, 0x40,		// chrominance saturation
	0xd7, 0x00,
	0xd8, 0x00,		// horizontal luminance scaling increment
	0xd9, 0x04,
	0xda, 0x00,		// horizontal luminance phase offset
	0xdb, 0x00,
	0xdc, 0x00,		// horizontal chrominance scaling increment
	0xdd, 0x02,
	0xde, 0x00,		// horizontal chrominance phase offset
	0xdf, 0x00,
	0xe0, 0x00,		// vertical luminance scaling increment
	0xe1, 0x04,
	0xe2, 0x00,		// vertical chrominance scaling increment
	0xe3, 0x04,
	0xe4, 0x00,
	0xe5, 0x00,
	0xe6, 0x00,
	0xe7, 0x00,
	0xe8, 0x00,
	0xe9, 0x00,
	0xea, 0x00,
	0xeb, 0x00,
	0xec, 0x00,
	0xed, 0x00,
	0xee, 0x00,
	0xef, 0x00
};

static int
saa7114_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct saa7114 *decoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
		//dprintk(1, KERN_INFO "%s: writing init\n", I2C_NAME(client));
		//saa7114_write_block(client, init, sizeof(init));
		break;

	case DECODER_DUMP:
	{
		int i;

		dprintk(1, KERN_INFO "%s: decoder dump\n", I2C_NAME(client));

		for (i = 0; i < 32; i += 16) {
			int j;

			printk(KERN_DEBUG "%s: %03x", I2C_NAME(client), i);
			for (j = 0; j < 16; ++j) {
				printk(" %02x",
				       saa7114_read(client, i + j));
			}
			printk("\n");
		}
	}
		break;

	case DECODER_GET_CAPABILITIES:
	{
		struct video_decoder_capability *cap = arg;

		dprintk(1, KERN_DEBUG "%s: decoder get capabilities\n",
			I2C_NAME(client));

		cap->flags = VIDEO_DECODER_PAL |
			     VIDEO_DECODER_NTSC |
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

		status = saa7114_read(client, 0x1f);

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

	case DECODER_SET_NORM:
	{
		int *iarg = arg;

		short int hoff = 0, voff = 0, w = 0, h = 0;

		dprintk(1, KERN_DEBUG "%s: decoder set norm ",
			I2C_NAME(client));
		switch (*iarg) {

		case VIDEO_MODE_NTSC:
			dprintk(1, "NTSC\n");
			decoder->reg[REG_ADDR(0x06)] =
			    SAA_7114_NTSC_HSYNC_START;
			decoder->reg[REG_ADDR(0x07)] =
			    SAA_7114_NTSC_HSYNC_STOP;

			decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8;	// PLL free when playback, PLL close when capture

			decoder->reg[REG_ADDR(0x0e)] = 0x85;
			decoder->reg[REG_ADDR(0x0f)] = 0x24;

			hoff = SAA_7114_NTSC_HOFFSET;
			voff = SAA_7114_NTSC_VOFFSET;
			w = SAA_7114_NTSC_WIDTH;
			h = SAA_7114_NTSC_HEIGHT;

			break;

		case VIDEO_MODE_PAL:
			dprintk(1, "PAL\n");
			decoder->reg[REG_ADDR(0x06)] =
			    SAA_7114_PAL_HSYNC_START;
			decoder->reg[REG_ADDR(0x07)] =
			    SAA_7114_PAL_HSYNC_STOP;

			decoder->reg[REG_ADDR(0x08)] = decoder->playback ? 0x7c : 0xb8;	// PLL free when playback, PLL close when capture

			decoder->reg[REG_ADDR(0x0e)] = 0x81;
			decoder->reg[REG_ADDR(0x0f)] = 0x24;

			hoff = SAA_7114_PAL_HOFFSET;
			voff = SAA_7114_PAL_VOFFSET;
			w = SAA_7114_PAL_WIDTH;
			h = SAA_7114_PAL_HEIGHT;

			break;

		default:
			dprintk(1, " Unknown video mode!!!\n");
			return -EINVAL;

		}


		decoder->reg[REG_ADDR(0x94)] = LOBYTE(hoff);	// hoffset low
		decoder->reg[REG_ADDR(0x95)] = HIBYTE(hoff) & 0x0f;	// hoffset high
		decoder->reg[REG_ADDR(0x96)] = LOBYTE(w);	// width low
		decoder->reg[REG_ADDR(0x97)] = HIBYTE(w) & 0x0f;	// width high
		decoder->reg[REG_ADDR(0x98)] = LOBYTE(voff);	// voffset low
		decoder->reg[REG_ADDR(0x99)] = HIBYTE(voff) & 0x0f;	// voffset high
		decoder->reg[REG_ADDR(0x9a)] = LOBYTE(h + 2);	// height low
		decoder->reg[REG_ADDR(0x9b)] = HIBYTE(h + 2) & 0x0f;	// height high
		decoder->reg[REG_ADDR(0x9c)] = LOBYTE(w);	// out width low
		decoder->reg[REG_ADDR(0x9d)] = HIBYTE(w) & 0x0f;	// out width high
		decoder->reg[REG_ADDR(0x9e)] = LOBYTE(h);	// out height low
		decoder->reg[REG_ADDR(0x9f)] = HIBYTE(h) & 0x0f;	// out height high

		decoder->reg[REG_ADDR(0xc4)] = LOBYTE(hoff);	// hoffset low
		decoder->reg[REG_ADDR(0xc5)] = HIBYTE(hoff) & 0x0f;	// hoffset high
		decoder->reg[REG_ADDR(0xc6)] = LOBYTE(w);	// width low
		decoder->reg[REG_ADDR(0xc7)] = HIBYTE(w) & 0x0f;	// width high
		decoder->reg[REG_ADDR(0xc8)] = LOBYTE(voff);	// voffset low
		decoder->reg[REG_ADDR(0xc9)] = HIBYTE(voff) & 0x0f;	// voffset high
		decoder->reg[REG_ADDR(0xca)] = LOBYTE(h + 2);	// height low
		decoder->reg[REG_ADDR(0xcb)] = HIBYTE(h + 2) & 0x0f;	// height high
		decoder->reg[REG_ADDR(0xcc)] = LOBYTE(w);	// out width low
		decoder->reg[REG_ADDR(0xcd)] = HIBYTE(w) & 0x0f;	// out width high
		decoder->reg[REG_ADDR(0xce)] = LOBYTE(h);	// out height low
		decoder->reg[REG_ADDR(0xcf)] = HIBYTE(h) & 0x0f;	// out height high


		saa7114_write(client, 0x80, 0x06);	// i-port and scaler back end clock selection, task A&B off
		saa7114_write(client, 0x88, 0xd8);	// sw reset scaler
		saa7114_write(client, 0x88, 0xf8);	// sw reset scaler release

		saa7114_write_block(client, decoder->reg + (0x06 << 1),
				    3 << 1);
		saa7114_write_block(client, decoder->reg + (0x0e << 1),
				    2 << 1);
		saa7114_write_block(client, decoder->reg + (0x5a << 1),
				    2 << 1);

		saa7114_write_block(client, decoder->reg + (0x94 << 1),
				    (0x9f + 1 - 0x94) << 1);
		saa7114_write_block(client, decoder->reg + (0xc4 << 1),
				    (0xcf + 1 - 0xc4) << 1);

		saa7114_write(client, 0x88, 0xd8);	// sw reset scaler
		saa7114_write(client, 0x88, 0xf8);	// sw reset scaler release
		saa7114_write(client, 0x80, 0x36);	// i-port and scaler back end clock selection

		decoder->norm = *iarg;
	}
		break;

	case DECODER_SET_INPUT:
	{
		int *iarg = arg;

		dprintk(1, KERN_DEBUG "%s: decoder set input (%d)\n",
			I2C_NAME(client), *iarg);
		if (*iarg < 0 || *iarg > 7) {
			return -EINVAL;
		}

		if (decoder->input != *iarg) {
			dprintk(1, KERN_DEBUG "%s: now setting %s input\n",
				I2C_NAME(client),
				*iarg >= 6 ? "S-Video" : "Composite");
			decoder->input = *iarg;

			/* select mode */
			decoder->reg[REG_ADDR(0x02)] =
			    (decoder->
			     reg[REG_ADDR(0x02)] & 0xf0) | (decoder->
							    input <
							    6 ? 0x0 : 0x9);
			saa7114_write(client, 0x02,
				      decoder->reg[REG_ADDR(0x02)]);

			/* bypass chrominance trap for modes 6..9 */
			decoder->reg[REG_ADDR(0x09)] =
			    (decoder->
			     reg[REG_ADDR(0x09)] & 0x7f) | (decoder->
							    input <
							    6 ? 0x0 :
							    0x80);
			saa7114_write(client, 0x09,
				      decoder->reg[REG_ADDR(0x09)]);

			decoder->reg[REG_ADDR(0x0e)] =
			    decoder->input <
			    6 ? decoder->
			    reg[REG_ADDR(0x0e)] | 1 : decoder->
			    reg[REG_ADDR(0x0e)] & ~1;
			saa7114_write(client, 0x0e,
				      decoder->reg[REG_ADDR(0x0e)]);
		}
	}
		break;

	case DECODER_SET_OUTPUT:
	{
		int *iarg = arg;

		dprintk(1, KERN_DEBUG "%s: decoder set output\n",
			I2C_NAME(client));

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

		dprintk(1, KERN_DEBUG "%s: decoder %s output\n",
			I2C_NAME(client), enable ? "enable" : "disable");

		decoder->playback = !enable;

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
				decoder->reg[REG_ADDR(0x08)] = 0xb8;
				decoder->reg[REG_ADDR(0x12)] = 0xc9;
				decoder->reg[REG_ADDR(0x13)] = 0x80;
				decoder->reg[REG_ADDR(0x87)] = 0x01;
			} else {
				decoder->reg[REG_ADDR(0x08)] = 0x7c;
				decoder->reg[REG_ADDR(0x12)] = 0x00;
				decoder->reg[REG_ADDR(0x13)] = 0x00;
				decoder->reg[REG_ADDR(0x87)] = 0x00;
			}

			saa7114_write_block(client,
					    decoder->reg + (0x12 << 1),
					    2 << 1);
			saa7114_write(client, 0x08,
				      decoder->reg[REG_ADDR(0x08)]);
			saa7114_write(client, 0x87,
				      decoder->reg[REG_ADDR(0x87)]);
			saa7114_write(client, 0x88, 0xd8);	// sw reset scaler
			saa7114_write(client, 0x88, 0xf8);	// sw reset scaler release
			saa7114_write(client, 0x80, 0x36);

		}
	}
		break;

	case DECODER_SET_PICTURE:
	{
		struct video_picture *pic = arg;

		dprintk(1,
			KERN_DEBUG
			"%s: decoder set picture bright=%d contrast=%d saturation=%d hue=%d\n",
			I2C_NAME(client), pic->brightness, pic->contrast,
			pic->colour, pic->hue);

		if (decoder->bright != pic->brightness) {
			/* We want 0 to 255 we get 0-65535 */
			decoder->bright = pic->brightness;
			saa7114_write(client, 0x0a, decoder->bright >> 8);
		}
		if (decoder->contrast != pic->contrast) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->contrast = pic->contrast;
			saa7114_write(client, 0x0b,
				      decoder->contrast >> 9);
		}
		if (decoder->sat != pic->colour) {
			/* We want 0 to 127 we get 0-65535 */
			decoder->sat = pic->colour;
			saa7114_write(client, 0x0c, decoder->sat >> 9);
		}
		if (decoder->hue != pic->hue) {
			/* We want -128 to 127 we get 0-65535 */
			decoder->hue = pic->hue;
			saa7114_write(client, 0x0d,
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
static unsigned short normal_i2c[] =
    { I2C_SAA7114 >> 1, I2C_SAA7114A >> 1, I2C_CLIENT_END };

static unsigned short ignore = I2C_CLIENT_END;

static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver i2c_driver_saa7114;

static int
saa7114_detect_client (struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	int i, err[30];
	short int hoff = SAA_7114_NTSC_HOFFSET;
	short int voff = SAA_7114_NTSC_VOFFSET;
	short int w = SAA_7114_NTSC_WIDTH;
	short int h = SAA_7114_NTSC_HEIGHT;
	struct i2c_client *client;
	struct saa7114 *decoder;

	dprintk(1,
		KERN_INFO
		"saa7114.c: detecting saa7114 client on address 0x%x\n",
		address << 1);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_saa7114;
	strlcpy(I2C_NAME(client), "saa7114", sizeof(I2C_NAME(client)));

	decoder = kzalloc(sizeof(struct saa7114), GFP_KERNEL);
	if (decoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	decoder->norm = VIDEO_MODE_NTSC;
	decoder->input = -1;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;
	decoder->playback = 0;	// initially capture mode useda
	i2c_set_clientdata(client, decoder);

	memcpy(decoder->reg, init, sizeof(init));

	decoder->reg[REG_ADDR(0x94)] = LOBYTE(hoff);	// hoffset low
	decoder->reg[REG_ADDR(0x95)] = HIBYTE(hoff) & 0x0f;	// hoffset high
	decoder->reg[REG_ADDR(0x96)] = LOBYTE(w);	// width low
	decoder->reg[REG_ADDR(0x97)] = HIBYTE(w) & 0x0f;	// width high
	decoder->reg[REG_ADDR(0x98)] = LOBYTE(voff);	// voffset low
	decoder->reg[REG_ADDR(0x99)] = HIBYTE(voff) & 0x0f;	// voffset high
	decoder->reg[REG_ADDR(0x9a)] = LOBYTE(h + 2);	// height low
	decoder->reg[REG_ADDR(0x9b)] = HIBYTE(h + 2) & 0x0f;	// height high
	decoder->reg[REG_ADDR(0x9c)] = LOBYTE(w);	// out width low
	decoder->reg[REG_ADDR(0x9d)] = HIBYTE(w) & 0x0f;	// out width high
	decoder->reg[REG_ADDR(0x9e)] = LOBYTE(h);	// out height low
	decoder->reg[REG_ADDR(0x9f)] = HIBYTE(h) & 0x0f;	// out height high

	decoder->reg[REG_ADDR(0xc4)] = LOBYTE(hoff);	// hoffset low
	decoder->reg[REG_ADDR(0xc5)] = HIBYTE(hoff) & 0x0f;	// hoffset high
	decoder->reg[REG_ADDR(0xc6)] = LOBYTE(w);	// width low
	decoder->reg[REG_ADDR(0xc7)] = HIBYTE(w) & 0x0f;	// width high
	decoder->reg[REG_ADDR(0xc8)] = LOBYTE(voff);	// voffset low
	decoder->reg[REG_ADDR(0xc9)] = HIBYTE(voff) & 0x0f;	// voffset high
	decoder->reg[REG_ADDR(0xca)] = LOBYTE(h + 2);	// height low
	decoder->reg[REG_ADDR(0xcb)] = HIBYTE(h + 2) & 0x0f;	// height high
	decoder->reg[REG_ADDR(0xcc)] = LOBYTE(w);	// out width low
	decoder->reg[REG_ADDR(0xcd)] = HIBYTE(w) & 0x0f;	// out width high
	decoder->reg[REG_ADDR(0xce)] = LOBYTE(h);	// out height low
	decoder->reg[REG_ADDR(0xcf)] = HIBYTE(h) & 0x0f;	// out height high

	decoder->reg[REG_ADDR(0xb8)] =
	    LOBYTE(LOWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xb9)] =
	    HIBYTE(LOWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xba)] =
	    LOBYTE(HIWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xbb)] =
	    HIBYTE(HIWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));

	decoder->reg[REG_ADDR(0xbc)] =
	    LOBYTE(LOWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xbd)] =
	    HIBYTE(LOWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xbe)] =
	    LOBYTE(HIWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xbf)] =
	    HIBYTE(HIWORD(SAA_7114_VERTICAL_LUMA_OFFSET));

	decoder->reg[REG_ADDR(0xe8)] =
	    LOBYTE(LOWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xe9)] =
	    HIBYTE(LOWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xea)] =
	    LOBYTE(HIWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));
	decoder->reg[REG_ADDR(0xeb)] =
	    HIBYTE(HIWORD(SAA_7114_VERTICAL_CHROMA_OFFSET));

	decoder->reg[REG_ADDR(0xec)] =
	    LOBYTE(LOWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xed)] =
	    HIBYTE(LOWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xee)] =
	    LOBYTE(HIWORD(SAA_7114_VERTICAL_LUMA_OFFSET));
	decoder->reg[REG_ADDR(0xef)] =
	    HIBYTE(HIWORD(SAA_7114_VERTICAL_LUMA_OFFSET));


	decoder->reg[REG_ADDR(0x13)] = 0x80;	// RTC0 on
	decoder->reg[REG_ADDR(0x87)] = 0x01;	// I-Port
	decoder->reg[REG_ADDR(0x12)] = 0xc9;	// RTS0

	decoder->reg[REG_ADDR(0x02)] = 0xc0;	// set composite1 input, aveasy
	decoder->reg[REG_ADDR(0x09)] = 0x00;	// chrominance trap
	decoder->reg[REG_ADDR(0x0e)] |= 1;	// combfilter on


	dprintk(1, KERN_DEBUG "%s_attach: starting decoder init\n",
		I2C_NAME(client));

	err[0] =
	    saa7114_write_block(client, decoder->reg + (0x20 << 1),
				0x10 << 1);
	err[1] =
	    saa7114_write_block(client, decoder->reg + (0x30 << 1),
				0x10 << 1);
	err[2] =
	    saa7114_write_block(client, decoder->reg + (0x63 << 1),
				(0x7f + 1 - 0x63) << 1);
	err[3] =
	    saa7114_write_block(client, decoder->reg + (0x89 << 1),
				6 << 1);
	err[4] =
	    saa7114_write_block(client, decoder->reg + (0xb8 << 1),
				8 << 1);
	err[5] =
	    saa7114_write_block(client, decoder->reg + (0xe8 << 1),
				8 << 1);


	for (i = 0; i <= 5; i++) {
		if (err[i] < 0) {
			dprintk(1,
				KERN_ERR
				"%s_attach: init error %d at stage %d, leaving attach.\n",
				I2C_NAME(client), i, err[i]);
			kfree(decoder);
			kfree(client);
			return 0;
		}
	}

	for (i = 6; i < 8; i++) {
		dprintk(1,
			KERN_DEBUG
			"%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
			I2C_NAME(client), i, saa7114_read(client, i),
			decoder->reg[REG_ADDR(i)]);
	}

	dprintk(1,
		KERN_DEBUG
		"%s_attach: performing decoder reset sequence\n",
		I2C_NAME(client));

	err[6] = saa7114_write(client, 0x80, 0x06);	// i-port and scaler backend clock selection, task A&B off
	err[7] = saa7114_write(client, 0x88, 0xd8);	// sw reset scaler
	err[8] = saa7114_write(client, 0x88, 0xf8);	// sw reset scaler release

	for (i = 6; i <= 8; i++) {
		if (err[i] < 0) {
			dprintk(1,
				KERN_ERR
				"%s_attach: init error %d at stage %d, leaving attach.\n",
				I2C_NAME(client), i, err[i]);
			kfree(decoder);
			kfree(client);
			return 0;
		}
	}

	dprintk(1, KERN_INFO "%s_attach: performing the rest of init\n",
		I2C_NAME(client));


	err[9] = saa7114_write(client, 0x01, decoder->reg[REG_ADDR(0x01)]);
	err[10] = saa7114_write_block(client, decoder->reg + (0x03 << 1), (0x1e + 1 - 0x03) << 1);	// big seq
	err[11] = saa7114_write_block(client, decoder->reg + (0x40 << 1), (0x5f + 1 - 0x40) << 1);	// slicer
	err[12] = saa7114_write_block(client, decoder->reg + (0x81 << 1), 2 << 1);	// ?
	err[13] = saa7114_write_block(client, decoder->reg + (0x83 << 1), 5 << 1);	// ?
	err[14] = saa7114_write_block(client, decoder->reg + (0x90 << 1), 4 << 1);	// Task A
	err[15] =
	    saa7114_write_block(client, decoder->reg + (0x94 << 1),
				12 << 1);
	err[16] =
	    saa7114_write_block(client, decoder->reg + (0xa0 << 1),
				8 << 1);
	err[17] =
	    saa7114_write_block(client, decoder->reg + (0xa8 << 1),
				8 << 1);
	err[18] =
	    saa7114_write_block(client, decoder->reg + (0xb0 << 1),
				8 << 1);
	err[19] = saa7114_write_block(client, decoder->reg + (0xc0 << 1), 4 << 1);	// Task B
	err[15] =
	    saa7114_write_block(client, decoder->reg + (0xc4 << 1),
				12 << 1);
	err[16] =
	    saa7114_write_block(client, decoder->reg + (0xd0 << 1),
				8 << 1);
	err[17] =
	    saa7114_write_block(client, decoder->reg + (0xd8 << 1),
				8 << 1);
	err[18] =
	    saa7114_write_block(client, decoder->reg + (0xe0 << 1),
				8 << 1);

	for (i = 9; i <= 18; i++) {
		if (err[i] < 0) {
			dprintk(1,
				KERN_ERR
				"%s_attach: init error %d at stage %d, leaving attach.\n",
				I2C_NAME(client), i, err[i]);
			kfree(decoder);
			kfree(client);
			return 0;
		}
	}


	for (i = 6; i < 8; i++) {
		dprintk(1,
			KERN_DEBUG
			"%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
			I2C_NAME(client), i, saa7114_read(client, i),
			decoder->reg[REG_ADDR(i)]);
	}


	for (i = 0x11; i <= 0x13; i++) {
		dprintk(1,
			KERN_DEBUG
			"%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
			I2C_NAME(client), i, saa7114_read(client, i),
			decoder->reg[REG_ADDR(i)]);
	}


	dprintk(1, KERN_DEBUG "%s_attach: setting video input\n",
		I2C_NAME(client));

	err[19] =
	    saa7114_write(client, 0x02, decoder->reg[REG_ADDR(0x02)]);
	err[20] =
	    saa7114_write(client, 0x09, decoder->reg[REG_ADDR(0x09)]);
	err[21] =
	    saa7114_write(client, 0x0e, decoder->reg[REG_ADDR(0x0e)]);

	for (i = 19; i <= 21; i++) {
		if (err[i] < 0) {
			dprintk(1,
				KERN_ERR
				"%s_attach: init error %d at stage %d, leaving attach.\n",
				I2C_NAME(client), i, err[i]);
			kfree(decoder);
			kfree(client);
			return 0;
		}
	}

	dprintk(1,
		KERN_DEBUG
		"%s_attach: performing decoder reset sequence\n",
		I2C_NAME(client));

	err[22] = saa7114_write(client, 0x88, 0xd8);	// sw reset scaler
	err[23] = saa7114_write(client, 0x88, 0xf8);	// sw reset scaler release
	err[24] = saa7114_write(client, 0x80, 0x36);	// i-port and scaler backend clock selection, task A&B off


	for (i = 22; i <= 24; i++) {
		if (err[i] < 0) {
			dprintk(1,
				KERN_ERR
				"%s_attach: init error %d at stage %d, leaving attach.\n",
				I2C_NAME(client), i, err[i]);
			kfree(decoder);
			kfree(client);
			return 0;
		}
	}

	err[25] = saa7114_write(client, 0x06, init[REG_ADDR(0x06)]);
	err[26] = saa7114_write(client, 0x07, init[REG_ADDR(0x07)]);
	err[27] = saa7114_write(client, 0x10, init[REG_ADDR(0x10)]);

	dprintk(1,
		KERN_INFO
		"%s_attach: chip version %x, decoder status 0x%02x\n",
		I2C_NAME(client), saa7114_read(client, 0x00) >> 4,
		saa7114_read(client, 0x1f));
	dprintk(1,
		KERN_DEBUG
		"%s_attach: power save control: 0x%02x, scaler status: 0x%02x\n",
		I2C_NAME(client), saa7114_read(client, 0x88),
		saa7114_read(client, 0x8f));


	for (i = 0x94; i < 0x96; i++) {
		dprintk(1,
			KERN_DEBUG
			"%s_attach: reg[0x%02x] = 0x%02x (0x%02x)\n",
			I2C_NAME(client), i, saa7114_read(client, i),
			decoder->reg[REG_ADDR(i)]);
	}

	i = i2c_attach_client(client);
	if (i) {
		kfree(client);
		kfree(decoder);
		return i;
	}

	//i = saa7114_write_block(client, init, sizeof(init));
	i = 0;
	if (i < 0) {
		dprintk(1, KERN_ERR "%s_attach error: init status %d\n",
			I2C_NAME(client), i);
	} else {
		dprintk(1,
			KERN_INFO
			"%s_attach: chip version %x at address 0x%x\n",
			I2C_NAME(client), saa7114_read(client, 0x00) >> 4,
			client->addr << 1);
	}

	return 0;
}

static int
saa7114_attach_adapter (struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"saa7114.c: starting probe for adapter %s (0x%x)\n",
		I2C_NAME(adapter), adapter->id);
	return i2c_probe(adapter, &addr_data, &saa7114_detect_client);
}

static int
saa7114_detach_client (struct i2c_client *client)
{
	struct saa7114 *decoder = i2c_get_clientdata(client);
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

static struct i2c_driver i2c_driver_saa7114 = {
	.driver = {
		.name = "saa7114",
	},

	.id = I2C_DRIVERID_SAA7114,

	.attach_adapter = saa7114_attach_adapter,
	.detach_client = saa7114_detach_client,
	.command = saa7114_command,
};

static int __init
saa7114_init (void)
{
	return i2c_add_driver(&i2c_driver_saa7114);
}

static void __exit
saa7114_exit (void)
{
	i2c_del_driver(&i2c_driver_saa7114);
}

module_init(saa7114_init);
module_exit(saa7114_exit);
