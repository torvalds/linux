/*
 * Sierra Wireless CDMA Wireless Serial USB driver
 *
 * Current Copy modified by: Kevin Lloyd <linux@sierrawireless.com>
 * Original Copyright (C) 2005-2006 Greg Kroah-Hartman <gregkh@suse.de>
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
#include <linux/usb/serial.h>

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x1199, 0x0018) },	/* Sierra Wireless MC5720 */
	{ USB_DEVICE(0x1199, 0x0020) },	/* Sierra Wireless MC5725 */
	{ USB_DEVICE(0x1199, 0x0017) },	/* Sierra Wireless EM5625 */
	{ USB_DEVICE(0x1199, 0x0019) },	/* Sierra Wireless AirCard 595 */
	{ USB_DEVICE(0x1199, 0x6802) },	/* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6803) },	/* Sierra Wireless MC8765 */
	{ USB_DEVICE(0x1199, 0x6812) },	/* Sierra Wireless MC8775 */
	{ USB_DEVICE(0x1199, 0x6820) },	/* Sierra Wireless AirCard 875 */
	/* Following devices are supported in the airprime.c driver */
	/* { USB_DEVICE(0x1199, 0x0112) }, */	/* Sierra Wireless AirCard 580 */
	/* { USB_DEVICE(0x0F3D, 0x0112) }, */	/* AirPrime/Sierra PC 5220 */
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver sierra_driver = {
	.name =		"sierra_wireless",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static struct usb_serial_driver sierra_device = {
	.driver = {
	.owner =		THIS_MODULE,
	.name =			"Sierra_Wireless",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		3,
};

static int __init sierra_init(void)
{
	int retval;

	retval = usb_serial_register(&sierra_device);
	if (retval)
		return retval;
	retval = usb_register(&sierra_driver);
	if (retval)
		usb_serial_deregister(&sierra_device);
	return retval;
}

static void __exit sierra_exit(void)
{
	usb_deregister(&sierra_driver);
	usb_serial_deregister(&sierra_device);
}

module_init(sierra_init);
module_exit(sierra_exit);
MODULE_LICENSE("GPL");
