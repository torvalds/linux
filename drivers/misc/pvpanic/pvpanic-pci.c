// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic PCI Device Support
 *
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

#define PCI_VENDOR_ID_REDHAT             0x1b36
#define PCI_DEVICE_ID_REDHAT_PVPANIC     0x0011

MODULE_AUTHOR("Mihai Carabas <mihai.carabas@oracle.com>");
MODULE_DESCRIPTION("pvpanic device driver");
MODULE_LICENSE("GPL");

static int pvpanic_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	void __iomem *base;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret < 0)
		return ret;

	base = pcim_iomap(pdev, 0, 0);
	if (!base)
		return -ENOMEM;

	return devm_pvpanic_probe(&pdev->dev, base);
}

static const struct pci_device_id pvpanic_pci_id_tbl[]  = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_PVPANIC)},
	{}
};
MODULE_DEVICE_TABLE(pci, pvpanic_pci_id_tbl);

static struct pci_driver pvpanic_pci_driver = {
	.name =         "pvpanic-pci",
	.id_table =     pvpanic_pci_id_tbl,
	.probe =        pvpanic_pci_probe,
	.dev_groups =   pvpanic_dev_groups,
};
module_pci_driver(pvpanic_pci_driver);
