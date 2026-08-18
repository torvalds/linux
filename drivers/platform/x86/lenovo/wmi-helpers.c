// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo Legion WMI helpers driver.
 *
 * The Lenovo Legion WMI interface is broken up into multiple GUID interfaces
 * that require cross-references between GUID's for some functionality. The
 * "Custom Mode" interface is a legacy interface for managing and displaying
 * CPU & GPU power and hwmon settings and readings. The "Other Mode" interface
 * is a modern interface that replaces or extends the "Custom Mode" interface
 * methods. The "Gamezone" interface adds advanced features such as fan
 * profiles and overclocking. The "Lighting" interface adds control of various
 * status lights related to different hardware components. Each of these
 * drivers uses a common procedure to get data from the WMI interface,
 * enumerated here.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include "wmi-helpers.h"

/* Thermal mode notifier chain. */
static BLOCKING_NOTIFIER_HEAD(tm_chain_head);

/**
 * lwmi_dev_evaluate_int() - Helper function for calling WMI methods that
 * return an integer.
 * @wdev: Pointer to the WMI device to be called.
 * @instance: Instance of the called method.
 * @method_id: WMI Method ID for the method to be called.
 * @buf: Buffer of all arguments for the given method_id.
 * @size: Length of the buffer.
 * @retval: Pointer for the return value to be assigned.
 *
 * Calls wmidev_evaluate_method for Lenovo WMI devices that return an ACPI
 * integer. Validates the return value type and assigns the value to the
 * retval pointer.
 *
 * Return: 0 on success, or an error code.
 */
int lwmi_dev_evaluate_int(struct wmi_device *wdev, u8 instance, u32 method_id,
			  unsigned char *buf, size_t size, u32 *retval)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input = { size, buf };
	acpi_status status;

	status = wmidev_evaluate_method(wdev, instance, method_id, &input,
					&output);
	if (ACPI_FAILURE(status))
		return -EIO;

	union acpi_object *ret_obj __free(kfree) = output.pointer;

	if (retval) {
		if (!ret_obj)
			return -ENODATA;

		switch (ret_obj->type) {
		/*
		 * The ACPI method may simply return a buffer when a u32
		 * is expected. This is valid on Windows as its WMI-ACPI
		 * driver converts everything to a common buffer.
		 */
		case ACPI_TYPE_BUFFER:
			if (ret_obj->buffer.length < sizeof(u32))
				return -ENXIO;

			*retval = get_unaligned_le32(ret_obj->buffer.pointer);
			return 0;
		case ACPI_TYPE_INTEGER:
			*retval = (u32)ret_obj->integer.value;
			return 0;
		default:
			return -ENXIO;
		}
	}

	return 0;
};
EXPORT_SYMBOL_NS_GPL(lwmi_dev_evaluate_int, "LENOVO_WMI_HELPERS");

/**
 * lwmi_tm_register_notifier() - Add a notifier to the blocking notifier chain
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_register to register the notifier block to the
 * thermal mode notifier chain.
 *
 * Return: 0 on success, %-EEXIST on error.
 */
int lwmi_tm_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&tm_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_tm_register_notifier, "LENOVO_WMI_HELPERS");

/**
 * lwmi_tm_unregister_notifier() - Remove a notifier from the blocking notifier
 * chain.
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_unregister to unregister the notifier block from the
 * thermal mode notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
int lwmi_tm_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&tm_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_tm_unregister_notifier, "LENOVO_WMI_HELPERS");

/**
 * devm_lwmi_tm_unregister_notifier() - Remove a notifier from the blocking
 * notifier chain.
 * @data: Void pointer to the notifier_block struct to register.
 *
 * Call lwmi_tm_unregister_notifier to unregister the notifier block from the
 * thermal mode notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
static void devm_lwmi_tm_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	lwmi_tm_unregister_notifier(nb);
}

/**
 * devm_lwmi_tm_register_notifier() - Add a notifier to the blocking notifier
 * chain.
 * @dev: The parent device of the notifier_block struct.
 * @nb: The notifier_block struct to register
 *
 * Call lwmi_tm_register_notifier to register the notifier block to the
 * thermal mode notifier chain. Then add devm_lwmi_tm_unregister_notifier
 * as a device managed action to automatically unregister the notifier block
 * upon parent device removal.
 *
 * Return: 0 on success, or an error code.
 */
int devm_lwmi_tm_register_notifier(struct device *dev,
				   struct notifier_block *nb)
{
	int ret;

	ret = lwmi_tm_register_notifier(nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_lwmi_tm_unregister_notifier,
					nb);
}
EXPORT_SYMBOL_NS_GPL(devm_lwmi_tm_register_notifier, "LENOVO_WMI_HELPERS");

/**
 * lwmi_tm_notifier_call() - Call functions for the notifier call chain.
 * @mode: Pointer to a thermal mode enum to retrieve the data from.
 *
 * Call blocking_notifier_call_chain to retrieve the thermal mode from the
 * lenovo-wmi-gamezone driver.
 *
 * Return: 0 on success, or an error code.
 */
int lwmi_tm_notifier_call(enum thermal_mode *mode)
{
	int ret;

	ret = blocking_notifier_call_chain(&tm_chain_head,
					   LWMI_GZ_GET_THERMAL_MODE, &mode);
	if ((ret & ~NOTIFY_STOP_MASK) != NOTIFY_OK)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lwmi_tm_notifier_call, "LENOVO_WMI_HELPERS");

static struct dentry *lwmi_debugfs_dir;

/**
 * lwmi_debugfs_create_dir() - Helper function for creating a debugfs directory
 * for a device.
 * @wdev: Pointer to the WMI device to be called.
 *
 * Caller must remove the directory with debugfs_remove_recursive() on device
 * removal.
 *
 * Return: Pointer to the created directory.
 */
struct dentry *lwmi_debugfs_create_dir(struct wmi_device *wdev)
{
	return debugfs_create_dir(dev_name(&wdev->dev), lwmi_debugfs_dir);
}
EXPORT_SYMBOL_NS_GPL(lwmi_debugfs_create_dir, "LENOVO_WMI_HELPERS");

static int __init lwmi_helpers_init(void)
{
	lwmi_debugfs_dir = debugfs_create_dir("lenovo_wmi", NULL);

	return 0;
}
subsys_initcall(lwmi_helpers_init)

static void __exit lwmi_helpers_exit(void)
{
	debugfs_remove_recursive(lwmi_debugfs_dir);
}
module_exit(lwmi_helpers_exit)

MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo WMI Helpers Driver");
MODULE_LICENSE("GPL");
