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
#include <linux/pm_qos.h>

#include "hub.h"

static const struct attribute_group *port_dev_group[];

static ssize_t connect_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);
	char *result;

	switch (port_dev->connect_type) {
	case USB_PORT_CONNECT_TYPE_HOT_PLUG:
		result = "hotplug";
		break;
	case USB_PORT_CONNECT_TYPE_HARD_WIRED:
		result = "hardwired";
		break;
	case USB_PORT_NOT_USED:
		result = "not used";
		break;
	default:
		result = "unknown";
		break;
	}

	return sprintf(buf, "%s\n", result);
}
static DEVICE_ATTR_RO(connect_type);

static struct attribute *port_dev_attrs[] = {
	&dev_attr_connect_type.attr,
	NULL,
};

static struct attribute_group port_dev_attr_grp = {
	.attrs = port_dev_attrs,
};

static const struct attribute_group *port_dev_group[] = {
	&port_dev_attr_grp,
	NULL,
};

static void usb_port_device_release(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);

	kfree(port_dev);
}

#ifdef CONFIG_PM_RUNTIME
static int usb_port_runtime_resume(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *hdev = to_usb_device(dev->parent->parent);
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_hub *hub = usb_hub_to_struct_hub(hdev);
	int port1 = port_dev->portnum;
	int retval;

	if (!hub)
		return -EINVAL;
	if (hub->in_reset) {
		port_dev->power_is_on = 1;
		return 0;
	}

	usb_autopm_get_interface(intf);
	set_bit(port1, hub->busy_bits);

	retval = usb_hub_set_port_power(hdev, hub, port1, true);
	if (port_dev->child && !retval) {
		/*
		 * Attempt to wait for usb hub port to be reconnected in order
		 * to make the resume procedure successful.  The device may have
		 * disconnected while the port was powered off, so ignore the
		 * return status.
		 */
		retval = hub_port_debounce_be_connected(hub, port1);
		if (retval < 0)
			dev_dbg(&port_dev->dev, "can't get reconnection after setting port  power on, status %d\n",
					retval);
		usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_ENABLE);
		retval = 0;
	}

	clear_bit(port1, hub->busy_bits);
	usb_autopm_put_interface(intf);
	return retval;
}

static int usb_port_runtime_suspend(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *hdev = to_usb_device(dev->parent->parent);
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_hub *hub = usb_hub_to_struct_hub(hdev);
	int port1 = port_dev->portnum;
	int retval;

	if (!hub)
		return -EINVAL;
	if (hub->in_reset)
		return -EBUSY;

	if (dev_pm_qos_flags(&port_dev->dev, PM_QOS_FLAG_NO_POWER_OFF)
			== PM_QOS_FLAGS_ALL)
		return -EAGAIN;

	usb_autopm_get_interface(intf);
	set_bit(port1, hub->busy_bits);
	retval = usb_hub_set_port_power(hdev, hub, port1, false);
	usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_CONNECTION);
	usb_clear_port_feature(hdev, port1,	USB_PORT_FEAT_C_ENABLE);
	clear_bit(port1, hub->busy_bits);
	usb_autopm_put_interface(intf);
	return retval;
}
#endif

static const struct dev_pm_ops usb_port_pm_ops = {
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend =	usb_port_runtime_suspend,
	.runtime_resume =	usb_port_runtime_resume,
#endif
};

struct device_type usb_port_device_type = {
	.name =		"usb_port",
	.release =	usb_port_device_release,
	.pm =		&usb_port_pm_ops,
};

static struct device_driver usb_port_driver = {
	.name = "usb",
	.owner = THIS_MODULE,
};

static void link_peers(struct usb_port *left, struct usb_port *right)
{
	if (left->peer == right && right->peer == left)
		return;

	if (left->peer || right->peer) {
		struct usb_port *lpeer = left->peer;
		struct usb_port *rpeer = right->peer;

		WARN(1, "failed to peer %s and %s (%s -> %p) (%s -> %p)\n",
			dev_name(&left->dev), dev_name(&right->dev),
			dev_name(&left->dev), lpeer,
			dev_name(&right->dev), rpeer);
		return;
	}

	left->peer = right;
	right->peer = left;
}

static void unlink_peers(struct usb_port *left, struct usb_port *right)
{
	WARN(right->peer != left || left->peer != right,
			"%s and %s are not peers?\n",
			dev_name(&left->dev), dev_name(&right->dev));

	right->peer = NULL;
	left->peer = NULL;
}

/*
 * Set the default peer port for root hubs, or via the upstream peer
 * relationship for all other hubs
 */
static void find_and_link_peer(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = hub->ports[port1 - 1], *peer;
	struct usb_device *hdev = hub->hdev;
	struct usb_device *peer_hdev;
	struct usb_hub *peer_hub;

	if (!hdev->parent) {
		struct usb_hcd *hcd = bus_to_hcd(hdev->bus);
		struct usb_hcd *peer_hcd = hcd->shared_hcd;

		if (!peer_hcd)
			return;

		peer_hdev = peer_hcd->self.root_hub;
	} else {
		struct usb_port *upstream;
		struct usb_device *parent = hdev->parent;
		struct usb_hub *parent_hub = usb_hub_to_struct_hub(parent);

		if (!parent_hub)
			return;

		upstream = parent_hub->ports[hdev->portnum - 1];
		if (!upstream || !upstream->peer)
			return;

		peer_hdev = upstream->peer->child;
	}

	peer_hub = usb_hub_to_struct_hub(peer_hdev);
	if (!peer_hub || port1 > peer_hdev->maxchild)
		return;

	peer = peer_hub->ports[port1 - 1];
	if (peer)
		link_peers(port_dev, peer);
}

int usb_hub_create_port_device(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev;
	int retval;

	port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
	if (!port_dev) {
		retval = -ENOMEM;
		goto exit;
	}

	hub->ports[port1 - 1] = port_dev;
	port_dev->portnum = port1;
	port_dev->power_is_on = true;
	port_dev->dev.parent = hub->intfdev;
	port_dev->dev.groups = port_dev_group;
	port_dev->dev.type = &usb_port_device_type;
	port_dev->dev.driver = &usb_port_driver;
	dev_set_name(&port_dev->dev, "%s-port%d", dev_name(&hub->hdev->dev),
			port1);
	retval = device_register(&port_dev->dev);
	if (retval)
		goto error_register;

	find_and_link_peer(hub, port1);

	pm_runtime_set_active(&port_dev->dev);

	/*
	 * Do not enable port runtime pm if the hub does not support
	 * power switching.  Also, userspace must have final say of
	 * whether a port is permitted to power-off.  Do not enable
	 * runtime pm if we fail to expose pm_qos_no_power_off.
	 */
	if (hub_is_port_power_switchable(hub)
			&& dev_pm_qos_expose_flags(&port_dev->dev,
			PM_QOS_FLAG_NO_POWER_OFF) == 0)
		pm_runtime_enable(&port_dev->dev);

	device_enable_async_suspend(&port_dev->dev);
	return 0;

error_register:
	put_device(&port_dev->dev);
exit:
	return retval;
}

void usb_hub_remove_port_device(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = hub->ports[port1 - 1];
	struct usb_port *peer;

	peer = port_dev->peer;
	if (peer)
		unlink_peers(port_dev, peer);
	device_unregister(&port_dev->dev);
}
