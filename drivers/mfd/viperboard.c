/*
 *  Nano River Technologies viperboard driver
 *
 *  This is the core driver for the viperboard. There are cell drivers
 *  available for I2C, ADC and both GPIOs. SPI is not yet supported.
 *  The drivers do not support all features the board exposes. See user
 *  manual of the viperboard.
 *
 *  (C) 2012 by Lemonage GmbH
 *  Author: Lars Poeschel <poeschel@lemonage.de>
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include <linux/mfd/core.h>
#include <linux/mfd/viperboard.h>

#include <linux/usb.h>


static const struct usb_device_id vprbrd_table[] = {
	{ USB_DEVICE(0x2058, 0x1005) },   /* Nano River Technologies */
	{ }                               /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, vprbrd_table);

static struct mfd_cell vprbrd_devs[] = {
	{
		.name = "viperboard-gpio",
	},
	{
		.name = "viperboard-i2c",
	},
};

static int vprbrd_probe(struct usb_interface *interface,
			      const struct usb_device_id *id)
{
	struct vprbrd *vb;

	u16 version = 0;
	int pipe, ret;
	unsigned char buf[1];

	/* allocate memory for our device state and initialize it */
	vb = kzalloc(sizeof(*vb), GFP_KERNEL);
	if (vb == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		return -ENOMEM;
	}

	mutex_init(&vb->lock);

	vb->usb_dev = usb_get_dev(interface_to_usbdev(interface));

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, vb);
	dev_set_drvdata(&vb->pdev.dev, vb);

	/* get version information, major first, minor then */
	pipe = usb_rcvctrlpipe(vb->usb_dev, 0);
	ret = usb_control_msg(vb->usb_dev, pipe, VPRBRD_USB_REQUEST_MAJOR,
		VPRBRD_USB_TYPE_IN, 0x0000, 0x0000, buf, 1,
		VPRBRD_USB_TIMEOUT_MS);
	if (ret == 1)
		version = buf[0];

	ret = usb_control_msg(vb->usb_dev, pipe, VPRBRD_USB_REQUEST_MINOR,
		VPRBRD_USB_TYPE_IN, 0x0000, 0x0000, buf, 1,
		VPRBRD_USB_TIMEOUT_MS);
	if (ret == 1) {
		version <<= 8;
		version = version | buf[0];
	}

	dev_info(&interface->dev,
		 "version %x.%02x found at bus %03d address %03d\n",
		 version >> 8, version & 0xff,
		 vb->usb_dev->bus->busnum, vb->usb_dev->devnum);

	ret = mfd_add_devices(&interface->dev, -1, vprbrd_devs,
				ARRAY_SIZE(vprbrd_devs), NULL, 0, NULL);
	if (ret != 0) {
		dev_err(&interface->dev, "Failed to add mfd devices to core.");
		goto error;
	}

	return 0;

error:
	if (vb) {
		usb_put_dev(vb->usb_dev);
		kfree(vb);
	}

	return ret;
}

static void vprbrd_disconnect(struct usb_interface *interface)
{
	struct vprbrd *vb = usb_get_intfdata(interface);

	mfd_remove_devices(&interface->dev);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(vb->usb_dev);
	kfree(vb);

	dev_dbg(&interface->dev, "disconnected\n");
}

static struct usb_driver vprbrd_driver = {
	.name		= "viperboard",
	.probe		= vprbrd_probe,
	.disconnect	= vprbrd_disconnect,
	.id_table	= vprbrd_table,
};

module_usb_driver(vprbrd_driver);

MODULE_DESCRIPTION("Nano River Technologies viperboard mfd core driver");
MODULE_AUTHOR("Lars Poeschel <poeschel@lemonage.de>");
MODULE_LICENSE("GPL");
