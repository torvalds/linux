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

void acpi_gpiochip_request_interrupts(struct gpio_chip *chip);
void acpi_gpiochip_free_interrupts(struct gpio_chip *chip);

struct gpio_desc *acpi_get_gpiod_by_index(struct acpi_device *adev,
					  const char *propname, int index,
					  struct acpi_gpio_info *info);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_request_interrupts(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_free_interrupts(struct gpio_chip *chip) { }

static inline struct gpio_desc *
acpi_get_gpiod_by_index(struct acpi_device *adev, const char *propname,
			int index, struct acpi_gpio_info *info)
{
	return ERR_PTR(-ENOSYS);
}
#endif

struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
		   const char *list_name, int index, enum of_gpio_flags *flags);

struct gpio_desc *gpiochip_get_desc(struct gpio_chip *chip, u16 hwnum);

extern struct spinlock gpio_lock;
extern struct list_head gpio_chips;

struct gpio_desc {
	struct gpio_chip	*chip;
	unsigned long		flags;
/* flag symbols are bit numbers */
#define FLAG_REQUESTED	0
#define FLAG_IS_OUT	1
#define FLAG_EXPORT	2	/* protected by sysfs_lock */
#define FLAG_SYSFS	3	/* exported via /sys/class/gpio/control */
#define FLAG_TRIG_FALL	4	/* trigger on falling edge */
#define FLAG_TRIG_RISE	5	/* trigger on rising edge */
#define FLAG_ACTIVE_LOW	6	/* value has active low */
#define FLAG_OPEN_DRAIN	7	/* Gpio is open drain type */
#define FLAG_OPEN_SOURCE 8	/* Gpio is open source type */
#define FLAG_USED_AS_IRQ 9	/* GPIO is connected to an IRQ */
#define FLAG_SYSFS_DIR	10	/* show sysfs direction attribute */

#define ID_SHIFT	16	/* add new flags before this one */

#define GPIO_FLAGS_MASK		((1 << ID_SHIFT) - 1)
#define GPIO_TRIGGER_MASK	(BIT(FLAG_TRIG_FALL) | BIT(FLAG_TRIG_RISE))

	const char		*label;
};

int gpiod_request(struct gpio_desc *desc, const char *label);
void gpiod_free(struct gpio_desc *desc);

/*
 * Return the GPIO number of the passed descriptor relative to its chip
 */
static int __maybe_unused gpio_chip_hwgpio(const struct gpio_desc *desc)
{
	return desc - &desc->chip->desc[0];
}

/* With descriptor prefix */

#define gpiod_emerg(desc, fmt, ...)					       \
	pr_emerg("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",\
		 ##__VA_ARGS__)
#define gpiod_crit(desc, fmt, ...)					       \
	pr_crit("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
		 ##__VA_ARGS__)
#define gpiod_err(desc, fmt, ...)					       \
	pr_err("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",  \
		 ##__VA_ARGS__)
#define gpiod_warn(desc, fmt, ...)					       \
	pr_warn("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
		 ##__VA_ARGS__)
#define gpiod_info(desc, fmt, ...)					       \
	pr_info("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
		 ##__VA_ARGS__)
#define gpiod_dbg(desc, fmt, ...)					       \
	pr_debug("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",\
		 ##__VA_ARGS__)

/* With chip prefix */

#define chip_emerg(chip, fmt, ...)					\
	pr_emerg("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_crit(chip, fmt, ...)					\
	pr_crit("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_err(chip, fmt, ...)					\
	pr_err("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_warn(chip, fmt, ...)					\
	pr_warn("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_info(chip, fmt, ...)					\
	pr_info("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_dbg(chip, fmt, ...)					\
	pr_debug("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)

#ifdef CONFIG_GPIO_SYSFS

int gpiochip_export(struct gpio_chip *chip);
void gpiochip_unexport(struct gpio_chip *chip);

#else

static inline int gpiochip_export(struct gpio_chip *chip)
{
	return 0;
}

static inline void gpiochip_unexport(struct gpio_chip *chip)
{
}

#endif /* CONFIG_GPIO_SYSFS */

#endif /* GPIOLIB_H */
