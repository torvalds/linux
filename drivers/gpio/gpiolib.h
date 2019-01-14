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

#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h> /* for enum gpiod_flags */
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cdev.h>

enum of_gpio_flags;
enum gpio_lookup_flags;
struct acpi_device;

/**
 * struct gpio_device - internal state container for GPIO devices
 * @id: numerical ID number for the GPIO chip
 * @dev: the GPIO device struct
 * @chrdev: character device for the GPIO device
 * @mockdev: class device used by the deprecated sysfs interface (may be
 * NULL)
 * @owner: helps prevent removal of modules exporting active GPIOs
 * @chip: pointer to the corresponding gpiochip, holding static
 * data for this device
 * @descs: array of ngpio descriptors.
 * @ngpio: the number of GPIO lines on this GPIO device, equal to the size
 * of the @descs array.
 * @base: GPIO base in the DEPRECATED global Linux GPIO numberspace, assigned
 * at device creation time.
 * @label: a descriptive name for the GPIO device, such as the part number
 * or name of the IP component in a System on Chip.
 * @data: per-instance data assigned by the driver
 * @list: links gpio_device:s together for traversal
 *
 * This state container holds most of the runtime variable data
 * for a GPIO device and can hold references and live on after the
 * GPIO chip has been removed, if it is still being used from
 * userspace.
 */
struct gpio_device {
	int			id;
	struct device		dev;
	struct cdev		chrdev;
	struct device		*mockdev;
	struct module		*owner;
	struct gpio_chip	*chip;
	struct gpio_desc	*descs;
	int			base;
	u16			ngpio;
	const char		*label;
	void			*data;
	struct list_head        list;

#ifdef CONFIG_PINCTRL
	/*
	 * If CONFIG_PINCTRL is enabled, then gpio controllers can optionally
	 * describe the actual pin range which they serve in an SoC. This
	 * information would be used by pinctrl subsystem to configure
	 * corresponding pins for gpio usage.
	 */
	struct list_head pin_ranges;
#endif
};

/**
 * struct acpi_gpio_info - ACPI GPIO specific information
 * @adev: reference to ACPI device which consumes GPIO resource
 * @flags: GPIO initialization flags
 * @gpioint: if %true this GPIO is of type GpioInt otherwise type is GpioIo
 * @polarity: interrupt polarity as provided by ACPI
 * @triggering: triggering type as provided by ACPI
 * @quirks: Linux specific quirks as provided by struct acpi_gpio_mapping
 */
struct acpi_gpio_info {
	struct acpi_device *adev;
	enum gpiod_flags flags;
	bool gpioint;
	int polarity;
	int triggering;
	unsigned int quirks;
};

/* gpio suffixes used for ACPI and device tree lookup */
static __maybe_unused const char * const gpio_suffixes[] = { "gpios", "gpio" };

#ifdef CONFIG_OF_GPIO
struct gpio_desc *of_find_gpio(struct device *dev,
			       const char *con_id,
			       unsigned int idx,
			       enum gpio_lookup_flags *flags);
struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
		   const char *list_name, int index, enum of_gpio_flags *flags);
int of_gpiochip_add(struct gpio_chip *gc);
void of_gpiochip_remove(struct gpio_chip *gc);
#else
static inline struct gpio_desc *of_find_gpio(struct device *dev,
					     const char *con_id,
					     unsigned int idx,
					     enum gpio_lookup_flags *flags)
{
	return ERR_PTR(-ENOENT);
}
static inline struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
		   const char *list_name, int index, enum of_gpio_flags *flags)
{
	return ERR_PTR(-ENOENT);
}
static inline int of_gpiochip_add(struct gpio_chip *gc) { return 0; }
static inline void of_gpiochip_remove(struct gpio_chip *gc) { }
#endif /* CONFIG_OF_GPIO */

#ifdef CONFIG_ACPI
void acpi_gpiochip_add(struct gpio_chip *chip);
void acpi_gpiochip_remove(struct gpio_chip *chip);

void acpi_gpiochip_request_interrupts(struct gpio_chip *chip);
void acpi_gpiochip_free_interrupts(struct gpio_chip *chip);

int acpi_gpio_update_gpiod_flags(enum gpiod_flags *flags,
				 struct acpi_gpio_info *info);

struct gpio_desc *acpi_find_gpio(struct device *dev,
				 const char *con_id,
				 unsigned int idx,
				 enum gpiod_flags *dflags,
				 enum gpio_lookup_flags *lookupflags);
struct gpio_desc *acpi_node_get_gpiod(struct fwnode_handle *fwnode,
				      const char *propname, int index,
				      struct acpi_gpio_info *info);

int acpi_gpio_count(struct device *dev, const char *con_id);

bool acpi_can_fallback_to_crs(struct acpi_device *adev, const char *con_id);
#else
static inline void acpi_gpiochip_add(struct gpio_chip *chip) { }
static inline void acpi_gpiochip_remove(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_request_interrupts(struct gpio_chip *chip) { }

static inline void
acpi_gpiochip_free_interrupts(struct gpio_chip *chip) { }

static inline int
acpi_gpio_update_gpiod_flags(enum gpiod_flags *flags, struct acpi_gpio_info *info)
{
	return 0;
}

static inline struct gpio_desc *
acpi_find_gpio(struct device *dev, const char *con_id,
	       unsigned int idx, enum gpiod_flags *dflags,
	       enum gpio_lookup_flags *lookupflags)
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

static inline bool acpi_can_fallback_to_crs(struct acpi_device *adev,
					    const char *con_id)
{
	return false;
}
#endif

struct gpio_desc *gpiochip_get_desc(struct gpio_chip *chip, u16 hwnum);
int gpiod_get_array_value_complex(bool raw, bool can_sleep,
				  unsigned int array_size,
				  struct gpio_desc **desc_array,
				  int *value_array);
int gpiod_set_array_value_complex(bool raw, bool can_sleep,
				   unsigned int array_size,
				   struct gpio_desc **desc_array,
				   int *value_array);

/* This is just passed between gpiolib and devres */
struct gpio_desc *gpiod_get_from_of_node(struct device_node *node,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label);

extern struct spinlock gpio_lock;
extern struct list_head gpio_devices;

struct gpio_desc {
	struct gpio_device	*gdev;
	unsigned long		flags;
/* flag symbols are bit numbers */
#define FLAG_REQUESTED	0
#define FLAG_IS_OUT	1
#define FLAG_EXPORT	2	/* protected by sysfs_lock */
#define FLAG_SYSFS	3	/* exported via /sys/class/gpio/control */
#define FLAG_ACTIVE_LOW	6	/* value has active low */
#define FLAG_OPEN_DRAIN	7	/* Gpio is open drain type */
#define FLAG_OPEN_SOURCE 8	/* Gpio is open source type */
#define FLAG_USED_AS_IRQ 9	/* GPIO is connected to an IRQ */
#define FLAG_IS_HOGGED	11	/* GPIO is hogged */
#define FLAG_TRANSITORY 12	/* GPIO may lose value in sleep or reset */

	/* Connection label */
	const char		*label;
	/* Name of the GPIO */
	const char		*name;
};

int gpiod_request(struct gpio_desc *desc, const char *label);
void gpiod_free(struct gpio_desc *desc);
int gpiod_configure_flags(struct gpio_desc *desc, const char *con_id,
		unsigned long lflags, enum gpiod_flags dflags);
int gpiod_hog(struct gpio_desc *desc, const char *name,
		unsigned long lflags, enum gpiod_flags dflags);

/*
 * Return the GPIO number of the passed descriptor relative to its chip
 */
static inline int gpio_chip_hwgpio(const struct gpio_desc *desc)
{
	return desc - &desc->gdev->descs[0];
}

void devprop_gpiochip_set_names(struct gpio_chip *chip,
				const struct fwnode_handle *fwnode);

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
	dev_emerg(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)
#define chip_crit(chip, fmt, ...)					\
	dev_crit(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)
#define chip_err(chip, fmt, ...)					\
	dev_err(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)
#define chip_warn(chip, fmt, ...)					\
	dev_warn(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)
#define chip_info(chip, fmt, ...)					\
	dev_info(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)
#define chip_dbg(chip, fmt, ...)					\
	dev_dbg(&chip->gpiodev->dev, "(%s): " fmt, chip->label, ##__VA_ARGS__)

#ifdef CONFIG_GPIO_SYSFS

int gpiochip_sysfs_register(struct gpio_device *gdev);
void gpiochip_sysfs_unregister(struct gpio_device *gdev);

#else

static inline int gpiochip_sysfs_register(struct gpio_device *gdev)
{
	return 0;
}

static inline void gpiochip_sysfs_unregister(struct gpio_device *gdev)
{
}

#endif /* CONFIG_GPIO_SYSFS */

#endif /* GPIOLIB_H */
