/* SPDX-License-Identifier: GPL-2.0 */
/*
 * devres.c - managed gpio resources
 * This file is based on kernel/irq/devres.c
 *
 * Copyright (c) 2011 John Crispin <john@phrozen.org>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/types.h>

#include <linux/gpio/consumer.h>

#include "gpiolib.h"

struct fwnode_handle;
struct lock_class_key;

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
 *
 * Returns:
 * The GPIO descriptor corresponding to the function @con_id of device
 * dev, %-ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check devm_gpiod_get(struct device *dev,
					      const char *con_id,
					      enum gpiod_flags flags)
{
	return devm_gpiod_get_index(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(devm_gpiod_get);

/**
 * devm_gpiod_get_optional - Resource-managed gpiod_get_optional()
 * @dev: GPIO consumer
 * @con_id: function within the GPIO consumer
 * @flags: optional GPIO initialization flags
 *
 * Managed gpiod_get_optional(). GPIO descriptors returned from this function
 * are automatically disposed on driver detach. See gpiod_get_optional() for
 * detailed information about behavior and return values.
 *
 * Returns:
 * The GPIO descriptor corresponding to the function @con_id of device
 * dev, NULL if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check devm_gpiod_get_optional(struct device *dev,
						       const char *con_id,
						       enum gpiod_flags flags)
{
	return devm_gpiod_get_index_optional(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(devm_gpiod_get_optional);

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
 *
 * Returns:
 * The GPIO descriptor corresponding to the function @con_id of device
 * dev, %-ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx,
						    enum gpiod_flags flags)
{
	struct gpio_desc **dr;
	struct gpio_desc *desc;

	desc = gpiod_get_index(dev, con_id, idx, flags);
	if (IS_ERR(desc))
		return desc;

	/*
	 * For non-exclusive GPIO descriptors, check if this descriptor is
	 * already under resource management by this device.
	 */
	if (flags & GPIOD_FLAGS_BIT_NONEXCLUSIVE) {
		struct devres *dres;

		dres = devres_find(dev, devm_gpiod_release,
				   devm_gpiod_match, &desc);
		if (dres)
			return desc;
	}

	dr = devres_alloc(devm_gpiod_release, sizeof(struct gpio_desc *),
			  GFP_KERNEL);
	if (!dr) {
		gpiod_put(desc);
		return ERR_PTR(-ENOMEM);
	}

	*dr = desc;
	devres_add(dev, dr);

	return desc;
}
EXPORT_SYMBOL_GPL(devm_gpiod_get_index);

/**
 * devm_fwnode_gpiod_get_index - get a GPIO descriptor from a given node
 * @dev:	GPIO consumer
 * @fwnode:	firmware node containing GPIO reference
 * @con_id:	function within the GPIO consumer
 * @index:	index of the GPIO to obtain in the consumer
 * @flags:	GPIO initialization flags
 * @label:	label to attach to the requested GPIO
 *
 * GPIO descriptors returned from this function are automatically disposed on
 * driver detach.
 *
 * Returns:
 * The GPIO descriptor corresponding to the function @con_id of device
 * dev, %-ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *devm_fwnode_gpiod_get_index(struct device *dev,
					      struct fwnode_handle *fwnode,
					      const char *con_id, int index,
					      enum gpiod_flags flags,
					      const char *label)
{
	struct gpio_desc **dr;
	struct gpio_desc *desc;

	dr = devres_alloc(devm_gpiod_release, sizeof(struct gpio_desc *),
			  GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	desc = gpiod_find_and_request(dev, fwnode, con_id, index, flags, label, false);
	if (IS_ERR(desc)) {
		devres_free(dr);
		return desc;
	}

	*dr = desc;
	devres_add(dev, dr);

	return desc;
}
EXPORT_SYMBOL_GPL(devm_fwnode_gpiod_get_index);

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
 *
 * Returns:
 * The GPIO descriptor corresponding to the function @con_id of device
 * dev, %NULL if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check devm_gpiod_get_index_optional(struct device *dev,
							     const char *con_id,
							     unsigned int index,
							     enum gpiod_flags flags)
{
	struct gpio_desc *desc;

	desc = devm_gpiod_get_index(dev, con_id, index, flags);
	if (gpiod_not_found(desc))
		return NULL;

	return desc;
}
EXPORT_SYMBOL_GPL(devm_gpiod_get_index_optional);

/**
 * devm_gpiod_get_array - Resource-managed gpiod_get_array()
 * @dev:	GPIO consumer
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Managed gpiod_get_array(). GPIO descriptors returned from this function are
 * automatically disposed on driver detach. See gpiod_get_array() for detailed
 * information about behavior and return values.
 *
 * Returns:
 * The GPIO descriptors corresponding to the function @con_id of device
 * dev, %-ENOENT if no GPIO has been assigned to the requested function,
 * or another IS_ERR() code if an error occurred while trying to acquire
 * the GPIOs.
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
EXPORT_SYMBOL_GPL(devm_gpiod_get_array);

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
 *
 * Returns:
 * The GPIO descriptors corresponding to the function @con_id of device
 * dev, %NULL if no GPIO has been assigned to the requested function,
 * or another IS_ERR() code if an error occurred while trying to acquire
 * the GPIOs.
 */
struct gpio_descs *__must_check
devm_gpiod_get_array_optional(struct device *dev, const char *con_id,
			      enum gpiod_flags flags)
{
	struct gpio_descs *descs;

	descs = devm_gpiod_get_array(dev, con_id, flags);
	if (gpiod_not_found(descs))
		return NULL;

	return descs;
}
EXPORT_SYMBOL_GPL(devm_gpiod_get_array_optional);

/**
 * devm_gpiod_put - Resource-managed gpiod_put()
 * @dev:	GPIO consumer
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
EXPORT_SYMBOL_GPL(devm_gpiod_put);

/**
 * devm_gpiod_unhinge - Remove resource management from a gpio descriptor
 * @dev:	GPIO consumer
 * @desc:	GPIO descriptor to remove resource management from
 *
 * Remove resource management from a GPIO descriptor. This is needed when
 * you want to hand over lifecycle management of a descriptor to another
 * mechanism.
 */

void devm_gpiod_unhinge(struct device *dev, struct gpio_desc *desc)
{
	int ret;

	if (IS_ERR_OR_NULL(desc))
		return;
	ret = devres_destroy(dev, devm_gpiod_release,
			     devm_gpiod_match, &desc);
	/*
	 * If the GPIO descriptor is requested as nonexclusive, we
	 * may call this function several times on the same descriptor
	 * so it is OK if devres_destroy() returns -ENOENT.
	 */
	if (ret == -ENOENT)
		return;
	/* Anything else we should warn about */
	WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(devm_gpiod_unhinge);

/**
 * devm_gpiod_put_array - Resource-managed gpiod_put_array()
 * @dev:	GPIO consumer
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
EXPORT_SYMBOL_GPL(devm_gpiod_put_array);

static void devm_gpio_chip_release(void *data)
{
	struct gpio_chip *gc = data;

	gpiochip_remove(gc);
}

/**
 * devm_gpiochip_add_data_with_key() - Resource managed gpiochip_add_data_with_key()
 * @dev: pointer to the device that gpio_chip belongs to.
 * @gc: the GPIO chip to register
 * @data: driver-private data associated with this chip
 * @lock_key: lockdep class for IRQ lock
 * @request_key: lockdep class for IRQ request
 *
 * Context: potentially before irqs will work
 *
 * The gpio chip automatically be released when the device is unbound.
 *
 * Returns:
 * A negative errno if the chip can't be registered, such as because the
 * gc->base is invalid or already associated with a different chip.
 * Otherwise it returns zero as a success code.
 */
int devm_gpiochip_add_data_with_key(struct device *dev, struct gpio_chip *gc, void *data,
				    struct lock_class_key *lock_key,
				    struct lock_class_key *request_key)
{
	int ret;

	ret = gpiochip_add_data_with_key(gc, data, lock_key, request_key);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_gpio_chip_release, gc);
}
EXPORT_SYMBOL_GPL(devm_gpiochip_add_data_with_key);
