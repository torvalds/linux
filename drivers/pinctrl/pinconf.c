/*
 * Core driver for the pin config portions of the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#define pr_fmt(fmt) "pinconfig core: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include "core.h"
#include "pinconf.h"

int pinconf_check_ops(struct pinctrl_dev *pctldev)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* We have to be able to config the pins in SOME way */
	if (!ops->pin_config_set && !ops->pin_config_group_set) {
		dev_err(pctldev->dev,
			"pinconf has to be able to set a pins config\n");
		return -EINVAL;
	}
	return 0;
}

int pinconf_validate_map(struct pinctrl_map const *map, int i)
{
	if (!map->data.configs.group_or_pin) {
		pr_err("failed to register map %s (%d): no group/pin given\n",
		       map->name, i);
		return -EINVAL;
	}

	if (!map->data.configs.num_configs ||
			!map->data.configs.configs) {
		pr_err("failed to register map %s (%d): no configs given\n",
		       map->name, i);
		return -EINVAL;
	}

	return 0;
}

int pin_config_get_for_pin(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	if (!ops || !ops->pin_config_get) {
		dev_dbg(pctldev->dev,
			"cannot get pin configuration, .pin_config_get missing in driver\n");
		return -ENOTSUPP;
	}

	return ops->pin_config_get(pctldev, pin, config);
}

int pin_config_group_get(const char *dev_name, const char *pin_group,
			 unsigned long *config)
{
	struct pinctrl_dev *pctldev;
	const struct pinconf_ops *ops;
	int selector, ret;

	pctldev = get_pinctrl_dev_from_devname(dev_name);
	if (!pctldev) {
		ret = -EINVAL;
		return ret;
	}

	mutex_lock(&pctldev->mutex);

	ops = pctldev->desc->confops;

	if (!ops || !ops->pin_config_group_get) {
		dev_dbg(pctldev->dev,
			"cannot get configuration for pin group, missing group config get function in driver\n");
		ret = -ENOTSUPP;
		goto unlock;
	}

	selector = pinctrl_get_group_selector(pctldev, pin_group);
	if (selector < 0) {
		ret = selector;
		goto unlock;
	}

	ret = ops->pin_config_group_get(pctldev, selector, config);

unlock:
	mutex_unlock(&pctldev->mutex);
	return ret;
}

int pinconf_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	int pin;

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		pin = pin_get_from_name(pctldev,
					map->data.configs.group_or_pin);
		if (pin < 0) {
			dev_err(pctldev->dev, "could not map pin config for \"%s\"",
				map->data.configs.group_or_pin);
			return pin;
		}
		setting->data.configs.group_or_pin = pin;
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		pin = pinctrl_get_group_selector(pctldev,
					 map->data.configs.group_or_pin);
		if (pin < 0) {
			dev_err(pctldev->dev, "could not map group config for \"%s\"",
				map->data.configs.group_or_pin);
			return pin;
		}
		setting->data.configs.group_or_pin = pin;
		break;
	default:
		return -EINVAL;
	}

	setting->data.configs.num_configs = map->data.configs.num_configs;
	setting->data.configs.configs = map->data.configs.configs;

	return 0;
}

void pinconf_free_setting(struct pinctrl_setting const *setting)
{
}

int pinconf_apply_setting(struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int ret;

	if (!ops) {
		dev_err(pctldev->dev, "missing confops\n");
		return -EINVAL;
	}

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		if (!ops->pin_config_set) {
			dev_err(pctldev->dev, "missing pin_config_set op\n");
			return -EINVAL;
		}
		ret = ops->pin_config_set(pctldev,
				setting->data.configs.group_or_pin,
				setting->data.configs.configs,
				setting->data.configs.num_configs);
		if (ret < 0) {
			dev_err(pctldev->dev,
				"pin_config_set op failed for pin %d\n",
				setting->data.configs.group_or_pin);
			return ret;
		}
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		if (!ops->pin_config_group_set) {
			dev_err(pctldev->dev,
				"missing pin_config_group_set op\n");
			return -EINVAL;
		}
		ret = ops->pin_config_group_set(pctldev,
				setting->data.configs.group_or_pin,
				setting->data.configs.configs,
				setting->data.configs.num_configs);
		if (ret < 0) {
			dev_err(pctldev->dev,
				"pin_config_group_set op failed for group %d\n",
				setting->data.configs.group_or_pin);
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int pinconf_set_config(struct pinctrl_dev *pctldev, unsigned pin,
		       unsigned long *configs, size_t nconfigs)
{
	const struct pinconf_ops *ops;

	ops = pctldev->desc->confops;
	if (!ops)
		return -ENOTSUPP;

	return ops->pin_config_set(pctldev, pin, configs, nconfigs);
}

#ifdef CONFIG_DEBUG_FS

static void pinconf_show_config(struct seq_file *s, struct pinctrl_dev *pctldev,
		      unsigned long *configs, unsigned num_configs)
{
	const struct pinconf_ops *confops;
	int i;

	if (pctldev)
		confops = pctldev->desc->confops;
	else
		confops = NULL;

	for (i = 0; i < num_configs; i++) {
		seq_puts(s, "config ");
		if (confops && confops->pin_config_config_dbg_show)
			confops->pin_config_config_dbg_show(pctldev, s,
							    configs[i]);
		else
			seq_printf(s, "%08lx", configs[i]);
		seq_putc(s, '\n');
	}
}

void pinconf_show_map(struct seq_file *s, struct pinctrl_map const *map)
{
	struct pinctrl_dev *pctldev;

	pctldev = get_pinctrl_dev_from_devname(map->ctrl_dev_name);

	switch (map->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		seq_puts(s, "pin ");
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		seq_puts(s, "group ");
		break;
	default:
		break;
	}

	seq_printf(s, "%s\n", map->data.configs.group_or_pin);

	pinconf_show_config(s, pctldev, map->data.configs.configs,
			    map->data.configs.num_configs);
}

void pinconf_show_setting(struct seq_file *s,
			  struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	struct pin_desc *desc;

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		desc = pin_desc_get(setting->pctldev,
				    setting->data.configs.group_or_pin);
		seq_printf(s, "pin %s (%d)", desc->name,
			   setting->data.configs.group_or_pin);
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		seq_printf(s, "group %s (%d)",
			   pctlops->get_group_name(pctldev,
					setting->data.configs.group_or_pin),
			   setting->data.configs.group_or_pin);
		break;
	default:
		break;
	}

	/*
	 * FIXME: We should really get the pin controller to dump the config
	 * values, so they can be decoded to something meaningful.
	 */
	pinconf_show_config(s, pctldev, setting->data.configs.configs,
			    setting->data.configs.num_configs);
}

static void pinconf_dump_pin(struct pinctrl_dev *pctldev,
			     struct seq_file *s, int pin)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* no-op when not using generic pin config */
	pinconf_generic_dump_pins(pctldev, s, NULL, pin);
	if (ops && ops->pin_config_dbg_show)
		ops->pin_config_dbg_show(pctldev, s, pin);
}

static int pinconf_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	unsigned i, pin;

	seq_puts(s, "Pin config settings per pin\n");
	seq_puts(s, "Format: pin (name): configs\n");

	mutex_lock(&pctldev->mutex);

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Skip if we cannot search the pin */
		if (!desc)
			continue;

		seq_printf(s, "pin %d (%s): ", pin, desc->name);

		pinconf_dump_pin(pctldev, s, pin);
		seq_putc(s, '\n');
	}

	mutex_unlock(&pctldev->mutex);

	return 0;
}

static void pinconf_dump_group(struct pinctrl_dev *pctldev,
			       struct seq_file *s, unsigned selector,
			       const char *gname)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* no-op when not using generic pin config */
	pinconf_generic_dump_pins(pctldev, s, gname, 0);
	if (ops && ops->pin_config_group_dbg_show)
		ops->pin_config_group_dbg_show(pctldev, s, selector);
}

static int pinconf_groups_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	unsigned ngroups = pctlops->get_groups_count(pctldev);
	unsigned selector = 0;

	seq_puts(s, "Pin config settings per pin group\n");
	seq_puts(s, "Format: group (name): configs\n");

	while (selector < ngroups) {
		const char *gname = pctlops->get_group_name(pctldev, selector);

		seq_printf(s, "%u (%s): ", selector, gname);
		pinconf_dump_group(pctldev, s, selector, gname);
		seq_putc(s, '\n');
		selector++;
	}

	return 0;
}

static int pinconf_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinconf_pins_show, inode->i_private);
}

static int pinconf_groups_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinconf_groups_show, inode->i_private);
}

static const struct file_operations pinconf_pins_ops = {
	.open		= pinconf_pins_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinconf_groups_ops = {
	.open		= pinconf_groups_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define MAX_NAME_LEN 15

struct dbg_cfg {
	enum pinctrl_map_type map_type;
	char dev_name[MAX_NAME_LEN + 1];
	char state_name[MAX_NAME_LEN + 1];
	char pin_name[MAX_NAME_LEN + 1];
};

/*
 * Goal is to keep this structure as global in order to simply read the
 * pinconf-config file after a write to check config is as expected
 */
static struct dbg_cfg pinconf_dbg_conf;

/**
 * pinconf_dbg_config_print() - display the pinctrl config from the pinctrl
 * map, of the dev/pin/state that was last written to pinconf-config file.
 * @s: string filled in  with config description
 * @d: not used
 */
static int pinconf_dbg_config_print(struct seq_file *s, void *d)
{
	struct pinctrl_maps *maps_node;
	const struct pinctrl_map *map;
	const struct pinctrl_map *found = NULL;
	struct pinctrl_dev *pctldev;
	struct dbg_cfg *dbg = &pinconf_dbg_conf;
	int i;

	mutex_lock(&pinctrl_maps_mutex);

	/* Parse the pinctrl map and look for the elected pin/state */
	for_each_maps(maps_node, i, map) {
		if (map->type != dbg->map_type)
			continue;
		if (strcmp(map->dev_name, dbg->dev_name))
			continue;
		if (strcmp(map->name, dbg->state_name))
			continue;

		if (!strcmp(map->data.configs.group_or_pin, dbg->pin_name)) {
			/* We found the right pin */
			found = map;
			break;
		}
	}

	if (!found) {
		seq_printf(s, "No config found for dev/state/pin, expected:\n");
		seq_printf(s, "Searched dev:%s\n", dbg->dev_name);
		seq_printf(s, "Searched state:%s\n", dbg->state_name);
		seq_printf(s, "Searched pin:%s\n", dbg->pin_name);
		seq_printf(s, "Use: modify config_pin <devname> "\
				"<state> <pinname> <value>\n");
		goto exit;
	}

	pctldev = get_pinctrl_dev_from_devname(found->ctrl_dev_name);
	seq_printf(s, "Dev %s has config of %s in state %s:\n",
		   dbg->dev_name, dbg->pin_name, dbg->state_name);
	pinconf_show_config(s, pctldev, found->data.configs.configs,
			    found->data.configs.num_configs);

exit:
	mutex_unlock(&pinctrl_maps_mutex);

	return 0;
}

/**
 * pinconf_dbg_config_write() - modify the pinctrl config in the pinctrl
 * map, of a dev/pin/state entry based on user entries to pinconf-config
 * @user_buf: contains the modification request with expected format:
 *     modify <config> <devicename> <state> <name> <newvalue>
 * modify is literal string, alternatives like add/delete not supported yet
 * <config> is the configuration to be changed. Supported configs are
 *     "config_pin" or "config_group", alternatives like config_mux are not
 *     supported yet.
 * <devicename> <state> <name> are values that should match the pinctrl-maps
 * <newvalue> reflects the new config and is driver dependent
 */
static ssize_t pinconf_dbg_config_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct pinctrl_maps *maps_node;
	const struct pinctrl_map *map;
	const struct pinctrl_map *found = NULL;
	struct pinctrl_dev *pctldev;
	const struct pinconf_ops *confops = NULL;
	struct dbg_cfg *dbg = &pinconf_dbg_conf;
	const struct pinctrl_map_configs *configs;
	char config[MAX_NAME_LEN + 1];
	char buf[128];
	char *b = &buf[0];
	int buf_size;
	char *token;
	int i;

	/* Get userspace string and assure termination */
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/*
	 * need to parse entry and extract parameters:
	 * modify configs_pin devicename state pinname newvalue
	 */

	/* Get arg: 'modify' */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (strcmp(token, "modify"))
		return -EINVAL;

	/*
	 * Get arg type: "config_pin" and "config_group"
	 *                types are supported so far
	 */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (!strcmp(token, "config_pin"))
		dbg->map_type = PIN_MAP_TYPE_CONFIGS_PIN;
	else if (!strcmp(token, "config_group"))
		dbg->map_type = PIN_MAP_TYPE_CONFIGS_GROUP;
	else
		return -EINVAL;

	/* get arg 'device_name' */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (strlen(token) >= MAX_NAME_LEN)
		return -EINVAL;
	strncpy(dbg->dev_name, token, MAX_NAME_LEN);

	/* get arg 'state_name' */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (strlen(token) >= MAX_NAME_LEN)
		return -EINVAL;
	strncpy(dbg->state_name, token, MAX_NAME_LEN);

	/* get arg 'pin_name' */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (strlen(token) >= MAX_NAME_LEN)
		return -EINVAL;
	strncpy(dbg->pin_name, token, MAX_NAME_LEN);

	/* get new_value of config' */
	token = strsep(&b, " ");
	if (!token)
		return -EINVAL;
	if (strlen(token) >= MAX_NAME_LEN)
		return -EINVAL;
	strncpy(config, token, MAX_NAME_LEN);

	mutex_lock(&pinctrl_maps_mutex);

	/* Parse the pinctrl map and look for the selected dev/state/pin */
	for_each_maps(maps_node, i, map) {
		if (strcmp(map->dev_name, dbg->dev_name))
			continue;
		if (map->type != dbg->map_type)
			continue;
		if (strcmp(map->name, dbg->state_name))
			continue;

		/*  we found the right pin / state, so overwrite config */
		if (!strcmp(map->data.configs.group_or_pin, dbg->pin_name)) {
			found = map;
			break;
		}
	}

	if (!found) {
		count = -EINVAL;
		goto exit;
	}

	pctldev = get_pinctrl_dev_from_devname(found->ctrl_dev_name);
	if (pctldev)
		confops = pctldev->desc->confops;

	if (confops && confops->pin_config_dbg_parse_modify) {
		configs = &found->data.configs;
		for (i = 0; i < configs->num_configs; i++) {
			confops->pin_config_dbg_parse_modify(pctldev,
						     config,
						     &configs->configs[i]);
		}
	}

exit:
	mutex_unlock(&pinctrl_maps_mutex);

	return count;
}

static int pinconf_dbg_config_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinconf_dbg_config_print, inode->i_private);
}

static const struct file_operations pinconf_dbg_pinconfig_fops = {
	.open = pinconf_dbg_config_open,
	.write = pinconf_dbg_config_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

void pinconf_init_device_debugfs(struct dentry *devroot,
			 struct pinctrl_dev *pctldev)
{
	debugfs_create_file("pinconf-pins", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinconf_pins_ops);
	debugfs_create_file("pinconf-groups", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinconf_groups_ops);
	debugfs_create_file("pinconf-config",  (S_IRUGO | S_IWUSR | S_IWGRP),
			    devroot, pctldev, &pinconf_dbg_pinconfig_fops);
}

#endif
