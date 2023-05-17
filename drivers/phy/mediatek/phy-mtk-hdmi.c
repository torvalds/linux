// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include "phy-mtk-hdmi.h"

static int mtk_hdmi_phy_power_on(struct phy *phy);
static int mtk_hdmi_phy_power_off(struct phy *phy);
static int mtk_hdmi_phy_configure(struct phy *phy, union phy_configure_opts *opts);

static const struct phy_ops mtk_hdmi_phy_dev_ops = {
	.power_on = mtk_hdmi_phy_power_on,
	.power_off = mtk_hdmi_phy_power_off,
	.configure = mtk_hdmi_phy_configure,
	.owner = THIS_MODULE,
};

inline struct mtk_hdmi_phy *to_mtk_hdmi_phy(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_hdmi_phy, pll_hw);
}

static int mtk_hdmi_phy_power_on(struct phy *phy)
{
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(hdmi_phy->pll);
	if (ret < 0)
		return ret;

	hdmi_phy->conf->hdmi_phy_enable_tmds(hdmi_phy);
	return 0;
}

static int mtk_hdmi_phy_power_off(struct phy *phy)
{
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);

	hdmi_phy->conf->hdmi_phy_disable_tmds(hdmi_phy);
	clk_disable_unprepare(hdmi_phy->pll);

	return 0;
}

static int mtk_hdmi_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);

	if (hdmi_phy->conf->hdmi_phy_configure)
		return hdmi_phy->conf->hdmi_phy_configure(phy, opts);

	return 0;
}

static const struct phy_ops *
mtk_hdmi_phy_dev_get_ops(const struct mtk_hdmi_phy *hdmi_phy)
{
	if (hdmi_phy && hdmi_phy->conf &&
	    hdmi_phy->conf->hdmi_phy_enable_tmds &&
	    hdmi_phy->conf->hdmi_phy_disable_tmds)
		return &mtk_hdmi_phy_dev_ops;

	if (hdmi_phy)
		dev_err(hdmi_phy->dev, "Failed to get dev ops of phy\n");
	return NULL;
}

static void mtk_hdmi_phy_clk_get_data(struct mtk_hdmi_phy *hdmi_phy,
				      struct clk_init_data *clk_init)
{
	clk_init->flags = hdmi_phy->conf->flags;
	clk_init->ops = hdmi_phy->conf->hdmi_phy_clk_ops;
}

static int mtk_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_hdmi_phy *hdmi_phy;
	struct clk *ref_clk;
	const char *ref_clk_name;
	struct clk_init_data clk_init = {
		.num_parents = 1,
		.parent_names = (const char * const *)&ref_clk_name,
	};

	struct phy *phy;
	struct phy_provider *phy_provider;
	int ret;

	hdmi_phy = devm_kzalloc(dev, sizeof(*hdmi_phy), GFP_KERNEL);
	if (!hdmi_phy)
		return -ENOMEM;

	hdmi_phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdmi_phy->regs))
		return PTR_ERR(hdmi_phy->regs);

	ref_clk = devm_clk_get(dev, "pll_ref");
	if (IS_ERR(ref_clk))
		return dev_err_probe(dev, PTR_ERR(ref_clk),
				     "Failed to get PLL reference clock\n");

	ref_clk_name = __clk_get_name(ref_clk);

	ret = of_property_read_string(dev->of_node, "clock-output-names",
				      &clk_init.name);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read clock-output-names\n");

	hdmi_phy->dev = dev;
	hdmi_phy->conf =
		(struct mtk_hdmi_phy_conf *)of_device_get_match_data(dev);
	mtk_hdmi_phy_clk_get_data(hdmi_phy, &clk_init);
	hdmi_phy->pll_hw.init = &clk_init;
	hdmi_phy->pll = devm_clk_register(dev, &hdmi_phy->pll_hw);
	if (IS_ERR(hdmi_phy->pll))
		return dev_err_probe(dev, PTR_ERR(hdmi_phy->pll),
				    "Failed to register PLL\n");

	ret = of_property_read_u32(dev->of_node, "mediatek,ibias",
				   &hdmi_phy->ibias);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get ibias\n");

	ret = of_property_read_u32(dev->of_node, "mediatek,ibias_up",
				   &hdmi_phy->ibias_up);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get ibias_up\n");

	dev_info(dev, "Using default TX DRV impedance: 4.2k/36\n");
	hdmi_phy->drv_imp_clk = 0x30;
	hdmi_phy->drv_imp_d2 = 0x30;
	hdmi_phy->drv_imp_d1 = 0x30;
	hdmi_phy->drv_imp_d0 = 0x30;

	phy = devm_phy_create(dev, NULL, mtk_hdmi_phy_dev_get_ops(hdmi_phy));
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "Cannot create HDMI PHY\n");

	phy_set_drvdata(phy, hdmi_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "Failed to register HDMI PHY\n");

	if (hdmi_phy->conf->pll_default_off)
		hdmi_phy->conf->hdmi_phy_disable_tmds(hdmi_phy);

	return of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				   hdmi_phy->pll);
}

static const struct of_device_id mtk_hdmi_phy_match[] = {
	{ .compatible = "mediatek,mt2701-hdmi-phy",
	  .data = &mtk_hdmi_phy_2701_conf,
	},
	{ .compatible = "mediatek,mt8173-hdmi-phy",
	  .data = &mtk_hdmi_phy_8173_conf,
	},
	{ .compatible = "mediatek,mt8195-hdmi-phy",
	  .data = &mtk_hdmi_phy_8195_conf,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_hdmi_phy_match);

static struct platform_driver mtk_hdmi_phy_driver = {
	.probe = mtk_hdmi_phy_probe,
	.driver = {
		.name = "mediatek-hdmi-phy",
		.of_match_table = mtk_hdmi_phy_match,
	},
};
module_platform_driver(mtk_hdmi_phy_driver);

MODULE_DESCRIPTION("MediaTek HDMI PHY Driver");
MODULE_LICENSE("GPL v2");
