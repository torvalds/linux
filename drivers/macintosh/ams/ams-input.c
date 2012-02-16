/*
 * Apple Motion Sensor driver (joystick emulation)
 *
 * Copyright (C) 2005 Stelian Pop (stelian@popies.net)
 * Copyright (C) 2006 Michael Hanselmann (linux-kernel@hansmi.ch)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "ams.h"

static bool joystick;
module_param(joystick, bool, S_IRUGO);
MODULE_PARM_DESC(joystick, "Enable the input class device on module load");

static bool invert;
module_param(invert, bool, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(invert, "Invert input data on X and Y axis");

static DEFINE_MUTEX(ams_input_mutex);

static void ams_idev_poll(struct input_polled_dev *dev)
{
	struct input_dev *idev = dev->input;
	s8 x, y, z;

	mutex_lock(&ams_info.lock);

	ams_sensors(&x, &y, &z);

	x -= ams_info.xcalib;
	y -= ams_info.ycalib;
	z -= ams_info.zcalib;

	input_report_abs(idev, ABS_X, invert ? -x : x);
	input_report_abs(idev, ABS_Y, invert ? -y : y);
	input_report_abs(idev, ABS_Z, z);

	input_sync(idev);

	mutex_unlock(&ams_info.lock);
}

/* Call with ams_info.lock held! */
static int ams_input_enable(void)
{
	struct input_dev *input;
	s8 x, y, z;
	int error;

	ams_sensors(&x, &y, &z);
	ams_info.xcalib = x;
	ams_info.ycalib = y;
	ams_info.zcalib = z;

	ams_info.idev = input_allocate_polled_device();
	if (!ams_info.idev)
		return -ENOMEM;

	ams_info.idev->poll = ams_idev_poll;
	ams_info.idev->poll_interval = 25;

	input = ams_info.idev->input;
	input->name = "Apple Motion Sensor";
	input->id.bustype = ams_info.bustype;
	input->id.vendor = 0;
	input->dev.parent = &ams_info.of_dev->dev;

	input_set_abs_params(input, ABS_X, -50, 50, 3, 0);
	input_set_abs_params(input, ABS_Y, -50, 50, 3, 0);
	input_set_abs_params(input, ABS_Z, -50, 50, 3, 0);

	set_bit(EV_ABS, input->evbit);
	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_TOUCH, input->keybit);

	error = input_register_polled_device(ams_info.idev);
	if (error) {
		input_free_polled_device(ams_info.idev);
		ams_info.idev = NULL;
		return error;
	}

	joystick = 1;

	return 0;
}

static void ams_input_disable(void)
{
	if (ams_info.idev) {
		input_unregister_polled_device(ams_info.idev);
		input_free_polled_device(ams_info.idev);
		ams_info.idev = NULL;
	}

	joystick = 0;
}

static ssize_t ams_input_show_joystick(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", joystick);
}

static ssize_t ams_input_store_joystick(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long enable;
	int error = 0;

	if (strict_strtoul(buf, 0, &enable) || enable > 1)
		return -EINVAL;

	mutex_lock(&ams_input_mutex);

	if (enable != joystick) {
		if (enable)
			error = ams_input_enable();
		else
			ams_input_disable();
	}

	mutex_unlock(&ams_input_mutex);

	return error ? error : count;
}

static DEVICE_ATTR(joystick, S_IRUGO | S_IWUSR,
	ams_input_show_joystick, ams_input_store_joystick);

int ams_input_init(void)
{
	if (joystick)
		ams_input_enable();

	return device_create_file(&ams_info.of_dev->dev, &dev_attr_joystick);
}

void ams_input_exit(void)
{
	device_remove_file(&ams_info.of_dev->dev, &dev_attr_joystick);

	mutex_lock(&ams_input_mutex);
	ams_input_disable();
	mutex_unlock(&ams_input_mutex);
}
