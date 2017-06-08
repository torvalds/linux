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

#include <linux/property.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>

static bool touchscreen_get_prop_u32(struct device *dev,
				     const char *property,
				     unsigned int default_value,
				     unsigned int *value)
{
	u32 val;
	int error;

	error = device_property_read_u32(dev, property, &val);
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
		dev_warn(&dev->dev,
			 "DT specifies parameters but the axis %lu is not set up\n",
			 axis);
		return;
	}

	absinfo = &dev->absinfo[axis];
	absinfo->maximum = max;
	absinfo->fuzz = fuzz;
}

/**
 * touchscreen_parse_properties - parse common touchscreen DT properties
 * @input: input device that should be parsed
 * @multitouch: specifies whether parsed properties should be applied to
 *	single-touch or multi-touch axes
 * @prop: pointer to a struct touchscreen_properties into which to store
 *	axis swap and invert info for use with touchscreen_report_x_y();
 *	or %NULL
 *
 * This function parses common DT properties for touchscreens and setups the
 * input device accordingly. The function keeps previously set up default
 * values if no value is specified via DT.
 */
void touchscreen_parse_properties(struct input_dev *input, bool multitouch,
				  struct touchscreen_properties *prop)
{
	struct device *dev = input->dev.parent;
	unsigned int axis;
	unsigned int maximum, fuzz;
	bool data_present;

	input_alloc_absinfo(input);
	if (!input->absinfo)
		return;

	axis = multitouch ? ABS_MT_POSITION_X : ABS_X;
	data_present = touchscreen_get_prop_u32(dev, "touchscreen-size-x",
						input_abs_get_max(input,
								  axis) + 1,
						&maximum) |
		       touchscreen_get_prop_u32(dev, "touchscreen-fuzz-x",
						input_abs_get_fuzz(input, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(input, axis, maximum - 1, fuzz);

	axis = multitouch ? ABS_MT_POSITION_Y : ABS_Y;
	data_present = touchscreen_get_prop_u32(dev, "touchscreen-size-y",
						input_abs_get_max(input,
								  axis) + 1,
						&maximum) |
		       touchscreen_get_prop_u32(dev, "touchscreen-fuzz-y",
						input_abs_get_fuzz(input, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(input, axis, maximum - 1, fuzz);

	axis = multitouch ? ABS_MT_PRESSURE : ABS_PRESSURE;
	data_present = touchscreen_get_prop_u32(dev,
						"touchscreen-max-pressure",
						input_abs_get_max(input, axis),
						&maximum) |
		       touchscreen_get_prop_u32(dev,
						"touchscreen-fuzz-pressure",
						input_abs_get_fuzz(input, axis),
						&fuzz);
	if (data_present)
		touchscreen_set_params(input, axis, maximum, fuzz);

	if (!prop)
		return;

	axis = multitouch ? ABS_MT_POSITION_X : ABS_X;

	prop->max_x = input_abs_get_max(input, axis);
	prop->max_y = input_abs_get_max(input, axis + 1);
	prop->invert_x =
		device_property_read_bool(dev, "touchscreen-inverted-x");
	prop->invert_y =
		device_property_read_bool(dev, "touchscreen-inverted-y");
	prop->swap_x_y =
		device_property_read_bool(dev, "touchscreen-swapped-x-y");

	if (prop->swap_x_y)
		swap(input->absinfo[axis], input->absinfo[axis + 1]);
}
EXPORT_SYMBOL(touchscreen_parse_properties);

static void
touchscreen_apply_prop_to_x_y(const struct touchscreen_properties *prop,
			      unsigned int *x, unsigned int *y)
{
	if (prop->invert_x)
		*x = prop->max_x - *x;

	if (prop->invert_y)
		*y = prop->max_y - *y;

	if (prop->swap_x_y)
		swap(*x, *y);
}

/**
 * touchscreen_set_mt_pos - Set input_mt_pos coordinates
 * @pos: input_mt_pos to set coordinates of
 * @prop: pointer to a struct touchscreen_properties
 * @x: X coordinate to store in pos
 * @y: Y coordinate to store in pos
 *
 * Adjust the passed in x and y values applying any axis inversion and
 * swapping requested in the passed in touchscreen_properties and store
 * the result in a struct input_mt_pos.
 */
void touchscreen_set_mt_pos(struct input_mt_pos *pos,
			    const struct touchscreen_properties *prop,
			    unsigned int x, unsigned int y)
{
	touchscreen_apply_prop_to_x_y(prop, &x, &y);
	pos->x = x;
	pos->y = y;
}
EXPORT_SYMBOL(touchscreen_set_mt_pos);

/**
 * touchscreen_report_pos - Report touchscreen coordinates
 * @input: input_device to report coordinates for
 * @prop: pointer to a struct touchscreen_properties
 * @x: X coordinate to report
 * @y: Y coordinate to report
 * @multitouch: Report coordinates on single-touch or multi-touch axes
 *
 * Adjust the passed in x and y values applying any axis inversion and
 * swapping requested in the passed in touchscreen_properties and then
 * report the resulting coordinates on the input_dev's x and y axis.
 */
void touchscreen_report_pos(struct input_dev *input,
			    const struct touchscreen_properties *prop,
			    unsigned int x, unsigned int y,
			    bool multitouch)
{
	touchscreen_apply_prop_to_x_y(prop, &x, &y);
	input_report_abs(input, multitouch ? ABS_MT_POSITION_X : ABS_X, x);
	input_report_abs(input, multitouch ? ABS_MT_POSITION_Y : ABS_Y, y);
}
EXPORT_SYMBOL(touchscreen_report_pos);
