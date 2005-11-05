/*
    hwmon.c - part of lm_sensors, Linux kernel modules for hardware monitoring

    This file defines the sysfs class "hwmon", for use by sensors drivers.

    Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/hwmon.h>

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

static struct class *hwmon_class;

static DEFINE_IDR(hwmon_idr);

/**
 * hwmon_device_register - register w/ hwmon sysfs class
 * @dev: the device to register
 *
 * hwmon_device_unregister() must be called when the class device is no
 * longer needed.
 *
 * Returns the pointer to the new struct class device.
 */
struct class_device *hwmon_device_register(struct device *dev)
{
	struct class_device *cdev;
	int id;

	if (idr_pre_get(&hwmon_idr, GFP_KERNEL) == 0)
		return ERR_PTR(-ENOMEM);

	if (idr_get_new(&hwmon_idr, NULL, &id) < 0)
		return ERR_PTR(-ENOMEM);

	id = id & MAX_ID_MASK;
	cdev = class_device_create(hwmon_class, NULL, MKDEV(0,0), dev,
					HWMON_ID_FORMAT, id);

	if (IS_ERR(cdev))
		idr_remove(&hwmon_idr, id);

	return cdev;
}

/**
 * hwmon_device_unregister - removes the previously registered class device
 *
 * @cdev: the class device to destroy
 */
void hwmon_device_unregister(struct class_device *cdev)
{
	int id;

	if (sscanf(cdev->class_id, HWMON_ID_FORMAT, &id) == 1) {
		class_device_unregister(cdev);
		idr_remove(&hwmon_idr, id);
	} else
		dev_dbg(cdev->dev,
			"hwmon_device_unregister() failed: bad class ID!\n");
}

static int __init hwmon_init(void)
{
	hwmon_class = class_create(THIS_MODULE, "hwmon");
	if (IS_ERR(hwmon_class)) {
		printk(KERN_ERR "hwmon.c: couldn't create sysfs class\n");
		return PTR_ERR(hwmon_class);
	}
	return 0;
}

static void __exit hwmon_exit(void)
{
	class_destroy(hwmon_class);
}

module_init(hwmon_init);
module_exit(hwmon_exit);

EXPORT_SYMBOL_GPL(hwmon_device_register);
EXPORT_SYMBOL_GPL(hwmon_device_unregister);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("hardware monitoring sysfs/class support");
MODULE_LICENSE("GPL");

