// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This code gives functions to avoid code duplication while interacting with
 * the TUXEDO NB04 wmi interfaces.
 *
 * Copyright (C) 2024-2025 Werner Sembach <wse@tuxedocomputers.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/wmi.h>

#include "wmi_util.h"

static int __wmi_method_acpi_object_out(struct wmi_device *wdev,
					u32 wmi_method_id,
					u8 *in,
					acpi_size in_len,
					union acpi_object **out)
{
	struct acpi_buffer acpi_buffer_in = { in_len, in };
	struct acpi_buffer acpi_buffer_out = { ACPI_ALLOCATE_BUFFER, NULL };

	dev_dbg(&wdev->dev, "Evaluate WMI method: %u in:\n", wmi_method_id);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, in, in_len);

	acpi_status status = wmidev_evaluate_method(wdev, 0, wmi_method_id,
						    &acpi_buffer_in,
						    &acpi_buffer_out);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "Failed to evaluate WMI method.\n");
		return -EIO;
	}
	if (!acpi_buffer_out.pointer) {
		dev_err(&wdev->dev, "Unexpected empty out buffer.\n");
		return -ENODATA;
	}

	*out = acpi_buffer_out.pointer;

	return 0;
}

static int __wmi_method_buffer_out(struct wmi_device *wdev,
				   u32 wmi_method_id,
				   u8 *in,
				   acpi_size in_len,
				   u8 *out,
				   acpi_size out_len)
{
	int ret;

	union acpi_object *acpi_object_out __free(kfree) = NULL;

	ret = __wmi_method_acpi_object_out(wdev, wmi_method_id,
					   in, in_len,
					   &acpi_object_out);
	if (ret)
		return ret;

	if (acpi_object_out->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Unexpected out buffer type. Expected: %u Got: %u\n",
			ACPI_TYPE_BUFFER, acpi_object_out->type);
		return -EIO;
	}
	if (acpi_object_out->buffer.length < out_len) {
		dev_err(&wdev->dev, "Unexpected out buffer length.\n");
		return -EIO;
	}

	memcpy(out, acpi_object_out->buffer.pointer, out_len);

	return 0;
}

int tux_wmi_xx_8in_80out(struct wmi_device *wdev,
			 enum tux_wmi_xx_8in_80out_methods method,
			 union tux_wmi_xx_8in_80out_in_t *in,
			 union tux_wmi_xx_8in_80out_out_t *out)
{
	return __wmi_method_buffer_out(wdev, method, in->raw, 8, out->raw, 80);
}

int tux_wmi_xx_496in_80out(struct wmi_device *wdev,
			   enum tux_wmi_xx_496in_80out_methods method,
			   union tux_wmi_xx_496in_80out_in_t *in,
			   union tux_wmi_xx_496in_80out_out_t *out)
{
	return __wmi_method_buffer_out(wdev, method, in->raw, 496, out->raw, 80);
}
