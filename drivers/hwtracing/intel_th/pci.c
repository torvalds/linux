/*
 * Intel(R) Trace Hub pci driver
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/pci.h>

#include "intel_th.h"

#define DRIVER_NAME "intel_th_pci"

#define BAR_MASK (BIT(TH_MMIO_CONFIG) | BIT(TH_MMIO_SW))

static int intel_th_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct intel_th *th;
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions_request_all(pdev, BAR_MASK, DRIVER_NAME);
	if (err)
		return err;

	th = intel_th_alloc(&pdev->dev, pdev->resource,
			    DEVICE_COUNT_RESOURCE, pdev->irq);
	if (IS_ERR(th))
		return PTR_ERR(th);

	pci_set_drvdata(pdev, th);

	return 0;
}

static void intel_th_pci_remove(struct pci_dev *pdev)
{
	struct intel_th *th = pci_get_drvdata(pdev);

	intel_th_free(th);
}

static const struct pci_device_id intel_th_pci_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x9d26),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa126),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Kaby Lake PCH-H */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa2a6),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Cannon Lake H */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa326),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Cannon Lake LP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x9da6),
		.driver_data = (kernel_ulong_t)0,
	},
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, intel_th_pci_id_table);

static struct pci_driver intel_th_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= intel_th_pci_id_table,
	.probe		= intel_th_pci_probe,
	.remove		= intel_th_pci_remove,
};

module_pci_driver(intel_th_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub PCI controller driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@intel.com>");
