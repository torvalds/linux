// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright 2013 Matthew Garrett <mjg59@srcf.ucam.org>
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>

MODULE_DESCRIPTION("Intel Smart Connect disabling driver");
MODULE_LICENSE("GPL");

static int smartconnect_acpi_probe(struct platform_device *pdev)
{
	unsigned long long value;
	acpi_handle handle;
	acpi_status status;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	status = acpi_evaluate_integer(handle, "GAOS", NULL, &value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	if (value & 0x1) {
		dev_info(&pdev->dev, "Disabling Intel Smart Connect\n");
		status = acpi_execute_simple_method(handle, "SAOS", 0);
	}

	return 0;
}

static const struct acpi_device_id smartconnect_ids[] = {
	{"INT33A0", 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, smartconnect_ids);

static struct platform_driver smartconnect_driver = {
	.probe = smartconnect_acpi_probe,
	.driver = {
		.name = "intel_smart_connect",
		.acpi_match_table = smartconnect_ids,
	},
};

module_platform_driver(smartconnect_driver);
