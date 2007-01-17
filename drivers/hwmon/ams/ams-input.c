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

static unsigned int joystick;
module_param(joystick, bool, 0644);
MODULE_PARM_DESC(joystick, "Enable the input class device on module load");

static unsigned int invert;
module_param(invert, bool, 0644);
MODULE_PARM_DESC(invert, "Invert input data on X and Y axis");

static int ams_input_kthread(void *data)
{
	s8 x, y, z;

	while (!kthread_should_stop()) {
		mutex_lock(&ams_info.lock);

		ams_sensors(&x, &y, &z);

		x -= ams_info.xcalib;
		y -= ams_info.ycalib;
		z -= ams_info.zcalib;

		input_report_abs(ams_info.idev, ABS_X, invert ? -x : x);
		input_report_abs(ams_info.idev, ABS_Y, invert ? -y : y);
		input_report_abs(ams_info.idev, ABS_Z, z);

		input_sync(ams_info.idev);

		mutex_unlock(&ams_info.lock);

		msleep(25);
	}

	return 0;
}

static int ams_input_open(struct input_dev *dev)
{
	ams_info.kthread = kthread_run(ams_input_kthread, NULL, "kams");
	return IS_ERR(ams_info.kthread) ? PTR_ERR(ams_info.kthread) : 0;
}

static void ams_input_close(struct input_dev *dev)
{
	kthread_stop(ams_info.kthread);
}

/* Call with ams_info.lock held! */
static void ams_input_enable(void)
{
	s8 x, y, z;

	if (ams_info.idev)
		return;

	ams_sensors(&x, &y, &z);
	ams_info.xcalib = x;
	ams_info.ycalib = y;
	ams_info.zcalib = z;

	ams_info.idev = input_allocate_device();
	if (!ams_info.idev)
		return;

	ams_info.idev->name = "Apple Motion Sensor";
	ams_info.idev->id.bustype = ams_info.bustype;
	ams_info.idev->id.vendor = 0;
	ams_info.idev->open = ams_input_open;
	ams_info.idev->close = ams_input_close;
	ams_info.idev->cdev.dev = &ams_info.of_dev->dev;

	input_set_abs_params(ams_info.idev, ABS_X, -50, 50, 3, 0);
	input_set_abs_params(ams_info.idev, ABS_Y, -50, 50, 3, 0);
	input_set_abs_params(ams_info.idev, ABS_Z, -50, 50, 3, 0);

	set_bit(EV_ABS, ams_info.idev->evbit);
	set_bit(EV_KEY, ams_info.idev->evbit);
	set_bit(BTN_TOUCH, ams_info.idev->keybit);

	if (input_register_device(ams_info.idev)) {
		input_free_device(ams_info.idev);
		ams_info.idev = NULL;
		return;
	}
}

/* Call with ams_info.lock held! */
static void ams_input_disable(void)
{
	if (ams_info.idev) {
		input_unregister_device(ams_info.idev);
		ams_info.idev = NULL;
	}
}

static ssize_t ams_input_show_joystick(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", joystick);
}

static ssize_t ams_input_store_joystick(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	if (sscanf(buf, "%d\n", &joystick) != 1)
		return -EINVAL;

	mutex_lock(&ams_info.lock);

	if (joystick)
		ams_input_enable();
	else
		ams_input_disable();

	mutex_unlock(&ams_info.lock);

	return count;
}

static DEVICE_ATTR(joystick, S_IRUGO | S_IWUSR,
	ams_input_show_joystick, ams_input_store_joystick);

/* Call with ams_info.lock held! */
int ams_input_init(void)
{
	int result;

	result = device_create_file(&ams_info.of_dev->dev, &dev_attr_joystick);

	if (!result && joystick)
		ams_input_enable();
	return result;
}

/* Call with ams_info.lock held! */
void ams_input_exit()
{
	ams_input_disable();
	device_remove_file(&ams_info.of_dev->dev, &dev_attr_joystick);
}
