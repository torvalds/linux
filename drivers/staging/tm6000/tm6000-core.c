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

	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00e2, 0x0c);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00e8, 0xff);
	tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0x00eb, 0x08);
	msleep(50);

	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x00);
	msleep(50);
	tm6000_set_reg (dev, REQ_04_EN_DISABLE_MCU_INT, 0x0020, 0x01);
	msleep(50);
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

#endif /* HACK */

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
