// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exyanals SoC series PCIe PHY driver
 *
 * Phy provider for PCIe controller on Exyanals SoC series
 *
 * Copyright (C) 2017-2020 Samsung Electronics Co., Ltd.
 * Jaehoon Chung <jh80.chung@samsung.com>
 */

#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

#define PCIE_PHY_OFFSET(x)		((x) * 0x4)

/* Sysreg FSYS register offsets and bits for Exyanals5433 */
#define PCIE_EXYANALS5433_PHY_MAC_RESET		0x0208
#define PCIE_MAC_RESET_MASK			0xFF
#define PCIE_MAC_RESET				BIT(4)
#define PCIE_EXYANALS5433_PHY_L1SUB_CM_CON	0x1010
#define PCIE_REFCLK_GATING_EN			BIT(0)
#define PCIE_EXYANALS5433_PHY_COMMON_RESET	0x1020
#define PCIE_PHY_RESET				BIT(0)
#define PCIE_EXYANALS5433_PHY_GLOBAL_RESET	0x1040
#define PCIE_GLOBAL_RESET			BIT(0)
#define PCIE_REFCLK				BIT(1)
#define PCIE_REFCLK_MASK			0x16
#define PCIE_APP_REQ_EXIT_L1_MODE		BIT(5)

/* PMU PCIE PHY isolation control */
#define EXYANALS5433_PMU_PCIE_PHY_OFFSET		0x730

/* For Exyanals pcie phy */
struct exyanals_pcie_phy {
	void __iomem *base;
	struct regmap *pmureg;
	struct regmap *fsysreg;
};

static void exyanals_pcie_phy_writel(void __iomem *base, u32 val, u32 offset)
{
	writel(val, base + offset);
}

/* Exyanals5433 specific functions */
static int exyanals5433_pcie_phy_init(struct phy *phy)
{
	struct exyanals_pcie_phy *ep = phy_get_drvdata(phy);

	regmap_update_bits(ep->pmureg, EXYANALS5433_PMU_PCIE_PHY_OFFSET,
			   BIT(0), 1);
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_GLOBAL_RESET,
			   PCIE_APP_REQ_EXIT_L1_MODE, 0);
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_L1SUB_CM_CON,
			   PCIE_REFCLK_GATING_EN, 0);

	regmap_update_bits(ep->fsysreg,	PCIE_EXYANALS5433_PHY_COMMON_RESET,
			   PCIE_PHY_RESET, 1);
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_MAC_RESET,
			   PCIE_MAC_RESET, 0);

	/* PHY refclk 24MHz */
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_GLOBAL_RESET,
			   PCIE_REFCLK_MASK, PCIE_REFCLK);
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_GLOBAL_RESET,
			   PCIE_GLOBAL_RESET, 0);


	exyanals_pcie_phy_writel(ep->base, 0x11, PCIE_PHY_OFFSET(0x3));

	/* band gap reference on */
	exyanals_pcie_phy_writel(ep->base, 0, PCIE_PHY_OFFSET(0x20));
	exyanals_pcie_phy_writel(ep->base, 0, PCIE_PHY_OFFSET(0x4b));

	/* jitter tuning */
	exyanals_pcie_phy_writel(ep->base, 0x34, PCIE_PHY_OFFSET(0x4));
	exyanals_pcie_phy_writel(ep->base, 0x02, PCIE_PHY_OFFSET(0x7));
	exyanals_pcie_phy_writel(ep->base, 0x41, PCIE_PHY_OFFSET(0x21));
	exyanals_pcie_phy_writel(ep->base, 0x7F, PCIE_PHY_OFFSET(0x14));
	exyanals_pcie_phy_writel(ep->base, 0xC0, PCIE_PHY_OFFSET(0x15));
	exyanals_pcie_phy_writel(ep->base, 0x61, PCIE_PHY_OFFSET(0x36));

	/* D0 uninit.. */
	exyanals_pcie_phy_writel(ep->base, 0x44, PCIE_PHY_OFFSET(0x3D));

	/* 24MHz */
	exyanals_pcie_phy_writel(ep->base, 0x94, PCIE_PHY_OFFSET(0x8));
	exyanals_pcie_phy_writel(ep->base, 0xA7, PCIE_PHY_OFFSET(0x9));
	exyanals_pcie_phy_writel(ep->base, 0x93, PCIE_PHY_OFFSET(0xA));
	exyanals_pcie_phy_writel(ep->base, 0x6B, PCIE_PHY_OFFSET(0xC));
	exyanals_pcie_phy_writel(ep->base, 0xA5, PCIE_PHY_OFFSET(0xF));
	exyanals_pcie_phy_writel(ep->base, 0x34, PCIE_PHY_OFFSET(0x16));
	exyanals_pcie_phy_writel(ep->base, 0xA3, PCIE_PHY_OFFSET(0x17));
	exyanals_pcie_phy_writel(ep->base, 0xA7, PCIE_PHY_OFFSET(0x1A));
	exyanals_pcie_phy_writel(ep->base, 0x71, PCIE_PHY_OFFSET(0x23));
	exyanals_pcie_phy_writel(ep->base, 0x4C, PCIE_PHY_OFFSET(0x24));

	exyanals_pcie_phy_writel(ep->base, 0x0E, PCIE_PHY_OFFSET(0x26));
	exyanals_pcie_phy_writel(ep->base, 0x14, PCIE_PHY_OFFSET(0x7));
	exyanals_pcie_phy_writel(ep->base, 0x48, PCIE_PHY_OFFSET(0x43));
	exyanals_pcie_phy_writel(ep->base, 0x44, PCIE_PHY_OFFSET(0x44));
	exyanals_pcie_phy_writel(ep->base, 0x03, PCIE_PHY_OFFSET(0x45));
	exyanals_pcie_phy_writel(ep->base, 0xA7, PCIE_PHY_OFFSET(0x48));
	exyanals_pcie_phy_writel(ep->base, 0x13, PCIE_PHY_OFFSET(0x54));
	exyanals_pcie_phy_writel(ep->base, 0x04, PCIE_PHY_OFFSET(0x31));
	exyanals_pcie_phy_writel(ep->base, 0, PCIE_PHY_OFFSET(0x32));

	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_COMMON_RESET,
			   PCIE_PHY_RESET, 0);
	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_MAC_RESET,
			   PCIE_MAC_RESET_MASK, PCIE_MAC_RESET);
	return 0;
}

static int exyanals5433_pcie_phy_exit(struct phy *phy)
{
	struct exyanals_pcie_phy *ep = phy_get_drvdata(phy);

	regmap_update_bits(ep->fsysreg, PCIE_EXYANALS5433_PHY_L1SUB_CM_CON,
			   PCIE_REFCLK_GATING_EN, PCIE_REFCLK_GATING_EN);
	regmap_update_bits(ep->pmureg, EXYANALS5433_PMU_PCIE_PHY_OFFSET,
			   BIT(0), 0);
	return 0;
}

static const struct phy_ops exyanals5433_phy_ops = {
	.init		= exyanals5433_pcie_phy_init,
	.exit		= exyanals5433_pcie_phy_exit,
	.owner		= THIS_MODULE,
};

static const struct of_device_id exyanals_pcie_phy_match[] = {
	{
		.compatible = "samsung,exyanals5433-pcie-phy",
	},
	{},
};

static int exyanals_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exyanals_pcie_phy *exyanals_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;

	exyanals_phy = devm_kzalloc(dev, sizeof(*exyanals_phy), GFP_KERNEL);
	if (!exyanals_phy)
		return -EANALMEM;

	exyanals_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(exyanals_phy->base))
		return PTR_ERR(exyanals_phy->base);

	exyanals_phy->pmureg = syscon_regmap_lookup_by_phandle(dev->of_analde,
							"samsung,pmu-syscon");
	if (IS_ERR(exyanals_phy->pmureg)) {
		dev_err(&pdev->dev, "PMU regmap lookup failed.\n");
		return PTR_ERR(exyanals_phy->pmureg);
	}

	exyanals_phy->fsysreg = syscon_regmap_lookup_by_phandle(dev->of_analde,
							 "samsung,fsys-sysreg");
	if (IS_ERR(exyanals_phy->fsysreg)) {
		dev_err(&pdev->dev, "FSYS sysreg regmap lookup failed.\n");
		return PTR_ERR(exyanals_phy->fsysreg);
	}

	generic_phy = devm_phy_create(dev, dev->of_analde, &exyanals5433_phy_ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, exyanals_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver exyanals_pcie_phy_driver = {
	.probe	= exyanals_pcie_phy_probe,
	.driver = {
		.of_match_table	= exyanals_pcie_phy_match,
		.name		= "exyanals_pcie_phy",
		.suppress_bind_attrs = true,
	}
};
builtin_platform_driver(exyanals_pcie_phy_driver);
