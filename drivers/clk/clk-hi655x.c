// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clock driver for Hi655x
 *
 * Copyright (c) 2017, Linaro Ltd.
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/hi655x-pmic.h>

#define HI655X_CLK_BASE	HI655X_BUS_ADDR(0x1c)
#define HI655X_CLK_SET	BIT(6)

struct hi655x_clk {
	struct hi655x_pmic *hi655x;
	struct clk_hw       clk_hw;
};

static unsigned long hi655x_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	return 32768;
}

static int hi655x_clk_enable(struct clk_hw *hw, bool enable)
{
	struct hi655x_clk *hi655x_clk =
		container_of(hw, struct hi655x_clk, clk_hw);

	struct hi655x_pmic *hi655x = hi655x_clk->hi655x;

	return regmap_update_bits(hi655x->regmap, HI655X_CLK_BASE,
				  HI655X_CLK_SET, enable ? HI655X_CLK_SET : 0);
}

static int hi655x_clk_prepare(struct clk_hw *hw)
{
	return hi655x_clk_enable(hw, true);
}

static void hi655x_clk_unprepare(struct clk_hw *hw)
{
	hi655x_clk_enable(hw, false);
}

static int hi655x_clk_is_prepared(struct clk_hw *hw)
{
	struct hi655x_clk *hi655x_clk =
		container_of(hw, struct hi655x_clk, clk_hw);
	struct hi655x_pmic *hi655x = hi655x_clk->hi655x;
	int ret;
	uint32_t val;

	ret = regmap_read(hi655x->regmap, HI655X_CLK_BASE, &val);
	if (ret < 0)
		return ret;

	return val & HI655X_CLK_BASE;
}

static const struct clk_ops hi655x_clk_ops = {
	.prepare     = hi655x_clk_prepare,
	.unprepare   = hi655x_clk_unprepare,
	.is_prepared = hi655x_clk_is_prepared,
	.recalc_rate = hi655x_clk_recalc_rate,
};

static int hi655x_clk_probe(struct platform_device *pdev)
{
	struct device *parent = pdev->dev.parent;
	struct hi655x_pmic *hi655x = dev_get_drvdata(parent);
	struct hi655x_clk *hi655x_clk;
	const char *clk_name = "hi655x-clk";
	struct clk_init_data init = {
		.name = clk_name,
		.ops = &hi655x_clk_ops
	};
	int ret;

	hi655x_clk = devm_kzalloc(&pdev->dev, sizeof(*hi655x_clk), GFP_KERNEL);
	if (!hi655x_clk)
		return -ENOMEM;

	of_property_read_string_index(parent->of_node, "clock-output-names",
				      0, &clk_name);

	hi655x_clk->clk_hw.init	= &init;
	hi655x_clk->hi655x	= hi655x;

	platform_set_drvdata(pdev, hi655x_clk);

	ret = devm_clk_hw_register(&pdev->dev, &hi655x_clk->clk_hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					   &hi655x_clk->clk_hw);
}

static struct platform_driver hi655x_clk_driver = {
	.probe =  hi655x_clk_probe,
	.driver		= {
		.name	= "hi655x-clk",
	},
};

module_platform_driver(hi655x_clk_driver);

MODULE_DESCRIPTION("Clk driver for the hi655x series PMICs");
MODULE_AUTHOR("Daniel Lezcano <daniel.lezcano@linaro.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hi655x-clk");
