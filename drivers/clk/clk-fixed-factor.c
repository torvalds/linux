// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 */
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/*
 * DOC: basic fixed multiplier and divider clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is fixed.  clk->rate = parent->rate / div * mult
 * parent - fixed parent.  No clk_set_parent support
 */

static unsigned long clk_factor_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_fixed_factor *fix = to_clk_fixed_factor(hw);
	unsigned long long int rate;

	rate = (unsigned long long int)parent_rate * fix->mult;
	do_div(rate, fix->div);
	return (unsigned long)rate;
}

static long clk_factor_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_fixed_factor *fix = to_clk_fixed_factor(hw);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		unsigned long best_parent;

		best_parent = (rate / fix->mult) * fix->div;
		*prate = clk_hw_round_rate(clk_hw_get_parent(hw), best_parent);
	}

	return (*prate / fix->div) * fix->mult;
}

static int clk_factor_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * clk_factor_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

const struct clk_ops clk_fixed_factor_ops = {
	.round_rate = clk_factor_round_rate,
	.set_rate = clk_factor_set_rate,
	.recalc_rate = clk_factor_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_fixed_factor_ops);

static void devm_clk_hw_register_fixed_factor_release(struct device *dev, void *res)
{
	struct clk_fixed_factor *fix = res;

	/*
	 * We can not use clk_hw_unregister_fixed_factor, since it will kfree()
	 * the hw, resulting in double free. Just unregister the hw and let
	 * devres code kfree() it.
	 */
	clk_hw_unregister(&fix->hw);
}

static struct clk_hw *
__clk_hw_register_fixed_factor(struct device *dev, struct device_node *np,
		const char *name, const char *parent_name, int index,
		unsigned long flags, unsigned int mult, unsigned int div,
		bool devm)
{
	struct clk_fixed_factor *fix;
	struct clk_init_data init = { };
	struct clk_parent_data pdata = { .index = index };
	struct clk_hw *hw;
	int ret;

	/* You can't use devm without a dev */
	if (devm && !dev)
		return ERR_PTR(-EINVAL);

	if (devm)
		fix = devres_alloc(devm_clk_hw_register_fixed_factor_release,
				sizeof(*fix), GFP_KERNEL);
	else
		fix = kmalloc(sizeof(*fix), GFP_KERNEL);
	if (!fix)
		return ERR_PTR(-ENOMEM);

	/* struct clk_fixed_factor assignments */
	fix->mult = mult;
	fix->div = div;
	fix->hw.init = &init;

	init.name = name;
	init.ops = &clk_fixed_factor_ops;
	init.flags = flags;
	if (parent_name)
		init.parent_names = &parent_name;
	else
		init.parent_data = &pdata;
	init.num_parents = 1;

	hw = &fix->hw;
	if (dev)
		ret = clk_hw_register(dev, hw);
	else
		ret = of_clk_hw_register(np, hw);
	if (ret) {
		if (devm)
			devres_free(fix);
		else
			kfree(fix);
		hw = ERR_PTR(ret);
	} else if (devm)
		devres_add(dev, fix);

	return hw;
}

/**
 * devm_clk_hw_register_fixed_factor_index - Register a fixed factor clock with
 * parent from DT index
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @index: index of phandle in @dev 'clocks' property
 * @flags: fixed factor flags
 * @mult: multiplier
 * @div: divider
 *
 * Return: Pointer to fixed factor clk_hw structure that was registered or
 * an error pointer.
 */
struct clk_hw *devm_clk_hw_register_fixed_factor_index(struct device *dev,
		const char *name, unsigned int index, unsigned long flags,
		unsigned int mult, unsigned int div)
{
	return __clk_hw_register_fixed_factor(dev, NULL, name, NULL, index,
					      flags, mult, div, true);
}
EXPORT_SYMBOL_GPL(devm_clk_hw_register_fixed_factor_index);

struct clk_hw *clk_hw_register_fixed_factor(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		unsigned int mult, unsigned int div)
{
	return __clk_hw_register_fixed_factor(dev, NULL, name, parent_name, -1,
					      flags, mult, div, false);
}
EXPORT_SYMBOL_GPL(clk_hw_register_fixed_factor);

struct clk *clk_register_fixed_factor(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned int mult, unsigned int div)
{
	struct clk_hw *hw;

	hw = clk_hw_register_fixed_factor(dev, name, parent_name, flags, mult,
					  div);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}
EXPORT_SYMBOL_GPL(clk_register_fixed_factor);

void clk_unregister_fixed_factor(struct clk *clk)
{
	struct clk_hw *hw;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	clk_unregister(clk);
	kfree(to_clk_fixed_factor(hw));
}
EXPORT_SYMBOL_GPL(clk_unregister_fixed_factor);

void clk_hw_unregister_fixed_factor(struct clk_hw *hw)
{
	struct clk_fixed_factor *fix;

	fix = to_clk_fixed_factor(hw);

	clk_hw_unregister(hw);
	kfree(fix);
}
EXPORT_SYMBOL_GPL(clk_hw_unregister_fixed_factor);

struct clk_hw *devm_clk_hw_register_fixed_factor(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		unsigned int mult, unsigned int div)
{
	return __clk_hw_register_fixed_factor(dev, NULL, name, parent_name, -1,
			flags, mult, div, true);
}
EXPORT_SYMBOL_GPL(devm_clk_hw_register_fixed_factor);

#ifdef CONFIG_OF
static const struct of_device_id set_rate_parent_matches[] = {
	{ .compatible = "allwinner,sun4i-a10-pll3-2x-clk" },
	{ /* Sentinel */ },
};

static struct clk_hw *_of_fixed_factor_clk_setup(struct device_node *node)
{
	struct clk_hw *hw;
	const char *clk_name = node->name;
	unsigned long flags = 0;
	u32 div, mult;
	int ret;

	if (of_property_read_u32(node, "clock-div", &div)) {
		pr_err("%s Fixed factor clock <%pOFn> must have a clock-div property\n",
			__func__, node);
		return ERR_PTR(-EIO);
	}

	if (of_property_read_u32(node, "clock-mult", &mult)) {
		pr_err("%s Fixed factor clock <%pOFn> must have a clock-mult property\n",
			__func__, node);
		return ERR_PTR(-EIO);
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	if (of_match_node(set_rate_parent_matches, node))
		flags |= CLK_SET_RATE_PARENT;

	hw = __clk_hw_register_fixed_factor(NULL, node, clk_name, NULL, 0,
					    flags, mult, div, false);
	if (IS_ERR(hw)) {
		/*
		 * Clear OF_POPULATED flag so that clock registration can be
		 * attempted again from probe function.
		 */
		of_node_clear_flag(node, OF_POPULATED);
		return ERR_CAST(hw);
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);
	if (ret) {
		clk_hw_unregister_fixed_factor(hw);
		return ERR_PTR(ret);
	}

	return hw;
}

/**
 * of_fixed_factor_clk_setup() - Setup function for simple fixed factor clock
 * @node:	device node for the clock
 */
void __init of_fixed_factor_clk_setup(struct device_node *node)
{
	_of_fixed_factor_clk_setup(node);
}
CLK_OF_DECLARE(fixed_factor_clk, "fixed-factor-clock",
		of_fixed_factor_clk_setup);

static int of_fixed_factor_clk_remove(struct platform_device *pdev)
{
	struct clk_hw *clk = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);
	clk_hw_unregister_fixed_factor(clk);

	return 0;
}

static int of_fixed_factor_clk_probe(struct platform_device *pdev)
{
	struct clk_hw *clk;

	/*
	 * This function is not executed when of_fixed_factor_clk_setup
	 * succeeded.
	 */
	clk = _of_fixed_factor_clk_setup(pdev->dev.of_node);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	platform_set_drvdata(pdev, clk);

	return 0;
}

static const struct of_device_id of_fixed_factor_clk_ids[] = {
	{ .compatible = "fixed-factor-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_fixed_factor_clk_ids);

static struct platform_driver of_fixed_factor_clk_driver = {
	.driver = {
		.name = "of_fixed_factor_clk",
		.of_match_table = of_fixed_factor_clk_ids,
	},
	.probe = of_fixed_factor_clk_probe,
	.remove = of_fixed_factor_clk_remove,
};
builtin_platform_driver(of_fixed_factor_clk_driver);
#endif
