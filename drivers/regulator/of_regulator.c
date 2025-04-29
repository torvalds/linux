// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OF helpers for regulator framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#include "internal.h"

static const char *const regulator_states[PM_SUSPEND_MAX + 1] = {
	[PM_SUSPEND_STANDBY]	= "regulator-state-standby",
	[PM_SUSPEND_MEM]	= "regulator-state-mem",
	[PM_SUSPEND_MAX]	= "regulator-state-disk",
};

static void fill_limit(int *limit, int val)
{
	if (val)
		if (val == 1)
			*limit = REGULATOR_NOTIF_LIMIT_ENABLE;
		else
			*limit = val;
	else
		*limit = REGULATOR_NOTIF_LIMIT_DISABLE;
}

static void of_get_regulator_prot_limits(struct device_node *np,
				struct regulation_constraints *constraints)
{
	u32 pval;
	int i;
	static const char *const props[] = {
		"regulator-oc-%s-microamp",
		"regulator-ov-%s-microvolt",
		"regulator-temp-%s-kelvin",
		"regulator-uv-%s-microvolt",
	};
	struct notification_limit *limits[] = {
		&constraints->over_curr_limits,
		&constraints->over_voltage_limits,
		&constraints->temp_limits,
		&constraints->under_voltage_limits,
	};
	bool set[4] = {0};

	/* Protection limits: */
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		char prop[255];
		bool found;
		int j;
		static const char *const lvl[] = {
			"protection", "error", "warn"
		};
		int *l[] = {
			&limits[i]->prot, &limits[i]->err, &limits[i]->warn,
		};

		for (j = 0; j < ARRAY_SIZE(lvl); j++) {
			snprintf(prop, 255, props[i], lvl[j]);
			found = !of_property_read_u32(np, prop, &pval);
			if (found)
				fill_limit(l[j], pval);
			set[i] |= found;
		}
	}
	constraints->over_current_detection = set[0];
	constraints->over_voltage_detection = set[1];
	constraints->over_temp_detection = set[2];
	constraints->under_voltage_detection = set[3];
}

static int of_get_regulation_constraints(struct device *dev,
					struct device_node *np,
					struct regulator_init_data **init_data,
					const struct regulator_desc *desc)
{
	struct regulation_constraints *constraints = &(*init_data)->constraints;
	struct regulator_state *suspend_state;
	struct device_node *suspend_np;
	unsigned int mode;
	int ret, i, len;
	int n_phandles;
	u32 pval;

	n_phandles = of_count_phandle_with_args(np, "regulator-coupled-with",
						NULL);
	n_phandles = max(n_phandles, 0);

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

	if (!of_property_read_u32(np, "regulator-power-budget-milliwatt", &pval))
		constraints->pw_budget_mW = pval;

	constraints->boot_on = of_property_read_bool(np, "regulator-boot-on");
	constraints->always_on = of_property_read_bool(np, "regulator-always-on");
	if (!constraints->always_on) /* status change should be possible. */
		constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;

	constraints->pull_down = of_property_read_bool(np, "regulator-pull-down");
	constraints->system_critical = of_property_read_bool(np,
						"system-critical-regulator");

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
		pr_warn("%pOFn: ambiguous configuration for settling time, ignoring 'regulator-settling-time-up-us'\n",
			np);
		constraints->settling_time_up = 0;
	}

	ret = of_property_read_u32(np, "regulator-settling-time-down-us",
				   &pval);
	if (!ret)
		constraints->settling_time_down = pval;
	if (constraints->settling_time_down && constraints->settling_time) {
		pr_warn("%pOFn: ambiguous configuration for settling time, ignoring 'regulator-settling-time-down-us'\n",
			np);
		constraints->settling_time_down = 0;
	}

	ret = of_property_read_u32(np, "regulator-enable-ramp-delay", &pval);
	if (!ret)
		constraints->enable_time = pval;

	ret = of_property_read_u32(np, "regulator-uv-less-critical-window-ms", &pval);
	if (!ret)
		constraints->uv_less_critical_window_ms = pval;
	else
		constraints->uv_less_critical_window_ms =
				REGULATOR_DEF_UV_LESS_CRITICAL_WINDOW_MS;

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
				pr_err("%pOFn: invalid mode %u\n", np, pval);
			else
				constraints->initial_mode = mode;
		} else {
			pr_warn("%pOFn: mapping for mode %d not defined\n",
				np, pval);
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
					pr_err("%pOFn: couldn't read allowed modes index %d, ret=%d\n",
						np, i, ret);
					break;
				}
				mode = desc->of_map_mode(pval);
				if (mode == REGULATOR_MODE_INVALID)
					pr_err("%pOFn: invalid regulator-allowed-modes element %u\n",
						np, pval);
				else
					constraints->valid_modes_mask |= mode;
			}
			if (constraints->valid_modes_mask)
				constraints->valid_ops_mask
					|= REGULATOR_CHANGE_MODE;
		} else {
			pr_warn("%pOFn: mode mapping not defined\n", np);
		}
	}

	if (!of_property_read_u32(np, "regulator-system-load", &pval))
		constraints->system_load = pval;

	if (n_phandles) {
		constraints->max_spread = devm_kzalloc(dev,
				sizeof(*constraints->max_spread) * n_phandles,
				GFP_KERNEL);

		if (!constraints->max_spread)
			return -ENOMEM;

		of_property_read_u32_array(np, "regulator-coupled-max-spread",
					   constraints->max_spread, n_phandles);
	}

	if (!of_property_read_u32(np, "regulator-max-step-microvolt",
				  &pval))
		constraints->max_uV_step = pval;

	constraints->over_current_protection = of_property_read_bool(np,
					"regulator-over-current-protection");

	of_get_regulator_prot_limits(np, constraints);

	for (i = 0; i < ARRAY_SIZE(regulator_states); i++) {
		switch (i) {
		case PM_SUSPEND_MEM:
			suspend_state = &constraints->state_mem;
			break;
		case PM_SUSPEND_MAX:
			suspend_state = &constraints->state_disk;
			break;
		case PM_SUSPEND_STANDBY:
			suspend_state = &constraints->state_standby;
			break;
		case PM_SUSPEND_ON:
		case PM_SUSPEND_TO_IDLE:
		default:
			continue;
		}

		suspend_np = of_get_child_by_name(np, regulator_states[i]);
		if (!suspend_np)
			continue;
		if (!suspend_state) {
			of_node_put(suspend_np);
			continue;
		}

		if (!of_property_read_u32(suspend_np, "regulator-mode",
					  &pval)) {
			if (desc && desc->of_map_mode) {
				mode = desc->of_map_mode(pval);
				if (mode == REGULATOR_MODE_INVALID)
					pr_err("%pOFn: invalid mode %u\n",
					       np, pval);
				else
					suspend_state->mode = mode;
			} else {
				pr_warn("%pOFn: mapping for mode %d not defined\n",
					np, pval);
			}
		}

		if (of_property_read_bool(suspend_np,
					"regulator-on-in-suspend"))
			suspend_state->enabled = ENABLE_IN_SUSPEND;
		else if (of_property_read_bool(suspend_np,
					"regulator-off-in-suspend"))
			suspend_state->enabled = DISABLE_IN_SUSPEND;

		if (!of_property_read_u32(suspend_np,
				"regulator-suspend-min-microvolt", &pval))
			suspend_state->min_uV = pval;

		if (!of_property_read_u32(suspend_np,
				"regulator-suspend-max-microvolt", &pval))
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

	return 0;
}

/**
 * of_get_regulator_init_data - extract regulator_init_data structure info
 * @dev: device requesting for regulator_init_data
 * @node: regulator device node
 * @desc: regulator description
 *
 * Populates regulator_init_data structure by extracting data from device
 * tree node.
 *
 * Return: Pointer to a populated &struct regulator_init_data or NULL if
 *	   memory allocation fails.
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

	if (of_get_regulation_constraints(dev, node, &init_data, desc))
		return NULL;

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
 * Return: The number of matches found or a negative error number on failure.
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
					"failed to parse DT for regulator %pOFn\n",
					child);
				of_node_put(child);
				goto err_put;
			}
			match->of_node = of_node_get(child);
			count++;
			break;
		}
	}

	return count;

err_put:
	for (i = 0; i < num_matches; i++) {
		struct of_regulator_match *match = &matches[i];

		match->init_data = NULL;
		if (match->of_node) {
			of_node_put(match->of_node);
			match->of_node = NULL;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_regulator_match);

static struct
device_node *regulator_of_get_init_node(struct device *dev,
					const struct regulator_desc *desc)
{
	struct device_node *search, *child;
	const char *name;

	if (!dev->of_node || !desc->of_match)
		return NULL;

	if (desc->regulators_node) {
		search = of_get_child_by_name(dev->of_node,
					      desc->regulators_node);
	} else {
		search = of_node_get(dev->of_node);

		if (!strcmp(desc->of_match, search->name))
			return search;
	}

	if (!search) {
		dev_dbg(dev, "Failed to find regulator container node '%s'\n",
			desc->regulators_node);
		return NULL;
	}

	for_each_available_child_of_node(search, child) {
		name = of_get_property(child, "regulator-compatible", NULL);
		if (!name) {
			if (!desc->of_match_full_name)
				name = child->name;
			else
				name = child->full_name;
		}

		if (!strcmp(desc->of_match, name)) {
			of_node_put(search);
			/*
			 * 'of_node_get(child)' is already performed by the
			 * for_each loop.
			 */
			return child;
		}
	}

	of_node_put(search);

	return NULL;
}

struct regulator_init_data *regulator_of_get_init_data(struct device *dev,
					    const struct regulator_desc *desc,
					    struct regulator_config *config,
					    struct device_node **node)
{
	struct device_node *child;
	struct regulator_init_data *init_data = NULL;

	child = regulator_of_get_init_node(config->dev, desc);
	if (!child)
		return NULL;

	init_data = of_get_regulator_init_data(dev, child, desc);
	if (!init_data) {
		dev_err(dev, "failed to parse DT for regulator %pOFn\n", child);
		goto error;
	}

	if (desc->of_parse_cb) {
		int ret;

		ret = desc->of_parse_cb(child, desc, config);
		if (ret) {
			if (ret == -EPROBE_DEFER) {
				of_node_put(child);
				return ERR_PTR(-EPROBE_DEFER);
			}
			dev_err(dev,
				"driver callback failed to parse DT for regulator %pOFn\n",
				child);
			goto error;
		}
	}

	*node = child;

	return init_data;

error:
	of_node_put(child);

	return NULL;
}

/**
 * of_get_child_regulator - get a child regulator device node
 * based on supply name
 * @parent: Parent device node
 * @prop_name: Combination regulator supply name and "-supply"
 *
 * Traverse all child nodes.
 * Extract the child regulator device node corresponding to the supply name.
 *
 * Return: Pointer to the &struct device_node corresponding to the regulator
 *	   if found, or %NULL if not found.
 */
static struct device_node *of_get_child_regulator(struct device_node *parent,
						  const char *prop_name)
{
	struct device_node *regnode = NULL;
	struct device_node *child = NULL;

	for_each_child_of_node(parent, child) {
		regnode = of_parse_phandle(child, prop_name, 0);
		if (regnode)
			goto err_node_put;

		regnode = of_get_child_regulator(child, prop_name);
		if (regnode)
			goto err_node_put;
	}
	return NULL;

err_node_put:
	of_node_put(child);
	return regnode;
}

/**
 * of_get_regulator - get a regulator device node based on supply name
 * @dev: Device pointer for dev_printk() messages
 * @node: Device node pointer for supply property lookup
 * @supply: regulator supply name
 *
 * Extract the regulator device node corresponding to the supply name.
 *
 * Return: Pointer to the &struct device_node corresponding to the regulator
 *	   if found, or %NULL if not found.
 */
static struct device_node *of_get_regulator(struct device *dev, struct device_node *node,
					    const char *supply)
{
	struct device_node *regnode = NULL;
	char prop_name[64]; /* 64 is max size of property name */

	dev_dbg(dev, "Looking up %s-supply from device node %pOF\n", supply, node);

	snprintf(prop_name, 64, "%s-supply", supply);
	regnode = of_parse_phandle(node, prop_name, 0);
	if (regnode)
		return regnode;

	regnode = of_get_child_regulator(dev->of_node, prop_name);
	if (regnode)
		return regnode;

	dev_dbg(dev, "Looking up %s property in node %pOF failed\n", prop_name, dev->of_node);
	return NULL;
}

static struct regulator_dev *of_find_regulator_by_node(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device_by_of_node(&regulator_class, np);

	return dev ? dev_to_rdev(dev) : NULL;
}

/**
 * of_regulator_dev_lookup - lookup a regulator device with device tree only
 * @dev: Device pointer for regulator supply lookup.
 * @np: Device node pointer for regulator supply lookup.
 * @supply: Supply name or regulator ID.
 *
 * Return: Pointer to the &struct regulator_dev on success, or ERR_PTR()
 *	   encoded value on error.
 *
 * If successful, returns a pointer to the &struct regulator_dev that
 * corresponds to the name @supply and with the embedded &struct device
 * refcount incremented by one. The refcount must be dropped by calling
 * put_device().
 *
 * On failure one of the following ERR_PTR() encoded values is returned:
 * * -%ENODEV if lookup fails permanently.
 * * -%EPROBE_DEFER if lookup could succeed in the future.
 */
struct regulator_dev *of_regulator_dev_lookup(struct device *dev, struct device_node *np,
					      const char *supply)
{
	struct regulator_dev *r;
	struct device_node *node;

	node = of_get_regulator(dev, np, supply);
	if (node) {
		r = of_find_regulator_by_node(node);
		of_node_put(node);
		if (r)
			return r;

		/*
		 * We have a node, but there is no device.
		 * assume it has not registered yet.
		 */
		return ERR_PTR(-EPROBE_DEFER);
	}

	return ERR_PTR(-ENODEV);
}

struct regulator *_of_regulator_get(struct device *dev, struct device_node *node,
				    const char *id, enum regulator_get_type get_type)
{
	struct regulator_dev *r;
	int ret;

	ret = _regulator_get_common_check(dev, id, get_type);
	if (ret)
		return ERR_PTR(ret);

	r = of_regulator_dev_lookup(dev, node, id);
	return _regulator_get_common(r, dev, id, get_type);
}

/**
 * of_regulator_get - get regulator via device tree lookup
 * @dev: device used for dev_printk() messages
 * @node: device node for regulator "consumer"
 * @id: Supply name
 *
 * Return: pointer to struct regulator corresponding to the regulator producer,
 *	   or PTR_ERR() encoded error number.
 *
 * This is intended for use by consumers that want to get a regulator
 * supply directly from a device node. This will _not_ consider supply
 * aliases. See regulator_dev_lookup().
 */
struct regulator *of_regulator_get(struct device *dev,
					    struct device_node *node,
					    const char *id)
{
	return _of_regulator_get(dev, node, id, NORMAL_GET);
}
EXPORT_SYMBOL_GPL(of_regulator_get);

/**
 * of_regulator_get_optional - get optional regulator via device tree lookup
 * @dev: device used for dev_printk() messages
 * @node: device node for regulator "consumer"
 * @id: Supply name
 *
 * Return: pointer to struct regulator corresponding to the regulator producer,
 *	   or PTR_ERR() encoded error number.
 *
 * This is intended for use by consumers that want to get a regulator
 * supply directly from a device node, and can and want to deal with
 * absence of such supplies. This will _not_ consider supply aliases.
 * See regulator_dev_lookup().
 */
struct regulator *of_regulator_get_optional(struct device *dev,
					    struct device_node *node,
					    const char *id)
{
	return _of_regulator_get(dev, node, id, OPTIONAL_GET);
}
EXPORT_SYMBOL_GPL(of_regulator_get_optional);

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
				  struct device_node *to_find,
				  int *index)
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

		if (found) {
			*index = i;
			break;
		}
	}

	return found;
}

/**
 * of_check_coupling_data - Parse rdev's coupling properties and check data
 *			    consistency
 * @rdev: pointer to regulator_dev whose data is checked
 *
 * Function checks if all the following conditions are met:
 * - rdev's max_spread is greater than 0
 * - all coupled regulators have the same max_spread
 * - all coupled regulators have the same number of regulator_dev phandles
 * - all regulators are linked to each other
 *
 * Return: True if all conditions are met; false otherwise.
 */
bool of_check_coupling_data(struct regulator_dev *rdev)
{
	struct device_node *node = rdev->dev.of_node;
	int n_phandles = of_get_n_coupled(rdev);
	struct device_node *c_node;
	int index;
	int i;
	bool ret = true;

	/* iterate over rdev's phandles */
	for (i = 0; i < n_phandles; i++) {
		int max_spread = rdev->constraints->max_spread[i];
		int c_max_spread, c_n_phandles;

		if (max_spread <= 0) {
			dev_err(&rdev->dev, "max_spread value invalid\n");
			return false;
		}

		c_node = of_parse_phandle(node,
					  "regulator-coupled-with", i);

		if (!c_node)
			ret = false;

		c_n_phandles = of_count_phandle_with_args(c_node,
							  "regulator-coupled-with",
							  NULL);

		if (c_n_phandles != n_phandles) {
			dev_err(&rdev->dev, "number of coupled reg phandles mismatch\n");
			ret = false;
			goto clean;
		}

		if (!of_coupling_find_node(c_node, node, &index)) {
			dev_err(&rdev->dev, "missing 2-way linking for coupled regulators\n");
			ret = false;
			goto clean;
		}

		if (of_property_read_u32_index(c_node, "regulator-coupled-max-spread",
					       index, &c_max_spread)) {
			ret = false;
			goto clean;
		}

		if (c_max_spread != max_spread) {
			dev_err(&rdev->dev,
				"coupled regulators max_spread mismatch\n");
			ret = false;
			goto clean;
		}

clean:
		of_node_put(c_node);
		if (!ret)
			break;
	}

	return ret;
}

/**
 * of_parse_coupled_regulator() - Get regulator_dev pointer from rdev's property
 * @rdev: Pointer to regulator_dev, whose DTS is used as a source to parse
 *	  "regulator-coupled-with" property
 * @index: Index in phandles array
 *
 * Return: Pointer to the &struct regulator_dev parsed from DTS, or %NULL if
 *	   it has not yet been registered.
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

/*
 * Check if name is a supply name according to the '*-supply' pattern
 * return 0 if false
 * return length of supply name without the -supply
 */
static int is_supply_name(const char *name)
{
	int strs, i;

	strs = strlen(name);
	/* string need to be at minimum len(x-supply) */
	if (strs < 8)
		return 0;
	for (i = strs - 6; i > 0; i--) {
		/* find first '-' and check if right part is supply */
		if (name[i] != '-')
			continue;
		if (strcmp(name + i + 1, "supply") != 0)
			return 0;
		return i;
	}
	return 0;
}

/**
 * of_regulator_bulk_get_all - get multiple regulator consumers
 *
 * @dev:	Device to supply
 * @np:		device node to search for consumers
 * @consumers:  Configuration of consumers; clients are stored here.
 *
 * This helper function allows drivers to get several regulator
 * consumers in one operation.  If any of the regulators cannot be
 * acquired then any regulators that were allocated will be freed
 * before returning to the caller, and @consumers will not be
 * changed.
 *
 * Return: Number of regulators on success, or a negative error number
 *	   on failure.
 */
int of_regulator_bulk_get_all(struct device *dev, struct device_node *np,
			      struct regulator_bulk_data **consumers)
{
	int num_consumers = 0;
	struct regulator *tmp;
	struct regulator_bulk_data *_consumers = NULL;
	struct property *prop;
	int i, n = 0, ret;
	char name[64];

	/*
	 * first pass: get numbers of xxx-supply
	 * second pass: fill consumers
	 */
restart:
	for_each_property_of_node(np, prop) {
		i = is_supply_name(prop->name);
		if (i == 0)
			continue;
		if (!_consumers) {
			num_consumers++;
			continue;
		} else {
			memcpy(name, prop->name, i);
			name[i] = '\0';
			tmp = regulator_get(dev, name);
			if (IS_ERR(tmp)) {
				ret = PTR_ERR(tmp);
				goto error;
			}
			_consumers[n].consumer = tmp;
			n++;
			continue;
		}
	}
	if (_consumers) {
		*consumers = _consumers;
		return num_consumers;
	}
	if (num_consumers == 0)
		return 0;
	_consumers = kmalloc_array(num_consumers,
				   sizeof(struct regulator_bulk_data),
				   GFP_KERNEL);
	if (!_consumers)
		return -ENOMEM;
	goto restart;

error:
	while (--n >= 0)
		regulator_put(_consumers[n].consumer);
	kfree(_consumers);
	return ret;
}
EXPORT_SYMBOL_GPL(of_regulator_bulk_get_all);
