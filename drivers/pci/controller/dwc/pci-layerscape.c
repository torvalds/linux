// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
 * Copyright 2021 NXP
 *
 * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "../../pci.h"
#include "pcie-designware.h"

/* PEX Internal Configuration Registers */
#define PCIE_STRFMR1		0x71c /* Symbol Timer & Filter Mask Register1 */
#define PCIE_ABSERR		0x8d0 /* Bridge Slave Error Response Register */
#define PCIE_ABSERR_SETTING	0x9401 /* Forward error of non-posted request */

/* PF Message Command Register */
#define LS_PCIE_PF_MCR		0x2c
#define PF_MCR_PTOMR		BIT(0)
#define PF_MCR_EXL2S		BIT(1)

#define PCIE_IATU_NUM		6

struct ls_pcie_drvdata {
	const u32 pf_off;
	bool pm_support;
};

struct ls_pcie {
	struct dw_pcie *pci;
	const struct ls_pcie_drvdata *drvdata;
	void __iomem *pf_base;
	bool big_endian;
};

#define ls_pcie_pf_readl_addr(addr)	ls_pcie_pf_readl(pcie, addr)
#define to_ls_pcie(x)	dev_get_drvdata((x)->dev)

static bool ls_pcie_is_bridge(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u32 header_type;

	header_type = ioread8(pci->dbi_base + PCI_HEADER_TYPE);
	header_type &= PCI_HEADER_TYPE_MASK;

	return header_type == PCI_HEADER_TYPE_BRIDGE;
}

/* Clear multi-function bit */
static void ls_pcie_clear_multifunction(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	iowrite8(PCI_HEADER_TYPE_BRIDGE, pci->dbi_base + PCI_HEADER_TYPE);
}

/* Drop MSG TLP except for Vendor MSG */
static void ls_pcie_drop_msg_tlp(struct ls_pcie *pcie)
{
	u32 val;
	struct dw_pcie *pci = pcie->pci;

	val = ioread32(pci->dbi_base + PCIE_STRFMR1);
	val &= 0xDFFFFFFF;
	iowrite32(val, pci->dbi_base + PCIE_STRFMR1);
}

/* Forward error response of outbound non-posted requests */
static void ls_pcie_fix_error_response(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	iowrite32(PCIE_ABSERR_SETTING, pci->dbi_base + PCIE_ABSERR);
}

static u32 ls_pcie_pf_readl(struct ls_pcie *pcie, u32 off)
{
	if (pcie->big_endian)
		return ioread32be(pcie->pf_base + off);

	return ioread32(pcie->pf_base + off);
}

static void ls_pcie_pf_writel(struct ls_pcie *pcie, u32 off, u32 val)
{
	if (pcie->big_endian)
		iowrite32be(val, pcie->pf_base + off);
	else
		iowrite32(val, pcie->pf_base + off);
}

static void ls_pcie_send_turnoff_msg(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 val;
	int ret;

	val = ls_pcie_pf_readl(pcie, LS_PCIE_PF_MCR);
	val |= PF_MCR_PTOMR;
	ls_pcie_pf_writel(pcie, LS_PCIE_PF_MCR, val);

	ret = readx_poll_timeout(ls_pcie_pf_readl_addr, LS_PCIE_PF_MCR,
				 val, !(val & PF_MCR_PTOMR),
				 PCIE_PME_TO_L2_TIMEOUT_US/10,
				 PCIE_PME_TO_L2_TIMEOUT_US);
	if (ret)
		dev_err(pcie->pci->dev, "PME_Turn_off timeout\n");
}

static void ls_pcie_exit_from_l2(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 val;
	int ret;

	/*
	 * Set PF_MCR_EXL2S bit in LS_PCIE_PF_MCR register for the link
	 * to exit L2 state.
	 */
	val = ls_pcie_pf_readl(pcie, LS_PCIE_PF_MCR);
	val |= PF_MCR_EXL2S;
	ls_pcie_pf_writel(pcie, LS_PCIE_PF_MCR, val);

	/*
	 * L2 exit timeout of 10ms is not defined in the specifications,
	 * it was chosen based on empirical observations.
	 */
	ret = readx_poll_timeout(ls_pcie_pf_readl_addr, LS_PCIE_PF_MCR,
				 val, !(val & PF_MCR_EXL2S),
				 1000,
				 10000);
	if (ret)
		dev_err(pcie->pci->dev, "L2 exit timeout\n");
}

static int ls_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);

	ls_pcie_fix_error_response(pcie);

	dw_pcie_dbi_ro_wr_en(pci);
	ls_pcie_clear_multifunction(pcie);
	dw_pcie_dbi_ro_wr_dis(pci);

	ls_pcie_drop_msg_tlp(pcie);

	return 0;
}

static const struct dw_pcie_host_ops ls_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
	.pme_turn_off = ls_pcie_send_turnoff_msg,
};

static const struct ls_pcie_drvdata ls1021a_drvdata = {
	.pm_support = false,
};

static const struct ls_pcie_drvdata layerscape_drvdata = {
	.pf_off = 0xc0000,
	.pm_support = true,
};

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1012a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls1021a-pcie", .data = &ls1021a_drvdata },
	{ .compatible = "fsl,ls1028a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls1043a-pcie", .data = &ls1021a_drvdata },
	{ .compatible = "fsl,ls1046a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls2080a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls2085a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls2088a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls1088a-pcie", .data = &layerscape_drvdata },
	{ },
};

static int ls_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie *pcie;
	struct resource *dbi_base;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pcie->drvdata = of_device_get_match_data(dev);

	pci->dev = dev;
	pci->pp.ops = &ls_pcie_host_ops;

	pcie->pci = pci;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pcie->big_endian = of_property_read_bool(dev->of_node, "big-endian");

	pcie->pf_base = pci->dbi_base + pcie->drvdata->pf_off;

	if (!ls_pcie_is_bridge(pcie))
		return -ENODEV;

	platform_set_drvdata(pdev, pcie);

	return dw_pcie_host_init(&pci->pp);
}

static int ls_pcie_suspend_noirq(struct device *dev)
{
	struct ls_pcie *pcie = dev_get_drvdata(dev);

	if (!pcie->drvdata->pm_support)
		return 0;

	return dw_pcie_suspend_noirq(pcie->pci);
}

static int ls_pcie_resume_noirq(struct device *dev)
{
	struct ls_pcie *pcie = dev_get_drvdata(dev);

	if (!pcie->drvdata->pm_support)
		return 0;

	ls_pcie_exit_from_l2(&pcie->pci->pp);

	return dw_pcie_resume_noirq(pcie->pci);
}

static const struct dev_pm_ops ls_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(ls_pcie_suspend_noirq, ls_pcie_resume_noirq)
};

static struct platform_driver ls_pcie_driver = {
	.probe = ls_pcie_probe,
	.driver = {
		.name = "layerscape-pcie",
		.of_match_table = ls_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &ls_pcie_pm_ops,
	},
};
builtin_platform_driver(ls_pcie_driver);
