/*
 *  lis3lv02d.c - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
 *  Copyright (C) 2008-2009 Pavel Machek
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
#include <linux/miscdevice.h>
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

struct lis3lv02d lis3_dev = {
	.misc_wait   = __WAIT_QUEUE_HEAD_INITIALIZER(lis3_dev.misc_wait),
};

EXPORT_SYMBOL_GPL(lis3_dev);

static s16 lis3lv02d_read_8(struct lis3lv02d *lis3, int reg)
{
	s8 lo;
	if (lis3->read(lis3, reg, &lo) < 0)
		return 0;

	return lo;
}

static s16 lis3lv02d_read_16(struct lis3lv02d *lis3, int reg)
{
	u8 lo, hi;

	lis3->read(lis3, reg - 1, &lo);
	lis3->read(lis3, reg, &hi);
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
 * @lis3: pointer to the device struct
 * @x:    where to store the X axis value
 * @y:    where to store the Y axis value
 * @z:    where to store the Z axis value
 *
 * Note that 40Hz input device can eat up about 10% CPU at 800MHZ
 */
static void lis3lv02d_get_xyz(struct lis3lv02d *lis3, int *x, int *y, int *z)
{
	int position[3];

	position[0] = lis3_dev.read_data(lis3, OUTX);
	position[1] = lis3_dev.read_data(lis3, OUTY);
	position[2] = lis3_dev.read_data(lis3, OUTZ);

	*x = lis3lv02d_get_axis(lis3_dev.ac.x, position);
	*y = lis3lv02d_get_axis(lis3_dev.ac.y, position);
	*z = lis3lv02d_get_axis(lis3_dev.ac.z, position);
}

void lis3lv02d_poweroff(struct lis3lv02d *lis3)
{
	lis3_dev.is_on = 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweroff);

void lis3lv02d_poweron(struct lis3lv02d *lis3)
{
	lis3_dev.is_on = 1;
	lis3_dev.init(lis3);
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweron);

/*
 * To be called before starting to use the device. It makes sure that the
 * device will always be on until a call to lis3lv02d_decrease_use(). Not to be
 * used from interrupt context.
 */
static void lis3lv02d_increase_use(struct lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage++;
	if (dev->usage == 1) {
		if (!dev->is_on)
			lis3lv02d_poweron(dev);
	}
	mutex_unlock(&dev->lock);
}

/*
 * To be called whenever a usage of the device is stopped.
 * It will make sure to turn off the device when there is not usage.
 */
static void lis3lv02d_decrease_use(struct lis3lv02d *dev)
{
	mutex_lock(&dev->lock);
	dev->usage--;
	if (dev->usage == 0)
		lis3lv02d_poweroff(dev);
	mutex_unlock(&dev->lock);
}

static irqreturn_t lis302dl_interrupt(int irq, void *dummy)
{
	/*
	 * Be careful: on some HP laptops the bios force DD when on battery and
	 * the lid is closed. This leads to interrupts as soon as a little move
	 * is done.
	 */
	atomic_inc(&lis3_dev.count);

	wake_up_interruptible(&lis3_dev.misc_wait);
	kill_fasync(&lis3_dev.async_queue, SIGIO, POLL_IN);
	return IRQ_HANDLED;
}

static int lis3lv02d_misc_open(struct inode *inode, struct file *file)
{
	int ret;

	if (test_and_set_bit(0, &lis3_dev.misc_opened))
		return -EBUSY; /* already open */

	atomic_set(&lis3_dev.count, 0);

	/*
	 * The sensor can generate interrupts for free-fall and direction
	 * detection (distinguishable with FF_WU_SRC and DD_SRC) but to keep
	 * the things simple and _fast_ we activate it only for free-fall, so
	 * no need to read register (very slow with ACPI). For the same reason,
	 * we forbid shared interrupts.
	 *
	 * IRQF_TRIGGER_RISING seems pointless on HP laptops because the
	 * io-apic is not configurable (and generates a warning) but I keep it
	 * in case of support for other hardware.
	 */
	ret = request_irq(lis3_dev.irq, lis302dl_interrupt, IRQF_TRIGGER_RISING,
			  DRIVER_NAME, &lis3_dev);

	if (ret) {
		clear_bit(0, &lis3_dev.misc_opened);
		printk(KERN_ERR DRIVER_NAME ": IRQ%d allocation failed\n", lis3_dev.irq);
		return -EBUSY;
	}
	lis3lv02d_increase_use(&lis3_dev);
	printk("lis3: registered interrupt %d\n", lis3_dev.irq);
	return 0;
}

static int lis3lv02d_misc_release(struct inode *inode, struct file *file)
{
	fasync_helper(-1, file, 0, &lis3_dev.async_queue);
	lis3lv02d_decrease_use(&lis3_dev);
	free_irq(lis3_dev.irq, &lis3_dev);
	clear_bit(0, &lis3_dev.misc_opened); /* release the device */
	return 0;
}

static ssize_t lis3lv02d_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	DECLARE_WAITQUEUE(wait, current);
	u32 data;
	unsigned char byte_data;
	ssize_t retval = 1;

	if (count < 1)
		return -EINVAL;

	add_wait_queue(&lis3_dev.misc_wait, &wait);
	while (true) {
		set_current_state(TASK_INTERRUPTIBLE);
		data = atomic_xchg(&lis3_dev.count, 0);
		if (data)
			break;

		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}

		schedule();
	}

	if (data < 255)
		byte_data = data;
	else
		byte_data = 255;

	/* make sure we are not going into copy_to_user() with
	 * TASK_INTERRUPTIBLE state */
	set_current_state(TASK_RUNNING);
	if (copy_to_user(buf, &byte_data, sizeof(byte_data)))
		retval = -EFAULT;

out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&lis3_dev.misc_wait, &wait);

	return retval;
}

static unsigned int lis3lv02d_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &lis3_dev.misc_wait, wait);
	if (atomic_read(&lis3_dev.count))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int lis3lv02d_misc_fasync(int fd, struct file *file, int on)
{
	return fasync_helper(fd, file, on, &lis3_dev.async_queue);
}

static const struct file_operations lis3lv02d_misc_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = lis3lv02d_misc_read,
	.open    = lis3lv02d_misc_open,
	.release = lis3lv02d_misc_release,
	.poll    = lis3lv02d_misc_poll,
	.fasync  = lis3lv02d_misc_fasync,
};

static struct miscdevice lis3lv02d_misc_device = {
	.minor   = MISC_DYNAMIC_MINOR,
	.name    = "freefall",
	.fops    = &lis3lv02d_misc_fops,
};

/**
 * lis3lv02d_joystick_kthread - Kthread polling function
 * @data: unused - here to conform to threadfn prototype
 */
static int lis3lv02d_joystick_kthread(void *data)
{
	int x, y, z;

	while (!kthread_should_stop()) {
		lis3lv02d_get_xyz(&lis3_dev, &x, &y, &z);
		input_report_abs(lis3_dev.idev, ABS_X, x - lis3_dev.xcalib);
		input_report_abs(lis3_dev.idev, ABS_Y, y - lis3_dev.ycalib);
		input_report_abs(lis3_dev.idev, ABS_Z, z - lis3_dev.zcalib);

		input_sync(lis3_dev.idev);

		try_to_freeze();
		msleep_interruptible(MDPS_POLL_INTERVAL);
	}

	return 0;
}

static int lis3lv02d_joystick_open(struct input_dev *input)
{
	lis3lv02d_increase_use(&lis3_dev);
	lis3_dev.kthread = kthread_run(lis3lv02d_joystick_kthread, NULL, "klis3lv02d");
	if (IS_ERR(lis3_dev.kthread)) {
		lis3lv02d_decrease_use(&lis3_dev);
		return PTR_ERR(lis3_dev.kthread);
	}

	return 0;
}

static void lis3lv02d_joystick_close(struct input_dev *input)
{
	kthread_stop(lis3_dev.kthread);
	lis3lv02d_decrease_use(&lis3_dev);
}

static inline void lis3lv02d_calibrate_joystick(void)
{
	lis3lv02d_get_xyz(&lis3_dev,
		&lis3_dev.xcalib, &lis3_dev.ycalib, &lis3_dev.zcalib);
}

int lis3lv02d_joystick_enable(void)
{
	int err;

	if (lis3_dev.idev)
		return -EINVAL;

	lis3_dev.idev = input_allocate_device();
	if (!lis3_dev.idev)
		return -ENOMEM;

	lis3lv02d_calibrate_joystick();

	lis3_dev.idev->name       = "ST LIS3LV02DL Accelerometer";
	lis3_dev.idev->phys       = DRIVER_NAME "/input0";
	lis3_dev.idev->id.bustype = BUS_HOST;
	lis3_dev.idev->id.vendor  = 0;
	lis3_dev.idev->dev.parent = &lis3_dev.pdev->dev;
	lis3_dev.idev->open       = lis3lv02d_joystick_open;
	lis3_dev.idev->close      = lis3lv02d_joystick_close;

	set_bit(EV_ABS, lis3_dev.idev->evbit);
	input_set_abs_params(lis3_dev.idev, ABS_X, -lis3_dev.mdps_max_val, lis3_dev.mdps_max_val, 3, 3);
	input_set_abs_params(lis3_dev.idev, ABS_Y, -lis3_dev.mdps_max_val, lis3_dev.mdps_max_val, 3, 3);
	input_set_abs_params(lis3_dev.idev, ABS_Z, -lis3_dev.mdps_max_val, lis3_dev.mdps_max_val, 3, 3);

	err = input_register_device(lis3_dev.idev);
	if (err) {
		input_free_device(lis3_dev.idev);
		lis3_dev.idev = NULL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_enable);

void lis3lv02d_joystick_disable(void)
{
	if (!lis3_dev.idev)
		return;

	misc_deregister(&lis3lv02d_misc_device);
	input_unregister_device(lis3_dev.idev);
	lis3_dev.idev = NULL;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_disable);

/* Sysfs stuff */
static ssize_t lis3lv02d_position_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int x, y, z;

	lis3lv02d_increase_use(&lis3_dev);
	lis3lv02d_get_xyz(&lis3_dev, &x, &y, &z);
	lis3lv02d_decrease_use(&lis3_dev);
	return sprintf(buf, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t lis3lv02d_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(%d,%d,%d)\n", lis3_dev.xcalib, lis3_dev.ycalib, lis3_dev.zcalib);
}

static ssize_t lis3lv02d_calibrate_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	lis3lv02d_increase_use(&lis3_dev);
	lis3lv02d_calibrate_joystick();
	lis3lv02d_decrease_use(&lis3_dev);
	return count;
}

/* conversion btw sampling rate and the register values */
static int lis3lv02dl_df_val[4] = {40, 160, 640, 2560};
static ssize_t lis3lv02d_rate_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u8 ctrl;
	int val;

	lis3lv02d_increase_use(&lis3_dev);
	lis3_dev.read(&lis3_dev, CTRL_REG1, &ctrl);
	lis3lv02d_decrease_use(&lis3_dev);
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


static int lis3lv02d_add_fs(struct lis3lv02d *lis3)
{
	lis3_dev.pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(lis3_dev.pdev))
		return PTR_ERR(lis3_dev.pdev);

	return sysfs_create_group(&lis3_dev.pdev->dev.kobj, &lis3lv02d_attribute_group);
}

int lis3lv02d_remove_fs(void)
{
	sysfs_remove_group(&lis3_dev.pdev->dev.kobj, &lis3lv02d_attribute_group);
	platform_device_unregister(lis3_dev.pdev);
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_remove_fs);

/*
 * Initialise the accelerometer and the various subsystems.
 * Should be rather independant of the bus system.
 */
int lis3lv02d_init_device(struct lis3lv02d *dev)
{
	dev->whoami = lis3lv02d_read_8(dev, WHO_AM_I);

	switch (dev->whoami) {
	case LIS_DOUBLE_ID:
		printk(KERN_INFO DRIVER_NAME ": 2-byte sensor found\n");
		dev->read_data = lis3lv02d_read_16;
		dev->mdps_max_val = 2048;
		break;
	case LIS_SINGLE_ID:
		printk(KERN_INFO DRIVER_NAME ": 1-byte sensor found\n");
		dev->read_data = lis3lv02d_read_8;
		dev->mdps_max_val = 128;
		break;
	default:
		printk(KERN_ERR DRIVER_NAME
			": unknown sensor type 0x%X\n", lis3_dev.whoami);
		return -EINVAL;
	}

	mutex_init(&dev->lock);
	lis3lv02d_add_fs(dev);
	lis3lv02d_increase_use(dev);

	if (lis3lv02d_joystick_enable())
		printk(KERN_ERR DRIVER_NAME ": joystick initialization failed\n");

	printk("lis3_init_device: irq %d\n", dev->irq);

	/* bail if we did not get an IRQ from the bus layer */
	if (!dev->irq) {
		printk(KERN_ERR DRIVER_NAME
			": No IRQ. Disabling /dev/freefall\n");
		goto out;
	}

	printk("lis3: registering device\n");
	if (misc_register(&lis3lv02d_misc_device))
		printk(KERN_ERR DRIVER_NAME ": misc_register failed\n");
out:
	lis3lv02d_decrease_use(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_init_device);

MODULE_DESCRIPTION("ST LIS3LV02Dx three-axis digital accelerometer driver");
MODULE_AUTHOR("Yan Burman, Eric Piel, Pavel Machek");
MODULE_LICENSE("GPL");

