/*
 * Core driver for the generic pin config portions of the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) "generic pinconfig core: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/of.h>
#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#ifdef CONFIG_DEBUG_FS

struct pin_config_item {
	const enum pin_config_param param;
	const char * const display;
	const char * const format;
	bool has_arg;
};

#define PCONFDUMP(a, b, c, d) { .param = a, .display = b, .format = c, \
				.has_arg = d }

static const struct pin_config_item conf_items[] = {
	PCONFDUMP(PIN_CONFIG_BIAS_DISABLE, "input bias disabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_HIGH_IMPEDANCE, "input bias high impedance", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_BUS_HOLD, "input bias bus hold", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_UP, "input bias pull up", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_DOWN, "input bias pull down", NULL, false),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_PIN_DEFAULT,
				"input bias pull to pin specific state", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_PUSH_PULL, "output drive push pull", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_DRAIN, "output drive open drain", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_SOURCE, "output drive open source", NULL, false),
	PCONFDUMP(PIN_CONFIG_DRIVE_STRENGTH, "output drive strength", "mA", true),
	PCONFDUMP(PIN_CONFIG_INPUT_ENABLE, "input enabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT_ENABLE, "input schmitt enabled", NULL, false),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT, "input schmitt trigger", NULL, false),
	PCONFDUMP(PIN_CONFIG_INPUT_DEBOUNCE, "input debounce", "usec", true),
	PCONFDUMP(PIN_CONFIG_POWER_SOURCE, "pin power source", "selector", true),
	PCONFDUMP(PIN_CONFIG_SLEW_RATE, "slew rate", NULL, true),
	PCONFDUMP(PIN_CONFIG_LOW_POWER_MODE, "pin low power", "mode", true),
	PCONFDUMP(PIN_CONFIG_OUTPUT, "pin output", "level", true),
};

void pinconf_generic_dump_pin(struct pinctrl_dev *pctldev,
			      struct seq_file *s, unsigned pin)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int i;

	if (!ops->is_generic)
		return;

	for (i = 0; i < ARRAY_SIZE(conf_items); i++) {
		unsigned long config;
		int ret;

		/* We want to check out this parameter */
		config = pinconf_to_config_packed(conf_items[i].param, 0);
		ret = pin_config_get_for_pin(pctldev, pin, &config);
		/* These are legal errors */
		if (ret == -EINVAL || ret == -ENOTSUPP)
			continue;
		if (ret) {
			seq_printf(s, "ERROR READING CONFIG SETTING %d ", i);
			continue;
		}
		/* Space between multiple configs */
		seq_puts(s, " ");
		seq_puts(s, conf_items[i].display);
		/* Print unit if available */
		if (conf_items[i].has_arg) {
			seq_printf(s, " (%u",
				   pinconf_to_config_argument(config));
			if (conf_items[i].format)
				seq_printf(s, " %s)", conf_items[i].format);
			else
				seq_puts(s, ")");
		}
	}
}

void pinconf_generic_dump_group(struct pinctrl_dev *pctldev,
			      struct seq_file *s, const char *gname)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int i;

	if (!ops->is_generic)
		return;

	for (i = 0; i < ARRAY_SIZE(conf_items); i++) {
		unsigned long config;
		int ret;

		/* We want to check out this parameter */
		config = pinconf_to_config_packed(conf_items[i].param, 0);
		ret = pin_config_group_get(dev_name(pctldev->dev), gname,
					   &config);
		/* These are legal errors */
		if (ret == -EINVAL || ret == -ENOTSUPP)
			continue;
		if (ret) {
			seq_printf(s, "ERROR READING CONFIG SETTING %d ", i);
			continue;
		}
		/* Space between multiple configs */
		seq_puts(s, " ");
		seq_puts(s, conf_items[i].display);
		/* Print unit if available */
		if (conf_items[i].has_arg) {
			seq_printf(s, " (%u",
				   pinconf_to_config_argument(config));
			if (conf_items[i].format)
				seq_printf(s, " %s)", conf_items[i].format);
			else
				seq_puts(s, ")");
		}
	}
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
}
EXPORT_SYMBOL_GPL(pinconf_generic_dump_config);
#endif

#ifdef CONFIG_OF
struct pinconf_generic_dt_params {
	const char * const property;
	enum pin_config_param param;
	u32 default_value;
};

static const struct pinconf_generic_dt_params dt_params[] = {
	{ "bias-disable", PIN_CONFIG_BIAS_DISABLE, 0 },
	{ "bias-high-impedance", PIN_CONFIG_BIAS_HIGH_IMPEDANCE, 0 },
	{ "bias-bus-hold", PIN_CONFIG_BIAS_BUS_HOLD, 0 },
	{ "bias-pull-up", PIN_CONFIG_BIAS_PULL_UP, 1 },
	{ "bias-pull-down", PIN_CONFIG_BIAS_PULL_DOWN, 1 },
	{ "bias-pull-pin-default", PIN_CONFIG_BIAS_PULL_PIN_DEFAULT, 1 },
	{ "drive-push-pull", PIN_CONFIG_DRIVE_PUSH_PULL, 0 },
	{ "drive-open-drain", PIN_CONFIG_DRIVE_OPEN_DRAIN, 0 },
	{ "drive-open-source", PIN_CONFIG_DRIVE_OPEN_SOURCE, 0 },
	{ "drive-strength", PIN_CONFIG_DRIVE_STRENGTH, 0 },
	{ "input-enable", PIN_CONFIG_INPUT_ENABLE, 1 },
	{ "input-disable", PIN_CONFIG_INPUT_ENABLE, 0 },
	{ "input-schmitt-enable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 1 },
	{ "input-schmitt-disable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 0 },
	{ "input-debounce", PIN_CONFIG_INPUT_DEBOUNCE, 0 },
	{ "power-source", PIN_CONFIG_POWER_SOURCE, 0 },
	{ "low-power-enable", PIN_CONFIG_LOW_POWER_MODE, 1 },
	{ "low-power-disable", PIN_CONFIG_LOW_POWER_MODE, 0 },
	{ "output-low", PIN_CONFIG_OUTPUT, 0, },
	{ "output-high", PIN_CONFIG_OUTPUT, 1, },
	{ "slew-rate", PIN_CONFIG_SLEW_RATE, 0},
};

/**
 * pinconf_generic_parse_dt_config()
 * parse the config properties into generic pinconfig values.
 * @np: node containing the pinconfig properties
 * @configs: array with nconfigs entries containing the generic pinconf values
 * @nconfigs: umber of configurations
 */
int pinconf_generic_parse_dt_config(struct device_node *np,
				    unsigned long **configs,
				    unsigned int *nconfigs)
{
	unsigned long *cfg;
	unsigned int ncfg = 0;
	int ret;
	int i;
	u32 val;

	if (!np)
		return -EINVAL;

	/* allocate a temporary array big enough to hold one of each option */
	cfg = kzalloc(sizeof(*cfg) * ARRAY_SIZE(dt_params), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(dt_params); i++) {
		const struct pinconf_generic_dt_params *par = &dt_params[i];
		ret = of_property_read_u32(np, par->property, &val);

		/* property not found */
		if (ret == -EINVAL)
			continue;

		/* use default value, when no value is specified */
		if (ret)
			val = par->default_value;

		pr_debug("found %s with value %u\n", par->property, val);
		cfg[ncfg] = pinconf_to_config_packed(par->param, val);
		ncfg++;
	}

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

int pinconf_generic_dt_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np, struct pinctrl_map **map,
		unsigned *reserved_maps, unsigned *num_maps,
		enum pinctrl_map_type type)
{
	int ret;
	const char *function;
	struct device *dev = pctldev->dev;
	unsigned long *configs = NULL;
	unsigned num_configs = 0;
	unsigned reserve;
	struct property *prop;
	const char *group;
	const char *subnode_target_type = "pins";

	ret = of_property_read_string(np, "function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev, "could not parse property function\n");
		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, &configs, &num_configs);
	if (ret < 0) {
		dev_err(dev, "could not parse node property\n");
		return ret;
	}

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;

	ret = of_property_count_strings(np, "pins");
	if (ret < 0) {
		ret = of_property_count_strings(np, "groups");
		if (ret < 0) {
			dev_err(dev, "could not parse property pins/groups\n");
			goto exit;
		}
		if (type == PIN_MAP_TYPE_INVALID)
			type = PIN_MAP_TYPE_CONFIGS_GROUP;
		subnode_target_type = "groups";
	} else {
		if (type == PIN_MAP_TYPE_INVALID)
			type = PIN_MAP_TYPE_CONFIGS_PIN;
	}
	reserve *= ret;

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
		unsigned *num_maps, enum pinctrl_map_type type)
{
	unsigned reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = pinconf_generic_dt_subnode_to_map(pctldev, np, map,
					&reserved_maps, num_maps, type);
		if (ret < 0) {
			pinctrl_utils_dt_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pinconf_generic_dt_node_to_map);

#endif
