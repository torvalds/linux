/*
 * ACPI support for int340x thermal drivers
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>

#include "../internal.h"

#define INT3401_DEVICE 0X01
static const struct acpi_device_id int340x_thermal_device_ids[] = {
	{"INT3400"},
	{"INT3401", INT3401_DEVICE},
	{"INT3402"},
	{"INT3403"},
	{"INT3404"},
	{"INT3406"},
	{"INT3407"},
	{"INT3408"},
	{"INT3409"},
	{"INT340A"},
	{"INT340B"},
	{""},
};

static int int340x_thermal_handler_attach(struct acpi_device *adev,
					const struct acpi_device_id *id)
{
	if (IS_ENABLED(CONFIG_INT340X_THERMAL))
		acpi_create_platform_device(adev, NULL);
	/* Intel SoC DTS thermal driver needs INT3401 to set IRQ descriptor */
	else if (IS_ENABLED(CONFIG_INTEL_SOC_DTS_THERMAL) &&
		 id->driver_data == INT3401_DEVICE)
		acpi_create_platform_device(adev, NULL);
	return 1;
}

static struct acpi_scan_handler int340x_thermal_handler = {
	.ids = int340x_thermal_device_ids,
	.attach = int340x_thermal_handler_attach,
};

void __init acpi_int340x_thermal_init(void)
{
	acpi_scan_add_handler(&int340x_thermal_handler);
}
