/*
 * USB-ACPI glue code
 *
 * Copyright 2012 Red Hat <mjg@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <acpi/acpi_bus.h>

#include "usb.h"

static int usb_acpi_find_device(struct device *dev, acpi_handle *handle)
{
	struct usb_device *udev;
	struct device *parent;
	acpi_handle *parent_handle;

	if (!is_usb_device(dev))
		return -ENODEV;

	udev = to_usb_device(dev);
	parent = dev->parent;
	parent_handle = DEVICE_ACPI_HANDLE(parent);

	if (!parent_handle)
		return -ENODEV;

	*handle = acpi_get_child(parent_handle, udev->portnum);

	if (!*handle)
		return -ENODEV;

	return 0;
}

static struct acpi_bus_type usb_acpi_bus = {
	.bus = &usb_bus_type,
	.find_bridge = NULL,
	.find_device = usb_acpi_find_device,
};

int usb_acpi_register(void)
{
	return register_acpi_bus_type(&usb_acpi_bus);
}

void usb_acpi_unregister(void)
{
	unregister_acpi_bus_type(&usb_acpi_bus);
}
