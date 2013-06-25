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

#ifdef CONFIG_DEBUG_FS

struct pin_config_item {
	const enum pin_config_param param;
	const char * const display;
	const char * const format;
};

#define PCONFDUMP(a, b, c) { .param = a, .display = b, .format = c }

static struct pin_config_item conf_items[] = {
	PCONFDUMP(PIN_CONFIG_BIAS_DISABLE, "input bias disabled", NULL),
	PCONFDUMP(PIN_CONFIG_BIAS_HIGH_IMPEDANCE, "input bias high impedance", NULL),
	PCONFDUMP(PIN_CONFIG_BIAS_BUS_HOLD, "input bias bus hold", NULL),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_UP, "input bias pull up", NULL),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_DOWN, "input bias pull down", NULL),
	PCONFDUMP(PIN_CONFIG_BIAS_PULL_PIN_DEFAULT,
				"input bias pull to pin specific state", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_PUSH_PULL, "output drive push pull", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_DRAIN, "output drive open drain", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_SOURCE, "output drive open source", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_STRENGTH, "output drive strength", "mA"),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT_ENABLE, "input schmitt enabled", NULL),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT, "input schmitt trigger", NULL),
	PCONFDUMP(PIN_CONFIG_INPUT_DEBOUNCE, "input debounce", "usec"),
	PCONFDUMP(PIN_CONFIG_POWER_SOURCE, "pin power source", "selector"),
	PCONFDUMP(PIN_CONFIG_SLEW_RATE, "slew rate", NULL),
	PCONFDUMP(PIN_CONFIG_LOW_POWER_MODE, "pin low power", "mode"),
	PCONFDUMP(PIN_CONFIG_OUTPUT, "pin output", "level"),
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
		if (conf_items[i].format &&
		    pinconf_to_config_argument(config) != 0)
			seq_printf(s, " (%u %s)",
				   pinconf_to_config_argument(config),
				   conf_items[i].format);
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
		if (conf_items[i].format && config != 0)
			seq_printf(s, " (%u %s)",
				   pinconf_to_config_argument(config),
				   conf_items[i].format);
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

static struct pinconf_generic_dt_params dt_params[] = {
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
	{ "input-schmitt-enable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 1 },
	{ "input-schmitt-disable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 0 },
	{ "input-debounce", PIN_CONFIG_INPUT_DEBOUNCE, 0 },
	{ "low-power-enable", PIN_CONFIG_LOW_POWER_MODE, 1 },
	{ "low-power-disable", PIN_CONFIG_LOW_POWER_MODE, 0 },
	{ "output-low", PIN_CONFIG_OUTPUT, 0, },
	{ "output-high", PIN_CONFIG_OUTPUT, 1, },
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
		struct pinconf_generic_dt_params *par = &dt_params[i];
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
	*configs = kzalloc(ncfg * sizeof(unsigned long), GFP_KERNEL);
	if (!*configs) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(*configs, cfg, ncfg * sizeof(unsigned long));
	*nconfigs = ncfg;

out:
	kfree(cfg);
	return ret;
}
#endif
