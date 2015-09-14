/*
 * Intel LPSS PCI support.
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

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "intel-lpss.h"

static int intel_lpss_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct intel_lpss_platform_info *info;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	info = devm_kmemdup(&pdev->dev, (void *)id->driver_data, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->mem = &pdev->resource[0];
	info->irq = pdev->irq;

	/* Probably it is enough to set this for iDMA capable devices only */
	pci_set_master(pdev);

	ret = intel_lpss_probe(&pdev->dev, info);
	if (ret)
		return ret;

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void intel_lpss_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	intel_lpss_remove(&pdev->dev);
}

static INTEL_LPSS_PM_OPS(intel_lpss_pci_pm_ops);

static const struct intel_lpss_platform_info spt_info = {
	.clk_rate = 120000000,
};

static const struct intel_lpss_platform_info spt_uart_info = {
	.clk_rate = 120000000,
	.clk_con_id = "baudclk",
};

static const struct pci_device_id intel_lpss_pci_ids[] = {
	/* SPT-LP */
	{ PCI_VDEVICE(INTEL, 0x9d27), (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9d28), (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9d29), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d2a), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d60), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d61), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d62), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d63), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d64), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d65), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0x9d66), (kernel_ulong_t)&spt_uart_info },
	/* SPT-H */
	{ PCI_VDEVICE(INTEL, 0xa127), (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa128), (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa129), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0xa12a), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0xa160), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0xa161), (kernel_ulong_t)&spt_info },
	{ PCI_VDEVICE(INTEL, 0xa166), (kernel_ulong_t)&spt_uart_info },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_lpss_pci_ids);

static struct pci_driver intel_lpss_pci_driver = {
	.name = "intel-lpss",
	.id_table = intel_lpss_pci_ids,
	.probe = intel_lpss_pci_probe,
	.remove = intel_lpss_pci_remove,
	.driver = {
		.pm = &intel_lpss_pci_pm_ops,
	},
};

module_pci_driver(intel_lpss_pci_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel LPSS PCI driver");
MODULE_LICENSE("GPL v2");
