// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-uniphier.h"

static struct clk_hw *uniphier_clk_register(struct device *dev,
					    struct regmap *regmap,
					const struct uniphier_clk_data *data)
{
	switch (data->type) {
	case UNIPHIER_CLK_TYPE_CPUGEAR:
		return uniphier_clk_register_cpugear(dev, regmap, data->name,
						     &data->data.cpugear);
	case UNIPHIER_CLK_TYPE_FIXED_FACTOR:
		return uniphier_clk_register_fixed_factor(dev, data->name,
							  &data->data.factor);
	case UNIPHIER_CLK_TYPE_FIXED_RATE:
		return uniphier_clk_register_fixed_rate(dev, data->name,
							&data->data.rate);
	case UNIPHIER_CLK_TYPE_GATE:
		return uniphier_clk_register_gate(dev, regmap, data->name,
						  &data->data.gate);
	case UNIPHIER_CLK_TYPE_MUX:
		return uniphier_clk_register_mux(dev, regmap, data->name,
						 &data->data.mux);
	default:
		dev_err(dev, "unsupported clock type\n");
		return ERR_PTR(-EINVAL);
	}
}

static int uniphier_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *hw_data;
	const struct uniphier_clk_data *p, *data;
	struct regmap *regmap;
	struct device_node *parent;
	int clk_num = 0;

	data = of_device_get_match_data(dev);
	if (WARN_ON(!data))
		return -EINVAL;

	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get regmap (error %ld)\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	for (p = data; p->name; p++)
		clk_num = max(clk_num, p->idx + 1);

	hw_data = devm_kzalloc(dev, struct_size(hw_data, hws, clk_num),
			GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	hw_data->num = clk_num;

	/* avoid returning NULL for unused idx */
	while (--clk_num >= 0)
		hw_data->hws[clk_num] = ERR_PTR(-EINVAL);

	for (p = data; p->name; p++) {
		struct clk_hw *hw;

		dev_dbg(dev, "register %s (index=%d)\n", p->name, p->idx);
		hw = uniphier_clk_register(dev, regmap, p);
		if (WARN(IS_ERR(hw), "failed to register %s", p->name))
			continue;

		if (p->idx >= 0)
			hw_data->hws[p->idx] = hw;
	}

	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				      hw_data);
}

static int uniphier_clk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id uniphier_clk_match[] = {
	/* System clock */
	{
		.compatible = "socionext,uniphier-ld4-clock",
		.data = uniphier_ld4_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-clock",
		.data = uniphier_pro4_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-clock",
		.data = uniphier_sld8_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-clock",
		.data = uniphier_pro5_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-clock",
		.data = uniphier_pxs2_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-clock",
		.data = uniphier_ld11_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-clock",
		.data = uniphier_ld20_sys_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-clock",
		.data = uniphier_pxs3_sys_clk_data,
	},
	/* Media I/O clock, SD clock */
	{
		.compatible = "socionext,uniphier-ld4-mio-clock",
		.data = uniphier_ld4_mio_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-mio-clock",
		.data = uniphier_ld4_mio_clk_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-mio-clock",
		.data = uniphier_ld4_mio_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-sd-clock",
		.data = uniphier_pro5_sd_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-sd-clock",
		.data = uniphier_pro5_sd_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-mio-clock",
		.data = uniphier_ld4_mio_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-sd-clock",
		.data = uniphier_pro5_sd_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-sd-clock",
		.data = uniphier_pro5_sd_clk_data,
	},
	/* Peripheral clock */
	{
		.compatible = "socionext,uniphier-ld4-peri-clock",
		.data = uniphier_ld4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro4-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-sld8-peri-clock",
		.data = uniphier_ld4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pro5-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-peri-clock",
		.data = uniphier_pro4_peri_clk_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver uniphier_clk_driver = {
	.probe = uniphier_clk_probe,
	.remove = uniphier_clk_remove,
	.driver = {
		.name = "uniphier-clk",
		.of_match_table = uniphier_clk_match,
	},
};
builtin_platform_driver(uniphier_clk_driver);
