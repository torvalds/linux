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

#define PEX_PF0_CONFIG			0xC0014
#define PEX_PF0_CFG_READY		BIT(0)

/* PEX PFa PCIE PME and message interrupt registers*/
#define PEX_PF0_PME_MES_DR		0xC0020
#define PEX_PF0_PME_MES_DR_LUD		BIT(7)
#define PEX_PF0_PME_MES_DR_LDD		BIT(9)
#define PEX_PF0_PME_MES_DR_HRD		BIT(10)

#define PEX_PF0_PME_MES_IER		0xC0028
#define PEX_PF0_PME_MES_IER_LUDIE	BIT(7)
#define PEX_PF0_PME_MES_IER_LDDIE	BIT(9)
#define PEX_PF0_PME_MES_IER_HRDIE	BIT(10)

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
	int				irq;
	u32				lnkcap;
	bool				big_endian;
};

static u32 ls_pcie_pf_lut_readl(struct ls_pcie_ep *pcie, u32 offset)
{
	struct dw_pcie *pci = pcie->pci;

	if (pcie->big_endian)
		return ioread32be(pci->dbi_base + offset);
	else
		return ioread32(pci->dbi_base + offset);
}

static void ls_pcie_pf_lut_writel(struct ls_pcie_ep *pcie, u32 offset, u32 value)
{
	struct dw_pcie *pci = pcie->pci;

	if (pcie->big_endian)
		iowrite32be(value, pci->dbi_base + offset);
	else
		iowrite32(value, pci->dbi_base + offset);
}

static irqreturn_t ls_pcie_ep_event_handler(int irq, void *dev_id)
{
	struct ls_pcie_ep *pcie = dev_id;
	struct dw_pcie *pci = pcie->pci;
	u32 val, cfg;
	u8 offset;

	val = ls_pcie_pf_lut_readl(pcie, PEX_PF0_PME_MES_DR);
	ls_pcie_pf_lut_writel(pcie, PEX_PF0_PME_MES_DR, val);

	if (!val)
		return IRQ_NONE;

	if (val & PEX_PF0_PME_MES_DR_LUD) {

		offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

		/*
		 * The values of the Maximum Link Width and Supported Link
		 * Speed from the Link Capabilities Register will be lost
		 * during link down or hot reset. Restore initial value
		 * that configured by the Reset Configuration Word (RCW).
		 */
		dw_pcie_dbi_ro_wr_en(pci);
		dw_pcie_writel_dbi(pci, offset + PCI_EXP_LNKCAP, pcie->lnkcap);
		dw_pcie_dbi_ro_wr_dis(pci);

		cfg = ls_pcie_pf_lut_readl(pcie, PEX_PF0_CONFIG);
		cfg |= PEX_PF0_CFG_READY;
		ls_pcie_pf_lut_writel(pcie, PEX_PF0_CONFIG, cfg);
		dw_pcie_ep_linkup(&pci->ep);

		dev_dbg(pci->dev, "Link up\n");
	} else if (val & PEX_PF0_PME_MES_DR_LDD) {
		dev_dbg(pci->dev, "Link down\n");
		pci_epc_linkdown(pci->ep.epc);
	} else if (val & PEX_PF0_PME_MES_DR_HRD) {
		dev_dbg(pci->dev, "Hot reset\n");
	}

	return IRQ_HANDLED;
}

static int ls_pcie_ep_interrupt_init(struct ls_pcie_ep *pcie,
				     struct platform_device *pdev)
{
	u32 val;
	int ret;

	pcie->irq = platform_get_irq_byname(pdev, "pme");
	if (pcie->irq < 0)
		return pcie->irq;

	ret = devm_request_irq(&pdev->dev, pcie->irq, ls_pcie_ep_event_handler,
			       IRQF_SHARED, pdev->name, pcie);
	if (ret) {
		dev_err(&pdev->dev, "Can't register PCIe IRQ\n");
		return ret;
	}

	/* Enable interrupts */
	val = ls_pcie_pf_lut_readl(pcie, PEX_PF0_PME_MES_IER);
	val |=  PEX_PF0_PME_MES_IER_LDDIE | PEX_PF0_PME_MES_IER_HRDIE |
		PEX_PF0_PME_MES_IER_LUDIE;
	ls_pcie_pf_lut_writel(pcie, PEX_PF0_PME_MES_IER, val);

	return 0;
}

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
				unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq_doorbell(ep, func_no,
							  interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}
}

static unsigned int ls_pcie_ep_get_dbi_offset(struct dw_pcie_ep *ep, u8 func_no)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct ls_pcie_ep *pcie = to_ls_pcie_ep(pci);

	WARN_ON(func_no && !pcie->drvdata->func_offset);
	return pcie->drvdata->func_offset * func_no;
}

static const struct dw_pcie_ep_ops ls_pcie_ep_ops = {
	.init = ls_pcie_ep_init,
	.raise_irq = ls_pcie_ep_raise_irq,
	.get_features = ls_pcie_ep_get_features,
	.get_dbi_offset = ls_pcie_ep_get_dbi_offset,
};

static const struct ls_pcie_ep_drvdata ls1_ep_drvdata = {
	.ops = &ls_pcie_ep_ops,
};

static const struct ls_pcie_ep_drvdata ls2_ep_drvdata = {
	.func_offset = 0x20000,
	.ops = &ls_pcie_ep_ops,
};

static const struct ls_pcie_ep_drvdata lx2_ep_drvdata = {
	.func_offset = 0x8000,
	.ops = &ls_pcie_ep_ops,
};

static const struct of_device_id ls_pcie_ep_of_match[] = {
	{ .compatible = "fsl,ls1028a-pcie-ep", .data = &ls1_ep_drvdata },
	{ .compatible = "fsl,ls1046a-pcie-ep", .data = &ls1_ep_drvdata },
	{ .compatible = "fsl,ls1088a-pcie-ep", .data = &ls2_ep_drvdata },
	{ .compatible = "fsl,ls2088a-pcie-ep", .data = &ls2_ep_drvdata },
	{ .compatible = "fsl,lx2160ar2-pcie-ep", .data = &lx2_ep_drvdata },
	{ },
};

static int __init ls_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie_ep *pcie;
	struct pci_epc_features *ls_epc;
	struct resource *dbi_base;
	u8 offset;
	int ret;

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

	ls_epc->bar_fixed_64bit = (1 << BAR_2) | (1 << BAR_4);
	ls_epc->linkup_notifier = true;

	pcie->pci = pci;
	pcie->ls_epc = ls_epc;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pci->ep.ops = &ls_pcie_ep_ops;

	pcie->big_endian = of_property_read_bool(dev->of_node, "big-endian");

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	platform_set_drvdata(pdev, pcie);

	offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	pcie->lnkcap = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);

	ret = dw_pcie_ep_init(&pci->ep);
	if (ret)
		return ret;

	return ls_pcie_ep_interrupt_init(pcie, pdev);
}

static struct platform_driver ls_pcie_ep_driver = {
	.driver = {
		.name = "layerscape-pcie-ep",
		.of_match_table = ls_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(ls_pcie_ep_driver, ls_pcie_ep_probe);
