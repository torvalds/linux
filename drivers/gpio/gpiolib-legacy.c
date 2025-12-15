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
	int err;

	err = gpio_request(gpio, label);
	if (err)
		return err;

	if (flags & GPIOF_IN)
		err = gpio_direction_input(gpio);
	else
		err = gpio_direction_output(gpio, !!(flags & GPIOF_OUT_INIT_HIGH));

	if (err)
		gpio_free(gpio);

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

static void devm_gpio_release(void *gpio)
{
	gpio_free((unsigned)(unsigned long)gpio);
}

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
	int rc;

	rc = gpio_request(gpio, label);
	if (rc)
		return rc;

	if (flags & GPIOF_IN)
		rc = gpio_direction_input(gpio);
	else
		rc = gpio_direction_output(gpio, !!(flags & GPIOF_OUT_INIT_HIGH));

	if (rc) {
		gpio_free(gpio);
		return rc;
	}

	return devm_add_action_or_reset(dev, devm_gpio_release, (void *)(unsigned long)gpio);
}
EXPORT_SYMBOL_GPL(devm_gpio_request_one);
