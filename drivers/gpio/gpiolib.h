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

#ifdef CONFIG_ACPI
void acpi_gpiochip_add(struct gpio_chip *chip);
void acpi_gpiochip_remove(struct gpio_chip *chip);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }
#endif

#endif /* GPIOLIB_H */
