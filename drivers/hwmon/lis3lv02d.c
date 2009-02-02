/*
 *  lis3lv02d.c - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
 *  Copyright (C) 2008 Pavel Machek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>
#include <acpi/acpi_drivers.h>
#include <asm/atomic.h>
#include "lis3lv02d.h"

#define DRIVER_NAME     "lis3lv02d"

/* joystick device poll interval in milliseconds */
#define MDPS_POLL_INTERVAL 50
/*
 * The sensor can also generate interrupts (DRDY) but it's pretty pointless
 * because their are generated even if the data do not change. So it's better
 * to keep the interrupt for the free-fall event. The values are updated at
 * 40Hz (at the lowest frequency), but as it can be pretty time consuming on
 * some low processor, we poll the sensor only at 20Hz... enough for the
 * joystick.
 */

/* Maximum value our axis may get for the input device (signed 12 bits) */
#define MDPS_MAX_VAL 2048

struct acpi_lis3lv02d adev;
EXPORT_SYMBOL_GPL(adev);

static int lis3lv02d_add_fs(struct acpi_device *device);

static s16 lis3lv02d_read_16(acpi_handle handle, int reg)
{
	u8 lo, hi;

	adev.read(handle, reg, &lo);
	adev.read(handle, reg + 1, &hi);
	/* In "12 bit right justified" mode, bit 6, bit 7, bit 8 = bit 5 */
	return (s16)((hi << 8) | lo);
}

/**
 * lis3lv02d_get_axis - For the given axis, give the value converted
 * @axis:      1,2,3 - can also be negative
 * @hw_values: raw values returned by the hardware
 *
 * Returns the converted value.
 */
static inline int lis3lv02d_get_axis(s8 axis, int hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

/**
 * lis3lv02d_get_xyz - Get X, Y and Z axis values from the accelerometer
 * @handle: the handle to the device
 * @x:      where to store the X axis value
 * @y:      where to store the Y axis value
 * @z:      where to store the Z axis value
 *
 * Note that 40Hz input device can eat up about 10% CPU at 800MHZ
 */
static void lis3lv02d_get_xyz(acpi_handle handle, int *x, int *y, int *z)
{
	int position[3];

	position[0] = lis3lv02d_read_16(handle, OUTX_L);
	position[1] = lis3lv02d_read_16(handle, OUTY_L);
	position[2] = lis3lv02d_read_16(handle, OUTZ_L);

	*x = lis3lv02d_get_axis(adev.ac.x, position);
	*y = lis3lv02d_get_axis(adev.ac.y, position);
	*z = lis3lv02d_get_axis(adev.ac.z, position);
}

void lis3lv02d_poweroff(acpi_handle handle)
{
	adev.is_on = 0;
	/* disable X,Y,Z axis and power down */
	adev.write(handle, CTRL_REG1, 0x00);
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweroff);

void lis3lv02d_poweron(acpi_handle handle)
{
	u8 val;

	adev.is_on = 1;
	adev.init(handle);
	adev.write(handle, FF_WU_CFG, 0);
	/*
	 * BDU: LSB and MSB values are not updated until both have been read.
	 *      So the value read will always be correct.
	 * IEN: Interrupt for free-fall and DD, not for data-ready.
	 */
	adev.read(handle, CTRL_REG2, &val);
	val |= CTRL2_BDU | CTRL2_IEN;
	adev.write(handle, CTRL_REG2, val);
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweron);

/*
 * To be called before starting to use the device. It makes sure that the
 * device will always be on until a call to lis3lv02d_decrease_use(). Not to be
 * used from interrupt context.
 */
static void lis3lv02d_increase_use(struct acpi_lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage++;
	if (dev->usage == 1) {
		if (!dev->is_on)
			lis3lv02d_poweron(dev->device->handle);
	}
	mutex_unlock(&dev->lock);
}

/*
 * To be called whenever a usage of the device is stopped.
 * It will make sure to turn off the device when there is not usage.
 */
static void lis3lv02d_decrease_use(struct acpi_lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage--;
	if (dev->usage == 0)
		lis3lv02d_poweroff(dev->device->handle);
	mutex_unlock(&dev->lock);
}

/**
 * lis3lv02d_joystick_kthread - Kthread polling function
 * @data: unused - here to conform to threadfn prototype
 */
static int lis3lv02d_joystick_kthread(void *data)
{
	int x, y, z;

	while (!kthread_should_stop()) {
		lis3lv02d_get_xyz(adev.device->handle, &x, &y, &z);
		input_report_abs(adev.idev, ABS_X, x - adev.xcalib);
		input_report_abs(adev.idev, ABS_Y, y - adev.ycalib);
		input_report_abs(adev.idev, ABS_Z, z - adev.zcalib);

		input_sync(adev.idev);

		try_to_freeze();
		msleep_interruptible(MDPS_POLL_INTERVAL);
	}

	return 0;
}

static int lis3lv02d_joystick_open(struct input_dev *input)
{
	lis3lv02d_increase_use(&adev);
	adev.kthread = kthread_run(lis3lv02d_joystick_kthread, NULL, "klis3lv02d");
	if (IS_ERR(adev.kthread)) {
		lis3lv02d_decrease_use(&adev);
		return PTR_ERR(adev.kthread);
	}

	return 0;
}

static void lis3lv02d_joystick_close(struct input_dev *input)
{
	kthread_stop(adev.kthread);
	lis3lv02d_decrease_use(&adev);
}


static inline void lis3lv02d_calibrate_joystick(void)
{
	lis3lv02d_get_xyz(adev.device->handle, &adev.xcalib, &adev.ycalib, &adev.zcalib);
}

int lis3lv02d_joystick_enable(void)
{
	int err;

	if (adev.idev)
		return -EINVAL;

	adev.idev = input_allocate_device();
	if (!adev.idev)
		return -ENOMEM;

	lis3lv02d_calibrate_joystick();

	adev.idev->name       = "ST LIS3LV02DL Accelerometer";
	adev.idev->phys       = DRIVER_NAME "/input0";
	adev.idev->id.bustype = BUS_HOST;
	adev.idev->id.vendor  = 0;
	adev.idev->dev.parent = &adev.pdev->dev;
	adev.idev->open       = lis3lv02d_joystick_open;
	adev.idev->close      = lis3lv02d_joystick_close;

	set_bit(EV_ABS, adev.idev->evbit);
	input_set_abs_params(adev.idev, ABS_X, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);
	input_set_abs_params(adev.idev, ABS_Y, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);
	input_set_abs_params(adev.idev, ABS_Z, -MDPS_MAX_VAL, MDPS_MAX_VAL, 3, 3);

	err = input_register_device(adev.idev);
	if (err) {
		input_free_device(adev.idev);
		adev.idev = NULL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_enable);

void lis3lv02d_joystick_disable(void)
{
	if (!adev.idev)
		return;

	input_unregister_device(adev.idev);
	adev.idev = NULL;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_disable);

/*
 * Initialise the accelerometer and the various subsystems.
 * Should be rather independant of the bus system.
 */
int lis3lv02d_init_device(struct acpi_lis3lv02d *dev)
{
	mutex_init(&dev->lock);
	lis3lv02d_add_fs(dev->device);
	lis3lv02d_increase_use(dev);

	if (lis3lv02d_joystick_enable())
		printk(KERN_ERR DRIVER_NAME ": joystick initialization failed\n");

	lis3lv02d_decrease_use(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_init_device);

/* Sysfs stuff */
static ssize_t lis3lv02d_position_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int x, y, z;

	lis3lv02d_increase_use(&adev);
	lis3lv02d_get_xyz(adev.device->handle, &x, &y, &z);
	lis3lv02d_decrease_use(&adev);
	return sprintf(buf, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t lis3lv02d_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(%d,%d,%d)\n", adev.xcalib, adev.ycalib, adev.zcalib);
}

static ssize_t lis3lv02d_calibrate_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	lis3lv02d_increase_use(&adev);
	lis3lv02d_calibrate_joystick();
	lis3lv02d_decrease_use(&adev);
	return count;
}

/* conversion btw sampling rate and the register values */
static int lis3lv02dl_df_val[4] = {40, 160, 640, 2560};
static ssize_t lis3lv02d_rate_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u8 ctrl;
	int val;

	lis3lv02d_increase_use(&adev);
	adev.read(adev.device->handle, CTRL_REG1, &ctrl);
	lis3lv02d_decrease_use(&adev);
	val = (ctrl & (CTRL1_DF0 | CTRL1_DF1)) >> 4;
	return sprintf(buf, "%d\n", lis3lv02dl_df_val[val]);
}

static DEVICE_ATTR(position, S_IRUGO, lis3lv02d_position_show, NULL);
static DEVICE_ATTR(calibrate, S_IRUGO|S_IWUSR, lis3lv02d_calibrate_show,
	lis3lv02d_calibrate_store);
static DEVICE_ATTR(rate, S_IRUGO, lis3lv02d_rate_show, NULL);

static struct attribute *lis3lv02d_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_rate.attr,
	NULL
};

static struct attribute_group lis3lv02d_attribute_group = {
	.attrs = lis3lv02d_attributes
};


static int lis3lv02d_add_fs(struct acpi_device *device)
{
	adev.pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(adev.pdev))
		return PTR_ERR(adev.pdev);

	return sysfs_create_group(&adev.pdev->dev.kobj, &lis3lv02d_attribute_group);
}

int lis3lv02d_remove_fs(void)
{
	sysfs_remove_group(&adev.pdev->dev.kobj, &lis3lv02d_attribute_group);
	platform_device_unregister(adev.pdev);
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_remove_fs);

MODULE_DESCRIPTION("ST LIS3LV02Dx three-axis digital accelerometer driver");
MODULE_AUTHOR("Yan Burman and Eric Piel");
MODULE_LICENSE("GPL");

