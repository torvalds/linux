/*
 * Samsung EXYNOS SoC series PCIe PHY driver
 *
 * Phy provider for PCIe controller on Exynos SoC series
 *
 * Copyright (C) 2017 Samsung Electronics Co., Ltd.
 * Jaehoon Chung <jh80.chung@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

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

struct exynos_pcie_phy_data {
	const struct phy_ops	*ops;
};

/* For Exynos pcie phy */
struct exynos_pcie_phy {
	const struct exynos_pcie_phy_data *drv_data;
	void __iomem *phy_base;
	void __iomem *blk_base; /* For exynos5440 */
};

static void exynos_pcie_phy_writel(void __iomem *base, u32 val, u32 offset)
{
	writel(val, base + offset);
}

static u32 exynos_pcie_phy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

/* For Exynos5440 specific functions */
static int exynos5440_pcie_phy_init(struct phy *phy)
{
	struct exynos_pcie_phy *ep = phy_get_drvdata(phy);

	/* DCC feedback control off */
	exynos_pcie_phy_writel(ep->phy_base, 0x29, PCIE_PHY_DCC_FEEDBACK);

	/* set TX/RX impedance */
	exynos_pcie_phy_writel(ep->phy_base, 0xd5, PCIE_PHY_IMPEDANCE);

	/* set 50Mhz PHY clock */
	exynos_pcie_phy_writel(ep->phy_base, 0x14, PCIE_PHY_PLL_DIV_0);
	exynos_pcie_phy_writel(ep->phy_base, 0x12, PCIE_PHY_PLL_DIV_1);

	/* set TX Differential output for lane 0 */
	exynos_pcie_phy_writel(ep->phy_base, 0x7f, PCIE_PHY_TRSV0_DRV_LVL);

	/* set TX Pre-emphasis Level Control for lane 0 to minimum */
	exynos_pcie_phy_writel(ep->phy_base, 0x0, PCIE_PHY_TRSV0_EMP_LVL);

	/* set RX clock and data recovery bandwidth */
	exynos_pcie_phy_writel(ep->phy_base, 0xe7, PCIE_PHY_PLL_BIAS);
	exynos_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV0_RXCDR);
	exynos_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV1_RXCDR);
	exynos_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV2_RXCDR);
	exynos_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV3_RXCDR);

	/* change TX Pre-emphasis Level Control for lanes */
	exynos_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV0_EMP_LVL);
	exynos_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV1_EMP_LVL);
	exynos_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV2_EMP_LVL);
	exynos_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV3_EMP_LVL);

	/* set LVCC */
	exynos_pcie_phy_writel(ep->phy_base, 0x20, PCIE_PHY_TRSV0_LVCC);
	exynos_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV1_LVCC);
	exynos_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV2_LVCC);
	exynos_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV3_LVCC);

	/* pulse for common reset */
	exynos_pcie_phy_writel(ep->blk_base, 1, PCIE_PHY_COMMON_RESET);
	udelay(500);
	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_COMMON_RESET);

	return 0;
}

static int exynos5440_pcie_phy_power_on(struct phy *phy)
{
	struct exynos_pcie_phy *ep = phy_get_drvdata(phy);
	u32 val;

	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_COMMON_RESET);
	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_CMN_REG);
	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_TRSVREG_RESET);
	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_TRSV_RESET);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_COMMON_POWER);
	val &= ~PCIE_PHY_COMMON_PD_CMN;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV0_POWER);
	val &= ~PCIE_PHY_TRSV0_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV1_POWER);
	val &= ~PCIE_PHY_TRSV1_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV2_POWER);
	val &= ~PCIE_PHY_TRSV2_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV3_POWER);
	val &= ~PCIE_PHY_TRSV3_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV3_POWER);

	return 0;
}

static int exynos5440_pcie_phy_power_off(struct phy *phy)
{
	struct exynos_pcie_phy *ep = phy_get_drvdata(phy);
	u32 val;

	if (readl_poll_timeout(ep->phy_base + PCIE_PHY_PLL_LOCKED, val,
				(val != 0), 1, 500)) {
		dev_err(&phy->dev, "PLL Locked: 0x%x\n", val);
		return -ETIMEDOUT;
	}

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_COMMON_POWER);
	val |= PCIE_PHY_COMMON_PD_CMN;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV0_POWER);
	val |= PCIE_PHY_TRSV0_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV1_POWER);
	val |= PCIE_PHY_TRSV1_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV2_POWER);
	val |= PCIE_PHY_TRSV2_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exynos_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV3_POWER);
	val |= PCIE_PHY_TRSV3_PD_TSV;
	exynos_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV3_POWER);

	return 0;
}

static int exynos5440_pcie_phy_reset(struct phy *phy)
{
	struct exynos_pcie_phy *ep = phy_get_drvdata(phy);

	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_MAC_RESET);
	exynos_pcie_phy_writel(ep->blk_base, 1, PCIE_PHY_GLOBAL_RESET);
	exynos_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_GLOBAL_RESET);

	return 0;
}

static const struct phy_ops exynos5440_phy_ops = {
	.init		= exynos5440_pcie_phy_init,
	.power_on	= exynos5440_pcie_phy_power_on,
	.power_off	= exynos5440_pcie_phy_power_off,
	.reset		= exynos5440_pcie_phy_reset,
	.owner		= THIS_MODULE,
};

static const struct exynos_pcie_phy_data exynos5440_pcie_phy_data = {
	.ops		= &exynos5440_phy_ops,
};

static const struct of_device_id exynos_pcie_phy_match[] = {
	{
		.compatible = "samsung,exynos5440-pcie-phy",
		.data = &exynos5440_pcie_phy_data,
	},
	{},
};

static int exynos_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_pcie_phy *exynos_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct resource *res;
	const struct exynos_pcie_phy_data *drv_data;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return -ENODEV;

	exynos_phy = devm_kzalloc(dev, sizeof(*exynos_phy), GFP_KERNEL);
	if (!exynos_phy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	exynos_phy->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(exynos_phy->phy_base))
		return PTR_ERR(exynos_phy->phy_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	exynos_phy->blk_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(exynos_phy->blk_base))
		return PTR_ERR(exynos_phy->blk_base);

	exynos_phy->drv_data = drv_data;

	generic_phy = devm_phy_create(dev, dev->of_node, drv_data->ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, exynos_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver exynos_pcie_phy_driver = {
	.probe	= exynos_pcie_phy_probe,
	.driver = {
		.of_match_table	= exynos_pcie_phy_match,
		.name		= "exynos_pcie_phy",
	}
};

builtin_platform_driver(exynos_pcie_phy_driver);
