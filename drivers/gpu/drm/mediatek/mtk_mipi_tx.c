// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include "mtk_mipi_tx.h"

inline struct mtk_mipi_tx *mtk_mipi_tx_from_clk_hw(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_mipi_tx, pll_hw);
}

void mtk_mipi_tx_clear_bits(struct mtk_mipi_tx *mipi_tx, u32 offset,
			    u32 bits)
{
	u32 temp = readl(mipi_tx->regs + offset);

	writel(temp & ~bits, mipi_tx->regs + offset);
}

void mtk_mipi_tx_set_bits(struct mtk_mipi_tx *mipi_tx, u32 offset,
			  u32 bits)
{
	u32 temp = readl(mipi_tx->regs + offset);

	writel(temp | bits, mipi_tx->regs + offset);
}

void mtk_mipi_tx_update_bits(struct mtk_mipi_tx *mipi_tx, u32 offset,
			     u32 mask, u32 data)
{
	u32 temp = readl(mipi_tx->regs + offset);

	writel((temp & ~mask) | (data & mask), mipi_tx->regs + offset);
}

int mtk_mipi_tx_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);

	dev_dbg(mipi_tx->dev, "set rate: %lu Hz\n", rate);

	mipi_tx->data_rate = rate;

	return 0;
}

unsigned long mtk_mipi_tx_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);

	return mipi_tx->data_rate;
}

static int mtk_mipi_tx_power_on(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	int ret;

	/* Power up core and enable PLL */
	ret = clk_prepare_enable(mipi_tx->pll);
	if (ret < 0)
		return ret;

	/* Enable DSI Lane LDO outputs, disable pad tie low */
	mipi_tx->driver_data->mipi_tx_enable_signal(phy);
	return 0;
}

static int mtk_mipi_tx_power_off(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);

	/* Enable pad tie low, disable DSI Lane LDO outputs */
	mipi_tx->driver_data->mipi_tx_disable_signal(phy);

	/* Disable PLL and power down core */
	clk_disable_unprepare(mipi_tx->pll);

	return 0;
}

static const struct phy_ops mtk_mipi_tx_ops = {
	.power_on = mtk_mipi_tx_power_on,
	.power_off = mtk_mipi_tx_power_off,
	.owner = THIS_MODULE,
};

static int mtk_mipi_tx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mipi_tx *mipi_tx;
	struct resource *mem;
	const char *ref_clk_name;
	struct clk *ref_clk;
	struct clk_init_data clk_init = {
		.num_parents = 1,
		.parent_names = (const char * const *)&ref_clk_name,
		.flags = CLK_SET_RATE_GATE,
	};
	struct phy *phy;
	struct phy_provider *phy_provider;
	int ret;

	mipi_tx = devm_kzalloc(dev, sizeof(*mipi_tx), GFP_KERNEL);
	if (!mipi_tx)
		return -ENOMEM;

	mipi_tx->driver_data = of_device_get_match_data(dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mipi_tx->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(mipi_tx->regs)) {
		ret = PTR_ERR(mipi_tx->regs);
		dev_err(dev, "Failed to get memory resource: %d\n", ret);
		return ret;
	}

	ref_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ref_clk)) {
		ret = PTR_ERR(ref_clk);
		dev_err(dev, "Failed to get reference clock: %d\n", ret);
		return ret;
	}

	ref_clk_name = __clk_get_name(ref_clk);

	ret = of_property_read_string(dev->of_node, "clock-output-names",
				      &clk_init.name);
	if (ret < 0) {
		dev_err(dev, "Failed to read clock-output-names: %d\n", ret);
		return ret;
	}

	clk_init.ops = mipi_tx->driver_data->mipi_tx_clk_ops;

	mipi_tx->pll_hw.init = &clk_init;
	mipi_tx->pll = devm_clk_register(dev, &mipi_tx->pll_hw);
	if (IS_ERR(mipi_tx->pll)) {
		ret = PTR_ERR(mipi_tx->pll);
		dev_err(dev, "Failed to register PLL: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &mtk_mipi_tx_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(dev, "Failed to create MIPI D-PHY: %d\n", ret);
		return ret;
	}
	phy_set_drvdata(phy, mipi_tx);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		return ret;
	}

	mipi_tx->dev = dev;

	return of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				   mipi_tx->pll);
}

static int mtk_mipi_tx_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
	return 0;
}

static const struct of_device_id mtk_mipi_tx_match[] = {
	{ .compatible = "mediatek,mt2701-mipi-tx",
	  .data = &mt2701_mipitx_data },
	{ .compatible = "mediatek,mt8173-mipi-tx",
	  .data = &mt8173_mipitx_data },
	{ .compatible = "mediatek,mt8183-mipi-tx",
	  .data = &mt8183_mipitx_data },
	{ },
};

struct platform_driver mtk_mipi_tx_driver = {
	.probe = mtk_mipi_tx_probe,
	.remove = mtk_mipi_tx_remove,
	.driver = {
		.name = "mediatek-mipi-tx",
		.of_match_table = mtk_mipi_tx_match,
	},
};

