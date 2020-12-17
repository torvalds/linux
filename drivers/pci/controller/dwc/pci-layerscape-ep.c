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

#define to_ls_pcie_ep(x)	dev_get_drvdata((x)->dev)

struct ls_pcie_ep_drvdata {
	u32				func_offset;
	const struct dw_pcie_ep_ops	*ops;
	const struct dw_pcie_ops	*dw_pcie_ops;
};

struct ls_pcie_ep {
	struct dw_pcie			*pci;
	struct pci_epc_features		*ls_epc;
	const struct ls_pcie_ep_drvdata *drvdata;
};

static int ls_pcie_establish_link(struct dw_pcie *pci)
{
	return 0;
}

static const struct dw_pcie_ops dw_ls_pcie_ep_ops = {
	.start_link = ls_pcie_establish_link,
};

static const struct pci_epc_features*
ls_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct ls_pcie_ep *pcie = to_ls_pcie_ep(pci);

	return pcie->ls_epc;
}

static void ls_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct ls_pcie_ep *pcie = to_ls_pcie_ep(pci);
	struct dw_pcie_ep_func *ep_func;
	enum pci_barno bar;

	ep_func = dw_pcie_ep_get_func_from_ep(ep, 0);
	if (!ep_func)
		return;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);

	pcie->ls_epc->msi_capable = ep_func->msi_cap ? true : false;
	pcie->ls_epc->msix_capable = ep_func->msix_cap ? true : false;
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
		return dw_pcie_ep_raise_msix_irq_doorbell(ep, func_no,
							  interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}
}

static unsigned int ls_pcie_ep_func_conf_select(struct dw_pcie_ep *ep,
						u8 func_no)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct ls_pcie_ep *pcie = to_ls_pcie_ep(pci);

	WARN_ON(func_no && !pcie->drvdata->func_offset);
	return pcie->drvdata->func_offset * func_no;
}

static const struct dw_pcie_ep_ops ls_pcie_ep_ops = {
	.ep_init = ls_pcie_ep_init,
	.raise_irq = ls_pcie_ep_raise_irq,
	.get_features = ls_pcie_ep_get_features,
	.func_conf_select = ls_pcie_ep_func_conf_select,
};

static const struct ls_pcie_ep_drvdata ls1_ep_drvdata = {
	.ops = &ls_pcie_ep_ops,
	.dw_pcie_ops = &dw_ls_pcie_ep_ops,
};

static const struct ls_pcie_ep_drvdata ls2_ep_drvdata = {
	.func_offset = 0x20000,
	.ops = &ls_pcie_ep_ops,
	.dw_pcie_ops = &dw_ls_pcie_ep_ops,
};

static const struct of_device_id ls_pcie_ep_of_match[] = {
	{ .compatible = "fsl,ls1046a-pcie-ep", .data = &ls1_ep_drvdata },
	{ .compatible = "fsl,ls1088a-pcie-ep", .data = &ls2_ep_drvdata },
	{ .compatible = "fsl,ls2088a-pcie-ep", .data = &ls2_ep_drvdata },
	{ },
};

static int __init ls_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie_ep *pcie;
	struct pci_epc_features *ls_epc;
	struct resource *dbi_base;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	ls_epc = devm_kzalloc(dev, sizeof(*ls_epc), GFP_KERNEL);
	if (!ls_epc)
		return -ENOMEM;

	pcie->drvdata = of_device_get_match_data(dev);

	pci->dev = dev;
	pci->ops = pcie->drvdata->dw_pcie_ops;

	ls_epc->bar_fixed_64bit = (1 << BAR_2) | (1 << BAR_4),

	pcie->pci = pci;
	pcie->ls_epc = ls_epc;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pci->ep.ops = &ls_pcie_ep_ops;

	platform_set_drvdata(pdev, pcie);

	return dw_pcie_ep_init(&pci->ep);
}

static struct platform_driver ls_pcie_ep_driver = {
	.driver = {
		.name = "layerscape-pcie-ep",
		.of_match_table = ls_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(ls_pcie_ep_driver, ls_pcie_ep_probe);
