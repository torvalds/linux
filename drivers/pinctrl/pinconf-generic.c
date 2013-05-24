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
	PCONFDUMP(PIN_CONFIG_DRIVE_PUSH_PULL, "output drive push pull", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_DRAIN, "output drive open drain", NULL),
	PCONFDUMP(PIN_CONFIG_DRIVE_OPEN_SOURCE, "output drive open source", NULL),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT_ENABLE, "input schmitt enabled", NULL),
	PCONFDUMP(PIN_CONFIG_INPUT_SCHMITT, "input schmitt trigger", NULL),
	PCONFDUMP(PIN_CONFIG_INPUT_DEBOUNCE, "input debounce", "time units"),
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
