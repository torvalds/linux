// SPDX-License-Identifier: GPL-2.0-or-later
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
 *  - JASTEC USB touch controller/DigiTech DTR-02U
 *  - Zytronic capacitive touchscreen
 *  - NEXIO/iNexio
 *  - Elo TouchSystems 2700 IntelliTouch
 *  - EasyTouch USB Dual/Multi touch controller from Data Modul
 *
 * Copyright (C) 2004-2007 by Daniel Ritz <daniel.ritz@gmx.ch>
 * Copyright (C) by Todd E. Johnson (mtouchusb.c)
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
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/mutex.h>

static bool swap_xy;
module_param(swap_xy, bool, 0644);
MODULE_PARM_DESC(swap_xy, "If set X and Y axes are swapped.");

static bool hwcalib_xy;
module_param(hwcalib_xy, bool, 0644);
MODULE_PARM_DESC(hwcalib_xy, "If set hw-calibrated X/Y are used if available");

/* device specifc data/functions */
struct usbtouch_usb;
struct usbtouch_device_info {
	int min_xc, max_xc;
	int min_yc, max_yc;
	int min_press, max_press;
	int rept_size;

	/*
	 * Always service the USB devices irq not just when the input device is
	 * open. This is useful when devices have a watchdog which prevents us
	 * from periodically polling the device. Leave this unset unless your
	 * touchscreen device requires it, as it does consume more of the USB
	 * bandwidth.
	 */
	bool irq_always;

	void (*process_pkt) (struct usbtouch_usb *usbtouch, unsigned char *pkt, int len);

	/*
	 * used to get the packet len. possible return values:
	 * > 0: packet len
	 * = 0: skip one byte
	 * < 0: -return value more bytes needed
	 */
	int  (*get_pkt_len) (unsigned char *pkt, int len);

	int  (*read_data)   (struct usbtouch_usb *usbtouch, unsigned char *pkt);
	int  (*alloc)       (struct usbtouch_usb *usbtouch);
	int  (*init)        (struct usbtouch_usb *usbtouch);
	void (*exit)	    (struct usbtouch_usb *usbtouch);
};

/* a usbtouch device */
struct usbtouch_usb {
	unsigned char *data;
	dma_addr_t data_dma;
	int data_size;
	unsigned char *buffer;
	int buf_len;
	struct urb *irq;
	struct usb_interface *interface;
	struct input_dev *input;
	struct usbtouch_device_info *type;
	struct mutex pm_mutex;  /* serialize access to open/suspend */
	bool is_open;
	char name[128];
	char phys[64];
	void *priv;

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
	DEVTYPE_IRTOUCH_HIRES,
	DEVTYPE_IDEALTEK,
	DEVTYPE_GENERAL_TOUCH,
	DEVTYPE_GOTOP,
	DEVTYPE_JASTEC,
	DEVTYPE_E2I,
	DEVTYPE_ZYTRONIC,
	DEVTYPE_TC45USB,
	DEVTYPE_NEXIO,
	DEVTYPE_ELO,
	DEVTYPE_ETOUCH,
};

#define USB_DEVICE_HID_CLASS(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS \
		| USB_DEVICE_ID_MATCH_DEVICE, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bInterfaceClass = USB_INTERFACE_CLASS_HID

static const struct usb_device_id usbtouch_devices[] = {
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
	{USB_DEVICE(0x16e3, 0xf9e9), .driver_info = DEVTYPE_ITM},
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
	{USB_DEVICE(0x255e, 0x0001), .driver_info = DEVTYPE_IRTOUCH},
	{USB_DEVICE(0x595a, 0x0001), .driver_info = DEVTYPE_IRTOUCH},
	{USB_DEVICE(0x6615, 0x0001), .driver_info = DEVTYPE_IRTOUCH},
	{USB_DEVICE(0x6615, 0x0012), .driver_info = DEVTYPE_IRTOUCH_HIRES},
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

#ifdef CONFIG_TOUCHSCREEN_USB_JASTEC
	{USB_DEVICE(0x0f92, 0x0001), .driver_info = DEVTYPE_JASTEC},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_E2I
	{USB_DEVICE(0x1ac7, 0x0001), .driver_info = DEVTYPE_E2I},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ZYTRONIC
	{USB_DEVICE(0x14c8, 0x0003), .driver_info = DEVTYPE_ZYTRONIC},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ETT_TC45USB
	/* TC5UH */
	{USB_DEVICE(0x0664, 0x0309), .driver_info = DEVTYPE_TC45USB},
	/* TC4UM */
	{USB_DEVICE(0x0664, 0x0306), .driver_info = DEVTYPE_TC45USB},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_NEXIO
	/* data interface only */
	{USB_DEVICE_AND_INTERFACE_INFO(0x10f0, 0x2002, 0x0a, 0x00, 0x00),
		.driver_info = DEVTYPE_NEXIO},
	{USB_DEVICE_AND_INTERFACE_INFO(0x1870, 0x0001, 0x0a, 0x00, 0x00),
		.driver_info = DEVTYPE_NEXIO},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ELO
	{USB_DEVICE(0x04e7, 0x0020), .driver_info = DEVTYPE_ELO},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_EASYTOUCH
	{USB_DEVICE(0x7374, 0x0001), .driver_info = DEVTYPE_ETOUCH},
#endif

	{}
};


/*****************************************************************************
 * e2i Part
 */

#ifdef CONFIG_TOUCHSCREEN_USB_E2I
static int e2i_init(struct usbtouch_usb *usbtouch)
{
	int ret;
	struct usb_device *udev = interface_to_usbdev(usbtouch->interface);

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
	                      0x01, 0x02, 0x0000, 0x0081,
	                      NULL, 0, USB_CTRL_SET_TIMEOUT);

	dev_dbg(&usbtouch->interface->dev,
		"%s - usb_control_msg - E2I_RESET - bytes|err: %d\n",
		__func__, ret);
	return ret;
}

static int e2i_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	int tmp = (pkt[0] << 8) | pkt[1];
	dev->x  = (pkt[2] << 8) | pkt[3];
	dev->y  = (pkt[4] << 8) | pkt[5];

	tmp = tmp - 0xA000;
	dev->touch = (tmp > 0);
	dev->press = (tmp > 0 ? tmp : 0);

	return 1;
}
#endif


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

static int egalax_init(struct usbtouch_usb *usbtouch)
{
	int ret, i;
	unsigned char *buf;
	struct usb_device *udev = interface_to_usbdev(usbtouch->interface);

	/*
	 * An eGalax diagnostic packet kicks the device into using the right
	 * protocol.  We send a "check active" packet.  The response will be
	 * read later and ignored.
	 */

	buf = kmalloc(3, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = EGALAX_PKT_TYPE_DIAG;
	buf[1] = 1;	/* length */
	buf[2] = 'A';	/* command - check active */

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				      0,
				      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				      0, 0, buf, 3,
				      USB_CTRL_SET_TIMEOUT);
		if (ret >= 0) {
			ret = 0;
			break;
		}
		if (ret != -EPIPE)
			break;
	}

	kfree(buf);

	return ret;
}

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
 * EasyTouch part
 */

#ifdef CONFIG_TOUCHSCREEN_USB_EASYTOUCH

#ifndef MULTI_PACKET
#define MULTI_PACKET
#endif

#define ETOUCH_PKT_TYPE_MASK		0xFE
#define ETOUCH_PKT_TYPE_REPT		0x80
#define ETOUCH_PKT_TYPE_REPT2		0xB0
#define ETOUCH_PKT_TYPE_DIAG		0x0A

static int etouch_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	if ((pkt[0] & ETOUCH_PKT_TYPE_MASK) != ETOUCH_PKT_TYPE_REPT &&
		(pkt[0] & ETOUCH_PKT_TYPE_MASK) != ETOUCH_PKT_TYPE_REPT2)
		return 0;

	dev->x = ((pkt[1] & 0x1F) << 7) | (pkt[2] & 0x7F);
	dev->y = ((pkt[3] & 0x1F) << 7) | (pkt[4] & 0x7F);
	dev->touch = pkt[0] & 0x01;

	return 1;
}

static int etouch_get_pkt_len(unsigned char *buf, int len)
{
	switch (buf[0] & ETOUCH_PKT_TYPE_MASK) {
	case ETOUCH_PKT_TYPE_REPT:
	case ETOUCH_PKT_TYPE_REPT2:
		return 5;

	case ETOUCH_PKT_TYPE_DIAG:
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

#define MTOUCHUSB_REQ_CTRLLR_ID_LEN	16

static int mtouch_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	if (hwcalib_xy) {
		dev->x = (pkt[4] << 8) | pkt[3];
		dev->y = 0xffff - ((pkt[6] << 8) | pkt[5]);
	} else {
		dev->x = (pkt[8] << 8) | pkt[7];
		dev->y = (pkt[10] << 8) | pkt[9];
	}
	dev->touch = (pkt[2] & 0x40) ? 1 : 0;

	return 1;
}

struct mtouch_priv {
	u8 fw_rev_major;
	u8 fw_rev_minor;
};

static ssize_t mtouch_firmware_rev_show(struct device *dev,
				struct device_attribute *attr, char *output)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usbtouch_usb *usbtouch = usb_get_intfdata(intf);
	struct mtouch_priv *priv = usbtouch->priv;

	return scnprintf(output, PAGE_SIZE, "%1x.%1x\n",
			 priv->fw_rev_major, priv->fw_rev_minor);
}
static DEVICE_ATTR(firmware_rev, 0444, mtouch_firmware_rev_show, NULL);

static struct attribute *mtouch_attrs[] = {
	&dev_attr_firmware_rev.attr,
	NULL
};

static const struct attribute_group mtouch_attr_group = {
	.attrs = mtouch_attrs,
};

static int mtouch_get_fw_revision(struct usbtouch_usb *usbtouch)
{
	struct usb_device *udev = interface_to_usbdev(usbtouch->interface);
	struct mtouch_priv *priv = usbtouch->priv;
	u8 *buf;
	int ret;

	buf = kzalloc(MTOUCHUSB_REQ_CTRLLR_ID_LEN, GFP_NOIO);
	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      MTOUCHUSB_REQ_CTRLLR_ID,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, 0, buf, MTOUCHUSB_REQ_CTRLLR_ID_LEN,
			      USB_CTRL_SET_TIMEOUT);
	if (ret != MTOUCHUSB_REQ_CTRLLR_ID_LEN) {
		dev_warn(&usbtouch->interface->dev,
			 "Failed to read FW rev: %d\n", ret);
		ret = ret < 0 ? ret : -EIO;
		goto free;
	}

	priv->fw_rev_major = buf[3];
	priv->fw_rev_minor = buf[4];

	ret = 0;

free:
	kfree(buf);
	return ret;
}

static int mtouch_alloc(struct usbtouch_usb *usbtouch)
{
	int ret;

	usbtouch->priv = kmalloc(sizeof(struct mtouch_priv), GFP_KERNEL);
	if (!usbtouch->priv)
		return -ENOMEM;

	ret = sysfs_create_group(&usbtouch->interface->dev.kobj,
				 &mtouch_attr_group);
	if (ret) {
		kfree(usbtouch->priv);
		usbtouch->priv = NULL;
		return ret;
	}

	return 0;
}

static int mtouch_init(struct usbtouch_usb *usbtouch)
{
	int ret, i;
	struct usb_device *udev = interface_to_usbdev(usbtouch->interface);

	ret = mtouch_get_fw_revision(usbtouch);
	if (ret)
		return ret;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
	                      MTOUCHUSB_RESET,
	                      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      1, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	dev_dbg(&usbtouch->interface->dev,
		"%s - usb_control_msg - MTOUCHUSB_RESET - bytes|err: %d\n",
		__func__, ret);
	if (ret < 0)
		return ret;
	msleep(150);

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      MTOUCHUSB_ASYNC_REPORT,
				      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				      1, 1, NULL, 0, USB_CTRL_SET_TIMEOUT);
		dev_dbg(&usbtouch->interface->dev,
			"%s - usb_control_msg - MTOUCHUSB_ASYNC_REPORT - bytes|err: %d\n",
			__func__, ret);
		if (ret >= 0)
			break;
		if (ret != -EPIPE)
			return ret;
	}

	/* Default min/max xy are the raw values, override if using hw-calib */
	if (hwcalib_xy) {
		input_set_abs_params(usbtouch->input, ABS_X, 0, 0xffff, 0, 0);
		input_set_abs_params(usbtouch->input, ABS_Y, 0, 0xffff, 0, 0);
	}

	return 0;
}

static void mtouch_exit(struct usbtouch_usb *usbtouch)
{
	struct mtouch_priv *priv = usbtouch->priv;

	sysfs_remove_group(&usbtouch->interface->dev.kobj, &mtouch_attr_group);
	kfree(priv);
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
	struct usb_device *dev = interface_to_usbdev(usbtouch->interface);
	int ret = -ENOMEM;
	unsigned char *buf;

	buf = kmalloc(2, GFP_NOIO);
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
	if (buf[0] != 0x06) {
		ret = -ENODEV;
		goto err_out;
	}

	/* TSC-25 data sheet specifies a delay after the RESET command */
	msleep(150);

	/* set coordinate output rate */
	buf[0] = buf[1] = 0xFF;
	ret = usb_control_msg(dev, usb_rcvctrlpipe (dev, 0),
	                      TSC10_CMD_RATE,
	                      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	                      TSC10_RATE_150, 0, buf, 2, USB_CTRL_SET_TIMEOUT);
	if (ret < 0)
		goto err_out;
	if ((buf[0] != 0x06) && (buf[0] != 0x15 || buf[1] != 0x01)) {
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
 * ET&T TC5UH/TC4UM part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_ETT_TC45USB
static int tc45usb_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[2] & 0x0F) << 8) | pkt[1];
	dev->y = ((pkt[4] & 0x0F) << 8) | pkt[3];
	dev->touch = pkt[0] & 0x01;

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
	dev->x = (pkt[2] << 8) | pkt[1];
	dev->y = (pkt[4] << 8) | pkt[3];
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
 * JASTEC Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_JASTEC
static int jastec_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = ((pkt[0] & 0x3f) << 6) | (pkt[2] & 0x3f);
	dev->y = ((pkt[1] & 0x3f) << 6) | (pkt[3] & 0x3f);
	dev->touch = (pkt[0] & 0x40) >> 6;

	return 1;
}
#endif

/*****************************************************************************
 * Zytronic Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_ZYTRONIC
static int zytronic_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	struct usb_interface *intf = dev->interface;

	switch (pkt[0]) {
	case 0x3A: /* command response */
		dev_dbg(&intf->dev, "%s: Command response %d\n", __func__, pkt[1]);
		break;

	case 0xC0: /* down */
		dev->x = (pkt[1] & 0x7f) | ((pkt[2] & 0x07) << 7);
		dev->y = (pkt[3] & 0x7f) | ((pkt[4] & 0x07) << 7);
		dev->touch = 1;
		dev_dbg(&intf->dev, "%s: down %d,%d\n", __func__, dev->x, dev->y);
		return 1;

	case 0x80: /* up */
		dev->x = (pkt[1] & 0x7f) | ((pkt[2] & 0x07) << 7);
		dev->y = (pkt[3] & 0x7f) | ((pkt[4] & 0x07) << 7);
		dev->touch = 0;
		dev_dbg(&intf->dev, "%s: up %d,%d\n", __func__, dev->x, dev->y);
		return 1;

	default:
		dev_dbg(&intf->dev, "%s: Unknown return %d\n", __func__, pkt[0]);
		break;
	}

	return 0;
}
#endif

/*****************************************************************************
 * NEXIO Part
 */
#ifdef CONFIG_TOUCHSCREEN_USB_NEXIO

#define NEXIO_TIMEOUT	5000
#define NEXIO_BUFSIZE	1024
#define NEXIO_THRESHOLD	50

struct nexio_priv {
	struct urb *ack;
	unsigned char *ack_buf;
};

struct nexio_touch_packet {
	u8	flags;		/* 0xe1 = touch, 0xe1 = release */
	__be16	data_len;	/* total bytes of touch data */
	__be16	x_len;		/* bytes for X axis */
	__be16	y_len;		/* bytes for Y axis */
	u8	data[];
} __attribute__ ((packed));

static unsigned char nexio_ack_pkt[2] = { 0xaa, 0x02 };
static unsigned char nexio_init_pkt[4] = { 0x82, 0x04, 0x0a, 0x0f };

static void nexio_ack_complete(struct urb *urb)
{
}

static int nexio_alloc(struct usbtouch_usb *usbtouch)
{
	struct nexio_priv *priv;
	int ret = -ENOMEM;

	usbtouch->priv = kmalloc(sizeof(struct nexio_priv), GFP_KERNEL);
	if (!usbtouch->priv)
		goto out_buf;

	priv = usbtouch->priv;

	priv->ack_buf = kmemdup(nexio_ack_pkt, sizeof(nexio_ack_pkt),
				GFP_KERNEL);
	if (!priv->ack_buf)
		goto err_priv;

	priv->ack = usb_alloc_urb(0, GFP_KERNEL);
	if (!priv->ack) {
		dev_dbg(&usbtouch->interface->dev,
			"%s - usb_alloc_urb failed: usbtouch->ack\n", __func__);
		goto err_ack_buf;
	}

	return 0;

err_ack_buf:
	kfree(priv->ack_buf);
err_priv:
	kfree(priv);
out_buf:
	return ret;
}

static int nexio_init(struct usbtouch_usb *usbtouch)
{
	struct usb_device *dev = interface_to_usbdev(usbtouch->interface);
	struct usb_host_interface *interface = usbtouch->interface->cur_altsetting;
	struct nexio_priv *priv = usbtouch->priv;
	int ret = -ENOMEM;
	int actual_len, i;
	unsigned char *buf;
	char *firmware_ver = NULL, *device_name = NULL;
	int input_ep = 0, output_ep = 0;

	/* find first input and output endpoint */
	for (i = 0; i < interface->desc.bNumEndpoints; i++) {
		if (!input_ep &&
		    usb_endpoint_dir_in(&interface->endpoint[i].desc))
			input_ep = interface->endpoint[i].desc.bEndpointAddress;
		if (!output_ep &&
		    usb_endpoint_dir_out(&interface->endpoint[i].desc))
			output_ep = interface->endpoint[i].desc.bEndpointAddress;
	}
	if (!input_ep || !output_ep)
		return -ENXIO;

	buf = kmalloc(NEXIO_BUFSIZE, GFP_NOIO);
	if (!buf)
		goto out_buf;

	/* two empty reads */
	for (i = 0; i < 2; i++) {
		ret = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, input_ep),
				   buf, NEXIO_BUFSIZE, &actual_len,
				   NEXIO_TIMEOUT);
		if (ret < 0)
			goto out_buf;
	}

	/* send init command */
	memcpy(buf, nexio_init_pkt, sizeof(nexio_init_pkt));
	ret = usb_bulk_msg(dev, usb_sndbulkpipe(dev, output_ep),
			   buf, sizeof(nexio_init_pkt), &actual_len,
			   NEXIO_TIMEOUT);
	if (ret < 0)
		goto out_buf;

	/* read replies */
	for (i = 0; i < 3; i++) {
		memset(buf, 0, NEXIO_BUFSIZE);
		ret = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, input_ep),
				   buf, NEXIO_BUFSIZE, &actual_len,
				   NEXIO_TIMEOUT);
		if (ret < 0 || actual_len < 1 || buf[1] != actual_len)
			continue;
		switch (buf[0]) {
		case 0x83:	/* firmware version */
			if (!firmware_ver)
				firmware_ver = kstrdup(&buf[2], GFP_NOIO);
			break;
		case 0x84:	/* device name */
			if (!device_name)
				device_name = kstrdup(&buf[2], GFP_NOIO);
			break;
		}
	}

	printk(KERN_INFO "Nexio device: %s, firmware version: %s\n",
	       device_name, firmware_ver);

	kfree(firmware_ver);
	kfree(device_name);

	usb_fill_bulk_urb(priv->ack, dev, usb_sndbulkpipe(dev, output_ep),
			  priv->ack_buf, sizeof(nexio_ack_pkt),
			  nexio_ack_complete, usbtouch);
	ret = 0;

out_buf:
	kfree(buf);
	return ret;
}

static void nexio_exit(struct usbtouch_usb *usbtouch)
{
	struct nexio_priv *priv = usbtouch->priv;

	usb_kill_urb(priv->ack);
	usb_free_urb(priv->ack);
	kfree(priv->ack_buf);
	kfree(priv);
}

static int nexio_read_data(struct usbtouch_usb *usbtouch, unsigned char *pkt)
{
	struct nexio_touch_packet *packet = (void *) pkt;
	struct nexio_priv *priv = usbtouch->priv;
	unsigned int data_len = be16_to_cpu(packet->data_len);
	unsigned int x_len = be16_to_cpu(packet->x_len);
	unsigned int y_len = be16_to_cpu(packet->y_len);
	int x, y, begin_x, begin_y, end_x, end_y, w, h, ret;

	/* got touch data? */
	if ((pkt[0] & 0xe0) != 0xe0)
		return 0;

	if (data_len > 0xff)
		data_len -= 0x100;
	if (x_len > 0xff)
		x_len -= 0x80;

	/* send ACK */
	ret = usb_submit_urb(priv->ack, GFP_ATOMIC);

	if (!usbtouch->type->max_xc) {
		usbtouch->type->max_xc = 2 * x_len;
		input_set_abs_params(usbtouch->input, ABS_X,
				     0, usbtouch->type->max_xc, 0, 0);
		usbtouch->type->max_yc = 2 * y_len;
		input_set_abs_params(usbtouch->input, ABS_Y,
				     0, usbtouch->type->max_yc, 0, 0);
	}
	/*
	 * The device reports state of IR sensors on X and Y axes.
	 * Each byte represents "darkness" percentage (0-100) of one element.
	 * 17" touchscreen reports only 64 x 52 bytes so the resolution is low.
	 * This also means that there's a limited multi-touch capability but
	 * it's disabled (and untested) here as there's no X driver for that.
	 */
	begin_x = end_x = begin_y = end_y = -1;
	for (x = 0; x < x_len; x++) {
		if (begin_x == -1 && packet->data[x] > NEXIO_THRESHOLD) {
			begin_x = x;
			continue;
		}
		if (end_x == -1 && begin_x != -1 && packet->data[x] < NEXIO_THRESHOLD) {
			end_x = x - 1;
			for (y = x_len; y < data_len; y++) {
				if (begin_y == -1 && packet->data[y] > NEXIO_THRESHOLD) {
					begin_y = y - x_len;
					continue;
				}
				if (end_y == -1 &&
				    begin_y != -1 && packet->data[y] < NEXIO_THRESHOLD) {
					end_y = y - 1 - x_len;
					w = end_x - begin_x;
					h = end_y - begin_y;
#if 0
					/* multi-touch */
					input_report_abs(usbtouch->input,
						    ABS_MT_TOUCH_MAJOR, max(w,h));
					input_report_abs(usbtouch->input,
						    ABS_MT_TOUCH_MINOR, min(x,h));
					input_report_abs(usbtouch->input,
						    ABS_MT_POSITION_X, 2*begin_x+w);
					input_report_abs(usbtouch->input,
						    ABS_MT_POSITION_Y, 2*begin_y+h);
					input_report_abs(usbtouch->input,
						    ABS_MT_ORIENTATION, w > h);
					input_mt_sync(usbtouch->input);
#endif
					/* single touch */
					usbtouch->x = 2 * begin_x + w;
					usbtouch->y = 2 * begin_y + h;
					usbtouch->touch = packet->flags & 0x01;
					begin_y = end_y = -1;
					return 1;
				}
			}
			begin_x = end_x = -1;
		}

	}
	return 0;
}
#endif


/*****************************************************************************
 * ELO part
 */

#ifdef CONFIG_TOUCHSCREEN_USB_ELO

static int elo_read_data(struct usbtouch_usb *dev, unsigned char *pkt)
{
	dev->x = (pkt[3] << 8) | pkt[2];
	dev->y = (pkt[5] << 8) | pkt[4];
	dev->touch = pkt[6] > 0;
	dev->press = pkt[6];

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
#ifdef CONFIG_TOUCHSCREEN_USB_ELO
	[DEVTYPE_ELO] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.max_press	= 0xff,
		.rept_size	= 8,
		.read_data	= elo_read_data,
	},
#endif

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
		.init		= egalax_init,
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
		.alloc		= mtouch_alloc,
		.init		= mtouch_init,
		.exit		= mtouch_exit,
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

	[DEVTYPE_IRTOUCH_HIRES] = {
		.min_xc		= 0x0,
		.max_xc		= 0x7fff,
		.min_yc		= 0x0,
		.max_yc		= 0x7fff,
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
		.max_xc		= 0x7fff,
		.min_yc		= 0x0,
		.max_yc		= 0x7fff,
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

#ifdef CONFIG_TOUCHSCREEN_USB_JASTEC
	[DEVTYPE_JASTEC] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 4,
		.read_data	= jastec_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_E2I
	[DEVTYPE_E2I] = {
		.min_xc		= 0x0,
		.max_xc		= 0x7fff,
		.min_yc		= 0x0,
		.max_yc		= 0x7fff,
		.rept_size	= 6,
		.init		= e2i_init,
		.read_data	= e2i_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ZYTRONIC
	[DEVTYPE_ZYTRONIC] = {
		.min_xc		= 0x0,
		.max_xc		= 0x03ff,
		.min_yc		= 0x0,
		.max_yc		= 0x03ff,
		.rept_size	= 5,
		.read_data	= zytronic_read_data,
		.irq_always     = true,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_ETT_TC45USB
	[DEVTYPE_TC45USB] = {
		.min_xc		= 0x0,
		.max_xc		= 0x0fff,
		.min_yc		= 0x0,
		.max_yc		= 0x0fff,
		.rept_size	= 5,
		.read_data	= tc45usb_read_data,
	},
#endif

#ifdef CONFIG_TOUCHSCREEN_USB_NEXIO
	[DEVTYPE_NEXIO] = {
		.rept_size	= 1024,
		.irq_always	= true,
		.read_data	= nexio_read_data,
		.alloc		= nexio_alloc,
		.init		= nexio_init,
		.exit		= nexio_exit,
	},
#endif
#ifdef CONFIG_TOUCHSCREEN_USB_EASYTOUCH
	[DEVTYPE_ETOUCH] = {
		.min_xc		= 0x0,
		.max_xc		= 0x07ff,
		.min_yc		= 0x0,
		.max_yc		= 0x07ff,
		.rept_size	= 16,
		.process_pkt	= usbtouch_process_multi,
		.get_pkt_len	= etouch_get_pkt_len,
		.read_data	= etouch_read_data,
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
	struct device *dev = &usbtouch->interface->dev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIME:
		/* this urb is timing out */
		dev_dbg(dev,
			"%s - urb timed out - was the device unplugged?\n",
			__func__);
		return;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPIPE:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}

	usbtouch->type->process_pkt(usbtouch, usbtouch->data, urb->actual_length);

exit:
	usb_mark_last_busy(interface_to_usbdev(usbtouch->interface));
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "%s - usb_submit_urb failed with result: %d\n",
			__func__, retval);
}

static int usbtouch_open(struct input_dev *input)
{
	struct usbtouch_usb *usbtouch = input_get_drvdata(input);
	int r;

	usbtouch->irq->dev = interface_to_usbdev(usbtouch->interface);

	r = usb_autopm_get_interface(usbtouch->interface) ? -EIO : 0;
	if (r < 0)
		goto out;

	mutex_lock(&usbtouch->pm_mutex);
	if (!usbtouch->type->irq_always) {
		if (usb_submit_urb(usbtouch->irq, GFP_KERNEL)) {
			r = -EIO;
			goto out_put;
		}
	}

	usbtouch->interface->needs_remote_wakeup = 1;
	usbtouch->is_open = true;
out_put:
	mutex_unlock(&usbtouch->pm_mutex);
	usb_autopm_put_interface(usbtouch->interface);
out:
	return r;
}

static void usbtouch_close(struct input_dev *input)
{
	struct usbtouch_usb *usbtouch = input_get_drvdata(input);
	int r;

	mutex_lock(&usbtouch->pm_mutex);
	if (!usbtouch->type->irq_always)
		usb_kill_urb(usbtouch->irq);
	usbtouch->is_open = false;
	mutex_unlock(&usbtouch->pm_mutex);

	r = usb_autopm_get_interface(usbtouch->interface);
	usbtouch->interface->needs_remote_wakeup = 0;
	if (!r)
		usb_autopm_put_interface(usbtouch->interface);
}

static int usbtouch_suspend
(struct usb_interface *intf, pm_message_t message)
{
	struct usbtouch_usb *usbtouch = usb_get_intfdata(intf);

	usb_kill_urb(usbtouch->irq);

	return 0;
}

static int usbtouch_resume(struct usb_interface *intf)
{
	struct usbtouch_usb *usbtouch = usb_get_intfdata(intf);
	int result = 0;

	mutex_lock(&usbtouch->pm_mutex);
	if (usbtouch->is_open || usbtouch->type->irq_always)
		result = usb_submit_urb(usbtouch->irq, GFP_NOIO);
	mutex_unlock(&usbtouch->pm_mutex);

	return result;
}

static int usbtouch_reset_resume(struct usb_interface *intf)
{
	struct usbtouch_usb *usbtouch = usb_get_intfdata(intf);
	int err = 0;

	/* reinit the device */
	if (usbtouch->type->init) {
		err = usbtouch->type->init(usbtouch);
		if (err) {
			dev_dbg(&intf->dev,
				"%s - type->init() failed, err: %d\n",
				__func__, err);
			return err;
		}
	}

	/* restart IO if needed */
	mutex_lock(&usbtouch->pm_mutex);
	if (usbtouch->is_open)
		err = usb_submit_urb(usbtouch->irq, GFP_NOIO);
	mutex_unlock(&usbtouch->pm_mutex);

	return err;
}

static void usbtouch_free_buffers(struct usb_device *udev,
				  struct usbtouch_usb *usbtouch)
{
	usb_free_coherent(udev, usbtouch->data_size,
			  usbtouch->data, usbtouch->data_dma);
	kfree(usbtouch->buffer);
}

static struct usb_endpoint_descriptor *
usbtouch_get_input_endpoint(struct usb_host_interface *interface)
{
	int i;

	for (i = 0; i < interface->desc.bNumEndpoints; i++)
		if (usb_endpoint_dir_in(&interface->endpoint[i].desc))
			return &interface->endpoint[i].desc;

	return NULL;
}

static int usbtouch_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usbtouch_usb *usbtouch;
	struct input_dev *input_dev;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usbtouch_device_info *type;
	int err = -ENOMEM;

	/* some devices are ignored */
	if (id->driver_info == DEVTYPE_IGNORE)
		return -ENODEV;

	endpoint = usbtouch_get_input_endpoint(intf->cur_altsetting);
	if (!endpoint)
		return -ENXIO;

	usbtouch = kzalloc(sizeof(struct usbtouch_usb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!usbtouch || !input_dev)
		goto out_free;

	mutex_init(&usbtouch->pm_mutex);

	type = &usbtouch_dev_info[id->driver_info];
	usbtouch->type = type;
	if (!type->process_pkt)
		type->process_pkt = usbtouch_process_pkt;

	usbtouch->data_size = type->rept_size;
	if (type->get_pkt_len) {
		/*
		 * When dealing with variable-length packets we should
		 * not request more than wMaxPacketSize bytes at once
		 * as we do not know if there is more data coming or
		 * we filled exactly wMaxPacketSize bytes and there is
		 * nothing else.
		 */
		usbtouch->data_size = min(usbtouch->data_size,
					  usb_endpoint_maxp(endpoint));
	}

	usbtouch->data = usb_alloc_coherent(udev, usbtouch->data_size,
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
		dev_dbg(&intf->dev,
			"%s - usb_alloc_urb failed: usbtouch->irq\n", __func__);
		goto out_free_buffers;
	}

	usbtouch->interface = intf;
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

	if (usb_endpoint_type(endpoint) == USB_ENDPOINT_XFER_INT)
		usb_fill_int_urb(usbtouch->irq, udev,
			 usb_rcvintpipe(udev, endpoint->bEndpointAddress),
			 usbtouch->data, usbtouch->data_size,
			 usbtouch_irq, usbtouch, endpoint->bInterval);
	else
		usb_fill_bulk_urb(usbtouch->irq, udev,
			 usb_rcvbulkpipe(udev, endpoint->bEndpointAddress),
			 usbtouch->data, usbtouch->data_size,
			 usbtouch_irq, usbtouch);

	usbtouch->irq->dev = udev;
	usbtouch->irq->transfer_dma = usbtouch->data_dma;
	usbtouch->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* device specific allocations */
	if (type->alloc) {
		err = type->alloc(usbtouch);
		if (err) {
			dev_dbg(&intf->dev,
				"%s - type->alloc() failed, err: %d\n",
				__func__, err);
			goto out_free_urb;
		}
	}

	/* device specific initialisation*/
	if (type->init) {
		err = type->init(usbtouch);
		if (err) {
			dev_dbg(&intf->dev,
				"%s - type->init() failed, err: %d\n",
				__func__, err);
			goto out_do_exit;
		}
	}

	err = input_register_device(usbtouch->input);
	if (err) {
		dev_dbg(&intf->dev,
			"%s - input_register_device failed, err: %d\n",
			__func__, err);
		goto out_do_exit;
	}

	usb_set_intfdata(intf, usbtouch);

	if (usbtouch->type->irq_always) {
		/* this can't fail */
		usb_autopm_get_interface(intf);
		err = usb_submit_urb(usbtouch->irq, GFP_KERNEL);
		if (err) {
			usb_autopm_put_interface(intf);
			dev_err(&intf->dev,
				"%s - usb_submit_urb failed with result: %d\n",
				__func__, err);
			goto out_unregister_input;
		}
	}

	return 0;

out_unregister_input:
	input_unregister_device(input_dev);
	input_dev = NULL;
out_do_exit:
	if (type->exit)
		type->exit(usbtouch);
out_free_urb:
	usb_free_urb(usbtouch->irq);
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

	if (!usbtouch)
		return;

	dev_dbg(&intf->dev,
		"%s - usbtouch is initialized, cleaning up\n", __func__);

	usb_set_intfdata(intf, NULL);
	/* this will stop IO via close */
	input_unregister_device(usbtouch->input);
	usb_free_urb(usbtouch->irq);
	if (usbtouch->type->exit)
		usbtouch->type->exit(usbtouch);
	usbtouch_free_buffers(interface_to_usbdev(intf), usbtouch);
	kfree(usbtouch);
}

MODULE_DEVICE_TABLE(usb, usbtouch_devices);

static struct usb_driver usbtouch_driver = {
	.name		= "usbtouchscreen",
	.probe		= usbtouch_probe,
	.disconnect	= usbtouch_disconnect,
	.suspend	= usbtouch_suspend,
	.resume		= usbtouch_resume,
	.reset_resume	= usbtouch_reset_resume,
	.id_table	= usbtouch_devices,
	.supports_autosuspend = 1,
};

module_usb_driver(usbtouch_driver);

MODULE_AUTHOR("Daniel Ritz <daniel.ritz@gmx.ch>");
MODULE_DESCRIPTION("USB Touchscreen Driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("touchkitusb");
MODULE_ALIAS("itmtouch");
MODULE_ALIAS("mtouchusb");
