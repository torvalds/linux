/*
 * PCIe host controller driver for Samsung EXYNOS SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#define to_exynos_pcie(x)	dev_get_drvdata((x)->dev)

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

/* PCIe Purple registers */
#define PCIE_PHY_GLOBAL_RESET		0x000
#define PCIE_PHY_COMMON_RESET		0x004
#define PCIE_PHY_CMN_REG		0x008
#define PCIE_PHY_MAC_RESET		0x00c
#define PCIE_PHY_PLL_LOCKED		0x010
#define PCIE_PHY_TRSVREG_RESET		0x020
#define PCIE_PHY_TRSV_RESET		0x024

/* PCIe PHY registers */
#define PCIE_PHY_IMPEDANCE		0x004
#define PCIE_PHY_PLL_DIV_0		0x008
#define PCIE_PHY_PLL_BIAS		0x00c
#define PCIE_PHY_DCC_FEEDBACK		0x014
#define PCIE_PHY_PLL_DIV_1		0x05c
#define PCIE_PHY_COMMON_POWER		0x064
#define PCIE_PHY_COMMON_PD_CMN		BIT(3)
#define PCIE_PHY_TRSV0_EMP_LVL		0x084
#define PCIE_PHY_TRSV0_DRV_LVL		0x088
#define PCIE_PHY_TRSV0_RXCDR		0x0ac
#define PCIE_PHY_TRSV0_POWER		0x0c4
#define PCIE_PHY_TRSV0_PD_TSV		BIT(7)
#define PCIE_PHY_TRSV0_LVCC		0x0dc
#define PCIE_PHY_TRSV1_EMP_LVL		0x144
#define PCIE_PHY_TRSV1_RXCDR		0x16c
#define PCIE_PHY_TRSV1_POWER		0x184
#define PCIE_PHY_TRSV1_PD_TSV		BIT(7)
#define PCIE_PHY_TRSV1_LVCC		0x19c
#define PCIE_PHY_TRSV2_EMP_LVL		0x204
#define PCIE_PHY_TRSV2_RXCDR		0x22c
#define PCIE_PHY_TRSV2_POWER		0x244
#define PCIE_PHY_TRSV2_PD_TSV		BIT(7)
#define PCIE_PHY_TRSV2_LVCC		0x25c
#define PCIE_PHY_TRSV3_EMP_LVL		0x2c4
#define PCIE_PHY_TRSV3_RXCDR		0x2ec
#define PCIE_PHY_TRSV3_POWER		0x304
#define PCIE_PHY_TRSV3_PD_TSV		BIT(7)
#define PCIE_PHY_TRSV3_LVCC		0x31c

struct exynos_pcie_mem_res {
	void __iomem *elbi_base;   /* DT 0th resource: PCIe CTRL */
	void __iomem *phy_base;    /* DT 1st resource: PHY CTRL */
	void __iomem *block_base;  /* DT 2nd resource: PHY ADDITIONAL CTRL */
};

struct exynos_pcie_clk_res {
	struct clk *clk;
	struct clk *bus_clk;
};

struct exynos_pcie {
	struct dw_pcie			*pci;
	struct exynos_pcie_mem_res	*mem_res;
	struct exynos_pcie_clk_res	*clk_res;
	const struct exynos_pcie_ops	*ops;
	int				reset_gpio;

	/* For Generic PHY Framework */
	bool				using_phy;
	struct phy			*phy;
};

struct exynos_pcie_ops {
	int (*get_mem_resources)(struct platform_device *pdev,
			struct exynos_pcie *ep);
	int (*get_clk_resources)(struct exynos_pcie *ep);
	int (*init_clk_resources)(struct exynos_pcie *ep);
	void (*deinit_clk_resources)(struct exynos_pcie *ep);
};

static int exynos5440_pcie_get_mem_resources(struct platform_device *pdev,
					     struct exynos_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;
	struct resource *res;

	/* If using the PHY framework, doesn't need to get other resource */
	if (ep->using_phy)
		return 0;

	ep->mem_res = devm_kzalloc(dev, sizeof(*ep->mem_res), GFP_KERNEL);
	if (!ep->mem_res)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ep->mem_res->elbi_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ep->mem_res->elbi_base))
		return PTR_ERR(ep->mem_res->elbi_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ep->mem_res->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ep->mem_res->phy_base))
		return PTR_ERR(ep->mem_res->phy_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	ep->mem_res->block_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ep->mem_res->block_base))
		return PTR_ERR(ep->mem_res->block_base);

	return 0;
}

static int exynos5440_pcie_get_clk_resources(struct exynos_pcie *ep)
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

static int exynos5440_pcie_init_clk_resources(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = clk_prepare_enable(ep->clk_res->clk);
	if (ret) {
		dev_err(dev, "cannot enable pcie rc clock");
		return ret;
	}

	ret = clk_prepare_enable(ep->clk_res->bus_clk);
	if (ret) {
		dev_err(dev, "cannot enable pcie bus clock");
		goto err_bus_clk;
	}

	return 0;

err_bus_clk:
	clk_disable_unprepare(ep->clk_res->clk);

	return ret;
}

static void exynos5440_pcie_deinit_clk_resources(struct exynos_pcie *ep)
{
	clk_disable_unprepare(ep->clk_res->bus_clk);
	clk_disable_unprepare(ep->clk_res->clk);
}

static const struct exynos_pcie_ops exynos5440_pcie_ops = {
	.get_mem_resources	= exynos5440_pcie_get_mem_resources,
	.get_clk_resources	= exynos5440_pcie_get_clk_resources,
	.init_clk_resources	= exynos5440_pcie_init_clk_resources,
	.deinit_clk_resources	= exynos5440_pcie_deinit_clk_resources,
};

static void exynos_pcie_writel(void __iomem *base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static u32 exynos_pcie_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static void exynos_pcie_sideband_dbi_w_mode(struct exynos_pcie *ep, bool on)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_SLV_AWMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_ELBI_SLV_AWMISC);
}

static void exynos_pcie_sideband_dbi_r_mode(struct exynos_pcie *ep, bool on)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_SLV_ARMISC);
	if (on)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_ELBI_SLV_ARMISC);
}

static void exynos_pcie_assert_core_reset(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_CORE_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_PWR_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_STICKY_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_NONSTICKY_RESET);
}

static void exynos_pcie_deassert_core_reset(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;

	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_CORE_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_STICKY_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_NONSTICKY_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_APP_INIT_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 0, PCIE_APP_INIT_RESET);
	exynos_pcie_writel(ep->mem_res->block_base, 1, PCIE_PHY_MAC_RESET);
}

static void exynos_pcie_assert_phy_reset(struct exynos_pcie *ep)
{
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_MAC_RESET);
	exynos_pcie_writel(ep->mem_res->block_base, 1, PCIE_PHY_GLOBAL_RESET);
}

static void exynos_pcie_deassert_phy_reset(struct exynos_pcie *ep)
{
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_GLOBAL_RESET);
	exynos_pcie_writel(ep->mem_res->elbi_base, 1, PCIE_PWR_RESET);
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_COMMON_RESET);
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_CMN_REG);
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_TRSVREG_RESET);
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_TRSV_RESET);
}

static void exynos_pcie_power_on_phy(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_COMMON_POWER);
	val &= ~PCIE_PHY_COMMON_PD_CMN;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV0_POWER);
	val &= ~PCIE_PHY_TRSV0_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV1_POWER);
	val &= ~PCIE_PHY_TRSV1_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV2_POWER);
	val &= ~PCIE_PHY_TRSV2_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV3_POWER);
	val &= ~PCIE_PHY_TRSV3_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV3_POWER);
}

static void exynos_pcie_power_off_phy(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_COMMON_POWER);
	val |= PCIE_PHY_COMMON_PD_CMN;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV0_POWER);
	val |= PCIE_PHY_TRSV0_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV1_POWER);
	val |= PCIE_PHY_TRSV1_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV2_POWER);
	val |= PCIE_PHY_TRSV2_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_pcie_readl(ep->mem_res->phy_base, PCIE_PHY_TRSV3_POWER);
	val |= PCIE_PHY_TRSV3_PD_TSV;
	exynos_pcie_writel(ep->mem_res->phy_base, val, PCIE_PHY_TRSV3_POWER);
}

static void exynos_pcie_init_phy(struct exynos_pcie *ep)
{
	/* DCC feedback control off */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x29, PCIE_PHY_DCC_FEEDBACK);

	/* set TX/RX impedance */
	exynos_pcie_writel(ep->mem_res->phy_base, 0xd5, PCIE_PHY_IMPEDANCE);

	/* set 50Mhz PHY clock */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x14, PCIE_PHY_PLL_DIV_0);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x12, PCIE_PHY_PLL_DIV_1);

	/* set TX Differential output for lane 0 */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x7f, PCIE_PHY_TRSV0_DRV_LVL);

	/* set TX Pre-emphasis Level Control for lane 0 to minimum */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x0, PCIE_PHY_TRSV0_EMP_LVL);

	/* set RX clock and data recovery bandwidth */
	exynos_pcie_writel(ep->mem_res->phy_base, 0xe7, PCIE_PHY_PLL_BIAS);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x82, PCIE_PHY_TRSV0_RXCDR);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x82, PCIE_PHY_TRSV1_RXCDR);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x82, PCIE_PHY_TRSV2_RXCDR);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x82, PCIE_PHY_TRSV3_RXCDR);

	/* change TX Pre-emphasis Level Control for lanes */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x39, PCIE_PHY_TRSV0_EMP_LVL);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x39, PCIE_PHY_TRSV1_EMP_LVL);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x39, PCIE_PHY_TRSV2_EMP_LVL);
	exynos_pcie_writel(ep->mem_res->phy_base, 0x39, PCIE_PHY_TRSV3_EMP_LVL);

	/* set LVCC */
	exynos_pcie_writel(ep->mem_res->phy_base, 0x20, PCIE_PHY_TRSV0_LVCC);
	exynos_pcie_writel(ep->mem_res->phy_base, 0xa0, PCIE_PHY_TRSV1_LVCC);
	exynos_pcie_writel(ep->mem_res->phy_base, 0xa0, PCIE_PHY_TRSV2_LVCC);
	exynos_pcie_writel(ep->mem_res->phy_base, 0xa0, PCIE_PHY_TRSV3_LVCC);
}

static void exynos_pcie_assert_reset(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct device *dev = pci->dev;

	if (ep->reset_gpio >= 0)
		devm_gpio_request_one(dev, ep->reset_gpio,
				GPIOF_OUT_INIT_HIGH, "RESET");
}

static int exynos_pcie_establish_link(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	u32 val;

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "Link already up\n");
		return 0;
	}

	exynos_pcie_assert_core_reset(ep);

	if (ep->using_phy) {
		phy_reset(ep->phy);

		exynos_pcie_writel(ep->mem_res->elbi_base, 1,
				PCIE_PWR_RESET);

		phy_power_on(ep->phy);
		phy_init(ep->phy);
	} else {
		exynos_pcie_assert_phy_reset(ep);
		exynos_pcie_deassert_phy_reset(ep);
		exynos_pcie_power_on_phy(ep);
		exynos_pcie_init_phy(ep);

		/* pulse for common reset */
		exynos_pcie_writel(ep->mem_res->block_base, 1,
					PCIE_PHY_COMMON_RESET);
		udelay(500);
		exynos_pcie_writel(ep->mem_res->block_base, 0,
					PCIE_PHY_COMMON_RESET);
	}

	/* pulse for common reset */
	exynos_pcie_writel(ep->mem_res->block_base, 1, PCIE_PHY_COMMON_RESET);
	udelay(500);
	exynos_pcie_writel(ep->mem_res->block_base, 0, PCIE_PHY_COMMON_RESET);

	exynos_pcie_deassert_core_reset(ep);
	dw_pcie_setup_rc(pp);
	exynos_pcie_assert_reset(ep);

	/* assert LTSSM enable */
	exynos_pcie_writel(ep->mem_res->elbi_base, PCIE_ELBI_LTSSM_ENABLE,
			  PCIE_APP_LTSSM_ENABLE);

	/* check if the link is up or not */
	if (!dw_pcie_wait_for_link(pci))
		return 0;

	if (ep->using_phy) {
		phy_power_off(ep->phy);
		return -ETIMEDOUT;
	}

	while (exynos_pcie_readl(ep->mem_res->phy_base,
				PCIE_PHY_PLL_LOCKED) == 0) {
		val = exynos_pcie_readl(ep->mem_res->block_base,
				PCIE_PHY_PLL_LOCKED);
		dev_info(dev, "PLL Locked: 0x%x\n", val);
	}
	exynos_pcie_power_off_phy(ep);
	return -ETIMEDOUT;
}

static void exynos_pcie_clear_irq_pulse(struct exynos_pcie *ep)
{
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_IRQ_PULSE);
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_PULSE);
}

static void exynos_pcie_enable_irq_pulse(struct exynos_pcie *ep)
{
	u32 val;

	/* enable INTX interrupt */
	val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		IRQ_INTC_ASSERT | IRQ_INTD_ASSERT;
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_EN_PULSE);
}

static irqreturn_t exynos_pcie_irq_handler(int irq, void *arg)
{
	struct exynos_pcie *ep = arg;

	exynos_pcie_clear_irq_pulse(ep);
	return IRQ_HANDLED;
}

static irqreturn_t exynos_pcie_msi_irq_handler(int irq, void *arg)
{
	struct exynos_pcie *ep = arg;
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;

	return dw_handle_msi_irq(pp);
}

static void exynos_pcie_msi_init(struct exynos_pcie *ep)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	u32 val;

	dw_pcie_msi_init(pp);

	/* enable MSI interrupt */
	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_IRQ_EN_LEVEL);
	val |= IRQ_MSI_ENABLE;
	exynos_pcie_writel(ep->mem_res->elbi_base, val, PCIE_IRQ_EN_LEVEL);
}

static void exynos_pcie_enable_interrupts(struct exynos_pcie *ep)
{
	exynos_pcie_enable_irq_pulse(ep);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		exynos_pcie_msi_init(ep);
}

static u32 exynos_pcie_readl_dbi(struct dw_pcie *pci, u32 reg)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val;

	exynos_pcie_sideband_dbi_r_mode(ep, true);
	val = readl(pci->dbi_base + reg);
	exynos_pcie_sideband_dbi_r_mode(ep, false);
	return val;
}

static void exynos_pcie_writel_dbi(struct dw_pcie *pci, u32 reg, u32 val)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);

	exynos_pcie_sideband_dbi_w_mode(ep, true);
	writel(val, pci->dbi_base + reg);
	exynos_pcie_sideband_dbi_w_mode(ep, false);
}

static int exynos_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	int ret;

	exynos_pcie_sideband_dbi_r_mode(ep, true);
	ret = dw_pcie_read(pci->dbi_base + where, size, val);
	exynos_pcie_sideband_dbi_r_mode(ep, false);
	return ret;
}

static int exynos_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
				u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	int ret;

	exynos_pcie_sideband_dbi_w_mode(ep, true);
	ret = dw_pcie_write(pci->dbi_base + where, size, val);
	exynos_pcie_sideband_dbi_w_mode(ep, false);
	return ret;
}

static int exynos_pcie_link_up(struct dw_pcie *pci)
{
	struct exynos_pcie *ep = to_exynos_pcie(pci);
	u32 val;

	val = exynos_pcie_readl(ep->mem_res->elbi_base, PCIE_ELBI_RDLH_LINKUP);
	if (val == PCIE_ELBI_LTSSM_ENABLE)
		return 1;

	return 0;
}

static void exynos_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct exynos_pcie *ep = to_exynos_pcie(pci);

	exynos_pcie_establish_link(ep);
	exynos_pcie_enable_interrupts(ep);
}

static struct dw_pcie_host_ops exynos_pcie_host_ops = {
	.rd_own_conf = exynos_pcie_rd_own_conf,
	.wr_own_conf = exynos_pcie_wr_own_conf,
	.host_init = exynos_pcie_host_init,
};

static int __init exynos_add_pcie_port(struct exynos_pcie *ep,
				       struct platform_device *pdev)
{
	struct dw_pcie *pci = ep->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (!pp->irq) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}
	ret = devm_request_irq(dev, pp->irq, exynos_pcie_irq_handler,
				IRQF_SHARED, "exynos-pcie", ep);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq(pdev, 0);
		if (!pp->msi_irq) {
			dev_err(dev, "failed to get msi irq\n");
			return -ENODEV;
		}

		ret = devm_request_irq(dev, pp->msi_irq,
					exynos_pcie_msi_irq_handler,
					IRQF_SHARED | IRQF_NO_THREAD,
					"exynos-pcie", ep);
		if (ret) {
			dev_err(dev, "failed to request msi irq\n");
			return ret;
		}
	}

	pp->root_bus_nr = -1;
	pp->ops = &exynos_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.readl_dbi = exynos_pcie_readl_dbi,
	.writel_dbi = exynos_pcie_writel_dbi,
	.link_up = exynos_pcie_link_up,
};

static int __init exynos_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct exynos_pcie *ep;
	struct device_node *np = dev->of_node;
	int ret;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	ep->ops = (const struct exynos_pcie_ops *)
		of_device_get_match_data(dev);

	ep->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	/* Assume that controller doesn't use the PHY framework */
	ep->using_phy = false;

	ep->phy = devm_of_phy_get(dev, np, NULL);
	if (IS_ERR(ep->phy)) {
		if (PTR_ERR(ep->phy) == -EPROBE_DEFER)
			return PTR_ERR(ep->phy);
		dev_warn(dev, "Use the 'phy' property. Current DT of pci-exynos was deprecated!!\n");
	} else
		ep->using_phy = true;

	if (ep->ops && ep->ops->get_mem_resources) {
		ret = ep->ops->get_mem_resources(pdev, ep);
		if (ret)
			return ret;
	}

	if (ep->ops && ep->ops->get_clk_resources) {
		ret = ep->ops->get_clk_resources(ep);
		if (ret)
			return ret;
		ret = ep->ops->init_clk_resources(ep);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, ep);

	ret = exynos_add_pcie_port(ep, pdev);
	if (ret < 0)
		goto fail_probe;

	return 0;

fail_probe:
	if (ep->using_phy)
		phy_exit(ep->phy);

	if (ep->ops && ep->ops->deinit_clk_resources)
		ep->ops->deinit_clk_resources(ep);
	return ret;
}

static int __exit exynos_pcie_remove(struct platform_device *pdev)
{
	struct exynos_pcie *ep = platform_get_drvdata(pdev);

	if (ep->ops && ep->ops->deinit_clk_resources)
		ep->ops->deinit_clk_resources(ep);

	return 0;
}

static const struct of_device_id exynos_pcie_of_match[] = {
	{
		.compatible = "samsung,exynos5440-pcie",
		.data = &exynos5440_pcie_ops
	},
	{},
};

static struct platform_driver exynos_pcie_driver = {
	.remove		= __exit_p(exynos_pcie_remove),
	.driver = {
		.name	= "exynos-pcie",
		.of_match_table = exynos_pcie_of_match,
	},
};

/* Exynos PCIe driver does not allow module unload */

static int __init exynos_pcie_init(void)
{
	return platform_driver_probe(&exynos_pcie_driver, exynos_pcie_probe);
}
subsys_initcall(exynos_pcie_init);
