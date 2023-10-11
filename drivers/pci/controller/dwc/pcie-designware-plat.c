// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe RC driver for Synopsys DesignWare Core
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <Joao.Pinto@synopsys.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>

#include "pcie-designware.h"

struct dw_plat_pcie {
	struct dw_pcie			*pci;
	enum dw_pcie_device_mode	mode;
};

struct dw_plat_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static const struct dw_pcie_host_ops dw_plat_pcie_host_ops = {
};

static void dw_plat_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static int dw_plat_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				     enum pci_epc_irq_type type,
				     u16 interrupt_num)
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
	}

	return 0;
}

static const struct pci_epc_features dw_plat_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = true,
};

static const struct pci_epc_features*
dw_plat_pcie_get_features(struct dw_pcie_ep *ep)
{
	return &dw_plat_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = dw_plat_pcie_ep_init,
	.raise_irq = dw_plat_pcie_ep_raise_irq,
	.get_features = dw_plat_pcie_get_features,
};

static int dw_plat_add_pcie_port(struct dw_plat_pcie *dw_plat_pcie,
				 struct platform_device *pdev)
{
	struct dw_pcie *pci = dw_plat_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &dw_plat_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int dw_plat_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_plat_pcie *dw_plat_pcie;
	struct dw_pcie *pci;
	int ret;
	const struct dw_plat_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	mode = (enum dw_pcie_device_mode)data->mode;

	dw_plat_pcie = devm_kzalloc(dev, sizeof(*dw_plat_pcie), GFP_KERNEL);
	if (!dw_plat_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;

	dw_plat_pcie->pci = pci;
	dw_plat_pcie->mode = mode;

	platform_set_drvdata(pdev, dw_plat_pcie);

	switch (dw_plat_pcie->mode) {
	case DW_PCIE_RC_TYPE:
		if (!IS_ENABLED(CONFIG_PCIE_DW_PLAT_HOST))
			return -ENODEV;

		ret = dw_plat_add_pcie_port(dw_plat_pcie, pdev);
		break;
	case DW_PCIE_EP_TYPE:
		if (!IS_ENABLED(CONFIG_PCIE_DW_PLAT_EP))
			return -ENODEV;

		pci->ep.ops = &pcie_ep_ops;
		ret = dw_pcie_ep_init(&pci->ep);
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", dw_plat_pcie->mode);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct dw_plat_pcie_of_data dw_plat_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct dw_plat_pcie_of_data dw_plat_pcie_ep_of_data = {
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id dw_plat_pcie_of_match[] = {
	{
		.compatible = "snps,dw-pcie",
		.data = &dw_plat_pcie_rc_of_data,
	},
	{
		.compatible = "snps,dw-pcie-ep",
		.data = &dw_plat_pcie_ep_of_data,
	},
	{},
};

static struct platform_driver dw_plat_pcie_driver = {
	.driver = {
		.name	= "dw-pcie",
		.of_match_table = dw_plat_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = dw_plat_pcie_probe,
};
builtin_platform_driver(dw_plat_pcie_driver);
