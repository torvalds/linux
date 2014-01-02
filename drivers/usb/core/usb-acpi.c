/*
 * USB-ACPI glue code
 *
 * Copyright 2012 Red Hat <mjg@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/usb/hcd.h>
#include <acpi/acpi_bus.h>

#include "usb.h"

/**
 * usb_acpi_power_manageable - check whether usb port has
 * acpi power resource.
 * @hdev: USB device belonging to the usb hub
 * @index: port index based zero
 *
 * Return true if the port has acpi power resource and false if no.
 */
bool usb_acpi_power_manageable(struct usb_device *hdev, int index)
{
	acpi_handle port_handle;
	int port1 = index + 1;

	port_handle = usb_get_hub_port_acpi_handle(hdev,
		port1);
	if (port_handle)
		return acpi_bus_power_manageable(port_handle);
	else
		return false;
}
EXPORT_SYMBOL_GPL(usb_acpi_power_manageable);

/**
 * usb_acpi_set_power_state - control usb port's power via acpi power
 * resource
 * @hdev: USB device belonging to the usb hub
 * @index: port index based zero
 * @enable: power state expected to be set
 *
 * Notice to use usb_acpi_power_manageable() to check whether the usb port
 * has acpi power resource before invoking this function.
 *
 * Returns 0 on success, else negative errno.
 */
int usb_acpi_set_power_state(struct usb_device *hdev, int index, bool enable)
{
	acpi_handle port_handle;
	unsigned char state;
	int port1 = index + 1;
	int error = -EINVAL;

	port_handle = (acpi_handle)usb_get_hub_port_acpi_handle(hdev,
		port1);
	if (!port_handle)
		return error;

	if (enable)
		state = ACPI_STATE_D0;
	else
		state = ACPI_STATE_D3_COLD;

	error = acpi_bus_set_power(port_handle, state);
	if (!error)
		dev_dbg(&hdev->dev, "The power of hub port %d was set to %d\n",
			port1, enable);
	else
		dev_dbg(&hdev->dev, "The power of hub port failed to be set\n");

	return error;
}
EXPORT_SYMBOL_GPL(usb_acpi_set_power_state);

static int usb_acpi_check_port_connect_type(struct usb_device *hdev,
	acpi_handle handle, int port1)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *upc;
	struct acpi_pld_info *pld;
	int ret = 0;

	/*
	 * Accoding to ACPI Spec 9.13. PLD indicates whether usb port is
	 * user visible and _UPC indicates whether it is connectable. If
	 * the port was visible and connectable, it could be freely connected
	 * and disconnected with USB devices. If no visible and connectable,
	 * a usb device is directly hard-wired to the port. If no visible and
	 * no connectable, the port would be not used.
	 */
	status = acpi_get_physical_device_location(handle, &pld);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	status = acpi_evaluate_object(handle, "_UPC", NULL, &buffer);
	upc = buffer.pointer;
	if (!upc || (upc->type != ACPI_TYPE_PACKAGE)
		|| upc->package.count != 4) {
		ret = -EINVAL;
		goto out;
	}

	if (upc->package.elements[0].integer.value)
		if (pld->user_visible)
			usb_set_hub_port_connect_type(hdev, port1,
				USB_PORT_CONNECT_TYPE_HOT_PLUG);
		else
			usb_set_hub_port_connect_type(hdev, port1,
				USB_PORT_CONNECT_TYPE_HARD_WIRED);
	else if (!pld->user_visible)
		usb_set_hub_port_connect_type(hdev, port1, USB_PORT_NOT_USED);

out:
	ACPI_FREE(pld);
	kfree(upc);
	return ret;
}

static int usb_acpi_find_device(struct device *dev, acpi_handle *handle)
{
	struct usb_device *udev;
	acpi_handle *parent_handle;
	int port_num;

	/*
	 * In the ACPI DSDT table, only usb root hub and usb ports are
	 * acpi device nodes. The hierarchy like following.
	 * Device (EHC1)
	 *	Device (HUBN)
	 *		Device (PR01)
	 *			Device (PR11)
	 *			Device (PR12)
	 *			Device (PR13)
	 *			...
	 * So all binding process is divided into two parts. binding
	 * root hub and usb ports.
	 */
	if (is_usb_device(dev)) {
		udev = to_usb_device(dev);
		if (udev->parent) {
			enum usb_port_connect_type type;

			/*
			 * According usb port's connect type to set usb device's
			 * removability.
			 */
			type = usb_get_hub_port_connect_type(udev->parent,
				udev->portnum);
			switch (type) {
			case USB_PORT_CONNECT_TYPE_HOT_PLUG:
				udev->removable = USB_DEVICE_REMOVABLE;
				break;
			case USB_PORT_CONNECT_TYPE_HARD_WIRED:
				udev->removable = USB_DEVICE_FIXED;
				break;
			default:
				udev->removable = USB_DEVICE_REMOVABLE_UNKNOWN;
				break;
			}

			return -ENODEV;
		}

		/* root hub's parent is the usb hcd. */
		parent_handle = ACPI_HANDLE(dev->parent);
		*handle = acpi_get_child(parent_handle, udev->portnum);
		if (!*handle)
			return -ENODEV;
		return 0;
	} else if (is_usb_port(dev)) {
		sscanf(dev_name(dev), "port%d", &port_num);
		/* Get the struct usb_device point of port's hub */
		udev = to_usb_device(dev->parent->parent);

		/*
		 * The root hub ports' parent is the root hub. The non-root-hub
		 * ports' parent is the parent hub port which the hub is
		 * connected to.
		 */
		if (!udev->parent) {
			struct usb_hcd *hcd = bus_to_hcd(udev->bus);
			int raw_port_num;

			raw_port_num = usb_hcd_find_raw_port_number(hcd,
				port_num);
			*handle = acpi_get_child(ACPI_HANDLE(&udev->dev),
				raw_port_num);
			if (!*handle)
				return -ENODEV;
		} else {
			parent_handle =
				usb_get_hub_port_acpi_handle(udev->parent,
				udev->portnum);
			if (!parent_handle)
				return -ENODEV;

			*handle = acpi_get_child(parent_handle,	port_num);
			if (!*handle)
				return -ENODEV;
		}
		usb_acpi_check_port_connect_type(udev, *handle, port_num);
	} else
		return -ENODEV;

	return 0;
}

static bool usb_acpi_bus_match(struct device *dev)
{
	return is_usb_device(dev) || is_usb_port(dev);
}

static struct acpi_bus_type usb_acpi_bus = {
	.name = "USB",
	.match = usb_acpi_bus_match,
	.find_device = usb_acpi_find_device,
};

int usb_acpi_register(void)
{
	return register_acpi_bus_type(&usb_acpi_bus);
}

void usb_acpi_unregister(void)
{
	unregister_acpi_bus_type(&usb_acpi_bus);
}
