/*
 * Core driver for the pin muxing portions of the pin control subsystem
 *
 * Copyright (C) 2011-2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
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
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinmux.h>
#include "core.h"
#include "pinmux.h"

/**
 * struct pinmux_group - group list item for pinmux groups
 * @node: pinmux group list node
 * @group_selector: the group selector for this group
 */
struct pinmux_group {
	struct list_head node;
	unsigned group_selector;
};

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

	spin_lock(&desc->lock);
	if (desc->owner && strcmp(desc->owner, owner)) {
		spin_unlock(&desc->lock);
		dev_err(pctldev->dev,
			"pin already requested\n");
		goto out;
	}
	desc->owner = owner;
	spin_unlock(&desc->lock);

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

	if (status)
		dev_err(pctldev->dev, "->request on device %s failed for pin %d\n",
		       pctldev->desc->name, pin);
out_free_pin:
	if (status) {
		spin_lock(&desc->lock);
		desc->owner = NULL;
		spin_unlock(&desc->lock);
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

	/*
	 * If there is no kind of request function for the pin we just assume
	 * we got it by default and proceed.
	 */
	if (gpio_range && ops->gpio_disable_free)
		ops->gpio_disable_free(pctldev, gpio_range, pin);
	else if (ops->free)
		ops->free(pctldev, pin);

	spin_lock(&desc->lock);
	owner = desc->owner;
	desc->owner = NULL;
	spin_unlock(&desc->lock);
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

/**
 * acquire_pins() - acquire all the pins for a certain function on a pinmux
 * @pctldev: the device to take the pins on
 * @owner: a representation of the owner of this pin; typically the device
 *	name that controls its mux function
 * @group_selector: the group selector containing the pins to acquire
 */
static int acquire_pins(struct pinctrl_dev *pctldev,
			const char *owner,
			unsigned group_selector)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const unsigned *pins;
	unsigned num_pins;
	int ret;
	int i;

	ret = pctlops->get_group_pins(pctldev, group_selector,
				      &pins, &num_pins);
	if (ret)
		return ret;

	dev_dbg(pctldev->dev, "requesting the %u pins from group %u\n",
		num_pins, group_selector);

	/* Try to allocate all pins in this group, one by one */
	for (i = 0; i < num_pins; i++) {
		ret = pin_request(pctldev, pins[i], owner, NULL);
		if (ret) {
			dev_err(pctldev->dev,
				"could not get request pin %d on device %s - conflicting mux mappings?\n",
				pins[i],
				pinctrl_dev_get_name(pctldev));
			/* On error release all taken pins */
			i--; /* this pin just failed */
			for (; i >= 0; i--)
				pin_free(pctldev, pins[i], NULL);
			return -ENODEV;
		}
	}
	return 0;
}

/**
 * release_pins() - release pins taken by earlier acquirement
 * @pctldev: the device to free the pins on
 * @group_selector: the group selector containing the pins to free
 */
static void release_pins(struct pinctrl_dev *pctldev,
			 unsigned group_selector)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const unsigned *pins;
	unsigned num_pins;
	int ret;
	int i;

	ret = pctlops->get_group_pins(pctldev, group_selector,
				      &pins, &num_pins);
	if (ret) {
		dev_err(pctldev->dev, "could not get pins to release for group selector %d\n",
			group_selector);
		return;
	}
	for (i = 0; i < num_pins; i++)
		pin_free(pctldev, pins[i], NULL);
}

/**
 * pinmux_check_pin_group() - check function and pin group combo
 * @pctldev: device to check the pin group vs function for
 * @func_selector: the function selector to check the pin group for, we have
 *	already looked this up in the calling function
 * @pin_group: the pin group to match to the function
 *
 * This function will check that the pinmux driver can supply the
 * selected pin group for a certain function, returns the group selector if
 * the group and function selector will work fine together, else returns
 * negative
 */
static int pinmux_check_pin_group(struct pinctrl_dev *pctldev,
				  unsigned func_selector,
				  const char *pin_group)
{
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	int ret;

	/*
	 * If the driver does not support different pin groups for the
	 * functions, we only support group 0, and assume this exists.
	 */
	if (!pctlops || !pctlops->list_groups)
		return 0;

	/*
	 * Passing NULL (no specific group) will select the first and
	 * hopefully only group of pins available for this function.
	 */
	if (!pin_group) {
		char const * const *groups;
		unsigned num_groups;

		ret = pmxops->get_function_groups(pctldev, func_selector,
						  &groups, &num_groups);
		if (ret)
			return ret;
		if (num_groups < 1)
			return -EINVAL;
		ret = pinctrl_get_group_selector(pctldev, groups[0]);
		if (ret < 0) {
			dev_err(pctldev->dev,
				"function %s wants group %s but the pin controller does not seem to have that group\n",
				pmxops->get_function_name(pctldev, func_selector),
				groups[0]);
			return ret;
		}

		if (num_groups > 1)
			dev_dbg(pctldev->dev,
				"function %s support more than one group, default-selecting first group %s (%d)\n",
				pmxops->get_function_name(pctldev, func_selector),
				groups[0],
				ret);

		return ret;
	}

	dev_dbg(pctldev->dev,
		"check if we have pin group %s on controller %s\n",
		pin_group, pinctrl_dev_get_name(pctldev));

	ret = pinctrl_get_group_selector(pctldev, pin_group);
	if (ret < 0) {
		dev_dbg(pctldev->dev,
			"%s does not support pin group %s with function %s\n",
			pinctrl_dev_get_name(pctldev),
			pin_group,
			pmxops->get_function_name(pctldev, func_selector));
	}
	return ret;
}

/**
 * pinmux_search_function() - check pin control driver for a certain function
 * @pctldev: device to check for function and position
 * @map: function map containing the function and position to look for
 * @func_selector: returns the applicable function selector if found
 * @group_selector: returns the applicable group selector if found
 *
 * This will search the pinmux driver for an applicable
 * function with a specific pin group, returns 0 if these can be mapped
 * negative otherwise
 */
static int pinmux_search_function(struct pinctrl_dev *pctldev,
				  struct pinctrl_map const *map,
				  unsigned *func_selector,
				  unsigned *group_selector)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	unsigned selector = 0;

	/* See if this pctldev has this function */
	while (ops->list_functions(pctldev, selector) >= 0) {
		const char *fname = ops->get_function_name(pctldev,
							   selector);
		int ret;

		if (!strcmp(map->function, fname)) {
			/* Found the function, check pin group */
			ret = pinmux_check_pin_group(pctldev, selector,
						     map->group);
			if (ret < 0)
				return ret;

			/* This function and group selector can be used */
			*func_selector = selector;
			*group_selector = ret;
			return 0;

		}
		selector++;
	}

	pr_err("%s does not support function %s\n",
	       pinctrl_dev_get_name(pctldev), map->function);
	return -EINVAL;
}

/**
 * pinmux_enable_muxmap() - enable a map entry for a certain pinmux
 */
static int pinmux_enable_muxmap(struct pinctrl_dev *pctldev,
				struct pinctrl *p,
				struct device *dev,
				const char *devname,
				struct pinctrl_map const *map)
{
	unsigned func_selector;
	unsigned group_selector;
	struct pinmux_group *grp;
	int ret;

	/*
	 * Note that we're not locking the pinmux mutex here, because
	 * this is only called at pinmux initialization time when it
	 * has not been added to any list and thus is not reachable
	 * by anyone else.
	 */

	if (p->pctldev && p->pctldev != pctldev) {
		dev_err(pctldev->dev,
			"different pin control devices given for device %s, function %s\n",
			devname, map->function);
		return -EINVAL;
	}
	p->dev = dev;
	p->pctldev = pctldev;

	/* Now go into the driver and try to match a function and group */
	ret = pinmux_search_function(pctldev, map, &func_selector,
				     &group_selector);
	if (ret < 0)
		return ret;

	/*
	 * If the function selector is already set, it needs to be identical,
	 * we support several groups with one function but not several
	 * functions with one or several groups in the same pinmux.
	 */
	if (p->func_selector != UINT_MAX &&
	    p->func_selector != func_selector) {
		dev_err(pctldev->dev,
			"dual function defines in the map for device %s\n",
		       devname);
		return -EINVAL;
	}
	p->func_selector = func_selector;

	/* Now add this group selector, we may have many of them */
	grp = kmalloc(sizeof(*grp), GFP_KERNEL);
	if (!grp)
		return -ENOMEM;
	grp->group_selector = group_selector;
	ret = acquire_pins(pctldev, devname, group_selector);
	if (ret) {
		kfree(grp);
		return ret;
	}
	list_add_tail(&grp->node, &p->groups);

	return 0;
}

/**
 * pinmux_apply_muxmap() - apply a certain mux mapping entry
 */
int pinmux_apply_muxmap(struct pinctrl_dev *pctldev,
			struct pinctrl *p,
			struct device *dev,
			const char *devname,
			struct pinctrl_map const *map)
{
	int ret;

	ret = pinmux_enable_muxmap(pctldev, p, dev,
				   devname, map);
	if (ret) {
		pinmux_put(p);
		return ret;
	}

	return 0;
}

/**
 * pinmux_put() - free up the pinmux portions of a pin controller handle
 */
void pinmux_put(struct pinctrl *p)
{
	struct list_head *node, *tmp;

	list_for_each_safe(node, tmp, &p->groups) {
		struct pinmux_group *grp =
			list_entry(node, struct pinmux_group, node);
		/* Release all pins taken by this group */
		release_pins(p->pctldev, grp->group_selector);
		list_del(node);
		kfree(grp);
	}
}

/**
 * pinmux_enable() - enable the pinmux portion of a pin control handle
 */
int pinmux_enable(struct pinctrl *p)
{
	struct pinctrl_dev *pctldev = p->pctldev;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	struct pinmux_group *grp;
	int ret;

	list_for_each_entry(grp, &p->groups, node) {
		ret = ops->enable(pctldev, p->func_selector,
				  grp->group_selector);
		if (ret)
			/*
			 * TODO: call disable() on all groups we called
			 * enable() on to this point?
			 */
			return ret;
	}
	return 0;
}

/**
 * pinmux_disable() - disable the pinmux portions of a pin control handle
 */
void pinmux_disable(struct pinctrl *p)
{
	struct pinctrl_dev *pctldev = p->pctldev;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	struct pinmux_group *grp;

	list_for_each_entry(grp, &p->groups, node) {
		ops->disable(pctldev, p->func_selector,
			     grp->group_selector);
	}
}

#ifdef CONFIG_DEBUG_FS

/* Called from pincontrol core */
static int pinmux_functions_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	unsigned func_selector = 0;

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

	return 0;
}

static int pinmux_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	unsigned i, pin;

	seq_puts(s, "Pinmux settings per pin\n");
	seq_puts(s, "Format: pin (name): owner\n");

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {

		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Skip if we cannot search the pin */
		if (desc == NULL)
			continue;

		seq_printf(s, "pin %d (%s): %s\n", pin,
			   desc->name ? desc->name : "unnamed",
			   desc->owner ? desc->owner : "UNCLAIMED");
	}

	return 0;
}

void pinmux_dbg_show(struct seq_file *s, struct pinctrl *p)
{
	struct pinctrl_dev *pctldev = p->pctldev;
	const struct pinmux_ops *pmxops;
	const struct pinctrl_ops *pctlops;
	struct pinmux_group *grp;

	pmxops = pctldev->desc->pmxops;
	pctlops = pctldev->desc->pctlops;

	seq_printf(s, " function: %s (%u),",
		   pmxops->get_function_name(pctldev,
					     p->func_selector),
		   p->func_selector);

	seq_printf(s, " groups: [");
	list_for_each_entry(grp, &p->groups, node) {
		seq_printf(s, " %s (%u)",
			   pctlops->get_group_name(pctldev,
						   grp->group_selector),
			   grp->group_selector);
	}
	seq_printf(s, " ]");
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
