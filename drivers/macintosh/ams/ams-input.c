// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple Motion Sensor driver (joystick emulation)
 *
 * Copyright (C) 2005 Stelian Pop (stelian@popies.net)
 * Copyright (C) 2006 Michael Hanselmann (linux-kernel@hansmi.ch)
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

static void ams_idev_poll(struct input_dev *idev)
{
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

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Apple Motion Sensor";
	input->id.bustype = ams_info.bustype;
	input->id.vendor = 0;
	input->dev.parent = &ams_info.of_dev->dev;

	input_set_abs_params(input, ABS_X, -50, 50, 3, 0);
	input_set_abs_params(input, ABS_Y, -50, 50, 3, 0);
	input_set_abs_params(input, ABS_Z, -50, 50, 3, 0);
	input_set_capability(input, EV_KEY, BTN_TOUCH);

	error = input_setup_polling(input, ams_idev_poll);
	if (error)
		goto err_free_input;

	input_set_poll_interval(input, 25);

	error = input_register_device(input);
	if (error)
		goto err_free_input;

	ams_info.idev = input;
	joystick = true;

	return 0;

err_free_input:
	input_free_device(input);
	return error;
}

static void ams_input_disable(void)
{
	if (ams_info.idev) {
		input_unregister_device(ams_info.idev);
		ams_info.idev = NULL;
	}

	joystick = false;
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
	int ret;

	ret = kstrtoul(buf, 0, &enable);
	if (ret)
		return ret;
	if (enable > 1)
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
