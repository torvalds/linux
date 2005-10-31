/*
 *  Nokia DKU2 USB driver
 *
 *  Copyright (C) 2004
 *  Author: C Kemp
 *
 *  This program is largely derived from work by the linux-usb group
 *  and associated source files.  Please see the usb/serial files for
 *  individual credits and copyrights.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  20.09.2005 - Matthias Blaesing <matthias.blaesing@rwth-aachen.de>
 *  Added short name to device structure to make driver load into kernel 2.6.13
 *
 *  20.09.2005 - Matthias Blaesing <matthias.blaesing@rwth-aachen.de>
 *  Added usb_deregister to exit code - to allow remove and reinsert of module
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usb-serial.h"


#define NOKIA_VENDOR_ID		0x0421
#define NOKIA7600_PRODUCT_ID	0x0400
#define NOKIA6230_PRODUCT_ID	0x040f
#define NOKIA6170_PRODUCT_ID	0x0416
#define NOKIA6670_PRODUCT_ID	0x041d
#define NOKIA6680_PRODUCT_ID	0x041e
#define NOKIA6230i_PRODUCT_ID	0x0428

#define NOKIA_AT_PORT	0x82
#define NOKIA_FBUS_PORT	0x86

/*
 * Version Information
 */
#define DRIVER_VERSION	"v0.2"
#define DRIVER_AUTHOR	"C Kemp"
#define DRIVER_DESC	"Nokia DKU2 Driver"

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA7600_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA6230_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA6170_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA6670_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA6680_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_VENDOR_ID, NOKIA6230i_PRODUCT_ID) },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* The only thing which makes this device different from a generic
 * device is that we have to set an alternative configuration to make
 * the relevant endpoints available. In 2.6 this is really easy... */
static int nokia_probe(struct usb_serial *serial,
		       const struct usb_device_id *id)
{
	int retval = -ENODEV;

	if (serial->interface->altsetting[0].endpoint[0].desc.bEndpointAddress == NOKIA_AT_PORT) {
		/* the AT port */
		dev_info(&serial->dev->dev, "Nokia AT Port:\n");
		retval = 0;
	} else if (serial->interface->num_altsetting == 2 &&
		   serial->interface->altsetting[1].endpoint[0].desc.bEndpointAddress == NOKIA_FBUS_PORT) {
		/* the FBUS port */
		dev_info(&serial->dev->dev, "Nokia FBUS Port:\n");
		usb_set_interface(serial->dev, 10, 1);
		retval = 0;
	}

	return retval;
}

static struct usb_driver nokia_driver = {
	.owner =	THIS_MODULE,
	.name =		"nokia_dku2",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static struct usb_serial_driver nokia_serial_driver = {
	.driver = {
		.owner =	THIS_MODULE,
		.name = 	"nokia_dku2",
	},
	.description =		"Nokia 7600/6230(i)/6170/66x0 DKU2 driver",
	.id_table =		id_table,
	.num_interrupt_in =	1,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_ports =		1,
	.probe =		nokia_probe,
};

static int __init nokia_init(void)
{
        int retval;

	retval = usb_serial_register(&nokia_serial_driver);
	if (retval)
		return retval;

	retval = usb_register(&nokia_driver);
	if (retval) {
	        usb_serial_deregister(&nokia_serial_driver);
		return retval;
	}

	info(DRIVER_VERSION " " DRIVER_AUTHOR);
	info(DRIVER_DESC);

	return retval;
}

static void __exit nokia_exit(void)
{
	usb_deregister(&nokia_driver);
	usb_serial_deregister(&nokia_serial_driver);
}

module_init(nokia_init);
module_exit(nokia_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
