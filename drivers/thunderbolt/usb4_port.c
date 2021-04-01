// SPDX-License-Identifier: GPL-2.0
/*
 * USB4 port device
 *
 * Copyright (C) 2021, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/pm_runtime.h>

#include "tb.h"

static ssize_t link_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);
	struct tb_port *port = usb4->port;
	struct tb *tb = port->sw->tb;
	const char *link;

	if (mutex_lock_interruptible(&tb->lock))
		return -ERESTARTSYS;

	if (tb_is_upstream_port(port))
		link = port->sw->link_usb4 ? "usb4" : "tbt";
	else if (tb_port_has_remote(port))
		link = port->remote->sw->link_usb4 ? "usb4" : "tbt";
	else
		link = "none";

	mutex_unlock(&tb->lock);

	return sysfs_emit(buf, "%s\n", link);
}
static DEVICE_ATTR_RO(link);

static struct attribute *common_attrs[] = {
	&dev_attr_link.attr,
	NULL
};

static const struct attribute_group common_group = {
	.attrs = common_attrs,
};

static const struct attribute_group *usb4_port_device_groups[] = {
	&common_group,
	NULL
};

static void usb4_port_device_release(struct device *dev)
{
	struct usb4_port *usb4 = container_of(dev, struct usb4_port, dev);

	kfree(usb4);
}

struct device_type usb4_port_device_type = {
	.name = "usb4_port",
	.groups = usb4_port_device_groups,
	.release = usb4_port_device_release,
};

/**
 * usb4_port_device_add() - Add USB4 port device
 * @port: Lane 0 adapter port to add the USB4 port
 *
 * Creates and registers a USB4 port device for @port. Returns the new
 * USB4 port device pointer or ERR_PTR() in case of error.
 */
struct usb4_port *usb4_port_device_add(struct tb_port *port)
{
	struct usb4_port *usb4;
	int ret;

	usb4 = kzalloc(sizeof(*usb4), GFP_KERNEL);
	if (!usb4)
		return ERR_PTR(-ENOMEM);

	usb4->port = port;
	usb4->dev.type = &usb4_port_device_type;
	usb4->dev.parent = &port->sw->dev;
	dev_set_name(&usb4->dev, "usb4_port%d", port->port);

	ret = device_register(&usb4->dev);
	if (ret) {
		put_device(&usb4->dev);
		return ERR_PTR(ret);
	}

	pm_runtime_no_callbacks(&usb4->dev);
	pm_runtime_set_active(&usb4->dev);
	pm_runtime_enable(&usb4->dev);
	pm_runtime_set_autosuspend_delay(&usb4->dev, TB_AUTOSUSPEND_DELAY);
	pm_runtime_mark_last_busy(&usb4->dev);
	pm_runtime_use_autosuspend(&usb4->dev);

	return usb4;
}

/**
 * usb4_port_device_remove() - Removes USB4 port device
 * @usb4: USB4 port device
 *
 * Unregisters the USB4 port device from the system. The device will be
 * released when the last reference is dropped.
 */
void usb4_port_device_remove(struct usb4_port *usb4)
{
	device_unregister(&usb4->dev);
}
