/*
 * Allwinner sun8i H3 E(thernet) PHY driver
 *
 * Copyright (C) 2016 Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define REG_PHY_ADDR_SHIFT	20
#define REG_PHY_ADDR_MASK	GENMASK(4, 0)
#define REG_LED_POL		BIT(17)	/* 1: active low, 0: active high */
#define REG_SHUTDOWN		BIT(16)	/* 1: shutdown, 0: power up */
#define REG_PHY_SELECT		BIT(15) /* 1: internal PHY, 0: external PHY */

#define REG_DEFAULT_VALUE	0x58000
#define REG_DEFAULT_MASK	GENMASK(31, 15)

#define REG_GPIT		2
#define REG_ETCS_MASK		0x3
#define SUN8I_H3_EMAC_PARENTS	2
#define MII_PHY_CLK_RATE	25000000
#define INT_TX_CLK_RATE		125000000
#define MII_PHY_CLK_NAME	"mii_phy_tx"
#define INT_TX_CLK_NAME		"int_tx"

struct sun8i_h3_ephy {
	struct device *dev;
	void __iomem *reg;
	struct clk *clk;
	struct reset_control *reset;
	struct clk *mac_clks[SUN8I_H3_EMAC_PARENTS + 1];
};

static DEFINE_SPINLOCK(sun8i_h3_ephy_lock);

/* The EMAC clock/interface controls are much like those found on the A20,
 * which is supported by sun7i-a20-gmac-clk. The H3 adds an RMII module,
 * enabled by bit 13. Unfortunately vendor code and documentation don't
 * explain  how it should work with the other bits, and no hardware
 * actually uses it.
 */
static u32 sun8i_h3_emac_mux_table[SUN8I_H3_EMAC_PARENTS] = {
	0x0,	/* Select TX clock from MII PHY */
	0x2,	/* Select internal TX clock (for RGMII) */
};

static const char *sun8i_h3_emac_mux_parents[SUN8I_H3_EMAC_PARENTS] = {
	MII_PHY_CLK_NAME,
	INT_TX_CLK_NAME,
};

int sun8i_h3_ephy_register_clocks(struct sun8i_h3_ephy *phy)
{
	struct device *dev = phy->dev;
	struct device_node *np = dev->of_node;
	const char *clk_name;
	struct clk_mux *mux;
	struct clk_gate *gate;
	int ret;

	if (of_property_read_string(np, "clock-output-names", &clk_name)) {
		dev_err(phy->dev, "No clock output name given\n");
		return -EINVAL;
	}

	/* allocate mux and gate clock structs */
	mux = devm_kzalloc(dev, sizeof(struct clk_mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	gate = devm_kzalloc(dev, sizeof(struct clk_gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	/* Register fixed clocks to use as parents */
	phy->mac_clks[0] = clk_register_fixed_rate(dev, MII_PHY_CLK_NAME,
						   NULL, 0, MII_PHY_CLK_RATE);
	if (IS_ERR(phy->mac_clks[0]))
		return PTR_ERR(phy->mac_clks[0]);

	phy->mac_clks[1] = clk_register_fixed_rate(dev, INT_TX_CLK_NAME,
						   NULL, 0, INT_TX_CLK_RATE);
	if (IS_ERR(phy->mac_clks[1])) {
		ret = PTR_ERR(phy->mac_clks[1]);
		goto err_int_tx_clk;
	}

	/* set up gate and mux properties */
	gate->reg = phy->reg;
	gate->bit_idx = REG_GPIT;
	gate->lock = &sun8i_h3_ephy_lock;
	mux->reg = phy->reg;
	mux->mask = REG_ETCS_MASK;
	mux->table = sun8i_h3_emac_mux_table;
	mux->lock = &sun8i_h3_ephy_lock;

	phy->mac_clks[2] = clk_register_composite(dev, clk_name,
						  sun8i_h3_emac_mux_parents,
						  SUN8I_H3_EMAC_PARENTS,
						  &mux->hw, &clk_mux_ops,
						  NULL, NULL,
						  &gate->hw, &clk_gate_ops,
						  0);
	if (IS_ERR(phy->mac_clks[2])) {
		ret = PTR_ERR(phy->mac_clks[2]);
		goto err_tx_clk;
	}

	ret = of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				  phy->mac_clks[2]);
	if (ret)
		goto err_of_clk;

	return 0;

err_of_clk:
	/* TODO: switch to clk_unregister_composite when it's available */
	clk_unregister(phy->mac_clks[2]);
err_tx_clk:
	clk_unregister_fixed_rate(phy->mac_clks[1]);
err_int_tx_clk:
	clk_unregister_fixed_rate(phy->mac_clks[0]);

	return ret;
}

static void sun8i_h3_ephy_init_phy(struct sun8i_h3_ephy *phy, u32 addr)
{
	u32 val = readl(phy->reg);

	/* set default values, but don't touch clock settings */
	val = (val & ~REG_DEFAULT_MASK) | REG_DEFAULT_VALUE;

	if (addr < REG_PHY_ADDR_MASK) {
		dev_info(phy->dev, "Using interal PHY at 0x%x\n", addr);

		val |= addr << REG_PHY_ADDR_SHIFT;
		val &= ~REG_SHUTDOWN;
		val |= REG_PHY_SELECT;

		if (of_property_read_bool(phy->dev->of_node,
					  "allwinner,leds-active-low"))
			val |= REG_LED_POL;
	} else {
		dev_info(phy->dev, "Using external PHY\n");
	}

	writel(val, phy->reg);
}

static void sun8i_h3_ephy_reset_phy(struct sun8i_h3_ephy *phy)
{
	u32 val;

	val = readl(phy->reg);
	val = (val & ~REG_DEFAULT_MASK) | REG_DEFAULT_VALUE;
	writel(val, phy->reg);
}

int sun8i_h3_ephy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sun8i_h3_ephy *phy;
	struct resource *res;
	u32 addr = REG_PHY_ADDR_MASK;
	int ret;

	ret = of_property_read_u32(np, "allwinner,ephy-addr", &addr);
	if (!ret && addr > REG_PHY_ADDR_MASK) {
		dev_err(dev, "invalid PHY address: 0x%x\n", addr);
		return -EINVAL;
	} else if (ret && ret != -EINVAL) {
		dev_err(dev, "could not get PHY address: %d\n", ret);
		return ret;
	}

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->dev = dev;
	platform_set_drvdata(pdev, phy);

	phy->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(phy->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(phy->clk);
	}

	phy->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(phy->reset)) {
		dev_err(dev, "failed to get reset control\n");
		return PTR_ERR(phy->reset);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy->reg)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(phy->reg);
	}

	ret = clk_prepare_enable(phy->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	ret = reset_control_deassert(phy->reset);
	if (ret) {
		dev_err(dev, "failed to deassert reset control\n");
		goto err_reset;
	}

	sun8i_h3_ephy_init_phy(phy, addr);

	ret = sun8i_h3_ephy_register_clocks(phy);
	if (ret)
		goto err_tx_clk;

	return 0;

err_tx_clk:
	sun8i_h3_ephy_reset_phy(phy);
	reset_control_assert(phy->reset);
err_reset:
	clk_disable_unprepare(phy->clk);
	return ret;
}

static const struct of_device_id sun8i_h3_ephy_of_match[] = {
	{ .compatible = "allwinner,sun8i-h3-ephy", },
	{ },
};
MODULE_DEVICE_TABLE(of, sun8i_h3_ephy_of_match);

static struct platform_driver sun8i_h3_ephy_driver = {
	.probe	= sun8i_h3_ephy_probe,
	.driver = {
		.of_match_table	= sun8i_h3_ephy_of_match,
		.name  = "sun8i-h3-ephy",
	}
};
module_platform_driver(sun8i_h3_ephy_driver);

MODULE_DESCRIPTION("Allwinner sun8i H3 Ethernet PHY driver");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL v2");
