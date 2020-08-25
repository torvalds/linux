// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for HiSilicon SoCs
 *
 * Copyright (C) 2015 HiSilicon Co., Ltd. http://www.hisilicon.com
 *
 * Authors: Zhou Wang <wangzhou1@hisilicon.com>
 *          Dacai Zhu <zhudacai@hisilicon.com>
 *          Gabriele Paoloni <gabriele.paoloni@huawei.com>
 */
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>
#include "../../pci.h"

#if defined(CONFIG_PCI_HISI) || (defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS))

static int hisi_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			     int size, u32 *val)
{
	struct pci_config_window *cfg = bus->sysdata;
	int dev = PCI_SLOT(devfn);

	if (bus->number == cfg->busr.start) {
		/* access only one slot on each root port */
		if (dev > 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
		else
			return pci_generic_config_read32(bus, devfn, where,
							 size, val);
	}

	return pci_generic_config_read(bus, devfn, where, size, val);
}

static int hisi_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			     int where, int size, u32 val)
{
	struct pci_config_window *cfg = bus->sysdata;
	int dev = PCI_SLOT(devfn);

	if (bus->number == cfg->busr.start) {
		/* access only one slot on each root port */
		if (dev > 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
		else
			return pci_generic_config_write32(bus, devfn, where,
							  size, val);
	}

	return pci_generic_config_write(bus, devfn, where, size, val);
}

static void __iomem *hisi_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				       int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	void __iomem *reg_base = cfg->priv;

	if (bus->number == cfg->busr.start)
		return reg_base + where;
	else
		return pci_ecam_map_bus(bus, devfn, where);
}

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)

static int hisi_pcie_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_pci_root *root = acpi_driver_data(adev);
	struct resource *res;
	void __iomem *reg_base;
	int ret;

	/*
	 * Retrieve RC base and size from a HISI0081 device with _UID
	 * matching our segment.
	 */
	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_get_rc_resources(dev, "HISI0081", root->segment, res);
	if (ret) {
		dev_err(dev, "can't get rc base address\n");
		return -ENOMEM;
	}

	reg_base = devm_pci_remap_cfgspace(dev, res->start, resource_size(res));
	if (!reg_base)
		return -ENOMEM;

	cfg->priv = reg_base;
	return 0;
}

const struct pci_ecam_ops hisi_pcie_ops = {
	.bus_shift    = 20,
	.init         =  hisi_pcie_init,
	.pci_ops      = {
		.map_bus    = hisi_pcie_map_bus,
		.read       = hisi_pcie_rd_conf,
		.write      = hisi_pcie_wr_conf,
	}
};

#endif

#ifdef CONFIG_PCI_HISI

static int hisi_pcie_platform_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	void __iomem *reg_base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "missing \"reg[1]\"property\n");
		return -EINVAL;
	}

	reg_base = devm_pci_remap_cfgspace(dev, res->start, resource_size(res));
	if (!reg_base)
		return -ENOMEM;

	cfg->priv = reg_base;
	return 0;
}

static const struct pci_ecam_ops hisi_pcie_platform_ops = {
	.bus_shift    = 20,
	.init         =  hisi_pcie_platform_init,
	.pci_ops      = {
		.map_bus    = hisi_pcie_map_bus,
		.read       = hisi_pcie_rd_conf,
		.write      = hisi_pcie_wr_conf,
	}
};

static const struct of_device_id hisi_pcie_almost_ecam_of_match[] = {
	{
		.compatible =  "hisilicon,hip06-pcie-ecam",
		.data	    =  &hisi_pcie_platform_ops,
	},
	{
		.compatible =  "hisilicon,hip07-pcie-ecam",
		.data       =  &hisi_pcie_platform_ops,
	},
	{},
};

static struct platform_driver hisi_pcie_almost_ecam_driver = {
	.probe  = pci_host_common_probe,
	.driver = {
		   .name = "hisi-pcie-almost-ecam",
		   .of_match_table = hisi_pcie_almost_ecam_of_match,
		   .suppress_bind_attrs = true,
	},
};
builtin_platform_driver(hisi_pcie_almost_ecam_driver);

#endif
#endif
