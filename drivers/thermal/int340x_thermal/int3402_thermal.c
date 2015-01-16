/*
 * INT3402 thermal driver for memory temperature reporting
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Aaron Lu <aaron.lu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include "int340x_thermal_zone.h"

struct int3402_thermal_data {
	acpi_handle *handle;
	struct int34x_thermal_zone *int340x_zone;
};

static int int3402_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3402_thermal_data *d;

	if (!acpi_has_method(adev->handle, "_TMP"))
		return -ENODEV;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->int340x_zone = int340x_thermal_zone_add(adev, NULL);
	if (IS_ERR(d->int340x_zone))
		return PTR_ERR(d->int340x_zone);

	d->handle = adev->handle;
	platform_set_drvdata(pdev, d);

	return 0;
}

static int int3402_thermal_remove(struct platform_device *pdev)
{
	struct int3402_thermal_data *d = platform_get_drvdata(pdev);

	int340x_thermal_zone_remove(d->int340x_zone);

	return 0;
}

static const struct acpi_device_id int3402_thermal_match[] = {
	{"INT3402", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3402_thermal_match);

static struct platform_driver int3402_thermal_driver = {
	.probe = int3402_thermal_probe,
	.remove = int3402_thermal_remove,
	.driver = {
		   .name = "int3402 thermal",
		   .acpi_match_table = int3402_thermal_match,
		   },
};

module_platform_driver(int3402_thermal_driver);

MODULE_DESCRIPTION("INT3402 Thermal driver");
MODULE_LICENSE("GPL");
