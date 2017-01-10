/*
 * Device property helpers for GPIO chips.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/property.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include "gpiolib.h"

/**
 * devprop_gpiochip_set_names - Set GPIO line names using device properties
 * @chip: GPIO chip whose lines should be named, if possible
 *
 * Looks for device property "gpio-line-names" and if it exists assigns
 * GPIO line names for the chip. The memory allocated for the assigned
 * names belong to the underlying firmware node and should not be released
 * by the caller.
 */
void devprop_gpiochip_set_names(struct gpio_chip *chip)
{
	struct gpio_device *gdev = chip->gpiodev;
	const char **names;
	int ret, i;

	if (!chip->parent) {
		dev_warn(&gdev->dev, "GPIO chip parent is NULL\n");
		return;
	}

	ret = device_property_read_string_array(chip->parent, "gpio-line-names",
						NULL, 0);
	if (ret < 0)
		return;

	if (ret != gdev->ngpio) {
		dev_warn(chip->parent,
			 "names %d do not match number of GPIOs %d\n", ret,
			 gdev->ngpio);
		return;
	}

	names = kcalloc(gdev->ngpio, sizeof(*names), GFP_KERNEL);
	if (!names)
		return;

	ret = device_property_read_string_array(chip->parent, "gpio-line-names",
						names, gdev->ngpio);
	if (ret < 0) {
		dev_warn(chip->parent, "failed to read GPIO line names\n");
		kfree(names);
		return;
	}

	for (i = 0; i < gdev->ngpio; i++)
		gdev->descs[i].name = names[i];

	kfree(names);
}
