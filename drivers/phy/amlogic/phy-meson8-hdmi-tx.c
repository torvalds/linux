// SPDX-License-Identifier: GPL-2.0+
/*
 * Meson8, Meson8b and Meson8m2 HDMI TX PHY.
 *
 * Copyright (C) 2021 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

/*
 * Unfortunately there is no detailed documentation available for the
 * HHI_HDMI_PHY_CNTL0 register. CTL0 and CTL1 is all we know about.
 * Magic register values in the driver below are taken from the vendor
 * BSP / kernel.
 */
#define HHI_HDMI_PHY_CNTL0				0x3a0
	#define HHI_HDMI_PHY_CNTL0_HDMI_CTL1		GENMASK(31, 16)
	#define HHI_HDMI_PHY_CNTL0_HDMI_CTL0		GENMASK(15, 0)

#define HHI_HDMI_PHY_CNTL1				0x3a4
	#define HHI_HDMI_PHY_CNTL1_CLOCK_ENABLE		BIT(1)
	#define HHI_HDMI_PHY_CNTL1_SOFT_RESET		BIT(0)

#define HHI_HDMI_PHY_CNTL2				0x3a8

struct phy_meson8_hdmi_tx_priv {
	struct regmap		*hhi;
	struct clk		*tmds_clk;
};

static int phy_meson8_hdmi_tx_init(struct phy *phy)
{
	struct phy_meson8_hdmi_tx_priv *priv = phy_get_drvdata(phy);

	return clk_prepare_enable(priv->tmds_clk);
}

static int phy_meson8_hdmi_tx_exit(struct phy *phy)
{
	struct phy_meson8_hdmi_tx_priv *priv = phy_get_drvdata(phy);

	clk_disable_unprepare(priv->tmds_clk);

	return 0;
}

static int phy_meson8_hdmi_tx_power_on(struct phy *phy)
{
	struct phy_meson8_hdmi_tx_priv *priv = phy_get_drvdata(phy);
	unsigned int i;
	u16 hdmi_ctl0;

	if (clk_get_rate(priv->tmds_clk) >= 2970UL * 1000 * 1000)
		hdmi_ctl0 = 0x1e8b;
	else
		hdmi_ctl0 = 0x4d0b;

	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0,
		     FIELD_PREP(HHI_HDMI_PHY_CNTL0_HDMI_CTL1, 0x08c3) |
		     FIELD_PREP(HHI_HDMI_PHY_CNTL0_HDMI_CTL0, hdmi_ctl0));

	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL1, 0x0);

	/* Reset three times, just like the vendor driver does */
	for (i = 0; i < 3; i++) {
		regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL1,
			     HHI_HDMI_PHY_CNTL1_CLOCK_ENABLE |
			     HHI_HDMI_PHY_CNTL1_SOFT_RESET);
		usleep_range(1000, 2000);

		regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL1,
			     HHI_HDMI_PHY_CNTL1_CLOCK_ENABLE);
		usleep_range(1000, 2000);
	}

	return 0;
}

static int phy_meson8_hdmi_tx_power_off(struct phy *phy)
{
	struct phy_meson8_hdmi_tx_priv *priv = phy_get_drvdata(phy);

	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0,
		     FIELD_PREP(HHI_HDMI_PHY_CNTL0_HDMI_CTL1, 0x0841) |
		     FIELD_PREP(HHI_HDMI_PHY_CNTL0_HDMI_CTL0, 0x8d00));

	return 0;
}

static const struct phy_ops phy_meson8_hdmi_tx_ops = {
	.init		= phy_meson8_hdmi_tx_init,
	.exit		= phy_meson8_hdmi_tx_exit,
	.power_on	= phy_meson8_hdmi_tx_power_on,
	.power_off	= phy_meson8_hdmi_tx_power_off,
	.owner		= THIS_MODULE,
};

static int phy_meson8_hdmi_tx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct phy_meson8_hdmi_tx_priv *priv;
	struct phy_provider *phy_provider;
	struct resource *res;
	struct phy *phy;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hhi = syscon_node_to_regmap(np->parent);
	if (IS_ERR(priv->hhi))
		return PTR_ERR(priv->hhi);

	priv->tmds_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->tmds_clk))
		return PTR_ERR(priv->tmds_clk);

	phy = devm_phy_create(&pdev->dev, np, &phy_meson8_hdmi_tx_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id phy_meson8_hdmi_tx_of_match[] = {
	{ .compatible = "amlogic,meson8-hdmi-tx-phy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, phy_meson8_hdmi_tx_of_match);

static struct platform_driver phy_meson8_hdmi_tx_driver = {
	.probe	= phy_meson8_hdmi_tx_probe,
	.driver	= {
		.name		= "phy-meson8-hdmi-tx",
		.of_match_table	= phy_meson8_hdmi_tx_of_match,
	},
};
module_platform_driver(phy_meson8_hdmi_tx_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Meson8, Meson8b and Meson8m2 HDMI TX PHY driver");
MODULE_LICENSE("GPL v2");
