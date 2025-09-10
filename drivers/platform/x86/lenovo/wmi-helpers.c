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
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/wmi.h>

#include "wmi-helpers.h"

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
	union acpi_object *ret_obj __free(kfree) = NULL;
	struct acpi_buffer input = { size, buf };
	acpi_status status;

	status = wmidev_evaluate_method(wdev, instance, method_id, &input,
					&output);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (retval) {
		ret_obj = output.pointer;
		if (!ret_obj)
			return -ENODATA;

		if (ret_obj->type != ACPI_TYPE_INTEGER)
			return -ENXIO;

		*retval = (u32)ret_obj->integer.value;
	}

	return 0;
};
EXPORT_SYMBOL_NS_GPL(lwmi_dev_evaluate_int, "LENOVO_WMI_HELPERS");

MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo WMI Helpers Driver");
MODULE_LICENSE("GPL");
