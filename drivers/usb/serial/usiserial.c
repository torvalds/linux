/*
 * USI WCDMA Modem driver
 *
 * Copyright (C) 2011 John Tseng
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static int usi_probe(struct usb_interface *interface,
			 const struct usb_device_id *id);

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x0e8d, 0x00a1) },	/* USI WCDMA modem 3COM */
	{ USB_DEVICE(0x0e8d, 0x00a2) },	/* USI WCDMA modem 2COM */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver usi_driver = {
	.name =		"usi-modem",
	.probe =	usi_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.suspend =	usb_serial_suspend,
	.resume =	usb_serial_resume,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver usi_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"usi-modem",
	},
        .usb_driver =           &usi_driver,
	.id_table =		id_table,
	.num_ports =		1,
};

static int usi_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	int if_num;

	if_num = interface->altsetting->desc.bInterfaceNumber;

	if (if_num == 2)
		return usb_serial_probe(interface, id);

	return -ENODEV;
}

static int __init usi_init(void)
{
	int retval;

	retval = usb_serial_register(&usi_device);
	if (retval)
		return retval;
	retval = usb_register(&usi_driver);
	if (retval)
		usb_serial_deregister(&usi_device);
	return retval;
}

static void __exit usi_exit(void)
{
	usb_deregister(&usi_driver);
	usb_serial_deregister(&usi_device);
}

module_init(usi_init);
module_exit(usi_exit);
MODULE_LICENSE("GPL");
