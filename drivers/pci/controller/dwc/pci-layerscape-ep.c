// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe controller EP driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2018 NXP Semiconductor.
 *
 * Author: Xiaowei Bao <xiaowei.bao@nxp.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>

#include "pcie-designware.h"

#define PCIE_DBI2_OFFSET		0x1000	/* DBI2 base address*/

struct ls_pcie_ep {
	struct dw_pcie		*pci;
};

#define to_ls_pcie_ep(x)	dev_get_drvdata((x)->dev)

static int ls_pcie_establish_link(struct dw_pcie *pci)
{
	return 0;
}

static const struct dw_pcie_ops ls_pcie_ep_ops = {
	.start_link = ls_pcie_establish_link,
};

static const struct of_device_id ls_pcie_ep_of_match[] = {
	{ .compatible = "fsl,ls-pcie-ep",},
	{ },
};

static const struct pci_epc_features ls_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
};

static const struct pci_epc_features*
ls_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &ls_pcie_epc_features;
}

static void ls_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static int ls_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				  enum pci_epc_irq_type type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_EPC_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = ls_pcie_ep_init,
	.raise_irq = ls_pcie_ep_raise_irq,
	.get_features = ls_pcie_ep_get_features,
};

static int __init ls_add_pcie_ep(struct ls_pcie_ep *pcie,
					struct platform_device *pdev)
{
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	struct dw_pcie_ep *ep;
	struct resource *res;
	int ret;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}

	return 0;
}

static int __init ls_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie_ep *pcie;
	struct resource *dbi_base;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pci->dbi_base2 = pci->dbi_base + PCIE_DBI2_OFFSET;
	pci->dev = dev;
	pci->ops = &ls_pcie_ep_ops;
	pcie->pci = pci;

	platform_set_drvdata(pdev, pcie);

	ret = ls_add_pcie_ep(pcie, pdev);

	return ret;
}

static struct platform_driver ls_pcie_ep_driver = {
	.driver = {
		.name = "layerscape-pcie-ep",
		.of_match_table = ls_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(ls_pcie_ep_driver, ls_pcie_ep_probe);
