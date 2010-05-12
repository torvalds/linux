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
	.no_dynamic_id =	1,
};

static struct usb_serial_driver vivopay_serial_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"vivopay-serial",
	},
	.id_table =		id_table,
	.usb_driver =		&vivopay_serial_driver,
	.num_ports =		1,
};

static int __init vivopay_serial_init(void)
{
	int retval;
	retval = usb_serial_register(&vivopay_serial_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&vivopay_serial_driver);
	if (retval)
		goto failed_usb_register;
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	    DRIVER_DESC "\n");
	return 0;
failed_usb_register:
	usb_serial_deregister(&vivopay_serial_device);
failed_usb_serial_register:
	return retval;
}

static void __exit vivopay_serial_exit(void)
{
	usb_deregister(&vivopay_serial_driver);
	usb_serial_deregister(&vivopay_serial_device);
}

module_init(vivopay_serial_init);
module_exit(vivopay_serial_exit);

MODULE_AUTHOR("Forest Bond <forest.bond@outpostembedded.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
