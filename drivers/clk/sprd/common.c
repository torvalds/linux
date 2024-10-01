// SPDX-License-Identifier: GPL-2.0
//
// Spreadtrum clock infrastructure
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "common.h"

static const struct regmap_config sprdclk_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.fast_io	= true,
};

static void sprd_clk_set_regmap(const struct sprd_clk_desc *desc,
			 struct regmap *regmap)
{
	int i;
	struct sprd_clk_common *cclk;

	for (i = 0; i < desc->num_clk_clks; i++) {
		cclk = desc->clk_clks[i];
		if (!cclk)
			continue;

		cclk->regmap = regmap;
	}
}

int sprd_clk_regmap_init(struct platform_device *pdev,
			 const struct sprd_clk_desc *desc)
{
	void __iomem *base;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *np;
	struct regmap *regmap;
	struct resource *res;
	struct regmap_config reg_config = sprdclk_regmap_config;

	if (of_property_present(node, "sprd,syscon")) {
		regmap = syscon_regmap_lookup_by_phandle(node, "sprd,syscon");
		if (IS_ERR(regmap)) {
			pr_err("%s: failed to get syscon regmap\n", __func__);
			return PTR_ERR(regmap);
		}
	} else if (of_device_is_compatible(np =	of_get_parent(node), "syscon") ||
		   (of_node_put(np), 0)) {
		regmap = device_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR(regmap)) {
			dev_err(dev, "failed to get regmap from its parent.\n");
			return PTR_ERR(regmap);
		}
	} else {
		base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		reg_config.max_register = resource_size(res) - reg_config.reg_stride;

		regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &reg_config);
		if (IS_ERR(regmap)) {
			pr_err("failed to init regmap\n");
			return PTR_ERR(regmap);
		}
	}

	sprd_clk_set_regmap(desc, regmap);

	return 0;
}
EXPORT_SYMBOL_GPL(sprd_clk_regmap_init);

int sprd_clk_probe(struct device *dev, struct clk_hw_onecell_data *clkhw)
{
	int i, ret;
	struct clk_hw *hw;

	for (i = 0; i < clkhw->num; i++) {
		const char *name;

		hw = clkhw->hws[i];
		if (!hw)
			continue;

		name = hw->init->name;
		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "Couldn't register clock %d - %s\n",
				i, name);
			return ret;
		}
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clkhw);
	if (ret)
		dev_err(dev, "Failed to add clock provider\n");

	return ret;
}
EXPORT_SYMBOL_GPL(sprd_clk_probe);

MODULE_DESCRIPTION("Spreadtrum clock infrastructure");
MODULE_LICENSE("GPL v2");
