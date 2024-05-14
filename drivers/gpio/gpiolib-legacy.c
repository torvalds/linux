// SPDX-License-Identifier: GPL-2.0
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

	if (flags & GPIOF_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);

	if (flags & GPIOF_DIR_IN)
		err = gpiod_direction_input(desc);
	else
		err = gpiod_direction_output_raw(desc,
				(flags & GPIOF_INIT_HIGH) ? 1 : 0);

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
