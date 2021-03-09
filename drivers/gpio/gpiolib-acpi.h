/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ACPI helpers for GPIO API
 *
 * Copyright (C) 2012,2019 Intel Corporation
 */

#ifndef GPIOLIB_ACPI_H
#define GPIOLIB_ACPI_H

struct acpi_device;

/**
 * struct acpi_gpio_info - ACPI GPIO specific information
 * @adev: reference to ACPI device which consumes GPIO resource
 * @flags: GPIO initialization flags
 * @gpioint: if %true this GPIO is of type GpioInt otherwise type is GpioIo
 * @pin_config: pin bias as provided by ACPI
 * @polarity: interrupt polarity as provided by ACPI
 * @triggering: triggering type as provided by ACPI
 * @debounce: debounce timeout as provided by ACPI
 * @quirks: Linux specific quirks as provided by struct acpi_gpio_mapping
 */
struct acpi_gpio_info {
	struct acpi_device *adev;
	enum gpiod_flags flags;
	bool gpioint;
	int pin_config;
	int polarity;
	int triggering;
	unsigned int debounce;
	unsigned int quirks;
};

#ifdef CONFIG_ACPI
void acpi_gpiochip_add(struct gpio_chip *chip);
void acpi_gpiochip_remove(struct gpio_chip *chip);

void acpi_gpio_dev_init(struct gpio_chip *gc, struct gpio_device *gdev);

void acpi_gpiochip_request_interrupts(struct gpio_chip *chip);
void acpi_gpiochip_free_interrupts(struct gpio_chip *chip);

int acpi_gpio_update_gpiod_flags(enum gpiod_flags *flags,
				 struct acpi_gpio_info *info);
int acpi_gpio_update_gpiod_lookup_flags(unsigned long *lookupflags,
					struct acpi_gpio_info *info);

struct gpio_desc *acpi_find_gpio(struct device *dev,
				 const char *con_id,
				 unsigned int idx,
				 enum gpiod_flags *dflags,
				 unsigned long *lookupflags);
struct gpio_desc *acpi_node_get_gpiod(struct fwnode_handle *fwnode,
				      const char *propname, int index,
				      struct acpi_gpio_info *info);

int acpi_gpio_count(struct device *dev, const char *con_id);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }

static inline void acpi_gpio_dev_init(struct gpio_chip *gc, struct gpio_device *gdev) { }

static inline void
acpi_gpiochip_request_interrupts(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_free_interrupts(struct gpio_chip *chip) { }

static inline int
acpi_gpio_update_gpiod_flags(enum gpiod_flags *flags, struct acpi_gpio_info *info)
{
	return 0;
}
static inline int
acpi_gpio_update_gpiod_lookup_flags(unsigned long *lookupflags,
				    struct acpi_gpio_info *info)
{
	return 0;
}

static inline struct gpio_desc *
acpi_find_gpio(struct device *dev, const char *con_id,
	       unsigned int idx, enum gpiod_flags *dflags,
	       unsigned long *lookupflags)
{
	return ERR_PTR(-ENOENT);
}
static inline struct gpio_desc *
acpi_node_get_gpiod(struct fwnode_handle *fwnode, const char *propname,
		    int index, struct acpi_gpio_info *info)
{
	return ERR_PTR(-ENXIO);
}
static inline int acpi_gpio_count(struct device *dev, const char *con_id)
{
	return -ENODEV;
}
#endif

#endif /* GPIOLIB_ACPI_H */
