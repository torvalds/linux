/******************************************************************************
 * usbtouchscreen.c
 * Driver for USB Touchscreens, supporting those devices:
 *  - eGalax Touchkit
 *    includes eTurboTouch CT-410/510/700
 *  - 3M/Microtouch  EX II series
 *  - ITM
 *  - PanJit TouchSet
 *  - eTurboTouch
 *  - Gunze AHL61
 *  - DMC TSC-10/25
 *  - IRTOUCHSYSTEMS/UNITOP
 *  - IdealTEK URTC1000
 *  - General Touch
 *  - GoTop Super_Q2/GogoPen/PenPower tablets
 *
 * Copyright (C) 2004-2007 by Daniel Ritz <daniel.ritz@gmx.ch>
 * Copyright (C) by Todd E. Johnson (mtouchusb.c)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Driver is based on touchkitusb.c
 * - ITM parts are from itmtouch.c
 * - 3M parts are from mtouchusb.c
 * - PanJit parts are from an unmerged driver by Lanslott Gish
 * - DMC TSC 10/25 are from Holger Schurig, with ideas from an unmerged
 *   driver from Marius Vollmer
 *
 *****************************************************************************/

//#define DEBUG

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>


#define DRIVER_VERSION		"v0.6"
#define DRIVER_AUTHOR		"Daniel Ritz <daniel.ritz@gmx.ch>"
#define DRIVER_DESC		"USB Touchscreen Driver"

static int swap_xy;
module_param(swap_xy, bool, 0644);
MODULE_PARM_DESC(swap_xy, "If set X and Y axes are swapped.");

/* device specifc data/functions */
struct usbtouch_usb;
struct usbtouch_device_info {
	int min_xc, max_xc;
	int min_yc, max_yc;
	int min_press, max_press;
	int rept_size;

	void (*process_pkt) (struct usbtouch_usb *usbtouch, unsigned char *pkt, int len);

	/*
	 * used to get the packet len. possible return values:
	 * > 0: packet len
	 * = 0: skip one byte
	 * < 0: -return value more bytes needed
	 */
	int  (*get_pkt_len) (unsigned char *pkt, int len);

	int  (*read_data)   (struct usbtouch_usb *usbtouch, unsigned char *pkt);
	int  (*init)        (struct usbtouch_usb *usbtouch);
};

/* a usbtouch device */
struct usbtouch_usb {
	unsigned char *data;
	dma_addr_t data_dma;
	unsigned char *buffer;
	int buf_len;
	struct urb *irq;
	struct usb_device *udev;
	struct input_dev *input;
	struct usbtouch_device_info *type;
	char name[128];
	char phys[64];

	int x, y;
	int touch, press;
};


/* device types */
enum {
	DEVTYPE_IGNORE = -1,
	DEVTYPE_EGALAX,
	DEVTYPE_PANJIT,
	DEVTYPE_3M,
	DEVTYPE_ITM,
	DEVTYPE_ETURBO,
	DEVTYPE_GUNZE,
	DEVTYPE_DMC_TSC10,
	DEVTYPE_IRTOUCH,
	DEVTYPE_IDEALTEK,
	DEVTYPE_GENERAL_TOUCH,
	DEVTYPE_GOTOP,
};

#define USB_DEVICE_HID_CLASS(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS \
		| USB_DEVICE_ID_MATCH_DEVICE, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bInterfaceClass = USB_INTERFACE_CLASS_HID, \
	.bInterfaceProtocol = USB_INTERFACE_PROTOCOL_MOUSE

static struct usb_device_id usbtouch_devices[] = {
#ifdef CONFIG_TOUCHSCREEN_USB_EGALAX
	/* ignore the HID capable devices, handled by usbhid */
	{USB_DEVICE_HID_CLASS(0x0eef, 0x0001), .driver_info = DEVTYPE_IGNORE},
	{USB_DEVICE_HID_CLASS(0x0eef, 0x0002), .driver_info = DEVTYPE_IGNORE},

	/* normal device IDs */
	{USB_DEVICE(0x3823, 0x0001), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x3823, 0x0002), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x0123, 0x0001), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x0eef, 0x0001), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x0eef, 0x0002), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x1234, 0x0001), .driver_info = DEVTYPE_EGALAX},
	{USB_DEVICE(0x1234, 0x0002), .driver_info = DEVTYPE_EGALAX},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_PANJIT
	{USB_DEVICE(0x134c, 0x0001), .driver_info = DEVTYPE_PANJIT},
	{USB_DEVICE(0x134c, 0x0002), .driver_info = DEVTYPE_PANJIT},
	{USB_DEVICE(0x134c, 0x0003), .driver_info = DEVTYPE_PANJIT},
	{USB_DEVICE(0x134c, 0x0004), .driver_info = DEVTYPE_PANJIT},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_3M
	{USB_DEVICE(0x0596, 0x0001), .driver_info = DEVTYPE_3M},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ITM
	{USB_DEVICE(0x0403, 0xf9e9), .driver_info = DEVTYPE_ITM},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ETURBO
	{USB_DEVICE(0x1234, 0x5678), .driver_info = DEVTYPE_ETURBO},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GUNZE
	{USB_DEVICE(0x0637, 0x0001), .driver_info = DEVTYPE_GUNZE},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_DMC_TSC10
	{USB_DEVICE(0x0afa, 0x03e8), .driver_info = DEVTYPE_DMC_TSC10},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_IRTOUCH
	{USB_DEVICE(0x595a, 0x0001), .driver_info = DEVTYPE_IRTOUCH},
	{USB_DEVICE(0x6615, 0x0001), .driver_info = DEVTYPE_IRTOUCH},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_IDEALTEK
	{USB_DEVICE(0x1391, 0x1000), .driver_info = DEVTYPE_IDEALTEK},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GENERAL_TOUCH
	{USB_DEVICE(0x0dfc, 0x0001), .driver_info = DEVTYPE_GENERAL_TOUCH},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GOTOP
	{USB_DEVICE(0x08f2, 0x007f), .driver_info = DEVTYPE_GOTOP},
	{USB_DEVICE(0x08f2, 0x00ce), .driver_info = DEVTYPE_GOTOP},
	{USB_DEVICE(0x08f2, 0x00f4), .driver_info = DEVTYPE_GOTOP},
#endif

	{}
};


/*****************************************************************************
 * eGalax part
 */

#ifdef CONFIG_TOUCHSCREEN_USB_EGALAX

#ifndef MULTI_PACKET
#define MULTI_PACKET
#endif

#define EGALAX_PKT_TYPE_MASK		0xFE
#define EGALAX_PKT_TYPE_REPT		0x80
#define EGALAX_PKT_TYPE_DIAG		0x0A

static int egalax_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	if ((pkt[0] & EGALAX_PKT_TYPE_MASK) != EGALAX_PKT_TYPE_REPT)
		return 0;

	dev->x = ((pkt[3] & 0x0F) << 7) | (pkt[4] & 0x7F);
	dev->y = ((pkt[1] & 0x0F) << 7) | (pkt[2] & 0x7F);
	dev->touch = pkt[0] & 0x01;

	return 1;
}

static int egalax_get_pkt_len(unsigned char *buf, int len)
{
	switch (buf[0] & EGALAX_PKT_TYPE_MASK) {
	case EGALAX_PKT_TYPE_REPT:
		return 5;

	case EGALAX_PKT_TYPE_DIAG:
		if (len < 2)
			return -1;

		return buf[1] + 2;
	}

	return 0;
}
#endif


/*****************************************************************************
 * PanJit Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_PANJIT
static int panjit_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[2] & 0x0F) << 8) | pkt[1];
	dev->y = ((pkt[4] & 0x0F) << 8) | pkt[3];
	dev->touch = pkt[0] & 0x01;

	return 1;
}
#endif


/*****************************************************************************
 * 3M/Microtouch Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_3M

#define MTOUCHUSB_ASYNC_REPORT          1
#define MTOUCHUSB_RESET                 7
#define MTOUCHUSB_REQ_CTRLLR_ID         10

static int mtouch_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = (pkt[8] << 8) | pkt[7];
	dev->y = (pkt[10] << 8) | pkt[9];
	dev->touch = (pkt[2] & 0x40) ? 1 : 0;

	return 1;
}

static int mtouch_init(struct usbtouch_usb *usbtouch)
{
	int ret, i;

	ret = usb_control_msg(usbtouch->udev, usb_rcvctrlpipe(usbtouch->udev, 0),
	                      MTOUCHUSB_RESET,
	                      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      1, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	dbg("%s - usb_control_msg - MTOUCHUSB_RESET - bytes|err: %d",
	    __func__, ret);
	if (ret < 0)
		return ret;
	msleep(150);

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(usbtouch->udev, usb_rcvctrlpipe(usbtouch->udev, 0),
				      MTOUCHUSB_ASYNC_REPORT,
				      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				      1, 1, NULL, 0, USB_CTRL_SET_TIMEOUT);
		dbg("%s - usb_control_msg - MTOUCHUSB_ASYNC_REPORT - bytes|err: %d",
		    __func__, ret);
		if (ret >= 0)
			break;
		if (ret != -EPIPE)
			return ret;
	}

	return 0;
}
#endif


/*****************************************************************************
 * ITM Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_ITM
static int itm_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	int touch;
	/*
	 * ITM devices report invalid x/y data if not touched.
	 * if the screen was touched before but is not touched any more
	 * report touch as 0 with the last valid x/y data once. then stop
	 * reporting data until touched again.
	 */
	dev->press = ((pkt[2] & 0x01) << 7) | (pkt[5] & 0x7F);

	touch = ~pkt[7] & 0x20;
	if (!touch) {
		if (dev->touch) {
			dev->touch = 0;
			return 1;
		}

		return 0;
	}

	dev->x = ((pkt[0] & 0x1F) << 7) | (pkt[3] & 0x7F);
	dev->y = ((pkt[1] & 0x1F) << 7) | (pkt[4] & 0x7F);
	dev->touch = touch;

	return 1;
}
#endif


/*****************************************************************************
 * eTurboTouch part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_ETURBO
#ifndef MULTI_PACKET
#define MULTI_PACKET
#endif
static int eturbo_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	unsigned int shift;

	/* packets should start with sync */
	if (!(pkt[0] & 0x80))
		return 0;

	shift = (6 - (pkt[0] & 0x03));
	dev->x = ((pkt[3] << 7) | pkt[4]) >> shift;
	dev->y = ((pkt[1] << 7) | pkt[2]) >> shift;
	dev->touch = (pkt[0] & 0x10) ? 1 : 0;

	return 1;
}

static int eturbo_get_pkt_len(unsigned char *buf, int len)
{
	if (buf[0] & 0x80)
		return 5;
	if (buf[0] == 0x01)
		return 3;
	return 0;
}
#endif


/*****************************************************************************
 * Gunze part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_GUNZE
static int gunze_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	if (!(pkt[0] & 0x80) || ((pkt[1] | pkt[2] | pkt[3]) & 0x80))
		return 0;

	dev->x = ((pkt[0] & 0x1F) << 7) | (pkt[2] & 0x7F);
	dev->y = ((pkt[1] & 0x1F) << 7) | (pkt[3] & 0x7F);
	dev->touch = pkt[0] & 0x20;

	return 1;
}
#endif

/*****************************************************************************
 * DMC TSC-10/25 Part
 *
 * Documentation about the controller and it's protocol can be found at
 *   http://www.dmccoltd.com/files/controler/tsc10usb_pi_e.pdf
 *   http://www.dmccoltd.com/files/controler/tsc25_usb_e.pdf
 */
#ifdef CONFIG_TOUCHSCREEN_USB_DMC_TSC10

/* supported data rates. currently using 130 */
#define TSC10_RATE_POINT	0x50
#define TSC10_RATE_30		0x40
#define TSC10_RATE_50		0x41
#define TSC10_RATE_80		0x42
#define TSC10_RATE_100		0x43
#define TSC10_RATE_130		0x44
#define TSC10_RATE_150		0x45

/* commands */
#define TSC10_CMD_RESET		0x55
#define TSC10_CMD_RATE		0x05
#define TSC10_CMD_DATA1		0x01

static int dmc_tsc10_init(struct usbtouch_usb *usbtouch)
{
	struct usb_device *dev = usbtouch->udev;
	int ret = -ENOMEM;
	unsigned char *buf;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		goto err_nobuf;
	/* reset */
	buf[0] = buf[1] = 0xFF;
	ret = usb_control_msg(dev, usb_rcvctrlpipe (dev, 0),
	                      TSC10_CMD_RESET,
	                      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      0, 0, buf, 2, USB_CTRL_SET_TIMEOUT);
	if (ret < 0)
		goto err_out;
	if (buf[0] != 0x06 || buf[1] != 0x00) {
		ret = -ENODEV;
		goto err_out;
	}

	/* set coordinate output rate */
	buf[0] = buf[1] = 0xFF;
	ret = usb_control_msg(dev, usb_rcvctrlpipe (dev, 0),
	                      TSC10_CMD_RATE,
	                      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      TSC10_RATE_150, 0, buf, 2, USB_CTRL_SET_TIMEOUT);
	if (ret < 0)
		goto err_out;
	if ((buf[0] != 0x06 || buf[1] != 0x00) &&
	    (buf[0] != 0x15 || buf[1] != 0x01)) {
		ret = -ENODEV;
		goto err_out;
	}

	/* start sending data */
	ret = usb_control_msg(dev, usb_rcvctrlpipe (dev, 0),
	                      TSC10_CMD_DATA1,
	                      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      0, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
err_out:
	kfree(buf);
err_nobuf:
	return ret;
}


static int dmc_tsc10_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[2] & 0x03) << 8) | pkt[1];
	dev->y = ((pkt[4] & 0x03) << 8) | pkt[3];
	dev->touch = pkt[0] & 0x01;

	return 1;
}
#endif


/*****************************************************************************
 * IRTOUCH Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_IRTOUCH
static int irtouch_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = (pkt[3] << 8) | pkt[2];
	dev->y = (pkt[5] << 8) | pkt[4];
	dev->touch = (pkt[1] & 0x03) ? 1 : 0;

	return 1;
}
#endif


/*****************************************************************************
 * IdealTEK URTC1000 Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_IDEALTEK
#ifndef MULTI_PACKET
#define MULTI_PACKET
#endif
static int idealtek_get_pkt_len(unsigned char *buf, int len)
{
	if (buf[0] & 0x80)
		return 5;
	if (buf[0] == 0x01)
		return len;
	return 0;
}

static int idealtek_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	switch (pkt[0] & 0x98) {
	case 0x88:
		/* touch data in IdealTEK mode */
		dev->x = (pkt[1] << 5) | (pkt[2] >> 2);
		dev->y = (pkt[3] << 5) | (pkt[4] >> 2);
		dev->touch = (pkt[0] & 0x40) ? 1 : 0;
		return 1;

	case 0x98:
		/* touch data in MT emulation mode */
		dev->x = (pkt[2] << 5) | (pkt[1] >> 2);
		dev->y = (pkt[4] << 5) | (pkt[3] >> 2);
		dev->touch = (pkt[0] & 0x40) ? 1 : 0;
		return 1;

	default:
		return 0;
	}
}
#endif

/*****************************************************************************
 * General Touch Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_GENERAL_TOUCH
static int general_touch_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[2] & 0x0F) << 8) | pkt[1] ;
	dev->y = ((pkt[4] & 0x0F) << 8) | pkt[3] ;
	dev->press = pkt[5] & 0xff;
	dev->touch = pkt[0] & 0x01;

	return 1;
}
#endif

/*****************************************************************************
 * GoTop Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_GOTOP
static int gotop_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[1] & 0x38) << 4) | pkt[2];
	dev->y = ((pkt[1] & 0x07) << 7) | pkt[3];
	dev->touch = pkt[0] & 0x01;
	return 1;
}
#endif


/*****************************************************************************
 * the different device descriptors
 */
#ifdef MULTI_PACKET
static void usbtouch_process_multi(struct usbtouch_usb *usbtouch,
				   unsigned char *pkt, int len);
#endif

static struct usbtouch_device_info usbtouch_dev_info[] = {
#ifdef CONFIG_TOUCHSCREEN_USB_EGALAX
	[DEVTYPE_EGALAX] = {
		.min_xc		= 0x0,
		.max_xc		= 0x07ff,
		.min_yc		= 0x0,
		.max_yc		= 0x07ff,
		.rept_size	= 16,
		.process_pkt	= usbtouch_process_multi,
		.get_pkt_len	= egalax_get_pkt_len,
		.read_data	= egalax_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_PANJIT
	[DEVTYPE_PANJIT] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 8,
		.read_data	= panjit_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_3M
	[DEVTYPE_3M] = {
		.min_xc		= 0x0,
		.max_xc		= 0x4000,
		.min_yc		= 0x0,
		.max_yc		= 0x4000,
		.rept_size	= 11,
		.read_data	= mtouch_read_data,
		.init		= mtouch_init,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ITM
	[DEVTYPE_ITM] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.max_press	= 0xff,
		.rept_size	= 8,
		.read_data	= itm_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ETURBO
	[DEVTYPE_ETURBO] = {
		.min_xc		= 0x0,
		.max_xc		= 0x07ff,
		.min_yc		= 0x0,
		.max_yc		= 0x07ff,
		.rept_size	= 8,
		.process_pkt	= usbtouch_process_multi,
		.get_pkt_len	= eturbo_get_pkt_len,
		.read_data	= eturbo_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GUNZE
	[DEVTYPE_GUNZE] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 4,
		.read_data	= gunze_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_DMC_TSC10
	[DEVTYPE_DMC_TSC10] = {
		.min_xc		= 0x0,
		.max_xc		= 0x03ff,
		.min_yc		= 0x0,
		.max_yc		= 0x03ff,
		.rept_size	= 5,
		.init		= dmc_tsc10_init,
		.read_data	= dmc_tsc10_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_IRTOUCH
	[DEVTYPE_IRTOUCH] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 8,
		.read_data	= irtouch_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_IDEALTEK
	[DEVTYPE_IDEALTEK] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 8,
		.process_pkt	= usbtouch_process_multi,
		.get_pkt_len	= idealtek_get_pkt_len,
		.read_data	= idealtek_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GENERAL_TOUCH
	[DEVTYPE_GENERAL_TOUCH] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0500,
		.min_yc		= 0x0,
		.max_yc		= 0x0500,
		.rept_size	= 7,
		.read_data	= general_touch_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_GOTOP
	[DEVTYPE_GOTOP] = {
		.min_xc		= 0x0,
		.max_xc		= 0x03ff,
		.min_yc		= 0x0,
		.max_yc		= 0x03ff,
		.rept_size	= 4,
		.read_data	= gotop_read_data,
	},
#endif
};


/*****************************************************************************
 * Generic Part
 */
static void usbtouch_process_pkt(struct usbtouch_usb *usbtouch,
                                 unsigned char *pkt, int len)
{
	struct usbtouch_device_info *type = usbtouch->type;

	if (!type->read_data(usbtouch, pkt))
			return;

	input_report_key(usbtouch->input, BTN_TOUCH, usbtouch->touch);

	if (swap_xy) {
		input_report_abs(usbtouch->input, ABS_X, usbtouch->y);
		input_report_abs(usbtouch->input, ABS_Y, usbtouch->x);
	} else {
		input_report_abs(usbtouch->input, ABS_X, usbtouch->x);
		input_report_abs(usbtouch->input, ABS_Y, usbtouch->y);
	}
	if (type->max_press)
		input_report_abs(usbtouch->input, ABS_PRESSURE, usbtouch->press);
	input_sync(usbtouch->input);
}


#ifdef MULTI_PACKET
static void usbtouch_process_multi(struct usbtouch_usb *usbtouch,
                                   unsigned char *pkt, int len)
{
	unsigned char *buffer;
	int pkt_len, pos, buf_len, tmp;

	/* process buffer */
	if (unlikely(usbtouch->buf_len)) {
		/* try to get size */
		pkt_len = usbtouch->type->get_pkt_len(
				usbtouch->buffer, usbtouch->buf_len);

		/* drop? */
		if (unlikely(!pkt_len))
			goto out_flush_buf;

		/* need to append -pkt_len bytes before able to get size */
		if (unlikely(pkt_len < 0)) {
			int append = -pkt_len;
			if (unlikely(append > len))
			       append = len;
			if (usbtouch->buf_len + append >= usbtouch->type->rept_size)
				goto out_flush_buf;
			memcpy(usbtouch->buffer + usbtouch->buf_len, pkt, append);
			usbtouch->buf_len += append;

			pkt_len = usbtouch->type->get_pkt_len(
					usbtouch->buffer, usbtouch->buf_len);
			if (pkt_len < 0)
				return;
		}

		/* append */
		tmp = pkt_len - usbtouch->buf_len;
		if (usbtouch->buf_len + tmp >= usbtouch->type->rept_size)
			goto out_flush_buf;
		memcpy(usbtouch->buffer + usbtouch->buf_len, pkt, tmp);
		usbtouch_process_pkt(usbtouch, usbtouch->buffer, pkt_len);

		buffer = pkt + tmp;
		buf_len = len - tmp;
	} else {
		buffer = pkt;
		buf_len = len;
	}

	/* loop over the received packet, process */
	pos = 0;
	while (pos < buf_len) {
		/* get packet len */
		pkt_len = usbtouch->type->get_pkt_len(buffer + pos,
							buf_len - pos);

		/* unknown packet: skip one byte */
		if (unlikely(!pkt_len)) {
			pos++;
			continue;
		}

		/* full packet: process */
		if (likely((pkt_len > 0) && (pkt_len <= buf_len - pos))) {
			usbtouch_process_pkt(usbtouch, buffer + pos, pkt_len);
		} else {
			/* incomplete packet: save in buffer */
			memcpy(usbtouch->buffer, buffer + pos, buf_len - pos);
			usbtouch->buf_len = buf_len - pos;
			return;
		}
		pos += pkt_len;
	}

out_flush_buf:
	usbtouch->buf_len = 0;
	return;
}
#endif


static void usbtouch_irq(struct urb *urb)
{
	struct usbtouch_usb *usbtouch = urb->context;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIME:
		/* this urb is timing out */
		dbg("%s - urb timed out - was the device unplugged?",
		    __func__);
		return;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __func__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __func__, urb->status);
		goto exit;
	}

	usbtouch->type->process_pkt(usbtouch, usbtouch->data, urb->actual_length);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result: %d",
		    __func__, retval);
}

static int usbtouch_open(struct input_dev *input)
{
	struct usbtouch_usb *usbtouch = input_get_drvdata(input);

	usbtouch->irq->dev = usbtouch->udev;

	if (usb_submit_urb(usbtouch->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void usbtouch_close(struct input_dev *input)
{
	struct usbtouch_usb *usbtouch = input_get_drvdata(input);

	usb_kill_urb(usbtouch->irq);
}


static void usbtouch_free_buffers(struct usb_device *udev,
				  struct usbtouch_usb *usbtouch)
{
	usb_buffer_free(udev, usbtouch->type->rept_size,
	                usbtouch->data, usbtouch->data_dma);
	kfree(usbtouch->buffer);
}


static int usbtouch_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usbtouch_usb *usbtouch;
	struct input_dev *input_dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usbtouch_device_info *type;
	int err = -ENOMEM;

	/* some devices are ignored */
	if (id->driver_info == DEVTYPE_IGNORE)
		return -ENODEV;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

	usbtouch = kzalloc(sizeof(struct usbtouch_usb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!usbtouch || !input_dev)
		goto out_free;

	type = &usbtouch_dev_info[id->driver_info];
	usbtouch->type = type;
	if (!type->process_pkt)
		type->process_pkt = usbtouch_process_pkt;

	usbtouch->data = usb_buffer_alloc(udev, type->rept_size,
	                                  GFP_KERNEL, &usbtouch->data_dma);
	if (!usbtouch->data)
		goto out_free;

	if (type->get_pkt_len) {
		usbtouch->buffer = kmalloc(type->rept_size, GFP_KERNEL);
		if (!usbtouch->buffer)
			goto out_free_buffers;
	}

	usbtouch->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!usbtouch->irq) {
		dbg("%s - usb_alloc_urb failed: usbtouch->irq", __func__);
		goto out_free_buffers;
	}

	usbtouch->udev = udev;
	usbtouch->input = input_dev;

	if (udev->manufacturer)
		strlcpy(usbtouch->name, udev->manufacturer, sizeof(usbtouch->name));

	if (udev->product) {
		if (udev->manufacturer)
			strlcat(usbtouch->name, " ", sizeof(usbtouch->name));
		strlcat(usbtouch->name, udev->product, sizeof(usbtouch->name));
	}

	if (!strlen(usbtouch->name))
		snprintf(usbtouch->name, sizeof(usbtouch->name),
			"USB Touchscreen %04x:%04x",
			 le16_to_cpu(udev->descriptor.idVendor),
			 le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, usbtouch->phys, sizeof(usbtouch->phys));
	strlcat(usbtouch->phys, "/input0", sizeof(usbtouch->phys));

	input_dev->name = usbtouch->name;
	input_dev->phys = usbtouch->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, usbtouch);

	input_dev->open = usbtouch_open;
	input_dev->close = usbtouch_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, type->min_xc, type->max_xc, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, type->min_yc, type->max_yc, 0, 0);
	if (type->max_press)
		input_set_abs_params(input_dev, ABS_PRESSURE, type->min_press,
		                     type->max_press, 0, 0);

	usb_fill_int_urb(usbtouch->irq, usbtouch->udev,
			 usb_rcvintpipe(usbtouch->udev, endpoint->bEndpointAddress),
			 usbtouch->data, type->rept_size,
			 usbtouch_irq, usbtouch, endpoint->bInterval);

	usbtouch->irq->dev = usbtouch->udev;
	usbtouch->irq->transfer_dma = usbtouch->data_dma;
	usbtouch->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* device specific init */
	if (type->init) {
		err = type->init(usbtouch);
		if (err) {
			dbg("%s - type->init() failed, err: %d", __func__, err);
			goto out_free_buffers;
		}
	}

	err = input_register_device(usbtouch->input);
	if (err) {
		dbg("%s - input_register_device failed, err: %d", __func__, err);
		goto out_free_buffers;
	}

	usb_set_intfdata(intf, usbtouch);

	return 0;

out_free_buffers:
	usbtouch_free_buffers(udev, usbtouch);
out_free:
	input_free_device(input_dev);
	kfree(usbtouch);
	return err;
}

static void usbtouch_disconnect(struct usb_interface *intf)
{
	struct usbtouch_usb *usbtouch = usb_get_intfdata(intf);

	dbg("%s - called", __func__);

	if (!usbtouch)
		return;

	dbg("%s - usbtouch is initialized, cleaning up", __func__);
	usb_set_intfdata(intf, NULL);
	usb_kill_urb(usbtouch->irq);
	input_unregister_device(usbtouch->input);
	usb_free_urb(usbtouch->irq);
	usbtouch_free_buffers(interface_to_usbdev(intf), usbtouch);
	kfree(usbtouch);
}

MODULE_DEVICE_TABLE(usb, usbtouch_devices);

static struct usb_driver usbtouch_driver = {
	.name		= "usbtouchscreen",
	.probe		= usbtouch_probe,
	.disconnect	= usbtouch_disconnect,
	.id_table	= usbtouch_devices,
};

static int __init usbtouch_init(void)
{
	return usb_register(&usbtouch_driver);
}

static void __exit usbtouch_cleanup(void)
{
	usb_deregister(&usbtouch_driver);
}

module_init(usbtouch_init);
module_exit(usbtouch_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_ALIAS("touchkitusb");
MODULE_ALIAS("itmtouch");
MODULE_ALIAS("mtouchusb");
