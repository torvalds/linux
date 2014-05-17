/*
 * Internal GPIO functions.
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GPIOLIB_H
#define GPIOLIB_H

#include <linux/err.h>
#include <linux/device.h>

enum of_gpio_flags;

/**
 * struct acpi_gpio_info - ACPI GPIO specific information
 * @gpioint: if %true this GPIO is of type GpioInt otherwise type is GpioIo
 * @active_low: in case of @gpioint, the pin is active low
 */
struct acpi_gpio_info {
	bool gpioint;
	bool active_low;
};

#ifdef CONFIG_ACPI
void acpi_gpiochip_add(struct gpio_chip *chip);
void acpi_gpiochip_remove(struct gpio_chip *chip);

struct gpio_desc *acpi_get_gpiod_by_index(struct device *dev, int index,
					  struct acpi_gpio_info *info);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }

static inline struct gpio_desc *
acpi_get_gpiod_by_index(struct device *dev, int index,
			struct acpi_gpio_info *info)
{
	return ERR_PTR(-ENOSYS);
}
#endif

int gpiochip_request_own_desc(struct gpio_desc *desc, const char *label);
void gpiochip_free_own_desc(struct gpio_desc *desc);

struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
		   const char *list_name, int index, enum of_gpio_flags *flags);

#endif /* GPIOLIB_H */
