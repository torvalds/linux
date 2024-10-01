// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright 2013 Matthew Garrett <mjg59@srcf.ucam.org>
 */

#include <linux/acpi.h>
#include <linux/module.h>

MODULE_DESCRIPTION("Intel Smart Connect disabling driver");
MODULE_LICENSE("GPL");

static int smartconnect_acpi_init(struct acpi_device *acpi)
{
	unsigned long long value;
	acpi_status status;

	status = acpi_evaluate_integer(acpi->handle, "GAOS", NULL, &value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	if (value & 0x1) {
		dev_info(&acpi->dev, "Disabling Intel Smart Connect\n");
		status = acpi_execute_simple_method(acpi->handle, "SAOS", 0);
	}

	return 0;
}

static const struct acpi_device_id smartconnect_ids[] = {
	{"INT33A0", 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, smartconnect_ids);

static struct acpi_driver smartconnect_driver = {
	.name = "intel_smart_connect",
	.class = "intel_smart_connect",
	.ids = smartconnect_ids,
	.ops = {
		.add = smartconnect_acpi_init,
	},
};

module_acpi_driver(smartconnect_driver);
