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
#include <linux/kref.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>

#ifdef CONFIG_GPIOLIB
#include <asm-generic/gpio.h>
#endif

#include "core.h"
#include "devicetree.h"
#include "pinmux.h"
#include "pinconf.h"


static bool pinctrl_dummy_state;

/* Mutex taken to protect pinctrl_list */
static DEFINE_MUTEX(pinctrl_list_mutex);

/* Mutex taken to protect pinctrl_maps */
DEFINE_MUTEX(pinctrl_maps_mutex);

/* Mutex taken to protect pinctrldev_list */
static DEFINE_MUTEX(pinctrldev_list_mutex);

/* Global list of pin control devices (struct pinctrl_dev) */
static LIST_HEAD(pinctrldev_list);

/* List of pin controller handles (struct pinctrl) */
static LIST_HEAD(pinctrl_list);

/* List of pinctrl maps (struct pinctrl_maps) */
LIST_HEAD(pinctrl_maps);


/**
 * pinctrl_provide_dummies() - indicate if pinctrl provides dummy state support
 *
 * Usually this function is called by platforms without pinctrl driver support
 * but run with some shared drivers using pinctrl APIs.
 * After calling this function, the pinctrl core will return successfully
 * with creating a dummy state for the driver to keep going smoothly.
 */
void pinctrl_provide_dummies(void)
{
	pinctrl_dummy_state = true;
}

const char *pinctrl_dev_get_name(struct pinctrl_dev *pctldev)
{
	/* We're not allowed to register devices without name */
	return pctldev->desc->name;
}
EXPORT_SYMBOL_GPL(pinctrl_dev_get_name);

const char *pinctrl_dev_get_devname(struct pinctrl_dev *pctldev)
{
	return dev_name(pctldev->dev);
}
EXPORT_SYMBOL_GPL(pinctrl_dev_get_devname);

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

	if (!devname)
		return NULL;

	mutex_lock(&pinctrldev_list_mutex);

	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		if (!strcmp(dev_name(pctldev->dev), devname)) {
			/* Matched on device name */
			mutex_unlock(&pinctrldev_list_mutex);
			return pctldev;
		}
	}

	mutex_unlock(&pinctrldev_list_mutex);

	return NULL;
}

struct pinctrl_dev *get_pinctrl_dev_from_of_node(struct device_node *np)
{
	struct pinctrl_dev *pctldev;

	mutex_lock(&pinctrldev_list_mutex);

	list_for_each_entry(pctldev, &pinctrldev_list, node)
		if (pctldev->dev->of_node == np) {
			mutex_unlock(&pinctrldev_list_mutex);
			return pctldev;
		}

	mutex_unlock(&pinctrldev_list_mutex);

	return NULL;
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
		if (desc && !strcmp(name, desc->name))
			return pin;
	}

	return -EINVAL;
}

/**
 * pin_get_name_from_id() - look up a pin name from a pin id
 * @pctldev: the pin control device to lookup the pin on
 * @name: the name of the pin to look up
 */
const char *pin_get_name(struct pinctrl_dev *pctldev, const unsigned pin)
{
	const struct pin_desc *desc;

	desc = pin_desc_get(pctldev, pin);
	if (desc == NULL) {
		dev_err(pctldev->dev, "failed to get pin(%d) name\n",
			pin);
		return NULL;
	}

	return desc->name;
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

	mutex_lock(&pctldev->mutex);
	pindesc = pin_desc_get(pctldev, pin);
	mutex_unlock(&pctldev->mutex);

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
				    const struct pinctrl_pin_desc *pin)
{
	struct pin_desc *pindesc;

	pindesc = pin_desc_get(pctldev, pin->number);
	if (pindesc != NULL) {
		dev_err(pctldev->dev, "pin %d already registered\n",
			pin->number);
		return -EINVAL;
	}

	pindesc = kzalloc(sizeof(*pindesc), GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	/* Set owner */
	pindesc->pctldev = pctldev;

	/* Copy basic pin info */
	if (pin->name) {
		pindesc->name = pin->name;
	} else {
		pindesc->name = kasprintf(GFP_KERNEL, "PIN%u", pin->number);
		if (pindesc->name == NULL) {
			kfree(pindesc);
			return -ENOMEM;
		}
		pindesc->dynamic_name = true;
	}

	pindesc->drv_data = pin->drv_data;

	radix_tree_insert(&pctldev->pin_desc_tree, pin->number, pindesc);
	pr_debug("registered pin %d (%s) on %s\n",
		 pin->number, pindesc->name, pctldev->desc->name);
	return 0;
}

static int pinctrl_register_pins(struct pinctrl_dev *pctldev,
				 struct pinctrl_pin_desc const *pins,
				 unsigned num_descs)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < num_descs; i++) {
		ret = pinctrl_register_one_pin(pctldev, &pins[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * gpio_to_pin() - GPIO range GPIO number to pin number translation
 * @range: GPIO range used for the translation
 * @gpio: gpio pin to translate to a pin number
 *
 * Finds the pin number for a given GPIO using the specified GPIO range
 * as a base for translation. The distinction between linear GPIO ranges
 * and pin list based GPIO ranges is managed correctly by this function.
 *
 * This function assumes the gpio is part of the specified GPIO range, use
 * only after making sure this is the case (e.g. by calling it on the
 * result of successful pinctrl_get_device_gpio_range calls)!
 */
static inline int gpio_to_pin(struct pinctrl_gpio_range *range,
				unsigned int gpio)
{
	unsigned int offset = gpio - range->base;
	if (range->pins)
		return range->pins[offset];
	else
		return range->pin_base + offset;
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

	mutex_lock(&pctldev->mutex);
	/* Loop over the ranges */
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		/* Check if we're in the valid range */
		if (gpio >= range->base &&
		    gpio < range->base + range->npins) {
			mutex_unlock(&pctldev->mutex);
			return range;
		}
	}
	mutex_unlock(&pctldev->mutex);
	return NULL;
}

/**
 * pinctrl_ready_for_gpio_range() - check if other GPIO pins of
 * the same GPIO chip are in range
 * @gpio: gpio pin to check taken from the global GPIO pin space
 *
 * This function is complement of pinctrl_match_gpio_range(). If the return
 * value of pinctrl_match_gpio_range() is NULL, this function could be used
 * to check whether pinctrl device is ready or not. Maybe some GPIO pins
 * of the same GPIO chip don't have back-end pinctrl interface.
 * If the return value is true, it means that pinctrl device is ready & the
 * certain GPIO pin doesn't have back-end pinctrl device. If the return value
 * is false, it means that pinctrl device may not be ready.
 */
#ifdef CONFIG_GPIOLIB
static bool pinctrl_ready_for_gpio_range(unsigned gpio)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range = NULL;
	struct gpio_chip *chip = gpio_to_chip(gpio);

	if (WARN(!chip, "no gpio_chip for gpio%i?", gpio))
		return false;

	mutex_lock(&pinctrldev_list_mutex);

	/* Loop over the pin controllers */
	list_for_each_entry(pctldev, &pinctrldev_list, node) {
		/* Loop over the ranges */
		mutex_lock(&pctldev->mutex);
		list_for_each_entry(range, &pctldev->gpio_ranges, node) {
			/* Check if any gpio range overlapped with gpio chip */
			if (range->base + range->npins - 1 < chip->base ||
			    range->base > chip->base + chip->ngpio - 1)
				continue;
			mutex_unlock(&pctldev->mutex);
			mutex_unlock(&pinctrldev_list_mutex);
			return true;
		}
		mutex_unlock(&pctldev->mutex);
	}

	mutex_unlock(&pinctrldev_list_mutex);

	return false;
}
#else
static bool pinctrl_ready_for_gpio_range(unsigned gpio) { return true; }
#endif

/**
 * pinctrl_get_device_gpio_range() - find device for GPIO range
 * @gpio: the pin to locate the pin controller for
 * @outdev: the pin control device if found
 * @outrange: the GPIO range if found
 *
 * Find the pin controller handling a certain GPIO pin from the pinspace of
 * the GPIO subsystem, return the device and the matching GPIO range. Returns
 * -EPROBE_DEFER if the GPIO range could not be found in any device since it
 * may still have not been registered.
 */
static int pinctrl_get_device_gpio_range(unsigned gpio,
					 struct pinctrl_dev **outdev,
					 struct pinctrl_gpio_range **outrange)
{
	struct pinctrl_dev *pctldev = NULL;

	mutex_lock(&pinctrldev_list_mutex);

	/* Loop over the pin controllers */
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

	return -EPROBE_DEFER;
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
	mutex_lock(&pctldev->mutex);
	list_add_tail(&range->node, &pctldev->gpio_ranges);
	mutex_unlock(&pctldev->mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_add_gpio_range);

void pinctrl_add_gpio_ranges(struct pinctrl_dev *pctldev,
			     struct pinctrl_gpio_range *ranges,
			     unsigned nranges)
{
	int i;

	for (i = 0; i < nranges; i++)
		pinctrl_add_gpio_range(pctldev, &ranges[i]);
}
EXPORT_SYMBOL_GPL(pinctrl_add_gpio_ranges);

struct pinctrl_dev *pinctrl_find_and_add_gpio_range(const char *devname,
		struct pinctrl_gpio_range *range)
{
	struct pinctrl_dev *pctldev;

	pctldev = get_pinctrl_dev_from_devname(devname);

	/*
	 * If we can't find this device, let's assume that is because
	 * it has not probed yet, so the driver trying to register this
	 * range need to defer probing.
	 */
	if (!pctldev) {
		return ERR_PTR(-EPROBE_DEFER);
	}
	pinctrl_add_gpio_range(pctldev, range);

	return pctldev;
}
EXPORT_SYMBOL_GPL(pinctrl_find_and_add_gpio_range);

int pinctrl_get_group_pins(struct pinctrl_dev *pctldev, const char *pin_group,
				const unsigned **pins, unsigned *num_pins)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	int gs;

	if (!pctlops->get_group_pins)
		return -EINVAL;

	gs = pinctrl_get_group_selector(pctldev, pin_group);
	if (gs < 0)
		return gs;

	return pctlops->get_group_pins(pctldev, gs, pins, num_pins);
}
EXPORT_SYMBOL_GPL(pinctrl_get_group_pins);

struct pinctrl_gpio_range *
pinctrl_find_gpio_range_from_pin_nolock(struct pinctrl_dev *pctldev,
					unsigned int pin)
{
	struct pinctrl_gpio_range *range;

	/* Loop over the ranges */
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		/* Check if we're in the valid range */
		if (range->pins) {
			int a;
			for (a = 0; a < range->npins; a++) {
				if (range->pins[a] == pin)
					return range;
			}
		} else if (pin >= range->pin_base &&
			   pin < range->pin_base + range->npins)
			return range;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(pinctrl_find_gpio_range_from_pin_nolock);

/**
 * pinctrl_find_gpio_range_from_pin() - locate the GPIO range for a pin
 * @pctldev: the pin controller device to look in
 * @pin: a controller-local number to find the range for
 */
struct pinctrl_gpio_range *
pinctrl_find_gpio_range_from_pin(struct pinctrl_dev *pctldev,
				 unsigned int pin)
{
	struct pinctrl_gpio_range *range;

	mutex_lock(&pctldev->mutex);
	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	mutex_unlock(&pctldev->mutex);

	return range;
}
EXPORT_SYMBOL_GPL(pinctrl_find_gpio_range_from_pin);

/**
 * pinctrl_remove_gpio_range() - remove a range of GPIOs from a pin controller
 * @pctldev: pin controller device to remove the range from
 * @range: the GPIO range to remove
 */
void pinctrl_remove_gpio_range(struct pinctrl_dev *pctldev,
			       struct pinctrl_gpio_range *range)
{
	mutex_lock(&pctldev->mutex);
	list_del(&range->node);
	mutex_unlock(&pctldev->mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_remove_gpio_range);

#ifdef CONFIG_GENERIC_PINCTRL_GROUPS

/**
 * pinctrl_generic_get_group_count() - returns the number of pin groups
 * @pctldev: pin controller device
 */
int pinctrl_generic_get_group_count(struct pinctrl_dev *pctldev)
{
	return pctldev->num_groups;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_get_group_count);

/**
 * pinctrl_generic_get_group_name() - returns the name of a pin group
 * @pctldev: pin controller device
 * @selector: group number
 */
const char *pinctrl_generic_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	struct group_desc *group;

	group = radix_tree_lookup(&pctldev->pin_group_tree,
				  selector);
	if (!group)
		return NULL;

	return group->name;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_get_group_name);

/**
 * pinctrl_generic_get_group_pins() - gets the pin group pins
 * @pctldev: pin controller device
 * @selector: group number
 * @pins: pins in the group
 * @num_pins: number of pins in the group
 */
int pinctrl_generic_get_group_pins(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   const unsigned int **pins,
				   unsigned int *num_pins)
{
	struct group_desc *group;

	group = radix_tree_lookup(&pctldev->pin_group_tree,
				  selector);
	if (!group) {
		dev_err(pctldev->dev, "%s could not find pingroup%i\n",
			__func__, selector);
		return -EINVAL;
	}

	*pins = group->pins;
	*num_pins = group->num_pins;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_get_group_pins);

/**
 * pinctrl_generic_get_group() - returns a pin group based on the number
 * @pctldev: pin controller device
 * @gselector: group number
 */
struct group_desc *pinctrl_generic_get_group(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	struct group_desc *group;

	group = radix_tree_lookup(&pctldev->pin_group_tree,
				  selector);
	if (!group)
		return NULL;

	return group;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_get_group);

/**
 * pinctrl_generic_add_group() - adds a new pin group
 * @pctldev: pin controller device
 * @name: name of the pin group
 * @pins: pins in the pin group
 * @num_pins: number of pins in the pin group
 * @data: pin controller driver specific data
 *
 * Note that the caller must take care of locking.
 */
int pinctrl_generic_add_group(struct pinctrl_dev *pctldev, const char *name,
			      int *pins, int num_pins, void *data)
{
	struct group_desc *group;

	group = devm_kzalloc(pctldev->dev, sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	group->name = name;
	group->pins = pins;
	group->num_pins = num_pins;
	group->data = data;

	radix_tree_insert(&pctldev->pin_group_tree, pctldev->num_groups,
			  group);

	pctldev->num_groups++;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_add_group);

/**
 * pinctrl_generic_remove_group() - removes a numbered pin group
 * @pctldev: pin controller device
 * @selector: group number
 *
 * Note that the caller must take care of locking.
 */
int pinctrl_generic_remove_group(struct pinctrl_dev *pctldev,
				 unsigned int selector)
{
	struct group_desc *group;

	group = radix_tree_lookup(&pctldev->pin_group_tree,
				  selector);
	if (!group)
		return -ENOENT;

	radix_tree_delete(&pctldev->pin_group_tree, selector);
	devm_kfree(pctldev->dev, group);

	pctldev->num_groups--;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_generic_remove_group);

/**
 * pinctrl_generic_free_groups() - removes all pin groups
 * @pctldev: pin controller device
 *
 * Note that the caller must take care of locking. The pinctrl groups
 * are allocated with devm_kzalloc() so no need to free them here.
 */
static void pinctrl_generic_free_groups(struct pinctrl_dev *pctldev)
{
	struct radix_tree_iter iter;
	void **slot;

	radix_tree_for_each_slot(slot, &pctldev->pin_group_tree, &iter, 0)
		radix_tree_delete(&pctldev->pin_group_tree, iter.index);

	pctldev->num_groups = 0;
}

#else
static inline void pinctrl_generic_free_groups(struct pinctrl_dev *pctldev)
{
}
#endif /* CONFIG_GENERIC_PINCTRL_GROUPS */

/**
 * pinctrl_get_group_selector() - returns the group selector for a group
 * @pctldev: the pin controller handling the group
 * @pin_group: the pin group to look up
 */
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group)
{
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	unsigned ngroups = pctlops->get_groups_count(pctldev);
	unsigned group_selector = 0;

	while (group_selector < ngroups) {
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
 * pinctrl_request_gpio() - request a single pin to be used as GPIO
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
	if (ret) {
		if (pinctrl_ready_for_gpio_range(gpio))
			ret = 0;
		return ret;
	}

	mutex_lock(&pctldev->mutex);

	/* Convert to the pin controllers number space */
	pin = gpio_to_pin(range, gpio);

	ret = pinmux_request_gpio(pctldev, range, pin, gpio);

	mutex_unlock(&pctldev->mutex);

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

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret) {
		return;
	}
	mutex_lock(&pctldev->mutex);

	/* Convert to the pin controllers number space */
	pin = gpio_to_pin(range, gpio);

	pinmux_free_gpio(pctldev, pin, range);

	mutex_unlock(&pctldev->mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_free_gpio);

static int pinctrl_gpio_direction(unsigned gpio, bool input)
{
	struct pinctrl_dev *pctldev;
	struct pinctrl_gpio_range *range;
	int ret;
	int pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret) {
		return ret;
	}

	mutex_lock(&pctldev->mutex);

	/* Convert to the pin controllers number space */
	pin = gpio_to_pin(range, gpio);
	ret = pinmux_gpio_direction(pctldev, range, pin, input);

	mutex_unlock(&pctldev->mutex);

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
 * pinctrl_gpio_set_config() - Apply config to given GPIO pin
 * @gpio: the GPIO pin number from the GPIO subsystem number space
 * @config: the configuration to apply to the GPIO
 *
 * This function should *ONLY* be used from gpiolib-based GPIO drivers, if
 * they need to call the underlying pin controller to change GPIO config
 * (for example set debounce time).
 */
int pinctrl_gpio_set_config(unsigned gpio, unsigned long config)
{
	unsigned long configs[] = { config };
	struct pinctrl_gpio_range *range;
	struct pinctrl_dev *pctldev;
	int ret, pin;

	ret = pinctrl_get_device_gpio_range(gpio, &pctldev, &range);
	if (ret)
		return ret;

	mutex_lock(&pctldev->mutex);
	pin = gpio_to_pin(range, gpio);
	ret = pinconf_set_config(pctldev, pin, configs, ARRAY_SIZE(configs));
	mutex_unlock(&pctldev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(pinctrl_gpio_set_config);

static struct pinctrl_state *find_state(struct pinctrl *p,
					const char *name)
{
	struct pinctrl_state *state;

	list_for_each_entry(state, &p->states, node)
		if (!strcmp(state->name, name))
			return state;

	return NULL;
}

static struct pinctrl_state *create_state(struct pinctrl *p,
					  const char *name)
{
	struct pinctrl_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->name = name;
	INIT_LIST_HEAD(&state->settings);

	list_add_tail(&state->node, &p->states);

	return state;
}

static int add_setting(struct pinctrl *p, struct pinctrl_dev *pctldev,
		       struct pinctrl_map const *map)
{
	struct pinctrl_state *state;
	struct pinctrl_setting *setting;
	int ret;

	state = find_state(p, map->name);
	if (!state)
		state = create_state(p, map->name);
	if (IS_ERR(state))
		return PTR_ERR(state);

	if (map->type == PIN_MAP_TYPE_DUMMY_STATE)
		return 0;

	setting = kzalloc(sizeof(*setting), GFP_KERNEL);
	if (!setting)
		return -ENOMEM;

	setting->type = map->type;

	if (pctldev)
		setting->pctldev = pctldev;
	else
		setting->pctldev =
			get_pinctrl_dev_from_devname(map->ctrl_dev_name);
	if (setting->pctldev == NULL) {
		kfree(setting);
		/* Do not defer probing of hogs (circular loop) */
		if (!strcmp(map->ctrl_dev_name, map->dev_name))
			return -ENODEV;
		/*
		 * OK let us guess that the driver is not there yet, and
		 * let's defer obtaining this pinctrl handle to later...
		 */
		dev_info(p->dev, "unknown pinctrl device %s in map entry, deferring probe",
			map->ctrl_dev_name);
		return -EPROBE_DEFER;
	}

	setting->dev_name = map->dev_name;

	switch (map->type) {
	case PIN_MAP_TYPE_MUX_GROUP:
		ret = pinmux_map_to_setting(map, setting);
		break;
	case PIN_MAP_TYPE_CONFIGS_PIN:
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		ret = pinconf_map_to_setting(map, setting);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		kfree(setting);
		return ret;
	}

	list_add_tail(&setting->node, &state->settings);

	return 0;
}

static struct pinctrl *find_pinctrl(struct device *dev)
{
	struct pinctrl *p;

	mutex_lock(&pinctrl_list_mutex);
	list_for_each_entry(p, &pinctrl_list, node)
		if (p->dev == dev) {
			mutex_unlock(&pinctrl_list_mutex);
			return p;
		}

	mutex_unlock(&pinctrl_list_mutex);
	return NULL;
}

static void pinctrl_free(struct pinctrl *p, bool inlist);

static struct pinctrl *create_pinctrl(struct device *dev,
				      struct pinctrl_dev *pctldev)
{
	struct pinctrl *p;
	const char *devname;
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;
	int ret;

	/*
	 * create the state cookie holder struct pinctrl for each
	 * mapping, this is what consumers will get when requesting
	 * a pin control handle with pinctrl_get()
	 */
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);
	p->dev = dev;
	INIT_LIST_HEAD(&p->states);
	INIT_LIST_HEAD(&p->dt_maps);

	ret = pinctrl_dt_to_map(p, pctldev);
	if (ret < 0) {
		kfree(p);
		return ERR_PTR(ret);
	}

	devname = dev_name(dev);

	mutex_lock(&pinctrl_maps_mutex);
	/* Iterate over the pin control maps to locate the right ones */
	for_each_maps(maps_node, i, map) {
		/* Map must be for this device */
		if (strcmp(map->dev_name, devname))
			continue;

		ret = add_setting(p, pctldev, map);
		/*
		 * At this point the adding of a setting may:
		 *
		 * - Defer, if the pinctrl device is not yet available
		 * - Fail, if the pinctrl device is not yet available,
		 *   AND the setting is a hog. We cannot defer that, since
		 *   the hog will kick in immediately after the device
		 *   is registered.
		 *
		 * If the error returned was not -EPROBE_DEFER then we
		 * accumulate the errors to see if we end up with
		 * an -EPROBE_DEFER later, as that is the worst case.
		 */
		if (ret == -EPROBE_DEFER) {
			pinctrl_free(p, false);
			mutex_unlock(&pinctrl_maps_mutex);
			return ERR_PTR(ret);
		}
	}
	mutex_unlock(&pinctrl_maps_mutex);

	if (ret < 0) {
		/* If some other error than deferral occurred, return here */
		pinctrl_free(p, false);
		return ERR_PTR(ret);
	}

	kref_init(&p->users);

	/* Add the pinctrl handle to the global list */
	mutex_lock(&pinctrl_list_mutex);
	list_add_tail(&p->node, &pinctrl_list);
	mutex_unlock(&pinctrl_list_mutex);

	return p;
}

/**
 * pinctrl_get() - retrieves the pinctrl handle for a device
 * @dev: the device to obtain the handle for
 */
struct pinctrl *pinctrl_get(struct device *dev)
{
	struct pinctrl *p;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	/*
	 * See if somebody else (such as the device core) has already
	 * obtained a handle to the pinctrl for this device. In that case,
	 * return another pointer to it.
	 */
	p = find_pinctrl(dev);
	if (p != NULL) {
		dev_dbg(dev, "obtain a copy of previously claimed pinctrl\n");
		kref_get(&p->users);
		return p;
	}

	return create_pinctrl(dev, NULL);
}
EXPORT_SYMBOL_GPL(pinctrl_get);

static void pinctrl_free_setting(bool disable_setting,
				 struct pinctrl_setting *setting)
{
	switch (setting->type) {
	case PIN_MAP_TYPE_MUX_GROUP:
		if (disable_setting)
			pinmux_disable_setting(setting);
		pinmux_free_setting(setting);
		break;
	case PIN_MAP_TYPE_CONFIGS_PIN:
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		pinconf_free_setting(setting);
		break;
	default:
		break;
	}
}

static void pinctrl_free(struct pinctrl *p, bool inlist)
{
	struct pinctrl_state *state, *n1;
	struct pinctrl_setting *setting, *n2;

	mutex_lock(&pinctrl_list_mutex);
	list_for_each_entry_safe(state, n1, &p->states, node) {
		list_for_each_entry_safe(setting, n2, &state->settings, node) {
			pinctrl_free_setting(state == p->state, setting);
			list_del(&setting->node);
			kfree(setting);
		}
		list_del(&state->node);
		kfree(state);
	}

	pinctrl_dt_free_maps(p);

	if (inlist)
		list_del(&p->node);
	kfree(p);
	mutex_unlock(&pinctrl_list_mutex);
}

/**
 * pinctrl_release() - release the pinctrl handle
 * @kref: the kref in the pinctrl being released
 */
static void pinctrl_release(struct kref *kref)
{
	struct pinctrl *p = container_of(kref, struct pinctrl, users);

	pinctrl_free(p, true);
}

/**
 * pinctrl_put() - decrease use count on a previously claimed pinctrl handle
 * @p: the pinctrl handle to release
 */
void pinctrl_put(struct pinctrl *p)
{
	kref_put(&p->users, pinctrl_release);
}
EXPORT_SYMBOL_GPL(pinctrl_put);

/**
 * pinctrl_lookup_state() - retrieves a state handle from a pinctrl handle
 * @p: the pinctrl handle to retrieve the state from
 * @name: the state name to retrieve
 */
struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p,
						 const char *name)
{
	struct pinctrl_state *state;

	state = find_state(p, name);
	if (!state) {
		if (pinctrl_dummy_state) {
			/* create dummy state */
			dev_dbg(p->dev, "using pinctrl dummy state (%s)\n",
				name);
			state = create_state(p, name);
		} else
			state = ERR_PTR(-ENODEV);
	}

	return state;
}
EXPORT_SYMBOL_GPL(pinctrl_lookup_state);

/**
 * pinctrl_select_state() - select/activate/program a pinctrl state to HW
 * @p: the pinctrl handle for the device that requests configuration
 * @state: the state handle to select/activate/program
 */
int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *state)
{
	struct pinctrl_setting *setting, *setting2;
	struct pinctrl_state *old_state = p->state;
	int ret;

	if (p->state == state)
		return 0;

	if (p->state) {
		/*
		 * For each pinmux setting in the old state, forget SW's record
		 * of mux owner for that pingroup. Any pingroups which are
		 * still owned by the new state will be re-acquired by the call
		 * to pinmux_enable_setting() in the loop below.
		 */
		list_for_each_entry(setting, &p->state->settings, node) {
			if (setting->type != PIN_MAP_TYPE_MUX_GROUP)
				continue;
			pinmux_disable_setting(setting);
		}
	}

	p->state = NULL;

	/* Apply all the settings for the new state */
	list_for_each_entry(setting, &state->settings, node) {
		switch (setting->type) {
		case PIN_MAP_TYPE_MUX_GROUP:
			ret = pinmux_enable_setting(setting);
			break;
		case PIN_MAP_TYPE_CONFIGS_PIN:
		case PIN_MAP_TYPE_CONFIGS_GROUP:
			ret = pinconf_apply_setting(setting);
			break;
		default:
			ret = -EINVAL;
			break;
		}

		if (ret < 0) {
			goto unapply_new_state;
		}
	}

	p->state = state;

	return 0;

unapply_new_state:
	dev_err(p->dev, "Error applying setting, reverse things back\n");

	list_for_each_entry(setting2, &state->settings, node) {
		if (&setting2->node == &setting->node)
			break;
		/*
		 * All we can do here is pinmux_disable_setting.
		 * That means that some pins are muxed differently now
		 * than they were before applying the setting (We can't
		 * "unmux a pin"!), but it's not a big deal since the pins
		 * are free to be muxed by another apply_setting.
		 */
		if (setting2->type == PIN_MAP_TYPE_MUX_GROUP)
			pinmux_disable_setting(setting2);
	}

	/* There's no infinite recursive loop here because p->state is NULL */
	if (old_state)
		pinctrl_select_state(p, old_state);

	return ret;
}
EXPORT_SYMBOL_GPL(pinctrl_select_state);

static void devm_pinctrl_release(struct device *dev, void *res)
{
	pinctrl_put(*(struct pinctrl **)res);
}

/**
 * struct devm_pinctrl_get() - Resource managed pinctrl_get()
 * @dev: the device to obtain the handle for
 *
 * If there is a need to explicitly destroy the returned struct pinctrl,
 * devm_pinctrl_put() should be used, rather than plain pinctrl_put().
 */
struct pinctrl *devm_pinctrl_get(struct device *dev)
{
	struct pinctrl **ptr, *p;

	ptr = devres_alloc(devm_pinctrl_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	p = pinctrl_get(dev);
	if (!IS_ERR(p)) {
		*ptr = p;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return p;
}
EXPORT_SYMBOL_GPL(devm_pinctrl_get);

static int devm_pinctrl_match(struct device *dev, void *res, void *data)
{
	struct pinctrl **p = res;

	return *p == data;
}

/**
 * devm_pinctrl_put() - Resource managed pinctrl_put()
 * @p: the pinctrl handle to release
 *
 * Deallocate a struct pinctrl obtained via devm_pinctrl_get(). Normally
 * this function will not need to be called and the resource management
 * code will ensure that the resource is freed.
 */
void devm_pinctrl_put(struct pinctrl *p)
{
	WARN_ON(devres_release(p->dev, devm_pinctrl_release,
			       devm_pinctrl_match, p));
}
EXPORT_SYMBOL_GPL(devm_pinctrl_put);

int pinctrl_register_map(struct pinctrl_map const *maps, unsigned num_maps,
			 bool dup)
{
	int i, ret;
	struct pinctrl_maps *maps_node;

	pr_debug("add %u pinctrl maps\n", num_maps);

	/* First sanity check the new mapping */
	for (i = 0; i < num_maps; i++) {
		if (!maps[i].dev_name) {
			pr_err("failed to register map %s (%d): no device given\n",
			       maps[i].name, i);
			return -EINVAL;
		}

		if (!maps[i].name) {
			pr_err("failed to register map %d: no map name given\n",
			       i);
			return -EINVAL;
		}

		if (maps[i].type != PIN_MAP_TYPE_DUMMY_STATE &&
				!maps[i].ctrl_dev_name) {
			pr_err("failed to register map %s (%d): no pin control device given\n",
			       maps[i].name, i);
			return -EINVAL;
		}

		switch (maps[i].type) {
		case PIN_MAP_TYPE_DUMMY_STATE:
			break;
		case PIN_MAP_TYPE_MUX_GROUP:
			ret = pinmux_validate_map(&maps[i], i);
			if (ret < 0)
				return ret;
			break;
		case PIN_MAP_TYPE_CONFIGS_PIN:
		case PIN_MAP_TYPE_CONFIGS_GROUP:
			ret = pinconf_validate_map(&maps[i], i);
			if (ret < 0)
				return ret;
			break;
		default:
			pr_err("failed to register map %s (%d): invalid type given\n",
			       maps[i].name, i);
			return -EINVAL;
		}
	}

	maps_node = kzalloc(sizeof(*maps_node), GFP_KERNEL);
	if (!maps_node)
		return -ENOMEM;

	maps_node->num_maps = num_maps;
	if (dup) {
		maps_node->maps = kmemdup(maps, sizeof(*maps) * num_maps,
					  GFP_KERNEL);
		if (!maps_node->maps) {
			pr_err("failed to duplicate mapping table\n");
			kfree(maps_node);
			return -ENOMEM;
		}
	} else {
		maps_node->maps = maps;
	}

	mutex_lock(&pinctrl_maps_mutex);
	list_add_tail(&maps_node->node, &pinctrl_maps);
	mutex_unlock(&pinctrl_maps_mutex);

	return 0;
}

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
	return pinctrl_register_map(maps, num_maps, true);
}

void pinctrl_unregister_map(struct pinctrl_map const *map)
{
	struct pinctrl_maps *maps_node;

	mutex_lock(&pinctrl_maps_mutex);
	list_for_each_entry(maps_node, &pinctrl_maps, node) {
		if (maps_node->maps == map) {
			list_del(&maps_node->node);
			kfree(maps_node);
			mutex_unlock(&pinctrl_maps_mutex);
			return;
		}
	}
	mutex_unlock(&pinctrl_maps_mutex);
}

/**
 * pinctrl_force_sleep() - turn a given controller device into sleep state
 * @pctldev: pin controller device
 */
int pinctrl_force_sleep(struct pinctrl_dev *pctldev)
{
	if (!IS_ERR(pctldev->p) && !IS_ERR(pctldev->hog_sleep))
		return pinctrl_select_state(pctldev->p, pctldev->hog_sleep);
	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_force_sleep);

/**
 * pinctrl_force_default() - turn a given controller device into default state
 * @pctldev: pin controller device
 */
int pinctrl_force_default(struct pinctrl_dev *pctldev)
{
	if (!IS_ERR(pctldev->p) && !IS_ERR(pctldev->hog_default))
		return pinctrl_select_state(pctldev->p, pctldev->hog_default);
	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_force_default);

/**
 * pinctrl_init_done() - tell pinctrl probe is done
 *
 * We'll use this time to switch the pins from "init" to "default" unless the
 * driver selected some other state.
 *
 * @dev: device to that's done probing
 */
int pinctrl_init_done(struct device *dev)
{
	struct dev_pin_info *pins = dev->pins;
	int ret;

	if (!pins)
		return 0;

	if (IS_ERR(pins->init_state))
		return 0; /* No such state */

	if (pins->p->state != pins->init_state)
		return 0; /* Not at init anyway */

	if (IS_ERR(pins->default_state))
		return 0; /* No default state */

	ret = pinctrl_select_state(pins->p, pins->default_state);
	if (ret)
		dev_err(dev, "failed to activate default pinctrl state\n");

	return ret;
}

#ifdef CONFIG_PM

/**
 * pinctrl_pm_select_state() - select pinctrl state for PM
 * @dev: device to select default state for
 * @state: state to set
 */
static int pinctrl_pm_select_state(struct device *dev,
				   struct pinctrl_state *state)
{
	struct dev_pin_info *pins = dev->pins;
	int ret;

	if (IS_ERR(state))
		return 0; /* No such state */
	ret = pinctrl_select_state(pins->p, state);
	if (ret)
		dev_err(dev, "failed to activate pinctrl state %s\n",
			state->name);
	return ret;
}

/**
 * pinctrl_pm_select_default_state() - select default pinctrl state for PM
 * @dev: device to select default state for
 */
int pinctrl_pm_select_default_state(struct device *dev)
{
	if (!dev->pins)
		return 0;

	return pinctrl_pm_select_state(dev, dev->pins->default_state);
}
EXPORT_SYMBOL_GPL(pinctrl_pm_select_default_state);

/**
 * pinctrl_pm_select_sleep_state() - select sleep pinctrl state for PM
 * @dev: device to select sleep state for
 */
int pinctrl_pm_select_sleep_state(struct device *dev)
{
	if (!dev->pins)
		return 0;

	return pinctrl_pm_select_state(dev, dev->pins->sleep_state);
}
EXPORT_SYMBOL_GPL(pinctrl_pm_select_sleep_state);

/**
 * pinctrl_pm_select_idle_state() - select idle pinctrl state for PM
 * @dev: device to select idle state for
 */
int pinctrl_pm_select_idle_state(struct device *dev)
{
	if (!dev->pins)
		return 0;

	return pinctrl_pm_select_state(dev, dev->pins->idle_state);
}
EXPORT_SYMBOL_GPL(pinctrl_pm_select_idle_state);
#endif

#ifdef CONFIG_DEBUG_FS

static int pinctrl_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;
	unsigned i, pin;

	seq_printf(s, "registered pins: %d\n", pctldev->desc->npins);

	mutex_lock(&pctldev->mutex);

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Pin space may be sparse */
		if (desc == NULL)
			continue;

		seq_printf(s, "pin %d (%s) ", pin, desc->name);

		/* Driver-specific info per pin */
		if (ops->pin_dbg_show)
			ops->pin_dbg_show(pctldev, s, pin);

		seq_puts(s, "\n");
	}

	mutex_unlock(&pctldev->mutex);

	return 0;
}

static int pinctrl_groups_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;
	unsigned ngroups, selector = 0;

	mutex_lock(&pctldev->mutex);

	ngroups = ops->get_groups_count(pctldev);

	seq_puts(s, "registered pin groups:\n");
	while (selector < ngroups) {
		const unsigned *pins = NULL;
		unsigned num_pins = 0;
		const char *gname = ops->get_group_name(pctldev, selector);
		const char *pname;
		int ret = 0;
		int i;

		if (ops->get_group_pins)
			ret = ops->get_group_pins(pctldev, selector,
						  &pins, &num_pins);
		if (ret)
			seq_printf(s, "%s [ERROR GETTING PINS]\n",
				   gname);
		else {
			seq_printf(s, "group: %s\n", gname);
			for (i = 0; i < num_pins; i++) {
				pname = pin_get_name(pctldev, pins[i]);
				if (WARN_ON(!pname)) {
					mutex_unlock(&pctldev->mutex);
					return -EINVAL;
				}
				seq_printf(s, "pin %d (%s)\n", pins[i], pname);
			}
			seq_puts(s, "\n");
		}
		selector++;
	}

	mutex_unlock(&pctldev->mutex);

	return 0;
}

static int pinctrl_gpioranges_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	struct pinctrl_gpio_range *range = NULL;

	seq_puts(s, "GPIO ranges handled:\n");

	mutex_lock(&pctldev->mutex);

	/* Loop over the ranges */
	list_for_each_entry(range, &pctldev->gpio_ranges, node) {
		if (range->pins) {
			int a;
			seq_printf(s, "%u: %s GPIOS [%u - %u] PINS {",
				range->id, range->name,
				range->base, (range->base + range->npins - 1));
			for (a = 0; a < range->npins - 1; a++)
				seq_printf(s, "%u, ", range->pins[a]);
			seq_printf(s, "%u}\n", range->pins[a]);
		}
		else
			seq_printf(s, "%u: %s GPIOS [%u - %u] PINS [%u - %u]\n",
				range->id, range->name,
				range->base, (range->base + range->npins - 1),
				range->pin_base,
				(range->pin_base + range->npins - 1));
	}

	mutex_unlock(&pctldev->mutex);

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

static inline const char *map_type(enum pinctrl_map_type type)
{
	static const char * const names[] = {
		"INVALID",
		"DUMMY_STATE",
		"MUX_GROUP",
		"CONFIGS_PIN",
		"CONFIGS_GROUP",
	};

	if (type >= ARRAY_SIZE(names))
		return "UNKNOWN";

	return names[type];
}

static int pinctrl_maps_show(struct seq_file *s, void *what)
{
	struct pinctrl_maps *maps_node;
	int i;
	struct pinctrl_map const *map;

	seq_puts(s, "Pinctrl maps:\n");

	mutex_lock(&pinctrl_maps_mutex);
	for_each_maps(maps_node, i, map) {
		seq_printf(s, "device %s\nstate %s\ntype %s (%d)\n",
			   map->dev_name, map->name, map_type(map->type),
			   map->type);

		if (map->type != PIN_MAP_TYPE_DUMMY_STATE)
			seq_printf(s, "controlling device %s\n",
				   map->ctrl_dev_name);

		switch (map->type) {
		case PIN_MAP_TYPE_MUX_GROUP:
			pinmux_show_map(s, map);
			break;
		case PIN_MAP_TYPE_CONFIGS_PIN:
		case PIN_MAP_TYPE_CONFIGS_GROUP:
			pinconf_show_map(s, map);
			break;
		default:
			break;
		}

		seq_printf(s, "\n");
	}
	mutex_unlock(&pinctrl_maps_mutex);

	return 0;
}

static int pinctrl_show(struct seq_file *s, void *what)
{
	struct pinctrl *p;
	struct pinctrl_state *state;
	struct pinctrl_setting *setting;

	seq_puts(s, "Requested pin control handlers their pinmux maps:\n");

	mutex_lock(&pinctrl_list_mutex);

	list_for_each_entry(p, &pinctrl_list, node) {
		seq_printf(s, "device: %s current state: %s\n",
			   dev_name(p->dev),
			   p->state ? p->state->name : "none");

		list_for_each_entry(state, &p->states, node) {
			seq_printf(s, "  state: %s\n", state->name);

			list_for_each_entry(setting, &state->settings, node) {
				struct pinctrl_dev *pctldev = setting->pctldev;

				seq_printf(s, "    type: %s controller %s ",
					   map_type(setting->type),
					   pinctrl_dev_get_name(pctldev));

				switch (setting->type) {
				case PIN_MAP_TYPE_MUX_GROUP:
					pinmux_show_setting(s, setting);
					break;
				case PIN_MAP_TYPE_CONFIGS_PIN:
				case PIN_MAP_TYPE_CONFIGS_GROUP:
					pinconf_show_setting(s, setting);
					break;
				default:
					break;
				}
			}
		}
	}

	mutex_unlock(&pinctrl_list_mutex);

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
	if (pctldev->desc->pmxops)
		pinmux_init_device_debugfs(device_root, pctldev);
	if (pctldev->desc->confops)
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

static int pinctrl_check_ops(struct pinctrl_dev *pctldev)
{
	const struct pinctrl_ops *ops = pctldev->desc->pctlops;

	if (!ops ||
	    !ops->get_groups_count ||
	    !ops->get_group_name)
		return -EINVAL;

	return 0;
}

/**
 * pinctrl_init_controller() - init a pin controller device
 * @pctldesc: descriptor for this pin controller
 * @dev: parent device for this pin controller
 * @driver_data: private pin controller data for this pin controller
 */
static struct pinctrl_dev *
pinctrl_init_controller(struct pinctrl_desc *pctldesc, struct device *dev,
			void *driver_data)
{
	struct pinctrl_dev *pctldev;
	int ret;

	if (!pctldesc)
		return ERR_PTR(-EINVAL);
	if (!pctldesc->name)
		return ERR_PTR(-EINVAL);

	pctldev = kzalloc(sizeof(*pctldev), GFP_KERNEL);
	if (!pctldev)
		return ERR_PTR(-ENOMEM);

	/* Initialize pin control device struct */
	pctldev->owner = pctldesc->owner;
	pctldev->desc = pctldesc;
	pctldev->driver_data = driver_data;
	INIT_RADIX_TREE(&pctldev->pin_desc_tree, GFP_KERNEL);
#ifdef CONFIG_GENERIC_PINCTRL_GROUPS
	INIT_RADIX_TREE(&pctldev->pin_group_tree, GFP_KERNEL);
#endif
#ifdef CONFIG_GENERIC_PINMUX_FUNCTIONS
	INIT_RADIX_TREE(&pctldev->pin_function_tree, GFP_KERNEL);
#endif
	INIT_LIST_HEAD(&pctldev->gpio_ranges);
	INIT_LIST_HEAD(&pctldev->node);
	pctldev->dev = dev;
	mutex_init(&pctldev->mutex);

	/* check core ops for sanity */
	ret = pinctrl_check_ops(pctldev);
	if (ret) {
		dev_err(dev, "pinctrl ops lacks necessary functions\n");
		goto out_err;
	}

	/* If we're implementing pinmuxing, check the ops for sanity */
	if (pctldesc->pmxops) {
		ret = pinmux_check_ops(pctldev);
		if (ret)
			goto out_err;
	}

	/* If we're implementing pinconfig, check the ops for sanity */
	if (pctldesc->confops) {
		ret = pinconf_check_ops(pctldev);
		if (ret)
			goto out_err;
	}

	/* Register all the pins */
	dev_dbg(dev, "try to register %d pins ...\n",  pctldesc->npins);
	ret = pinctrl_register_pins(pctldev, pctldesc->pins, pctldesc->npins);
	if (ret) {
		dev_err(dev, "error during pin registration\n");
		pinctrl_free_pindescs(pctldev, pctldesc->pins,
				      pctldesc->npins);
		goto out_err;
	}

	return pctldev;

out_err:
	mutex_destroy(&pctldev->mutex);
	kfree(pctldev);
	return ERR_PTR(ret);
}

static int pinctrl_claim_hogs(struct pinctrl_dev *pctldev)
{
	pctldev->p = create_pinctrl(pctldev->dev, pctldev);
	if (PTR_ERR(pctldev->p) == -ENODEV) {
		dev_dbg(pctldev->dev, "no hogs found\n");

		return 0;
	}

	if (IS_ERR(pctldev->p)) {
		dev_err(pctldev->dev, "error claiming hogs: %li\n",
			PTR_ERR(pctldev->p));

		return PTR_ERR(pctldev->p);
	}

	kref_get(&pctldev->p->users);
	pctldev->hog_default =
		pinctrl_lookup_state(pctldev->p, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(pctldev->hog_default)) {
		dev_dbg(pctldev->dev,
			"failed to lookup the default state\n");
	} else {
		if (pinctrl_select_state(pctldev->p,
					 pctldev->hog_default))
			dev_err(pctldev->dev,
				"failed to select default state\n");
	}

	pctldev->hog_sleep =
		pinctrl_lookup_state(pctldev->p,
				     PINCTRL_STATE_SLEEP);
	if (IS_ERR(pctldev->hog_sleep))
		dev_dbg(pctldev->dev,
			"failed to lookup the sleep state\n");

	return 0;
}

int pinctrl_enable(struct pinctrl_dev *pctldev)
{
	int error;

	error = pinctrl_claim_hogs(pctldev);
	if (error) {
		dev_err(pctldev->dev, "could not claim hogs: %i\n",
			error);
		mutex_destroy(&pctldev->mutex);
		kfree(pctldev);

		return error;
	}

	mutex_lock(&pinctrldev_list_mutex);
	list_add_tail(&pctldev->node, &pinctrldev_list);
	mutex_unlock(&pinctrldev_list_mutex);

	pinctrl_init_device_debugfs(pctldev);

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_enable);

/**
 * pinctrl_register() - register a pin controller device
 * @pctldesc: descriptor for this pin controller
 * @dev: parent device for this pin controller
 * @driver_data: private pin controller data for this pin controller
 *
 * Note that pinctrl_register() is known to have problems as the pin
 * controller driver functions are called before the driver has a
 * struct pinctrl_dev handle. To avoid issues later on, please use the
 * new pinctrl_register_and_init() below instead.
 */
struct pinctrl_dev *pinctrl_register(struct pinctrl_desc *pctldesc,
				    struct device *dev, void *driver_data)
{
	struct pinctrl_dev *pctldev;
	int error;

	pctldev = pinctrl_init_controller(pctldesc, dev, driver_data);
	if (IS_ERR(pctldev))
		return pctldev;

	error = pinctrl_enable(pctldev);
	if (error)
		return ERR_PTR(error);

	return pctldev;

}
EXPORT_SYMBOL_GPL(pinctrl_register);

/**
 * pinctrl_register_and_init() - register and init pin controller device
 * @pctldesc: descriptor for this pin controller
 * @dev: parent device for this pin controller
 * @driver_data: private pin controller data for this pin controller
 * @pctldev: pin controller device
 *
 * Note that pinctrl_enable() still needs to be manually called after
 * this once the driver is ready.
 */
int pinctrl_register_and_init(struct pinctrl_desc *pctldesc,
			      struct device *dev, void *driver_data,
			      struct pinctrl_dev **pctldev)
{
	struct pinctrl_dev *p;

	p = pinctrl_init_controller(pctldesc, dev, driver_data);
	if (IS_ERR(p))
		return PTR_ERR(p);

	/*
	 * We have pinctrl_start() call functions in the pin controller
	 * driver with create_pinctrl() for at least dt_node_to_map(). So
	 * let's make sure pctldev is properly initialized for the
	 * pin controller driver before we do anything.
	 */
	*pctldev = p;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_register_and_init);

/**
 * pinctrl_unregister() - unregister pinmux
 * @pctldev: pin controller to unregister
 *
 * Called by pinmux drivers to unregister a pinmux.
 */
void pinctrl_unregister(struct pinctrl_dev *pctldev)
{
	struct pinctrl_gpio_range *range, *n;

	if (pctldev == NULL)
		return;

	mutex_lock(&pctldev->mutex);
	pinctrl_remove_device_debugfs(pctldev);
	mutex_unlock(&pctldev->mutex);

	if (!IS_ERR_OR_NULL(pctldev->p))
		pinctrl_put(pctldev->p);

	mutex_lock(&pinctrldev_list_mutex);
	mutex_lock(&pctldev->mutex);
	/* TODO: check that no pinmuxes are still active? */
	list_del(&pctldev->node);
	pinmux_generic_free_functions(pctldev);
	pinctrl_generic_free_groups(pctldev);
	/* Destroy descriptor tree */
	pinctrl_free_pindescs(pctldev, pctldev->desc->pins,
			      pctldev->desc->npins);
	/* remove gpio ranges map */
	list_for_each_entry_safe(range, n, &pctldev->gpio_ranges, node)
		list_del(&range->node);

	mutex_unlock(&pctldev->mutex);
	mutex_destroy(&pctldev->mutex);
	kfree(pctldev);
	mutex_unlock(&pinctrldev_list_mutex);
}
EXPORT_SYMBOL_GPL(pinctrl_unregister);

static void devm_pinctrl_dev_release(struct device *dev, void *res)
{
	struct pinctrl_dev *pctldev = *(struct pinctrl_dev **)res;

	pinctrl_unregister(pctldev);
}

static int devm_pinctrl_dev_match(struct device *dev, void *res, void *data)
{
	struct pctldev **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

/**
 * devm_pinctrl_register() - Resource managed version of pinctrl_register().
 * @dev: parent device for this pin controller
 * @pctldesc: descriptor for this pin controller
 * @driver_data: private pin controller data for this pin controller
 *
 * Returns an error pointer if pincontrol register failed. Otherwise
 * it returns valid pinctrl handle.
 *
 * The pinctrl device will be automatically released when the device is unbound.
 */
struct pinctrl_dev *devm_pinctrl_register(struct device *dev,
					  struct pinctrl_desc *pctldesc,
					  void *driver_data)
{
	struct pinctrl_dev **ptr, *pctldev;

	ptr = devres_alloc(devm_pinctrl_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pctldev = pinctrl_register(pctldesc, dev, driver_data);
	if (IS_ERR(pctldev)) {
		devres_free(ptr);
		return pctldev;
	}

	*ptr = pctldev;
	devres_add(dev, ptr);

	return pctldev;
}
EXPORT_SYMBOL_GPL(devm_pinctrl_register);

/**
 * devm_pinctrl_register_and_init() - Resource managed pinctrl register and init
 * @dev: parent device for this pin controller
 * @pctldesc: descriptor for this pin controller
 * @driver_data: private pin controller data for this pin controller
 *
 * Returns an error pointer if pincontrol register failed. Otherwise
 * it returns valid pinctrl handle.
 *
 * The pinctrl device will be automatically released when the device is unbound.
 */
int devm_pinctrl_register_and_init(struct device *dev,
				   struct pinctrl_desc *pctldesc,
				   void *driver_data,
				   struct pinctrl_dev **pctldev)
{
	struct pinctrl_dev **ptr;
	int error;

	ptr = devres_alloc(devm_pinctrl_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	error = pinctrl_register_and_init(pctldesc, dev, driver_data, pctldev);
	if (error) {
		devres_free(ptr);
		return error;
	}

	*ptr = *pctldev;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_pinctrl_register_and_init);

/**
 * devm_pinctrl_unregister() - Resource managed version of pinctrl_unregister().
 * @dev: device for which which resource was allocated
 * @pctldev: the pinctrl device to unregister.
 */
void devm_pinctrl_unregister(struct device *dev, struct pinctrl_dev *pctldev)
{
	WARN_ON(devres_release(dev, devm_pinctrl_dev_release,
			       devm_pinctrl_dev_match, pctldev));
}
EXPORT_SYMBOL_GPL(devm_pinctrl_unregister);

static int __init pinctrl_init(void)
{
	pr_info("initialized pinctrl subsystem\n");
	pinctrl_init_debugfs();
	return 0;
}

/* init early since many drivers really need to initialized pinmux early */
core_initcall(pinctrl_init);
