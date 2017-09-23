/*
 * LIRC base driver
 *
 * by Artur Lipowski <alipowski@interia.pl>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/idr.h>

#include "rc-core-priv.h"
#include <media/lirc.h>
#include <media/lirc_dev.h>

#define LOGHEAD		"lirc_dev (%s[%d]): "

static dev_t lirc_base_dev;

/* Used to keep track of allocated lirc devices */
#define LIRC_MAX_DEVICES 256
static DEFINE_IDA(lirc_ida);

/* Only used for sysfs but defined to void otherwise */
static struct class *lirc_class;

static void lirc_release_device(struct device *ld)
{
	struct lirc_dev *d = container_of(ld, struct lirc_dev, dev);
	struct rc_dev *rcdev = d->rdev;

	if (rcdev->driver_type == RC_DRIVER_IR_RAW)
		kfifo_free(&rcdev->rawir);

	kfree(d);
	module_put(THIS_MODULE);
	put_device(d->dev.parent);
}

struct lirc_dev *
lirc_allocate_device(void)
{
	struct lirc_dev *d;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (d) {
		mutex_init(&d->mutex);
		device_initialize(&d->dev);
		d->dev.class = lirc_class;
		d->dev.release = lirc_release_device;
		__module_get(THIS_MODULE);
	}

	return d;
}
EXPORT_SYMBOL(lirc_allocate_device);

void lirc_free_device(struct lirc_dev *d)
{
	if (!d)
		return;

	put_device(&d->dev);
}
EXPORT_SYMBOL(lirc_free_device);

int lirc_register_device(struct lirc_dev *d)
{
	struct rc_dev *rcdev = d->rdev;
	int minor;
	int err;

	if (!d) {
		pr_err("driver pointer must be not NULL!\n");
		return -EBADRQC;
	}

	if (!d->dev.parent) {
		pr_err("dev parent pointer not filled in!\n");
		return -EINVAL;
	}

	if (!d->fops) {
		pr_err("fops pointer not filled in!\n");
		return -EINVAL;
	}

	/* some safety check 8-) */
	d->name[sizeof(d->name) - 1] = '\0';

	if (rcdev->driver_type == RC_DRIVER_IR_RAW) {
		if (kfifo_alloc(&rcdev->rawir, MAX_IR_EVENT_SIZE, GFP_KERNEL))
			return -ENOMEM;
	}

	init_waitqueue_head(&rcdev->wait_poll);

	minor = ida_simple_get(&lirc_ida, 0, LIRC_MAX_DEVICES, GFP_KERNEL);
	if (minor < 0)
		return minor;

	d->minor = minor;
	d->dev.devt = MKDEV(MAJOR(lirc_base_dev), d->minor);
	dev_set_name(&d->dev, "lirc%d", d->minor);

	cdev_init(&d->cdev, d->fops);
	d->cdev.owner = d->owner;
	d->attached = true;

	err = cdev_device_add(&d->cdev, &d->dev);
	if (err) {
		ida_simple_remove(&lirc_ida, minor);
		return err;
	}

	get_device(d->dev.parent);

	dev_info(&d->dev, "lirc_dev: driver %s registered at minor = %d\n",
		 d->name, d->minor);

	return 0;
}
EXPORT_SYMBOL(lirc_register_device);

void lirc_unregister_device(struct lirc_dev *d)
{
	struct rc_dev *rcdev;

	if (!d)
		return;

	rcdev = d->rdev;

	dev_dbg(&d->dev, "lirc_dev: driver %s unregistered from minor = %d\n",
		d->name, d->minor);

	mutex_lock(&d->mutex);

	d->attached = false;
	if (d->open) {
		dev_dbg(&d->dev, LOGHEAD "releasing opened driver\n",
			d->name, d->minor);
		wake_up_poll(&rcdev->wait_poll, POLLHUP);
	}

	mutex_unlock(&d->mutex);

	cdev_device_del(&d->cdev, &d->dev);
	ida_simple_remove(&lirc_ida, d->minor);
	put_device(&d->dev);
}
EXPORT_SYMBOL(lirc_unregister_device);

int lirc_dev_fop_open(struct inode *inode, struct file *file)
{
	struct lirc_dev *d = container_of(inode->i_cdev, struct lirc_dev, cdev);
	struct rc_dev *rcdev = d->rdev;
	int retval;

	dev_dbg(&d->dev, LOGHEAD "open called\n", d->name, d->minor);

	retval = mutex_lock_interruptible(&d->mutex);
	if (retval)
		return retval;

	if (!d->attached) {
		retval = -ENODEV;
		goto out;
	}

	if (d->open) {
		retval = -EBUSY;
		goto out;
	}

	if (d->rdev) {
		retval = rc_open(d->rdev);
		if (retval)
			goto out;
	}

	if (rcdev->driver_type == RC_DRIVER_IR_RAW)
		kfifo_reset_out(&rcdev->rawir);

	d->open++;

	file->private_data = d->rdev;
	nonseekable_open(inode, file);
	mutex_unlock(&d->mutex);

	return 0;

out:
	mutex_unlock(&d->mutex);
	return retval;
}
EXPORT_SYMBOL(lirc_dev_fop_open);

int lirc_dev_fop_close(struct inode *inode, struct file *file)
{
	struct rc_dev *rcdev = file->private_data;
	struct lirc_dev *d = rcdev->lirc_dev;

	mutex_lock(&d->mutex);

	rc_close(rcdev);
	d->open--;

	mutex_unlock(&d->mutex);

	return 0;
}
EXPORT_SYMBOL(lirc_dev_fop_close);

int __init lirc_dev_init(void)
{
	int retval;

	lirc_class = class_create(THIS_MODULE, "lirc");
	if (IS_ERR(lirc_class)) {
		pr_err("class_create failed\n");
		return PTR_ERR(lirc_class);
	}

	retval = alloc_chrdev_region(&lirc_base_dev, 0, LIRC_MAX_DEVICES,
				     "BaseRemoteCtl");
	if (retval) {
		class_destroy(lirc_class);
		pr_err("alloc_chrdev_region failed\n");
		return retval;
	}

	pr_info("IR Remote Control driver registered, major %d\n",
						MAJOR(lirc_base_dev));

	return 0;
}

void __exit lirc_dev_exit(void)
{
	class_destroy(lirc_class);
	unregister_chrdev_region(lirc_base_dev, LIRC_MAX_DEVICES);
}
