// SPDX-License-Identifier: GPL-2.0+
/*
 * P2U (PIPE to UPHY) driver for Tegra T194 SoC
 *
 * Copyright (C) 2019 NVIDIA Corporation.
 *
 * Author: Vidya Sagar <vidyas@nvidia.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>

#define P2U_PERIODIC_EQ_CTRL_GEN3	0xc0
#define P2U_PERIODIC_EQ_CTRL_GEN3_PERIODIC_EQ_EN		BIT(0)
#define P2U_PERIODIC_EQ_CTRL_GEN3_INIT_PRESET_EQ_TRAIN_EN	BIT(1)
#define P2U_PERIODIC_EQ_CTRL_GEN4	0xc4
#define P2U_PERIODIC_EQ_CTRL_GEN4_INIT_PRESET_EQ_TRAIN_EN	BIT(1)

#define P2U_RX_DEBOUNCE_TIME				0xa4
#define P2U_RX_DEBOUNCE_TIME_DEBOUNCE_TIMER_MASK	0xffff
#define P2U_RX_DEBOUNCE_TIME_DEBOUNCE_TIMER_VAL		160

struct tegra_p2u {
	void __iomem *base;
};

static inline void p2u_writel(struct tegra_p2u *phy, const u32 value,
			      const u32 reg)
{
	writel_relaxed(value, phy->base + reg);
}

static inline u32 p2u_readl(struct tegra_p2u *phy, const u32 reg)
{
	return readl_relaxed(phy->base + reg);
}

static int tegra_p2u_power_on(struct phy *x)
{
	struct tegra_p2u *phy = phy_get_drvdata(x);
	u32 val;

	val = p2u_readl(phy, P2U_PERIODIC_EQ_CTRL_GEN3);
	val &= ~P2U_PERIODIC_EQ_CTRL_GEN3_PERIODIC_EQ_EN;
	val |= P2U_PERIODIC_EQ_CTRL_GEN3_INIT_PRESET_EQ_TRAIN_EN;
	p2u_writel(phy, val, P2U_PERIODIC_EQ_CTRL_GEN3);

	val = p2u_readl(phy, P2U_PERIODIC_EQ_CTRL_GEN4);
	val |= P2U_PERIODIC_EQ_CTRL_GEN4_INIT_PRESET_EQ_TRAIN_EN;
	p2u_writel(phy, val, P2U_PERIODIC_EQ_CTRL_GEN4);

	val = p2u_readl(phy, P2U_RX_DEBOUNCE_TIME);
	val &= ~P2U_RX_DEBOUNCE_TIME_DEBOUNCE_TIMER_MASK;
	val |= P2U_RX_DEBOUNCE_TIME_DEBOUNCE_TIMER_VAL;
	p2u_writel(phy, val, P2U_RX_DEBOUNCE_TIME);

	return 0;
}

static const struct phy_ops ops = {
	.power_on = tegra_p2u_power_on,
	.owner = THIS_MODULE,
};

static int tegra_p2u_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	struct tegra_p2u *phy;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->base = devm_platform_ioremap_resource_byname(pdev, "ctl");
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	platform_set_drvdata(pdev, phy);

	generic_phy = devm_phy_create(dev, NULL, &ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static const struct of_device_id tegra_p2u_id_table[] = {
	{
		.compatible = "nvidia,tegra194-p2u",
	},
	{}
};
MODULE_DEVICE_TABLE(of, tegra_p2u_id_table);

static struct platform_driver tegra_p2u_driver = {
	.probe = tegra_p2u_probe,
	.driver = {
		.name = "tegra194-p2u",
		.of_match_table = tegra_p2u_id_table,
	},
};
module_platform_driver(tegra_p2u_driver);

MODULE_AUTHOR("Vidya Sagar <vidyas@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra194 PIPE2UPHY PHY driver");
MODULE_LICENSE("GPL v2");
