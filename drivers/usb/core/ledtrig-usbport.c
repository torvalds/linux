// SPDX-License-Identifier: GPL-2.0
/*
 * USB port LED trigger
 *
 * Copyright (C) 2016 Rafał Miłecki <rafal@milecki.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/of.h>

struct usbport_trig_data {
	struct led_classdev *led_cdev;
	struct list_head ports;
	struct notifier_block nb;
	int count; /* Amount of connected matching devices */
};

struct usbport_trig_port {
	struct usbport_trig_data *data;
	struct usb_device *hub;
	int portnum;
	char *port_name;
	bool observed;
	struct device_attribute attr;
	struct list_head list;
};

/***************************************
 * Helpers
 ***************************************/

/**
 * usbport_trig_usb_dev_observed - Check if dev is connected to observed port
 */
static bool usbport_trig_usb_dev_observed(struct usbport_trig_data *usbport_data,
					  struct usb_device *usb_dev)
{
	struct usbport_trig_port *port;

	if (!usb_dev->parent)
		return false;

	list_for_each_entry(port, &usbport_data->ports, list) {
		if (usb_dev->parent == port->hub &&
		    usb_dev->portnum == port->portnum)
			return port->observed;
	}

	return false;
}

static int usbport_trig_usb_dev_check(struct usb_device *usb_dev, void *data)
{
	struct usbport_trig_data *usbport_data = data;

	if (usbport_trig_usb_dev_observed(usbport_data, usb_dev))
		usbport_data->count++;

	return 0;
}

/**
 * usbport_trig_update_count - Recalculate amount of connected matching devices
 */
static void usbport_trig_update_count(struct usbport_trig_data *usbport_data)
{
	struct led_classdev *led_cdev = usbport_data->led_cdev;

	usbport_data->count = 0;
	usb_for_each_dev(usbport_data, usbport_trig_usb_dev_check);
	led_set_brightness(led_cdev, usbport_data->count ? LED_FULL : LED_OFF);
}

/***************************************
 * Device attr
 ***************************************/

static ssize_t usbport_trig_port_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct usbport_trig_port *port = container_of(attr,
						      struct usbport_trig_port,
						      attr);

	return sprintf(buf, "%d\n", port->observed) + 1;
}

static ssize_t usbport_trig_port_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct usbport_trig_port *port = container_of(attr,
						      struct usbport_trig_port,
						      attr);

	if (!strcmp(buf, "0") || !strcmp(buf, "0\n"))
		port->observed = 0;
	else if (!strcmp(buf, "1") || !strcmp(buf, "1\n"))
		port->observed = 1;
	else
		return -EINVAL;

	usbport_trig_update_count(port->data);

	return size;
}

static struct attribute *ports_attrs[] = {
	NULL,
};
static const struct attribute_group ports_group = {
	.name = "ports",
	.attrs = ports_attrs,
};

/***************************************
 * Adding & removing ports
 ***************************************/

/**
 * usbport_trig_port_observed - Check if port should be observed
 */
static bool usbport_trig_port_observed(struct usbport_trig_data *usbport_data,
				       struct usb_device *usb_dev, int port1)
{
	struct device *dev = usbport_data->led_cdev->dev;
	struct device_node *led_np = dev->of_node;
	struct of_phandle_args args;
	struct device_node *port_np;
	int count, i;

	if (!led_np)
		return false;

	/* Get node of port being added */
	port_np = usb_of_get_child_node(usb_dev->dev.of_node, port1);
	if (!port_np)
		return false;

	/* Amount of trigger sources for this LED */
	count = of_count_phandle_with_args(led_np, "trigger-sources",
					   "#trigger-source-cells");
	if (count < 0) {
		dev_warn(dev, "Failed to get trigger sources for %pOF\n",
			 led_np);
		return false;
	}

	/* Check list of sources for this specific port */
	for (i = 0; i < count; i++) {
		int err;

		err = of_parse_phandle_with_args(led_np, "trigger-sources",
						 "#trigger-source-cells", i,
						 &args);
		if (err) {
			dev_err(dev, "Failed to get trigger source phandle at index %d: %d\n",
				i, err);
			continue;
		}

		of_node_put(args.np);

		if (args.np == port_np)
			return true;
	}

	return false;
}

static int usbport_trig_add_port(struct usbport_trig_data *usbport_data,
				 struct usb_device *usb_dev,
				 const char *hub_name, int portnum)
{
	struct led_classdev *led_cdev = usbport_data->led_cdev;
	struct usbport_trig_port *port;
	size_t len;
	int err;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		goto err_out;
	}

	port->data = usbport_data;
	port->hub = usb_dev;
	port->portnum = portnum;
	port->observed = usbport_trig_port_observed(usbport_data, usb_dev,
						    portnum);

	len = strlen(hub_name) + 8;
	port->port_name = kzalloc(len, GFP_KERNEL);
	if (!port->port_name) {
		err = -ENOMEM;
		goto err_free_port;
	}
	snprintf(port->port_name, len, "%s-port%d", hub_name, portnum);

	sysfs_attr_init(&port->attr.attr);
	port->attr.attr.name = port->port_name;
	port->attr.attr.mode = S_IRUSR | S_IWUSR;
	port->attr.show = usbport_trig_port_show;
	port->attr.store = usbport_trig_port_store;

	err = sysfs_add_file_to_group(&led_cdev->dev->kobj, &port->attr.attr,
				      ports_group.name);
	if (err)
		goto err_free_port_name;

	list_add_tail(&port->list, &usbport_data->ports);

	return 0;

err_free_port_name:
	kfree(port->port_name);
err_free_port:
	kfree(port);
err_out:
	return err;
}

static int usbport_trig_add_usb_dev_ports(struct usb_device *usb_dev,
					  void *data)
{
	struct usbport_trig_data *usbport_data = data;
	int i;

	for (i = 1; i <= usb_dev->maxchild; i++)
		usbport_trig_add_port(usbport_data, usb_dev,
				      dev_name(&usb_dev->dev), i);

	return 0;
}

static void usbport_trig_remove_port(struct usbport_trig_data *usbport_data,
				     struct usbport_trig_port *port)
{
	struct led_classdev *led_cdev = usbport_data->led_cdev;

	list_del(&port->list);
	sysfs_remove_file_from_group(&led_cdev->dev->kobj, &port->attr.attr,
				     ports_group.name);
	kfree(port->port_name);
	kfree(port);
}

static void usbport_trig_remove_usb_dev_ports(struct usbport_trig_data *usbport_data,
					      struct usb_device *usb_dev)
{
	struct usbport_trig_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &usbport_data->ports, list) {
		if (port->hub == usb_dev)
			usbport_trig_remove_port(usbport_data, port);
	}
}

/***************************************
 * Init, exit, etc.
 ***************************************/

static int usbport_trig_notify(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct usbport_trig_data *usbport_data =
		container_of(nb, struct usbport_trig_data, nb);
	struct led_classdev *led_cdev = usbport_data->led_cdev;
	struct usb_device *usb_dev = data;
	bool observed;

	observed = usbport_trig_usb_dev_observed(usbport_data, usb_dev);

	switch (action) {
	case USB_DEVICE_ADD:
		usbport_trig_add_usb_dev_ports(usb_dev, usbport_data);
		if (observed && usbport_data->count++ == 0)
			led_set_brightness(led_cdev, LED_FULL);
		return NOTIFY_OK;
	case USB_DEVICE_REMOVE:
		usbport_trig_remove_usb_dev_ports(usbport_data, usb_dev);
		if (observed && --usbport_data->count == 0)
			led_set_brightness(led_cdev, LED_OFF);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static void usbport_trig_activate(struct led_classdev *led_cdev)
{
	struct usbport_trig_data *usbport_data;
	int err;

	usbport_data = kzalloc(sizeof(*usbport_data), GFP_KERNEL);
	if (!usbport_data)
		return;
	usbport_data->led_cdev = led_cdev;

	/* List of ports */
	INIT_LIST_HEAD(&usbport_data->ports);
	err = sysfs_create_group(&led_cdev->dev->kobj, &ports_group);
	if (err)
		goto err_free;
	usb_for_each_dev(usbport_data, usbport_trig_add_usb_dev_ports);
	usbport_trig_update_count(usbport_data);

	/* Notifications */
	usbport_data->nb.notifier_call = usbport_trig_notify,
	led_cdev->trigger_data = usbport_data;
	usb_register_notify(&usbport_data->nb);

	led_cdev->activated = true;
	return;

err_free:
	kfree(usbport_data);
}

static void usbport_trig_deactivate(struct led_classdev *led_cdev)
{
	struct usbport_trig_data *usbport_data = led_cdev->trigger_data;
	struct usbport_trig_port *port, *tmp;

	if (!led_cdev->activated)
		return;

	list_for_each_entry_safe(port, tmp, &usbport_data->ports, list) {
		usbport_trig_remove_port(usbport_data, port);
	}

	usb_unregister_notify(&usbport_data->nb);

	sysfs_remove_group(&led_cdev->dev->kobj, &ports_group);

	kfree(usbport_data);

	led_cdev->activated = false;
}

static struct led_trigger usbport_led_trigger = {
	.name     = "usbport",
	.activate = usbport_trig_activate,
	.deactivate = usbport_trig_deactivate,
};

static int __init usbport_trig_init(void)
{
	return led_trigger_register(&usbport_led_trigger);
}

static void __exit usbport_trig_exit(void)
{
	led_trigger_unregister(&usbport_led_trigger);
}

module_init(usbport_trig_init);
module_exit(usbport_trig_exit);

MODULE_AUTHOR("Rafał Miłecki <rafal@milecki.pl>");
MODULE_DESCRIPTION("USB port trigger");
MODULE_LICENSE("GPL v2");
