/*
 * Core driver for the pin control subsystem
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
#define pr_fmt(fmt) "pinctrl core: " fmt

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

/**
 * struct pinctrl_maps - a list item containing part of the mapping table
 * @node: mapping table list node
 * @maps: array of mapping table entries
 * @num_maps: the number of entries in @maps
 */
struct pinctrl_maps {
	struct list_head node;
	struct pinctrl_map const *maps;
	unsigned num_maps;
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

/* Global list of pin control devices */
static DEFINE_MUTEX(pinctrldev_list_mutex);
static LIST_HEAD(pinctrldev_list);

/* List of pin controller handles */
static DEFINE_MUTEX(pinctrl_list_mutex);
static LIST_HEAD(pinctrl_list);

/* Global pinctrl maps */
static DEFINE_MUTEX(pinctrl_maps_mutex);
static LIST_HEAD(pinctrl_maps);

#define for_each_maps(_maps_node_, _i_, _map_) \
	list_for_each_entry(_maps_node_, &pinctrl_maps, node) \
		for (_i_ = 0, _map_ = &_maps_node_->maps[_i_]; \
			_i_ < _maps_node_->num_maps; \
			i++, _map_ = &_maps_node_->maps[_i_])

const char *pinctrl_dev_get_name(struct pinctrl_dev *pctldev)
{
	/* We're not allowed to register devices without name */
	return pctldev->desc->name;
}
EXPORT_SYMBOL_GPL(pinctrl_dev_get_name);

void *pinctrl_dev_get_drvdata(struct pinctrl_dev *pctldev)
{
	return pctldev->driver_data;
}
EXPORT_SYMBOL_GPL(pinctrl_dev_get_drvdata);

/**
 * get_pinctrl_dev_from_devname() - look up pin controller device
 * @devname: the name of a device instance, as returned by dev_name()
 *
 * Looks up a pin control device matching a certain device name or pure device
 * pointer, the pure device pointer will take precedence.
 */
struct pinctrl_dev *get_pinctrl_dev_from_devname(const char *devname)
{
	struct pinctrl_dev *pctldev = NULL;
	bool found = false;

	if (!devname)
		return NULL;

	mutex_lock(&pinctrldev_list_mutex);
	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		if (!strcmp(dev_name(pctldev->dev), devname)) {
			/* Matched on device name */
			found = true;
			break;
		}
	}
	mutex_unlock(&pinctrldev_list_mutex);

	return found ? pctldev : NULL;
}

struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev, unsigned int pin)
{
	struct pin_desc *pindesc;
	unsigned long flags;

	spin_lock_irqsave(&pctldev->pin_desc_tree_lock, flags);
	pindesc = radix_tree_lookup(&pctldev->pin_desc_tree, pin);
	spin_unlock_irqrestore(&pctldev->pin_desc_tree_lock, flags);

	return pindesc;
}

/**
 * pin_get_from_name() - look up a pin number from a name
 * @pctldev: the pin control device to lookup the pin on
 * @name: the name of the pin to look up
 */
int pin_get_from_name(struct pinctrl_dev *pctldev, const char *name)
{
	unsigned i, pin;

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Pin space may be sparse */
		if (desc == NULL)
			continue;
		if (desc->name && !strcmp(name, desc->name))
			return pin;
	}

	return -EINVAL;
}

/**
 * pin_is_valid() - check if pin exists on controller
 * @pctldev: the pin control device to check the pin on
 * @pin: pin to check, use the local pin controller index number
 *
 * This tells us whether a certain pin exist on a certain pin controller or
 * not. Pin lists may be sparse, so some pins may not exist.
 */
bool pin_is_valid(struct pinctrl_dev *pctldev, int pin)
{
	struct pin_desc *pindesc;

	if (pin < 0)
		return false;

	pindesc = pin_desc_get(pctldev, pin);
	if (pindesc == NULL)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(pin_is_valid);

/* Deletes a range of pin descriptors */
static void pinctrl_free_pindescs(struct pinctrl_dev *pctldev,
				  const struct pinctrl_pin_desc *pins,
				  unsigned num_pins)
{
	int i;

	spin_lock(&pctldev->pin_desc_tree_lock);
	for (i = 0; i < num_pins; i++) {
		struct pin_desc *pindesc;

		pindesc = radix_tree_lookup(&pctldev->pin_desc_tree,
					    pins[i].number);
		if (pindesc != NULL) {
			radix_tree_delete(&pctldev->pin_desc_tree,
					  pins[i].number);
			if (pindesc->dynamic_name)
				kfree(pindesc->name);
		}
		kfree(pindesc);
	}
	spin_unlock(&pctldev->pin_desc_tree_lock);
}

static int pinctrl_register_one_pin(struct pinctrl_dev *pctldev,
				    unsigned number, const char *name)
{
	struct pin_desc *pindesc;

	pindesc = pin_desc_get(pctldev, number);
	if (pindesc != NULL) {
		pr_err("pin %d already registered on %s\n", number,
		       pctldev->desc->name);
		return -EINVAL;
	}

	pindesc = kzalloc(sizeof(*pindesc), GFP_KERNEL);
	if (pindesc == NULL)
		return -ENOMEM;

	spin_lock_init(&pindesc->lock);

	/* Set owner */
	pindesc->pctldev = pctldev;

	/* Copy basic pin info */
	if (name) {
		pindesc->name = name;
	} else {
		pindesc->name = kasprintf(GFP_KERNEL, "PIN%u", number);
		if (pindesc->name == NULL)
			return -ENOMEM;
		pindesc->dynamic_name = true;
	}

	spin_lock(&pctldev->pin_desc_tree_lock);
	radix_tree_insert(&pctldev->pin_desc_tree, number, pindesc);
	spin_unlock(&pctldev->pin_desc_tree_lock);
	pr_debug("registered pin %d (%s) on %s\n",
		 number, pindesc->name, pctldev->desc->name);
	return 0;
}

static int pinctrl_register_pins(struct pinctrl_dev *pctldev,
				 struct pinctrl_pin_desc const *pins,
				 unsigned num_descs)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < num_descs; i++) {
		ret = pinctrl_register_one_pin(pctldev,
					       pins[i].number, pins[i].name);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * pinctrl_match_gpio_range() - check if a certain GPIO pin is in range
 * @pctldev: pin controller device to check
 * @gpio: gpio pin to check taken from the global GPIO pin space
 *
 * Tries to match a GPIO pin number to the ranges handled by a certain pin
 * controller, return the range or NULL
 */
static struct pinctrl_gpio_range *
pinctrl_match_gpio_range(struct pinctrl_dev *pctldev, unsigned gpio)
{
	struct pinctrl_gpio_range *range = NULL;

	/* Loop over the ranges */
	mutex_lock(&pctldev->gpio_ranges_lock);
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		/* Check if we're in the valid range */
		if (gpio >= range->base &&
		    gpio < range->base + range->npins) {
			mutex_unlock(&pctldev->gpio_ranges_lock);
			return range;
		}
	}
	mutex_unlock(&pctldev->gpio_ranges_lock);

	return NULL;
}

/**
 * pinctrl_get_device_gpio_range() - find device for GPIO range
 * @gpio: the pin to locate the pin controller for
 * @outdev: the pin control device if found
 * @outrange: the GPIO range if found
 *
 * Find the pin controller handling a certain GPIO pin from the pinspace of
 * the GPIO subsystem, return the device and the matching GPIO range. Returns
 * negative if the GPIO range could not be found in any device.
 */
static int pinctrl_get_device_gpio_range(unsigned gpio,
					 struct pinctrl_dev **outdev,
					 struct pinctrl_gpio_range **outrange)
{
	struct pinctrl_dev *pctldev = NULL;

	/* Loop over the pin controllers */
	mutex_lock(&pinctrldev_list_mutex);
	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		struct pinctrl_gpio_range *range;

		range = pinctrl_match_gpio_range(pctldev, gpio);
		if (range != NULL) {
			*outdev = pctldev;
			*outrange = range;
			mutex_unlock(&pinctrldev_list_mutex);
			return 0;
		}
	}
	mutex_unlock(&pinctrldev_list_mutex);

	return -EINVAL;
}

/**
 * pinctrl_add_gpio_range() - register a GPIO range for a controller
 * @pctldev: pin controller device to add the range to
 * @range: the GPIO range to add
 *
 * This adds a range of GPIOs to be handled by a certain pin controller. Call
 * this to register handled ranges after registering your pin controller.
 */
void pinctrl_add_gpio_range(struct pinctrl_dev *pctldev,
			    struct pinctrl_gpio_range *range)
{
	mutex_lock(&pctldev->gpio_ranges_lock);
	list_add_tail(&range->node, &pctldev->gpio_ranges);
	mutex_unlock(&pctldev->gpio_ranges_lock);
}
EXPORT_SYMBOL_GPL(pinctrl_add_gpio_range);

/**
 * pinctrl_remove_gpio_range() - remove a range of GPIOs fro a pin controller
 * @pctldev: pin controller device to remove the range from
 * @range: the GPIO range to remove
 */
void pinctrl_remove_gpio_range(struct pinctrl_dev *pctldev,
			       struct pinctrl_gpio_range *range)
{
	mutex_lock(&pctldev->gpio_ranges_lock);
	list_del(&range->node);
	mutex_unlock(&pctldev->gpio_ranges_lock);
}
EXPORT_SYMBOL_GPL(pinctrl_remove_gpio_range);

/**
 * pinctrl_get_group_selector() - returns the group selector for a group
 * @pctldev: the pin controller handling the group
 * @pin_group: the pin group to look up
 */
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	unsigned group_selector = 0;

	while (pctlops->list_groups(pctldev, group_selector) >= 0) {
		const char *gname = pctlops->get_group_name(pctldev,
							    group_selector);
		if (!strcmp(gname, pin_group)) {
			dev_dbg(pctldev->dev,
				"found group selector %u for %s\n",
				group_selector,
				pin_group);
			return group_selector;
		}

		group_selector++;
	}

	dev_err(pctldev->dev, "does not have pin group %s\n",
		pin_group);

	return -EINVAL;
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
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return -EINVAL;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	return pinmux_request_gpio(pctldev, range, pin, gpio);
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

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	return pinmux_free_gpio(pctldev, pin, range);
}
EXPORT_SYMBOL_GPL(pinctrl_free_gpio);

static int pinctrl_gpio_direction(unsigned gpio, bool input)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return ret;

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	return pinmux_gpio_direction(pctldev, range, pin, input);
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

static struct pinctrl *pinctrl_get_locked(struct device *dev, const char *name)
{
	struct pinctrl_dev *pctldev = NULL;
	const char *devname;
	struct pinctrl *p;
	unsigned num_maps = 0;
	int ret = -ENODEV;
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;

	/* We must have a dev name */
	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	devname = dev_name(dev);

	pr_debug("get pin control handle device %s state %s\n", devname, name);

	/*
	 * create the state cookie holder struct pinctrl for each
	 * mapping, this is what consumers will get when requesting
	 * a pin control handle with pinctrl_get()
	 */
	p = kzalloc(sizeof(struct pinctrl), GFP_KERNEL);
	if (p == NULL)
		return ERR_PTR(-ENOMEM);
	mutex_init(&p->mutex);
	pinmux_init_pinctrl_handle(p);

	/* Iterate over the pin control maps to locate the right ones */
	for_each_maps(maps_node, i, map) {
		/*
		 * First, try to find the pctldev given in the map
		 */
		pctldev = get_pinctrl_dev_from_devname(map->ctrl_dev_name);
		if (!pctldev) {
			dev_err(dev, "unknown pinctrl device %s in map entry",
				map->ctrl_dev_name);
			pinmux_put(p);
			kfree(p);
			/* Eventually, this should trigger deferred probe */
			return ERR_PTR(-ENODEV);
		}

		pr_debug("in map, found pctldev %s to handle function %s",
			 dev_name(pctldev->dev), map->function);

		/* Map must be for this device */
		if (strcmp(map->dev_name, devname))
			continue;

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

		ret = pinmux_apply_muxmap(pctldev, p, dev, devname, map);
		if (ret) {
			kfree(p);
			return ERR_PTR(ret);
		}
		num_maps++;
	}

	/*
	 * This may be perfectly legitimate. An IP block may get re-used
	 * across SoCs. Not all of those SoCs may need pinmux settings for the
	 * IP block, e.g. if one SoC dedicates pins to that function but
	 * another doesn't. The driver won't know this, and will always
	 * attempt to set up the pinmux. The mapping table defines whether any
	 * HW programming is actually needed.
	 */
	if (!num_maps)
		dev_info(dev, "zero maps found for mapping %s\n", name);

	pr_debug("found %u mux maps for device %s, UD %s\n",
		 num_maps, devname, name ? name : "(undefined)");

	/* Add the pinmux to the global list */
	mutex_lock(&pinctrl_list_mutex);
	list_add_tail(&p->node, &pinctrl_list);
	mutex_unlock(&pinctrl_list_mutex);

	return p;
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
	struct pinctrl *p;

	mutex_lock(&pinctrl_maps_mutex);
	p = pinctrl_get_locked(dev, name);
	mutex_unlock(&pinctrl_maps_mutex);

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
	pinmux_put(p);
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
		ret = pinmux_enable(p);
		if (ret)
			p->usecount--;
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
		pinmux_disable(p);
	}
	mutex_unlock(&p->mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_disable);

/**
 * pinctrl_register_mappings() - register a set of pin controller mappings
 * @maps: the pincontrol mappings table to register. This should probably be
 *	marked with __initdata so it can be discarded after boot. This
 *	function will perform a shallow copy for the mapping entries.
 * @num_maps: the number of maps in the mapping table
 */
int pinctrl_register_mappings(struct pinctrl_map const *maps,
			      unsigned num_maps)
{
	int i;
	struct pinctrl_maps *maps_node;

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

		if (!maps[i].dev_name) {
			pr_err("failed to register map %s (%d): no device given\n",
					maps[i].name, i);
			return -EINVAL;
		}
	}

	maps_node = kzalloc(sizeof(*maps_node), GFP_KERNEL);
	if (!maps_node) {
		pr_err("failed to alloc struct pinctrl_maps\n");
		return -ENOMEM;
	}

	maps_node->num_maps = num_maps;
	maps_node->maps = kmemdup(maps, sizeof(*maps) * num_maps, GFP_KERNEL);
	if (!maps_node->maps) {
		kfree(maps_node);
		return -ENOMEM;
	}

	mutex_lock(&pinctrl_maps_mutex);
	list_add_tail(&maps_node->node, &pinctrl_maps);
	mutex_unlock(&pinctrl_maps_mutex);

	return 0;
}

/* Hog a single map entry and add to the hoglist */
static int pinctrl_hog_map(struct pinctrl_dev *pctldev,
			   struct pinctrl_map const *map)
{
	struct pinctrl_hog *hog;
	struct pinctrl *p;
	int ret;

	hog = kzalloc(sizeof(struct pinctrl_hog), GFP_KERNEL);
	if (!hog)
		return -ENOMEM;

	p = pinctrl_get_locked(pctldev->dev, map->name);
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
	list_add_tail(&hog->node, &pctldev->pinctrl_hogs);
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
static int pinctrl_hog_maps(struct pinctrl_dev *pctldev)
{
	struct device *dev = pctldev->dev;
	const char *devname = dev_name(dev);
	int ret;
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;

	INIT_LIST_HEAD(&pctldev->pinctrl_hogs);
	mutex_init(&pctldev->pinctrl_hogs_lock);

	mutex_lock(&pinctrl_maps_mutex);
	for_each_maps(maps_node, i, map) {
		if (!strcmp(map->ctrl_dev_name, devname) &&
		    !strcmp(map->dev_name, devname)) {
			/* OK time to hog! */
			ret = pinctrl_hog_map(pctldev, map);
			if (ret) {
				mutex_unlock(&pinctrl_maps_mutex);
				return ret;
			}
		}
	}
	mutex_unlock(&pinctrl_maps_mutex);

	return 0;
}

/**
 * pinctrl_unhog_maps() - unhog specific map entries on controller device
 * @pctldev: the pin control device to unhog entries on
 */
static void pinctrl_unhog_maps(struct pinctrl_dev *pctldev)
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

static int pinctrl_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;
	unsigned i, pin;

	seq_printf(s, "registered pins: %d\n", pctldev->desc->npins);

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Pin space may be sparse */
		if (desc == NULL)
			continue;

		seq_printf(s, "pin %d (%s) ", pin,
			   desc->name ? desc->name : "unnamed");

		/* Driver-specific info per pin */
		if (ops->pin_dbg_show)
			ops->pin_dbg_show(pctldev, s, pin);

		seq_puts(s, "\n");
	}

	return 0;
}

static int pinctrl_groups_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;
	unsigned selector = 0;

	/* No grouping */
	if (!ops)
		return 0;

	seq_puts(s, "registered pin groups:\n");
	while (ops->list_groups(pctldev, selector) >= 0) {
		const unsigned *pins;
		unsigned num_pins;
		const char *gname = ops->get_group_name(pctldev, selector);
		int ret;
		int i;

		ret = ops->get_group_pins(pctldev, selector,
					  &pins, &num_pins);
		if (ret)
			seq_printf(s, "%s [ERROR GETTING PINS]\n",
				   gname);
		else {
			seq_printf(s, "group: %s, pins = [ ", gname);
			for (i = 0; i < num_pins; i++)
				seq_printf(s, "%d ", pins[i]);
			seq_puts(s, "]\n");
		}
		selector++;
	}


	return 0;
}

static int pinctrl_gpioranges_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	struct pinctrl_gpio_range *range = NULL;

	seq_puts(s, "GPIO ranges handled:\n");

	/* Loop over the ranges */
	mutex_lock(&pctldev->gpio_ranges_lock);
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		seq_printf(s, "%u: %s GPIOS [%u - %u] PINS [%u - %u]\n",
			   range->id, range->name,
			   range->base, (range->base + range->npins - 1),
			   range->pin_base,
			   (range->pin_base + range->npins - 1));
	}
	mutex_unlock(&pctldev->gpio_ranges_lock);

	return 0;
}

static int pinctrl_maps_show(struct seq_file *s, void *what)
{
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;

	seq_puts(s, "Pinctrl maps:\n");

	mutex_lock(&pinctrl_maps_mutex);
	for_each_maps(maps_node, i, map) {
		seq_printf(s, "%s:\n", map->name);
		seq_printf(s, "  device: %s\n", map->dev_name);
		seq_printf(s, "  controlling device %s\n", map->ctrl_dev_name);
		seq_printf(s, "  function: %s\n", map->function);
		seq_printf(s, "  group: %s\n", map->group ? map->group :
			   "(default)");
	}
	mutex_unlock(&pinctrl_maps_mutex);

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

static int pinctrl_devices_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev;

	seq_puts(s, "name [pinmux] [pinconf]\n");
	mutex_lock(&pinctrldev_list_mutex);
	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		seq_printf(s, "%s ", pctldev->desc->name);
		if (pctldev->desc->pmxops)
			seq_puts(s, "yes ");
		else
			seq_puts(s, "no ");
		if (pctldev->desc->confops)
			seq_puts(s, "yes");
		else
			seq_puts(s, "no");
		seq_puts(s, "\n");
	}
	mutex_unlock(&pinctrldev_list_mutex);

	return 0;
}

static int pinctrl_show(struct seq_file *s, void *what)
{
	struct pinctrl *p;

	seq_puts(s, "Requested pin control handlers their pinmux maps:\n");
	list_for_each_entry(p, &pinctrl_list, node) {
		struct pinctrl_dev *pctldev = p->pctldev;

		if (!pctldev) {
			seq_puts(s, "NO PIN CONTROLLER DEVICE\n");
			continue;
		}

		seq_printf(s, "device: %s",
			   pinctrl_dev_get_name(p->pctldev));

		pinmux_dbg_show(s, p);

		seq_printf(s, " users: %u map-> %s\n",
			   p->usecount,
			   p->dev ? dev_name(p->dev) : "(system)");
	}

	return 0;
}

static int pinctrl_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_pins_show, inode->i_private);
}

static int pinctrl_groups_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_groups_show, inode->i_private);
}

static int pinctrl_gpioranges_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_gpioranges_show, inode->i_private);
}

static int pinctrl_maps_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_maps_show, inode->i_private);
}

static int pinmux_hogs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinmux_hogs_show, inode->i_private);
}

static int pinctrl_devices_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_devices_show, NULL);
}

static int pinctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_show, NULL);
}

static const struct file_operations pinctrl_pins_ops = {
	.open		= pinctrl_pins_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinctrl_groups_ops = {
	.open		= pinctrl_groups_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinctrl_gpioranges_ops = {
	.open		= pinctrl_gpioranges_open,
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

static const struct file_operations pinmux_hogs_ops = {
	.open		= pinmux_hogs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinctrl_devices_ops = {
	.open		= pinctrl_devices_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinctrl_ops = {
	.open		= pinctrl_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *debugfs_root;

static void pinctrl_init_device_debugfs(struct pinctrl_dev *pctldev)
{
	struct dentry *device_root;

	device_root = debugfs_create_dir(dev_name(pctldev->dev),
					 debugfs_root);
	pctldev->device_root = device_root;

	if (IS_ERR(device_root) || !device_root) {
		pr_warn("failed to create debugfs directory for %s\n",
			dev_name(pctldev->dev));
		return;
	}
	debugfs_create_file("pins", S_IFREG | S_IRUGO,
			    device_root, pctldev, &pinctrl_pins_ops);
	debugfs_create_file("pingroups", S_IFREG | S_IRUGO,
			    device_root, pctldev, &pinctrl_groups_ops);
	debugfs_create_file("gpio-ranges", S_IFREG | S_IRUGO,
			    device_root, pctldev, &pinctrl_gpioranges_ops);
	debugfs_create_file("pinctrl-maps", S_IFREG | S_IRUGO,
			    device_root, pctldev, &pinctrl_maps_ops);
	debugfs_create_file("pinmux-hogs", S_IFREG | S_IRUGO,
			    device_root, pctldev, &pinmux_hogs_ops);
	pinmux_init_device_debugfs(device_root, pctldev);
	pinconf_init_device_debugfs(device_root, pctldev);
}

static void pinctrl_remove_device_debugfs(struct pinctrl_dev *pctldev)
{
	debugfs_remove_recursive(pctldev->device_root);
}

static void pinctrl_init_debugfs(void)
{
	debugfs_root = debugfs_create_dir("pinctrl", NULL);
	if (IS_ERR(debugfs_root) || !debugfs_root) {
		pr_warn("failed to create debugfs directory\n");
		debugfs_root = NULL;
		return;
	}

	debugfs_create_file("pinctrl-devices", S_IFREG | S_IRUGO,
			    debugfs_root, NULL, &pinctrl_devices_ops);
	debugfs_create_file("pinctrl-handles", S_IFREG | S_IRUGO,
			    debugfs_root, NULL, &pinctrl_ops);
}

#else /* CONFIG_DEBUG_FS */

static void pinctrl_init_device_debugfs(struct pinctrl_dev *pctldev)
{
}

static void pinctrl_init_debugfs(void)
{
}

static void pinctrl_remove_device_debugfs(struct pinctrl_dev *pctldev)
{
}

#endif

/**
 * pinctrl_register() - register a pin controller device
 * @pctldesc: descriptor for this pin controller
 * @dev: parent device for this pin controller
 * @driver_data: private pin controller data for this pin controller
 */
struct pinctrl_dev *pinctrl_register(struct pinctrl_desc *pctldesc,
				    struct device *dev, void *driver_data)
{
	struct pinctrl_dev *pctldev;
	int ret;

	if (pctldesc == NULL)
		return NULL;
	if (pctldesc->name == NULL)
		return NULL;

	pctldev = kzalloc(sizeof(struct pinctrl_dev), GFP_KERNEL);
	if (pctldev == NULL)
		return NULL;

	/* Initialize pin control device struct */
	pctldev->owner = pctldesc->owner;
	pctldev->desc = pctldesc;
	pctldev->driver_data = driver_data;
	INIT_RADIX_TREE(&pctldev->pin_desc_tree, GFP_KERNEL);
	spin_lock_init(&pctldev->pin_desc_tree_lock);
	INIT_LIST_HEAD(&pctldev->gpio_ranges);
	mutex_init(&pctldev->gpio_ranges_lock);
	pctldev->dev = dev;

	/* If we're implementing pinmuxing, check the ops for sanity */
	if (pctldesc->pmxops) {
		ret = pinmux_check_ops(pctldev);
		if (ret) {
			pr_err("%s pinmux ops lacks necessary functions\n",
			       pctldesc->name);
			goto out_err;
		}
	}

	/* If we're implementing pinconfig, check the ops for sanity */
	if (pctldesc->confops) {
		ret = pinconf_check_ops(pctldev);
		if (ret) {
			pr_err("%s pin config ops lacks necessary functions\n",
			       pctldesc->name);
			goto out_err;
		}
	}

	/* Register all the pins */
	pr_debug("try to register %d pins on %s...\n",
		 pctldesc->npins, pctldesc->name);
	ret = pinctrl_register_pins(pctldev, pctldesc->pins, pctldesc->npins);
	if (ret) {
		pr_err("error during pin registration\n");
		pinctrl_free_pindescs(pctldev, pctldesc->pins,
				      pctldesc->npins);
		goto out_err;
	}

	pinctrl_init_device_debugfs(pctldev);
	mutex_lock(&pinctrldev_list_mutex);
	list_add_tail(&pctldev->node, &pinctrldev_list);
	mutex_unlock(&pinctrldev_list_mutex);
	pinctrl_hog_maps(pctldev);
	return pctldev;

out_err:
	kfree(pctldev);
	return NULL;
}
EXPORT_SYMBOL_GPL(pinctrl_register);

/**
 * pinctrl_unregister() - unregister pinmux
 * @pctldev: pin controller to unregister
 *
 * Called by pinmux drivers to unregister a pinmux.
 */
void pinctrl_unregister(struct pinctrl_dev *pctldev)
{
	if (pctldev == NULL)
		return;

	pinctrl_remove_device_debugfs(pctldev);
	pinctrl_unhog_maps(pctldev);
	/* TODO: check that no pinmuxes are still active? */
	mutex_lock(&pinctrldev_list_mutex);
	list_del(&pctldev->node);
	mutex_unlock(&pinctrldev_list_mutex);
	/* Destroy descriptor tree */
	pinctrl_free_pindescs(pctldev, pctldev->desc->pins,
			      pctldev->desc->npins);
	kfree(pctldev);
}
EXPORT_SYMBOL_GPL(pinctrl_unregister);

static int __init pinctrl_init(void)
{
	pr_info("initialized pinctrl subsystem\n");
	pinctrl_init_debugfs();
	return 0;
}

/* init early since many drivers really need to initialized pinmux early */
core_initcall(pinctrl_init);
