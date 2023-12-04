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

/* LS1021A PEXn PM Write Control Register */
#define SCFG_PEXPMWRCR(idx)	(0x5c + (idx) * 0x64)
#define PMXMTTURNOFF		BIT(31)
#define SCFG_PEXSFTRSTCR	0x190
#define PEXSR(idx)		BIT(idx)

/* LS1043A PEX PME control register */
#define SCFG_PEXPMECR		0x144
#define PEXPME(idx)		BIT(31 - (idx) * 4)

/* LS1043A PEX LUT debug register */
#define LS_PCIE_LDBG	0x7fc
#define LDBG_SR		BIT(30)
#define LDBG_WE		BIT(31)

#define PCIE_IATU_NUM		6

struct ls_pcie_drvdata {
	const u32 pf_lut_off;
	const struct dw_pcie_host_ops *ops;
	int (*exit_from_l2)(struct dw_pcie_rp *pp);
	bool scfg_support;
	bool pm_support;
};

struct ls_pcie {
	struct dw_pcie *pci;
	const struct ls_pcie_drvdata *drvdata;
	void __iomem *pf_lut_base;
	struct regmap *scfg;
	int index;
	bool big_endian;
};

#define ls_pcie_pf_lut_readl_addr(addr)	ls_pcie_pf_lut_readl(pcie, addr)
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

static u32 ls_pcie_pf_lut_readl(struct ls_pcie *pcie, u32 off)
{
	if (pcie->big_endian)
		return ioread32be(pcie->pf_lut_base + off);

	return ioread32(pcie->pf_lut_base + off);
}

static void ls_pcie_pf_lut_writel(struct ls_pcie *pcie, u32 off, u32 val)
{
	if (pcie->big_endian)
		iowrite32be(val, pcie->pf_lut_base + off);
	else
		iowrite32(val, pcie->pf_lut_base + off);
}

static void ls_pcie_send_turnoff_msg(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 val;
	int ret;

	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_PF_MCR);
	val |= PF_MCR_PTOMR;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_PF_MCR, val);

	ret = readx_poll_timeout(ls_pcie_pf_lut_readl_addr, LS_PCIE_PF_MCR,
				 val, !(val & PF_MCR_PTOMR),
				 PCIE_PME_TO_L2_TIMEOUT_US/10,
				 PCIE_PME_TO_L2_TIMEOUT_US);
	if (ret)
		dev_err(pcie->pci->dev, "PME_Turn_off timeout\n");
}

static int ls_pcie_exit_from_l2(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 val;
	int ret;

	/*
	 * Set PF_MCR_EXL2S bit in LS_PCIE_PF_MCR register for the link
	 * to exit L2 state.
	 */
	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_PF_MCR);
	val |= PF_MCR_EXL2S;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_PF_MCR, val);

	/*
	 * L2 exit timeout of 10ms is not defined in the specifications,
	 * it was chosen based on empirical observations.
	 */
	ret = readx_poll_timeout(ls_pcie_pf_lut_readl_addr, LS_PCIE_PF_MCR,
				 val, !(val & PF_MCR_EXL2S),
				 1000,
				 10000);
	if (ret)
		dev_err(pcie->pci->dev, "L2 exit timeout\n");

	return ret;
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

static void scfg_pcie_send_turnoff_msg(struct regmap *scfg, u32 reg, u32 mask)
{
	/* Send PME_Turn_Off message */
	regmap_write_bits(scfg, reg, mask, mask);

	/*
	 * There is no specific register to check for PME_To_Ack from endpoint.
	 * So on the safe side, wait for PCIE_PME_TO_L2_TIMEOUT_US.
	 */
	mdelay(PCIE_PME_TO_L2_TIMEOUT_US/1000);

	/*
	 * Layerscape hardware reference manual recommends clearing the PMXMTTURNOFF bit
	 * to complete the PME_Turn_Off handshake.
	 */
	regmap_write_bits(scfg, reg, mask, 0);
}

static void ls1021a_pcie_send_turnoff_msg(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);

	scfg_pcie_send_turnoff_msg(pcie->scfg, SCFG_PEXPMWRCR(pcie->index), PMXMTTURNOFF);
}

static int scfg_pcie_exit_from_l2(struct regmap *scfg, u32 reg, u32 mask)
{
	/* Reset the PEX wrapper to bring the link out of L2 */
	regmap_write_bits(scfg, reg, mask, mask);
	regmap_write_bits(scfg, reg, mask, 0);

	return 0;
}

static int ls1021a_pcie_exit_from_l2(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);

	return scfg_pcie_exit_from_l2(pcie->scfg, SCFG_PEXSFTRSTCR, PEXSR(pcie->index));
}

static void ls1043a_pcie_send_turnoff_msg(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);

	scfg_pcie_send_turnoff_msg(pcie->scfg, SCFG_PEXPMECR, PEXPME(pcie->index));
}

static int ls1043a_pcie_exit_from_l2(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 val;

	/*
	 * Reset the PEX wrapper to bring the link out of L2.
	 * LDBG_WE: allows the user to have write access to the PEXDBG[SR] for both setting and
	 *	    clearing the soft reset on the PEX module.
	 * LDBG_SR: When SR is set to 1, the PEX module enters soft reset.
	 */
	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_LDBG);
	val |= LDBG_WE;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_LDBG, val);

	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_LDBG);
	val |= LDBG_SR;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_LDBG, val);

	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_LDBG);
	val &= ~LDBG_SR;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_LDBG, val);

	val = ls_pcie_pf_lut_readl(pcie, LS_PCIE_LDBG);
	val &= ~LDBG_WE;
	ls_pcie_pf_lut_writel(pcie, LS_PCIE_LDBG, val);

	return 0;
}

static const struct dw_pcie_host_ops ls_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
	.pme_turn_off = ls_pcie_send_turnoff_msg,
};

static const struct dw_pcie_host_ops ls1021a_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
	.pme_turn_off = ls1021a_pcie_send_turnoff_msg,
};

static const struct ls_pcie_drvdata ls1021a_drvdata = {
	.pm_support = true,
	.scfg_support = true,
	.ops = &ls1021a_pcie_host_ops,
	.exit_from_l2 = ls1021a_pcie_exit_from_l2,
};

static const struct dw_pcie_host_ops ls1043a_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
	.pme_turn_off = ls1043a_pcie_send_turnoff_msg,
};

static const struct ls_pcie_drvdata ls1043a_drvdata = {
	.pf_lut_off = 0x10000,
	.pm_support = true,
	.scfg_support = true,
	.ops = &ls1043a_pcie_host_ops,
	.exit_from_l2 = ls1043a_pcie_exit_from_l2,
};

static const struct ls_pcie_drvdata layerscape_drvdata = {
	.pf_lut_off = 0xc0000,
	.pm_support = true,
	.ops = &ls_pcie_host_ops,
	.exit_from_l2 = ls_pcie_exit_from_l2,
};

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1012a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls1021a-pcie", .data = &ls1021a_drvdata },
	{ .compatible = "fsl,ls1028a-pcie", .data = &layerscape_drvdata },
	{ .compatible = "fsl,ls1043a-pcie", .data = &ls1043a_drvdata },
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
	u32 index[2];
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pcie->drvdata = of_device_get_match_data(dev);

	pci->dev = dev;
	pcie->pci = pci;
	pci->pp.ops = pcie->drvdata->ops;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pcie->big_endian = of_property_read_bool(dev->of_node, "big-endian");

	pcie->pf_lut_base = pci->dbi_base + pcie->drvdata->pf_lut_off;

	if (pcie->drvdata->scfg_support) {
		pcie->scfg = syscon_regmap_lookup_by_phandle(dev->of_node, "fsl,pcie-scfg");
		if (IS_ERR(pcie->scfg)) {
			dev_err(dev, "No syscfg phandle specified\n");
			return PTR_ERR(pcie->scfg);
		}

		ret = of_property_read_u32_array(dev->of_node, "fsl,pcie-scfg", index, 2);
		if (ret)
			return ret;

		pcie->index = index[1];
	}

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
	int ret;

	if (!pcie->drvdata->pm_support)
		return 0;

	ret = pcie->drvdata->exit_from_l2(&pcie->pci->pp);
	if (ret)
		return ret;

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
