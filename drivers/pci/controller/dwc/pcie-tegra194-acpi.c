// SPDX-License-Identifier: GPL-2.0+
/*
 * ACPI quirks for Tegra194 PCIe host controller
 *
 * Copyright (C) 2021 NVIDIA Corporation.
 *
 * Author: Vidya Sagar <vidyas@nvidia.com>
 */

#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>

#include "pcie-designware.h"

struct tegra194_pcie_ecam  {
	void __iomem *config_base;
	void __iomem *iatu_base;
	void __iomem *dbi_base;
};

static int tegra194_acpi_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct tegra194_pcie_ecam *pcie_ecam;

	pcie_ecam = devm_kzalloc(dev, sizeof(*pcie_ecam), GFP_KERNEL);
	if (!pcie_ecam)
		return -ENOMEM;

	pcie_ecam->config_base = cfg->win;
	pcie_ecam->iatu_base = cfg->win + SZ_256K;
	pcie_ecam->dbi_base = cfg->win + SZ_512K;
	cfg->priv = pcie_ecam;

	return 0;
}

static void atu_reg_write(struct tegra194_pcie_ecam *pcie_ecam, int index,
			  u32 val, u32 reg)
{
	u32 offset = PCIE_ATU_UNROLL_BASE(PCIE_ATU_REGION_DIR_OB, index) +
		     PCIE_ATU_VIEWPORT_BASE;

	writel(val, pcie_ecam->iatu_base + offset + reg);
}

static void program_outbound_atu(struct tegra194_pcie_ecam *pcie_ecam,
				 int index, int type, u64 cpu_addr,
				 u64 pci_addr, u64 size)
{
	atu_reg_write(pcie_ecam, index, lower_32_bits(cpu_addr),
		      PCIE_ATU_LOWER_BASE);
	atu_reg_write(pcie_ecam, index, upper_32_bits(cpu_addr),
		      PCIE_ATU_UPPER_BASE);
	atu_reg_write(pcie_ecam, index, lower_32_bits(pci_addr),
		      PCIE_ATU_LOWER_TARGET);
	atu_reg_write(pcie_ecam, index, lower_32_bits(cpu_addr + size - 1),
		      PCIE_ATU_LIMIT);
	atu_reg_write(pcie_ecam, index, upper_32_bits(pci_addr),
		      PCIE_ATU_UPPER_TARGET);
	atu_reg_write(pcie_ecam, index, type, PCIE_ATU_REGION_CTRL1);
	atu_reg_write(pcie_ecam, index, PCIE_ATU_ENABLE, PCIE_ATU_REGION_CTRL2);
}

static void __iomem *tegra194_map_bus(struct pci_bus *bus,
				      unsigned int devfn, int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct tegra194_pcie_ecam *pcie_ecam = cfg->priv;
	u32 busdev;
	int type;

	if (bus->number < cfg->busr.start || bus->number > cfg->busr.end)
		return NULL;

	if (bus->number == cfg->busr.start) {
		if (PCI_SLOT(devfn) == 0)
			return pcie_ecam->dbi_base + where;
		else
			return NULL;
	}

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == cfg->busr.start) {
		if (PCI_SLOT(devfn) == 0)
			type = PCIE_ATU_TYPE_CFG0;
		else
			return NULL;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
	}

	program_outbound_atu(pcie_ecam, 0, type, cfg->res.start, busdev,
			     SZ_256K);

	return pcie_ecam->config_base + where;
}

const struct pci_ecam_ops tegra194_pcie_ops = {
	.init		= tegra194_acpi_init,
	.pci_ops	= {
		.map_bus	= tegra194_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};
