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
#include <linux/input/touchscreen.h>

/**
 * touchscreen_parse_of_params - parse common touchscreen DT properties
 * @dev: device that should be parsed
 *
 * This function parses common DT properties for touchscreens and setups the
 * input device accordingly. The function keeps previously setuped default
 * values if no value is specified via DT.
 */
void touchscreen_parse_of_params(struct input_dev *dev)
{
	struct device_node *np = dev->dev.parent->of_node;
	struct input_absinfo *absinfo;

	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	absinfo = &dev->absinfo[ABS_X];
	of_property_read_u32(np, "touchscreen-size-x", &absinfo->maximum);
	of_property_read_u32(np, "touchscreen-fuzz-x", &absinfo->fuzz);

	absinfo = &dev->absinfo[ABS_Y];
	of_property_read_u32(np, "touchscreen-size-y", &absinfo->maximum);
	of_property_read_u32(np, "touchscreen-fuzz-y", &absinfo->fuzz);

	absinfo = &dev->absinfo[ABS_PRESSURE];
	of_property_read_u32(np, "touchscreen-max-pressure", &absinfo->maximum);
	of_property_read_u32(np, "touchscreen-fuzz-pressure", &absinfo->fuzz);
}
EXPORT_SYMBOL(touchscreen_parse_of_params);
