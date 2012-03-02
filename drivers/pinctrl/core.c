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
#include <linux/err.h>
#include <linux/list.h>
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

/* Mutex taken by all entry points */
DEFINE_MUTEX(pinctrl_mutex);

/* Global list of pin control devices (struct pinctrl_dev) */
static LIST_HEAD(pinctrldev_list);

/* List of pin controller handles (struct pinctrl) */
static LIST_HEAD(pinctrl_list);

/* List of pinctrl maps (struct pinctrl_maps) */
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

	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		if (!strcmp(dev_name(pctldev->dev), devname)) {
			/* Matched on device name */
			found = true;
			break;
		}
	}

	return found ? pctldev : NULL;
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

	mutex_lock(&pinctrl_mutex);
	pindesc = pin_desc_get(pctldev, pin);
	mutex_unlock(&pinctrl_mutex);

	return pindesc != NULL;
}
EXPORT_SYMBOL_GPL(pin_is_valid);

/* Deletes a range of pin descriptors */
static void pinctrl_free_pindescs(struct pinctrl_dev *pctldev,
				  const struct pinctrl_pin_desc *pins,
				  unsigned num_pins)
{
	int i;

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
	if (pindesc == NULL) {
		dev_err(pctldev->dev, "failed to alloc struct pin_desc\n");
		return -ENOMEM;
	}

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

	radix_tree_insert(&pctldev->pin_desc_tree, number, pindesc);
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
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		/* Check if we're in the valid range */
		if (gpio >= range->base &&
		    gpio < range->base + range->npins) {
			return range;
		}
	}

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
	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		struct pinctrl_gpio_range *range;

		range = pinctrl_match_gpio_range(pctldev, gpio);
		if (range != NULL) {
			*outdev = pctldev;
			*outrange = range;
			return 0;
		}
	}

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
	mutex_lock(&pinctrl_mutex);
	list_add_tail(&range->node, &pctldev->gpio_ranges);
	mutex_unlock(&pinctrl_mutex);
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
	mutex_lock(&pinctrl_mutex);
	list_del(&range->node);
	mutex_unlock(&pinctrl_mutex);
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

	mutex_lock(&pinctrl_mutex);

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret) {
		mutex_unlock(&pinctrl_mutex);
		return -EINVAL;
	}

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	ret = pinmux_request_gpio(pctldev, range, pin, gpio);

	mutex_unlock(&pinctrl_mutex);
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

	mutex_lock(&pinctrl_mutex);

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret) {
		mutex_unlock(&pinctrl_mutex);
		return;
	}

	/* Convert to the pin controllers number space */
	pin = gpio - range->base + range->pin_base;

	pinmux_free_gpio(pctldev, pin, range);

	mutex_unlock(&pinctrl_mutex);
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
	int ret;
	mutex_lock(&pinctrl_mutex);
	ret = pinctrl_gpio_direction(gpio, true);
	mutex_unlock(&pinctrl_mutex);
	return ret;
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
	int ret;
	mutex_lock(&pinctrl_mutex);
	ret = pinctrl_gpio_direction(gpio, false);
	mutex_unlock(&pinctrl_mutex);
	return ret;
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

	/* We must have both a dev and state name */
	if (WARN_ON(!dev || !name))
		return ERR_PTR(-EINVAL);

	devname = dev_name(dev);

	dev_dbg(dev, "pinctrl_get() for device %s state %s\n", devname, name);

	/*
	 * create the state cookie holder struct pinctrl for each
	 * mapping, this is what consumers will get when requesting
	 * a pin control handle with pinctrl_get()
	 */
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) {
		dev_err(dev, "failed to alloc struct pinctrl\n");
		return ERR_PTR(-ENOMEM);
	}
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

		dev_dbg(dev, "in map, found pctldev %s to handle function %s",
			dev_name(pctldev->dev), map->function);

		/* Map must be for this device */
		if (strcmp(map->dev_name, devname))
			continue;

		/* State name must be the one we're looking for */
		if (strcmp(map->name, name))
			continue;

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

	dev_dbg(dev, "found %u maps for device %s state %s\n",
		num_maps, devname, name ? name : "(undefined)");

	/* Add the pinmux to the global list */
	list_add_tail(&p->node, &pinctrl_list);

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

	mutex_lock(&pinctrl_mutex);
	p = pinctrl_get_locked(dev, name);
	mutex_unlock(&pinctrl_mutex);

	return p;
}
EXPORT_SYMBOL_GPL(pinctrl_get);

static void pinctrl_put_locked(struct pinctrl *p)
{
	if (p == NULL)
		return;

	if (p->usecount)
		pr_warn("releasing pin control handle with active users!\n");
	/* Free the groups and all acquired pins */
	pinmux_put(p);

	/* Remove from list */
	list_del(&p->node);

	kfree(p);
}

/**
 * pinctrl_put() - release a previously claimed pin control handle
 * @p: a pin control handle previously claimed by pinctrl_get()
 */
void pinctrl_put(struct pinctrl *p)
{
	mutex_lock(&pinctrl_mutex);
	pinctrl_put(p);
	mutex_unlock(&pinctrl_mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_put);

static int pinctrl_enable_locked(struct pinctrl *p)
{
	int ret = 0;

	if (p == NULL)
		return -EINVAL;

	if (p->usecount++ == 0) {
		ret = pinmux_enable(p);
		if (ret)
			p->usecount--;
	}

	return ret;
}

/**
 * pinctrl_enable() - enable a certain pin controller setting
 * @p: the pin control handle to enable, previously claimed by pinctrl_get()
 */
int pinctrl_enable(struct pinctrl *p)
{
	int ret;
	mutex_lock(&pinctrl_mutex);
	ret = pinctrl_enable_locked(p);
	mutex_unlock(&pinctrl_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(pinctrl_enable);

static void pinctrl_disable_locked(struct pinctrl *p)
{
	if (p == NULL)
		return;

	if (--p->usecount == 0) {
		pinmux_disable(p);
	}
}

/**
 * pinctrl_disable() - disable a certain pin control setting
 * @p: the pin control handle to disable, previously claimed by pinctrl_get()
 */
void pinctrl_disable(struct pinctrl *p)
{
	mutex_lock(&pinctrl_mutex);
	pinctrl_disable_locked(p);
	mutex_unlock(&pinctrl_mutex);
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
		pr_err("failed to duplicate mapping table\n");
		kfree(maps_node);
		return -ENOMEM;
	}

	mutex_lock(&pinctrl_mutex);
	list_add_tail(&maps_node->node, &pinctrl_maps);
	mutex_unlock(&pinctrl_mutex);

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int pinctrl_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;
	unsigned i, pin;

	seq_printf(s, "registered pins: %d\n", pctldev->desc->npins);

	mutex_lock(&pinctrl_mutex);

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

	mutex_unlock(&pinctrl_mutex);

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

	mutex_lock(&pinctrl_mutex);

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

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinctrl_gpioranges_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	struct pinctrl_gpio_range *range = NULL;

	seq_puts(s, "GPIO ranges handled:\n");

	mutex_lock(&pinctrl_mutex);

	/* Loop over the ranges */
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		seq_printf(s, "%u: %s GPIOS [%u - %u] PINS [%u - %u]\n",
			   range->id, range->name,
			   range->base, (range->base + range->npins - 1),
			   range->pin_base,
			   (range->pin_base + range->npins - 1));
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinctrl_devices_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev;

	seq_puts(s, "name [pinmux] [pinconf]\n");

	mutex_lock(&pinctrl_mutex);

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

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinctrl_maps_show(struct seq_file *s, void *what)
{
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;

	seq_puts(s, "Pinctrl maps:\n");

	mutex_lock(&pinctrl_mutex);

	for_each_maps(maps_node, i, map) {
		seq_printf(s, "%s:\n", map->name);
		seq_printf(s, "  device: %s\n", map->dev_name);
		seq_printf(s, "  controlling device %s\n", map->ctrl_dev_name);
		seq_printf(s, "  function: %s\n", map->function);
		seq_printf(s, "  group: %s\n", map->group ? map->group :
			   "(default)");
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinctrl_show(struct seq_file *s, void *what)
{
	struct pinctrl *p;

	seq_puts(s, "Requested pin control handlers their pinmux maps:\n");

	mutex_lock(&pinctrl_mutex);

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

	mutex_unlock(&pinctrl_mutex);

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

static int pinctrl_devices_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_devices_show, NULL);
}

static int pinctrl_maps_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinctrl_maps_show, NULL);
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

static const struct file_operations pinctrl_devices_ops = {
	.open		= pinctrl_devices_open,
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
	debugfs_create_file("pinctrl-maps", S_IFREG | S_IRUGO,
			    debugfs_root, NULL, &pinctrl_maps_ops);
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

	pctldev = kzalloc(sizeof(*pctldev), GFP_KERNEL);
	if (pctldev == NULL) {
		dev_err(dev, "failed to alloc struct pinctrl_dev\n");
		return NULL;
	}

	/* Initialize pin control device struct */
	pctldev->owner = pctldesc->owner;
	pctldev->desc = pctldesc;
	pctldev->driver_data = driver_data;
	INIT_RADIX_TREE(&pctldev->pin_desc_tree, GFP_KERNEL);
	INIT_LIST_HEAD(&pctldev->gpio_ranges);
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

	mutex_lock(&pinctrl_mutex);

	list_add_tail(&pctldev->node, &pinctrldev_list);

	pctldev->p = pinctrl_get_locked(pctldev->dev, PINCTRL_STATE_DEFAULT);
	if (!IS_ERR(pctldev->p))
		pinctrl_enable_locked(pctldev->p);

	mutex_unlock(&pinctrl_mutex);

	pinctrl_init_device_debugfs(pctldev);

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

	mutex_lock(&pinctrl_mutex);

	if (!IS_ERR(pctldev->p)) {
		pinctrl_disable_locked(pctldev->p);
		pinctrl_put_locked(pctldev->p);
	}

	/* TODO: check that no pinmuxes are still active? */
	list_del(&pctldev->node);
	/* Destroy descriptor tree */
	pinctrl_free_pindescs(pctldev, pctldev->desc->pins,
			      pctldev->desc->npins);
	kfree(pctldev);

	mutex_unlock(&pinctrl_mutex);
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
