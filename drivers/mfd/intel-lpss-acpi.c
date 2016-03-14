/*
 * Intel LPSS ACPI support.
 *
 * Copyright (C) 2015, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include "intel-lpss.h"

static const struct intel_lpss_platform_info spt_info = {
	.clk_rate = 120000000,
};

static struct property_entry spt_i2c_properties[] = {
	PROPERTY_ENTRY_U32("i2c-sda-hold-time-ns", 230),
	{ },
};

static struct property_set spt_i2c_pset = {
	.properties = spt_i2c_properties,
};

static const struct intel_lpss_platform_info spt_i2c_info = {
	.clk_rate = 120000000,
	.pset = &spt_i2c_pset,
};

static const struct intel_lpss_platform_info bxt_info = {
	.clk_rate = 100000000,
};

static const struct intel_lpss_platform_info bxt_i2c_info = {
	.clk_rate = 133000000,
};

static const struct acpi_device_id intel_lpss_acpi_ids[] = {
	/* SPT */
	{ "INT3446", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3447", (kernel_ulong_t)&spt_i2c_info },
	/* BXT */
	{ "80860AAC", (kernel_ulong_t)&bxt_i2c_info },
	{ "80860ABC", (kernel_ulong_t)&bxt_info },
	{ "80860AC2", (kernel_ulong_t)&bxt_info },
	/* APL */
	{ "80865AAC", (kernel_ulong_t)&bxt_i2c_info },
	{ "80865ABC", (kernel_ulong_t)&bxt_info },
	{ "80865AC2", (kernel_ulong_t)&bxt_info },
	{ }
};
MODULE_DEVICE_TABLE(acpi, intel_lpss_acpi_ids);

static int intel_lpss_acpi_probe(struct platform_device *pdev)
{
	struct intel_lpss_platform_info *info;
	const struct acpi_device_id *id;

	id = acpi_match_device(intel_lpss_acpi_ids, &pdev->dev);
	if (!id)
		return -ENODEV;

	info = devm_kmemdup(&pdev->dev, (void *)id->driver_data, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->irq = platform_get_irq(pdev, 0);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return intel_lpss_probe(&pdev->dev, info);
}

static int intel_lpss_acpi_remove(struct platform_device *pdev)
{
	intel_lpss_remove(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static INTEL_LPSS_PM_OPS(intel_lpss_acpi_pm_ops);

static struct platform_driver intel_lpss_acpi_driver = {
	.probe = intel_lpss_acpi_probe,
	.remove = intel_lpss_acpi_remove,
	.driver = {
		.name = "intel-lpss",
		.acpi_match_table = intel_lpss_acpi_ids,
		.pm = &intel_lpss_acpi_pm_ops,
	},
};

module_platform_driver(intel_lpss_acpi_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel LPSS ACPI driver");
MODULE_LICENSE("GPL v2");
