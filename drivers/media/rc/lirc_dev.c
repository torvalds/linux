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
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/poll.h>

#include "rc-core-priv.h"
#include <media/lirc.h>

#define LOGHEAD		"lirc_dev (%s[%d]): "

static dev_t lirc_base_dev;

/* Used to keep track of allocated lirc devices */
static DEFINE_IDA(lirc_ida);

/* Only used for sysfs but defined to void otherwise */
static struct class *lirc_class;

static void lirc_release_device(struct device *ld)
{
	struct rc_dev *rcdev = container_of(ld, struct rc_dev, lirc_dev);

	if (rcdev->driver_type == RC_DRIVER_IR_RAW)
		kfifo_free(&rcdev->rawir);

	put_device(&rcdev->dev);
}

int ir_lirc_register(struct rc_dev *dev)
{
	int err, minor;

	device_initialize(&dev->lirc_dev);
	dev->lirc_dev.class = lirc_class;
	dev->lirc_dev.release = lirc_release_device;
	dev->send_mode = LIRC_MODE_PULSE;

	if (dev->driver_type == RC_DRIVER_IR_RAW) {
		if (kfifo_alloc(&dev->rawir, MAX_IR_EVENT_SIZE, GFP_KERNEL))
			return -ENOMEM;
	}

	init_waitqueue_head(&dev->wait_poll);

	minor = ida_simple_get(&lirc_ida, 0, RC_DEV_MAX, GFP_KERNEL);
	if (minor < 0) {
		err = minor;
		goto out_kfifo;
	}

	dev->lirc_dev.parent = &dev->dev;
	dev->lirc_dev.devt = MKDEV(MAJOR(lirc_base_dev), minor);
	dev_set_name(&dev->lirc_dev, "lirc%d", minor);

	cdev_init(&dev->lirc_cdev, &lirc_fops);

	err = cdev_device_add(&dev->lirc_cdev, &dev->lirc_dev);
	if (err)
		goto out_ida;

	get_device(&dev->dev);

	dev_info(&dev->dev, "lirc_dev: driver %s registered at minor = %d",
		 dev->driver_name, minor);

	return 0;

out_ida:
	ida_simple_remove(&lirc_ida, minor);
out_kfifo:
	if (dev->driver_type == RC_DRIVER_IR_RAW)
		kfifo_free(&dev->rawir);
	return err;
}

void ir_lirc_unregister(struct rc_dev *dev)
{
	dev_dbg(&dev->dev, "lirc_dev: driver %s unregistered from minor = %d\n",
		dev->driver_name, MINOR(dev->lirc_dev.devt));

	mutex_lock(&dev->lock);

	if (dev->lirc_open) {
		dev_dbg(&dev->dev, LOGHEAD "releasing opened driver\n",
			dev->driver_name, MINOR(dev->lirc_dev.devt));
		wake_up_poll(&dev->wait_poll, POLLHUP);
	}

	mutex_unlock(&dev->lock);

	cdev_device_del(&dev->lirc_cdev, &dev->lirc_dev);
	ida_simple_remove(&lirc_ida, MINOR(dev->lirc_dev.devt));
	put_device(&dev->lirc_dev);
}

int __init lirc_dev_init(void)
{
	int retval;

	lirc_class = class_create(THIS_MODULE, "lirc");
	if (IS_ERR(lirc_class)) {
		pr_err("class_create failed\n");
		return PTR_ERR(lirc_class);
	}

	retval = alloc_chrdev_region(&lirc_base_dev, 0, RC_DEV_MAX,
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
	unregister_chrdev_region(lirc_base_dev, RC_DEV_MAX);
}
