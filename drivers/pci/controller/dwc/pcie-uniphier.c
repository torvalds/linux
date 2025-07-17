// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for UniPhier SoCs
 * Copyright 2018 Socionext Inc.
 * Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "pcie-designware.h"

#define PCL_PINCTRL0			0x002c
#define PCL_PERST_PLDN_REGEN		BIT(12)
#define PCL_PERST_NOE_REGEN		BIT(11)
#define PCL_PERST_OUT_REGEN		BIT(8)
#define PCL_PERST_PLDN_REGVAL		BIT(4)
#define PCL_PERST_NOE_REGVAL		BIT(3)
#define PCL_PERST_OUT_REGVAL		BIT(0)

#define PCL_PIPEMON			0x0044
#define PCL_PCLK_ALIVE			BIT(15)

#define PCL_MODE			0x8000
#define PCL_MODE_REGEN			BIT(8)
#define PCL_MODE_REGVAL			BIT(0)

#define PCL_APP_READY_CTRL		0x8008
#define PCL_APP_LTSSM_ENABLE		BIT(0)

#define PCL_APP_PM0			0x8078
#define PCL_SYS_AUX_PWR_DET		BIT(8)

#define PCL_RCV_INT			0x8108
#define PCL_RCV_INT_ALL_ENABLE		GENMASK(20, 17)
#define PCL_CFG_BW_MGT_STATUS		BIT(4)
#define PCL_CFG_LINK_AUTO_BW_STATUS	BIT(3)
#define PCL_CFG_AER_RC_ERR_MSI_STATUS	BIT(2)
#define PCL_CFG_PME_MSI_STATUS		BIT(1)

#define PCL_RCV_INTX			0x810c
#define PCL_RCV_INTX_ALL_ENABLE		GENMASK(19, 16)
#define PCL_RCV_INTX_ALL_MASK		GENMASK(11, 8)
#define PCL_RCV_INTX_MASK_SHIFT		8
#define PCL_RCV_INTX_ALL_STATUS		GENMASK(3, 0)
#define PCL_RCV_INTX_STATUS_SHIFT	0

#define PCL_STATUS_LINK			0x8140
#define PCL_RDLH_LINK_UP		BIT(1)
#define PCL_XMLH_LINK_UP		BIT(0)

struct uniphier_pcie {
	struct dw_pcie pci;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	struct phy *phy;
	struct irq_domain *intx_irq_domain;
};

#define to_uniphier_pcie(x)	dev_get_drvdata((x)->dev)

static void uniphier_pcie_ltssm_enable(struct uniphier_pcie *pcie,
				       bool enable)
{
	u32 val;

	val = readl(pcie->base + PCL_APP_READY_CTRL);
	if (enable)
		val |= PCL_APP_LTSSM_ENABLE;
	else
		val &= ~PCL_APP_LTSSM_ENABLE;
	writel(val, pcie->base + PCL_APP_READY_CTRL);
}

static void uniphier_pcie_init_rc(struct uniphier_pcie *pcie)
{
	u32 val;

	/* set RC MODE */
	val = readl(pcie->base + PCL_MODE);
	val |= PCL_MODE_REGEN;
	val &= ~PCL_MODE_REGVAL;
	writel(val, pcie->base + PCL_MODE);

	/* use auxiliary power detection */
	val = readl(pcie->base + PCL_APP_PM0);
	val |= PCL_SYS_AUX_PWR_DET;
	writel(val, pcie->base + PCL_APP_PM0);

	/* assert PERST# */
	val = readl(pcie->base + PCL_PINCTRL0);
	val &= ~(PCL_PERST_NOE_REGVAL | PCL_PERST_OUT_REGVAL
		 | PCL_PERST_PLDN_REGVAL);
	val |= PCL_PERST_NOE_REGEN | PCL_PERST_OUT_REGEN
		| PCL_PERST_PLDN_REGEN;
	writel(val, pcie->base + PCL_PINCTRL0);

	uniphier_pcie_ltssm_enable(pcie, false);

	usleep_range(100000, 200000);

	/* deassert PERST# */
	val = readl(pcie->base + PCL_PINCTRL0);
	val |= PCL_PERST_OUT_REGVAL | PCL_PERST_OUT_REGEN;
	writel(val, pcie->base + PCL_PINCTRL0);
}

static int uniphier_pcie_wait_rc(struct uniphier_pcie *pcie)
{
	u32 status;
	int ret;

	/* wait PIPE clock */
	ret = readl_poll_timeout(pcie->base + PCL_PIPEMON, status,
				 status & PCL_PCLK_ALIVE, 100000, 1000000);
	if (ret) {
		dev_err(pcie->pci.dev,
			"Failed to initialize controller in RC mode\n");
		return ret;
	}

	return 0;
}

static bool uniphier_pcie_link_up(struct dw_pcie *pci)
{
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	u32 val, mask;

	val = readl(pcie->base + PCL_STATUS_LINK);
	mask = PCL_RDLH_LINK_UP | PCL_XMLH_LINK_UP;

	return (val & mask) == mask;
}

static int uniphier_pcie_start_link(struct dw_pcie *pci)
{
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);

	uniphier_pcie_ltssm_enable(pcie, true);

	return 0;
}

static void uniphier_pcie_stop_link(struct dw_pcie *pci)
{
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);

	uniphier_pcie_ltssm_enable(pcie, false);
}

static void uniphier_pcie_irq_enable(struct uniphier_pcie *pcie)
{
	writel(PCL_RCV_INT_ALL_ENABLE, pcie->base + PCL_RCV_INT);
	writel(PCL_RCV_INTX_ALL_ENABLE, pcie->base + PCL_RCV_INTX);
}


static void uniphier_pcie_irq_mask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pp->lock, flags);

	val = readl(pcie->base + PCL_RCV_INTX);
	val |= BIT(irqd_to_hwirq(d) + PCL_RCV_INTX_MASK_SHIFT);
	writel(val, pcie->base + PCL_RCV_INTX);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void uniphier_pcie_irq_unmask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pp->lock, flags);

	val = readl(pcie->base + PCL_RCV_INTX);
	val &= ~BIT(irqd_to_hwirq(d) + PCL_RCV_INTX_MASK_SHIFT);
	writel(val, pcie->base + PCL_RCV_INTX);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static struct irq_chip uniphier_pcie_irq_chip = {
	.name = "PCI",
	.irq_mask = uniphier_pcie_irq_mask,
	.irq_unmask = uniphier_pcie_irq_unmask,
};

static int uniphier_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &uniphier_pcie_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops uniphier_intx_domain_ops = {
	.map = uniphier_pcie_intx_map,
};

static void uniphier_pcie_irq_handler(struct irq_desc *desc)
{
	struct dw_pcie_rp *pp = irq_desc_get_handler_data(desc);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long reg;
	u32 val, bit;

	/* INT for debug */
	val = readl(pcie->base + PCL_RCV_INT);

	if (val & PCL_CFG_BW_MGT_STATUS)
		dev_dbg(pci->dev, "Link Bandwidth Management Event\n");
	if (val & PCL_CFG_LINK_AUTO_BW_STATUS)
		dev_dbg(pci->dev, "Link Autonomous Bandwidth Event\n");
	if (val & PCL_CFG_AER_RC_ERR_MSI_STATUS)
		dev_dbg(pci->dev, "Root Error\n");
	if (val & PCL_CFG_PME_MSI_STATUS)
		dev_dbg(pci->dev, "PME Interrupt\n");

	writel(val, pcie->base + PCL_RCV_INT);

	/* INTx */
	chained_irq_enter(chip, desc);

	val = readl(pcie->base + PCL_RCV_INTX);
	reg = FIELD_GET(PCL_RCV_INTX_ALL_STATUS, val);

	for_each_set_bit(bit, &reg, PCI_NUM_INTX)
		generic_handle_domain_irq(pcie->intx_irq_domain, bit);

	chained_irq_exit(chip, desc);
}

static int uniphier_pcie_config_intx_irq(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	struct device_node *np = pci->dev->of_node;
	struct device_node *np_intc;
	int ret = 0;

	np_intc = of_get_child_by_name(np, "legacy-interrupt-controller");
	if (!np_intc) {
		dev_err(pci->dev, "Failed to get legacy-interrupt-controller node\n");
		return -EINVAL;
	}

	pp->irq = irq_of_parse_and_map(np_intc, 0);
	if (!pp->irq) {
		dev_err(pci->dev, "Failed to get an IRQ entry in legacy-interrupt-controller\n");
		ret = -EINVAL;
		goto out_put_node;
	}

	pcie->intx_irq_domain = irq_domain_create_linear(of_fwnode_handle(np_intc), PCI_NUM_INTX,
						&uniphier_intx_domain_ops, pp);
	if (!pcie->intx_irq_domain) {
		dev_err(pci->dev, "Failed to get INTx domain\n");
		ret = -ENODEV;
		goto out_put_node;
	}

	irq_set_chained_handler_and_data(pp->irq, uniphier_pcie_irq_handler,
					 pp);

out_put_node:
	of_node_put(np_intc);
	return ret;
}

static int uniphier_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct uniphier_pcie *pcie = to_uniphier_pcie(pci);
	int ret;

	ret = uniphier_pcie_config_intx_irq(pp);
	if (ret)
		return ret;

	uniphier_pcie_irq_enable(pcie);

	return 0;
}

static const struct dw_pcie_host_ops uniphier_pcie_host_ops = {
	.init = uniphier_pcie_host_init,
};

static int uniphier_pcie_host_enable(struct uniphier_pcie *pcie)
{
	int ret;

	ret = clk_prepare_enable(pcie->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(pcie->rst);
	if (ret)
		goto out_clk_disable;

	uniphier_pcie_init_rc(pcie);

	ret = phy_init(pcie->phy);
	if (ret)
		goto out_rst_assert;

	ret = uniphier_pcie_wait_rc(pcie);
	if (ret)
		goto out_phy_exit;

	return 0;

out_phy_exit:
	phy_exit(pcie->phy);
out_rst_assert:
	reset_control_assert(pcie->rst);
out_clk_disable:
	clk_disable_unprepare(pcie->clk);

	return ret;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = uniphier_pcie_start_link,
	.stop_link = uniphier_pcie_stop_link,
	.link_up = uniphier_pcie_link_up,
};

static int uniphier_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_pcie *pcie;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pci.dev = dev;
	pcie->pci.ops = &dw_pcie_ops;

	pcie->base = devm_platform_ioremap_resource_byname(pdev, "link");
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	pcie->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pcie->clk))
		return PTR_ERR(pcie->clk);

	pcie->rst = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(pcie->rst))
		return PTR_ERR(pcie->rst);

	pcie->phy = devm_phy_optional_get(dev, "pcie-phy");
	if (IS_ERR(pcie->phy))
		return PTR_ERR(pcie->phy);

	platform_set_drvdata(pdev, pcie);

	ret = uniphier_pcie_host_enable(pcie);
	if (ret)
		return ret;

	pcie->pci.pp.ops = &uniphier_pcie_host_ops;

	return dw_pcie_host_init(&pcie->pci.pp);
}

static const struct of_device_id uniphier_pcie_match[] = {
	{ .compatible = "socionext,uniphier-pcie", },
	{ /* sentinel */ },
};

static struct platform_driver uniphier_pcie_driver = {
	.probe  = uniphier_pcie_probe,
	.driver = {
		.name = "uniphier-pcie",
		.of_match_table = uniphier_pcie_match,
	},
};
builtin_platform_driver(uniphier_pcie_driver);
