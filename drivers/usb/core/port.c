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
	struct usb_device *udev = port_dev->child;
	struct usb_port *peer = port_dev->peer;
	int port1 = port_dev->portnum;
	int retval;

	if (!hub)
		return -EINVAL;
	if (hub->in_reset) {
		set_bit(port1, hub->power_bits);
		return 0;
	}

	/*
	 * Power on our usb3 peer before this usb2 port to prevent a usb3
	 * device from degrading to its usb2 connection
	 */
	if (!port_dev->is_superspeed && peer)
		pm_runtime_get_sync(&peer->dev);

	usb_autopm_get_interface(intf);
	retval = usb_hub_set_port_power(hdev, hub, port1, true);
	msleep(hub_power_on_good_delay(hub));
	if (udev && !retval) {
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
		retval = 0;

		/* Force the child awake to revalidate after the power loss. */
		if (!test_and_set_bit(port1, hub->child_usage_bits)) {
			pm_runtime_get_noresume(&port_dev->dev);
			pm_request_resume(&udev->dev);
		}
	}

	usb_autopm_put_interface(intf);

	return retval;
}

static int usb_port_runtime_suspend(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *hdev = to_usb_device(dev->parent->parent);
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_hub *hub = usb_hub_to_struct_hub(hdev);
	struct usb_port *peer = port_dev->peer;
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
	retval = usb_hub_set_port_power(hdev, hub, port1, false);
	usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_CONNECTION);
	if (!port_dev->is_superspeed)
		usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_ENABLE);
	usb_autopm_put_interface(intf);

	/*
	 * Our peer usb3 port may now be able to suspend, so
	 * asynchronously queue a suspend request to observe that this
	 * usb2 port is now off.
	 */
	if (!port_dev->is_superspeed && peer)
		pm_runtime_put(&peer->dev);

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

static int link_peers(struct usb_port *left, struct usb_port *right)
{
	struct usb_port *ss_port, *hs_port;
	int rc;

	if (left->peer == right && right->peer == left)
		return 0;

	if (left->peer || right->peer) {
		struct usb_port *lpeer = left->peer;
		struct usb_port *rpeer = right->peer;

		WARN(1, "failed to peer %s and %s (%s -> %p) (%s -> %p)\n",
			dev_name(&left->dev), dev_name(&right->dev),
			dev_name(&left->dev), lpeer,
			dev_name(&right->dev), rpeer);
		return -EBUSY;
	}

	rc = sysfs_create_link(&left->dev.kobj, &right->dev.kobj, "peer");
	if (rc)
		return rc;
	rc = sysfs_create_link(&right->dev.kobj, &left->dev.kobj, "peer");
	if (rc) {
		sysfs_remove_link(&left->dev.kobj, "peer");
		return rc;
	}

	/*
	 * We need to wake the HiSpeed port to make sure we don't race
	 * setting ->peer with usb_port_runtime_suspend().  Otherwise we
	 * may miss a suspend event for the SuperSpeed port.
	 */
	if (left->is_superspeed) {
		ss_port = left;
		WARN_ON(right->is_superspeed);
		hs_port = right;
	} else {
		ss_port = right;
		WARN_ON(!right->is_superspeed);
		hs_port = left;
	}
	pm_runtime_get_sync(&hs_port->dev);

	left->peer = right;
	right->peer = left;

	/*
	 * The SuperSpeed reference is dropped when the HiSpeed port in
	 * this relationship suspends, i.e. when it is safe to allow a
	 * SuperSpeed connection to drop since there is no risk of a
	 * device degrading to its powered-off HiSpeed connection.
	 *
	 * Also, drop the HiSpeed ref taken above.
	 */
	pm_runtime_get_sync(&ss_port->dev);
	pm_runtime_put(&hs_port->dev);

	return 0;
}

static void link_peers_report(struct usb_port *left, struct usb_port *right)
{
	int rc;

	rc = link_peers(left, right);
	if (rc == 0) {
		dev_dbg(&left->dev, "peered to %s\n", dev_name(&right->dev));
	} else {
		dev_warn(&left->dev, "failed to peer to %s (%d)\n",
				dev_name(&right->dev), rc);
		pr_warn_once("usb: port power management may be unreliable\n");
	}
}

static void unlink_peers(struct usb_port *left, struct usb_port *right)
{
	struct usb_port *ss_port, *hs_port;

	WARN(right->peer != left || left->peer != right,
			"%s and %s are not peers?\n",
			dev_name(&left->dev), dev_name(&right->dev));

	/*
	 * We wake the HiSpeed port to make sure we don't race its
	 * usb_port_runtime_resume() event which takes a SuperSpeed ref
	 * when ->peer is !NULL.
	 */
	if (left->is_superspeed) {
		ss_port = left;
		hs_port = right;
	} else {
		ss_port = right;
		hs_port = left;
	}

	pm_runtime_get_sync(&hs_port->dev);

	sysfs_remove_link(&left->dev.kobj, "peer");
	right->peer = NULL;
	sysfs_remove_link(&right->dev.kobj, "peer");
	left->peer = NULL;

	/* Drop the SuperSpeed ref held on behalf of the active HiSpeed port */
	pm_runtime_put(&ss_port->dev);

	/* Drop the ref taken above */
	pm_runtime_put(&hs_port->dev);
}

/*
 * For each usb hub device in the system check to see if it is in the
 * peer domain of the given port_dev, and if it is check to see if it
 * has a port that matches the given port by location
 */
static int match_location(struct usb_device *peer_hdev, void *p)
{
	int port1;
	struct usb_hcd *hcd, *peer_hcd;
	struct usb_port *port_dev = p, *peer;
	struct usb_hub *peer_hub = usb_hub_to_struct_hub(peer_hdev);
	struct usb_device *hdev = to_usb_device(port_dev->dev.parent->parent);

	if (!peer_hub)
		return 0;

	hcd = bus_to_hcd(hdev->bus);
	peer_hcd = bus_to_hcd(peer_hdev->bus);
	/* peer_hcd is provisional until we verify it against the known peer */
	if (peer_hcd != hcd->shared_hcd)
		return 0;

	for (port1 = 1; port1 <= peer_hdev->maxchild; port1++) {
		peer = peer_hub->ports[port1 - 1];
		if (peer && peer->location == port_dev->location) {
			link_peers_report(port_dev, peer);
			return 1; /* done */
		}
	}

	return 0;
}

/*
 * Find the peer port either via explicit platform firmware "location"
 * data, the peer hcd for root hubs, or the upstream peer relationship
 * for all other hubs.
 */
static void find_and_link_peer(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = hub->ports[port1 - 1], *peer;
	struct usb_device *hdev = hub->hdev;
	struct usb_device *peer_hdev;
	struct usb_hub *peer_hub;

	/*
	 * If location data is available then we can only peer this port
	 * by a location match, not the default peer (lest we create a
	 * situation where we need to go back and undo a default peering
	 * when the port is later peered by location data)
	 */
	if (port_dev->location) {
		/* we link the peer in match_location() if found */
		usb_for_each_dev(port_dev, match_location);
		return;
	} else if (!hdev->parent) {
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

	/*
	 * we found a valid default peer, last check is to make sure it
	 * does not have location data
	 */
	peer = peer_hub->ports[port1 - 1];
	if (peer && peer->location == 0)
		link_peers_report(port_dev, peer);
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
	set_bit(port1, hub->power_bits);
	port_dev->dev.parent = hub->intfdev;
	port_dev->dev.groups = port_dev_group;
	port_dev->dev.type = &usb_port_device_type;
	port_dev->dev.driver = &usb_port_driver;
	if (hub_is_superspeed(hub->hdev))
		port_dev->is_superspeed = 1;
	dev_set_name(&port_dev->dev, "%s-port%d", dev_name(&hub->hdev->dev),
			port1);
	mutex_init(&port_dev->status_lock);
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
