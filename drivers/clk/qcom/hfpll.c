// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "clk-regmap.h"
#include "clk-hfpll.h"

static const struct hfpll_data hdata = {
	.mode_reg = 0x00,
	.l_reg = 0x04,
	.m_reg = 0x08,
	.n_reg = 0x0c,
	.user_reg = 0x10,
	.config_reg = 0x14,
	.config_val = 0x430405d,
	.status_reg = 0x1c,
	.lock_bit = 16,

	.user_val = 0x8,
	.user_vco_mask = 0x100000,
	.low_vco_max_rate = 1248000000,
	.min_rate = 537600000UL,
	.max_rate = 2900000000UL,
};

static const struct of_device_id qcom_hfpll_match_table[] = {
	{ .compatible = "qcom,hfpll" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_hfpll_match_table);

static const struct regmap_config hfpll_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x30,
	.fast_io	= true,
};

static int qcom_hfpll_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *regmap;
	struct clk_hfpll *h;
	struct clk_init_data init = {
		.num_parents = 1,
		.ops = &clk_ops_hfpll,
		/*
		 * rather than marking the clock critical and forcing the clock
		 * to be always enabled, we make sure that the clock is not
		 * disabled: the firmware remains responsible of enabling this
		 * clock (for more info check the commit log)
		 */
		.flags = CLK_IGNORE_UNUSED,
	};
	int ret;
	struct clk_parent_data pdata = { .index = 0 };

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, &hfpll_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (of_property_read_string_index(dev->of_node, "clock-output-names",
					  0, &init.name))
		return -ENODEV;

	init.parent_data = &pdata;

	h->d = &hdata;
	h->clkr.hw.init = &init;
	spin_lock_init(&h->lock);

	ret = devm_clk_register_regmap(dev, &h->clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		return ret;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &h->clkr.hw);
}

static struct platform_driver qcom_hfpll_driver = {
	.probe		= qcom_hfpll_probe,
	.driver		= {
		.name	= "qcom-hfpll",
		.of_match_table = qcom_hfpll_match_table,
	},
};
module_platform_driver(qcom_hfpll_driver);

MODULE_DESCRIPTION("QCOM HFPLL Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-hfpll");
