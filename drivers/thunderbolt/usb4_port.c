// SPDX-License-Identifier: GPL-2.0
/*
 * USB4 port device
 *
 * Copyright (C) 2021, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

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

static int usb4_port_offline(struct usb4_port *usb4)
{
	struct tb_port *port = usb4->port;
	int ret;

	ret = tb_acpi_power_on_retimers(port);
	if (ret)
		return ret;

	ret = usb4_port_router_offline(port);
	if (ret) {
		tb_acpi_power_off_retimers(port);
		return ret;
	}

	ret = tb_retimer_scan(port, false);
	if (ret) {
		usb4_port_router_online(port);
		tb_acpi_power_off_retimers(port);
	}

	return ret;
}

static void usb4_port_online(struct usb4_port *usb4)
{
	struct tb_port *port = usb4->port;

	usb4_port_router_online(port);
	tb_acpi_power_off_retimers(port);
}

static ssize_t offline_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);

	return sysfs_emit(buf, "%d\n", usb4->offline);
}

static ssize_t offline_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);
	struct tb_port *port = usb4->port;
	struct tb *tb = port->sw->tb;
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	pm_runtime_get_sync(&usb4->dev);

	if (mutex_lock_interruptible(&tb->lock)) {
		ret = -ERESTARTSYS;
		goto out_rpm;
	}

	if (val == usb4->offline)
		goto out_unlock;

	/* Offline mode works only for ports that are not connected */
	if (tb_port_has_remote(port)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	if (val) {
		ret = usb4_port_offline(usb4);
		if (ret)
			goto out_unlock;
	} else {
		usb4_port_online(usb4);
		tb_retimer_remove_all(port);
	}

	usb4->offline = val;
	tb_port_dbg(port, "%s offline mode\n", val ? "enter" : "exit");

out_unlock:
	mutex_unlock(&tb->lock);
out_rpm:
	pm_runtime_mark_last_busy(&usb4->dev);
	pm_runtime_put_autosuspend(&usb4->dev);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(offline);

static ssize_t rescan_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);
	struct tb_port *port = usb4->port;
	struct tb *tb = port->sw->tb;
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	if (!val)
		return count;

	pm_runtime_get_sync(&usb4->dev);

	if (mutex_lock_interruptible(&tb->lock)) {
		ret = -ERESTARTSYS;
		goto out_rpm;
	}

	/* Must be in offline mode already */
	if (!usb4->offline) {
		ret = -EINVAL;
		goto out_unlock;
	}

	tb_retimer_remove_all(port);
	ret = tb_retimer_scan(port, true);

out_unlock:
	mutex_unlock(&tb->lock);
out_rpm:
	pm_runtime_mark_last_busy(&usb4->dev);
	pm_runtime_put_autosuspend(&usb4->dev);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(rescan);

static struct attribute *service_attrs[] = {
	&dev_attr_offline.attr,
	&dev_attr_rescan.attr,
	NULL
};

static umode_t service_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);

	/*
	 * Always need some platform help to cycle the modes so that
	 * retimers can be accessed through the sideband.
	 */
	return usb4->can_offline ? attr->mode : 0;
}

static const struct attribute_group service_group = {
	.attrs = service_attrs,
	.is_visible = service_attr_is_visible,
};

static const struct attribute_group *usb4_port_device_groups[] = {
	&common_group,
	&service_group,
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

/**
 * usb4_port_device_resume() - Resumes USB4 port device
 * @usb4: USB4 port device
 *
 * Used to resume USB4 port device after sleep state.
 */
int usb4_port_device_resume(struct usb4_port *usb4)
{
	return usb4->offline ? usb4_port_offline(usb4) : 0;
}
