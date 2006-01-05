/*
 * USB Serial Converter Bus specific functions
 *
 * Copyright (C) 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usb-serial.h"

static int usb_serial_device_match (struct device *dev, struct device_driver *drv)
{
	struct usb_serial_driver *driver;
	const struct usb_serial_port *port;

	/*
	 * drivers are already assigned to ports in serial_probe so it's
	 * a simple check here.
	 */
	port = to_usb_serial_port(dev);
	if (!port)
		return 0;

	driver = to_usb_serial_driver(drv);

	if (driver == port->serial->type)
		return 1;

	return 0;
}

static int usb_serial_device_probe (struct device *dev)
{
	struct usb_serial_driver *driver;
	struct usb_serial_port *port;
	int retval = 0;
	int minor;

	port = to_usb_serial_port(dev);
	if (!port) {
		retval = -ENODEV;
		goto exit;
	}

	driver = port->serial->type;
	if (driver->port_probe) {
		if (!try_module_get(driver->driver.owner)) {
			dev_err(dev, "module get failed, exiting\n");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_probe (port);
		module_put(driver->driver.owner);
		if (retval)
			goto exit;
	}

	minor = port->number;
	tty_register_device (usb_serial_tty_driver, minor, dev);
	dev_info(&port->serial->dev->dev, 
		 "%s converter now attached to ttyUSB%d\n",
		 driver->description, minor);

exit:
	return retval;
}

static int usb_serial_device_remove (struct device *dev)
{
	struct usb_serial_driver *driver;
	struct usb_serial_port *port;
	int retval = 0;
	int minor;

	port = to_usb_serial_port(dev);
	if (!port) {
		return -ENODEV;
	}

	driver = port->serial->type;
	if (driver->port_remove) {
		if (!try_module_get(driver->driver.owner)) {
			dev_err(dev, "module get failed, exiting\n");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_remove (port);
		module_put(driver->driver.owner);
	}
exit:
	minor = port->number;
	tty_unregister_device (usb_serial_tty_driver, minor);
	dev_info(dev, "%s converter now disconnected from ttyUSB%d\n",
		 driver->description, minor);

	return retval;
}

struct bus_type usb_serial_bus_type = {
	.name =		"usb-serial",
	.match =	usb_serial_device_match,
	.probe =	usb_serial_device_probe,
	.remove =	usb_serial_device_remove,
};

int usb_serial_bus_register(struct usb_serial_driver *driver)
{
	int retval;

	driver->driver.bus = &usb_serial_bus_type;
	retval = driver_register(&driver->driver);

	return retval;
}

void usb_serial_bus_deregister(struct usb_serial_driver *driver)
{
	driver_unregister(&driver->driver);
}

