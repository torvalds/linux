// SPDX-License-Identifier: GPL-2.0-only
/*
 * PHY driver for NXP LPC18xx/43xx internal USB OTG PHY
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* USB OTG PHY register offset and bit in CREG */
#define LPC18XX_CREG_CREG0		0x004
#define LPC18XX_CREG_CREG0_USB0PHY	BIT(5)

struct lpc18xx_usb_otg_phy {
	struct phy *phy;
	struct clk *clk;
	struct regmap *reg;
};

static int lpc18xx_usb_otg_phy_init(struct phy *phy)
{
	struct lpc18xx_usb_otg_phy *lpc = phy_get_drvdata(phy);
	int ret;

	/* The PHY must be clocked at 480 MHz */
	ret = clk_set_rate(lpc->clk, 480000000);
	if (ret)
		return ret;

	return clk_prepare(lpc->clk);
}

static int lpc18xx_usb_otg_phy_exit(struct phy *phy)
{
	struct lpc18xx_usb_otg_phy *lpc = phy_get_drvdata(phy);

	clk_unprepare(lpc->clk);

	return 0;
}

static int lpc18xx_usb_otg_phy_power_on(struct phy *phy)
{
	struct lpc18xx_usb_otg_phy *lpc = phy_get_drvdata(phy);
	int ret;

	ret = clk_enable(lpc->clk);
	if (ret)
		return ret;

	/* The bit in CREG is cleared to enable the PHY */
	ret = regmap_update_bits(lpc->reg, LPC18XX_CREG_CREG0,
				  LPC18XX_CREG_CREG0_USB0PHY, 0);
	if (ret) {
		clk_disable(lpc->clk);
		return ret;
	}

	return 0;
}

static int lpc18xx_usb_otg_phy_power_off(struct phy *phy)
{
	struct lpc18xx_usb_otg_phy *lpc = phy_get_drvdata(phy);
	int ret;

	ret = regmap_update_bits(lpc->reg, LPC18XX_CREG_CREG0,
				 LPC18XX_CREG_CREG0_USB0PHY,
				 LPC18XX_CREG_CREG0_USB0PHY);
	if (ret)
		return ret;

	clk_disable(lpc->clk);

	return 0;
}

static const struct phy_ops lpc18xx_usb_otg_phy_ops = {
	.init		= lpc18xx_usb_otg_phy_init,
	.exit		= lpc18xx_usb_otg_phy_exit,
	.power_on	= lpc18xx_usb_otg_phy_power_on,
	.power_off	= lpc18xx_usb_otg_phy_power_off,
	.owner		= THIS_MODULE,
};

static int lpc18xx_usb_otg_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct lpc18xx_usb_otg_phy *lpc;

	lpc = devm_kzalloc(&pdev->dev, sizeof(*lpc), GFP_KERNEL);
	if (!lpc)
		return -ENOMEM;

	lpc->reg = syscon_node_to_regmap(pdev->dev.of_node->parent);
	if (IS_ERR(lpc->reg)) {
		dev_err(&pdev->dev, "failed to get syscon\n");
		return PTR_ERR(lpc->reg);
	}

	lpc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lpc->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(lpc->clk);
	}

	lpc->phy = devm_phy_create(&pdev->dev, NULL, &lpc18xx_usb_otg_phy_ops);
	if (IS_ERR(lpc->phy)) {
		dev_err(&pdev->dev, "failed to create PHY\n");
		return PTR_ERR(lpc->phy);
	}

	phy_set_drvdata(lpc->phy, lpc);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id lpc18xx_usb_otg_phy_match[] = {
	{ .compatible = "nxp,lpc1850-usb-otg-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpc18xx_usb_otg_phy_match);

static struct platform_driver lpc18xx_usb_otg_phy_driver = {
	.probe		= lpc18xx_usb_otg_phy_probe,
	.driver		= {
		.name	= "lpc18xx-usb-otg-phy",
		.of_match_table = lpc18xx_usb_otg_phy_match,
	},
};
module_platform_driver(lpc18xx_usb_otg_phy_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("NXP LPC18xx/43xx USB OTG PHY driver");
MODULE_LICENSE("GPL v2");
