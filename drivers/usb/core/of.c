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
 * usb_of_get_child_node - Find the device node match port number
 * @parent: the parent device node
 * @portnum: the port number which device is connecting
 *
 * Find the node from device tree according to its port number.
 *
 * Return: A pointer to the node with incremented refcount if found, or
 * %NULL otherwise.
 */
struct device_node *usb_of_get_child_node(struct device_node *parent,
					int portnum)
{
	struct device_node *node;
	u32 port;

	for_each_child_of_node(parent, node) {
		if (!of_property_read_u32(node, "reg", &port)) {
			if (port == portnum)
				return node;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_child_node);

/**
 * usb_of_has_combined_node() - determine whether a device has a combined node
 * @udev: USB device
 *
 * Determine whether a USB device has a so called combined node which is
 * shared with its sole interface. This is the case if and only if the device
 * has a node and its decriptors report the following:
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

/**
 * usb_of_get_companion_dev - Find the companion device
 * @dev: the device pointer to find a companion
 *
 * Find the companion device from platform bus.
 *
 * Takes a reference to the returned struct device which needs to be dropped
 * after use.
 *
 * Return: On success, a pointer to the companion device, %NULL on failure.
 */
struct device *usb_of_get_companion_dev(struct device *dev)
{
	struct device_node *node;
	struct platform_device *pdev = NULL;

	node = of_parse_phandle(dev->of_node, "companion", 0);
	if (node)
		pdev = of_find_device_by_node(node);

	of_node_put(node);

	return pdev ? &pdev->dev : NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_companion_dev);
