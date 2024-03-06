// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for ST Microelectronics SPEAr13xx SoCs
 *
 * SPEAr13xx PCIe Glue Layer Source Code
 *
 * Copyright (C) 2010-2014 ST Microelectronics
 * Pratyush Anand <pratyush.anand@gmail.com>
 * Mohit Kumar <mohit.kumar.dhaka@gmail.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>

#include "pcie-designware.h"

struct spear13xx_pcie {
	struct dw_pcie		*pci;
	void __iomem		*app_base;
	struct phy		*phy;
	struct clk		*clk;
};

struct pcie_app_reg {
	u32	app_ctrl_0;		/* cr0 */
	u32	app_ctrl_1;		/* cr1 */
	u32	app_status_0;		/* cr2 */
	u32	app_status_1;		/* cr3 */
	u32	msg_status;		/* cr4 */
	u32	msg_payload;		/* cr5 */
	u32	int_sts;		/* cr6 */
	u32	int_clr;		/* cr7 */
	u32	int_mask;		/* cr8 */
	u32	mst_bmisc;		/* cr9 */
	u32	phy_ctrl;		/* cr10 */
	u32	phy_status;		/* cr11 */
	u32	cxpl_debug_info_0;	/* cr12 */
	u32	cxpl_debug_info_1;	/* cr13 */
	u32	ven_msg_ctrl_0;		/* cr14 */
	u32	ven_msg_ctrl_1;		/* cr15 */
	u32	ven_msg_data_0;		/* cr16 */
	u32	ven_msg_data_1;		/* cr17 */
	u32	ven_msi_0;		/* cr18 */
	u32	ven_msi_1;		/* cr19 */
	u32	mst_rmisc;		/* cr20 */
};

/* CR0 ID */
#define APP_LTSSM_ENABLE_ID			3
#define DEVICE_TYPE_RC				(4 << 25)
#define MISCTRL_EN_ID				30
#define REG_TRANSLATION_ENABLE			31

/* CR3 ID */
#define XMLH_LINK_UP				(1 << 6)

/* CR6 */
#define MSI_CTRL_INT				(1 << 26)

#define to_spear13xx_pcie(x)	dev_get_drvdata((x)->dev)

static int spear13xx_pcie_start_link(struct dw_pcie *pci)
{
	struct spear13xx_pcie *spear13xx_pcie = to_spear13xx_pcie(pci);
	struct pcie_app_reg __iomem *app_reg = spear13xx_pcie->app_base;

	/* enable ltssm */
	writel(DEVICE_TYPE_RC | (1 << MISCTRL_EN_ID)
			| (1 << APP_LTSSM_ENABLE_ID)
			| ((u32)1 << REG_TRANSLATION_ENABLE),
			&app_reg->app_ctrl_0);

	return 0;
}

static irqreturn_t spear13xx_pcie_irq_handler(int irq, void *arg)
{
	struct spear13xx_pcie *spear13xx_pcie = arg;
	struct pcie_app_reg __iomem *app_reg = spear13xx_pcie->app_base;
	struct dw_pcie *pci = spear13xx_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	unsigned int status;

	status = readl(&app_reg->int_sts);

	if (status & MSI_CTRL_INT) {
		BUG_ON(!IS_ENABLED(CONFIG_PCI_MSI));
		dw_handle_msi_irq(pp);
	}

	writel(status, &app_reg->int_clr);

	return IRQ_HANDLED;
}

static void spear13xx_pcie_enable_interrupts(struct spear13xx_pcie *spear13xx_pcie)
{
	struct pcie_app_reg __iomem *app_reg = spear13xx_pcie->app_base;

	/* Enable MSI interrupt */
	if (IS_ENABLED(CONFIG_PCI_MSI))
		writel(readl(&app_reg->int_mask) |
				MSI_CTRL_INT, &app_reg->int_mask);
}

static int spear13xx_pcie_link_up(struct dw_pcie *pci)
{
	struct spear13xx_pcie *spear13xx_pcie = to_spear13xx_pcie(pci);
	struct pcie_app_reg __iomem *app_reg = spear13xx_pcie->app_base;

	if (readl(&app_reg->app_status_1) & XMLH_LINK_UP)
		return 1;

	return 0;
}

static int spear13xx_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct spear13xx_pcie *spear13xx_pcie = to_spear13xx_pcie(pci);
	u32 exp_cap_off = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;

	spear13xx_pcie->app_base = pci->dbi_base + 0x2000;

	/*
	 * this controller support only 128 bytes read size, however its
	 * default value in capability register is 512 bytes. So force
	 * it to 128 here.
	 */
	val = dw_pcie_readw_dbi(pci, exp_cap_off + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_READRQ;
	dw_pcie_writew_dbi(pci, exp_cap_off + PCI_EXP_DEVCTL, val);

	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, 0x104A);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, 0xCD80);

	spear13xx_pcie_enable_interrupts(spear13xx_pcie);

	return 0;
}

static const struct dw_pcie_host_ops spear13xx_pcie_host_ops = {
	.init = spear13xx_pcie_host_init,
};

static int spear13xx_add_pcie_port(struct spear13xx_pcie *spear13xx_pcie,
				   struct platform_device *pdev)
{
	struct dw_pcie *pci = spear13xx_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	ret = devm_request_irq(dev, pp->irq, spear13xx_pcie_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "spear1340-pcie", spear13xx_pcie);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", pp->irq);
		return ret;
	}

	pp->ops = &spear13xx_pcie_host_ops;
	pp->msi_irq[0] = -ENODEV;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = spear13xx_pcie_link_up,
	.start_link = spear13xx_pcie_start_link,
};

static int spear13xx_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct spear13xx_pcie *spear13xx_pcie;
	struct device_node *np = dev->of_node;
	int ret;

	spear13xx_pcie = devm_kzalloc(dev, sizeof(*spear13xx_pcie), GFP_KERNEL);
	if (!spear13xx_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	spear13xx_pcie->pci = pci;

	spear13xx_pcie->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(spear13xx_pcie->phy)) {
		ret = PTR_ERR(spear13xx_pcie->phy);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "probe deferred\n");
		else
			dev_err(dev, "couldn't get pcie-phy\n");
		return ret;
	}

	phy_init(spear13xx_pcie->phy);

	spear13xx_pcie->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(spear13xx_pcie->clk)) {
		dev_err(dev, "couldn't get clk for pcie\n");
		return PTR_ERR(spear13xx_pcie->clk);
	}
	ret = clk_prepare_enable(spear13xx_pcie->clk);
	if (ret) {
		dev_err(dev, "couldn't enable clk for pcie\n");
		return ret;
	}

	if (of_property_read_bool(np, "st,pcie-is-gen1"))
		pci->link_gen = 1;

	platform_set_drvdata(pdev, spear13xx_pcie);

	ret = spear13xx_add_pcie_port(spear13xx_pcie, pdev);
	if (ret < 0)
		goto fail_clk;

	return 0;

fail_clk:
	clk_disable_unprepare(spear13xx_pcie->clk);

	return ret;
}

static const struct of_device_id spear13xx_pcie_of_match[] = {
	{ .compatible = "st,spear1340-pcie", },
	{},
};

static struct platform_driver spear13xx_pcie_driver = {
	.probe		= spear13xx_pcie_probe,
	.driver = {
		.name	= "spear-pcie",
		.of_match_table = spear13xx_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(spear13xx_pcie_driver);
