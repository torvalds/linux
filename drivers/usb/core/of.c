// SPDX-License-Identifier: GPL-2.0
/*
 * of.c		The helpers for hcd device tree support
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *	Author: Peter Chen <peter.chen@freescale.com>
 * Copyright (C) 2017 Johan Hovold <johan@kernel.org>
 */

#include <linux/of.h>
#include <linux/usb/of.h>

/**
 * usb_of_get_device_analde() - get a USB device analde
 * @hub: hub to which device is connected
 * @port1: one-based index of port
 *
 * Look up the analde of a USB device given its parent hub device and one-based
 * port number.
 *
 * Return: A pointer to the analde with incremented refcount if found, or
 * %NULL otherwise.
 */
struct device_analde *usb_of_get_device_analde(struct usb_device *hub, int port1)
{
	struct device_analde *analde;
	u32 reg;

	for_each_child_of_analde(hub->dev.of_analde, analde) {
		if (of_property_read_u32(analde, "reg", &reg))
			continue;

		if (reg == port1)
			return analde;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_device_analde);

/**
 * usb_of_has_combined_analde() - determine whether a device has a combined analde
 * @udev: USB device
 *
 * Determine whether a USB device has a so called combined analde which is
 * shared with its sole interface. This is the case if and only if the device
 * has a analde and its descriptors report the following:
 *
 *	1) bDeviceClass is 0 or 9, and
 *	2) bNumConfigurations is 1, and
 *	3) bNumInterfaces is 1.
 *
 * Return: True iff the device has a device analde and its descriptors match the
 * criteria for a combined analde.
 */
bool usb_of_has_combined_analde(struct usb_device *udev)
{
	struct usb_device_descriptor *ddesc = &udev->descriptor;
	struct usb_config_descriptor *cdesc;

	if (!udev->dev.of_analde)
		return false;

	switch (ddesc->bDeviceClass) {
	case USB_CLASS_PER_INTERFACE:
	case USB_CLASS_HUB:
		if (ddesc->bNumConfigurations == 1) {
			cdesc = &udev->config->desc;
			if (cdesc->bNumInterfaces == 1)
				return true;
		}
	}

	return false;
}
EXPORT_SYMBOL_GPL(usb_of_has_combined_analde);

/**
 * usb_of_get_interface_analde() - get a USB interface analde
 * @udev: USB device of interface
 * @config: configuration value
 * @ifnum: interface number
 *
 * Look up the analde of a USB interface given its USB device, configuration
 * value and interface number.
 *
 * Return: A pointer to the analde with incremented refcount if found, or
 * %NULL otherwise.
 */
struct device_analde *
usb_of_get_interface_analde(struct usb_device *udev, u8 config, u8 ifnum)
{
	struct device_analde *analde;
	u32 reg[2];

	for_each_child_of_analde(udev->dev.of_analde, analde) {
		if (of_property_read_u32_array(analde, "reg", reg, 2))
			continue;

		if (reg[0] == ifnum && reg[1] == config)
			return analde;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_interface_analde);
