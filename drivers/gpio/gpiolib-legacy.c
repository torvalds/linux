// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/gfp.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include <linux/gpio.h>

#include "gpiolib.h"

/*
 * **DEPRECATED** This function is deprecated and must not be used in new code.
 */
void gpio_free(unsigned gpio)
{
	gpiod_free(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_free);

/**
 * gpio_request_one - request a single GPIO with initial configuration
 * @gpio:	the GPIO number
 * @flags:	GPIO configuration as specified by GPIOF_*
 * @label:	a literal description string of this GPIO
 *
 * **DEPRECATED** This function is deprecated and must not be used in new code.
 *
 * Returns:
 * 0 on success, or negative errno on failure.
 */
int gpio_request_one(unsigned gpio, unsigned long flags, const char *label)
{
	struct gpio_desc *desc;
	int err;

	/* Compatibility: assume unavailable "valid" GPIOs will appear later */
	desc = gpio_to_desc(gpio);
	if (!desc)
		return -EPROBE_DEFER;

	err = gpiod_request(desc, label);
	if (err)
		return err;

	if (flags & GPIOF_IN)
		err = gpiod_direction_input(desc);
	else
		err = gpiod_direction_output_raw(desc, !!(flags & GPIOF_OUT_INIT_HIGH));

	if (err)
		goto free_gpio;

	return 0;

 free_gpio:
	gpiod_free(desc);
	return err;
}
EXPORT_SYMBOL_GPL(gpio_request_one);

/*
 * **DEPRECATED** This function is deprecated and must not be used in new code.
 */
int gpio_request(unsigned gpio, const char *label)
{
	struct gpio_desc *desc;

	/* Compatibility: assume unavailable "valid" GPIOs will appear later */
	desc = gpio_to_desc(gpio);
	if (!desc)
		return -EPROBE_DEFER;

	return gpiod_request(desc, label);
}
EXPORT_SYMBOL_GPL(gpio_request);

static void devm_gpio_release(struct device *dev, void *res)
{
	unsigned *gpio = res;

	gpio_free(*gpio);
}

/**
 * devm_gpio_request - request a GPIO for a managed device
 * @dev: device to request the GPIO for
 * @gpio: GPIO to allocate
 * @label: the name of the requested GPIO
 *
 * Except for the extra @dev argument, this function takes the
 * same arguments and performs the same function as gpio_request().
 * GPIOs requested with this function will be automatically freed
 * on driver detach.
 *
 * **DEPRECATED** This function is deprecated and must not be used in new code.
 *
 * Returns:
 * 0 on success, or negative errno on failure.
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
EXPORT_SYMBOL_GPL(devm_gpio_request);

/**
 * devm_gpio_request_one - request a single GPIO with initial setup
 * @dev: device to request for
 * @gpio: the GPIO number
 * @flags: GPIO configuration as specified by GPIOF_*
 * @label: a literal description string of this GPIO
 *
 * **DEPRECATED** This function is deprecated and must not be used in new code.
 *
 * Returns:
 * 0 on success, or negative errno on failure.
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
EXPORT_SYMBOL_GPL(devm_gpio_request_one);
