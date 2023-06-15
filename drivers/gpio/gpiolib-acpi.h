/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ACPI helpers for GPIO API
 *
 * Copyright (C) 2012,2019 Intel Corporation
 */

#ifndef GPIOLIB_ACPI_H
#define GPIOLIB_ACPI_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gpio/consumer.h>

struct device;
struct fwnode_handle;

struct gpio_chip;
struct gpio_desc;
struct gpio_device;

#ifdef CONFIG_ACPI
void acpi_gpiochip_add(struct gpio_chip *chip);
void acpi_gpiochip_remove(struct gpio_chip *chip);

void acpi_gpiochip_request_interrupts(struct gpio_chip *chip);
void acpi_gpiochip_free_interrupts(struct gpio_chip *chip);

struct gpio_desc *acpi_find_gpio(struct fwnode_handle *fwnode,
				 const char *con_id,
				 unsigned int idx,
				 enum gpiod_flags *dflags,
				 unsigned long *lookupflags);

int acpi_gpio_count(struct device *dev, const char *con_id);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_request_interrupts(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_free_interrupts(struct gpio_chip *chip) { }

static inline struct gpio_desc *
acpi_find_gpio(struct fwnode_handle *fwnode, const char *con_id,
	       unsigned int idx, enum gpiod_flags *dflags,
	       unsigned long *lookupflags)
{
	return ERR_PTR(-ENOENT);
}
static inline int acpi_gpio_count(struct device *dev, const char *con_id)
{
	return -ENODEV;
}
#endif

#endif /* GPIOLIB_ACPI_H */
