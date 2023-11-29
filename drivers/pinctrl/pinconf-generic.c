// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core driver for the generic pin config portions of the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#define pr_fmt(fmt) "generic pinconfig core: " fmt

#include <linux/array_size.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item conf_items[] = {
	PCONFDUMP(PIN_CONFIG_BIAS_BUS_HOLD, "input bias bus hold", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_DISABLE, "input bias disabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_HIGH_IMPEDANCE, "input bias high impedance", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_DOWN, "input bias pull down", "ohms", true),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_PIN_DEFAULT,
				"input bias pull to pin specific state", "ohms", true),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_UP, "input bias pull up", "ohms", true),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_DRAIN, "output drive open drain", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_SOURCE, "output drive open source", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_PUSH_PULL, "output drive push pull", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_STRENGTH, "output drive strength", "mA", true),
	PCONFDUMP(PIN_CONFIG_DRIVE_STRENGTH_UA, "output drive strength", "uA", true),
	PCONFDUMP(PIN_CONFIG_INPUT_DEBOUNCE, "input debounce", "usec", true),
	PCONFDUMP(PIN_CONFIG_INPUT_ENABLE, "input enabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT, "input schmitt trigger", NULL, false),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT_ENABLE, "input schmitt enabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_MODE_LOW_POWER, "pin low power", "mode", true),
	PCONFDUMP(PIN_CONFIG_OUTPUT_ENABLE, "output enabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_OUTPUT, "pin output", "level", true),
	PCONFDUMP(PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS, "output impedance", "ohms", true),
	PCONFDUMP(PIN_CONFIG_POWER_SOURCE, "pin power source", "selector", true),
	PCONFDUMP(PIN_CONFIG_SLEEP_HARDWARE_STATE, "sleep hardware state", NULL, false),
	PCONFDUMP(PIN_CONFIG_SLEW_RATE, "slew rate", NULL, true),
	PCONFDUMP(PIN_CONFIG_SKEW_DELAY, "skew delay", NULL, true),
};

static void pinconf_generic_dump_one(struct pinctrl_dev *pctldev,
				     struct seq_file *s, const char *gname,
				     unsigned int pin,
				     const struct pin_config_item *items,
				     int nitems, int *print_sep)
{
	int i;

	for (i = 0; i < nitems; i++) {
		unsigned long config;
		int ret;

		/* We want to check out this parameter */
		config = pinconf_to_config_packed(items[i].param, 0);
		if (gname)
			ret = pin_config_group_get(dev_name(pctldev->dev),
						   gname, &config);
		else
			ret = pin_config_get_for_pin(pctldev, pin, &config);
		/* These are legal errors */
		if (ret == -EINVAL || ret == -ENOTSUPP)
			continue;
		if (ret) {
			seq_printf(s, "ERROR READING CONFIG SETTING %d ", i);
			continue;
		}
		/* comma between multiple configs */
		if (*print_sep)
			seq_puts(s, ", ");
		*print_sep = 1;
		seq_puts(s, items[i].display);
		/* Print unit if available */
		if (items[i].has_arg) {
			seq_printf(s, " (%u",
				   pinconf_to_config_argument(config));
			if (items[i].format)
				seq_printf(s, " %s)", items[i].format);
			else
				seq_puts(s, ")");
		}
	}
}

/**
 * pinconf_generic_dump_pins - Print information about pin or group of pins
 * @pctldev:	Pincontrol device
 * @s:		File to print to
 * @gname:	Group name specifying pins
 * @pin:	Pin number specyfying pin
 *
 * Print the pinconf configuration for the requested pin(s) to @s. Pins can be
 * specified either by pin using @pin or by group using @gname. Only one needs
 * to be specified the other can be NULL/0.
 */
void pinconf_generic_dump_pins(struct pinctrl_dev *pctldev, struct seq_file *s,
			       const char *gname, unsigned int pin)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int print_sep = 0;

	if (!ops->is_generic)
		return;

	/* generic parameters */
	pinconf_generic_dump_one(pctldev, s, gname, pin, conf_items,
				 ARRAY_SIZE(conf_items), &print_sep);
	/* driver-specific parameters */
	if (pctldev->desc->num_custom_params &&
	    pctldev->desc->custom_conf_items)
		pinconf_generic_dump_one(pctldev, s, gname, pin,
					 pctldev->desc->custom_conf_items,
					 pctldev->desc->num_custom_params,
					 &print_sep);
}

void pinconf_generic_dump_config(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned long config)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(conf_items); i++) {
		if (pinconf_to_config_param(config) != conf_items[i].param)
			continue;
		seq_printf(s, "%s: 0x%x", conf_items[i].display,
			   pinconf_to_config_argument(config));
	}

	if (!pctldev->desc->num_custom_params ||
	    !pctldev->desc->custom_conf_items)
		return;

	for (i = 0; i < pctldev->desc->num_custom_params; i++) {
		if (pinconf_to_config_param(config) !=
		    pctldev->desc->custom_conf_items[i].param)
			continue;
		seq_printf(s, "%s: 0x%x",
				pctldev->desc->custom_conf_items[i].display,
				pinconf_to_config_argument(config));
	}
}
EXPORT_SYMBOL_GPL(pinconf_generic_dump_config);
#endif

#ifdef CONFIG_OF
static const struct pinconf_generic_params dt_params[] = {
	{ "bias-bus-hold", PIN_CONFIG_BIAS_BUS_HOLD, 0 },
	{ "bias-disable", PIN_CONFIG_BIAS_DISABLE, 0 },
	{ "bias-high-impedance", PIN_CONFIG_BIAS_HIGH_IMPEDANCE, 0 },
	{ "bias-pull-up", PIN_CONFIG_BIAS_PULL_UP, 1 },
	{ "bias-pull-pin-default", PIN_CONFIG_BIAS_PULL_PIN_DEFAULT, 1 },
	{ "bias-pull-down", PIN_CONFIG_BIAS_PULL_DOWN, 1 },
	{ "drive-open-drain", PIN_CONFIG_DRIVE_OPEN_DRAIN, 0 },
	{ "drive-open-source", PIN_CONFIG_DRIVE_OPEN_SOURCE, 0 },
	{ "drive-push-pull", PIN_CONFIG_DRIVE_PUSH_PULL, 0 },
	{ "drive-strength", PIN_CONFIG_DRIVE_STRENGTH, 0 },
	{ "drive-strength-microamp", PIN_CONFIG_DRIVE_STRENGTH_UA, 0 },
	{ "input-debounce", PIN_CONFIG_INPUT_DEBOUNCE, 0 },
	{ "input-disable", PIN_CONFIG_INPUT_ENABLE, 0 },
	{ "input-enable", PIN_CONFIG_INPUT_ENABLE, 1 },
	{ "input-schmitt", PIN_CONFIG_INPUT_SCHMITT, 0 },
	{ "input-schmitt-disable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 0 },
	{ "input-schmitt-enable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 1 },
	{ "low-power-disable", PIN_CONFIG_MODE_LOW_POWER, 0 },
	{ "low-power-enable", PIN_CONFIG_MODE_LOW_POWER, 1 },
	{ "output-disable", PIN_CONFIG_OUTPUT_ENABLE, 0 },
	{ "output-enable", PIN_CONFIG_OUTPUT_ENABLE, 1 },
	{ "output-high", PIN_CONFIG_OUTPUT, 1, },
	{ "output-impedance-ohms", PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS, 0 },
	{ "output-low", PIN_CONFIG_OUTPUT, 0, },
	{ "power-source", PIN_CONFIG_POWER_SOURCE, 0 },
	{ "sleep-hardware-state", PIN_CONFIG_SLEEP_HARDWARE_STATE, 0 },
	{ "slew-rate", PIN_CONFIG_SLEW_RATE, 0 },
	{ "skew-delay", PIN_CONFIG_SKEW_DELAY, 0 },
};

/**
 * parse_dt_cfg() - Parse DT pinconf parameters
 * @np:	DT node
 * @params:	Array of describing generic parameters
 * @count:	Number of entries in @params
 * @cfg:	Array of parsed config options
 * @ncfg:	Number of entries in @cfg
 *
 * Parse the config options described in @params from @np and puts the result
 * in @cfg. @cfg does not need to be empty, entries are added beginning at
 * @ncfg. @ncfg is updated to reflect the number of entries after parsing. @cfg
 * needs to have enough memory allocated to hold all possible entries.
 */
static void parse_dt_cfg(struct device_node *np,
			 const struct pinconf_generic_params *params,
			 unsigned int count, unsigned long *cfg,
			 unsigned int *ncfg)
{
	int i;

	for (i = 0; i < count; i++) {
		u32 val;
		int ret;
		const struct pinconf_generic_params *par = &params[i];

		ret = of_property_read_u32(np, par->property, &val);

		/* property not found */
		if (ret == -EINVAL)
			continue;

		/* use default value, when no value is specified */
		if (ret)
			val = par->default_value;

		pr_debug("found %s with value %u\n", par->property, val);
		cfg[*ncfg] = pinconf_to_config_packed(par->param, val);
		(*ncfg)++;
	}
}

/**
 * pinconf_generic_parse_dt_config()
 * parse the config properties into generic pinconfig values.
 * @np: node containing the pinconfig properties
 * @pctldev: pincontrol device
 * @configs: array with nconfigs entries containing the generic pinconf values
 *           must be freed when no longer necessary.
 * @nconfigs: number of configurations
 */
int pinconf_generic_parse_dt_config(struct device_node *np,
				    struct pinctrl_dev *pctldev,
				    unsigned long **configs,
				    unsigned int *nconfigs)
{
	unsigned long *cfg;
	unsigned int max_cfg, ncfg = 0;
	int ret;

	if (!np)
		return -EINVAL;

	/* allocate a temporary array big enough to hold one of each option */
	max_cfg = ARRAY_SIZE(dt_params);
	if (pctldev)
		max_cfg += pctldev->desc->num_custom_params;
	cfg = kcalloc(max_cfg, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	parse_dt_cfg(np, dt_params, ARRAY_SIZE(dt_params), cfg, &ncfg);
	if (pctldev && pctldev->desc->num_custom_params &&
		pctldev->desc->custom_params)
		parse_dt_cfg(np, pctldev->desc->custom_params,
			     pctldev->desc->num_custom_params, cfg, &ncfg);

	ret = 0;

	/* no configs found at all */
	if (ncfg == 0) {
		*configs = NULL;
		*nconfigs = 0;
		goto out;
	}

	/*
	 * Now limit the number of configs to the real number of
	 * found properties.
	 */
	*configs = kmemdup(cfg, ncfg * sizeof(unsigned long), GFP_KERNEL);
	if (!*configs) {
		ret = -ENOMEM;
		goto out;
	}

	*nconfigs = ncfg;

out:
	kfree(cfg);
	return ret;
}
EXPORT_SYMBOL_GPL(pinconf_generic_parse_dt_config);

int pinconf_generic_dt_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np, struct pinctrl_map **map,
		unsigned int *reserved_maps, unsigned int *num_maps,
		enum pinctrl_map_type type)
{
	int ret;
	const char *function;
	struct device *dev = pctldev->dev;
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	unsigned int reserve, strings_count;
	struct property *prop;
	const char *group;
	const char *subnode_target_type = "pins";

	ret = of_property_count_strings(np, "pins");
	if (ret < 0) {
		ret = of_property_count_strings(np, "groups");
		if (ret < 0)
			/* skip this node; may contain config child nodes */
			return 0;
		if (type == PIN_MAP_TYPE_INVALID)
			type = PIN_MAP_TYPE_CONFIGS_GROUP;
		subnode_target_type = "groups";
	} else {
		if (type == PIN_MAP_TYPE_INVALID)
			type = PIN_MAP_TYPE_CONFIGS_PIN;
	}
	strings_count = ret;

	ret = of_property_read_string(np, "function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev, "%pOF: could not parse property function\n",
				np);
		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs,
					      &num_configs);
	if (ret < 0) {
		dev_err(dev, "%pOF: could not parse node property\n", np);
		return ret;
	}

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;

	reserve *= strings_count;

	ret = pinctrl_utils_reserve_map(pctldev, map, reserved_maps,
			num_maps, reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_string(np, subnode_target_type, prop, group) {
		if (function) {
			ret = pinctrl_utils_add_map_mux(pctldev, map,
					reserved_maps, num_maps, group,
					function);
			if (ret < 0)
				goto exit;
		}

		if (num_configs) {
			ret = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, group, configs,
					num_configs, type);
			if (ret < 0)
				goto exit;
		}
	}
	ret = 0;

exit:
	kfree(configs);
	return ret;
}
EXPORT_SYMBOL_GPL(pinconf_generic_dt_subnode_to_map);

int pinconf_generic_dt_node_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np_config, struct pinctrl_map **map,
		unsigned int *num_maps, enum pinctrl_map_type type)
{
	unsigned int reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	ret = pinconf_generic_dt_subnode_to_map(pctldev, np_config, map,
						&reserved_maps, num_maps, type);
	if (ret < 0)
		goto exit;

	for_each_available_child_of_node(np_config, np) {
		ret = pinconf_generic_dt_subnode_to_map(pctldev, np, map,
					&reserved_maps, num_maps, type);
		if (ret < 0) {
			of_node_put(np);
			goto exit;
		}
	}
	return 0;

exit:
	pinctrl_utils_free_map(pctldev, *map, *num_maps);
	return ret;
}
EXPORT_SYMBOL_GPL(pinconf_generic_dt_node_to_map);

void pinconf_generic_dt_free_map(struct pinctrl_dev *pctldev,
				 struct pinctrl_map *map,
				 unsigned int num_maps)
{
	pinctrl_utils_free_map(pctldev, map, num_maps);
}
EXPORT_SYMBOL_GPL(pinconf_generic_dt_free_map);

#endif
