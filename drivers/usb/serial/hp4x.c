/*
 * HP4x Calculators Serial USB driver
 *
 * Copyright (C) 2005 Arthur Huillet (ahuillet@users.sf.net)
 * Copyright (C) 2001-2005 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.00"
#define DRIVER_DESC "HP4x (48/49) Generic Serial driver"

#define HP_VENDOR_ID 0x03f0
#define HP49GP_PRODUCT_ID 0x0121

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(HP_VENDOR_ID, HP49GP_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver hp49gp_driver = {
	.name =		"hp4X",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static struct usb_serial_driver hp49gp_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"hp4X",
	},
	.id_table =		id_table,
	.num_ports =		1,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&hp49gp_device, NULL
};

static int __init hp49gp_init(void)
{
	int retval;

	retval = usb_serial_register_drivers(&hp49gp_driver, serial_drivers);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
			       DRIVER_DESC "\n");
	return retval;
}

static void __exit hp49gp_exit(void)
{
	usb_serial_deregister_drivers(&hp49gp_driver, serial_drivers);
}

module_init(hp49gp_init);
module_exit(hp49gp_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
