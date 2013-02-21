/*
 * Copyright (C) 2001-2005 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2009 Outpost Embedded, LLC
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_DESC "ViVOpay USB Serial Driver"

#define VIVOPAY_VENDOR_ID 0x1d5f


static struct usb_device_id id_table [] = {
	/* ViVOpay 8800 */
	{ USB_DEVICE(VIVOPAY_VENDOR_ID, 0x1004) },
	{ },
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver vivopay_serial_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"vivopay-serial",
	},
	.id_table =		id_table,
	.num_ports =		1,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&vivopay_serial_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR("Forest Bond <forest.bond@outpostembedded.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
