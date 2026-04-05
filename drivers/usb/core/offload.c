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
	int ret = 0;

	if (!usb_get_dev(udev))
		return -ENODEV;

	if (pm_runtime_get_if_active(&udev->dev) != 1) {
		ret = -EBUSY;
		goto err_rpm;
	}

	spin_lock(&udev->offload_lock);

	if (udev->offload_pm_locked) {
		ret = -EAGAIN;
		goto err;
	}

	udev->offload_usage++;

err:
	spin_unlock(&udev->offload_lock);
	pm_runtime_put_autosuspend(&udev->dev);
err_rpm:
	usb_put_dev(udev);

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
	int ret = 0;

	if (!usb_get_dev(udev))
		return -ENODEV;

	if (pm_runtime_get_if_active(&udev->dev) != 1) {
		ret = -EBUSY;
		goto err_rpm;
	}

	spin_lock(&udev->offload_lock);

	if (udev->offload_pm_locked) {
		ret = -EAGAIN;
		goto err;
	}

	/* Drop the count when it wasn't 0, ignore the operation otherwise. */
	if (udev->offload_usage)
		udev->offload_usage--;

err:
	spin_unlock(&udev->offload_lock);
	pm_runtime_put_autosuspend(&udev->dev);
err_rpm:
	usb_put_dev(udev);

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
 * ensure the device itself and the downstream usb devices are all marked as
 * "offload_pm_locked" to ensure the correctness of the return value.
 *
 * Returns true on any offload activity, false otherwise.
 */
bool usb_offload_check(struct usb_device *udev) __must_hold(&udev->dev->mutex)
{
	struct usb_device *child;
	bool active = false;
	int port1;

	if (udev->offload_usage)
		return true;

	usb_hub_for_each_child(udev, port1, child) {
		usb_lock_device(child);
		active = usb_offload_check(child);
		usb_unlock_device(child);

		if (active)
			break;
	}

	return active;
}
EXPORT_SYMBOL_GPL(usb_offload_check);

/**
 * usb_offload_set_pm_locked - set the PM lock state of a USB device
 * @udev: the USB device to modify
 * @locked: the new lock state
 *
 * Setting @locked to true prevents offload_usage from being modified. This
 * ensures that offload activities cannot be started or stopped during critical
 * power management transitions, maintaining a stable state for the duration
 * of the transition.
 */
void usb_offload_set_pm_locked(struct usb_device *udev, bool locked)
{
	spin_lock(&udev->offload_lock);
	udev->offload_pm_locked = locked;
	spin_unlock(&udev->offload_lock);
}
EXPORT_SYMBOL_GPL(usb_offload_set_pm_locked);
