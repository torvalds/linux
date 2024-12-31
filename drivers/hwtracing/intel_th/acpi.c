// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub ACPI driver
 *
 * Copyright (C) 2017 Intel Corporation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#include "intel_th.h"

#define DRIVER_NAME "intel_th_acpi"

static const struct intel_th_drvdata intel_th_acpi_pch = {
	.host_mode_only	= 1,
};

static const struct intel_th_drvdata intel_th_acpi_uncore = {
	.host_mode_only	= 1,
};

static const struct acpi_device_id intel_th_acpi_ids[] = {
	{ "INTC1000",	(kernel_ulong_t)&intel_th_acpi_uncore },
	{ "INTC1001",	(kernel_ulong_t)&intel_th_acpi_pch },
	{ "",		0 },
};

MODULE_DEVICE_TABLE(acpi, intel_th_acpi_ids);

static int intel_th_acpi_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct resource resource[TH_MMIO_END];
	const struct acpi_device_id *id;
	struct intel_th *th;
	int i, r;

	id = acpi_match_device(intel_th_acpi_ids, &pdev->dev);
	if (!id)
		return -ENODEV;

	for (i = 0, r = 0; i < pdev->num_resources && r < TH_MMIO_END; i++)
		if (pdev->resource[i].flags &
		    (IORESOURCE_IRQ | IORESOURCE_MEM))
			resource[r++] = pdev->resource[i];

	th = intel_th_alloc(&pdev->dev, (void *)id->driver_data, resource, r);
	if (IS_ERR(th))
		return PTR_ERR(th);

	adev->driver_data = th;

	return 0;
}

static void intel_th_acpi_remove(struct platform_device *pdev)
{
	struct intel_th *th = platform_get_drvdata(pdev);

	intel_th_free(th);
}

static struct platform_driver intel_th_acpi_driver = {
	.probe		= intel_th_acpi_probe,
	.remove		= intel_th_acpi_remove,
	.driver		= {
		.name			= DRIVER_NAME,
		.acpi_match_table	= intel_th_acpi_ids,
	},
};

module_platform_driver(intel_th_acpi_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub ACPI controller driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@intel.com>");
