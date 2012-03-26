/*
 * Core driver for the pin muxing portions of the pin control subsystem
 *
 * Copyright (C) 2011-2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * Copyright (C) 2012 NVIDIA CORPORATION. All rights reserved.
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#define pr_fmt(fmt) "pinmux core: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinmux.h>
#include "core.h"
#include "pinmux.h"

int pinmux_check_ops(struct pinctrl_dev *pctldev)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned selector = 0;

	/* Check that we implement required operations */
	if (!ops->list_functions ||
	    !ops->get_function_name ||
	    !ops->get_function_groups ||
	    !ops->enable ||
	    !ops->disable)
		return -EINVAL;

	/* Check that all functions registered have names */
	while (ops->list_functions(pctldev, selector) >= 0) {
		const char *fname = ops->get_function_name(pctldev,
							   selector);
		if (!fname) {
			pr_err("pinmux ops has no name for function%u\n",
				selector);
			return -EINVAL;
		}
		selector++;
	}

	return 0;
}

int pinmux_validate_map(struct pinctrl_map const *map, int i)
{
	if (!map->data.mux.function) {
		pr_err("failed to register map %s (%d): no function given\n",
		       map->name, i);
		return -EINVAL;
	}

	return 0;
}

/**
 * pin_request() - request a single pin to be muxed in, typically for GPIO
 * @pin: the pin number in the global pin space
 * @owner: a representation of the owner of this pin; typically the device
 *	name that controls its mux function, or the requested GPIO name
 * @gpio_range: the range matching the GPIO pin if this is a request for a
 *	single GPIO pin
 */
static int pin_request(struct pinctrl_dev *pctldev,
		       int pin, const char *owner,
		       struct pinctrl_gpio_range *gpio_range)
{
	struct pin_desc *desc;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	int status = -EINVAL;

	dev_dbg(pctldev->dev, "request pin %d for %s\n", pin, owner);

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		dev_err(pctldev->dev,
			"pin is not registered so it cannot be requested\n");
		goto out;
	}

	if (gpio_range) {
		/* There's no need to support multiple GPIO requests */
		if (desc->gpio_owner) {
			dev_err(pctldev->dev,
				"pin already requested\n");
			goto out;
		}

		desc->gpio_owner = owner;
	} else {
		if (desc->mux_usecount && strcmp(desc->mux_owner, owner)) {
			dev_err(pctldev->dev,
				"pin already requested\n");
			goto out;
		}

		desc->mux_usecount++;
		if (desc->mux_usecount > 1)
			return 0;

		desc->mux_owner = owner;
	}

	/* Let each pin increase references to this module */
	if (!try_module_get(pctldev->owner)) {
		dev_err(pctldev->dev,
			"could not increase module refcount for pin %d\n",
			pin);
		status = -EINVAL;
		goto out_free_pin;
	}

	/*
	 * If there is no kind of request function for the pin we just assume
	 * we got it by default and proceed.
	 */
	if (gpio_range && ops->gpio_request_enable)
		/* This requests and enables a single GPIO pin */
		status = ops->gpio_request_enable(pctldev, gpio_range, pin);
	else if (ops->request)
		status = ops->request(pctldev, pin);
	else
		status = 0;

	if (status) {
		dev_err(pctldev->dev, "->request on device %s failed for pin %d\n",
		       pctldev->desc->name, pin);
		module_put(pctldev->owner);
	}

out_free_pin:
	if (status) {
		if (gpio_range) {
			desc->gpio_owner = NULL;
		} else {
			desc->mux_usecount--;
			if (!desc->mux_usecount)
				desc->mux_owner = NULL;
		}
	}
out:
	if (status)
		dev_err(pctldev->dev, "pin-%d (%s) status %d\n",
		       pin, owner, status);

	return status;
}

/**
 * pin_free() - release a single muxed in pin so something else can be muxed
 * @pctldev: pin controller device handling this pin
 * @pin: the pin to free
 * @gpio_range: the range matching the GPIO pin if this is a request for a
 *	single GPIO pin
 *
 * This function returns a pointer to the previous owner. This is used
 * for callers that dynamically allocate an owner name so it can be freed
 * once the pin is free. This is done for GPIO request functions.
 */
static const char *pin_free(struct pinctrl_dev *pctldev, int pin,
			    struct pinctrl_gpio_range *gpio_range)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	struct pin_desc *desc;
	const char *owner;

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		dev_err(pctldev->dev,
			"pin is not registered so it cannot be freed\n");
		return NULL;
	}

	if (!gpio_range) {
		desc->mux_usecount--;
		if (desc->mux_usecount)
			return NULL;
	}

	/*
	 * If there is no kind of request function for the pin we just assume
	 * we got it by default and proceed.
	 */
	if (gpio_range && ops->gpio_disable_free)
		ops->gpio_disable_free(pctldev, gpio_range, pin);
	else if (ops->free)
		ops->free(pctldev, pin);

	if (gpio_range) {
		owner = desc->gpio_owner;
		desc->gpio_owner = NULL;
	} else {
		owner = desc->mux_owner;
		desc->mux_owner = NULL;
		desc->mux_setting = NULL;
	}

	module_put(pctldev->owner);

	return owner;
}

/**
 * pinmux_request_gpio() - request pinmuxing for a GPIO pin
 * @pctldev: pin controller device affected
 * @pin: the pin to mux in for GPIO
 * @range: the applicable GPIO range
 */
int pinmux_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, unsigned gpio)
{
	char gpiostr[16];
	const char *owner;
	int ret;

	/* Conjure some name stating what chip and pin this is taken by */
	snprintf(gpiostr, 15, "%s:%d", range->name, gpio);

	owner = kstrdup(gpiostr, GFP_KERNEL);
	if (!owner)
		return -EINVAL;

	ret = pin_request(pctldev, pin, owner, range);
	if (ret < 0)
		kfree(owner);

	return ret;
}

/**
 * pinmux_free_gpio() - release a pin from GPIO muxing
 * @pctldev: the pin controller device for the pin
 * @pin: the affected currently GPIO-muxed in pin
 * @range: applicable GPIO range
 */
void pinmux_free_gpio(struct pinctrl_dev *pctldev, unsigned pin,
		      struct pinctrl_gpio_range *range)
{
	const char *owner;

	owner = pin_free(pctldev, pin, range);
	kfree(owner);
}

/**
 * pinmux_gpio_direction() - set the direction of a single muxed-in GPIO pin
 * @pctldev: the pin controller handling this pin
 * @range: applicable GPIO range
 * @pin: the affected GPIO pin in this controller
 * @input: true if we set the pin as input, false for output
 */
int pinmux_gpio_direction(struct pinctrl_dev *pctldev,
			  struct pinctrl_gpio_range *range,
			  unsigned pin, bool input)
{
	const struct pinmux_ops *ops;
	int ret;

	ops = pctldev->desc->pmxops;

	if (ops->gpio_set_direction)
		ret = ops->gpio_set_direction(pctldev, range, pin, input);
	else
		ret = 0;

	return ret;
}

static int pinmux_func_name_to_selector(struct pinctrl_dev *pctldev,
					const char *function)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned selector = 0;

	/* See if this pctldev has this function */
	while (ops->list_functions(pctldev, selector) >= 0) {
		const char *fname = ops->get_function_name(pctldev,
							   selector);

		if (!strcmp(function, fname))
			return selector;

		selector++;
	}

	pr_err("%s does not support function %s\n",
	       pinctrl_dev_get_name(pctldev), function);
	return -EINVAL;
}

int pinmux_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	char const * const *groups;
	unsigned num_groups;
	int ret;
	const char *group;
	int i;
	const unsigned *pins;
	unsigned num_pins;

	setting->data.mux.func =
		pinmux_func_name_to_selector(pctldev, map->data.mux.function);
	if (setting->data.mux.func < 0)
		return setting->data.mux.func;

	ret = pmxops->get_function_groups(pctldev, setting->data.mux.func,
					  &groups, &num_groups);
	if (ret < 0)
		return ret;
	if (!num_groups)
		return -EINVAL;

	if (map->data.mux.group) {
		bool found = false;
		group = map->data.mux.group;
		for (i = 0; i < num_groups; i++) {
			if (!strcmp(group, groups[i])) {
				found = true;
				break;
			}
		}
		if (!found)
			return -EINVAL;
	} else {
		group = groups[0];
	}

	setting->data.mux.group = pinctrl_get_group_selector(pctldev, group);
	if (setting->data.mux.group < 0)
		return setting->data.mux.group;

	ret = pctlops->get_group_pins(pctldev, setting->data.mux.group, &pins,
				      &num_pins);
	if (ret) {
		dev_err(pctldev->dev,
			"could not get pins for device %s group selector %d\n",
			pinctrl_dev_get_name(pctldev), setting->data.mux.group);
			return -ENODEV;
	}

	/* Try to allocate all pins in this group, one by one */
	for (i = 0; i < num_pins; i++) {
		ret = pin_request(pctldev, pins[i], map->dev_name, NULL);
		if (ret) {
			dev_err(pctldev->dev,
				"could not get request pin %d on device %s\n",
				pins[i], pinctrl_dev_get_name(pctldev));
			/* On error release all taken pins */
			i--; /* this pin just failed */
			for (; i >= 0; i--)
				pin_free(pctldev, pins[i], NULL);
			return -ENODEV;
		}
	}

	return 0;
}

void pinmux_free_setting(struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const unsigned *pins;
	unsigned num_pins;
	int ret;
	int i;

	ret = pctlops->get_group_pins(pctldev, setting->data.mux.group,
				      &pins, &num_pins);
	if (ret) {
		dev_err(pctldev->dev,
			"could not get pins for device %s group selector %d\n",
			pinctrl_dev_get_name(pctldev), setting->data.mux.group);
		return;
	}

	for (i = 0; i < num_pins; i++)
		pin_free(pctldev, pins[i], NULL);
}

int pinmux_enable_setting(struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	int ret;
	const unsigned *pins;
	unsigned num_pins;
	int i;
	struct pin_desc *desc;

	ret = pctlops->get_group_pins(pctldev, setting->data.mux.group,
				      &pins, &num_pins);
	if (ret) {
		/* errors only affect debug data, so just warn */
		dev_warn(pctldev->dev,
			 "could not get pins for group selector %d\n",
			 setting->data.mux.group);
		num_pins = 0;
	}

	for (i = 0; i < num_pins; i++) {
		desc = pin_desc_get(pctldev, pins[i]);
		if (desc == NULL) {
			dev_warn(pctldev->dev,
				 "could not get pin desc for pin %d\n",
				 pins[i]);
			continue;
		}
		desc->mux_setting = &(setting->data.mux);
	}

	return ops->enable(pctldev, setting->data.mux.func,
			   setting->data.mux.group);
}

void pinmux_disable_setting(struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	int ret;
	const unsigned *pins;
	unsigned num_pins;
	int i;
	struct pin_desc *desc;

	ret = pctlops->get_group_pins(pctldev, setting->data.mux.group,
				      &pins, &num_pins);
	if (ret) {
		/* errors only affect debug data, so just warn */
		dev_warn(pctldev->dev,
			 "could not get pins for group selector %d\n",
			 setting->data.mux.group);
		num_pins = 0;
	}

	for (i = 0; i < num_pins; i++) {
		desc = pin_desc_get(pctldev, pins[i]);
		if (desc == NULL) {
			dev_warn(pctldev->dev,
				 "could not get pin desc for pin %d\n",
				 pins[i]);
			continue;
		}
		desc->mux_setting = NULL;
	}

	ops->disable(pctldev, setting->data.mux.func, setting->data.mux.group);
}

#ifdef CONFIG_DEBUG_FS

/* Called from pincontrol core */
static int pinmux_functions_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	unsigned func_selector = 0;

	mutex_lock(&pinctrl_mutex);

	while (pmxops->list_functions(pctldev, func_selector) >= 0) {
		const char *func = pmxops->get_function_name(pctldev,
							  func_selector);
		const char * const *groups;
		unsigned num_groups;
		int ret;
		int i;

		ret = pmxops->get_function_groups(pctldev, func_selector,
						  &groups, &num_groups);
		if (ret)
			seq_printf(s, "function %s: COULD NOT GET GROUPS\n",
				   func);

		seq_printf(s, "function: %s, groups = [ ", func);
		for (i = 0; i < num_groups; i++)
			seq_printf(s, "%s ", groups[i]);
		seq_puts(s, "]\n");

		func_selector++;
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinmux_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	unsigned i, pin;

	seq_puts(s, "Pinmux settings per pin\n");
	seq_puts(s, "Format: pin (name): mux_owner gpio_owner hog?\n");

	mutex_lock(&pinctrl_mutex);

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;
		bool is_hog = false;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Skip if we cannot search the pin */
		if (desc == NULL)
			continue;

		if (desc->mux_owner &&
		    !strcmp(desc->mux_owner, pinctrl_dev_get_name(pctldev)))
			is_hog = true;

		seq_printf(s, "pin %d (%s): %s %s%s", pin,
			   desc->name ? desc->name : "unnamed",
			   desc->mux_owner ? desc->mux_owner
				: "(MUX UNCLAIMED)",
			   desc->gpio_owner ? desc->gpio_owner
				: "(GPIO UNCLAIMED)",
			   is_hog ? " (HOG)" : "");

		if (desc->mux_setting)
			seq_printf(s, " function %s group %s\n",
				   pmxops->get_function_name(pctldev,
					desc->mux_setting->func),
				   pctlops->get_group_name(pctldev,
					desc->mux_setting->group));
		else
			seq_printf(s, "\n");
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

void pinmux_show_map(struct seq_file *s, struct pinctrl_map const *map)
{
	seq_printf(s, "group %s\nfunction %s\n",
		map->data.mux.group ? map->data.mux.group : "(default)",
		map->data.mux.function);
}

void pinmux_show_setting(struct seq_file *s,
			 struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;

	seq_printf(s, "group: %s (%u) function: %s (%u)\n",
		   pctlops->get_group_name(pctldev, setting->data.mux.group),
		   setting->data.mux.group,
		   pmxops->get_function_name(pctldev, setting->data.mux.func),
		   setting->data.mux.func);
}

static int pinmux_functions_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_functions_show, inode->i_private);
}

static int pinmux_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_pins_show, inode->i_private);
}

static const struct file_operations pinmux_functions_ops = {
	.open		= pinmux_functions_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinmux_pins_ops = {
	.open		= pinmux_pins_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void pinmux_init_device_debugfs(struct dentry *devroot,
			 struct pinctrl_dev *pctldev)
{
	debugfs_create_file("pinmux-functions", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinmux_functions_ops);
	debugfs_create_file("pinmux-pins", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinmux_pins_ops);
}

#endif /* CONFIG_DEBUG_FS */
