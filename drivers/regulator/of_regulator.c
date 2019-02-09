/*
 * OF helpers for regulator framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#include "internal.h"

static const char *const regulator_states[PM_SUSPEND_MAX + 1] = {
	[PM_SUSPEND_MEM]	= "regulator-state-mem",
	[PM_SUSPEND_MAX]	= "regulator-state-disk",
};

static void of_get_regulation_constraints(struct device_node *np,
					struct regulator_init_data **init_data,
					const struct regulator_desc *desc)
{
	struct regulation_constraints *constraints = &(*init_data)->constraints;
	struct regulator_state *suspend_state;
	struct device_node *suspend_np;
	unsigned int mode;
	int ret, i, len;
	u32 pval;

	constraints->name = of_get_property(np, "regulator-name", NULL);

	if (!of_property_read_u32(np, "regulator-min-microvolt", &pval))
		constraints->min_uV = pval;

	if (!of_property_read_u32(np, "regulator-max-microvolt", &pval))
		constraints->max_uV = pval;

	/* Voltage change possible? */
	if (constraints->min_uV != constraints->max_uV)
		constraints->valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE;

	/* Do we have a voltage range, if so try to apply it? */
	if (constraints->min_uV && constraints->max_uV)
		constraints->apply_uV = true;

	if (!of_property_read_u32(np, "regulator-microvolt-offset", &pval))
		constraints->uV_offset = pval;
	if (!of_property_read_u32(np, "regulator-min-microamp", &pval))
		constraints->min_uA = pval;
	if (!of_property_read_u32(np, "regulator-max-microamp", &pval))
		constraints->max_uA = pval;

	if (!of_property_read_u32(np, "regulator-input-current-limit-microamp",
				  &pval))
		constraints->ilim_uA = pval;

	/* Current change possible? */
	if (constraints->min_uA != constraints->max_uA)
		constraints->valid_ops_mask |= REGULATOR_CHANGE_CURRENT;

	constraints->boot_on = of_property_read_bool(np, "regulator-boot-on");
	constraints->always_on = of_property_read_bool(np, "regulator-always-on");
	if (!constraints->always_on) /* status change should be possible. */
		constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;

	constraints->pull_down = of_property_read_bool(np, "regulator-pull-down");

	if (of_property_read_bool(np, "regulator-allow-bypass"))
		constraints->valid_ops_mask |= REGULATOR_CHANGE_BYPASS;

	if (of_property_read_bool(np, "regulator-allow-set-load"))
		constraints->valid_ops_mask |= REGULATOR_CHANGE_DRMS;

	ret = of_property_read_u32(np, "regulator-ramp-delay", &pval);
	if (!ret) {
		if (pval)
			constraints->ramp_delay = pval;
		else
			constraints->ramp_disable = true;
	}

	ret = of_property_read_u32(np, "regulator-settling-time-us", &pval);
	if (!ret)
		constraints->settling_time = pval;

	ret = of_property_read_u32(np, "regulator-settling-time-up-us", &pval);
	if (!ret)
		constraints->settling_time_up = pval;
	if (constraints->settling_time_up && constraints->settling_time) {
		pr_warn("%s: ambiguous configuration for settling time, ignoring 'regulator-settling-time-up-us'\n",
			np->name);
		constraints->settling_time_up = 0;
	}

	ret = of_property_read_u32(np, "regulator-settling-time-down-us",
				   &pval);
	if (!ret)
		constraints->settling_time_down = pval;
	if (constraints->settling_time_down && constraints->settling_time) {
		pr_warn("%s: ambiguous configuration for settling time, ignoring 'regulator-settling-time-down-us'\n",
			np->name);
		constraints->settling_time_down = 0;
	}

	ret = of_property_read_u32(np, "regulator-enable-ramp-delay", &pval);
	if (!ret)
		constraints->enable_time = pval;

	constraints->soft_start = of_property_read_bool(np,
					"regulator-soft-start");
	ret = of_property_read_u32(np, "regulator-active-discharge", &pval);
	if (!ret) {
		constraints->active_discharge =
				(pval) ? REGULATOR_ACTIVE_DISCHARGE_ENABLE :
					REGULATOR_ACTIVE_DISCHARGE_DISABLE;
	}

	if (!of_property_read_u32(np, "regulator-initial-mode", &pval)) {
		if (desc && desc->of_map_mode) {
			mode = desc->of_map_mode(pval);
			if (mode == REGULATOR_MODE_INVALID)
				pr_err("%s: invalid mode %u\n", np->name, pval);
			else
				constraints->initial_mode = mode;
		} else {
			pr_warn("%s: mapping for mode %d not defined\n",
				np->name, pval);
		}
	}

	len = of_property_count_elems_of_size(np, "regulator-allowed-modes",
						sizeof(u32));
	if (len > 0) {
		if (desc && desc->of_map_mode) {
			for (i = 0; i < len; i++) {
				ret = of_property_read_u32_index(np,
					"regulator-allowed-modes", i, &pval);
				if (ret) {
					pr_err("%s: couldn't read allowed modes index %d, ret=%d\n",
						np->name, i, ret);
					break;
				}
				mode = desc->of_map_mode(pval);
				if (mode == REGULATOR_MODE_INVALID)
					pr_err("%s: invalid regulator-allowed-modes element %u\n",
						np->name, pval);
				else
					constraints->valid_modes_mask |= mode;
			}
			if (constraints->valid_modes_mask)
				constraints->valid_ops_mask
					|= REGULATOR_CHANGE_MODE;
		} else {
			pr_warn("%s: mode mapping not defined\n", np->name);
		}
	}

	if (!of_property_read_u32(np, "regulator-system-load", &pval))
		constraints->system_load = pval;

	if (!of_property_read_u32(np, "regulator-coupled-max-spread",
				  &pval))
		constraints->max_spread = pval;

	constraints->over_current_protection = of_property_read_bool(np,
					"regulator-over-current-protection");

	for (i = 0; i < ARRAY_SIZE(regulator_states); i++) {
		switch (i) {
		case PM_SUSPEND_MEM:
			suspend_state = &constraints->state_mem;
			break;
		case PM_SUSPEND_MAX:
			suspend_state = &constraints->state_disk;
			break;
		case PM_SUSPEND_ON:
		case PM_SUSPEND_TO_IDLE:
		case PM_SUSPEND_STANDBY:
		default:
			continue;
		}

		suspend_np = of_get_child_by_name(np, regulator_states[i]);
		if (!suspend_np || !suspend_state)
			continue;

		if (!of_property_read_u32(suspend_np, "regulator-mode",
					  &pval)) {
			if (desc && desc->of_map_mode) {
				mode = desc->of_map_mode(pval);
				if (mode == REGULATOR_MODE_INVALID)
					pr_err("%s: invalid mode %u\n",
					       np->name, pval);
				else
					suspend_state->mode = mode;
			} else {
				pr_warn("%s: mapping for mode %d not defined\n",
					np->name, pval);
			}
		}

		if (of_property_read_bool(suspend_np,
					"regulator-on-in-suspend"))
			suspend_state->enabled = ENABLE_IN_SUSPEND;
		else if (of_property_read_bool(suspend_np,
					"regulator-off-in-suspend"))
			suspend_state->enabled = DISABLE_IN_SUSPEND;

		if (!of_property_read_u32(np, "regulator-suspend-min-microvolt",
					  &pval))
			suspend_state->min_uV = pval;

		if (!of_property_read_u32(np, "regulator-suspend-max-microvolt",
					  &pval))
			suspend_state->max_uV = pval;

		if (!of_property_read_u32(suspend_np,
					"regulator-suspend-microvolt", &pval))
			suspend_state->uV = pval;
		else /* otherwise use min_uV as default suspend voltage */
			suspend_state->uV = suspend_state->min_uV;

		if (of_property_read_bool(suspend_np,
					"regulator-changeable-in-suspend"))
			suspend_state->changeable = true;

		if (i == PM_SUSPEND_MEM)
			constraints->initial_state = PM_SUSPEND_MEM;

		of_node_put(suspend_np);
		suspend_state = NULL;
		suspend_np = NULL;
	}
}

/**
 * of_get_regulator_init_data - extract regulator_init_data structure info
 * @dev: device requesting for regulator_init_data
 * @node: regulator device node
 * @desc: regulator description
 *
 * Populates regulator_init_data structure by extracting data from device
 * tree node, returns a pointer to the populated struture or NULL if memory
 * alloc fails.
 */
struct regulator_init_data *of_get_regulator_init_data(struct device *dev,
					  struct device_node *node,
					  const struct regulator_desc *desc)
{
	struct regulator_init_data *init_data;

	if (!node)
		return NULL;

	init_data = devm_kzalloc(dev, sizeof(*init_data), GFP_KERNEL);
	if (!init_data)
		return NULL; /* Out of memory? */

	of_get_regulation_constraints(node, &init_data, desc);
	return init_data;
}
EXPORT_SYMBOL_GPL(of_get_regulator_init_data);

struct devm_of_regulator_matches {
	struct of_regulator_match *matches;
	unsigned int num_matches;
};

static void devm_of_regulator_put_matches(struct device *dev, void *res)
{
	struct devm_of_regulator_matches *devm_matches = res;
	int i;

	for (i = 0; i < devm_matches->num_matches; i++)
		of_node_put(devm_matches->matches[i].of_node);
}

/**
 * of_regulator_match - extract multiple regulator init data from device tree.
 * @dev: device requesting the data
 * @node: parent device node of the regulators
 * @matches: match table for the regulators
 * @num_matches: number of entries in match table
 *
 * This function uses a match table specified by the regulator driver to
 * parse regulator init data from the device tree. @node is expected to
 * contain a set of child nodes, each providing the init data for one
 * regulator. The data parsed from a child node will be matched to a regulator
 * based on either the deprecated property regulator-compatible if present,
 * or otherwise the child node's name. Note that the match table is modified
 * in place and an additional of_node reference is taken for each matched
 * regulator.
 *
 * Returns the number of matches found or a negative error code on failure.
 */
int of_regulator_match(struct device *dev, struct device_node *node,
		       struct of_regulator_match *matches,
		       unsigned int num_matches)
{
	unsigned int count = 0;
	unsigned int i;
	const char *name;
	struct device_node *child;
	struct devm_of_regulator_matches *devm_matches;

	if (!dev || !node)
		return -EINVAL;

	devm_matches = devres_alloc(devm_of_regulator_put_matches,
				    sizeof(struct devm_of_regulator_matches),
				    GFP_KERNEL);
	if (!devm_matches)
		return -ENOMEM;

	devm_matches->matches = matches;
	devm_matches->num_matches = num_matches;

	devres_add(dev, devm_matches);

	for (i = 0; i < num_matches; i++) {
		struct of_regulator_match *match = &matches[i];
		match->init_data = NULL;
		match->of_node = NULL;
	}

	for_each_child_of_node(node, child) {
		name = of_get_property(child,
					"regulator-compatible", NULL);
		if (!name)
			name = child->name;
		for (i = 0; i < num_matches; i++) {
			struct of_regulator_match *match = &matches[i];
			if (match->of_node)
				continue;

			if (strcmp(match->name, name))
				continue;

			match->init_data =
				of_get_regulator_init_data(dev, child,
							   match->desc);
			if (!match->init_data) {
				dev_err(dev,
					"failed to parse DT for regulator %s\n",
					child->name);
				of_node_put(child);
				return -EINVAL;
			}
			match->of_node = of_node_get(child);
			count++;
			break;
		}
	}

	return count;
}
EXPORT_SYMBOL_GPL(of_regulator_match);

struct regulator_init_data *regulator_of_get_init_data(struct device *dev,
					    const struct regulator_desc *desc,
					    struct regulator_config *config,
					    struct device_node **node)
{
	struct device_node *search, *child;
	struct regulator_init_data *init_data = NULL;
	const char *name;

	if (!dev->of_node || !desc->of_match)
		return NULL;

	if (desc->regulators_node)
		search = of_get_child_by_name(dev->of_node,
					      desc->regulators_node);
	else
		search = of_node_get(dev->of_node);

	if (!search) {
		dev_dbg(dev, "Failed to find regulator container node '%s'\n",
			desc->regulators_node);
		return NULL;
	}

	for_each_available_child_of_node(search, child) {
		name = of_get_property(child, "regulator-compatible", NULL);
		if (!name)
			name = child->name;

		if (strcmp(desc->of_match, name))
			continue;

		init_data = of_get_regulator_init_data(dev, child, desc);
		if (!init_data) {
			dev_err(dev,
				"failed to parse DT for regulator %s\n",
				child->name);
			break;
		}

		if (desc->of_parse_cb) {
			if (desc->of_parse_cb(child, desc, config)) {
				dev_err(dev,
					"driver callback failed to parse DT for regulator %s\n",
					child->name);
				init_data = NULL;
				break;
			}
		}

		of_node_get(child);
		*node = child;
		break;
	}

	of_node_put(search);

	return init_data;
}

static int of_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

struct regulator_dev *of_find_regulator_by_node(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device(&regulator_class, NULL, np, of_node_match);

	return dev ? dev_to_rdev(dev) : NULL;
}

/*
 * Returns number of regulators coupled with rdev.
 */
int of_get_n_coupled(struct regulator_dev *rdev)
{
	struct device_node *node = rdev->dev.of_node;
	int n_phandles;

	n_phandles = of_count_phandle_with_args(node,
						"regulator-coupled-with",
						NULL);

	return (n_phandles > 0) ? n_phandles : 0;
}

/* Looks for "to_find" device_node in src's "regulator-coupled-with" property */
static bool of_coupling_find_node(struct device_node *src,
				  struct device_node *to_find)
{
	int n_phandles, i;
	bool found = false;

	n_phandles = of_count_phandle_with_args(src,
						"regulator-coupled-with",
						NULL);

	for (i = 0; i < n_phandles; i++) {
		struct device_node *tmp = of_parse_phandle(src,
					   "regulator-coupled-with", i);

		if (!tmp)
			break;

		/* found */
		if (tmp == to_find)
			found = true;

		of_node_put(tmp);

		if (found)
			break;
	}

	return found;
}

/**
 * of_check_coupling_data - Parse rdev's coupling properties and check data
 *			    consistency
 * @rdev - pointer to regulator_dev whose data is checked
 *
 * Function checks if all the following conditions are met:
 * - rdev's max_spread is greater than 0
 * - all coupled regulators have the same max_spread
 * - all coupled regulators have the same number of regulator_dev phandles
 * - all regulators are linked to each other
 *
 * Returns true if all conditions are met.
 */
bool of_check_coupling_data(struct regulator_dev *rdev)
{
	int max_spread = rdev->constraints->max_spread;
	struct device_node *node = rdev->dev.of_node;
	int n_phandles = of_get_n_coupled(rdev);
	struct device_node *c_node;
	int i;
	bool ret = true;

	if (max_spread <= 0) {
		dev_err(&rdev->dev, "max_spread value invalid\n");
		return false;
	}

	/* iterate over rdev's phandles */
	for (i = 0; i < n_phandles; i++) {
		int c_max_spread, c_n_phandles;

		c_node = of_parse_phandle(node,
					  "regulator-coupled-with", i);

		if (!c_node)
			ret = false;

		c_n_phandles = of_count_phandle_with_args(c_node,
							  "regulator-coupled-with",
							  NULL);

		if (c_n_phandles != n_phandles) {
			dev_err(&rdev->dev, "number of couped reg phandles mismatch\n");
			ret = false;
			goto clean;
		}

		if (of_property_read_u32(c_node, "regulator-coupled-max-spread",
					 &c_max_spread)) {
			ret = false;
			goto clean;
		}

		if (c_max_spread != max_spread) {
			dev_err(&rdev->dev,
				"coupled regulators max_spread mismatch\n");
			ret = false;
			goto clean;
		}

		if (!of_coupling_find_node(c_node, node)) {
			dev_err(&rdev->dev, "missing 2-way linking for coupled regulators\n");
			ret = false;
		}

clean:
		of_node_put(c_node);
		if (!ret)
			break;
	}

	return ret;
}

/**
 * of_parse_coupled regulator - Get regulator_dev pointer from rdev's property
 * @rdev: Pointer to regulator_dev, whose DTS is used as a source to parse
 *	  "regulator-coupled-with" property
 * @index: Index in phandles array
 *
 * Returns the regulator_dev pointer parsed from DTS. If it has not been yet
 * registered, returns NULL
 */
struct regulator_dev *of_parse_coupled_regulator(struct regulator_dev *rdev,
						 int index)
{
	struct device_node *node = rdev->dev.of_node;
	struct device_node *c_node;
	struct regulator_dev *c_rdev;

	c_node = of_parse_phandle(node, "regulator-coupled-with", index);
	if (!c_node)
		return NULL;

	c_rdev = of_find_regulator_by_node(c_node);

	of_node_put(c_node);

	return c_rdev;
}
