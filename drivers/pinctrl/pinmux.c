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

/* List of pin controller handles */
static DEFINE_MUTEX(pinctrl_list_mutex);
static LIST_HEAD(pinctrl_list);

/* Global pinctrl maps */
static struct pinctrl_map *pinctrl_maps;
static unsigned pinctrl_maps_num;

/**
 * struct pinmux_group - group list item for pinmux groups
 * @node: pinmux group list node
 * @group_selector: the group selector for this group
 */
struct pinmux_group {
	struct list_head node;
	unsigned group_selector;
};

/**
 * struct pinctrl - per-device pin control state holder
 * @node: global list node
 * @dev: the device using this pin control handle
 * @usecount: the number of active users of this pin controller setting, used
 *	to keep track of nested use cases
 * @pctldev: pin control device handling this pin control handle
 * @func_selector: the function selector for the pinmux device handling
 *	this pinmux
 * @groups: the group selectors for the pinmux device and
 *	selector combination handling this pinmux, this is a list that
 *	will be traversed on all pinmux operations such as
 *	get/put/enable/disable
 * @mutex: a lock for the pinmux state holder
 */
struct pinctrl {
	struct list_head node;
	struct device *dev;
	unsigned usecount;
	struct pinctrl_dev *pctldev;
	unsigned func_selector;
	struct list_head groups;
	struct mutex mutex;
};

/**
 * struct pinctrl_hog - a list item to stash control hogs
 * @node: pin control hog list node
 * @map: map entry responsible for this hogging
 * @pmx: the pin control hogged by this item
 */
struct pinctrl_hog {
	struct list_head node;
	struct pinctrl_map const *map;
	struct pinctrl *p;
};

/**
 * pin_request() - request a single pin to be muxed in, typically for GPIO
 * @pin: the pin number in the global pin space
 * @function: a functional name to give to this pin, passed to the driver
 *	so it knows what function to mux in, e.g. the string "gpioNN"
 *	means that you want to mux in the pin for use as GPIO number NN
 * @gpio_range: the range matching the GPIO pin if this is a request for a
 *	single GPIO pin
 */
static int pin_request(struct pinctrl_dev *pctldev,
		       int pin, const char *function,
		       struct pinctrl_gpio_range *gpio_range)
{
	struct pin_desc *desc;
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	int status = -EINVAL;

	dev_dbg(pctldev->dev, "request pin %d for %s\n", pin, function);

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		dev_err(pctldev->dev,
			"pin is not registered so it cannot be requested\n");
		goto out;
	}

	if (!function) {
		dev_err(pctldev->dev, "no function name given\n");
		return -EINVAL;
	}

	spin_lock(&desc->lock);
	if (desc->mux_function) {
		spin_unlock(&desc->lock);
		dev_err(pctldev->dev,
			"pin already requested\n");
		goto out;
	}
	desc->mux_function = function;
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
		desc->mux_function = NULL;
		spin_unlock(&desc->lock);
	}
out:
	if (status)
		dev_err(pctldev->dev, "pin-%d (%s) status %d\n",
		       pin, function ? : "?", status);

	return status;
}

/**
 * pin_free() - release a single muxed in pin so something else can be muxed
 * @pctldev: pin controller device handling this pin
 * @pin: the pin to free
 * @gpio_range: the range matching the GPIO pin if this is a request for a
 *	single GPIO pin
 *
 * This function returns a pointer to the function name in use. This is used
 * for callers that dynamically allocate a function name so it can be freed
 * once the pin is free. This is done for GPIO request functions.
 */
static const char *pin_free(struct pinctrl_dev *pctldev, int pin,
			    struct pinctrl_gpio_range *gpio_range)
{
	const struct pinmux_ops *ops = pctldev->desc->pmxops;
	struct pin_desc *desc;
	const char *func;

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
	func = desc->mux_function;
	desc->mux_function = NULL;
	spin_unlock(&desc->lock);
	module_put(pctldev->owner);

	return func;
}

/**
 * pinctrl_request_gpio() - request a single pin to be used in as GPIO
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 *
 * This function should *ONLY* be used from gpiolib-based GPIO drivers,
 * as part of their gpio_request() semantics, platforms and individual drivers
 * shall *NOT* request GPIO pins to be muxed in.
 */
int pinctrl_request_gpio(unsigned gpio)
{
	char gpiostr[16];
	const char *function;
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return -EINVAL;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	/* Conjure some name stating what chip and pin this is taken by */
	snprintf(gpiostr, 15, "%s:%d", range->name, gpio);

	function = kstrdup(gpiostr, GFP_KERNEL);
	if (!function)
		return -EINVAL;

	ret = pin_request(pctldev, pin, function, range);
	if (ret < 0)
		kfree(function);

	return ret;
}
EXPORT_SYMBOL_GPL(pinctrl_request_gpio);

/**
 * pinctrl_free_gpio() - free control on a single pin, currently used as GPIO
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 *
 * This function should *ONLY* be used from gpiolib-based GPIO drivers,
 * as part of their gpio_free() semantics, platforms and individual drivers
 * shall *NOT* request GPIO pins to be muxed out.
 */
void pinctrl_free_gpio(unsigned gpio)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;
	const char *func;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	func = pin_free(pctldev, pin, range);
	kfree(func);
}
EXPORT_SYMBOL_GPL(pinctrl_free_gpio);

static int pinctrl_gpio_direction(unsigned gpio, bool input)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	const struct pinmux_ops *ops;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return ret;

	ops = pctldev->desc->pmxops;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	if (ops->gpio_set_direction)
		ret = ops->gpio_set_direction(pctldev, range, pin, input);
	else
		ret = 0;

	return ret;
}

/**
 * pinctrl_gpio_direction_input() - request a GPIO pin to go into input mode
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 *
 * This function should *ONLY* be used from gpiolib-based GPIO drivers,
 * as part of their gpio_direction_input() semantics, platforms and individual
 * drivers shall *NOT* touch pin control GPIO calls.
 */
int pinctrl_gpio_direction_input(unsigned gpio)
{
	return pinctrl_gpio_direction(gpio, true);
}
EXPORT_SYMBOL_GPL(pinctrl_gpio_direction_input);

/**
 * pinctrl_gpio_direction_output() - request a GPIO pin to go into output mode
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 *
 * This function should *ONLY* be used from gpiolib-based GPIO drivers,
 * as part of their gpio_direction_output() semantics, platforms and individual
 * drivers shall *NOT* touch pin control GPIO calls.
 */
int pinctrl_gpio_direction_output(unsigned gpio)
{
	return pinctrl_gpio_direction(gpio, false);
}
EXPORT_SYMBOL_GPL(pinctrl_gpio_direction_output);

/**
 * pinctrl_register_mappings() - register a set of pin controller mappings
 * @maps: the pincontrol mappings table to register, this should be marked with
 *	__initdata so it can be discarded after boot, this function will
 *	perform a shallow copy for the mapping entries.
 * @num_maps: the number of maps in the mapping table
 *
 * Only call this once during initialization of your machine, the function is
 * tagged as __init and won't be callable after init has completed. The map
 * passed into this function will be owned by the pinmux core and cannot be
 * freed.
 */
int __init pinctrl_register_mappings(struct pinctrl_map const *maps,
				     unsigned num_maps)
{
	void *tmp_maps;
	int i;

	pr_debug("add %d pinmux maps\n", num_maps);

	/* First sanity check the new mapping */
	for (i = 0; i < num_maps; i++) {
		if (!maps[i].name) {
			pr_err("failed to register map %d: no map name given\n",
					i);
			return -EINVAL;
		}

		if (!maps[i].ctrl_dev_name) {
			pr_err("failed to register map %s (%d): no pin control device given\n",
			       maps[i].name, i);
			return -EINVAL;
		}

		if (!maps[i].function) {
			pr_err("failed to register map %s (%d): no function ID given\n",
					maps[i].name, i);
			return -EINVAL;
		}

		if (!maps[i].dev_name)
			pr_debug("add system map %s function %s with no device\n",
				 maps[i].name,
				 maps[i].function);
		else
			pr_debug("register map %s, function %s\n",
				 maps[i].name,
				 maps[i].function);
	}

	/*
	 * Make a copy of the map array - string pointers will end up in the
	 * kernel const section anyway so these do not need to be deep copied.
	 */
	if (!pinctrl_maps_num) {
		/* On first call, just copy them */
		tmp_maps = kmemdup(maps,
				   sizeof(struct pinctrl_map) * num_maps,
				   GFP_KERNEL);
		if (!tmp_maps)
			return -ENOMEM;
	} else {
		/* Subsequent calls, reallocate array to new size */
		size_t oldsize = sizeof(struct pinctrl_map) * pinctrl_maps_num;
		size_t newsize = sizeof(struct pinctrl_map) * num_maps;

		tmp_maps = krealloc(pinctrl_maps,
				    oldsize + newsize, GFP_KERNEL);
		if (!tmp_maps)
			return -ENOMEM;
		memcpy((tmp_maps + oldsize), maps, newsize);
	}

	pinctrl_maps = tmp_maps;
	pinctrl_maps_num += num_maps;
	return 0;
}

/**
 * acquire_pins() - acquire all the pins for a certain function on a pinmux
 * @pctldev: the device to take the pins on
 * @func_selector: the function selector to acquire the pins for
 * @group_selector: the group selector containing the pins to acquire
 */
static int acquire_pins(struct pinctrl_dev *pctldev,
			unsigned func_selector,
			unsigned group_selector)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const struct pinmux_ops *pmxops = pctldev->desc->pmxops;
	const char *func = pmxops->get_function_name(pctldev,
						     func_selector);
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
		ret = pin_request(pctldev, pins[i], func, NULL);
		if (ret) {
			dev_err(pctldev->dev,
				"could not get pin %d for function %s on device %s - conflicting mux mappings?\n",
				pins[i], func ? : "(undefined)",
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
	grp = kmalloc(sizeof(struct pinmux_group), GFP_KERNEL);
	if (!grp)
		return -ENOMEM;
	grp->group_selector = group_selector;
	ret = acquire_pins(pctldev, func_selector, group_selector);
	if (ret) {
		kfree(grp);
		return ret;
	}
	list_add(&grp->node, &p->groups);

	return 0;
}

static void pinmux_free_groups(struct pinctrl *p)
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
 * pinctrl_get() - retrieves the pin controller handle for a certain device
 * @dev: the device to get the pin controller handle for
 * @name: an optional specific control mapping name or NULL, the name is only
 *	needed if you want to have more than one mapping per device, or if you
 *	need an anonymous pin control (not tied to any specific device)
 */
struct pinctrl *pinctrl_get(struct device *dev, const char *name)
{
	struct pinctrl_map const *map = NULL;
	struct pinctrl_dev *pctldev = NULL;
	const char *devname = NULL;
	struct pinctrl *p;
	bool found_map;
	unsigned num_maps = 0;
	int ret = -ENODEV;
	int i;

	/* We must have dev or ID or both */
	if (!dev && !name)
		return ERR_PTR(-EINVAL);

	if (dev)
		devname = dev_name(dev);

	pr_debug("get mux %s for device %s\n", name,
		 devname ? devname : "(none)");

	/*
	 * create the state cookie holder struct pinmux for each
	 * mapping, this is what consumers will get when requesting
	 * a pinmux handle with pinmux_get()
	 */
	p = kzalloc(sizeof(struct pinctrl), GFP_KERNEL);
	if (p == NULL)
		return ERR_PTR(-ENOMEM);
	mutex_init(&p->mutex);
	p->func_selector = UINT_MAX;
	INIT_LIST_HEAD(&p->groups);

	/* Iterate over the pin control maps to locate the right ones */
	for (i = 0; i < pinctrl_maps_num; i++) {
		map = &pinctrl_maps[i];
		found_map = false;

		/*
		 * First, try to find the pctldev given in the map
		 */
		pctldev = get_pinctrl_dev_from_devname(map->ctrl_dev_name);
		if (!pctldev) {
			pr_warning("could not find a pinctrl device for pinmux function %s, fishy, they shall all have one\n",
				   map->function);
			pr_warning("given pinctrl device name: %s",
				   map->ctrl_dev_name);

			/* Continue to check the other mappings anyway... */
			continue;
		}

		pr_debug("in map, found pctldev %s to handle function %s",
			 dev_name(pctldev->dev), map->function);


		/*
		 * If we're looking for a specific named map, this must match,
		 * else we loop and look for the next.
		 */
		if (name != NULL) {
			if (map->name == NULL)
				continue;
			if (strcmp(map->name, name))
				continue;
		}

		/*
		 * This is for the case where no device name is given, we
		 * already know that the function name matches from above
		 * code.
		 */
		if (!map->dev_name && (name != NULL))
			found_map = true;

		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || !strcmp(map->dev_name, devname)))
			/* MATCH! */
			found_map = true;

		/* If this map is applicable, then apply it */
		if (found_map) {
			ret = pinmux_enable_muxmap(pctldev, p, dev,
						   devname, map);
			if (ret) {
				pinmux_free_groups(p);
				kfree(p);
				return ERR_PTR(ret);
			}
			num_maps++;
		}
	}


	/* We should have atleast one map, right */
	if (!num_maps) {
		pr_err("could not find any mux maps for device %s, ID %s\n",
		       devname ? devname : "(anonymous)",
		       name ? name : "(undefined)");
		kfree(p);
		return ERR_PTR(-EINVAL);
	}

	pr_debug("found %u mux maps for device %s, UD %s\n",
		 num_maps,
		 devname ? devname : "(anonymous)",
		 name ? name : "(undefined)");

	/* Add the pinmux to the global list */
	mutex_lock(&pinctrl_list_mutex);
	list_add(&p->node, &pinctrl_list);
	mutex_unlock(&pinctrl_list_mutex);

	return p;
}
EXPORT_SYMBOL_GPL(pinctrl_get);

/**
 * pinctrl_put() - release a previously claimed pin control handle
 * @p: a pin control handle previously claimed by pinctrl_get()
 */
void pinctrl_put(struct pinctrl *p)
{
	if (p == NULL)
		return;

	mutex_lock(&p->mutex);
	if (p->usecount)
		pr_warn("releasing pin control handle with active users!\n");
	/* Free the groups and all acquired pins */
	pinmux_free_groups(p);
	mutex_unlock(&p->mutex);

	/* Remove from list */
	mutex_lock(&pinctrl_list_mutex);
	list_del(&p->node);
	mutex_unlock(&pinctrl_list_mutex);

	kfree(p);
}
EXPORT_SYMBOL_GPL(pinctrl_put);

/**
 * pinctrl_enable() - enable a certain pin controller setting
 * @p: the pin control handle to enable, previously claimed by pinctrl_get()
 */
int pinctrl_enable(struct pinctrl *p)
{
	int ret = 0;

	if (p == NULL)
		return -EINVAL;
	mutex_lock(&p->mutex);
	if (p->usecount++ == 0) {
		struct pinctrl_dev *pctldev = p->pctldev;
		const struct pinmux_ops *ops = pctldev->desc->pmxops;
		struct pinmux_group *grp;

		list_for_each_entry(grp, &p->groups, node) {
			ret = ops->enable(pctldev, p->func_selector,
					  grp->group_selector);
			if (ret) {
				/*
				 * TODO: call disable() on all groups we called
				 * enable() on to this point?
				 */
				p->usecount--;
				break;
			}
		}
	}
	mutex_unlock(&p->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(pinctrl_enable);

/**
 * pinctrl_disable() - disable a certain pin control setting
 * @p: the pin control handle to disable, previously claimed by pinctrl_get()
 */
void pinctrl_disable(struct pinctrl *p)
{
	if (p == NULL)
		return;

	mutex_lock(&p->mutex);
	if (--p->usecount == 0) {
		struct pinctrl_dev *pctldev = p->pctldev;
		const struct pinmux_ops *ops = pctldev->desc->pmxops;
		struct pinmux_group *grp;

		list_for_each_entry(grp, &p->groups, node) {
			ops->disable(pctldev, p->func_selector,
				     grp->group_selector);
		}
	}
	mutex_unlock(&p->mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_disable);

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

/* Hog a single map entry and add to the hoglist */
static int pinctrl_hog_map(struct pinctrl_dev *pctldev,
			   struct pinctrl_map const *map)
{
	struct pinctrl_hog *hog;
	struct pinctrl *p;
	int ret;

	if (map->dev_name) {
		/*
		 * TODO: the day we have device tree support, we can
		 * traverse the device tree and hog to specific device nodes
		 * without any problems, so then we can hog pinmuxes for
		 * all devices that just want a static pin mux at this point.
		 */
		dev_err(pctldev->dev, "map %s wants to hog a non-system pinmux, this is not going to work\n",
				map->name);
		return -EINVAL;
	}

	hog = kzalloc(sizeof(struct pinctrl_hog), GFP_KERNEL);
	if (!hog)
		return -ENOMEM;

	p = pinctrl_get(NULL, map->name);
	if (IS_ERR(p)) {
		kfree(hog);
		dev_err(pctldev->dev,
			"could not get the %s pin control mapping for hogging\n",
			map->name);
		return PTR_ERR(p);
	}

	ret = pinctrl_enable(p);
	if (ret) {
		pinctrl_put(p);
		kfree(hog);
		dev_err(pctldev->dev,
			"could not enable the %s pin control mapping for hogging\n",
			map->name);
		return ret;
	}

	hog->map = map;
	hog->p = p;

	dev_info(pctldev->dev, "hogged map %s, function %s\n", map->name,
		 map->function);
	mutex_lock(&pctldev->pinctrl_hogs_lock);
	list_add(&hog->node, &pctldev->pinctrl_hogs);
	mutex_unlock(&pctldev->pinctrl_hogs_lock);

	return 0;
}

/**
 * pinctrl_hog_maps() - hog specific map entries on controller device
 * @pctldev: the pin control device to hog entries on
 *
 * When the pin controllers are registered, there may be some specific pinmux
 * map entries that need to be hogged, i.e. get+enabled until the system shuts
 * down.
 */
int pinctrl_hog_maps(struct pinctrl_dev *pctldev)
{
	struct device *dev = pctldev->dev;
	const char *devname = dev_name(dev);
	int ret;
	int i;

	INIT_LIST_HEAD(&pctldev->pinctrl_hogs);
	mutex_init(&pctldev->pinctrl_hogs_lock);

	for (i = 0; i < pinctrl_maps_num; i++) {
		struct pinctrl_map const *map = &pinctrl_maps[i];

		if (!map->hog_on_boot)
			continue;

		if (map->ctrl_dev_name &&
		    !strcmp(map->ctrl_dev_name, devname)) {
			/* OK time to hog! */
			ret = pinctrl_hog_map(pctldev, map);
			if (ret)
				return ret;
		}
	}
	return 0;
}

/**
 * pinctrl_unhog_maps() - unhog specific map entries on controller device
 * @pctldev: the pin control device to unhog entries on
 */
void pinctrl_unhog_maps(struct pinctrl_dev *pctldev)
{
	struct list_head *node, *tmp;

	mutex_lock(&pctldev->pinctrl_hogs_lock);
	list_for_each_safe(node, tmp, &pctldev->pinctrl_hogs) {
		struct pinctrl_hog *hog =
			list_entry(node, struct pinctrl_hog, node);
		pinctrl_disable(hog->p);
		pinctrl_put(hog->p);
		list_del(node);
		kfree(hog);
	}
	mutex_unlock(&pctldev->pinctrl_hogs_lock);
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
	seq_puts(s, "Format: pin (name): pinmuxfunction\n");

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
			   desc->mux_function ? desc->mux_function
					      : "UNCLAIMED");
	}

	return 0;
}

static int pinmux_hogs_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	struct pinctrl_hog *hog;

	seq_puts(s, "Pin control map hogs held by device\n");

	list_for_each_entry(hog, &pctldev->pinctrl_hogs, node)
		seq_printf(s, "%s\n", hog->map->name);

	return 0;
}

static int pinmux_show(struct seq_file *s, void *what)
{
	struct pinctrl *p;

	seq_puts(s, "Requested pinmuxes and their maps:\n");
	list_for_each_entry(p, &pinctrl_list, node) {
		struct pinctrl_dev *pctldev = p->pctldev;
		const struct pinmux_ops *pmxops;
		const struct pinctrl_ops *pctlops;
		struct pinmux_group *grp;

		if (!pctldev) {
			seq_puts(s, "NO PIN CONTROLLER DEVICE\n");
			continue;
		}

		pmxops = pctldev->desc->pmxops;
		pctlops = pctldev->desc->pctlops;

		seq_printf(s, "device: %s function: %s (%u),",
			   pinctrl_dev_get_name(p->pctldev),
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

		seq_printf(s, " users: %u map-> %s\n",
			   p->usecount,
			   p->dev ? dev_name(p->dev) : "(system)");
	}

	return 0;
}

static int pinctrl_maps_show(struct seq_file *s, void *what)
{
	int i;

	seq_puts(s, "Pinctrl maps:\n");

	for (i = 0; i < pinctrl_maps_num; i++) {
		struct pinctrl_map const *map = &pinctrl_maps[i];

		seq_printf(s, "%s:\n", map->name);
		if (map->dev_name)
			seq_printf(s, "  device: %s\n",
				   map->dev_name);
		else
			seq_printf(s, "  SYSTEM MUX\n");
		seq_printf(s, "  controlling device %s\n",
			   map->ctrl_dev_name);
		seq_printf(s, "  function: %s\n", map->function);
		seq_printf(s, "  group: %s\n", map->group ? map->group :
			   "(default)");
	}
	return 0;
}

static int pinmux_functions_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_functions_show, inode->i_private);
}

static int pinmux_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_pins_show, inode->i_private);
}

static int pinmux_hogs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_hogs_show, inode->i_private);
}

static int pinmux_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_show, NULL);
}

static int pinctrl_maps_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_maps_show, NULL);
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

static const struct file_operations pinmux_hogs_ops = {
	.open		= pinmux_hogs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinmux_ops = {
	.open		= pinmux_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinctrl_maps_ops = {
	.open		= pinctrl_maps_open,
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
	debugfs_create_file("pinmux-hogs", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinmux_hogs_ops);
}

void pinmux_init_debugfs(struct dentry *subsys_root)
{
	debugfs_create_file("pinmuxes", S_IFREG | S_IRUGO,
			    subsys_root, NULL, &pinmux_ops);
	debugfs_create_file("pinctrl-maps", S_IFREG | S_IRUGO,
			    subsys_root, NULL, &pinctrl_maps_ops);
}

#endif /* CONFIG_DEBUG_FS */
