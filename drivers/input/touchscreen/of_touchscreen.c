/*
 *  Generic DT helper functions for touchscreen devices
 *
 *  Copyright (c) 2014 Sebastian Reichel <sre@kernel.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/of.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>

static bool touchscreen_get_prop_u32(struct device_node *np,
				     const char *property,
				     unsigned int default_value,
				     unsigned int *value)
{
	u32 val;
	int error;

	error = of_property_read_u32(np, property, &val);
	if (error) {
		*value = default_value;
		return false;
	}

	*value = val;
	return true;
}

static void touchscreen_set_params(struct input_dev *dev,
				   unsigned long axis,
				   int max, int fuzz)
{
	struct input_absinfo *absinfo;

	if (!test_bit(axis, dev->absbit)) {
		/*
		 * Emit a warning only if the axis is not a multitouch
		 * axis, which might not be set by the driver.
		 */
		if (!input_is_mt_axis(axis))
			dev_warn(&dev->dev,
				 "DT specifies parameters but the axis is not set up\n");
		return;
	}

	absinfo = &dev->absinfo[axis];
	absinfo->maximum = max;
	absinfo->fuzz = fuzz;
}

/**
 * touchscreen_parse_of_params - parse common touchscreen DT properties
 * @dev: device that should be parsed
 *
 * This function parses common DT properties for touchscreens and setups the
 * input device accordingly. The function keeps previously setuped default
 * values if no value is specified via DT.
 */
void touchscreen_parse_of_params(struct input_dev *dev, bool multitouch)
{
	struct device_node *np = dev->dev.parent->of_node;
	unsigned int axis;
	unsigned int maximum, fuzz;
	bool data_present;

	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	axis = multitouch ? ABS_MT_POSITION_X : ABS_X;
	data_present = touchscreen_get_prop_u32(np, "touchscreen-size-x",
						input_abs_get_max(dev, axis),
						&maximum) |
		       touchscreen_get_prop_u32(np, "touchscreen-fuzz-x",
						input_abs_get_fuzz(dev, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(dev, axis, maximum, fuzz);

	axis = multitouch ? ABS_MT_POSITION_Y : ABS_Y;
	data_present = touchscreen_get_prop_u32(np, "touchscreen-size-y",
						input_abs_get_max(dev, axis),
						&maximum) |
		       touchscreen_get_prop_u32(np, "touchscreen-fuzz-y",
						input_abs_get_fuzz(dev, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(dev, axis, maximum, fuzz);

	axis = multitouch ? ABS_MT_PRESSURE : ABS_PRESSURE;
	data_present = touchscreen_get_prop_u32(np, "touchscreen-max-pressure",
						input_abs_get_max(dev, axis),
						&maximum) |
		       touchscreen_get_prop_u32(np, "touchscreen-fuzz-pressure",
						input_abs_get_fuzz(dev, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(dev, axis, maximum, fuzz);
}
EXPORT_SYMBOL(touchscreen_parse_of_params);
