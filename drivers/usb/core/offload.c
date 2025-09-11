// SPDX-License-Identifier: GPL-2.0

/*
 * offload.c - USB offload related functions
 *
 * Copyright (c) 2025, Google LLC.
 *
 * Author: Guan-Yu Lin
 */

#include <linux/usb.h>

#include "usb.h"

/**
 * usb_offload_get - increment the offload_usage of a USB device
 * @udev: the USB device to increment its offload_usage
 *
 * Incrementing the offload_usage of a usb_device indicates that offload is
 * enabled on this usb_device; that is, another entity is actively handling USB
 * transfers. This information allows the USB driver to adjust its power
 * management policy based on offload activity.
 *
 * Return: 0 on success. A negative error code otherwise.
 */
int usb_offload_get(struct usb_device *udev)
{
	int ret;

	usb_lock_device(udev);
	if (udev->state == USB_STATE_NOTATTACHED) {
		usb_unlock_device(udev);
		return -ENODEV;
	}

	if (udev->state == USB_STATE_SUSPENDED ||
		   udev->offload_at_suspend) {
		usb_unlock_device(udev);
		return -EBUSY;
	}

	/*
	 * offload_usage could only be modified when the device is active, since
	 * it will alter the suspend flow of the device.
	 */
	ret = usb_autoresume_device(udev);
	if (ret < 0) {
		usb_unlock_device(udev);
		return ret;
	}

	udev->offload_usage++;
	usb_autosuspend_device(udev);
	usb_unlock_device(udev);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_offload_get);

/**
 * usb_offload_put - drop the offload_usage of a USB device
 * @udev: the USB device to drop its offload_usage
 *
 * The inverse operation of usb_offload_get, which drops the offload_usage of
 * a USB device. This information allows the USB driver to adjust its power
 * management policy based on offload activity.
 *
 * Return: 0 on success. A negative error code otherwise.
 */
int usb_offload_put(struct usb_device *udev)
{
	int ret;

	usb_lock_device(udev);
	if (udev->state == USB_STATE_NOTATTACHED) {
		usb_unlock_device(udev);
		return -ENODEV;
	}

	if (udev->state == USB_STATE_SUSPENDED ||
		   udev->offload_at_suspend) {
		usb_unlock_device(udev);
		return -EBUSY;
	}

	/*
	 * offload_usage could only be modified when the device is active, since
	 * it will alter the suspend flow of the device.
	 */
	ret = usb_autoresume_device(udev);
	if (ret < 0) {
		usb_unlock_device(udev);
		return ret;
	}

	/* Drop the count when it wasn't 0, ignore the operation otherwise. */
	if (udev->offload_usage)
		udev->offload_usage--;
	usb_autosuspend_device(udev);
	usb_unlock_device(udev);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_offload_put);

/**
 * usb_offload_check - check offload activities on a USB device
 * @udev: the USB device to check its offload activity.
 *
 * Check if there are any offload activity on the USB device right now. This
 * information could be used for power management or other forms of resource
 * management.
 *
 * The caller must hold @udev's device lock. In addition, the caller should
 * ensure downstream usb devices are all either suspended or marked as
 * "offload_at_suspend" to ensure the correctness of the return value.
 *
 * Returns true on any offload activity, false otherwise.
 */
bool usb_offload_check(struct usb_device *udev) __must_hold(&udev->dev->mutex)
{
	struct usb_device *child;
	bool active;
	int port1;

	usb_hub_for_each_child(udev, port1, child) {
		usb_lock_device(child);
		active = usb_offload_check(child);
		usb_unlock_device(child);
		if (active)
			return true;
	}

	return !!udev->offload_usage;
}
EXPORT_SYMBOL_GPL(usb_offload_check);
