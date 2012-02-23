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


#define DRIVER_VERSION "v1.0"
#define DRIVER_DESC "ViVOpay USB Serial Driver"

#define VIVOPAY_VENDOR_ID 0x1d5f


static struct usb_device_id id_table [] = {
	/* ViVOpay 8800 */
	{ USB_DEVICE(VIVOPAY_VENDOR_ID, 0x1004) },
	{ },
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver vivopay_serial_driver = {
	.name =			"vivopay-serial",
	.probe =		usb_serial_probe,
	.disconnect =		usb_serial_disconnect,
	.id_table =		id_table,
};

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

static int __init vivopay_serial_init(void)
{
	int retval;

	retval = usb_serial_register_drivers(&vivopay_serial_driver,
			serial_drivers);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
				DRIVER_DESC "\n");
	return retval;
}

static void __exit vivopay_serial_exit(void)
{
	usb_serial_deregister_drivers(&vivopay_serial_driver, serial_drivers);
}

module_init(vivopay_serial_init);
module_exit(vivopay_serial_exit);

MODULE_AUTHOR("Forest Bond <forest.bond@outpostembedded.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
