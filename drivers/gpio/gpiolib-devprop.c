// SPDX-License-Identifier: GPL-2.0
/*
 * Device property helpers for GPIO chips.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/property.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/export.h>

#include "gpiolib.h"

/**
 * devprop_gpiochip_set_names - Set GPIO line names using device properties
 * @chip: GPIO chip whose lines should be named, if possible
 *
 * Looks for device property "gpio-line-names" and if it exists assigns
 * GPIO line names for the chip. The memory allocated for the assigned
 * names belong to the underlying software node and should not be released
 * by the caller.
 */
int devprop_gpiochip_set_names(struct gpio_chip *chip)
{
	struct gpio_device *gdev = chip->gpiodev;
	struct device *dev = chip->parent;
	const char **names;
	int ret, i;
	int count;

	count = device_property_string_array_count(dev, "gpio-line-names");
	if (count < 0)
		return 0;

	if (count > gdev->ngpio) {
		dev_warn(&gdev->dev, "gpio-line-names is length %d but should be at most length %d",
			 count, gdev->ngpio);
		count = gdev->ngpio;
	}

	names = kcalloc(count, sizeof(*names), GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	ret = device_property_read_string_array(dev, "gpio-line-names",
						names, count);
	if (ret < 0) {
		dev_warn(&gdev->dev, "failed to read GPIO line names\n");
		kfree(names);
		return ret;
	}

	for (i = 0; i < count; i++)
		gdev->descs[i].name = names[i];

	kfree(names);

	return 0;
}
EXPORT_SYMBOL_GPL(devprop_gpiochip_set_names);
