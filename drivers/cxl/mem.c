// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "pci.h"

static int cxl_mem_dvsec(struct pci_dev *pdev, int dvsec)
{
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_DVSEC);
	if (!pos)
		return 0;

	while (pos) {
		u16 vendor, id;

		pci_read_config_word(pdev, pos + PCI_DVSEC_HEADER1, &vendor);
		pci_read_config_word(pdev, pos + PCI_DVSEC_HEADER2, &id);
		if (vendor == PCI_DVSEC_VENDOR_ID_CXL && dvsec == id)
			return pos;

		pos = pci_find_next_ext_capability(pdev, pos,
						   PCI_EXT_CAP_ID_DVSEC);
	}

	return 0;
}

static int cxl_mem_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	int regloc;

	regloc = cxl_mem_dvsec(pdev, PCI_DVSEC_ID_CXL_REGLOC_OFFSET);
	if (!regloc) {
		dev_err(dev, "register location dvsec not found\n");
		return -ENXIO;
	}

	return 0;
}

static const struct pci_device_id cxl_mem_pci_tbl[] = {
	/* PCI class code for CXL.mem Type-3 Devices */
	{ PCI_DEVICE_CLASS((PCI_CLASS_MEMORY_CXL << 8 | CXL_MEMORY_PROGIF), ~0)},
	{ /* terminate list */ },
};
MODULE_DEVICE_TABLE(pci, cxl_mem_pci_tbl);

static struct pci_driver cxl_mem_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= cxl_mem_pci_tbl,
	.probe			= cxl_mem_probe,
	.driver	= {
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
	},
};

MODULE_LICENSE("GPL v2");
module_pci_driver(cxl_mem_driver);
