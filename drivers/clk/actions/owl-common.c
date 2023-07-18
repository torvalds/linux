// SPDX-License-Identifier: GPL-2.0+
//
// OWL common clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "owl-common.h"

static const struct regmap_config owl_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x00cc,
	.fast_io	= true,
};

static void owl_clk_set_regmap(const struct owl_clk_desc *desc,
			 struct regmap *regmap)
{
	int i;
	struct owl_clk_common *clks;

	for (i = 0; i < desc->num_clks; i++) {
		clks = desc->clks[i];
		if (!clks)
			continue;

		clks->regmap = regmap;
	}
}

int owl_clk_regmap_init(struct platform_device *pdev,
			struct owl_clk_desc *desc)
{
	void __iomem *base;
	struct regmap *regmap;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, &owl_regmap_config);
	if (IS_ERR(regmap)) {
		pr_err("failed to init regmap\n");
		return PTR_ERR(regmap);
	}

	owl_clk_set_regmap(desc, regmap);
	desc->regmap = regmap;

	return 0;
}

int owl_clk_probe(struct device *dev, struct clk_hw_onecell_data *hw_clks)
{
	int i, ret;
	struct clk_hw *hw;

	for (i = 0; i < hw_clks->num; i++) {
		const char *name;

		hw = hw_clks->hws[i];
		if (IS_ERR_OR_NULL(hw))
			continue;

		name = hw->init->name;
		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "Couldn't register clock %d - %s\n",
				i, name);
			return ret;
		}
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, hw_clks);
	if (ret)
		dev_err(dev, "Failed to add clock provider\n");

	return ret;
}
