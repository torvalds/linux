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
	if (dev->dev_type == TM6010) {
		if (dev->fourcc == V4L2_PIX_FMT_UYVY)
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0xfc);
		else
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0xfd);
	} else {
		if (dev->fourcc == V4L2_PIX_FMT_UYVY)
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0xd0);
		else
			tm6000_set_reg (dev, REQ_07_SET_GET_AVREG, 0xc1, 0x90);
	}
}

int tm6000_init_analog_mode (struct tm6000_core *dev)
{
	if (dev->dev_type == TM6010) {
		int val;

		/* Enable video */
		val = tm6000_get_reg(dev, REQ_07_SET_GET_AVREG, 0xcc, 0);
		val |= 0x60;
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xcc, val);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xfe, 0xcf);

	} else {
		/* Enables soft reset */
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0x3f, 0x01);

		if (dev->scaler) {
			tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xc0, 0x20);
		} else {
			/* Enable Hfilter and disable TS Drop err */
			tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xc0, 0x80);
		}

		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xc3, 0x88);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xda, 0x23);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xd1, 0xc0);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xd2, 0xd8);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xd6, 0x06);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xdf, 0x1f);

		/* AP Software reset */
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xff, 0x08);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xff, 0x00);

		tm6000_set_fourcc_format(dev);

		/* Disables soft reset */
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0x3f, 0x00);

		/* E3: Select input 0 - TV tuner */
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xe3, 0x00);
		tm6000_set_reg(dev, REQ_07_SET_GET_AVREG, 0xeb, 0x60);

		/* This controls input */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_2, 0x0);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6000_GPIO_3, 0x01);
	}
	msleep(20);

	/* Tuner firmware can now be loaded */

	/*FIXME: Hack!!! */
	struct v4l2_frequency f;
	mutex_lock(&dev->lock);
	f.frequency=dev->freq;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);
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

struct reg_init {
	u8 req;
	u8 reg;
	u8 val;
};

/* The meaning of those initializations are unknown */
struct reg_init tm6000_init_tab[] = {
	/* REG  VALUE */
	{ REQ_07_SET_GET_AVREG,  0xdf, 0x1f },
	{ REQ_07_SET_GET_AVREG,  0xff, 0x08 },
	{ REQ_07_SET_GET_AVREG,  0xff, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0xd5, 0x4f },
	{ REQ_07_SET_GET_AVREG,  0xda, 0x23 },
	{ REQ_07_SET_GET_AVREG,  0xdb, 0x08 },
	{ REQ_07_SET_GET_AVREG,  0xe2, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0xe3, 0x10 },
	{ REQ_07_SET_GET_AVREG,  0xe5, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0xe8, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0xeb, 0x64 },		/* 48000 bits/sample, external input */
	{ REQ_07_SET_GET_AVREG,  0xee, 0xc2 },
	{ REQ_07_SET_GET_AVREG,  0x3f, 0x01 },		/* Start of soft reset */
	{ REQ_07_SET_GET_AVREG,  0x00, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0x01, 0x07 },
	{ REQ_07_SET_GET_AVREG,  0x02, 0x5f },
	{ REQ_07_SET_GET_AVREG,  0x03, 0x00 },
	{ REQ_07_SET_GET_AVREG,  0x05, 0x64 },
	{ REQ_07_SET_GET_AVREG,  0x07, 0x01 },
	{ REQ_07_SET_GET_AVREG,  0x08, 0x82 },
	{ REQ_07_SET_GET_AVREG,  0x09, 0x36 },
	{ REQ_07_SET_GET_AVREG,  0x0a, 0x50 },
	{ REQ_07_SET_GET_AVREG,  0x0c, 0x6a },
	{ REQ_07_SET_GET_AVREG,  0x11, 0xc9 },
	{ REQ_07_SET_GET_AVREG,  0x12, 0x07 },
	{ REQ_07_SET_GET_AVREG,  0x13, 0x3b },
	{ REQ_07_SET_GET_AVREG,  0x14, 0x47 },
	{ REQ_07_SET_GET_AVREG,  0x15, 0x6f },
	{ REQ_07_SET_GET_AVREG,  0x17, 0xcd },
	{ REQ_07_SET_GET_AVREG,  0x18, 0x1e },
	{ REQ_07_SET_GET_AVREG,  0x19, 0x8b },
	{ REQ_07_SET_GET_AVREG,  0x1a, 0xa2 },
	{ REQ_07_SET_GET_AVREG,  0x1b, 0xe9 },
	{ REQ_07_SET_GET_AVREG,  0x1c, 0x1c },
	{ REQ_07_SET_GET_AVREG,  0x1d, 0xcc },
	{ REQ_07_SET_GET_AVREG,  0x1e, 0xcc },
	{ REQ_07_SET_GET_AVREG,  0x1f, 0xcd },
	{ REQ_07_SET_GET_AVREG,  0x20, 0x3c },
	{ REQ_07_SET_GET_AVREG,  0x21, 0x3c },
	{ REQ_07_SET_GET_AVREG,  0x2d, 0x48 },
	{ REQ_07_SET_GET_AVREG,  0x2e, 0x88 },
	{ REQ_07_SET_GET_AVREG,  0x30, 0x22 },
	{ REQ_07_SET_GET_AVREG,  0x31, 0x61 },
	{ REQ_07_SET_GET_AVREG,  0x32, 0x74 },
	{ REQ_07_SET_GET_AVREG,  0x33, 0x1c },
	{ REQ_07_SET_GET_AVREG,  0x34, 0x74 },
	{ REQ_07_SET_GET_AVREG,  0x35, 0x1c },
	{ REQ_07_SET_GET_AVREG,  0x36, 0x7a },
	{ REQ_07_SET_GET_AVREG,  0x37, 0x26 },
	{ REQ_07_SET_GET_AVREG,  0x38, 0x40 },
	{ REQ_07_SET_GET_AVREG,  0x39, 0x0a },
	{ REQ_07_SET_GET_AVREG,  0x42, 0x55 },
	{ REQ_07_SET_GET_AVREG,  0x51, 0x11 },
	{ REQ_07_SET_GET_AVREG,  0x55, 0x01 },
	{ REQ_07_SET_GET_AVREG,  0x57, 0x02 },
	{ REQ_07_SET_GET_AVREG,  0x58, 0x35 },
	{ REQ_07_SET_GET_AVREG,  0x59, 0xa0 },
	{ REQ_07_SET_GET_AVREG,  0x80, 0x15 },
	{ REQ_07_SET_GET_AVREG,  0x82, 0x42 },
	{ REQ_07_SET_GET_AVREG,  0xc1, 0xd0 },
	{ REQ_07_SET_GET_AVREG,  0xc3, 0x88 },
	{ REQ_07_SET_GET_AVREG,  0x3f, 0x00 },		/* End of the soft reset */
	{ REQ_05_SET_GET_USBREG, 0x18, 0x00 },
};

struct reg_init tm6010_init_tab[] = {
	{ REQ_07_SET_GET_AVREG, 0xc0, 0x00 },
	{ REQ_07_SET_GET_AVREG, 0xc4, 0xa0 },
	{ REQ_07_SET_GET_AVREG, 0xc6, 0x40 },
	{ REQ_07_SET_GET_AVREG, 0xca, 0x31 },
	{ REQ_07_SET_GET_AVREG, 0xcc, 0xe1 },
	{ REQ_07_SET_GET_AVREG, 0xe0, 0x03 },
	{ REQ_07_SET_GET_AVREG, 0xfe, 0x7f },

	{ REQ_08_SET_GET_AVREG_BIT, 0xe2, 0xf0 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xe3, 0xf4 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xe4, 0xf8 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xe6, 0x00 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xea, 0xf2 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xeb, 0xf0 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xec, 0xc2 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xf0, 0x60 },
	{ REQ_08_SET_GET_AVREG_BIT, 0xf1, 0xfc },

	{ REQ_07_SET_GET_AVREG, 0x3f, 0x01 },
	{ REQ_07_SET_GET_AVREG, 0x00, 0x00 },
	{ REQ_07_SET_GET_AVREG, 0x01, 0x07 },
	{ REQ_07_SET_GET_AVREG, 0x02, 0x5f },
	{ REQ_07_SET_GET_AVREG, 0x03, 0x00 },
	{ REQ_07_SET_GET_AVREG, 0x05, 0x64 },
	{ REQ_07_SET_GET_AVREG, 0x07, 0x01 },
	{ REQ_07_SET_GET_AVREG, 0x08, 0x82 },
	{ REQ_07_SET_GET_AVREG, 0x09, 0x36 },
	{ REQ_07_SET_GET_AVREG, 0x0a, 0x50 },
	{ REQ_07_SET_GET_AVREG, 0x0c, 0x6a },
	{ REQ_07_SET_GET_AVREG, 0x11, 0xc9 },
	{ REQ_07_SET_GET_AVREG, 0x12, 0x07 },
	{ REQ_07_SET_GET_AVREG, 0x13, 0x3b },
	{ REQ_07_SET_GET_AVREG, 0x14, 0x47 },
	{ REQ_07_SET_GET_AVREG, 0x15, 0x6f },
	{ REQ_07_SET_GET_AVREG, 0x17, 0xcd },
	{ REQ_07_SET_GET_AVREG, 0x18, 0x1e },
	{ REQ_07_SET_GET_AVREG, 0x19, 0x8b },
	{ REQ_07_SET_GET_AVREG, 0x1a, 0xa2 },
	{ REQ_07_SET_GET_AVREG, 0x1b, 0xe9 },
	{ REQ_07_SET_GET_AVREG, 0x1c, 0x1c },
	{ REQ_07_SET_GET_AVREG, 0x1d, 0xcc },
	{ REQ_07_SET_GET_AVREG, 0x1e, 0xcc },
	{ REQ_07_SET_GET_AVREG, 0x1f, 0xcd },
	{ REQ_07_SET_GET_AVREG, 0x20, 0x3c },
	{ REQ_07_SET_GET_AVREG, 0x21, 0x3c },
	{ REQ_07_SET_GET_AVREG, 0x2d, 0x48 },
	{ REQ_07_SET_GET_AVREG, 0x2e, 0x88 },
	{ REQ_07_SET_GET_AVREG, 0x30, 0x22 },
	{ REQ_07_SET_GET_AVREG, 0x31, 0x61 },
	{ REQ_07_SET_GET_AVREG, 0x32, 0x74 },
	{ REQ_07_SET_GET_AVREG, 0x33, 0x1c },
	{ REQ_07_SET_GET_AVREG, 0x34, 0x74 },
	{ REQ_07_SET_GET_AVREG, 0x35, 0x1c },
	{ REQ_07_SET_GET_AVREG, 0x36, 0x7a },
	{ REQ_07_SET_GET_AVREG, 0x37, 0x26 },
	{ REQ_07_SET_GET_AVREG, 0x38, 0x40 },
	{ REQ_07_SET_GET_AVREG, 0x39, 0x0a },
	{ REQ_07_SET_GET_AVREG, 0x42, 0x55 },
	{ REQ_07_SET_GET_AVREG, 0x51, 0x11 },
	{ REQ_07_SET_GET_AVREG, 0x55, 0x01 },
	{ REQ_07_SET_GET_AVREG, 0x57, 0x02 },
	{ REQ_07_SET_GET_AVREG, 0x58, 0x35 },
	{ REQ_07_SET_GET_AVREG, 0x59, 0xa0 },
	{ REQ_07_SET_GET_AVREG, 0x80, 0x15 },
	{ REQ_07_SET_GET_AVREG, 0x82, 0x42 },
	{ REQ_07_SET_GET_AVREG, 0xc1, 0xd0 },
	{ REQ_07_SET_GET_AVREG, 0xc3, 0x88 },
	{ REQ_07_SET_GET_AVREG, 0x3f, 0x00 },

	{ REQ_05_SET_GET_USBREG, 0x18, 0x00 },

	/* set remote wakeup key:any key wakeup */
	{ REQ_07_SET_GET_AVREG,  0xe5,  0xfe },
	{ REQ_07_SET_GET_AVREG,  0xda,  0xff },
};

int tm6000_init (struct tm6000_core *dev)
{
	int board, rc=0, i, size;
	struct reg_init *tab;

	if (dev->dev_type == TM6010) {
		tab = tm6010_init_tab;
		size = ARRAY_SIZE(tm6010_init_tab);
	} else {
		tab = tm6000_init_tab;
		size = ARRAY_SIZE(tm6000_init_tab);
	}

	/* Load board's initialization table */
	for (i=0; i< size; i++) {
		rc= tm6000_set_reg (dev, tab[i].req, tab[i].reg, tab[i].val);
		if (rc<0) {
			printk (KERN_ERR "Error %i while setting req %d, "
					 "reg %d to value %d\n", rc,
					 tab[i].req,tab[i].reg, tab[i].val);
			return rc;
		}
	}

	msleep(5); /* Just to be conservative */

	/* Check board version - maybe 10Moons specific */
	board=tm6000_get_reg16 (dev, 0x40, 0, 0);
	if (board >=0) {
		printk (KERN_INFO "Board version = 0x%04x\n",board);
	} else {
		printk (KERN_ERR "Error %i while retrieving board version\n",board);
	}

	if (dev->dev_type == TM6010) {
		/* Turn xceive 3028 on */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, TM6010_GPIO_3, 0x01);
		msleep(11);
	}

	/* Reset GPIO1 and GPIO4. */
	for (i=0; i< 2; i++) {
		rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					dev->tuner_reset_gpio, 0x00);
		if (rc<0) {
			printk (KERN_ERR "Error %i doing GPIO1 reset\n",rc);
			return rc;
		}

		msleep(10); /* Just to be conservative */
		rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					dev->tuner_reset_gpio, 0x01);
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

		if (!i) {
			rc=tm6000_get_reg16(dev, 0x40,0,0);
			if (rc>=0) {
				printk ("board=%d\n", rc);
			}
		}
	}

	msleep(50);

	return 0;
}

int tm6000_set_audio_bitrate(struct tm6000_core *dev, int bitrate)
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
EXPORT_SYMBOL_GPL(tm6000_set_audio_bitrate);
