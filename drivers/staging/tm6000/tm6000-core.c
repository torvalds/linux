/*
   tm6000-core.c - driver for TM5600/TM6000 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>
       - DVB-T support

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/video_decoder.h>
#include "tm6000.h"
#include "tm6000-regs.h"
#include <media/v4l2-common.h>
#include <media/tuner.h>

#ifdef HACK /* HACK */
#include "tm6000-hack.c"
#endif

#define USB_TIMEOUT	5*HZ /* ms */

int tm6000_read_write_usb (struct tm6000_core *dev, u8 req_type, u8 req,
			   u16 value, u16 index, u8 *buf, u16 len)
{
	int          ret, i;
	unsigned int pipe;
	static int   ini=0, last=0, n=0;
	u8	     *data=NULL;

	if (len)
		data = kzalloc(len, GFP_KERNEL);


	if (req_type & USB_DIR_IN)
		pipe=usb_rcvctrlpipe(dev->udev, 0);
	else {
		pipe=usb_sndctrlpipe(dev->udev, 0);
		memcpy(data, buf, len);
	}

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		if (!ini)
			last=ini=jiffies;

		printk("%06i (dev %p, pipe %08x): ", n, dev->udev, pipe);

		printk( "%s: %06u ms %06u ms %02x %02x %02x %02x %02x %02x %02x %02x ",
			(req_type & USB_DIR_IN)?" IN":"OUT",
			jiffies_to_msecs(jiffies-last),
			jiffies_to_msecs(jiffies-ini),
			req_type, req,value&0xff,value>>8, index&0xff, index>>8,
			len&0xff, len>>8);
		last=jiffies;
		n++;

		if ( !(req_type & USB_DIR_IN) ) {
			printk(">>> ");
			for (i=0;i<len;i++) {
				printk(" %02x",buf[i]);
			}
			printk("\n");
		}
	}

	ret = usb_control_msg(dev->udev, pipe, req, req_type, value, index, data,
			      len, USB_TIMEOUT);

	if (req_type &  USB_DIR_IN)
		memcpy(buf, data, len);

	if (tm6000_debug & V4L2_DEBUG_I2C) {
		if (ret<0) {
			if (req_type &  USB_DIR_IN)
				printk("<<< (len=%d)\n",len);

			printk("%s: Error #%d\n", __FUNCTION__, ret);
		} else if (req_type &  USB_DIR_IN) {
			printk("<<< ");
			for (i=0;i<len;i++) {
				printk(" %02x",buf[i]);
			}
			printk("\n");
		}
	}

	kfree(data);

	msleep(5);

	return ret;
}

int tm6000_set_reg (struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	return
		tm6000_read_write_usb (dev, USB_DIR_OUT | USB_TYPE_VENDOR,
				       req, value, index, NULL, 0);
}

int tm6000_get_reg (struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	int rc;
	u8 buf[1];

	rc=tm6000_read_write_usb (dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
				       value, index, buf, 1);

	if (rc<0)
		return rc;

	return *buf;
}

int tm6000_get_reg16 (struct tm6000_core *dev, u8 req, u16 value, u16 index)
{
	int rc;
	u8 buf[2];

	rc=tm6000_read_write_usb (dev, USB_DIR_IN | USB_TYPE_VENDOR, req,
				       value, index, buf, 2);

	if (rc<0)
		return rc;

	return buf[1]|buf[0]<<8;
}

void tm6000_set_fourcc_format(struct tm6000_core *dev)
{
	if (dev->fourcc==V4L2_PIX_FMT_UYVY) {
		/* Sets driver to UYUV */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0xd0);
	} else {
		/* Sets driver to YUV2 */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0x90);
	}
}

int tm6000_init_analog_mode (struct tm6000_core *dev)
{

	/* Enables soft reset */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x01);

	if (dev->scaler) {
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc0, 0x20);
	} else {
		/* Enable Hfilter and disable TS Drop err */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc0, 0x80);
	}
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc3, 0x88);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xda, 0x23);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd1, 0xc0);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd2, 0xd8);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd6, 0x06);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xdf, 0x1f);

	/* AP Software reset */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xff, 0x08);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xff, 0x00);

	tm6000_set_fourcc_format(dev);

	/* Disables soft reset */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x00);

	/* E3: Select input 0 - TV tuner */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x00);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);

	/* Tuner firmware can now be loaded */

	tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_1, 0x00);
	msleep(11);

	/* This controls input */
	tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_2, 0x0);
	tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_3, 0x01);
	msleep(20);

	/*FIXME: Hack!!! */
	struct v4l2_frequency f;
	mutex_lock(&dev->lock);
	f.frequency=dev->freq;
	tm6000_i2c_call_clients(dev,VIDIOC_S_FREQUENCY,&f);
	mutex_unlock(&dev->lock);

	msleep(100);
	tm6000_set_standard (dev, &dev->norm);
	tm6000_set_audio_bitrate (dev,48000);


	return 0;
}

int tm6000_init_digital_mode (struct tm6000_core *dev)
{
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00ff, 0x08);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00ff, 0x00);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x003f, 0x01);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00df, 0x08);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00e2, 0x0c);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00e8, 0xff);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00eb, 0xd8);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00c0, 0x40);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00c1, 0xd0);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00c3, 0x09);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00da, 0x37);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00d1, 0xd8);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00d2, 0xc0);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00d6, 0x60);
	msleep(50);

	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x00);
	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x01);
	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x00);

	msleep(100);

	return 0;
}

/* The meaning of those initializations are unknown */
u8 init_tab[][2] = {
	/* REG  VALUE */
	{ 0xdf, 0x1f },
	{ 0xff, 0x08 },
	{ 0xff, 0x00 },
	{ 0xd5, 0x4f },
	{ 0xda, 0x23 },
	{ 0xdb, 0x08 },
	{ 0xe2, 0x00 },
	{ 0xe3, 0x10 },
	{ 0xe5, 0x00 },
	{ 0xe8, 0x00 },
	{ 0xeb, 0x64 },		/* 48000 bits/sample, external input */
	{ 0xee, 0xc2 },
	{ 0x3f, 0x01 },		/* Start of soft reset */
	{ 0x00, 0x00 },
	{ 0x01, 0x07 },
	{ 0x02, 0x5f },
	{ 0x03, 0x00 },
	{ 0x05, 0x64 },
	{ 0x07, 0x01 },
	{ 0x08, 0x82 },
	{ 0x09, 0x36 },
	{ 0x0a, 0x50 },
	{ 0x0c, 0x6a },
	{ 0x11, 0xc9 },
	{ 0x12, 0x07 },
	{ 0x13, 0x3b },
	{ 0x14, 0x47 },
	{ 0x15, 0x6f },
	{ 0x17, 0xcd },
	{ 0x18, 0x1e },
	{ 0x19, 0x8b },
	{ 0x1a, 0xa2 },
	{ 0x1b, 0xe9 },
	{ 0x1c, 0x1c },
	{ 0x1d, 0xcc },
	{ 0x1e, 0xcc },
	{ 0x1f, 0xcd },
	{ 0x20, 0x3c },
	{ 0x21, 0x3c },
	{ 0x2d, 0x48 },
	{ 0x2e, 0x88 },
	{ 0x30, 0x22 },
	{ 0x31, 0x61 },
	{ 0x32, 0x74 },
	{ 0x33, 0x1c },
	{ 0x34, 0x74 },
	{ 0x35, 0x1c },
	{ 0x36, 0x7a },
	{ 0x37, 0x26 },
	{ 0x38, 0x40 },
	{ 0x39, 0x0a },
	{ 0x42, 0x55 },
	{ 0x51, 0x11 },
	{ 0x55, 0x01 },
	{ 0x57, 0x02 },
	{ 0x58, 0x35 },
	{ 0x59, 0xa0 },
	{ 0x80, 0x15 },
	{ 0x82, 0x42 },
	{ 0xc1, 0xd0 },
	{ 0xc3, 0x88 },
	{ 0x3f, 0x00 },		/* End of the soft reset */
};

int tm6000_init (struct tm6000_core *dev)
{
	int board, rc=0, i;

#ifdef HACK /* HACK */
	init_tm6000(dev);
	return 0;
#else

	/* Load board's initialization table */
	for (i=0; i< ARRAY_SIZE(init_tab); i++) {
		rc= tm6000_set_reg (dev, REQ_07_SET_GET_AVREG,
			init_tab[i][0],init_tab[i][1]);
		if (rc<0) {
			printk (KERN_ERR "Error %i while setting reg %d to value %d\n",
			rc, init_tab[i][0],init_tab[i][1]);
			return rc;
		}
	}

	/* Check board version - maybe 10Moons specific */
	board=tm6000_get_reg16 (dev, 0x40, 0, 0);
	if (board >=0) {
		printk (KERN_INFO "Board version = 0x%04x\n",board);
	} else {
		printk (KERN_ERR "Error %i while retrieving board version\n",board);
	}

	tm6000_set_reg (dev, REQ_05_SET_GET_USBREG, 0x18, 0x00);
	msleep(5); /* Just to be conservative */

	/* Reset GPIO1 and GPIO4. */
	for (i=0; i< 2; i++) {
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_1, 0);
		if (rc<0) {
			printk (KERN_ERR "Error %i doing GPIO1 reset\n",rc);
			return rc;
		}

		msleep(10); /* Just to be conservative */
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_1, 1);
		if (rc<0) {
			printk (KERN_ERR "Error %i doing GPIO1 reset\n",rc);
			return rc;
		}

		msleep(10);
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_4, 0);
		if (rc<0) {
			printk (KERN_ERR "Error %i doing GPIO4 reset\n",rc);
			return rc;
		}

		msleep(10);
		rc=tm6000_set_reg (dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_4, 1);
		if (rc<0) {
			printk (KERN_ERR "Error %i doing GPIO4 reset\n",rc);
			return rc;
		}

		if (!i)
			rc=tm6000_get_reg16(dev, 0x40,0,0);
	}

	msleep(50);

	return 0;

#endif /* HACK */
}

#define tm6000_wrt(dev,req,reg,val, data...)				\
	{ const static u8 _val[] = data;				\
	tm6000_read_write_usb(dev,USB_DIR_OUT | USB_TYPE_VENDOR,	\
	req,reg, val, (u8 *) _val, ARRAY_SIZE(_val));			\
	}

/*
TM5600/6000 register values to set video standards.
	There's an adjust, common to all, for composite video
	Additional adjustments are required for S-Video, based on std.

      Standards values for TV             S-Video Changes
REG   PAL   PAL_M PAL_N SECAM NTSC  Comp. PAL  PAL_M PAL_N SECAM NTSC
0xdf  0x1f  0x1f  0x1f  0x1f  0x1f
0xe2  0x00  0x00  0x00  0x00  0x00
0xe8  0x0f  0x0f  0x0f  0x0f  0x0f        0x00 0x00  0x00  0x00  0x00
0xeb  0x60  0x60  0x60  0x60  0x60  0x64  0x64 0x64  0x64  0x64  0x64
0xd5  0x5f  0x5f  0x5f  0x4f  0x4f        0x4f 0x4f  0x4f  0x4f  0x4f
0xe3  0x00  0x00  0x00  0x00  0x00  0x10  0x10 0x10  0x10  0x10  0x10
0xe5  0x00  0x00  0x00  0x00  0x00        0x10 0x10  0x10  0x10  0x10
0x3f  0x01  0x01  0x01  0x01  0x01
0x00  0x32  0x04  0x36  0x38  0x00        0x33 0x05  0x37  0x39  0x01
0x01  0x0e  0x0e  0x0e  0x0e  0x0f
0x02  0x5f  0x5f  0x5f  0x5f  0x5f
0x03  0x02  0x00  0x02  0x02  0x00        0x04 0x04  0x04  0x03  0x03
0x07  0x01  0x01  0x01  0x01  0x01        0x00                   0x00
0x17  0xcd  0xcd  0xcd  0xcd  0xcd                               0x8b
0x18  0x25  0x1e  0x1e  0x24  0x1e
0x19  0xd5  0x83  0x91  0x92  0x8b
0x1a  0x63  0x0a  0x1f  0xe8  0xa2
0x1b  0x50  0xe0  0x0c  0xed  0xe9
0x1c  0x1c  0x1c  0x1c  0x1c  0x1c
0x1d  0xcc  0xcc  0xcc  0xcc  0xcc
0x1e  0xcc  0xcc  0xcc  0xcc  0xcc
0x1f  0xcd  0xcd  0xcd  0xcd  0xcd
0x2e  0x8c  0x88  0x8c  0x8c  0x88                   0x88
0x30  0x2c  0x20  0x2c  0x2c  0x22        0x2a 0x22  0x22  0x2a
0x31  0xc1  0x61  0xc1  0xc1  0x61
0x33  0x0c  0x0c  0x0c  0x2c  0x1c
0x35  0x1c  0x1c  0x1c  0x18  0x1c
0x82  0x52  0x52  0x52  0x42  0x42
0x04  0xdc  0xdc  0xdc        0xdd
0x0d  0x07  0x07  0x07  0x87  0x07
0x3f  0x00  0x00  0x00  0x00  0x00
*/


void tm6000_get_std_res(struct tm6000_core *dev)
{
	/* Currently, those are the only supported resoltions */
	if (dev->norm & V4L2_STD_525_60) {
		dev->height=480;
	} else {
		dev->height=576;
	}
	dev->width=720;

printk("tm6000: res= %dx%d\n",dev->width,dev->height);
}

int tm6000_set_standard (struct tm6000_core *dev, v4l2_std_id *norm)
{
	dev->norm=*norm;
	tm6000_get_std_res(dev);

	/* HACK: Should use, instead, the common code!!! */
	if (*norm & V4L2_STD_PAL_M) {
printk("calling PAL/M hack\n");
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xdf, 0x1f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe2, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe8, 0x0f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd5, 0x5f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe5, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x01);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x04);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x01, 0x0e);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x02, 0x5f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x07, 0x01);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x1e);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0x83);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0x0a);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0xe0);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1c, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1d, 0xcc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1e, 0xcc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1f, 0xcd);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x88);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x20);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x31, 0x61);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x33, 0x0c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x35, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x82, 0x52);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x04, 0xdc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x0d, 0x07);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x00);
		return 0;
	}

	if (*norm & V4L2_STD_PAL) {
printk("calling PAL hack\n");
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xdf, 0x1f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe2, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe8, 0x0f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd5, 0x5f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe5, 0x00);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x01);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x32);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x01, 0x0e);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x02, 0x5f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x02);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x07, 0x01);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x25);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0xd5);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0x63);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0x50);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1c, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1d, 0xcc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1e, 0xcc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1f, 0xcd);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x8c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x31, 0xc1);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x33, 0x0c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x35, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x82, 0x52);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x04, 0xdc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x0d, 0x07);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x00);

		return 0;
	}

	/* */
//	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x02, 0x01);
//	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x02, 0x00);

	/* Set registers common to all standards */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xdf, 0x1f);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe2, 0x00);

	switch (dev->input) {
	case TM6000_INPUT_TV:
		/* Seems to disable ADC2 - needed for TV and RCA */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe8, 0x0f);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);

		if (*norm & V4L2_STD_PAL) {
			/* Enable UV_FLT_EN */
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd5, 0x5f);
		} else {
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd5, 0x4f);
		}

		/* E3: Select input 0 */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x00);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe5, 0x10);

		break;
	case TM6000_INPUT_COMPOSITE:
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x64);
		/* E3: Select input 1 */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x10);
		break;
	case TM6000_INPUT_SVIDEO:
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe8, 0x00);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x64);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xd5, 0x4f);
		/* E3: Select input 1 */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe3, 0x10);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xe5, 0x10);

		break;
	}

	/* Software reset */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x01);

	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x02, 0x5f);

	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x07, 0x01);
//	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x17, 0xcd);

	/* Horizontal Sync DTO = 0x1ccccccd */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1c, 0x1c);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1d, 0xcc);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1e, 0xcc);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1f, 0xcd);

	/* Vertical Height */
	if (*norm & V4L2_STD_525_60) {
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x31, 0x61);
	} else {
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x31, 0xc1);
	}

	/* Horizontal Length */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2f, 640/8);

	if (*norm & V4L2_STD_PAL) {
		/* Common to All PAL Standards */

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x01, 0x0e);

		/* Vsync Hsinc Lockout End */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x33, 0x0c);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x35, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x82, 0x52);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x04, 0xdc);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x0d, 0x07);
		if (*norm & V4L2_STD_PAL_M) {

			/* Chroma DTO */
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x1e);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0x83);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0x0a);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0xe0);

			/* Active Video Horiz Start Time */
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x88);

			if (dev->input==TM6000_INPUT_SVIDEO) {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x05);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x04);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x22);
			} else {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x04);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x00);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x20);
			}
		} else if (*norm & V4L2_STD_PAL_N) {
			/* Chroma DTO */
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x1e);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0x91);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0x1f);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0x0c);

			if (dev->input==TM6000_INPUT_SVIDEO) {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x37);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x04);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x88);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x22);
			} else {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x36);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x02);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x8c);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2c);
			}
		} else {	// Other PAL standards
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x25);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0xd5);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0x63);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0x50);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x8c);

			if (dev->input==TM6000_INPUT_SVIDEO) {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x33);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x04);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2a);

				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2c);
			} else {
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x32);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x02);
				tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2c);
			}
		}
	} if (*norm & V4L2_STD_SECAM) {
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x01, 0x0e);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x24);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0x92);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0xe8);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0xed);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x8c);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x33, 0x2c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x35, 0x18);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x82, 0x42);
		// Register 0x04 is not initialized on SECAM
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x0d, 0x87);

		if (dev->input==TM6000_INPUT_SVIDEO) {
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x39);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x03);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2a);
		} else {
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x38);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x02);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x2c);
		}
	} else {	/* NTSC */
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x01, 0x0f);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x18, 0x1e);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x19, 0x8b);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1a, 0xa2);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x1b, 0xe9);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x2e, 0x88);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x30, 0x22);

		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x33, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x35, 0x1c);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x82, 0x42);
		tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x0d, 0x07);
		if (dev->input==TM6000_INPUT_SVIDEO) {
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x01);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x03);

			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x07, 0x00);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x17, 0x8b);
		} else {
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00, 0x00);
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x03, 0x00);
		}
	}


	/* End of software reset */
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x3f, 0x00);

	msleep(40);

	return 0;
}

int tm6000_set_audio_bitrate (struct tm6000_core *dev, int bitrate)
{
	int val;

	val=tm6000_get_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, 0x0);
printk("Original value=%d\n",val);
	if (val<0)
		return val;

	val &= 0x0f;		/* Preserve the audio input control bits */
	switch (bitrate) {
	case 44100:
		val|=0xd0;
		dev->audio_bitrate=bitrate;
		break;
	case 48000:
		val|=0x60;
		dev->audio_bitrate=bitrate;
		break;
	}
	val=tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xeb, val);

	return val;
}
