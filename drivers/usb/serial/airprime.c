/*
 * AirPrime CDMA Wireless Serial USB driver
 *
 * Copyright (C) 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usb-serial.h"

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0xf3d, 0x0112) },  /* AirPrime CDMA Wireless PC Card */
	{ USB_DEVICE(0x1410, 0x1110) }, /* Novatel Wireless Merlin CDMA */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver airprime_driver = {
	.owner =	THIS_MODULE,
	.name =		"airprime",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static struct usb_serial_driver airprime_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"airprime",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
};

static int __init airprime_init(void)
{
	int retval;

	retval = usb_serial_register(&airprime_device);
	if (retval)
		return retval;
	retval = usb_register(&airprime_driver);
	if (retval)
		usb_serial_deregister(&airprime_device);
	return retval;
}

static void __exit airprime_exit(void)
{
	usb_deregister(&airprime_driver);
	usb_serial_deregister(&airprime_device);
}

module_init(airprime_init);
module_exit(airprime_exit);
MODULE_LICENSE("GPL");
