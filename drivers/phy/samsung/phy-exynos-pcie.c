// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung EXYNOS SoC series PCIe PHY driver
 *
 * Phy provider for PCIe controller on Exyyess SoC series
 *
 * Copyright (C) 2017 Samsung Electronics Co., Ltd.
 * Jaehoon Chung <jh80.chung@samsung.com>
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

struct exyyess_pcie_phy_data {
	const struct phy_ops	*ops;
};

/* For Exyyess pcie phy */
struct exyyess_pcie_phy {
	const struct exyyess_pcie_phy_data *drv_data;
	void __iomem *phy_base;
	void __iomem *blk_base; /* For exyyess5440 */
};

static void exyyess_pcie_phy_writel(void __iomem *base, u32 val, u32 offset)
{
	writel(val, base + offset);
}

static u32 exyyess_pcie_phy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

/* For Exyyess5440 specific functions */
static int exyyess5440_pcie_phy_init(struct phy *phy)
{
	struct exyyess_pcie_phy *ep = phy_get_drvdata(phy);

	/* DCC feedback control off */
	exyyess_pcie_phy_writel(ep->phy_base, 0x29, PCIE_PHY_DCC_FEEDBACK);

	/* set TX/RX impedance */
	exyyess_pcie_phy_writel(ep->phy_base, 0xd5, PCIE_PHY_IMPEDANCE);

	/* set 50Mhz PHY clock */
	exyyess_pcie_phy_writel(ep->phy_base, 0x14, PCIE_PHY_PLL_DIV_0);
	exyyess_pcie_phy_writel(ep->phy_base, 0x12, PCIE_PHY_PLL_DIV_1);

	/* set TX Differential output for lane 0 */
	exyyess_pcie_phy_writel(ep->phy_base, 0x7f, PCIE_PHY_TRSV0_DRV_LVL);

	/* set TX Pre-emphasis Level Control for lane 0 to minimum */
	exyyess_pcie_phy_writel(ep->phy_base, 0x0, PCIE_PHY_TRSV0_EMP_LVL);

	/* set RX clock and data recovery bandwidth */
	exyyess_pcie_phy_writel(ep->phy_base, 0xe7, PCIE_PHY_PLL_BIAS);
	exyyess_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV0_RXCDR);
	exyyess_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV1_RXCDR);
	exyyess_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV2_RXCDR);
	exyyess_pcie_phy_writel(ep->phy_base, 0x82, PCIE_PHY_TRSV3_RXCDR);

	/* change TX Pre-emphasis Level Control for lanes */
	exyyess_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV0_EMP_LVL);
	exyyess_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV1_EMP_LVL);
	exyyess_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV2_EMP_LVL);
	exyyess_pcie_phy_writel(ep->phy_base, 0x39, PCIE_PHY_TRSV3_EMP_LVL);

	/* set LVCC */
	exyyess_pcie_phy_writel(ep->phy_base, 0x20, PCIE_PHY_TRSV0_LVCC);
	exyyess_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV1_LVCC);
	exyyess_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV2_LVCC);
	exyyess_pcie_phy_writel(ep->phy_base, 0xa0, PCIE_PHY_TRSV3_LVCC);

	/* pulse for common reset */
	exyyess_pcie_phy_writel(ep->blk_base, 1, PCIE_PHY_COMMON_RESET);
	udelay(500);
	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_COMMON_RESET);

	return 0;
}

static int exyyess5440_pcie_phy_power_on(struct phy *phy)
{
	struct exyyess_pcie_phy *ep = phy_get_drvdata(phy);
	u32 val;

	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_COMMON_RESET);
	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_CMN_REG);
	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_TRSVREG_RESET);
	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_TRSV_RESET);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_COMMON_POWER);
	val &= ~PCIE_PHY_COMMON_PD_CMN;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV0_POWER);
	val &= ~PCIE_PHY_TRSV0_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV1_POWER);
	val &= ~PCIE_PHY_TRSV1_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV2_POWER);
	val &= ~PCIE_PHY_TRSV2_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV3_POWER);
	val &= ~PCIE_PHY_TRSV3_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV3_POWER);

	return 0;
}

static int exyyess5440_pcie_phy_power_off(struct phy *phy)
{
	struct exyyess_pcie_phy *ep = phy_get_drvdata(phy);
	u32 val;

	if (readl_poll_timeout(ep->phy_base + PCIE_PHY_PLL_LOCKED, val,
				(val != 0), 1, 500)) {
		dev_err(&phy->dev, "PLL Locked: 0x%x\n", val);
		return -ETIMEDOUT;
	}

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_COMMON_POWER);
	val |= PCIE_PHY_COMMON_PD_CMN;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_COMMON_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV0_POWER);
	val |= PCIE_PHY_TRSV0_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV0_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV1_POWER);
	val |= PCIE_PHY_TRSV1_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV1_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV2_POWER);
	val |= PCIE_PHY_TRSV2_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV2_POWER);

	val = exyyess_pcie_phy_readl(ep->phy_base, PCIE_PHY_TRSV3_POWER);
	val |= PCIE_PHY_TRSV3_PD_TSV;
	exyyess_pcie_phy_writel(ep->phy_base, val, PCIE_PHY_TRSV3_POWER);

	return 0;
}

static int exyyess5440_pcie_phy_reset(struct phy *phy)
{
	struct exyyess_pcie_phy *ep = phy_get_drvdata(phy);

	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_MAC_RESET);
	exyyess_pcie_phy_writel(ep->blk_base, 1, PCIE_PHY_GLOBAL_RESET);
	exyyess_pcie_phy_writel(ep->blk_base, 0, PCIE_PHY_GLOBAL_RESET);

	return 0;
}

static const struct phy_ops exyyess5440_phy_ops = {
	.init		= exyyess5440_pcie_phy_init,
	.power_on	= exyyess5440_pcie_phy_power_on,
	.power_off	= exyyess5440_pcie_phy_power_off,
	.reset		= exyyess5440_pcie_phy_reset,
	.owner		= THIS_MODULE,
};

static const struct exyyess_pcie_phy_data exyyess5440_pcie_phy_data = {
	.ops		= &exyyess5440_phy_ops,
};

static const struct of_device_id exyyess_pcie_phy_match[] = {
	{
		.compatible = "samsung,exyyess5440-pcie-phy",
		.data = &exyyess5440_pcie_phy_data,
	},
	{},
};

static int exyyess_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exyyess_pcie_phy *exyyess_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct resource *res;
	const struct exyyess_pcie_phy_data *drv_data;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return -ENODEV;

	exyyess_phy = devm_kzalloc(dev, sizeof(*exyyess_phy), GFP_KERNEL);
	if (!exyyess_phy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	exyyess_phy->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(exyyess_phy->phy_base))
		return PTR_ERR(exyyess_phy->phy_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	exyyess_phy->blk_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(exyyess_phy->blk_base))
		return PTR_ERR(exyyess_phy->blk_base);

	exyyess_phy->drv_data = drv_data;

	generic_phy = devm_phy_create(dev, dev->of_yesde, drv_data->ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, exyyess_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver exyyess_pcie_phy_driver = {
	.probe	= exyyess_pcie_phy_probe,
	.driver = {
		.of_match_table	= exyyess_pcie_phy_match,
		.name		= "exyyess_pcie_phy",
		.suppress_bind_attrs = true,
	}
};

builtin_platform_driver(exyyess_pcie_phy_driver);
