/* 
 * vpx3220a, vpx3216b & vpx3214c video decoder driver version 0.0.1
 *
 * Copyright (C) 2001 Laurent Pinchart <lpinchart@freegates.be>
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
#include <linux/types.h>
#include <linux/slab.h>

#include <linux/byteorder/swab.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/i2c.h>

#define I2C_NAME(x) (x)->name

#include <linux/videodev.h>
#include <linux/video_decoder.h>

#define I2C_VPX3220        0x86
#define VPX3220_DEBUG	KERN_DEBUG "vpx3220: "

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format, ##args); \
	} while (0)

#define VPX_TIMEOUT_COUNT  10

/* ----------------------------------------------------------------------- */

struct vpx3220 {
	unsigned char reg[255];

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static char *inputs[] = { "internal", "composite", "svideo" };

/* ----------------------------------------------------------------------- */
static inline int
vpx3220_write (struct i2c_client *client,
	       u8                 reg,
	       u8                 value)
{
	struct vpx3220 *decoder = i2c_get_clientdata(client);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int
vpx3220_read (struct i2c_client *client,
	      u8                 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int
vpx3220_fp_status (struct i2c_client *client)
{
	unsigned char status;
	unsigned int i;

	for (i = 0; i < VPX_TIMEOUT_COUNT; i++) {
		status = vpx3220_read(client, 0x29);

		if (!(status & 4))
			return 0;

		udelay(10);

		if (need_resched())
			cond_resched();
	}

	return -1;
}

static int
vpx3220_fp_write (struct i2c_client *client,
		  u8                 fpaddr,
		  u16                data)
{
	/* Write the 16-bit address to the FPWR register */
	if (i2c_smbus_write_word_data(client, 0x27, swab16(fpaddr)) == -1) {
		dprintk(1, VPX3220_DEBUG "%s: failed\n", __func__);
		return -1;
	}

	if (vpx3220_fp_status(client) < 0)
		return -1;

	/* Write the 16-bit data to the FPDAT register */
	if (i2c_smbus_write_word_data(client, 0x28, swab16(data)) == -1) {
		dprintk(1, VPX3220_DEBUG "%s: failed\n", __func__);
		return -1;
	}

	return 0;
}

static u16
vpx3220_fp_read (struct i2c_client *client,
		 u16                fpaddr)
{
	s16 data;

	/* Write the 16-bit address to the FPRD register */
	if (i2c_smbus_write_word_data(client, 0x26, swab16(fpaddr)) == -1) {
		dprintk(1, VPX3220_DEBUG "%s: failed\n", __func__);
		return -1;
	}

	if (vpx3220_fp_status(client) < 0)
		return -1;

	/* Read the 16-bit data from the FPDAT register */
	data = i2c_smbus_read_word_data(client, 0x28);
	if (data == -1) {
		dprintk(1, VPX3220_DEBUG "%s: failed\n", __func__);
		return -1;
	}

	return swab16(data);
}

static int
vpx3220_write_block (struct i2c_client *client,
		     const u8          *data,
		     unsigned int       len)
{
	u8 reg;
	int ret = -1;

	while (len >= 2) {
		reg = *data++;
		if ((ret =
		     vpx3220_write(client, reg, *data++)) < 0)
			break;
		len -= 2;
	}

	return ret;
}

static int
vpx3220_write_fp_block (struct i2c_client *client,
		        const u16         *data,
		        unsigned int       len)
{
	u8 reg;
	int ret = 0;

	while (len > 1) {
		reg = *data++;
		ret |= vpx3220_fp_write(client, reg, *data++);
		len -= 2;
	}

	return ret;
}

/* ---------------------------------------------------------------------- */

static const unsigned short init_ntsc[] = {
	0x1c, 0x00,		/* NTSC tint angle */
	0x88, 17,		/* Window 1 vertical */
	0x89, 240,		/* Vertical lines in */
	0x8a, 240,		/* Vertical lines out */
	0x8b, 000,		/* Horizontal begin */
	0x8c, 640,		/* Horizontal length */
	0x8d, 640,		/* Number of pixels */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x73,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x13,		/* NTSC M, composite input */
	0xe7, 0x1e1,		/* Enable vertical standard
				 * locking @ 240 lines */
};

static const unsigned short init_pal[] = {
	0x88, 23,		/* Window 1 vertical begin */
	0x89, 288,		/* Vertical lines in (16 lines
				 * skipped by the VFE) */
	0x8a, 288,		/* Vertical lines out (16 lines
				 * skipped by the VFE) */
	0x8b, 16,		/* Horizontal begin */
	0x8c, 768,		/* Horizontal length */
	0x8d, 784, 		/* Number of pixels
				 * Must be >= Horizontal begin + Horizontal length */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x77,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x3d1,		/* PAL B,G,H,I, composite input */
	0xe7, 0x241,		/* PAL/SECAM set to 288 lines */
};

static const unsigned short init_secam[] = {
	0x88, 23,		/* Window 1 vertical begin */
	0x89, 288,		/* Vertical lines in (16 lines
				 * skipped by the VFE) */
	0x8a, 288,		/* Vertical lines out (16 lines
				 * skipped by the VFE) */
	0x8b, 16,		/* Horizontal begin */
	0x8c, 768,		/* Horizontal length */
	0x8d, 784,		/* Number of pixels
				 * Must be >= Horizontal begin + Horizontal length */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x77,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x3d5,		/* SECAM, composite input */
	0xe7, 0x241,		/* PAL/SECAM set to 288 lines */
};

static const unsigned char init_common[] = {
	0xf2, 0x00,		/* Disable all outputs */
	0x33, 0x0d,		/* Luma : VIN2, Chroma : CIN
				 * (clamp off) */
	0xd8, 0xa8,		/* HREF/VREF active high, VREF
				 * pulse = 2, Odd/Even flag */
	0x20, 0x03,		/* IF compensation 0dB/oct */
	0xe0, 0xff,		/* Open up all comparators */
	0xe1, 0x00,
	0xe2, 0x7f,
	0xe3, 0x80,
	0xe4, 0x7f,
	0xe5, 0x80,
	0xe6, 0x00,		/* Brightness set to 0 */
	0xe7, 0xe0,		/* Contrast to 1.0, noise shaping
				 * 10 to 8 2-bit error diffusion */
	0xe8, 0xf8,		/* YUV422, CbCr binary offset,
				 * ... (p.32) */
	0xea, 0x18,		/* LLC2 connected, output FIFO
				 * reset with VACTintern */
	0xf0, 0x8a,		/* Half full level to 10, bus
				 * shuffler [7:0, 23:16, 15:8] */
	0xf1, 0x18,		/* Single clock, sync mode, no
				 * FE delay, no HLEN counter */
	0xf8, 0x12,		/* Port A, PIXCLK, HF# & FE#
				 * strength to 2 */
	0xf9, 0x24,		/* Port B, HREF, VREF, PREF &
				 * ALPHA strength to 4 */
};

static const unsigned short init_fp[] = {
	0x59, 0,
	0xa0, 2070,		/* ACC reference */
	0xa3, 0,
	0xa4, 0,
	0xa8, 30,
	0xb2, 768,
	0xbe, 27,
	0x58, 0,
	0x26, 0,
	0x4b, 0x298,		/* PLL gain */
};

static void
vpx3220_dump_i2c (struct i2c_client *client)
{
	int len = sizeof(init_common);
	const unsigned char *data = init_common;

	while (len > 1) {
		dprintk(1,
			KERN_DEBUG "vpx3216b i2c reg 0x%02x data 0x%02x\n",
			*data, vpx3220_read(client, *data));
		data += 2;
		len -= 2;
	}
}

static int
vpx3220_command (struct i2c_client *client,
		 unsigned int       cmd,
		 void              *arg)
{
	struct vpx3220 *decoder = i2c_get_clientdata(client);

	switch (cmd) {
	case 0:
	{
		vpx3220_write_block(client, init_common,
				    sizeof(init_common));
		vpx3220_write_fp_block(client, init_fp,
				       sizeof(init_fp) >> 1);
		switch (decoder->norm) {
			
		case VIDEO_MODE_NTSC:
			vpx3220_write_fp_block(client, init_ntsc,
					       sizeof(init_ntsc) >> 1);
			break;

		case VIDEO_MODE_PAL:
			vpx3220_write_fp_block(client, init_pal,
				       	       sizeof(init_pal) >> 1);
			break;
		case VIDEO_MODE_SECAM:
			vpx3220_write_fp_block(client, init_secam,
					       sizeof(init_secam) >> 1);
			break;
		default:
			vpx3220_write_fp_block(client, init_pal,
				       	       sizeof(init_pal) >> 1);
			break;
		}
	}		
		break;

	case DECODER_DUMP:
	{
		vpx3220_dump_i2c(client);
	}
		break;

	case DECODER_GET_CAPABILITIES:
	{
		struct video_decoder_capability *cap = arg;

		dprintk(1, KERN_DEBUG "%s: DECODER_GET_CAPABILITIES\n",
			I2C_NAME(client));

		cap->flags = VIDEO_DECODER_PAL |
			     VIDEO_DECODER_NTSC |
			     VIDEO_DECODER_SECAM |
			     VIDEO_DECODER_AUTO |
			     VIDEO_DECODER_CCIR;
		cap->inputs = 3;
		cap->outputs = 1;
	}
		break;

	case DECODER_GET_STATUS:
	{
		int res = 0, status;

		dprintk(1, KERN_INFO "%s: DECODER_GET_STATUS\n",
			I2C_NAME(client));

		status = vpx3220_fp_read(client, 0x0f3);

		dprintk(1, KERN_INFO "%s: status: 0x%04x\n", I2C_NAME(client),
			status);

		if (status < 0)
			return status;

		if ((status & 0x20) == 0) {
			res |= DECODER_STATUS_GOOD | DECODER_STATUS_COLOR;

			switch (status & 0x18) {

			case 0x00:
			case 0x10:
			case 0x14:
			case 0x18:
				res |= DECODER_STATUS_PAL;
				break;

			case 0x08:
				res |= DECODER_STATUS_SECAM;
				break;

			case 0x04:
			case 0x0c:
			case 0x1c:
				res |= DECODER_STATUS_NTSC;
				break;
			}
		}

		*(int *) arg = res;
	}
		break;

	case DECODER_SET_NORM:
	{
		int *iarg = arg, data;
		int temp_input;

		/* Here we back up the input selection because it gets
		   overwritten when we fill the registers with the
                   choosen video norm */
		temp_input = vpx3220_fp_read(client, 0xf2);

		dprintk(1, KERN_DEBUG "%s: DECODER_SET_NORM %d\n",
			I2C_NAME(client), *iarg);
		switch (*iarg) {

		case VIDEO_MODE_NTSC:
			vpx3220_write_fp_block(client, init_ntsc,
					       sizeof(init_ntsc) >> 1);
			dprintk(1, KERN_INFO "%s: norm switched to NTSC\n",
				I2C_NAME(client));
			break;

		case VIDEO_MODE_PAL:
			vpx3220_write_fp_block(client, init_pal,
					       sizeof(init_pal) >> 1);
			dprintk(1, KERN_INFO "%s: norm switched to PAL\n",
				I2C_NAME(client));
			break;

		case VIDEO_MODE_SECAM:
			vpx3220_write_fp_block(client, init_secam,
					       sizeof(init_secam) >> 1);
			dprintk(1, KERN_INFO "%s: norm switched to SECAM\n",
				I2C_NAME(client));
			break;

		case VIDEO_MODE_AUTO:
			/* FIXME This is only preliminary support */
			data = vpx3220_fp_read(client, 0xf2) & 0x20;
			vpx3220_fp_write(client, 0xf2, 0x00c0 | data);
			dprintk(1, KERN_INFO "%s: norm switched to Auto\n",
				I2C_NAME(client));
			break;

		default:
			return -EINVAL;

		}
		decoder->norm = *iarg;

		/* And here we set the backed up video input again */
		vpx3220_fp_write(client, 0xf2, temp_input | 0x0010);
		udelay(10);
	}
		break;

	case DECODER_SET_INPUT:
	{
		int *iarg = arg, data;

		/* RJ:  *iarg = 0: ST8 (PCTV) input
		 *iarg = 1: COMPOSITE  input
		 *iarg = 2: SVHS       input  */

		const int input[3][2] = {
			{0x0c, 0},
			{0x0d, 0},
			{0x0e, 1}
		};

		if (*iarg < 0 || *iarg > 2)
			return -EINVAL;

		dprintk(1, KERN_INFO "%s: input switched to %s\n",
			I2C_NAME(client), inputs[*iarg]);

		vpx3220_write(client, 0x33, input[*iarg][0]);

		data = vpx3220_fp_read(client, 0xf2) & ~(0x0020);
		if (data < 0)
			return data;
		/* 0x0010 is required to latch the setting */
		vpx3220_fp_write(client, 0xf2,
				 data | (input[*iarg][1] << 5) | 0x0010);

		udelay(10);
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

		dprintk(1, KERN_DEBUG "%s: DECODER_ENABLE_OUTPUT %d\n",
			I2C_NAME(client), *iarg);

		vpx3220_write(client, 0xf2, (*iarg ? 0x1b : 0x00));
	}
		break;

	case DECODER_SET_PICTURE:
	{
		struct video_picture *pic = arg;

		if (decoder->bright != pic->brightness) {
			/* We want -128 to 128 we get 0-65535 */
			decoder->bright = pic->brightness;
			vpx3220_write(client, 0xe6,
				      (decoder->bright - 32768) >> 8);
		}
		if (decoder->contrast != pic->contrast) {
			/* We want 0 to 64 we get 0-65535 */
			/* Bit 7 and 8 is for noise shaping */
			decoder->contrast = pic->contrast;
			vpx3220_write(client, 0xe7,
				      (decoder->contrast >> 10) + 192);
		}
		if (decoder->sat != pic->colour) {
			/* We want 0 to 4096 we get 0-65535 */
			decoder->sat = pic->colour;
			vpx3220_fp_write(client, 0xa0,
					 decoder->sat >> 4);
		}
		if (decoder->hue != pic->hue) {
			/* We want -512 to 512 we get 0-65535 */
			decoder->hue = pic->hue;
			vpx3220_fp_write(client, 0x1c,
					 ((decoder->hue - 32768) >> 6) & 0xFFF);
		}
	}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int
vpx3220_init_client (struct i2c_client *client)
{
	vpx3220_write_block(client, init_common, sizeof(init_common));
	vpx3220_write_fp_block(client, init_fp, sizeof(init_fp) >> 1);
	/* Default to PAL */
	vpx3220_write_fp_block(client, init_pal, sizeof(init_pal) >> 1);

	return 0;
}

/* -----------------------------------------------------------------------
 * Client managment code
 */

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */
static unsigned short normal_i2c[] =
    { I2C_VPX3220 >> 1, (I2C_VPX3220 >> 1) + 4,
	I2C_CLIENT_END
};

static unsigned short ignore = I2C_CLIENT_END;
                                                                                
static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_i2c,
	.probe			= &ignore,
	.ignore			= &ignore,
};

static struct i2c_driver vpx3220_i2c_driver;

static int
vpx3220_detach_client (struct i2c_client *client)
{
	struct vpx3220 *decoder = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(decoder);
	kfree(client);

	return 0;
}

static int
vpx3220_detect_client (struct i2c_adapter *adapter,
		       int                 address,
		       int                 kind)
{
	int err;
	struct i2c_client *client;
	struct vpx3220 *decoder;

	dprintk(1, VPX3220_DEBUG "%s\n", __func__);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality
	    (adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL) {
		return -ENOMEM;
	}

	client->addr = address;
	client->adapter = adapter;
	client->driver = &vpx3220_i2c_driver;

	/* Check for manufacture ID and part number */
	if (kind < 0) {
		u8 id;
		u16 pn;

		id = vpx3220_read(client, 0x00);
		if (id != 0xec) {
			dprintk(1,
				KERN_INFO
				"vpx3220_attach: Wrong manufacturer ID (0x%02x)\n",
				id);
			kfree(client);
			return 0;
		}

		pn = (vpx3220_read(client, 0x02) << 8) +
		    vpx3220_read(client, 0x01);
		switch (pn) {
		case 0x4680:
			strlcpy(I2C_NAME(client), "vpx3220a",
				sizeof(I2C_NAME(client)));
			break;
		case 0x4260:
			strlcpy(I2C_NAME(client), "vpx3216b",
				sizeof(I2C_NAME(client)));
			break;
		case 0x4280:
			strlcpy(I2C_NAME(client), "vpx3214c",
				sizeof(I2C_NAME(client)));
			break;
		default:
			dprintk(1,
				KERN_INFO 
				"%s: Wrong part number (0x%04x)\n",
				__func__, pn);
			kfree(client);
			return 0;
		}
	} else {
		strlcpy(I2C_NAME(client), "forced vpx32xx",
			sizeof(I2C_NAME(client)));
	}

	decoder = kzalloc(sizeof(struct vpx3220), GFP_KERNEL);
	if (decoder == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	decoder->norm = VIDEO_MODE_PAL;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;
	i2c_set_clientdata(client, decoder);

	err = i2c_attach_client(client);
	if (err) {
		kfree(client);
		kfree(decoder);
		return err;
	}

	dprintk(1, KERN_INFO "%s: vpx32xx client found at address 0x%02x\n",
		I2C_NAME(client), client->addr << 1);

	vpx3220_init_client(client);

	return 0;
}

static int
vpx3220_attach_adapter (struct i2c_adapter *adapter)
{
	int ret;

	ret = i2c_probe(adapter, &addr_data, &vpx3220_detect_client);
	dprintk(1, VPX3220_DEBUG "%s: i2c_probe returned %d\n",
		__func__, ret);
	return ret;
}

/* -----------------------------------------------------------------------
 * Driver initialization and cleanup code
 */

static struct i2c_driver vpx3220_i2c_driver = {
	.driver = {
		.name = "vpx3220",
	},

	.id = I2C_DRIVERID_VPX3220,

	.attach_adapter = vpx3220_attach_adapter,
	.detach_client = vpx3220_detach_client,
	.command = vpx3220_command,
};

static int __init
vpx3220_init (void)
{
	return i2c_add_driver(&vpx3220_i2c_driver);
}

static void __exit
vpx3220_cleanup (void)
{
	i2c_del_driver(&vpx3220_i2c_driver);
}

module_init(vpx3220_init);
module_exit(vpx3220_cleanup);

MODULE_DESCRIPTION("vpx3220a/vpx3216b/vpx3214c video encoder driver");
MODULE_AUTHOR("Laurent Pinchart");
MODULE_LICENSE("GPL");
