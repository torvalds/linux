// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014, 2019-2021 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/export.h>
#include <linux/pm_runtime.h>

#include "clk-regmap.h"
#include "clk-debug.h"

static LIST_HEAD(clk_regmap_list);
static DEFINE_SPINLOCK(clk_regmap_lock);

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
 * clk_pre_change_regmap() - standard pre_change call back for regmap clks
 *
 * @hw: clk to operate on
 * @cur_rate: current rate of the clk
 * @new_rate: new rate about to be set for the clk
 *
 * Finds new vdd level corresponding to new rate and update vdd_level
 * cache if new_vdd_level is greater than vdd_level. If clock is prepared
 * then update existing vdd vote.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_pre_change_regmap(struct clk_hw *hw, unsigned long cur_rate,
			unsigned long new_rate)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	int vdd_level = rclk->vdd_data.vdd_level;
	int new_vdd_level;
	int ret = 0;

	if (!rclk->vdd_data.rate_max)
		return 0;

	new_vdd_level = clk_find_vdd_level(hw, &rclk->vdd_data, new_rate);
	if (new_vdd_level < 0)
		return new_vdd_level;

	if (new_vdd_level <= vdd_level)
		return 0;

	if (clk_hw_is_prepared(hw)) {
		ret = clk_vote_vdd_level(&rclk->vdd_data, new_vdd_level);
		if (ret)
			return ret;

		clk_unvote_vdd_level(&rclk->vdd_data, vdd_level);
	}

	rclk->vdd_data.vdd_level = new_vdd_level;

	return 0;
}
EXPORT_SYMBOL(clk_pre_change_regmap);

/**
 * clk_post_change_regmap() - standard post_change call back for regmap clks
 *
 * @hw: clk to operate on
 * @old_rate: previous rate of the clk
 * @cur_rate: current rate of the recently changed clk
 *
 * Finds new vdd level corresponding to current rate and update vdd_level
 * cache if cur_vdd_level is less than vdd_level. If clock is prepared
 * then update existing vdd vote.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_post_change_regmap(struct clk_hw *hw, unsigned long old_rate,
			unsigned long cur_rate)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	int vdd_level = rclk->vdd_data.vdd_level;
	int cur_vdd_level;
	int ret = 0;

	if (!rclk->vdd_data.rate_max)
		return 0;

	cur_vdd_level = clk_find_vdd_level(hw, &rclk->vdd_data, cur_rate);
	if (cur_vdd_level < 0)
		return cur_vdd_level;

	if (cur_vdd_level >= vdd_level)
		return 0;

	if (clk_hw_is_prepared(hw)) {
		ret = clk_vote_vdd_level(&rclk->vdd_data, cur_vdd_level);
		if (ret)
			return ret;

		clk_unvote_vdd_level(&rclk->vdd_data, vdd_level);
	}

	rclk->vdd_data.vdd_level = cur_vdd_level;

	return 0;
}
EXPORT_SYMBOL(clk_post_change_regmap);

/**
 * clk_prepare_regmap() - standard prepare call back for regmap clks
 *
 * @hw: clk to operate on
 *
 * Prepare the clock by updating the vdd_level to level required by
 * the current rate of the clock if it hasn't been initialized before.
 * Vdd_level and level required by current clock rate mismatches can
 * occur due to error cases and upon initial clock registration
 * if the clock becomes an orphan and is later reparented.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_prepare_regmap(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	unsigned long rate = clk_hw_get_rate(hw);
	int vdd_level;

	if (!rclk->vdd_data.rate_max)
		return 0;

	vdd_level = clk_find_vdd_level(hw, &rclk->vdd_data, rate);
	if (vdd_level < 0)
		return vdd_level;

	if (rclk->vdd_data.vdd_level == 0)
		rclk->vdd_data.vdd_level = vdd_level;

	WARN(vdd_level > rclk->vdd_data.vdd_level,
	     "%s level:%d > vdd_level:%d\n", clk_hw_get_name(hw),
	     vdd_level, rclk->vdd_data.vdd_level);

	return clk_vote_vdd_level(&rclk->vdd_data, rclk->vdd_data.vdd_level);
}
EXPORT_SYMBOL(clk_prepare_regmap);

/**
 * clk_prepare_regmap() - standard prepare call back for regmap clks
 *
 * @hw: clk to operate on
 *
 * Unprepare the clock by removing the outstanding vdd_level vote.
 *
 */
void clk_unprepare_regmap(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->vdd_data.rate_max)
		return;

	clk_unvote_vdd_level(&rclk->vdd_data, rclk->vdd_data.vdd_level);
}
EXPORT_SYMBOL(clk_unprepare_regmap);

/**
 * clk_is_regmap_clk - Checks if clk is a regmap clk
 *
 * @hw: clk to check on
 *
 * Iterate over maintained clk regmap list to know
 * if concern clk is regmap
 *
 * Returns true on success, false otherwise.
 */
bool clk_is_regmap_clk(struct clk_hw *hw)
{
	struct clk_regmap *rclk;
	bool is_regmap_clk = false;

	if (hw) {
		spin_lock(&clk_regmap_lock);
		list_for_each_entry(rclk, &clk_regmap_list, list_node) {
			if (&rclk->hw == hw) {
				is_regmap_clk = true;
				break;
			}
		}
		spin_unlock(&clk_regmap_lock);
	}

	return is_regmap_clk;
}
EXPORT_SYMBOL(clk_is_regmap_clk);

/**
 * devm_clk_register_regmap - register a clk_regmap clock
 *
 * @dev: reference to the caller's device
 * @rclk: clk to operate on
 *
 * Clocks that use regmap for their register I/O should register their
 * clk_regmap struct via this function so that the regmap is initialized
 * and so that the clock is registered with the common clock framework.
 * Also maintain clk-regmap clks list for providers use.
 */
int devm_clk_register_regmap(struct device *dev, struct clk_regmap *rclk)
{
	const struct clk_ops *ops;
	int ret;

	rclk->dev = dev;

	if (dev && dev_get_regmap(dev, NULL))
		rclk->regmap = dev_get_regmap(dev, NULL);
	else if (dev && dev->parent)
		rclk->regmap = dev_get_regmap(dev->parent, NULL);

	if (rclk->flags & QCOM_CLK_IS_CRITICAL || rclk->flags & QCOM_CLK_BOOT_CRITICAL) {
		ops = rclk->hw.init->ops;
		if (ops && ops->enable)
			ops->enable(&rclk->hw);

		if (rclk->flags & QCOM_CLK_IS_CRITICAL)
			return 0;
	}

	ret = devm_clk_hw_register(dev, &rclk->hw);
	if (!ret) {
		spin_lock(&clk_regmap_lock);
		list_add(&rclk->list_node, &clk_regmap_list);
		spin_unlock(&clk_regmap_lock);

		ret = clk_hw_debug_register(dev, &rclk->hw);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_clk_register_regmap);

/**
 * devm_clk_regmap_list_node - Add a clk-regmap clock list for providers
 *
 * @rclk: clk to operate on
 *
 * Maintain clk-regmap clks list for providers use.
 */
void devm_clk_regmap_list_node(struct device *dev, struct clk_regmap *rclk)
{
	list_add(&rclk->list_node, &clk_regmap_list);
}
EXPORT_SYMBOL_GPL(devm_clk_regmap_list_node);

int clk_runtime_get_regmap(struct clk_regmap *rclk)
{
	int ret;

	if (pm_runtime_enabled(rclk->dev)) {
		ret = pm_runtime_get_sync(rclk->dev);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(clk_runtime_get_regmap);

void clk_runtime_put_regmap(struct clk_regmap *rclk)
{
	if (pm_runtime_enabled(rclk->dev))
		pm_runtime_put_sync(rclk->dev);
}
EXPORT_SYMBOL(clk_runtime_put_regmap);

void clk_restore_critical_clocks(struct device *dev)
{
	struct qcom_cc_desc *desc = dev_get_drvdata(dev);
	struct regmap *regmap = dev_get_regmap(dev, NULL);
	struct critical_clk_offset *cclks = desc->critical_clk_en;
	int i;

	if (!regmap)
		return;

	for (i = 0; i < desc->num_critical_clk; i++)
		regmap_update_bits(regmap, cclks[i].offset, cclks[i].mask,
					 cclks[i].mask);
}
EXPORT_SYMBOL(clk_restore_critical_clocks);
