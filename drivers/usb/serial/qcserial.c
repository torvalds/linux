/*
 * Qualcomm Serial USB driver
 *
 *	Copyright (c) 2008 QUALCOMM Incorporated.
 *	Copyright (c) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 *	Copyright (c) 2009 Novell Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_AUTHOR "Qualcomm Inc"
#define DRIVER_DESC "Qualcomm USB Serial driver"

static int debug;

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(0x05c6, 0x9211)},	/* Acer Gobi QDL device */
	{USB_DEVICE(0x05c6, 0x9212)},	/* Acer Gobi Modem Device */
	{USB_DEVICE(0x03f0, 0x1f1d)},	/* HP un2400 Gobi Modem Device */
	{USB_DEVICE(0x03f0, 0x201d)},	/* HP un2400 Gobi QDL Device */
	{USB_DEVICE(0x04da, 0x250d)},	/* Panasonic Gobi Modem device */
	{USB_DEVICE(0x04da, 0x250c)},	/* Panasonic Gobi QDL device */
	{USB_DEVICE(0x413c, 0x8172)},	/* Dell Gobi Modem device */
	{USB_DEVICE(0x413c, 0x8171)},	/* Dell Gobi QDL device */
	{USB_DEVICE(0x1410, 0xa001)},	/* Novatel Gobi Modem device */
	{USB_DEVICE(0x1410, 0xa008)},	/* Novatel Gobi QDL device */
	{USB_DEVICE(0x0b05, 0x1776)},	/* Asus Gobi Modem device */
	{USB_DEVICE(0x0b05, 0x1774)},	/* Asus Gobi QDL device */
	{USB_DEVICE(0x19d2, 0xfff3)},	/* ONDA Gobi Modem device */
	{USB_DEVICE(0x19d2, 0xfff2)},	/* ONDA Gobi QDL device */
	{USB_DEVICE(0x1557, 0x0a80)},	/* OQO Gobi QDL device */
	{USB_DEVICE(0x05c6, 0x9001)},   /* Generic Gobi Modem device */
	{USB_DEVICE(0x05c6, 0x9002)},	/* Generic Gobi Modem device */
	{USB_DEVICE(0x05c6, 0x9202)},	/* Generic Gobi Modem device */
	{USB_DEVICE(0x05c6, 0x9203)},	/* Generic Gobi Modem device */
	{USB_DEVICE(0x05c6, 0x9222)},	/* Generic Gobi Modem device */
	{USB_DEVICE(0x05c6, 0x9008)},	/* Generic Gobi QDL device */
	{USB_DEVICE(0x05c6, 0x9201)},	/* Generic Gobi QDL device */
	{USB_DEVICE(0x05c6, 0x9221)},	/* Generic Gobi QDL device */
	{USB_DEVICE(0x05c6, 0x9231)},	/* Generic Gobi QDL device */
	{USB_DEVICE(0x1f45, 0x0001)},	/* Unknown Gobi QDL device */
	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver qcdriver = {
	.name			= "qcserial",
	.probe			= usb_serial_probe,
	.disconnect		= usb_serial_disconnect,
	.id_table		= id_table,
	.suspend		= usb_serial_suspend,
	.resume			= usb_serial_resume,
	.supports_autosuspend	= true,
};

static int qcprobe(struct usb_serial *serial, const struct usb_device_id *id)
{
	int retval = -ENODEV;
	__u8 nintf;
	__u8 ifnum;

	dbg("%s", __func__);

	nintf = serial->dev->actconfig->desc.bNumInterfaces;
	dbg("Num Interfaces = %d", nintf);
	ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;
	dbg("This Interface = %d", ifnum);

	switch (nintf) {
	case 1:
		/* QDL mode */
		if (serial->interface->num_altsetting == 2) {
			struct usb_host_interface *intf;

			intf = &serial->interface->altsetting[1];
			if (intf->desc.bNumEndpoints == 2) {
				if (usb_endpoint_is_bulk_in(&intf->endpoint[0].desc) &&
				    usb_endpoint_is_bulk_out(&intf->endpoint[1].desc)) {
					dbg("QDL port found");
					retval = usb_set_interface(serial->dev, ifnum, 1);
					if (retval < 0) {
						dev_err(&serial->dev->dev,
							"Could not set interface, error %d\n",
							retval);
						retval = -ENODEV;
					}
					return retval;
				}
			}
		}
		break;

	case 4:
		/* Composite mode */
		if (ifnum == 2) {
			dbg("Modem port found");
			retval = usb_set_interface(serial->dev, ifnum, 0);
			if (retval < 0) {
				dev_err(&serial->dev->dev,
					"Could not set interface, error %d\n",
					retval);
				retval = -ENODEV;
			}
			return retval;
		}
		break;

	default:
		dev_err(&serial->dev->dev,
			"unknown number of interfaces: %d\n", nintf);
		return -ENODEV;
	}

	return retval;
}

static struct usb_serial_driver qcdevice = {
	.driver = {
		.owner     = THIS_MODULE,
		.name      = "qcserial",
	},
	.description         = "Qualcomm USB modem",
	.id_table            = id_table,
	.usb_driver          = &qcdriver,
	.num_ports           = 1,
	.probe               = qcprobe,
};

static int __init qcinit(void)
{
	int retval;

	retval = usb_serial_register(&qcdevice);
	if (retval)
		return retval;

	retval = usb_register(&qcdriver);
	if (retval) {
		usb_serial_deregister(&qcdevice);
		return retval;
	}

	return 0;
}

static void __exit qcexit(void)
{
	usb_deregister(&qcdriver);
	usb_serial_deregister(&qcdevice);
}

module_init(qcinit);
module_exit(qcexit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
