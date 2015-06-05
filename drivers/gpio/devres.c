/*
 * drivers/gpio/devres.c - managed gpio resources
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This file is based on kernel/irq/devres.c
 *
 * Copyright (c) 2011 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/gfp.h>

static void devm_gpiod_release(struct device *dev, void *res)
{
	struct gpio_desc **desc = res;

	gpiod_put(*desc);
}

static int devm_gpiod_match(struct device *dev, void *res, void *data)
{
	struct gpio_desc **this = res, **gpio = data;

	return *this == *gpio;
}

static void devm_gpiod_release_array(struct device *dev, void *res)
{
	struct gpio_descs **descs = res;

	gpiod_put_array(*descs);
}

static int devm_gpiod_match_array(struct device *dev, void *res, void *data)
{
	struct gpio_descs **this = res, **gpios = data;

	return *this == *gpios;
}

/**
 * devm_gpiod_get - Resource-managed gpiod_get()
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Managed gpiod_get(). GPIO descriptors returned from this function are
 * automatically disposed on driver detach. See gpiod_get() for detailed
 * information about behavior and return values.
 */
struct gpio_desc *__must_check __devm_gpiod_get(struct device *dev,
					      const char *con_id,
					      enum gpiod_flags flags)
{
	return devm_gpiod_get_index(dev, con_id, 0, flags);
}
EXPORT_SYMBOL(__devm_gpiod_get);

/**
 * devm_gpiod_get_optional - Resource-managed gpiod_get_optional()
 * @dev: GPIO consumer
 * @con_id: function within the GPIO consumer
 * @flags: optional GPIO initialization flags
 *
 * Managed gpiod_get_optional(). GPIO descriptors returned from this function
 * are automatically disposed on driver detach. See gpiod_get_optional() for
 * detailed information about behavior and return values.
 */
struct gpio_desc *__must_check __devm_gpiod_get_optional(struct device *dev,
						       const char *con_id,
						       enum gpiod_flags flags)
{
	return devm_gpiod_get_index_optional(dev, con_id, 0, flags);
}
EXPORT_SYMBOL(__devm_gpiod_get_optional);

/**
 * devm_gpiod_get_index - Resource-managed gpiod_get_index()
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @idx:	index of the GPIO to obtain in the consumer
 * @flags:	optional GPIO initialization flags
 *
 * Managed gpiod_get_index(). GPIO descriptors returned from this function are
 * automatically disposed on driver detach. See gpiod_get_index() for detailed
 * information about behavior and return values.
 */
struct gpio_desc *__must_check __devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx,
						    enum gpiod_flags flags)
{
	struct gpio_desc **dr;
	struct gpio_desc *desc;

	dr = devres_alloc(devm_gpiod_release, sizeof(struct gpio_desc *),
			  GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	desc = gpiod_get_index(dev, con_id, idx, flags);
	if (IS_ERR(desc)) {
		devres_free(dr);
		return desc;
	}

	*dr = desc;
	devres_add(dev, dr);

	return desc;
}
EXPORT_SYMBOL(__devm_gpiod_get_index);

/**
 * devm_get_gpiod_from_child - get a GPIO descriptor from a device's child node
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @child:	firmware node (child of @dev)
 *
 * GPIO descriptors returned from this function are automatically disposed on
 * driver detach.
 */
struct gpio_desc *devm_get_gpiod_from_child(struct device *dev,
					    const char *con_id,
					    struct fwnode_handle *child)
{
	static const char * const suffixes[] = { "gpios", "gpio" };
	char prop_name[32]; /* 32 is max size of property name */
	struct gpio_desc **dr;
	struct gpio_desc *desc;
	unsigned int i;

	dr = devres_alloc(devm_gpiod_release, sizeof(struct gpio_desc *),
			  GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(suffixes); i++) {
		if (con_id)
			snprintf(prop_name, sizeof(prop_name), "%s-%s",
					    con_id, suffixes[i]);
		else
			snprintf(prop_name, sizeof(prop_name), "%s",
							       suffixes[i]);

		desc = fwnode_get_named_gpiod(child, prop_name);
		if (!IS_ERR(desc) || (PTR_ERR(desc) == -EPROBE_DEFER))
			break;
	}
	if (IS_ERR(desc)) {
		devres_free(dr);
		return desc;
	}

	*dr = desc;
	devres_add(dev, dr);

	return desc;
}
EXPORT_SYMBOL(devm_get_gpiod_from_child);

/**
 * devm_gpiod_get_index_optional - Resource-managed gpiod_get_index_optional()
 * @dev: GPIO consumer
 * @con_id: function within the GPIO consumer
 * @index: index of the GPIO to obtain in the consumer
 * @flags: optional GPIO initialization flags
 *
 * Managed gpiod_get_index_optional(). GPIO descriptors returned from this
 * function are automatically disposed on driver detach. See
 * gpiod_get_index_optional() for detailed information about behavior and
 * return values.
 */
struct gpio_desc *__must_check __devm_gpiod_get_index_optional(struct device *dev,
							     const char *con_id,
							     unsigned int index,
							 enum gpiod_flags flags)
{
	struct gpio_desc *desc;

	desc = devm_gpiod_get_index(dev, con_id, index, flags);
	if (IS_ERR(desc)) {
		if (PTR_ERR(desc) == -ENOENT)
			return NULL;
	}

	return desc;
}
EXPORT_SYMBOL(__devm_gpiod_get_index_optional);

/**
 * devm_gpiod_get_array - Resource-managed gpiod_get_array()
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Managed gpiod_get_array(). GPIO descriptors returned from this function are
 * automatically disposed on driver detach. See gpiod_get_array() for detailed
 * information about behavior and return values.
 */
struct gpio_descs *__must_check devm_gpiod_get_array(struct device *dev,
						     const char *con_id,
						     enum gpiod_flags flags)
{
	struct gpio_descs **dr;
	struct gpio_descs *descs;

	dr = devres_alloc(devm_gpiod_release_array,
			  sizeof(struct gpio_descs *), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	descs = gpiod_get_array(dev, con_id, flags);
	if (IS_ERR(descs)) {
		devres_free(dr);
		return descs;
	}

	*dr = descs;
	devres_add(dev, dr);

	return descs;
}
EXPORT_SYMBOL(devm_gpiod_get_array);

/**
 * devm_gpiod_get_array_optional - Resource-managed gpiod_get_array_optional()
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Managed gpiod_get_array_optional(). GPIO descriptors returned from this
 * function are automatically disposed on driver detach.
 * See gpiod_get_array_optional() for detailed information about behavior and
 * return values.
 */
struct gpio_descs *__must_check
devm_gpiod_get_array_optional(struct device *dev, const char *con_id,
			      enum gpiod_flags flags)
{
	struct gpio_descs *descs;

	descs = devm_gpiod_get_array(dev, con_id, flags);
	if (IS_ERR(descs) && (PTR_ERR(descs) == -ENOENT))
		return NULL;

	return descs;
}
EXPORT_SYMBOL(devm_gpiod_get_array_optional);

/**
 * devm_gpiod_put - Resource-managed gpiod_put()
 * @desc:	GPIO descriptor to dispose of
 *
 * Dispose of a GPIO descriptor obtained with devm_gpiod_get() or
 * devm_gpiod_get_index(). Normally this function will not be called as the GPIO
 * will be disposed of by the resource management code.
 */
void devm_gpiod_put(struct device *dev, struct gpio_desc *desc)
{
	WARN_ON(devres_release(dev, devm_gpiod_release, devm_gpiod_match,
		&desc));
}
EXPORT_SYMBOL(devm_gpiod_put);

/**
 * devm_gpiod_put_array - Resource-managed gpiod_put_array()
 * @descs:	GPIO descriptor array to dispose of
 *
 * Dispose of an array of GPIO descriptors obtained with devm_gpiod_get_array().
 * Normally this function will not be called as the GPIOs will be disposed of
 * by the resource management code.
 */
void devm_gpiod_put_array(struct device *dev, struct gpio_descs *descs)
{
	WARN_ON(devres_release(dev, devm_gpiod_release_array,
			       devm_gpiod_match_array, &descs));
}
EXPORT_SYMBOL(devm_gpiod_put_array);




static void devm_gpio_release(struct device *dev, void *res)
{
	unsigned *gpio = res;

	gpio_free(*gpio);
}

static int devm_gpio_match(struct device *dev, void *res, void *data)
{
	unsigned *this = res, *gpio = data;

	return *this == *gpio;
}

/**
 *      devm_gpio_request - request a GPIO for a managed device
 *      @dev: device to request the GPIO for
 *      @gpio: GPIO to allocate
 *      @label: the name of the requested GPIO
 *
 *      Except for the extra @dev argument, this function takes the
 *      same arguments and performs the same function as
 *      gpio_request().  GPIOs requested with this function will be
 *      automatically freed on driver detach.
 *
 *      If an GPIO allocated with this function needs to be freed
 *      separately, devm_gpio_free() must be used.
 */

int devm_gpio_request(struct device *dev, unsigned gpio, const char *label)
{
	unsigned *dr;
	int rc;

	dr = devres_alloc(devm_gpio_release, sizeof(unsigned), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = gpio_request(gpio, label);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = gpio;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL(devm_gpio_request);

/**
 *	devm_gpio_request_one - request a single GPIO with initial setup
 *	@dev:   device to request for
 *	@gpio:	the GPIO number
 *	@flags:	GPIO configuration as specified by GPIOF_*
 *	@label:	a literal description string of this GPIO
 */
int devm_gpio_request_one(struct device *dev, unsigned gpio,
			  unsigned long flags, const char *label)
{
	unsigned *dr;
	int rc;

	dr = devres_alloc(devm_gpio_release, sizeof(unsigned), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = gpio_request_one(gpio, flags, label);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = gpio;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL(devm_gpio_request_one);

/**
 *      devm_gpio_free - free a GPIO
 *      @dev: device to free GPIO for
 *      @gpio: GPIO to free
 *
 *      Except for the extra @dev argument, this function takes the
 *      same arguments and performs the same function as gpio_free().
 *      This function instead of gpio_free() should be used to manually
 *      free GPIOs allocated with devm_gpio_request().
 */
void devm_gpio_free(struct device *dev, unsigned int gpio)
{

	WARN_ON(devres_release(dev, devm_gpio_release, devm_gpio_match,
		&gpio));
}
EXPORT_SYMBOL(devm_gpio_free);
