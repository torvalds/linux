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
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/regmap.h>

#include "pcie-designware.h"

struct dw_plat_pcie {
	struct dw_pcie			*pci;
	struct regmap			*regmap;
	enum dw_pcie_device_mode	mode;
};

struct dw_plat_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static const struct of_device_id dw_plat_pcie_of_match[];

static int dw_plat_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	dw_pcie_setup_rc(pp);
	dw_pcie_wait_for_link(pci);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);

	return 0;
}

static void dw_plat_set_num_vectors(struct pcie_port *pp)
{
	pp->num_vectors = MAX_MSI_IRQS;
}

static const struct dw_pcie_host_ops dw_plat_pcie_host_ops = {
	.host_init = dw_plat_pcie_host_init,
	.set_num_vectors = dw_plat_set_num_vectors,
};

static int dw_plat_pcie_establish_link(struct dw_pcie *pci)
{
	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = dw_plat_pcie_establish_link,
};

static void dw_plat_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
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
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq(pdev, 0);
		if (pp->msi_irq < 0)
			return pp->msi_irq;
	}

	pp->ops = &dw_plat_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int dw_plat_add_pcie_ep(struct dw_plat_pcie *dw_plat_pcie,
			       struct platform_device *pdev)
{
	int ret;
	struct dw_pcie_ep *ep;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci = dw_plat_pcie->pci;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi2");
	pci->dbi_base2 = devm_ioremap_resource(dev, res);
	if (IS_ERR(pci->dbi_base2))
		return PTR_ERR(pci->dbi_base2);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize endpoint\n");
		return ret;
	}
	return 0;
}

static int dw_plat_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_plat_pcie *dw_plat_pcie;
	struct dw_pcie *pci;
	struct resource *res;  /* Resource from DT */
	int ret;
	const struct of_device_id *match;
	const struct dw_plat_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	match = of_match_device(dw_plat_pcie_of_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct dw_plat_pcie_of_data *)match->data;
	mode = (enum dw_pcie_device_mode)data->mode;

	dw_plat_pcie = devm_kzalloc(dev, sizeof(*dw_plat_pcie), GFP_KERNEL);
	if (!dw_plat_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	dw_plat_pcie->pci = pci;
	dw_plat_pcie->mode = mode;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	pci->dbi_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	platform_set_drvdata(pdev, dw_plat_pcie);

	switch (dw_plat_pcie->mode) {
	case DW_PCIE_RC_TYPE:
		if (!IS_ENABLED(CONFIG_PCIE_DW_PLAT_HOST))
			return -ENODEV;

		ret = dw_plat_add_pcie_port(dw_plat_pcie, pdev);
		if (ret < 0)
			return ret;
		break;
	case DW_PCIE_EP_TYPE:
		if (!IS_ENABLED(CONFIG_PCIE_DW_PLAT_EP))
			return -ENODEV;

		ret = dw_plat_add_pcie_ep(dw_plat_pcie, pdev);
		if (ret < 0)
			return ret;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", dw_plat_pcie->mode);
	}

	return 0;
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
