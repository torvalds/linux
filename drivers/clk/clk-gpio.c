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
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/device.h>

/**
 * DOC: basic gpio gated clock which can be enabled and disabled
 *      with gpio output
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gpio
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_gpio(_hw) container_of(_hw, struct clk_gpio, hw)

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

	return gpiod_get_value(clk->gpiod);
}

static int clk_gpio_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value(clk->gpiod, index);

	return 0;
}

const struct clk_ops clk_gpio_mux_ops = {
	.get_parent = clk_gpio_mux_get_parent,
	.set_parent = clk_gpio_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_gpio_mux_ops);

static struct clk *clk_register_gpio(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents, unsigned gpio,
		bool active_low, unsigned long flags,
		const struct clk_ops *clk_gpio_ops)
{
	struct clk_gpio *clk_gpio;
	struct clk *clk;
	struct clk_init_data init = {};
	unsigned long gpio_flags;
	int err;

	if (dev)
		clk_gpio = devm_kzalloc(dev, sizeof(*clk_gpio),	GFP_KERNEL);
	else
		clk_gpio = kzalloc(sizeof(*clk_gpio), GFP_KERNEL);

	if (!clk_gpio)
		return ERR_PTR(-ENOMEM);

	if (active_low)
		gpio_flags = GPIOF_ACTIVE_LOW | GPIOF_OUT_INIT_HIGH;
	else
		gpio_flags = GPIOF_OUT_INIT_LOW;

	if (dev)
		err = devm_gpio_request_one(dev, gpio, gpio_flags, name);
	else
		err = gpio_request_one(gpio, gpio_flags, name);
	if (err) {
		if (err != -EPROBE_DEFER)
			pr_err("%s: %s: Error requesting clock control gpio %u\n",
					__func__, name, gpio);
		if (!dev)
			kfree(clk_gpio);

		return ERR_PTR(err);
	}

	init.name = name;
	init.ops = clk_gpio_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_gpio->gpiod = gpio_to_desc(gpio);
	clk_gpio->hw.init = &init;

	if (dev)
		clk = devm_clk_register(dev, &clk_gpio->hw);
	else
		clk = clk_register(NULL, &clk_gpio->hw);

	if (!IS_ERR(clk))
		return clk;

	if (!dev) {
		gpiod_put(clk_gpio->gpiod);
		kfree(clk_gpio);
	}

	return clk;
}

/**
 * clk_register_gpio_gate - register a gpio clock gate with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @gpio: gpio number to gate this clock
 * @active_low: true if gpio should be set to 0 to enable clock
 * @flags: clock flags
 */
struct clk *clk_register_gpio_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned gpio, bool active_low,
		unsigned long flags)
{
	return clk_register_gpio(dev, name,
			(parent_name ? &parent_name : NULL),
			(parent_name ? 1 : 0), gpio, active_low, flags,
			&clk_gpio_gate_ops);
}
EXPORT_SYMBOL_GPL(clk_register_gpio_gate);

/**
 * clk_register_gpio_mux - register a gpio clock mux with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_names: names of this clock's parents
 * @num_parents: number of parents listed in @parent_names
 * @gpio: gpio number to gate this clock
 * @active_low: true if gpio should be set to 0 to enable clock
 * @flags: clock flags
 */
struct clk *clk_register_gpio_mux(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents, unsigned gpio,
		bool active_low, unsigned long flags)
{
	if (num_parents != 2) {
		pr_err("mux-clock %s must have 2 parents\n", name);
		return ERR_PTR(-EINVAL);
	}

	return clk_register_gpio(dev, name, parent_names, num_parents,
			gpio, active_low, flags, &clk_gpio_mux_ops);
}
EXPORT_SYMBOL_GPL(clk_register_gpio_mux);

#ifdef CONFIG_OF
/**
 * clk_register_get() has to be delayed, because -EPROBE_DEFER
 * can not be handled properly at of_clk_init() call time.
 */

struct clk_gpio_delayed_register_data {
	const char *gpio_name;
	int num_parents;
	const char **parent_names;
	struct device_node *node;
	struct mutex lock;
	struct clk *clk;
	struct clk *(*clk_register_get)(const char *name,
			const char * const *parent_names, u8 num_parents,
			unsigned gpio, bool active_low);
};

static struct clk *of_clk_gpio_delayed_register_get(
		struct of_phandle_args *clkspec, void *_data)
{
	struct clk_gpio_delayed_register_data *data = _data;
	struct clk *clk;
	int gpio;
	enum of_gpio_flags of_flags;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, data->gpio_name, 0,
			&of_flags);
	if (gpio < 0) {
		mutex_unlock(&data->lock);
		if (gpio == -EPROBE_DEFER)
			pr_debug("%s: %s: GPIOs not yet available, retry later\n",
					data->node->name, __func__);
		else
			pr_err("%s: %s: Can't get '%s' DT property\n",
					data->node->name, __func__,
					data->gpio_name);
		return ERR_PTR(gpio);
	}

	clk = data->clk_register_get(data->node->name, data->parent_names,
			data->num_parents, gpio, of_flags & OF_GPIO_ACTIVE_LOW);
	if (IS_ERR(clk))
		goto out;

	data->clk = clk;
out:
	mutex_unlock(&data->lock);

	return clk;
}

static struct clk *of_clk_gpio_gate_delayed_register_get(const char *name,
		const char * const *parent_names, u8 num_parents,
		unsigned gpio, bool active_low)
{
	return clk_register_gpio_gate(NULL, name, parent_names ?
			parent_names[0] : NULL, gpio, active_low, 0);
}

static struct clk *of_clk_gpio_mux_delayed_register_get(const char *name,
		const char * const *parent_names, u8 num_parents, unsigned gpio,
		bool active_low)
{
	return clk_register_gpio_mux(NULL, name, parent_names, num_parents,
			gpio, active_low, 0);
}

static void __init of_gpio_clk_setup(struct device_node *node,
		const char *gpio_name,
		struct clk *(*clk_register_get)(const char *name,
				const char * const *parent_names,
				u8 num_parents,
				unsigned gpio, bool active_low))
{
	struct clk_gpio_delayed_register_data *data;
	const char **parent_names;
	int i, num_parents;

	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 0)
		num_parents = 0;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	if (num_parents) {
		parent_names = kcalloc(num_parents, sizeof(char *), GFP_KERNEL);
		if (!parent_names) {
			kfree(data);
			return;
		}

		for (i = 0; i < num_parents; i++)
			parent_names[i] = of_clk_get_parent_name(node, i);
	} else {
		parent_names = NULL;
	}

	data->num_parents = num_parents;
	data->parent_names = parent_names;
	data->node = node;
	data->gpio_name = gpio_name;
	data->clk_register_get = clk_register_get;
	mutex_init(&data->lock);

	of_clk_add_provider(node, of_clk_gpio_delayed_register_get, data);
}

static void __init of_gpio_gate_clk_setup(struct device_node *node)
{
	of_gpio_clk_setup(node, "enable-gpios",
		of_clk_gpio_gate_delayed_register_get);
}
CLK_OF_DECLARE(gpio_gate_clk, "gpio-gate-clock", of_gpio_gate_clk_setup);

void __init of_gpio_mux_clk_setup(struct device_node *node)
{
	of_gpio_clk_setup(node, "select-gpios",
		of_clk_gpio_mux_delayed_register_get);
}
CLK_OF_DECLARE(gpio_mux_clk, "gpio-mux-clock", of_gpio_mux_clk_setup);
#endif
