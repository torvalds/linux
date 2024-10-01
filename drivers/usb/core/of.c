// SPDX-License-Identifier: GPL-2.0
/*
 * of.c		The helpers for hcd device tree support
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *	Author: Peter Chen <peter.chen@freescale.com>
 * Copyright (C) 2017 Johan Hovold <johan@kernel.org>
 */

#include <linux/of.h>
#include <linux/of_graph.h>
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

static bool usb_of_has_devices_or_graph(const struct usb_device *hub)
{
	const struct device_node *np = hub->dev.of_node;
	struct device_node *child;

	if (of_graph_is_present(np))
		return true;

	for_each_child_of_node(np, child) {
		if (of_property_present(child, "reg")) {
			of_node_put(child);
			return true;
		}
	}

	return false;
}

/**
 * usb_of_get_connect_type() - get a USB hub's port connect_type
 * @hub: hub to which port is for @port1
 * @port1: one-based index of port
 *
 * Get the connect_type of @port1 based on the device node for @hub. If the
 * port is described in the OF graph, the connect_type is "hotplug". If the
 * @hub has a child device has with a 'reg' property equal to @port1 the
 * connect_type is "hard-wired". If there isn't an OF graph or child node at
 * all then the connect_type is "unknown". Otherwise, the port is considered
 * "unused" because it isn't described at all.
 *
 * Return: A connect_type for @port1 based on the device node for @hub.
 */
enum usb_port_connect_type usb_of_get_connect_type(struct usb_device *hub, int port1)
{
	struct device_node *np, *child, *ep, *remote_np;
	enum usb_port_connect_type connect_type;

	/* Only set connect_type if binding has ports/hardwired devices. */
	if (!usb_of_has_devices_or_graph(hub))
		return USB_PORT_CONNECT_TYPE_UNKNOWN;

	/* Assume port is unused if there's a graph or a child node. */
	connect_type = USB_PORT_NOT_USED;

	np = hub->dev.of_node;
	/*
	 * Hotplug ports are connected to an available remote node, e.g.
	 * usb-a-connector compatible node, in the OF graph.
	 */
	if (of_graph_is_present(np)) {
		ep = of_graph_get_endpoint_by_regs(np, port1, -1);
		if (ep) {
			remote_np = of_graph_get_remote_port_parent(ep);
			of_node_put(ep);
			if (of_device_is_available(remote_np))
				connect_type = USB_PORT_CONNECT_TYPE_HOT_PLUG;
			of_node_put(remote_np);
		}
	}

	/*
	 * Hard-wired ports are child nodes with a reg property corresponding
	 * to the port number, i.e. a usb device.
	 */
	child = usb_of_get_device_node(hub, port1);
	if (of_device_is_available(child))
		connect_type = USB_PORT_CONNECT_TYPE_HARD_WIRED;
	of_node_put(child);

	return connect_type;
}
EXPORT_SYMBOL_GPL(usb_of_get_connect_type);

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
