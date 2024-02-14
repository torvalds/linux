// SPDX-License-Identifier: GPL-2.0
/*
 * of.c		The helpers for hcd device tree support
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *	Author: Peter Chen <peter.chen@freescale.com>
 * Copyright (C) 2017 Johan Hovold <johan@kernel.org>
 */

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/usb/of.h>

/**
 * usb_of_get_device_node() - get a USB device node
 * @hub: hub to which device is connected
 * @port1: one-based index of port
 *
 * Look up the node of a USB device given its parent hub device and one-based
 * port number.
 *
 * Return: A pointer to the node with incremented refcount if found, or
 * %NULL otherwise.
 */
struct device_node *usb_of_get_device_node(struct usb_device *hub, int port1)
{
	struct device_node *node;
	u32 reg;

	for_each_child_of_node(hub->dev.of_node, node) {
		if (of_property_read_u32(node, "reg", &reg))
			continue;

		if (reg == port1)
			return node;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_device_node);

/**
 * usb_of_has_combined_node() - determine whether a device has a combined node
 * @udev: USB device
 *
 * Determine whether a USB device has a so called combined node which is
 * shared with its sole interface. This is the case if and only if the device
 * has a node and its descriptors report the following:
 *
 *	1) bDeviceClass is 0 or 9, and
 *	2) bNumConfigurations is 1, and
 *	3) bNumInterfaces is 1.
 *
 * Return: True iff the device has a device node and its descriptors match the
 * criteria for a combined node.
 */
bool usb_of_has_combined_node(struct usb_device *udev)
{
	struct usb_device_descriptor *ddesc = &udev->descriptor;
	struct usb_config_descriptor *cdesc;

	if (!udev->dev.of_node)
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
EXPORT_SYMBOL_GPL(usb_of_has_combined_node);

/**
 * usb_of_get_interface_node() - get a USB interface node
 * @udev: USB device of interface
 * @config: configuration value
 * @ifnum: interface number
 *
 * Look up the node of a USB interface given its USB device, configuration
 * value and interface number.
 *
 * Return: A pointer to the node with incremented refcount if found, or
 * %NULL otherwise.
 */
struct device_node *
usb_of_get_interface_node(struct usb_device *udev, u8 config, u8 ifnum)
{
	struct device_node *node;
	u32 reg[2];

	for_each_child_of_node(udev->dev.of_node, node) {
		if (of_property_read_u32_array(node, "reg", reg, 2))
			continue;

		if (reg[0] == ifnum && reg[1] == config)
			return node;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_interface_node);
