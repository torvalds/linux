// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 - 2014 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors:
 *    Jyri Sarha <jsarha@ti.com>
 *    Sergej Sawazki <ce3a@gmx.de>
 *
 * Gpio controlled clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

/**
 * DOC: basic gpio gated clock which can be enabled and disabled
 *      with gpio output
 * Traits of this clock:
 * prepare - clk_(un)prepare are functional and control a gpio that can sleep
 * enable - clk_enable and clk_disable are functional & control
 *          non-sleeping gpio
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

/**
 * struct clk_gpio - gpio gated clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @gpiod:	gpio descriptor
 *
 * Clock with a gpio control for enabling and disabling the parent clock
 * or switching between two parents by asserting or deasserting the gpio.
 *
 * Implements .enable, .disable and .is_enabled or
 * .get_parent, .set_parent and .determine_rate depending on which clk_ops
 * is used.
 */
struct clk_gpio {
	struct clk_hw	hw;
	struct gpio_desc *gpiod;
};

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

static const struct clk_ops clk_gpio_gate_ops = {
	.enable = clk_gpio_gate_enable,
	.disable = clk_gpio_gate_disable,
	.is_enabled = clk_gpio_gate_is_enabled,
};

static int clk_sleeping_gpio_gate_prepare(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value_cansleep(clk->gpiod, 1);

	return 0;
}

static void clk_sleeping_gpio_gate_unprepare(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	gpiod_set_value_cansleep(clk->gpiod, 0);
}

static int clk_sleeping_gpio_gate_is_prepared(struct clk_hw *hw)
{
	struct clk_gpio *clk = to_clk_gpio(hw);

	return gpiod_get_value_cansleep(clk->gpiod);
}

static const struct clk_ops clk_sleeping_gpio_gate_ops = {
	.prepare = clk_sleeping_gpio_gate_prepare,
	.unprepare = clk_sleeping_gpio_gate_unprepare,
	.is_prepared = clk_sleeping_gpio_gate_is_prepared,
};

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

static const struct clk_ops clk_gpio_mux_ops = {
	.get_parent = clk_gpio_mux_get_parent,
	.set_parent = clk_gpio_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_hw *clk_register_gpio(struct device *dev, u8 num_parents,
					struct gpio_desc *gpiod,
					const struct clk_ops *clk_gpio_ops)
{
	struct clk_gpio *clk_gpio;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	int err;
	const struct clk_parent_data gpio_parent_data[] = {
		{ .index = 0 },
		{ .index = 1 },
	};

	clk_gpio = devm_kzalloc(dev, sizeof(*clk_gpio),	GFP_KERNEL);
	if (!clk_gpio)
		return ERR_PTR(-ENOMEM);

	init.name = dev->of_node->name;
	init.ops = clk_gpio_ops;
	init.parent_data = gpio_parent_data;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_PARENT;

	clk_gpio->gpiod = gpiod;
	clk_gpio->hw.init = &init;

	hw = &clk_gpio->hw;
	err = devm_clk_hw_register(dev, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

static struct clk_hw *clk_hw_register_gpio_gate(struct device *dev,
						int num_parents,
						struct gpio_desc *gpiod)
{
	const struct clk_ops *ops;

	if (gpiod_cansleep(gpiod))
		ops = &clk_sleeping_gpio_gate_ops;
	else
		ops = &clk_gpio_gate_ops;

	return clk_register_gpio(dev, num_parents, gpiod, ops);
}

static struct clk_hw *clk_hw_register_gpio_mux(struct device *dev,
					       struct gpio_desc *gpiod)
{
	return clk_register_gpio(dev, 2, gpiod, &clk_gpio_mux_ops);
}

static int gpio_clk_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const char *gpio_name;
	unsigned int num_parents;
	struct gpio_desc *gpiod;
	struct clk_hw *hw;
	bool is_mux;

	is_mux = of_device_is_compatible(node, "gpio-mux-clock");

	num_parents = of_clk_get_parent_count(node);
	if (is_mux && num_parents != 2) {
		dev_err(dev, "mux-clock must have 2 parents\n");
		return -EINVAL;
	}

	gpio_name = is_mux ? "select" : "enable";
	gpiod = devm_gpiod_get(dev, gpio_name, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod))
		return dev_err_probe(dev, PTR_ERR(gpiod),
				     "Can't get '%s' named GPIO property\n", gpio_name);

	if (is_mux)
		hw = clk_hw_register_gpio_mux(dev, gpiod);
	else
		hw = clk_hw_register_gpio_gate(dev, num_parents, gpiod);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
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

/**
 * DOC: gated fixed clock, controlled with a gpio output and a regulator
 * Traits of this clock:
 * prepare - clk_prepare and clk_unprepare are function & control regulator
 *           optionally a gpio that can sleep
 * enable - clk_enable and clk_disable are functional & control gpio
 * rate - rate is fixed and set on clock registration
 * parent - fixed clock is a root clock and has no parent
 */

/**
 * struct clk_gated_fixed - Gateable fixed rate clock
 * @clk_gpio:	instance of clk_gpio for gate-gpio
 * @supply:	supply regulator
 * @rate:	fixed rate
 */
struct clk_gated_fixed {
	struct clk_gpio clk_gpio;
	struct regulator *supply;
	unsigned long rate;
};

#define to_clk_gated_fixed(_clk_gpio) container_of(_clk_gpio, struct clk_gated_fixed, clk_gpio)

static unsigned long clk_gated_fixed_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	return to_clk_gated_fixed(to_clk_gpio(hw))->rate;
}

static int clk_gated_fixed_prepare(struct clk_hw *hw)
{
	struct clk_gated_fixed *clk = to_clk_gated_fixed(to_clk_gpio(hw));

	if (!clk->supply)
		return 0;

	return regulator_enable(clk->supply);
}

static void clk_gated_fixed_unprepare(struct clk_hw *hw)
{
	struct clk_gated_fixed *clk = to_clk_gated_fixed(to_clk_gpio(hw));

	if (!clk->supply)
		return;

	regulator_disable(clk->supply);
}

static int clk_gated_fixed_is_prepared(struct clk_hw *hw)
{
	struct clk_gated_fixed *clk = to_clk_gated_fixed(to_clk_gpio(hw));

	if (!clk->supply)
		return true;

	return regulator_is_enabled(clk->supply);
}

/*
 * Fixed gated clock with non-sleeping gpio.
 *
 * Prepare operation turns on the supply regulator
 * and the enable operation switches the enable-gpio.
 */
static const struct clk_ops clk_gated_fixed_ops = {
	.prepare = clk_gated_fixed_prepare,
	.unprepare = clk_gated_fixed_unprepare,
	.is_prepared = clk_gated_fixed_is_prepared,
	.enable = clk_gpio_gate_enable,
	.disable = clk_gpio_gate_disable,
	.is_enabled = clk_gpio_gate_is_enabled,
	.recalc_rate = clk_gated_fixed_recalc_rate,
};

static int clk_sleeping_gated_fixed_prepare(struct clk_hw *hw)
{
	int ret;

	ret = clk_gated_fixed_prepare(hw);
	if (ret)
		return ret;

	ret = clk_sleeping_gpio_gate_prepare(hw);
	if (ret)
		clk_gated_fixed_unprepare(hw);

	return ret;
}

static void clk_sleeping_gated_fixed_unprepare(struct clk_hw *hw)
{
	clk_gated_fixed_unprepare(hw);
	clk_sleeping_gpio_gate_unprepare(hw);
}

/*
 * Fixed gated clock with non-sleeping gpio.
 *
 * Enabling the supply regulator and switching the enable-gpio happens
 * both in the prepare step.
 * is_prepared only needs to check the gpio state, as toggling the
 * gpio is the last step when preparing.
 */
static const struct clk_ops clk_sleeping_gated_fixed_ops = {
	.prepare = clk_sleeping_gated_fixed_prepare,
	.unprepare = clk_sleeping_gated_fixed_unprepare,
	.is_prepared = clk_sleeping_gpio_gate_is_prepared,
	.recalc_rate = clk_gated_fixed_recalc_rate,
};

static int clk_gated_fixed_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_gated_fixed *clk;
	const struct clk_ops *ops;
	const char *clk_name;
	u32 rate;
	int ret;

	clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;

	ret = device_property_read_u32(dev, "clock-frequency", &rate);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clock-frequency\n");
	clk->rate = rate;

	ret = device_property_read_string(dev, "clock-output-names", &clk_name);
	if (ret)
		clk_name = fwnode_get_name(dev->fwnode);

	clk->supply = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(clk->supply)) {
		if (PTR_ERR(clk->supply) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(clk->supply),
					     "Failed to get regulator\n");
		clk->supply = NULL;
	}

	clk->clk_gpio.gpiod = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(clk->clk_gpio.gpiod))
		return dev_err_probe(dev, PTR_ERR(clk->clk_gpio.gpiod),
				     "Failed to get gpio\n");

	if (gpiod_cansleep(clk->clk_gpio.gpiod))
		ops = &clk_sleeping_gated_fixed_ops;
	else
		ops = &clk_gated_fixed_ops;

	clk->clk_gpio.hw.init = CLK_HW_INIT_NO_PARENT(clk_name, ops, 0);

	/* register the clock */
	ret = devm_clk_hw_register(dev, &clk->clk_gpio.hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register clock\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &clk->clk_gpio.hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register clock provider\n");

	return 0;
}

static const struct of_device_id gated_fixed_clk_match_table[] = {
	{ .compatible = "gated-fixed-clock" },
	{ /* sentinel */ }
};

static struct platform_driver gated_fixed_clk_driver = {
	.probe		= clk_gated_fixed_probe,
	.driver		= {
		.name	= "gated-fixed-clk",
		.of_match_table = gated_fixed_clk_match_table,
	},
};
builtin_platform_driver(gated_fixed_clk_driver);
