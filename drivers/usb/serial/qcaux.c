/*
 * Qualcomm USB Auxiliary Serial Port driver
 *
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2010 Dan Williams <dcbw@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 * Devices listed here usually provide a CDC ACM port on which normal modem
 * AT commands and PPP can be used.  But when that port is in-use by PPP it
 * cannot be used simultaneously for status or signal strength.  Instead, the
 * ports here can be queried for that information using the Qualcomm DM
 * protocol.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

/* NOTE: for now, only use this driver for devices that provide a CDC-ACM port
 * for normal AT commands, but also provide secondary USB interfaces for the
 * QCDM-capable ports.  Devices that do not provide a CDC-ACM port should
 * probably be driven by option.ko.
 */

/* UTStarcom/Pantech/Curitel devices */
#define UTSTARCOM_VENDOR_ID			0x106c
#define UTSTARCOM_PRODUCT_PC5740		0x3701
#define UTSTARCOM_PRODUCT_PC5750		0x3702 /* aka Pantech PX-500 */
#define UTSTARCOM_PRODUCT_UM150			0x3711
#define UTSTARCOM_PRODUCT_UM175_V1		0x3712
#define UTSTARCOM_PRODUCT_UM175_V2		0x3714
#define UTSTARCOM_PRODUCT_UM175_ALLTEL		0x3715

/* CMOTECH devices */
#define CMOTECH_VENDOR_ID			0x16d8
#define CMOTECH_PRODUCT_CDU550			0x5553
#define CMOTECH_PRODUCT_CDX650			0x6512

static struct usb_device_id id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_PC5740, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_PC5750, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_UM150, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_UM175_V1, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_UM175_V2, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(UTSTARCOM_VENDOR_ID, UTSTARCOM_PRODUCT_UM175_ALLTEL, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(CMOTECH_VENDOR_ID, CMOTECH_PRODUCT_CDU550, 0xff, 0xff, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(CMOTECH_VENDOR_ID, CMOTECH_PRODUCT_CDX650, 0xff, 0xff, 0x00) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver qcaux_driver = {
	.name =		"qcaux",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver qcaux_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"qcaux",
	},
	.id_table =		id_table,
	.num_ports =		1,
};

static int __init qcaux_init(void)
{
	int retval;

	retval = usb_serial_register(&qcaux_device);
	if (retval)
		return retval;
	retval = usb_register(&qcaux_driver);
	if (retval)
		usb_serial_deregister(&qcaux_device);
	return retval;
}

static void __exit qcaux_exit(void)
{
	usb_deregister(&qcaux_driver);
	usb_serial_deregister(&qcaux_device);
}

module_init(qcaux_init);
module_exit(qcaux_exit);
MODULE_LICENSE("GPL");
