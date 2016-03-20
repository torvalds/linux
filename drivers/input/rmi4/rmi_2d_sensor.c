/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/rmi.h>
#include "rmi_driver.h"
#include "rmi_2d_sensor.h"

#define RMI_2D_REL_POS_MIN		-128
#define RMI_2D_REL_POS_MAX		127

/* maximum ABS_MT_POSITION displacement (in mm) */
#define DMAX 10

void rmi_2d_sensor_abs_process(struct rmi_2d_sensor *sensor,
				struct rmi_2d_sensor_abs_object *obj,
				int slot)
{
	struct rmi_2d_axis_alignment *axis_align = &sensor->axis_align;

	/* we keep the previous values if the finger is released */
	if (obj->type == RMI_2D_OBJECT_NONE)
		return;

	if (axis_align->swap_axes)
		swap(obj->x, obj->y);

	if (axis_align->flip_x)
		obj->x = sensor->max_x - obj->x;

	if (axis_align->flip_y)
		obj->y = sensor->max_y - obj->y;

	/*
	 * Here checking if X offset or y offset are specified is
	 * redundant. We just add the offsets or clip the values.
	 *
	 * Note: offsets need to be applied before clipping occurs,
	 * or we could get funny values that are outside of
	 * clipping boundaries.
	 */
	obj->x += axis_align->offset_x;
	obj->y += axis_align->offset_y;

	obj->x =  max(axis_align->clip_x_low, obj->x);
	obj->y =  max(axis_align->clip_y_low, obj->y);

	if (axis_align->clip_x_high)
		obj->x = min(sensor->max_x, obj->x);

	if (axis_align->clip_y_high)
		obj->y =  min(sensor->max_y, obj->y);

	sensor->tracking_pos[slot].x = obj->x;
	sensor->tracking_pos[slot].y = obj->y;
}
EXPORT_SYMBOL_GPL(rmi_2d_sensor_abs_process);

void rmi_2d_sensor_abs_report(struct rmi_2d_sensor *sensor,
				struct rmi_2d_sensor_abs_object *obj,
				int slot)
{
	struct rmi_2d_axis_alignment *axis_align = &sensor->axis_align;
	struct input_dev *input = sensor->input;
	int wide, major, minor;

	if (sensor->kernel_tracking)
		input_mt_slot(input, sensor->tracking_slots[slot]);
	else
		input_mt_slot(input, slot);

	input_mt_report_slot_state(input, obj->mt_tool,
				   obj->type != RMI_2D_OBJECT_NONE);

	if (obj->type != RMI_2D_OBJECT_NONE) {
		obj->x = sensor->tracking_pos[slot].x;
		obj->y = sensor->tracking_pos[slot].y;

		if (axis_align->swap_axes)
			swap(obj->wx, obj->wy);

		wide = (obj->wx > obj->wy);
		major = max(obj->wx, obj->wy);
		minor = min(obj->wx, obj->wy);

		if (obj->type == RMI_2D_OBJECT_STYLUS) {
			major = max(1, major);
			minor = max(1, minor);
		}

		input_event(sensor->input, EV_ABS, ABS_MT_POSITION_X, obj->x);
		input_event(sensor->input, EV_ABS, ABS_MT_POSITION_Y, obj->y);
		input_event(sensor->input, EV_ABS, ABS_MT_ORIENTATION, wide);
		input_event(sensor->input, EV_ABS, ABS_MT_PRESSURE, obj->z);
		input_event(sensor->input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
		input_event(sensor->input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);

		rmi_dbg(RMI_DEBUG_2D_SENSOR, &sensor->input->dev,
			"%s: obj[%d]: type: 0x%02x X: %d Y: %d Z: %d WX: %d WY: %d\n",
			__func__, slot, obj->type, obj->x, obj->y, obj->z,
			obj->wx, obj->wy);
	}
}
EXPORT_SYMBOL_GPL(rmi_2d_sensor_abs_report);

void rmi_2d_sensor_rel_report(struct rmi_2d_sensor *sensor, int x, int y)
{
	struct rmi_2d_axis_alignment *axis_align = &sensor->axis_align;

	x = min(RMI_2D_REL_POS_MAX, max(RMI_2D_REL_POS_MIN, (int)x));
	y = min(RMI_2D_REL_POS_MAX, max(RMI_2D_REL_POS_MIN, (int)y));

	if (axis_align->swap_axes)
		swap(x, y);

	if (axis_align->flip_x)
		x = min(RMI_2D_REL_POS_MAX, -x);

	if (axis_align->flip_y)
		y = min(RMI_2D_REL_POS_MAX, -y);

	if (x || y) {
		input_report_rel(sensor->input, REL_X, x);
		input_report_rel(sensor->input, REL_Y, y);
	}
}
EXPORT_SYMBOL_GPL(rmi_2d_sensor_rel_report);

static void rmi_2d_sensor_set_input_params(struct rmi_2d_sensor *sensor)
{
	struct input_dev *input = sensor->input;
	int res_x;
	int res_y;
	int input_flags = 0;

	if (sensor->report_abs) {
		if (sensor->axis_align.swap_axes)
			swap(sensor->max_x, sensor->max_y);

		sensor->min_x = sensor->axis_align.clip_x_low;
		if (sensor->axis_align.clip_x_high)
			sensor->max_x = min(sensor->max_x,
				sensor->axis_align.clip_x_high);

		sensor->min_y = sensor->axis_align.clip_y_low;
		if (sensor->axis_align.clip_y_high)
			sensor->max_y = min(sensor->max_y,
				sensor->axis_align.clip_y_high);

		set_bit(EV_ABS, input->evbit);
		input_set_abs_params(input, ABS_MT_POSITION_X, 0, sensor->max_x,
					0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0, sensor->max_y,
					0, 0);

		if (sensor->x_mm && sensor->y_mm) {
			res_x = (sensor->max_x - sensor->min_x) / sensor->x_mm;
			res_y = (sensor->max_y - sensor->min_y) / sensor->y_mm;

			input_abs_set_res(input, ABS_X, res_x);
			input_abs_set_res(input, ABS_Y, res_y);

			input_abs_set_res(input, ABS_MT_POSITION_X, res_x);
			input_abs_set_res(input, ABS_MT_POSITION_Y, res_y);

			if (!sensor->dmax)
				sensor->dmax = DMAX * res_x;
		}

		input_set_abs_params(input, ABS_MT_PRESSURE, 0,	0xff, 0, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MAJOR,	0, 0x0f, 0, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MINOR,	0, 0x0f, 0, 0);
		input_set_abs_params(input, ABS_MT_ORIENTATION,	0, 1, 0, 0);

		if (sensor->sensor_type == rmi_sensor_touchpad)
			input_flags = INPUT_MT_POINTER;
		else
			input_flags = INPUT_MT_DIRECT;

		if (sensor->kernel_tracking)
			input_flags |= INPUT_MT_TRACK;

		input_mt_init_slots(input, sensor->nbr_fingers, input_flags);
	}

	if (sensor->report_rel) {
		set_bit(EV_REL, input->evbit);
		set_bit(REL_X, input->relbit);
		set_bit(REL_Y, input->relbit);
	}

	if (sensor->topbuttonpad)
		set_bit(INPUT_PROP_TOPBUTTONPAD, input->propbit);
}
EXPORT_SYMBOL_GPL(rmi_2d_sensor_set_input_params);

int rmi_2d_sensor_configure_input(struct rmi_function *fn,
					struct rmi_2d_sensor *sensor)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	struct rmi_driver_data *drv_data = dev_get_drvdata(&rmi_dev->dev);

	if (!drv_data->input)
		return -ENODEV;

	sensor->input = drv_data->input;
	rmi_2d_sensor_set_input_params(sensor);

	return 0;
}
EXPORT_SYMBOL_GPL(rmi_2d_sensor_configure_input);

#ifdef CONFIG_OF
int rmi_2d_sensor_of_probe(struct device *dev,
			struct rmi_2d_sensor_platform_data *pdata)
{
	int retval;
	u32 val;

	pdata->axis_align.swap_axes = of_property_read_bool(dev->of_node,
						"touchscreen-swapped-x-y");

	pdata->axis_align.flip_x = of_property_read_bool(dev->of_node,
						"touchscreen-inverted-x");

	pdata->axis_align.flip_y = of_property_read_bool(dev->of_node,
						"touchscreen-inverted-y");

	retval = rmi_of_property_read_u32(dev, &val, "syna,clip-x-low", 1);
	if (retval)
		return retval;

	pdata->axis_align.clip_x_low = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,clip-y-low",	1);
	if (retval)
		return retval;

	pdata->axis_align.clip_y_low = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,clip-x-high", 1);
	if (retval)
		return retval;

	pdata->axis_align.clip_x_high = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,clip-y-high", 1);
	if (retval)
		return retval;

	pdata->axis_align.clip_y_high = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,offset-x", 1);
	if (retval)
		return retval;

	pdata->axis_align.offset_x = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,offset-y", 1);
	if (retval)
		return retval;

	pdata->axis_align.offset_y = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,delta-x-threshold",
						1);
	if (retval)
		return retval;

	pdata->axis_align.delta_x_threshold = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,delta-y-threshold",
						1);
	if (retval)
		return retval;

	pdata->axis_align.delta_y_threshold = val;

	retval = rmi_of_property_read_u32(dev, (u32 *)&pdata->sensor_type,
			"syna,sensor-type", 1);
	if (retval)
		return retval;

	retval = rmi_of_property_read_u32(dev, &val, "touchscreen-x-mm", 1);
	if (retval)
		return retval;

	pdata->x_mm = val;

	retval = rmi_of_property_read_u32(dev, &val, "touchscreen-y-mm", 1);
	if (retval)
		return retval;

	pdata->y_mm = val;

	retval = rmi_of_property_read_u32(dev, &val,
				"syna,disable-report-mask", 1);
	if (retval)
		return retval;

	pdata->disable_report_mask = val;

	retval = rmi_of_property_read_u32(dev, &val, "syna,rezero-wait-ms",
						1);
	if (retval)
		return retval;

	pdata->rezero_wait = val;

	return 0;
}
#else
inline int rmi_2d_sensor_of_probe(struct device *dev,
			struct rmi_2d_sensor_platform_data *pdata)
{
	return -ENODEV;
}
#endif
EXPORT_SYMBOL_GPL(rmi_2d_sensor_of_probe);
