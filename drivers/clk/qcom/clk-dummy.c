// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019, 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "common.h"
#include "clk-debug.h"

#define to_clk_dummy(_hw)	container_of(_hw, struct clk_dummy, hw)

#define RESET_MAX	100

static int dummy_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_dummy *dummy = to_clk_dummy(hw);

	dummy->rrate = rate;

	pr_debug("%s: rate %lu\n", __func__, rate);

	return 0;
}

static long dummy_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	return rate;
}

static unsigned long dummy_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_dummy *dummy = to_clk_dummy(hw);

	pr_debug("%s: returning a clock rate of %lu\n",
		 __func__, dummy->rrate);

	return dummy->rrate;
}

const struct clk_ops clk_dummy_ops = {
	.set_rate = dummy_clk_set_rate,
	.round_rate = dummy_clk_round_rate,
	.recalc_rate = dummy_clk_recalc_rate,
	.debug_init = clk_debug_measure_add,
};
EXPORT_SYMBOL(clk_dummy_ops);

static int dummy_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return 0;
}

static int dummy_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return 0;
}

static struct reset_control_ops dummy_reset_ops = {
	.assert         = dummy_reset_assert,
	.deassert       = dummy_reset_deassert,
};

/**
 * clk_register_dummy - register dummy clock with the
 *				   clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @flags: framework-specific flags
 * @node: device node
 */
static struct clk *clk_register_dummy(struct device *dev, const char *name,
				       unsigned long flags, struct device_node *node)
{
	struct clk_dummy *dummy;
	struct clk *clk;
	struct clk_init_data init = {};

	/* allocate dummy clock */
	dummy = devm_kzalloc(dev, sizeof(*dummy), GFP_KERNEL);
	if (!dummy)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_dummy_ops;
	init.flags = flags;
	init.num_parents = 0;
	dummy->hw.init = &init;

	/* register the clock */
	clk = devm_clk_register(dev, &dummy->hw);
	if (IS_ERR(clk))
		return clk;

	dummy->reset.of_node = node;
	dummy->reset.ops = &dummy_reset_ops;
	dummy->reset.nr_resets = RESET_MAX;

	if (devm_reset_controller_register(dev, &dummy->reset))
		pr_err("Failed to register reset controller for %s\n", name);
	else
		pr_info("Successfully registered dummy reset controller for %s\n", name);

	return clk;
}

static int dummy_clk_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const char *clk_name = "dummy_clk";
	struct clk *clk;
	int ret;

	of_property_read_string(node, "clock-output-names", &clk_name);

	clk = clk_register_dummy(&pdev->dev, clk_name, 0, node);
	if (!IS_ERR(clk)) {
		ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
		if (ret)
			return ret;
	} else {
		ret = PTR_ERR(clk);
		pr_err("Failed to register dummy clock controller for %s, ret=%d\n",
		       clk_name, ret);
		return ret;
	}

	dev_info(&pdev->dev, "Successfully registered dummy clock controller for %s\n",
		 clk_name);
	return 0;
}

static int dummy_clk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id dummy_clk_match_table[] = {
	{ .compatible = "qcom,dummycc" },
	{ }
};
MODULE_DEVICE_TABLE(of, dummy_clk_match_table);

static struct platform_driver dummy_clk_driver = {
	.probe = dummy_clk_probe,
	.remove = dummy_clk_remove,
	.driver = {
		.name = "clk-dummy",
		.of_match_table = dummy_clk_match_table,
	},
};

static int __init dummy_clk_init(void)
{
	return platform_driver_register(&dummy_clk_driver);
}
arch_initcall(dummy_clk_init);

static void __exit dummy_clk_exit(void)
{
	platform_driver_unregister(&dummy_clk_driver);
}
module_exit(dummy_clk_exit);

MODULE_DESCRIPTION("QTI Dummy Clock Driver");
MODULE_LICENSE("GPL v2");
