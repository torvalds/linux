// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for the Intel SCU.
 *
 * Copyright (C) 2019, Intel Corporation
 * Authors: Divya Sasidharan <divya.s.sasidharan@intel.com>
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Rajmohan Mani <rajmohan.mani@intel.com>
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/intel_scu_ipc.h>

static int intel_scu_platform_probe(struct platform_device *pdev)
{
	struct intel_scu_ipc_data scu_data = {};
	struct intel_scu_ipc_dev *scu;
	const struct resource *res;

	scu_data.irq = platform_get_irq_optional(pdev, 0);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	scu_data.mem = *res;

	scu = devm_intel_scu_ipc_register(&pdev->dev, &scu_data);
	if (IS_ERR(scu))
		return PTR_ERR(scu);

	platform_set_drvdata(pdev, scu);
	return 0;
}

static const struct acpi_device_id intel_scu_acpi_ids[] = {
	{ "INTC1026" },
	{}
};
MODULE_DEVICE_TABLE(acpi, intel_scu_acpi_ids);

static struct platform_driver intel_scu_platform_driver = {
	.probe = intel_scu_platform_probe,
	.driver = {
		.name = "intel_scu",
		.acpi_match_table = intel_scu_acpi_ids,
	},
};
module_platform_driver(intel_scu_platform_driver);

MODULE_AUTHOR("Divya Sasidharan <divya.s.sasidharan@intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com");
MODULE_AUTHOR("Rajmohan Mani <rajmohan.mani@intel.com>");
MODULE_DESCRIPTION("Intel SCU platform driver");
MODULE_LICENSE("GPL v2");
