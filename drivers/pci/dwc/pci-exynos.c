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
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>

#include "pcie-designware.h"

#define to_exynos_pcie(x)	container_of(x, struct exynos_pcie, pp)

struct exynos_pcie {
	struct pcie_port	pp;
	void __iomem		*elbi_base;	/* DT 0th resource */
	void __iomem		*phy_base;	/* DT 1st resource */
	void __iomem		*block_base;	/* DT 2nd resource */
	int			reset_gpio;
	struct clk		*clk;
	struct clk		*bus_clk;
};

/* PCIe ELBI registers */
#define PCIE_IRQ_PULSE			0x000
#define IRQ_INTA_ASSERT			(0x1 << 0)
#define IRQ_INTB_ASSERT			(0x1 << 2)
#define IRQ_INTC_ASSERT			(0x1 << 4)
#define IRQ_INTD_ASSERT			(0x1 << 6)
#define PCIE_IRQ_LEVEL			0x004
#define PCIE_IRQ_SPECIAL		0x008
#define PCIE_IRQ_EN_PULSE		0x00c
#define PCIE_IRQ_EN_LEVEL		0x010
#define IRQ_MSI_ENABLE			(0x1 << 2)
#define PCIE_IRQ_EN_SPECIAL		0x014
#define PCIE_PWR_RESET			0x018
#define PCIE_CORE_RESET			0x01c
#define PCIE_CORE_RESET_ENABLE		(0x1 << 0)
#define PCIE_STICKY_RESET		0x020
#define PCIE_NONSTICKY_RESET		0x024
#define PCIE_APP_INIT_RESET		0x028
#define PCIE_APP_LTSSM_ENABLE		0x02c
#define PCIE_ELBI_RDLH_LINKUP		0x064
#define PCIE_ELBI_LTSSM_ENABLE		0x1
#define PCIE_ELBI_SLV_AWMISC		0x11c
#define PCIE_ELBI_SLV_ARMISC		0x120
#define PCIE_ELBI_SLV_DBI_ENABLE	(0x1 << 21)

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
#define PCIE_PHY_COMMON_PD_CMN		(0x1 << 3)
#define PCIE_PHY_TRSV0_EMP_LVL		0x084
#define PCIE_PHY_TRSV0_DRV_LVL		0x088
#define PCIE_PHY_TRSV0_RXCDR		0x0ac
#define PCIE_PHY_TRSV0_POWER		0x0c4
#define PCIE_PHY_TRSV0_PD_TSV		(0x1 << 7)
#define PCIE_PHY_TRSV0_LVCC		0x0dc
#define PCIE_PHY_TRSV1_EMP_LVL		0x144
#define PCIE_PHY_TRSV1_RXCDR		0x16c
#define PCIE_PHY_TRSV1_POWER		0x184
#define PCIE_PHY_TRSV1_PD_TSV		(0x1 << 7)
#define PCIE_PHY_TRSV1_LVCC		0x19c
#define PCIE_PHY_TRSV2_EMP_LVL		0x204
#define PCIE_PHY_TRSV2_RXCDR		0x22c
#define PCIE_PHY_TRSV2_POWER		0x244
#define PCIE_PHY_TRSV2_PD_TSV		(0x1 << 7)
#define PCIE_PHY_TRSV2_LVCC		0x25c
#define PCIE_PHY_TRSV3_EMP_LVL		0x2c4
#define PCIE_PHY_TRSV3_RXCDR		0x2ec
#define PCIE_PHY_TRSV3_POWER		0x304
#define PCIE_PHY_TRSV3_PD_TSV		(0x1 << 7)
#define PCIE_PHY_TRSV3_LVCC		0x31c

static void exynos_elb_writel(struct exynos_pcie *exynos_pcie, u32 val, u32 reg)
{
	writel(val, exynos_pcie->elbi_base + reg);
}

static u32 exynos_elb_readl(struct exynos_pcie *exynos_pcie, u32 reg)
{
	return readl(exynos_pcie->elbi_base + reg);
}

static void exynos_phy_writel(struct exynos_pcie *exynos_pcie, u32 val, u32 reg)
{
	writel(val, exynos_pcie->phy_base + reg);
}

static u32 exynos_phy_readl(struct exynos_pcie *exynos_pcie, u32 reg)
{
	return readl(exynos_pcie->phy_base + reg);
}

static void exynos_blk_writel(struct exynos_pcie *exynos_pcie, u32 val, u32 reg)
{
	writel(val, exynos_pcie->block_base + reg);
}

static u32 exynos_blk_readl(struct exynos_pcie *exynos_pcie, u32 reg)
{
	return readl(exynos_pcie->block_base + reg);
}

static void exynos_pcie_sideband_dbi_w_mode(struct exynos_pcie *exynos_pcie,
					    bool on)
{
	u32 val;

	if (on) {
		val = exynos_elb_readl(exynos_pcie, PCIE_ELBI_SLV_AWMISC);
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
		exynos_elb_writel(exynos_pcie, val, PCIE_ELBI_SLV_AWMISC);
	} else {
		val = exynos_elb_readl(exynos_pcie, PCIE_ELBI_SLV_AWMISC);
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
		exynos_elb_writel(exynos_pcie, val, PCIE_ELBI_SLV_AWMISC);
	}
}

static void exynos_pcie_sideband_dbi_r_mode(struct exynos_pcie *exynos_pcie,
					    bool on)
{
	u32 val;

	if (on) {
		val = exynos_elb_readl(exynos_pcie, PCIE_ELBI_SLV_ARMISC);
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
		exynos_elb_writel(exynos_pcie, val, PCIE_ELBI_SLV_ARMISC);
	} else {
		val = exynos_elb_readl(exynos_pcie, PCIE_ELBI_SLV_ARMISC);
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
		exynos_elb_writel(exynos_pcie, val, PCIE_ELBI_SLV_ARMISC);
	}
}

static void exynos_pcie_assert_core_reset(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	val = exynos_elb_readl(exynos_pcie, PCIE_CORE_RESET);
	val &= ~PCIE_CORE_RESET_ENABLE;
	exynos_elb_writel(exynos_pcie, val, PCIE_CORE_RESET);
	exynos_elb_writel(exynos_pcie, 0, PCIE_PWR_RESET);
	exynos_elb_writel(exynos_pcie, 0, PCIE_STICKY_RESET);
	exynos_elb_writel(exynos_pcie, 0, PCIE_NONSTICKY_RESET);
}

static void exynos_pcie_deassert_core_reset(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	val = exynos_elb_readl(exynos_pcie, PCIE_CORE_RESET);
	val |= PCIE_CORE_RESET_ENABLE;

	exynos_elb_writel(exynos_pcie, val, PCIE_CORE_RESET);
	exynos_elb_writel(exynos_pcie, 1, PCIE_STICKY_RESET);
	exynos_elb_writel(exynos_pcie, 1, PCIE_NONSTICKY_RESET);
	exynos_elb_writel(exynos_pcie, 1, PCIE_APP_INIT_RESET);
	exynos_elb_writel(exynos_pcie, 0, PCIE_APP_INIT_RESET);
	exynos_blk_writel(exynos_pcie, 1, PCIE_PHY_MAC_RESET);
}

static void exynos_pcie_assert_phy_reset(struct exynos_pcie *exynos_pcie)
{
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_MAC_RESET);
	exynos_blk_writel(exynos_pcie, 1, PCIE_PHY_GLOBAL_RESET);
}

static void exynos_pcie_deassert_phy_reset(struct exynos_pcie *exynos_pcie)
{
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_GLOBAL_RESET);
	exynos_elb_writel(exynos_pcie, 1, PCIE_PWR_RESET);
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_COMMON_RESET);
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_CMN_REG);
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_TRSVREG_RESET);
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_TRSV_RESET);
}

static void exynos_pcie_power_on_phy(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_COMMON_POWER);
	val &= ~PCIE_PHY_COMMON_PD_CMN;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_COMMON_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV0_POWER);
	val &= ~PCIE_PHY_TRSV0_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV1_POWER);
	val &= ~PCIE_PHY_TRSV1_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV2_POWER);
	val &= ~PCIE_PHY_TRSV2_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV3_POWER);
	val &= ~PCIE_PHY_TRSV3_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV3_POWER);
}

static void exynos_pcie_power_off_phy(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_COMMON_POWER);
	val |= PCIE_PHY_COMMON_PD_CMN;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_COMMON_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV0_POWER);
	val |= PCIE_PHY_TRSV0_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV1_POWER);
	val |= PCIE_PHY_TRSV1_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV2_POWER);
	val |= PCIE_PHY_TRSV2_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_phy_readl(exynos_pcie, PCIE_PHY_TRSV3_POWER);
	val |= PCIE_PHY_TRSV3_PD_TSV;
	exynos_phy_writel(exynos_pcie, val, PCIE_PHY_TRSV3_POWER);
}

static void exynos_pcie_init_phy(struct exynos_pcie *exynos_pcie)
{
	/* DCC feedback control off */
	exynos_phy_writel(exynos_pcie, 0x29, PCIE_PHY_DCC_FEEDBACK);

	/* set TX/RX impedance */
	exynos_phy_writel(exynos_pcie, 0xd5, PCIE_PHY_IMPEDANCE);

	/* set 50Mhz PHY clock */
	exynos_phy_writel(exynos_pcie, 0x14, PCIE_PHY_PLL_DIV_0);
	exynos_phy_writel(exynos_pcie, 0x12, PCIE_PHY_PLL_DIV_1);

	/* set TX Differential output for lane 0 */
	exynos_phy_writel(exynos_pcie, 0x7f, PCIE_PHY_TRSV0_DRV_LVL);

	/* set TX Pre-emphasis Level Control for lane 0 to minimum */
	exynos_phy_writel(exynos_pcie, 0x0, PCIE_PHY_TRSV0_EMP_LVL);

	/* set RX clock and data recovery bandwidth */
	exynos_phy_writel(exynos_pcie, 0xe7, PCIE_PHY_PLL_BIAS);
	exynos_phy_writel(exynos_pcie, 0x82, PCIE_PHY_TRSV0_RXCDR);
	exynos_phy_writel(exynos_pcie, 0x82, PCIE_PHY_TRSV1_RXCDR);
	exynos_phy_writel(exynos_pcie, 0x82, PCIE_PHY_TRSV2_RXCDR);
	exynos_phy_writel(exynos_pcie, 0x82, PCIE_PHY_TRSV3_RXCDR);

	/* change TX Pre-emphasis Level Control for lanes */
	exynos_phy_writel(exynos_pcie, 0x39, PCIE_PHY_TRSV0_EMP_LVL);
	exynos_phy_writel(exynos_pcie, 0x39, PCIE_PHY_TRSV1_EMP_LVL);
	exynos_phy_writel(exynos_pcie, 0x39, PCIE_PHY_TRSV2_EMP_LVL);
	exynos_phy_writel(exynos_pcie, 0x39, PCIE_PHY_TRSV3_EMP_LVL);

	/* set LVCC */
	exynos_phy_writel(exynos_pcie, 0x20, PCIE_PHY_TRSV0_LVCC);
	exynos_phy_writel(exynos_pcie, 0xa0, PCIE_PHY_TRSV1_LVCC);
	exynos_phy_writel(exynos_pcie, 0xa0, PCIE_PHY_TRSV2_LVCC);
	exynos_phy_writel(exynos_pcie, 0xa0, PCIE_PHY_TRSV3_LVCC);
}

static void exynos_pcie_assert_reset(struct exynos_pcie *exynos_pcie)
{
	struct pcie_port *pp = &exynos_pcie->pp;
	struct device *dev = pp->dev;

	if (exynos_pcie->reset_gpio >= 0)
		devm_gpio_request_one(dev, exynos_pcie->reset_gpio,
				GPIOF_OUT_INIT_HIGH, "RESET");
}

static int exynos_pcie_establish_link(struct exynos_pcie *exynos_pcie)
{
	struct pcie_port *pp = &exynos_pcie->pp;
	struct device *dev = pp->dev;
	u32 val;

	if (dw_pcie_link_up(pp)) {
		dev_err(dev, "Link already up\n");
		return 0;
	}

	exynos_pcie_assert_core_reset(exynos_pcie);
	exynos_pcie_assert_phy_reset(exynos_pcie);
	exynos_pcie_deassert_phy_reset(exynos_pcie);
	exynos_pcie_power_on_phy(exynos_pcie);
	exynos_pcie_init_phy(exynos_pcie);

	/* pulse for common reset */
	exynos_blk_writel(exynos_pcie, 1, PCIE_PHY_COMMON_RESET);
	udelay(500);
	exynos_blk_writel(exynos_pcie, 0, PCIE_PHY_COMMON_RESET);

	exynos_pcie_deassert_core_reset(exynos_pcie);
	dw_pcie_setup_rc(pp);
	exynos_pcie_assert_reset(exynos_pcie);

	/* assert LTSSM enable */
	exynos_elb_writel(exynos_pcie, PCIE_ELBI_LTSSM_ENABLE,
			  PCIE_APP_LTSSM_ENABLE);

	/* check if the link is up or not */
	if (!dw_pcie_wait_for_link(pp))
		return 0;

	while (exynos_phy_readl(exynos_pcie, PCIE_PHY_PLL_LOCKED) == 0) {
		val = exynos_blk_readl(exynos_pcie, PCIE_PHY_PLL_LOCKED);
		dev_info(dev, "PLL Locked: 0x%x\n", val);
	}
	exynos_pcie_power_off_phy(exynos_pcie);
	return -ETIMEDOUT;
}

static void exynos_pcie_clear_irq_pulse(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	val = exynos_elb_readl(exynos_pcie, PCIE_IRQ_PULSE);
	exynos_elb_writel(exynos_pcie, val, PCIE_IRQ_PULSE);
}

static void exynos_pcie_enable_irq_pulse(struct exynos_pcie *exynos_pcie)
{
	u32 val;

	/* enable INTX interrupt */
	val = IRQ_INTA_ASSERT | IRQ_INTB_ASSERT |
		IRQ_INTC_ASSERT | IRQ_INTD_ASSERT;
	exynos_elb_writel(exynos_pcie, val, PCIE_IRQ_EN_PULSE);
}

static irqreturn_t exynos_pcie_irq_handler(int irq, void *arg)
{
	struct exynos_pcie *exynos_pcie = arg;

	exynos_pcie_clear_irq_pulse(exynos_pcie);
	return IRQ_HANDLED;
}

static irqreturn_t exynos_pcie_msi_irq_handler(int irq, void *arg)
{
	struct exynos_pcie *exynos_pcie = arg;
	struct pcie_port *pp = &exynos_pcie->pp;

	return dw_handle_msi_irq(pp);
}

static void exynos_pcie_msi_init(struct exynos_pcie *exynos_pcie)
{
	struct pcie_port *pp = &exynos_pcie->pp;
	u32 val;

	dw_pcie_msi_init(pp);

	/* enable MSI interrupt */
	val = exynos_elb_readl(exynos_pcie, PCIE_IRQ_EN_LEVEL);
	val |= IRQ_MSI_ENABLE;
	exynos_elb_writel(exynos_pcie, val, PCIE_IRQ_EN_LEVEL);
}

static void exynos_pcie_enable_interrupts(struct exynos_pcie *exynos_pcie)
{
	exynos_pcie_enable_irq_pulse(exynos_pcie);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		exynos_pcie_msi_init(exynos_pcie);
}

static u32 exynos_pcie_readl_rc(struct pcie_port *pp, u32 reg)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);
	u32 val;

	exynos_pcie_sideband_dbi_r_mode(exynos_pcie, true);
	val = readl(pp->dbi_base + reg);
	exynos_pcie_sideband_dbi_r_mode(exynos_pcie, false);
	return val;
}

static void exynos_pcie_writel_rc(struct pcie_port *pp, u32 reg, u32 val)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);

	exynos_pcie_sideband_dbi_w_mode(exynos_pcie, true);
	writel(val, pp->dbi_base + reg);
	exynos_pcie_sideband_dbi_w_mode(exynos_pcie, false);
}

static int exynos_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				u32 *val)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);
	int ret;

	exynos_pcie_sideband_dbi_r_mode(exynos_pcie, true);
	ret = dw_pcie_read(pp->dbi_base + where, size, val);
	exynos_pcie_sideband_dbi_r_mode(exynos_pcie, false);
	return ret;
}

static int exynos_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
				u32 val)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);
	int ret;

	exynos_pcie_sideband_dbi_w_mode(exynos_pcie, true);
	ret = dw_pcie_write(pp->dbi_base + where, size, val);
	exynos_pcie_sideband_dbi_w_mode(exynos_pcie, false);
	return ret;
}

static int exynos_pcie_link_up(struct pcie_port *pp)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);
	u32 val;

	val = exynos_elb_readl(exynos_pcie, PCIE_ELBI_RDLH_LINKUP);
	if (val == PCIE_ELBI_LTSSM_ENABLE)
		return 1;

	return 0;
}

static void exynos_pcie_host_init(struct pcie_port *pp)
{
	struct exynos_pcie *exynos_pcie = to_exynos_pcie(pp);

	exynos_pcie_establish_link(exynos_pcie);
	exynos_pcie_enable_interrupts(exynos_pcie);
}

static struct pcie_host_ops exynos_pcie_host_ops = {
	.readl_rc = exynos_pcie_readl_rc,
	.writel_rc = exynos_pcie_writel_rc,
	.rd_own_conf = exynos_pcie_rd_own_conf,
	.wr_own_conf = exynos_pcie_wr_own_conf,
	.link_up = exynos_pcie_link_up,
	.host_init = exynos_pcie_host_init,
};

static int __init exynos_add_pcie_port(struct exynos_pcie *exynos_pcie,
				       struct platform_device *pdev)
{
	struct pcie_port *pp = &exynos_pcie->pp;
	struct device *dev = pp->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (!pp->irq) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}
	ret = devm_request_irq(dev, pp->irq, exynos_pcie_irq_handler,
				IRQF_SHARED, "exynos-pcie", exynos_pcie);
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
					"exynos-pcie", exynos_pcie);
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

static int __init exynos_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_pcie *exynos_pcie;
	struct pcie_port *pp;
	struct device_node *np = dev->of_node;
	struct resource *elbi_base;
	struct resource *phy_base;
	struct resource *block_base;
	int ret;

	exynos_pcie = devm_kzalloc(dev, sizeof(*exynos_pcie), GFP_KERNEL);
	if (!exynos_pcie)
		return -ENOMEM;

	pp = &exynos_pcie->pp;
	pp->dev = dev;

	exynos_pcie->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	exynos_pcie->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(exynos_pcie->clk)) {
		dev_err(dev, "Failed to get pcie rc clock\n");
		return PTR_ERR(exynos_pcie->clk);
	}
	ret = clk_prepare_enable(exynos_pcie->clk);
	if (ret)
		return ret;

	exynos_pcie->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(exynos_pcie->bus_clk)) {
		dev_err(dev, "Failed to get pcie bus clock\n");
		ret = PTR_ERR(exynos_pcie->bus_clk);
		goto fail_clk;
	}
	ret = clk_prepare_enable(exynos_pcie->bus_clk);
	if (ret)
		goto fail_clk;

	elbi_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	exynos_pcie->elbi_base = devm_ioremap_resource(dev, elbi_base);
	if (IS_ERR(exynos_pcie->elbi_base)) {
		ret = PTR_ERR(exynos_pcie->elbi_base);
		goto fail_bus_clk;
	}

	phy_base = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	exynos_pcie->phy_base = devm_ioremap_resource(dev, phy_base);
	if (IS_ERR(exynos_pcie->phy_base)) {
		ret = PTR_ERR(exynos_pcie->phy_base);
		goto fail_bus_clk;
	}

	block_base = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	exynos_pcie->block_base = devm_ioremap_resource(dev, block_base);
	if (IS_ERR(exynos_pcie->block_base)) {
		ret = PTR_ERR(exynos_pcie->block_base);
		goto fail_bus_clk;
	}

	platform_set_drvdata(pdev, exynos_pcie);

	ret = exynos_add_pcie_port(exynos_pcie, pdev);
	if (ret < 0)
		goto fail_bus_clk;

	return 0;

fail_bus_clk:
	clk_disable_unprepare(exynos_pcie->bus_clk);
fail_clk:
	clk_disable_unprepare(exynos_pcie->clk);
	return ret;
}

static int __exit exynos_pcie_remove(struct platform_device *pdev)
{
	struct exynos_pcie *exynos_pcie = platform_get_drvdata(pdev);

	clk_disable_unprepare(exynos_pcie->bus_clk);
	clk_disable_unprepare(exynos_pcie->clk);

	return 0;
}

static const struct of_device_id exynos_pcie_of_match[] = {
	{ .compatible = "samsung,exynos5440-pcie", },
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
