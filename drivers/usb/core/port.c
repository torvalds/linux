/*
 * usb port device code
 *
 * Copyright (C) 2012 Intel Corp
 *
 * Author: Lan Tianyu <tianyu.lan@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/slab.h>

#include "hub.h"

static void usb_port_device_release(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);

	kfree(port_dev);
}

struct device_type usb_port_device_type = {
	.name =		"usb_port",
	.release =	usb_port_device_release,
};

int usb_hub_create_port_device(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = NULL;
	int retval;

	port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
	if (!port_dev) {
		retval = -ENOMEM;
		goto exit;
	}

	hub->ports[port1 - 1] = port_dev;
	port_dev->dev.parent = hub->intfdev;
	port_dev->dev.type = &usb_port_device_type;
	dev_set_name(&port_dev->dev, "port%d", port1);

	retval = device_register(&port_dev->dev);
	if (retval)
		goto error_register;

	return 0;

error_register:
	put_device(&port_dev->dev);
exit:
	return retval;
}

void usb_hub_remove_port_device(struct usb_hub *hub,
				       int port1)
{
	device_unregister(&hub->ports[port1 - 1]->dev);
}

