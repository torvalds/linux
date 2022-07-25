// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI PCIe host controller driver for Rockchip SoCs
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *		http://www.rock-chips.com
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pci-ecam.h>
#include <linux/pci-acpi.h>
#include <linux/pci.h>

#include "pcie-designware.h"
#include "../../pci.h"

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)

#define DWC_ATU_REGION_INDEX1		(0x1 << 0)
#define ECAM_RESV_SIZE	SZ_16M

struct rk_pcie_acpi  {
	void __iomem *dbi_base;
	void __iomem *cfg_base;
	phys_addr_t mcfg_addr;
};

static void rk_pcie_writel_ob_unroll(void __iomem *dbi_base, u32 index, u32 reg, u32 val)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	writel(val, dbi_base + offset + reg + DEFAULT_DBI_ATU_OFFSET);
}

static u32 rk_pcie_readl_ob_unroll(void __iomem *dbi_base, u32 index, u32 reg)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	return readl(dbi_base + offset + reg + DEFAULT_DBI_ATU_OFFSET);
}

static void rk_pcie_prog_outbound_atu_unroll(struct device *dev, void __iomem *dbi_base, u32 index,
					     u32 type, u64 cpu_addr, u64 pci_addr, u32 size)
{
	u32 retries, val;

	dev_dbg(dev, "%s: ATU programmed with: index: %d, type: %d, cpu addr: %8llx, pci addr: %8llx, size: %8x\n",
			__func__, index, type, cpu_addr, pci_addr, size);

	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_LOWER_BASE, lower_32_bits(cpu_addr));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_UPPER_BASE, upper_32_bits(cpu_addr));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_LOWER_LIMIT, lower_32_bits(cpu_addr + size - 1));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_UPPER_LIMIT, upper_32_bits(cpu_addr + size - 1));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_LOWER_TARGET, lower_32_bits(pci_addr));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_UPPER_TARGET, upper_32_bits(pci_addr));
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_REGION_CTRL1, type);
	rk_pcie_writel_ob_unroll(dbi_base, index, PCIE_ATU_UNR_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = rk_pcie_readl_ob_unroll(dbi_base, index, PCIE_ATU_UNR_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return;
		mdelay(LINK_WAIT_IATU);
	}

	dev_err(dev, "outbound iATU is not being enabled\n");
}

static int rk_pcie_ecam_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_pci_root *root = acpi_driver_data(adev);
	struct resource *res;
	phys_addr_t mcfg_addr;
	struct rk_pcie_acpi *rk_pcie;
	int ret;

	rk_pcie = devm_kzalloc(dev, sizeof(*rk_pcie), GFP_KERNEL);
	if (!rk_pcie)
		return -ENOMEM;

	/*
	 * Retrieve RC base and size from a RKCP0001 device with _UID
	 * matching our segment.
	 */
	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_get_rc_resources(dev, "RKCP0001", root->segment, res);
	if (ret) {
		dev_err(dev, "can't get rc base (DBI) address\n");
		return -ENOMEM;
	}

	dev_info(dev, "DBI address is %pa\n", &res->start);
	rk_pcie->dbi_base = devm_pci_remap_cfgspace(dev, res->start, resource_size(res));
	if (!rk_pcie->dbi_base)
		return -ENOMEM;

	mcfg_addr = acpi_pci_root_get_mcfg_addr(adev->handle);
	if (!mcfg_addr) {
		dev_err(dev, "can't get mcfg base (cfg) address\n");
		return -ENOMEM;
	}

	dev_info(dev, "mcfg address is %pa\n", &mcfg_addr);
	rk_pcie->mcfg_addr = mcfg_addr;

	rk_pcie->cfg_base = devm_pci_remap_cfgspace(dev, mcfg_addr, SZ_1M);
	if (!rk_pcie->cfg_base)
		return -ENOMEM;

	cfg->priv = rk_pcie;

	return 0;
}

static int rk_pcie_ecam_rd_conf(struct pci_bus *bus, u32 devfn, int where, int size, u32 *val)
{
	struct pci_config_window *cfg = bus->sysdata;
	int dev = PCI_SLOT(devfn);

	/* access only one slot on each root port */
	if (bus->number == cfg->busr.start && dev > 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return pci_generic_config_read(bus, devfn, where, size, val);
}

static int rk_pcie_ecam_wr_conf(struct pci_bus *bus, u32 devfn, int where, int size, u32 val)
{
	struct pci_config_window *cfg = bus->sysdata;
	int dev = PCI_SLOT(devfn);

	/* access only one slot on each root port */
	if (bus->number == cfg->busr.start && dev > 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return pci_generic_config_write(bus, devfn, where, size, val);
}

static void __iomem *rk_pcie_ecam_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct rk_pcie_acpi *rk_pcie = cfg->priv;
	u32 atu_type;
	u32 busdev;

	/* read RC config space */
	if (bus->number == cfg->busr.start)
		return rk_pcie->dbi_base + where;

	if (pci_is_root_bus(bus->parent))
		atu_type = PCIE_ATU_TYPE_CFG0;
	else
		atu_type = PCIE_ATU_TYPE_CFG1;

	busdev = PCIE_ATU_BUS(bus->number) |
		 PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	/*
	 * UEFI region mapping relation:
	 * index0: 32-bit np memory
	 * index1: config
	 * index2: IO
	 * index3: 64-bit np memory
	 */
	rk_pcie_prog_outbound_atu_unroll(cfg->parent, rk_pcie->dbi_base, DWC_ATU_REGION_INDEX1,
					 atu_type, (u64)rk_pcie->mcfg_addr, busdev, ECAM_RESV_SIZE);

	dev_dbg(cfg->parent, "Read other config: 0x%p where = %d\n",
		rk_pcie->cfg_base + where, where);

	return rk_pcie->cfg_base + where;
}

const struct pci_ecam_ops rk_pcie_ecam_ops = {
	.bus_shift    = 20, /* We don't need this */
	.init         =  rk_pcie_ecam_init,
	.pci_ops      = {
		.map_bus    = rk_pcie_ecam_map_bus,
		.read       = rk_pcie_ecam_rd_conf,
		.write      = rk_pcie_ecam_wr_conf,
	}
};
#endif
