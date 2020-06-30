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
	void (*setup_fn)(void) = (void (*)(void))id->driver_data;
	struct intel_scu_ipc_data scu_data = {};
	struct intel_scu_ipc_dev *scu;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	scu_data.mem = pdev->resource[0];
	scu_data.irq = pdev->irq;

	scu = intel_scu_ipc_register(&pdev->dev, &scu_data);
	if (IS_ERR(scu))
		return PTR_ERR(scu);

	if (setup_fn)
		setup_fn();
	return 0;
}

static void intel_mid_scu_setup(void)
{
	intel_scu_devices_create();
}

static const struct pci_device_id pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x080e),
	  .driver_data = (kernel_ulong_t)intel_mid_scu_setup },
	{ PCI_VDEVICE(INTEL, 0x08ea),
	  .driver_data = (kernel_ulong_t)intel_mid_scu_setup },
	{ PCI_VDEVICE(INTEL, 0x0a94) },
	{ PCI_VDEVICE(INTEL, 0x11a0),
	  .driver_data = (kernel_ulong_t)intel_mid_scu_setup },
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
