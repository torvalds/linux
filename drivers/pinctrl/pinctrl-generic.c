// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "generic pinconfig core: " fmt

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"
#include "pinmux.h"

static int pinctrl_generic_pins_function_dt_subnode_to_map(struct pinctrl_dev *pctldev,
							   struct device_node *parent,
							   struct device_node *np,
							   struct pinctrl_map **maps,
							   unsigned int *num_maps,
							   unsigned int *num_reserved_maps,
							   const char **group_names,
							   unsigned int ngroups)
{
	struct device *dev = pctldev->dev;
	const char **functions;
	const char *group_name;
	unsigned long *configs;
	unsigned int num_configs, pin, *pins;
	int npins, ret, reserve = 1;

	npins = of_property_count_u32_elems(np, "pins");

	if (npins < 1) {
		dev_err(dev, "invalid pinctrl group %pOFn.%pOFn %d\n",
			parent, np, npins);
		return npins;
	}

	group_name = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn", parent, np);
	if (!group_name)
		return -ENOMEM;

	group_names[ngroups] = group_name;

	pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	functions = devm_kcalloc(dev, npins, sizeof(*functions), GFP_KERNEL);
	if (!functions)
		return -ENOMEM;

	for (int i = 0; i < npins; i++) {
		ret = of_property_read_u32_index(np, "pins", i, &pin);
		if (ret)
			return ret;

		pins[i] = pin;

		ret = of_property_read_string(np, "function", &functions[i]);
		if (ret)
			return ret;
	}

	ret = pinctrl_utils_reserve_map(pctldev, maps, num_reserved_maps, num_maps, reserve);
	if (ret)
		return ret;

	ret = pinctrl_utils_add_map_mux(pctldev, maps, num_reserved_maps, num_maps, group_name,
					parent->name);
	if (ret < 0)
		return ret;

	ret = pinctrl_generic_add_group(pctldev, group_name, pins, npins, functions);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to add group %s: %d\n",
				     group_name, ret);

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs, &num_configs);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse pin config of group %s\n",
			group_name);

	if (num_configs == 0)
		return 0;

	ret = pinctrl_utils_reserve_map(pctldev, maps, num_reserved_maps, num_maps, reserve);
	if (ret)
		return ret;

	ret = pinctrl_utils_add_map_configs(pctldev, maps, num_reserved_maps, num_maps, group_name,
					    configs,
			num_configs, PIN_MAP_TYPE_CONFIGS_GROUP);
	kfree(configs);
	if (ret)
		return ret;

	return 0;
};

/*
 * For platforms that do not define groups or functions in the driver, but
 * instead use the devicetree to describe them. This function will, unlike
 * pinconf_generic_dt_node_to_map() etc which rely on driver defined groups
 * and functions, create them in addition to parsing pinconf properties and
 * adding mappings.
 */
int pinctrl_generic_pins_function_dt_node_to_map(struct pinctrl_dev *pctldev,
						 struct device_node *np,
						 struct pinctrl_map **maps,
						 unsigned int *num_maps)
{
	struct device *dev = pctldev->dev;
	struct device_node *child_np;
	const char **group_names;
	unsigned int num_reserved_maps = 0;
	int ngroups = 0;
	int ret;

	*maps = NULL;
	*num_maps = 0;

	/*
	 * Check if this is actually the pins node, or a parent containing
	 * multiple pins nodes.
	 */
	if (!of_property_present(np, "pins"))
		goto parent;

	group_names = devm_kcalloc(dev, 1, sizeof(*group_names), GFP_KERNEL);
	if (!group_names)
		return -ENOMEM;

	ret = pinctrl_generic_pins_function_dt_subnode_to_map(pctldev, np, np,
							      maps, num_maps,
							      &num_reserved_maps,
							      group_names,
							      ngroups);
	if (ret) {
		pinctrl_utils_free_map(pctldev, *maps, *num_maps);
		return dev_err_probe(dev, ret, "error figuring out mappings for %s\n", np->name);
	}

	ret = pinmux_generic_add_function(pctldev, np->name, group_names, 1, NULL);
	if (ret < 0) {
		pinctrl_utils_free_map(pctldev, *maps, *num_maps);
		return dev_err_probe(dev, ret, "error adding function %s\n", np->name);
	}

	return 0;

parent:
	for_each_available_child_of_node(np, child_np)
		ngroups += 1;

	group_names = devm_kcalloc(dev, ngroups, sizeof(*group_names), GFP_KERNEL);
	if (!group_names)
		return -ENOMEM;

	ngroups = 0;
	for_each_available_child_of_node_scoped(np, child_np) {
		ret = pinctrl_generic_pins_function_dt_subnode_to_map(pctldev, np, child_np,
								      maps, num_maps,
								      &num_reserved_maps,
								      group_names,
								      ngroups);
		if (ret) {
			pinctrl_utils_free_map(pctldev, *maps, *num_maps);
			return dev_err_probe(dev, ret, "error figuring out mappings for %s\n",
					     np->name);
		}

		ngroups++;
	}

	ret = pinmux_generic_add_function(pctldev, np->name, group_names, ngroups, NULL);
	if (ret < 0) {
		pinctrl_utils_free_map(pctldev, *maps, *num_maps);
		return dev_err_probe(dev, ret, "error adding function %s\n", np->name);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_pins_function_dt_node_to_map);
