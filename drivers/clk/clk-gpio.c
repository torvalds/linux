/*
 * Copyright (C) 2013 - 2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors:
 *    Jyri Sarha <jsarha@ti.com>
 *    Sergej Sawazki <ce3a@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gpio controlled clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

/**
 * DOC: basic gpio gated clock which can be enabled and disabled
 *      with gpio output
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gpio
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

static int clk_gpio_gate_enable(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value(clk->gpiod, 1);

	return 0;
}

static void clk_gpio_gate_disable(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value(clk->gpiod, 0);
}

static int clk_gpio_gate_is_enabled(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	return gpiod_get_value(clk->gpiod);
}

const struct clk_ops clk_gpio_gate_ops = {
	.enable = clk_gpio_gate_enable,
	.disable = clk_gpio_gate_disable,
	.is_enabled = clk_gpio_gate_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_gpio_gate_ops);

/**
 * DOC: basic clock multiplexer which can be controlled with a gpio output
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

static u8 clk_gpio_mux_get_parent(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	return gpiod_get_value_cansleep(clk->gpiod);
}

static int clk_gpio_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value_cansleep(clk->gpiod, index);

	return 0;
}

const struct clk_ops clk_gpio_mux_ops = {
	.get_parent = clk_gpio_mux_get_parent,
	.set_parent = clk_gpio_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_gpio_mux_ops);

static struct clk_hw *clk_register_gpio(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents, struct gpio_desc *gpiod,
		unsigned long flags, const struct clk_ops *clk_gpio_ops)
{
	struct clk_gpio *clk_gpio;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	int err;

	if (dev)
		clk_gpio = devm_kzalloc(dev, sizeof(*clk_gpio),	GFP_KERNEL);
	else
		clk_gpio = kzalloc(sizeof(*clk_gpio), GFP_KERNEL);

	if (!clk_gpio)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = clk_gpio_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_gpio->gpiod = gpiod;
	clk_gpio->hw.init = &init;

	hw = &clk_gpio->hw;
	if (dev)
		err = devm_clk_hw_register(dev, hw);
	else
		err = clk_hw_register(NULL, hw);

	if (!err)
		return hw;

	if (!dev) {
		kfree(clk_gpio);
	}

	return ERR_PTR(err);
}

/**
 * clk_hw_register_gpio_gate - register a gpio clock gate with the clock
 * framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @gpiod: gpio descriptor to gate this clock
 * @flags: clock flags
 */
struct clk_hw *clk_hw_register_gpio_gate(struct device *dev, const char *name,
		const char *parent_name, struct gpio_desc *gpiod,
		unsigned long flags)
{
	return clk_register_gpio(dev, name,
			(parent_name ? &parent_name : NULL),
			(parent_name ? 1 : 0), gpiod, flags,
			&clk_gpio_gate_ops);
}
EXPORT_SYMBOL_GPL(clk_hw_register_gpio_gate);

struct clk *clk_register_gpio_gate(struct device *dev, const char *name,
		const char *parent_name, struct gpio_desc *gpiod,
		unsigned long flags)
{
	struct clk_hw *hw;

	hw = clk_hw_register_gpio_gate(dev, name, parent_name, gpiod, flags);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio_gate);

/**
 * clk_hw_register_gpio_mux - register a gpio clock mux with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_names: names of this clock's parents
 * @num_parents: number of parents listed in @parent_names
 * @gpiod: gpio descriptor to gate this clock
 * @flags: clock flags
 */
struct clk_hw *clk_hw_register_gpio_mux(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents, struct gpio_desc *gpiod,
		unsigned long flags)
{
	if (num_parents != 2) {
		pr_err("mux-clock %s must have 2 parents\n", name);
		return ERR_PTR(-EINVAL);
	}

	return clk_register_gpio(dev, name, parent_names, num_parents,
			gpiod, flags, &clk_gpio_mux_ops);
}
EXPORT_SYMBOL_GPL(clk_hw_register_gpio_mux);

struct clk *clk_register_gpio_mux(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents, struct gpio_desc *gpiod,
		unsigned long flags)
{
	struct clk_hw *hw;

	hw = clk_hw_register_gpio_mux(dev, name, parent_names, num_parents,
			gpiod, flags);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio_mux);

static int gpio_clk_driver_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const char **parent_names, *gpio_name;
	unsigned int num_parents;
	struct gpio_desc *gpiod;
	struct clk *clk;
	bool is_mux;
	int ret;

	num_parents = of_clk_get_parent_count(node);
	if (num_parents) {
		parent_names = devm_kcalloc(&pdev->dev, num_parents,
					    sizeof(char *), GFP_KERNEL);
		if (!parent_names)
			return -ENOMEM;

		of_clk_parent_fill(node, parent_names, num_parents);
	} else {
		parent_names = NULL;
	}

	is_mux = of_device_is_compatible(node, "gpio-mux-clock");

	gpio_name = is_mux ? "select" : "enable";
	gpiod = devm_gpiod_get(&pdev->dev, gpio_name, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		ret = PTR_ERR(gpiod);
		if (ret == -EPROBE_DEFER)
			pr_debug("%pOFn: %s: GPIOs not yet available, retry later\n",
					node, __func__);
		else
			pr_err("%pOFn: %s: Can't get '%s' named GPIO property\n",
					node, __func__,
					gpio_name);
		return ret;
	}

	if (is_mux)
		clk = clk_register_gpio_mux(&pdev->dev, node->name,
				parent_names, num_parents, gpiod, 0);
	else
		clk = clk_register_gpio_gate(&pdev->dev, node->name,
				parent_names ?  parent_names[0] : NULL, gpiod,
				0);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return of_clk_add_provider(node, of_clk_src_simple_get, clk);
}

static const struct of_device_id gpio_clk_match_table[] = {
	{ .compatible = "gpio-mux-clock" },
	{ .compatible = "gpio-gate-clock" },
	{ }
};

static struct platform_driver gpio_clk_driver = {
	.probe		= gpio_clk_driver_probe,
	.driver		= {
		.name	= "gpio-clk",
		.of_match_table = gpio_clk_match_table,
	},
};
builtin_platform_driver(gpio_clk_driver);
