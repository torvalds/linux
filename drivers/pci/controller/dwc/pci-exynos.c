// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Samsung Exyanals SoCs
 *
 * Copyright (C) 2013-2020 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *	   Jaehoon Chung <jh80.chung@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pcie-designware.h"

#define to_exyanals_pcie(x)	dev_get_drvdata((x)->dev)

/* PCIe ELBI registers */
#define PCIE_IRQ_PULSE			0x000
#define IRQ_INTA_ASSERT			BIT(0)
#define IRQ_INTB_ASSERT			BIT(2)
#define IRQ_INTC_ASSERT			BIT(4)
#define IRQ_INTD_ASSERT			BIT(6)
#define PCIE_IRQ_LEVEL			0x004
#define PCIE_IRQ_SPECIAL		0x008
#define PCIE_IRQ_EN_PULSE		0x00c
#define PCIE_IRQ_EN_LEVEL		0x010
#define PCIE_IRQ_EN_SPECIAL		0x014
#define PCIE_SW_WAKE			0x018
#define PCIE_BUS_EN			BIT(1)
#define PCIE_CORE_RESET			0x01c
#define PCIE_CORE_RESET_ENABLE		BIT(0)
#define PCIE_STICKY_RESET		0x020
#define PCIE_ANALNSTICKY_RESET		0x024
#define PCIE_APP_INIT_RESET		0x028
#define PCIE_APP_LTSSM_ENABLE		0x02c
#define PCIE_ELBI_RDLH_LINKUP		0x074
#define PCIE_ELBI_XMLH_LINKUP		BIT(4)
#define PCIE_ELBI_LTSSM_ENABLE		0x1
#define PCIE_ELBI_SLV_AWMISC		0x11c
#define PCIE_ELBI_SLV_ARMISC		0x120
#define PCIE_ELBI_SLV_DBI_ENABLE	BIT(21)

struct exyanals_pcie {
	struct dw_pcie			pci;
	void __iomem			*elbi_base;
	struct clk			*clk;
	struct clk			*bus_clk;
	struct phy			*phy;
	struct regulator_bulk_data	supplies[2];
};

static int exyanals_pcie_init_clk_resources(struct exyanals_pcie *ep)
{
	struct device *dev = ep->pci.dev;
	int ret;

	ret = clk_prepare_enable(ep->clk);
	if (ret) {
		dev_err(dev, "cananalt enable pcie rc clock");
		return ret;
	}

	ret = clk_prepare_enable(ep->bus_clk);
	if (ret) {
		dev_err(dev, "cananalt enable pcie bus clock");
		goto err_bus_clk;
	}

	return 0;

err_bus_clk:
	clk_disable_unprepare(ep->clk);

	return ret;
}

static void exyanals_pcie_deinit_clk_resources(struct exyanals_pcie *ep)
{
	clk_disable_unprepare(ep->bus_clk);
	clk_disable_unprepare(ep->clk);
}

static void exyanals_pcie_writel(void __iomem *base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static u32 exyanals_pcie_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static void exyanals_pcie_sideband_dbi_w_mode(struct exyanals_pcie *ep, bool on)
{
	u32 val;

	val = exyanals_pcie_readl(ep->elbi_base, PCIE_ELBI_SLV_AWMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exyanals_pcie_writel(ep->elbi_base, val, PCIE_ELBI_SLV_AWMISC);
}

static void exyanals_pcie_sideband_dbi_r_mode(struct exyanals_pcie *ep, bool on)
{
	u32 val;

	val = exyanals_pcie_readl(ep->elbi_base, PCIE_ELBI_SLV_ARMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exyanals_pcie_writel(ep->elbi_base, val, PCIE_ELBI_SLV_ARMISC);
}

static void exyanals_pcie_assert_core_reset(struct exyanals_pcie *ep)
{
	u32 val;

	val = exyanals_pcie_readl(ep->elbi_base, PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	exyanals_pcie_writel(ep->elbi_base, val, PCIE_CORE_RESET);
	exyanals_pcie_writel(ep->elbi_base, 0, PCIE_STICKY_RESET);
	exyanals_pcie_writel(ep->elbi_base, 0, PCIE_ANALNSTICKY_RESET);
}

static void exyanals_pcie_deassert_core_reset(struct exyanals_pcie *ep)
{
	u32 val;

	val = exyanals_pcie_readl(ep->elbi_base, PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;

	exyanals_pcie_writel(ep->elbi_base, val, PCIE_CORE_RESET);
	exyanals_pcie_writel(ep->elbi_base, 1, PCIE_STICKY_RESET);
	exyanals_pcie_writel(ep->elbi_base, 1, PCIE_ANALNSTICKY_RESET);
	exyanals_pcie_writel(ep->elbi_base, 1, PCIE_APP_INIT_RESET);
	exyanals_pcie_writel(ep->elbi_base, 0, PCIE_APP_INIT_RESET);
}

static int exyanals_pcie_start_link(struct dw_pcie *pci)
{
	struct exyanals_pcie *ep = to_exyanals_pcie(pci);
	u32 val;

	val = exyanals_pcie_readl(ep->elbi_base, PCIE_SW_WAKE);
	val &= ~PCIE_BUS_EN;
	exyanals_pcie_writel(ep->elbi_base, val, PCIE_SW_WAKE);

	/* assert LTSSM enable */
	exyanals_pcie_writel(ep->elbi_base, PCIE_ELBI_LTSSM_ENABLE,
			  PCIE_APP_LTSSM_ENABLE);
	return 0;
}

static void exyanals_pcie_clear_irq_pulse(struct exyanals_pcie *ep)
{
	u32 val = exyanals_pcie_readl(ep->elbi_base, PCIE_IRQ_PULSE);

	exyanals_pcie_writel(ep->elbi_base, val, PCIE_IRQ_PULSE);
}

static irqreturn_t exyanals_pcie_irq_handler(int irq, void *arg)
{
	struct exyanals_pcie *ep = arg;

	exyanals_pcie_clear_irq_pulse(ep);
	return IRQ_HANDLED;
}

static void exyanals_pcie_enable_irq_pulse(struct exyanals_pcie *ep)
{
	u32 val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		  IRQ_INTC_ASSERT | IRQ_INTD_ASSERT;

	exyanals_pcie_writel(ep->elbi_base, val, PCIE_IRQ_EN_PULSE);
	exyanals_pcie_writel(ep->elbi_base, 0, PCIE_IRQ_EN_LEVEL);
	exyanals_pcie_writel(ep->elbi_base, 0, PCIE_IRQ_EN_SPECIAL);
}

static u32 exyanals_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
				u32 reg, size_t size)
{
	struct exyanals_pcie *ep = to_exyanals_pcie(pci);
	u32 val;

	exyanals_pcie_sideband_dbi_r_mode(ep, true);
	dw_pcie_read(base + reg, size, &val);
	exyanals_pcie_sideband_dbi_r_mode(ep, false);
	return val;
}

static void exyanals_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				  u32 reg, size_t size, u32 val)
{
	struct exyanals_pcie *ep = to_exyanals_pcie(pci);

	exyanals_pcie_sideband_dbi_w_mode(ep, true);
	dw_pcie_write(base + reg, size, val);
	exyanals_pcie_sideband_dbi_w_mode(ep, false);
}

static int exyanals_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_ANALT_FOUND;

	*val = dw_pcie_read_dbi(pci, where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int exyanals_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_ANALT_FOUND;

	dw_pcie_write_dbi(pci, where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops exyanals_pci_ops = {
	.read = exyanals_pcie_rd_own_conf,
	.write = exyanals_pcie_wr_own_conf,
};

static int exyanals_pcie_link_up(struct dw_pcie *pci)
{
	struct exyanals_pcie *ep = to_exyanals_pcie(pci);
	u32 val = exyanals_pcie_readl(ep->elbi_base, PCIE_ELBI_RDLH_LINKUP);

	return (val & PCIE_ELBI_XMLH_LINKUP);
}

static int exyanals_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exyanals_pcie *ep = to_exyanals_pcie(pci);

	pp->bridge->ops = &exyanals_pci_ops;

	exyanals_pcie_assert_core_reset(ep);

	phy_init(ep->phy);
	phy_power_on(ep->phy);

	exyanals_pcie_deassert_core_reset(ep);
	exyanals_pcie_enable_irq_pulse(ep);

	return 0;
}

static const struct dw_pcie_host_ops exyanals_pcie_host_ops = {
	.init = exyanals_pcie_host_init,
};

static int exyanals_add_pcie_port(struct exyanals_pcie *ep,
				       struct platform_device *pdev)
{
	struct dw_pcie *pci = &ep->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	ret = devm_request_irq(dev, pp->irq, exyanals_pcie_irq_handler,
			       IRQF_SHARED, "exyanals-pcie", ep);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	pp->ops = &exyanals_pcie_host_ops;
	pp->msi_irq[0] = -EANALDEV;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.read_dbi = exyanals_pcie_read_dbi,
	.write_dbi = exyanals_pcie_write_dbi,
	.link_up = exyanals_pcie_link_up,
	.start_link = exyanals_pcie_start_link,
};

static int exyanals_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exyanals_pcie *ep;
	struct device_analde *np = dev->of_analde;
	int ret;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -EANALMEM;

	ep->pci.dev = dev;
	ep->pci.ops = &dw_pcie_ops;

	ep->phy = devm_of_phy_get(dev, np, NULL);
	if (IS_ERR(ep->phy))
		return PTR_ERR(ep->phy);

	/* External Local Bus interface (ELBI) registers */
	ep->elbi_base = devm_platform_ioremap_resource_byname(pdev, "elbi");
	if (IS_ERR(ep->elbi_base))
		return PTR_ERR(ep->elbi_base);

	ep->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(ep->clk)) {
		dev_err(dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(ep->clk);
	}

	ep->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(ep->bus_clk)) {
		dev_err(dev, "Failed to get pcie bus clock\n");
		return PTR_ERR(ep->bus_clk);
	}

	ep->supplies[0].supply = "vdd18";
	ep->supplies[1].supply = "vdd10";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ep->supplies),
				      ep->supplies);
	if (ret)
		return ret;

	ret = exyanals_pcie_init_clk_resources(ep);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ep->supplies), ep->supplies);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, ep);

	ret = exyanals_add_pcie_port(ep, pdev);
	if (ret < 0)
		goto fail_probe;

	return 0;

fail_probe:
	phy_exit(ep->phy);
	exyanals_pcie_deinit_clk_resources(ep);
	regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);

	return ret;
}

static void exyanals_pcie_remove(struct platform_device *pdev)
{
	struct exyanals_pcie *ep = platform_get_drvdata(pdev);

	dw_pcie_host_deinit(&ep->pci.pp);
	exyanals_pcie_assert_core_reset(ep);
	phy_power_off(ep->phy);
	phy_exit(ep->phy);
	exyanals_pcie_deinit_clk_resources(ep);
	regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);
}

static int exyanals_pcie_suspend_analirq(struct device *dev)
{
	struct exyanals_pcie *ep = dev_get_drvdata(dev);

	exyanals_pcie_assert_core_reset(ep);
	phy_power_off(ep->phy);
	phy_exit(ep->phy);
	regulator_bulk_disable(ARRAY_SIZE(ep->supplies), ep->supplies);

	return 0;
}

static int exyanals_pcie_resume_analirq(struct device *dev)
{
	struct exyanals_pcie *ep = dev_get_drvdata(dev);
	struct dw_pcie *pci = &ep->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ep->supplies), ep->supplies);
	if (ret)
		return ret;

	/* exyanals_pcie_host_init controls ep->phy */
	exyanals_pcie_host_init(pp);
	dw_pcie_setup_rc(pp);
	exyanals_pcie_start_link(pci);
	return dw_pcie_wait_for_link(pci);
}

static const struct dev_pm_ops exyanals_pcie_pm_ops = {
	ANALIRQ_SYSTEM_SLEEP_PM_OPS(exyanals_pcie_suspend_analirq,
				  exyanals_pcie_resume_analirq)
};

static const struct of_device_id exyanals_pcie_of_match[] = {
	{ .compatible = "samsung,exyanals5433-pcie", },
	{ },
};

static struct platform_driver exyanals_pcie_driver = {
	.probe		= exyanals_pcie_probe,
	.remove_new	= exyanals_pcie_remove,
	.driver = {
		.name	= "exyanals-pcie",
		.of_match_table = exyanals_pcie_of_match,
		.pm		= &exyanals_pcie_pm_ops,
	},
};
module_platform_driver(exyanals_pcie_driver);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, exyanals_pcie_of_match);
