// SPDX-License-Identifier: GPL-2.0
/*
 * PCI driver for the Intel SCU.
 *
 * Copyright (C) 2008-2010, 2015, 2020 Intel Corporation
 * Authors: Sreedhara DS (sreedhara.ds@intel.com)
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/intel-mid.h>
#include <asm/intel_scu_ipc.h>

static int intel_scu_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct intel_scu_ipc_data scu_data = {};
	struct intel_scu_ipc_dev *scu;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	scu_data.mem = pdev->resource[0];
	scu_data.irq = pdev->irq;

	scu = intel_scu_ipc_register(&pdev->dev, &scu_data);
	return PTR_ERR_OR_ZERO(scu);
}

static const struct pci_device_id pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x080e) },
	{ PCI_VDEVICE(INTEL, 0x082a) },
	{ PCI_VDEVICE(INTEL, 0x08ea) },
	{ PCI_VDEVICE(INTEL, 0x0a94) },
	{ PCI_VDEVICE(INTEL, 0x11a0) },
	{ PCI_VDEVICE(INTEL, 0x1a94) },
	{ PCI_VDEVICE(INTEL, 0x5a94) },
	{}
};

static struct pci_driver intel_scu_pci_driver = {
	.driver = {
		.suppress_bind_attrs = true,
	},
	.name = "intel_scu",
	.id_table = pci_ids,
	.probe = intel_scu_pci_probe,
};

builtin_pci_driver(intel_scu_pci_driver);
