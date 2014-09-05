/*
 * Copyright (C) 2013 - 2014 Texas Instruments Incorporated - http://www.ti.com
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gpio gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
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
 * clk_register_gpio - register a gpip clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @gpiod: gpio descriptor to gate this clock
 */
struct clk *clk_register_gpio_gate(struct device *dev, const char *name,
		const char *parent_name, struct gpio_desc *gpiod,
		unsigned long flags)
{
	struct clk_gpio *clk_gpio = NULL;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct clk_init_data init = { NULL };
	unsigned long gpio_flags;
	int err;

	if (gpiod_is_active_low(gpiod))
		gpio_flags = GPIOF_OUT_INIT_HIGH;
	else
		gpio_flags = GPIOF_OUT_INIT_LOW;

	if (dev)
		err = devm_gpio_request_one(dev, desc_to_gpio(gpiod),
					    gpio_flags, name);
	else
		err = gpio_request_one(desc_to_gpio(gpiod), gpio_flags, name);

	if (err) {
		pr_err("%s: %s: Error requesting clock control gpio %u\n",
		       __func__, name, desc_to_gpio(gpiod));
		return ERR_PTR(err);
	}

	if (dev)
		clk_gpio = devm_kzalloc(dev, sizeof(struct clk_gpio),
					GFP_KERNEL);
	else
		clk_gpio = kzalloc(sizeof(struct clk_gpio), GFP_KERNEL);

	if (!clk_gpio) {
		clk = ERR_PTR(-ENOMEM);
		goto clk_register_gpio_gate_err;
	}

	init.name = name;
	init.ops = &clk_gpio_gate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	clk_gpio->gpiod = gpiod;
	clk_gpio->hw.init = &init;

	clk = clk_register(dev, &clk_gpio->hw);

	if (!IS_ERR(clk))
		return clk;

	if (!dev)
		kfree(clk_gpio);

clk_register_gpio_gate_err:
	gpiod_put(gpiod);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio_gate);

#ifdef CONFIG_OF
/**
 * The clk_register_gpio_gate has to be delayed, because the EPROBE_DEFER
 * can not be handled properly at of_clk_init() call time.
 */

struct clk_gpio_gate_delayed_register_data {
	struct device_node *node;
	struct mutex lock;
	struct clk *clk;
};

static struct clk *of_clk_gpio_gate_delayed_register_get(
		struct of_phandle_args *clkspec,
		void *_data)
{
	struct clk_gpio_gate_delayed_register_data *data = _data;
	struct clk *clk;
	const char *clk_name = data->node->name;
	const char *parent_name;
	struct gpio_desc *gpiod;
	int gpio;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, "enable-gpios", 0, NULL);
	if (gpio < 0) {
		mutex_unlock(&data->lock);
		if (gpio != -EPROBE_DEFER)
			pr_err("%s: %s: Can't get 'enable-gpios' DT property\n",
			       __func__, clk_name);
		return ERR_PTR(gpio);
	}
	gpiod = gpio_to_desc(gpio);

	parent_name = of_clk_get_parent_name(data->node, 0);

	clk = clk_register_gpio_gate(NULL, clk_name, parent_name, gpiod, 0);
	if (IS_ERR(clk)) {
		mutex_unlock(&data->lock);
		return clk;
	}

	data->clk = clk;
	mutex_unlock(&data->lock);

	return clk;
}

/**
 * of_gpio_gate_clk_setup() - Setup function for gpio controlled clock
 */
void __init of_gpio_gate_clk_setup(struct device_node *node)
{
	struct clk_gpio_gate_delayed_register_data *data;

	data = kzalloc(sizeof(struct clk_gpio_gate_delayed_register_data),
		       GFP_KERNEL);
	if (!data)
		return;

	data->node = node;
	mutex_init(&data->lock);

	of_clk_add_provider(node, of_clk_gpio_gate_delayed_register_get, data);
}
EXPORT_SYMBOL_GPL(of_gpio_gate_clk_setup);
CLK_OF_DECLARE(gpio_gate_clk, "gpio-gate-clock", of_gpio_gate_clk_setup);
#endif
