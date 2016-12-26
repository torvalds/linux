/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap.h"

/**
 * clk_is_enabled_regmap - standard is_enabled() for regmap users
 *
 * @hw: clk to operate on
 *
 * Clocks that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their struct clk_regmap and then use
 * this as their is_enabled operation, saving some code.
 */
int clk_is_enabled_regmap(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	unsigned int val;
	int ret;

	ret = regmap_read(rclk->regmap, rclk->enable_reg, &val);
	if (ret != 0)
		return ret;

	if (rclk->enable_is_inverted)
		return (val & rclk->enable_mask) == 0;
	else
		return (val & rclk->enable_mask) != 0;
}
EXPORT_SYMBOL_GPL(clk_is_enabled_regmap);

/**
 * clk_enable_regmap - standard enable() for regmap users
 *
 * @hw: clk to operate on
 *
 * Clocks that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their struct clk_regmap and then use
 * this as their enable() operation, saving some code.
 */
int clk_enable_regmap(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	unsigned int val;

	if (rclk->enable_is_inverted)
		val = 0;
	else
		val = rclk->enable_mask;

	return regmap_update_bits(rclk->regmap, rclk->enable_reg,
				  rclk->enable_mask, val);
}
EXPORT_SYMBOL_GPL(clk_enable_regmap);

/**
 * clk_disable_regmap - standard disable() for regmap users
 *
 * @hw: clk to operate on
 *
 * Clocks that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their struct clk_regmap and then use
 * this as their disable() operation, saving some code.
 */
void clk_disable_regmap(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	unsigned int val;

	if (rclk->enable_is_inverted)
		val = rclk->enable_mask;
	else
		val = 0;

	regmap_update_bits(rclk->regmap, rclk->enable_reg, rclk->enable_mask,
			   val);
}
EXPORT_SYMBOL_GPL(clk_disable_regmap);

/**
 * devm_clk_register_regmap - register a clk_regmap clock
 *
 * @rclk: clk to operate on
 *
 * Clocks that use regmap for their register I/O should register their
 * clk_regmap struct via this function so that the regmap is initialized
 * and so that the clock is registered with the common clock framework.
 */
int devm_clk_register_regmap(struct device *dev, struct clk_regmap *rclk)
{
	if (dev && dev_get_regmap(dev, NULL))
		rclk->regmap = dev_get_regmap(dev, NULL);
	else if (dev && dev->parent)
		rclk->regmap = dev_get_regmap(dev->parent, NULL);

	return devm_clk_hw_register(dev, &rclk->hw);
}
EXPORT_SYMBOL_GPL(devm_clk_register_regmap);
