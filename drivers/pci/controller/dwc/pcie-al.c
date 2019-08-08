// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Amazon's Annapurna Labs IP (used in chips
 * such as Graviton and Alpine)
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Author: Jonathan Chocron <jonnyc@amazon.com>
 */

#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/pci-acpi.h>
#include "../../pci.h"

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)

struct al_pcie_acpi  {
	void __iomem *dbi_base;
};

static void __iomem *al_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				     int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct al_pcie_acpi *pcie = cfg->priv;
	void __iomem *dbi_base = pcie->dbi_base;

	if (bus->number == cfg->busr.start) {
		/*
		 * The DW PCIe core doesn't filter out transactions to other
		 * devices/functions on the root bus num, so we do this here.
		 */
		if (PCI_SLOT(devfn) > 0)
			return NULL;
		else
			return dbi_base + where;
	}

	return pci_ecam_map_bus(bus, devfn, where);
}

static int al_pcie_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_pci_root *root = acpi_driver_data(adev);
	struct al_pcie_acpi *al_pcie;
	struct resource *res;
	int ret;

	al_pcie = devm_kzalloc(dev, sizeof(*al_pcie), GFP_KERNEL);
	if (!al_pcie)
		return -ENOMEM;

	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_get_rc_resources(dev, "AMZN0001", root->segment, res);
	if (ret) {
		dev_err(dev, "can't get rc dbi base address for SEG %d\n",
			root->segment);
		return ret;
	}

	dev_dbg(dev, "Root port dbi res: %pR\n", res);

	al_pcie->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(al_pcie->dbi_base)) {
		long err = PTR_ERR(al_pcie->dbi_base);

		dev_err(dev, "couldn't remap dbi base %pR (err:%ld)\n",
			res, err);
		return err;
	}

	cfg->priv = al_pcie;

	return 0;
}

struct pci_ecam_ops al_pcie_ops = {
	.bus_shift    = 20,
	.init         =  al_pcie_init,
	.pci_ops      = {
		.map_bus    = al_pcie_map_bus,
		.read       = pci_generic_config_read,
		.write      = pci_generic_config_write,
	}
};

#endif /* defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS) */
