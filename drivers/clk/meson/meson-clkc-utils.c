// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "meson-clkc-utils.h"

struct clk_hw *meson_clk_hw_get(struct of_phandle_args *clkspec, void *clk_hw_data)
{
	const struct meson_clk_hw_data *data = clk_hw_data;
	unsigned int idx = clkspec->args[0];

	if (idx >= data->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return data->hws[idx];
}
EXPORT_SYMBOL_NS_GPL(meson_clk_hw_get, "CLK_MESON");

static int meson_clkc_init(struct device *dev, struct regmap *map)
{
	const struct meson_clkc_data *data;
	struct clk_hw *hw;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	if (data->init_count)
		regmap_multi_reg_write(map, data->init_regs, data->init_count);

	for (i = 0; i < data->hw_clks.num; i++) {
		hw = data->hw_clks.hws[i];

		/* array might be sparse */
		if (!hw)
			continue;

		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "registering %s clock failed\n",
				hw->init->name);
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, meson_clk_hw_get, (void *)&data->hw_clks);
}

int meson_clkc_syscon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct regmap *map;

	np = of_get_parent(dev->of_node);
	map = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to get parent syscon regmap\n");
		return PTR_ERR(map);
	}

	return meson_clkc_init(dev, map);
}
EXPORT_SYMBOL_NS_GPL(meson_clkc_syscon_probe, "CLK_MESON");

int meson_clkc_mmio_probe(struct platform_device *pdev)
{
	const struct meson_clkc_data *data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *base;
	struct regmap *map;
	struct regmap_config regmap_cfg = {
			.reg_bits	= 32,
			.val_bits	= 32,
			.reg_stride	= 4,
	};

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap_cfg.max_register = resource_size(res) - regmap_cfg.reg_stride;

	map = devm_regmap_init_mmio(dev, base, &regmap_cfg);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return meson_clkc_init(dev, map);
}
EXPORT_SYMBOL_NS_GPL(meson_clkc_mmio_probe, "CLK_MESON");

MODULE_DESCRIPTION("Amlogic Clock Controller Utilities");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
