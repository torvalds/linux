/*
 * helpers.c  --  Voltage/Current Regulator framework helper functions.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>

/**
 * regulator_is_enabled_regmap - standard is_enabled() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their is_enabled operation, saving some code.
 */
int regulator_is_enabled_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->enable_mask;

	if (rdev->desc->enable_is_inverted) {
		if (rdev->desc->enable_val)
			return val != rdev->desc->enable_val;
		return val == 0;
	} else {
		if (rdev->desc->enable_val)
			return val == rdev->desc->enable_val;
		return val != 0;
	}
}
EXPORT_SYMBOL_GPL(regulator_is_enabled_regmap);

/**
 * regulator_enable_regmap - standard enable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their enable() operation, saving some code.
 */
int regulator_enable_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->disable_val;
	} else {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_enable_regmap);

/**
 * regulator_disable_regmap - standard disable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their disable() operation, saving some code.
 */
int regulator_disable_regmap(struct regulator_dev *rdev)
{
	unsigned int val;

	if (rdev->desc->enable_is_inverted) {
		val = rdev->desc->enable_val;
		if (!val)
			val = rdev->desc->enable_mask;
	} else {
		val = rdev->desc->disable_val;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_disable_regmap);

/**
 * regulator_get_voltage_sel_regmap - standard get_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their get_voltage_vsel operation, saving some code.
 */
int regulator_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage_sel_regmap);

/**
 * regulator_set_voltage_sel_regmap - standard set_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their set_voltage_vsel operation, saving some code.
 */
int regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
	int ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  rdev->desc->vsel_mask, sel);
	if (ret)
		return ret;

	if (rdev->desc->apply_bit)
		ret = regmap_update_bits(rdev->regmap, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_sel_regmap);

/**
 * regulator_map_voltage_iterate - map_voltage() based on list_voltage()
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers implementing set_voltage_sel() and list_voltage() can use
 * this as their map_voltage() operation.  It will find a suitable
 * voltage by calling list_voltage() until it gets something in bounds
 * for the requested voltages.
 */
int regulator_map_voltage_iterate(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	int best_val = INT_MAX;
	int selector = 0;
	int i, ret;

	/* Find the smallest voltage that falls within the specified
	 * range.
	 */
	for (i = 0; i < rdev->desc->n_voltages; i++) {
		ret = rdev->desc->ops->list_voltage(rdev, i);
		if (ret < 0)
			continue;

		if (ret < best_val && ret >= min_uV && ret <= max_uV) {
			best_val = ret;
			selector = i;
		}
	}

	if (best_val != INT_MAX)
		return selector;
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_iterate);

/**
 * regulator_map_voltage_ascend - map_voltage() for ascendant voltage list
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers that have ascendant voltage list can use this as their
 * map_voltage() operation.
 */
int regulator_map_voltage_ascend(struct regulator_dev *rdev,
				 int min_uV, int max_uV)
{
	int i, ret;

	for (i = 0; i < rdev->desc->n_voltages; i++) {
		ret = rdev->desc->ops->list_voltage(rdev, i);
		if (ret < 0)
			continue;

		if (ret > max_uV)
			break;

		if (ret >= min_uV && ret <= max_uV)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_ascend);

/**
 * regulator_map_voltage_linear - map_voltage() for simple linear mappings
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing min_uV and uV_step in their regulator_desc can
 * use this as their map_voltage() operation.
 */
int regulator_map_voltage_linear(struct regulator_dev *rdev,
				 int min_uV, int max_uV)
{
	int ret, voltage;

	/* Allow uV_step to be 0 for fixed voltage */
	if (rdev->desc->n_voltages == 1 && rdev->desc->uV_step == 0) {
		if (min_uV <= rdev->desc->min_uV && rdev->desc->min_uV <= max_uV)
			return 0;
		else
			return -EINVAL;
	}

	if (!rdev->desc->uV_step) {
		BUG_ON(!rdev->desc->uV_step);
		return -EINVAL;
	}

	if (min_uV < rdev->desc->min_uV)
		min_uV = rdev->desc->min_uV;

	ret = DIV_ROUND_UP(min_uV - rdev->desc->min_uV, rdev->desc->uV_step);
	if (ret < 0)
		return ret;

	ret += rdev->desc->linear_min_sel;

	/* Map back into a voltage to verify we're still in bounds */
	voltage = rdev->desc->ops->list_voltage(rdev, ret);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_linear);

/**
 * regulator_map_voltage_linear_range - map_voltage() for multiple linear ranges
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing linear_ranges in their descriptor can use this as
 * their map_voltage() callback.
 */
int regulator_map_voltage_linear_range(struct regulator_dev *rdev,
				       int min_uV, int max_uV)
{
	const struct regulator_linear_range *range;
	int ret = -EINVAL;
	int voltage, i;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		int linear_max_uV;

		range = &rdev->desc->linear_ranges[i];
		linear_max_uV = range->min_uV +
			(range->max_sel - range->min_sel) * range->uV_step;

		if (!(min_uV <= linear_max_uV && max_uV >= range->min_uV))
			continue;

		if (min_uV <= range->min_uV)
			min_uV = range->min_uV;

		/* range->uV_step == 0 means fixed voltage range */
		if (range->uV_step == 0) {
			ret = 0;
		} else {
			ret = DIV_ROUND_UP(min_uV - range->min_uV,
					   range->uV_step);
			if (ret < 0)
				return ret;
		}

		ret += range->min_sel;

		break;
	}

	if (i == rdev->desc->n_linear_ranges)
		return -EINVAL;

	/* Map back into a voltage to verify we're still in bounds */
	voltage = rdev->desc->ops->list_voltage(rdev, ret);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_linear_range);

/**
 * regulator_list_voltage_linear - List voltages with simple calculation
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with a simple linear mapping between voltages and
 * selectors can set min_uV and uV_step in the regulator descriptor
 * and then use this function as their list_voltage() operation,
 */
int regulator_list_voltage_linear(struct regulator_dev *rdev,
				  unsigned int selector)
{
	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;
	if (selector < rdev->desc->linear_min_sel)
		return 0;

	selector -= rdev->desc->linear_min_sel;

	return rdev->desc->min_uV + (rdev->desc->uV_step * selector);
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_linear);

/**
 * regulator_list_voltage_linear_range - List voltages for linear ranges
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with a series of simple linear mappings between voltages
 * and selectors can set linear_ranges in the regulator descriptor and
 * then use this function as their list_voltage() operation,
 */
int regulator_list_voltage_linear_range(struct regulator_dev *rdev,
					unsigned int selector)
{
	const struct regulator_linear_range *range;
	int i;

	if (!rdev->desc->n_linear_ranges) {
		BUG_ON(!rdev->desc->n_linear_ranges);
		return -EINVAL;
	}

	for (i = 0; i < rdev->desc->n_linear_ranges; i++) {
		range = &rdev->desc->linear_ranges[i];

		if (!(selector >= range->min_sel &&
		      selector <= range->max_sel))
			continue;

		selector -= range->min_sel;

		return range->min_uV + (range->uV_step * selector);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_linear_range);

/**
 * regulator_list_voltage_table - List voltages with table based mapping
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with table based mapping between voltages and
 * selectors can set volt_table in the regulator descriptor
 * and then use this function as their list_voltage() operation.
 */
int regulator_list_voltage_table(struct regulator_dev *rdev,
				 unsigned int selector)
{
	if (!rdev->desc->volt_table) {
		BUG_ON(!rdev->desc->volt_table);
		return -EINVAL;
	}

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return rdev->desc->volt_table[selector];
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_table);

/**
 * regulator_set_bypass_regmap - Default set_bypass() using regmap
 *
 * @rdev: device to operate on.
 * @enable: state to set.
 */
int regulator_set_bypass_regmap(struct regulator_dev *rdev, bool enable)
{
	unsigned int val;

	if (enable) {
		val = rdev->desc->bypass_val_on;
		if (!val)
			val = rdev->desc->bypass_mask;
	} else {
		val = rdev->desc->bypass_val_off;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->bypass_reg,
				  rdev->desc->bypass_mask, val);
}
EXPORT_SYMBOL_GPL(regulator_set_bypass_regmap);

/**
 * regulator_get_bypass_regmap - Default get_bypass() using regmap
 *
 * @rdev: device to operate on.
 * @enable: current state.
 */
int regulator_get_bypass_regmap(struct regulator_dev *rdev, bool *enable)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->bypass_reg, &val);
	if (ret != 0)
		return ret;

	*enable = val & rdev->desc->bypass_mask;

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_get_bypass_regmap);
