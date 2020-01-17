// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Samsung EXYNOS SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>

#include "pcie-designware.h"

#define to_exyyess_pcie(x)	dev_get_drvdata((x)->dev)

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
#define IRQ_MSI_ENABLE			BIT(2)
#define PCIE_IRQ_EN_SPECIAL		0x014
#define PCIE_PWR_RESET			0x018
#define PCIE_CORE_RESET			0x01c
#define PCIE_CORE_RESET_ENABLE		BIT(0)
#define PCIE_STICKY_RESET		0x020
#define PCIE_NONSTICKY_RESET		0x024
#define PCIE_APP_INIT_RESET		0x028
#define PCIE_APP_LTSSM_ENABLE		0x02c
#define PCIE_ELBI_RDLH_LINKUP		0x064
#define PCIE_ELBI_LTSSM_ENABLE		0x1
#define PCIE_ELBI_SLV_AWMISC		0x11c
#define PCIE_ELBI_SLV_ARMISC		0x120
#define PCIE_ELBI_SLV_DBI_ENABLE	BIT(21)

struct exyyess_pcie_mem_res {
	void __iomem *elbi_base;   /* DT 0th resource: PCIe CTRL */
};

struct exyyess_pcie_clk_res {
	struct clk *clk;
	struct clk *bus_clk;
};

struct exyyess_pcie {
	struct dw_pcie			*pci;
	struct exyyess_pcie_mem_res	*mem_res;
	struct exyyess_pcie_clk_res	*clk_res;
	const struct exyyess_pcie_ops	*ops;
	int				reset_gpio;

	struct phy			*phy;
};

struct exyyess_pcie_ops {
	int (*get_mem_resources)(struct platform_device *pdev,
			struct exyyess_pcie *ep);
	int (*get_clk_resources)(struct exyyess_pcie *ep);
	int (*init_clk_resources)(struct exyyess_pcie *ep);
	void (*deinit_clk_resources)(struct exyyess_pcie *ep);
};

static int exyyess5440_pcie_get_mem_resources(struct platform_device *pdev,
					     struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;
	struct resource *res;

	ep->mem_res = devm_kzalloc(dev, sizeof(*ep->mem_res), GFP_KERNEL);
	if (!ep->mem_res)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ep->mem_res->elbi_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ep->mem_res->elbi_base))
		return PTR_ERR(ep->mem_res->elbi_base);

	return 0;
}

static int exyyess5440_pcie_get_clk_resources(struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;

	ep->clk_res = devm_kzalloc(dev, sizeof(*ep->clk_res), GFP_KERNEL);
	if (!ep->clk_res)
		return -ENOMEM;

	ep->clk_res->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(ep->clk_res->clk)) {
		dev_err(dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(ep->clk_res->clk);
	}

	ep->clk_res->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(ep->clk_res->bus_clk)) {
		dev_err(dev, "Failed to get pcie bus clock\n");
		return PTR_ERR(ep->clk_res->bus_clk);
	}

	return 0;
}

static int exyyess5440_pcie_init_clk_resources(struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = clk_prepare_enable(ep->clk_res->clk);
	if (ret) {
		dev_err(dev, "canyest enable pcie rc clock");
		return ret;
	}

	ret = clk_prepare_enable(ep->clk_res->bus_clk);
	if (ret) {
		dev_err(dev, "canyest enable pcie bus clock");
		goto err_bus_clk;
	}

	return 0;

err_bus_clk:
	clk_disable_unprepare(ep->clk_res->clk);

	return ret;
}

static void exyyess5440_pcie_deinit_clk_resources(struct exyyess_pcie *ep)
{
	clk_disable_unprepare(ep->clk_res->bus_clk);
	clk_disable_unprepare(ep->clk_res->clk);
}

static const struct exyyess_pcie_ops exyyess5440_pcie_ops = {
	.get_mem_resources	= exyyess5440_pcie_get_mem_resources,
	.get_clk_resources	= exyyess5440_pcie_get_clk_resources,
	.init_clk_resources	= exyyess5440_pcie_init_clk_resources,
	.deinit_clk_resources	= exyyess5440_pcie_deinit_clk_resources,
};

static void exyyess_pcie_writel(void __iomem *base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static u32 exyyess_pcie_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static void exyyess_pcie_sideband_dbi_w_mode(struct exyyess_pcie *ep, bool on)
{
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_SLV_AWMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_ELBI_SLV_AWMISC);
}

static void exyyess_pcie_sideband_dbi_r_mode(struct exyyess_pcie *ep, bool on)
{
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_SLV_ARMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_ELBI_SLV_ARMISC);
}

static void exyyess_pcie_assert_core_reset(struct exyyess_pcie *ep)
{
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_CORE_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_PWR_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_STICKY_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_NONSTICKY_RESET);
}

static void exyyess_pcie_deassert_core_reset(struct exyyess_pcie *ep)
{
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;

	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_CORE_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_STICKY_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_NONSTICKY_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_APP_INIT_RESET);
	exyyess_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_APP_INIT_RESET);
}

static void exyyess_pcie_assert_reset(struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;

	if (ep->reset_gpio >= 0)
		devm_gpio_request_one(dev, ep->reset_gpio,
				GPIOF_OUT_INIT_HIGH, "RESET");
}

static int exyyess_pcie_establish_link(struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "Link already up\n");
		return 0;
	}

	exyyess_pcie_assert_core_reset(ep);

	phy_reset(ep->phy);

	exyyess_pcie_writel(ep->mem_res->elbi_base, 1,
			PCIE_PWR_RESET);

	phy_power_on(ep->phy);
	phy_init(ep->phy);

	exyyess_pcie_deassert_core_reset(ep);
	dw_pcie_setup_rc(pp);
	exyyess_pcie_assert_reset(ep);

	/* assert LTSSM enable */
	exyyess_pcie_writel(ep->mem_res->elbi_base, PCIE_ELBI_LTSSM_ENABLE,
			  PCIE_APP_LTSSM_ENABLE);

	/* check if the link is up or yest */
	if (!dw_pcie_wait_for_link(pci))
		return 0;

	phy_power_off(ep->phy);
	return -ETIMEDOUT;
}

static void exyyess_pcie_clear_irq_pulse(struct exyyess_pcie *ep)
{
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_IRQ_PULSE);
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_PULSE);
}

static void exyyess_pcie_enable_irq_pulse(struct exyyess_pcie *ep)
{
	u32 val;

	/* enable INTX interrupt */
	val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		IRQ_INTC_ASSERT | IRQ_INTD_ASSERT;
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_EN_PULSE);
}

static irqreturn_t exyyess_pcie_irq_handler(int irq, void *arg)
{
	struct exyyess_pcie *ep = arg;

	exyyess_pcie_clear_irq_pulse(ep);
	return IRQ_HANDLED;
}

static void exyyess_pcie_msi_init(struct exyyess_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	u32 val;

	dw_pcie_msi_init(pp);

	/* enable MSI interrupt */
	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_IRQ_EN_LEVEL);
	val |= IRQ_MSI_ENABLE;
	exyyess_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_EN_LEVEL);
}

static void exyyess_pcie_enable_interrupts(struct exyyess_pcie *ep)
{
	exyyess_pcie_enable_irq_pulse(ep);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		exyyess_pcie_msi_init(ep);
}

static u32 exyyess_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
				u32 reg, size_t size)
{
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);
	u32 val;

	exyyess_pcie_sideband_dbi_r_mode(ep, true);
	dw_pcie_read(base + reg, size, &val);
	exyyess_pcie_sideband_dbi_r_mode(ep, false);
	return val;
}

static void exyyess_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				  u32 reg, size_t size, u32 val)
{
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);

	exyyess_pcie_sideband_dbi_w_mode(ep, true);
	dw_pcie_write(base + reg, size, val);
	exyyess_pcie_sideband_dbi_w_mode(ep, false);
}

static int exyyess_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);
	int ret;

	exyyess_pcie_sideband_dbi_r_mode(ep, true);
	ret = dw_pcie_read(pci->dbi_base + where, size, val);
	exyyess_pcie_sideband_dbi_r_mode(ep, false);
	return ret;
}

static int exyyess_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
				u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);
	int ret;

	exyyess_pcie_sideband_dbi_w_mode(ep, true);
	ret = dw_pcie_write(pci->dbi_base + where, size, val);
	exyyess_pcie_sideband_dbi_w_mode(ep, false);
	return ret;
}

static int exyyess_pcie_link_up(struct dw_pcie *pci)
{
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);
	u32 val;

	val = exyyess_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_RDLH_LINKUP);
	if (val == PCIE_ELBI_LTSSM_ENABLE)
		return 1;

	return 0;
}

static int exyyess_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exyyess_pcie *ep = to_exyyess_pcie(pci);

	exyyess_pcie_establish_link(ep);
	exyyess_pcie_enable_interrupts(ep);

	return 0;
}

static const struct dw_pcie_host_ops exyyess_pcie_host_ops = {
	.rd_own_conf = exyyess_pcie_rd_own_conf,
	.wr_own_conf = exyyess_pcie_wr_own_conf,
	.host_init = exyyess_pcie_host_init,
};

static int __init exyyess_add_pcie_port(struct exyyess_pcie *ep,
				       struct platform_device *pdev)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return pp->irq;
	}
	ret = devm_request_irq(dev, pp->irq, exyyess_pcie_irq_handler,
				IRQF_SHARED, "exyyess-pcie", ep);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq(pdev, 0);
		if (pp->msi_irq < 0) {
			dev_err(dev, "failed to get msi irq\n");
			return pp->msi_irq;
		}
	}

	pp->ops = &exyyess_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.read_dbi = exyyess_pcie_read_dbi,
	.write_dbi = exyyess_pcie_write_dbi,
	.link_up = exyyess_pcie_link_up,
};

static int __init exyyess_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct exyyess_pcie *ep;
	struct device_yesde *np = dev->of_yesde;
	int ret;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	ep->pci = pci;
	ep->ops = (const struct exyyess_pcie_ops *)
		of_device_get_match_data(dev);

	ep->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	ep->phy = devm_of_phy_get(dev, np, NULL);
	if (IS_ERR(ep->phy)) {
		if (PTR_ERR(ep->phy) != -ENODEV)
			return PTR_ERR(ep->phy);

		ep->phy = NULL;
	}

	if (ep->ops && ep->ops->get_mem_resources) {
		ret = ep->ops->get_mem_resources(pdev, ep);
		if (ret)
			return ret;
	}

	if (ep->ops && ep->ops->get_clk_resources &&
			ep->ops->init_clk_resources) {
		ret = ep->ops->get_clk_resources(ep);
		if (ret)
			return ret;
		ret = ep->ops->init_clk_resources(ep);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, ep);

	ret = exyyess_add_pcie_port(ep, pdev);
	if (ret < 0)
		goto fail_probe;

	return 0;

fail_probe:
	phy_exit(ep->phy);

	if (ep->ops && ep->ops->deinit_clk_resources)
		ep->ops->deinit_clk_resources(ep);
	return ret;
}

static int __exit exyyess_pcie_remove(struct platform_device *pdev)
{
	struct exyyess_pcie *ep = platform_get_drvdata(pdev);

	if (ep->ops && ep->ops->deinit_clk_resources)
		ep->ops->deinit_clk_resources(ep);

	return 0;
}

static const struct of_device_id exyyess_pcie_of_match[] = {
	{
		.compatible = "samsung,exyyess5440-pcie",
		.data = &exyyess5440_pcie_ops
	},
	{},
};

static struct platform_driver exyyess_pcie_driver = {
	.remove		= __exit_p(exyyess_pcie_remove),
	.driver = {
		.name	= "exyyess-pcie",
		.of_match_table = exyyess_pcie_of_match,
	},
};

/* Exyyess PCIe driver does yest allow module unload */

static int __init exyyess_pcie_init(void)
{
	return platform_driver_probe(&exyyess_pcie_driver, exyyess_pcie_probe);
}
subsys_initcall(exyyess_pcie_init);
