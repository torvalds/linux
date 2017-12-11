// SPDX-License-Identifier: GPL-2.0
/*
 * of.c		The helpers for hcd device tree support
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Author: Peter Chen <peter.chen@freescale.com>
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
