/*
 * Siemens USB-MPI Serial USB driver
 *
 * Copyright (C) 2005 Thomas Hergenhahn <thomas.hergenhahn@suse.de>
 * Copyright (C) 2005,2008 Greg Kroah-Hartman <gregkh@suse.de>
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

/* Version Information */
#define DRIVER_VERSION "Version 0.1 09/26/2005"
#define DRIVER_AUTHOR "Thomas Hergenhahn@web.de http://libnodave.sf.net"
#define DRIVER_DESC "Driver for Siemens USB/MPI adapter"


static const struct usb_device_id id_table[] = {
	/* Vendor and product id for 6ES7-972-0CB20-0XA0 */
	{ USB_DEVICE(0x908, 0x0004) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver siemens_usb_mpi_driver = {
	.name =		"siemens_mpi",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static struct usb_serial_driver siemens_usb_mpi_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"siemens_mpi",
	},
	.id_table =		id_table,
	.num_ports =		1,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&siemens_usb_mpi_device, NULL
};

module_usb_serial_driver(siemens_usb_mpi_driver, serial_drivers);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
