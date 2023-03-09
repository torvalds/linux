// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

#include "hfi.h"
#include "device.h"

static struct class *class;
static struct class *user_class;
static dev_t hfi1_dev;

int hfi1_cdev_init(int minor, const char *name,
		   const struct file_operations *fops,
		   struct cdev *cdev, struct device **devp,
		   bool user_accessible,
		   struct kobject *parent)
{
	const dev_t dev = MKDEV(MAJOR(hfi1_dev), minor);
	struct device *device = NULL;
	int ret;

	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;
	cdev_set_parent(cdev, parent);
	kobject_set_name(&cdev->kobj, name);

	ret = cdev_add(cdev, dev, 1);
	if (ret < 0) {
		pr_err("Could not add cdev for minor %d, %s (err %d)\n",
		       minor, name, -ret);
		goto done;
	}

	if (user_accessible)
		device = device_create(user_class, NULL, dev, NULL, "%s", name);
	else
		device = device_create(class, NULL, dev, NULL, "%s", name);

	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		device = NULL;
		pr_err("Could not create device for minor %d, %s (err %d)\n",
			minor, name, -ret);
		cdev_del(cdev);
	}
done:
	*devp = device;
	return ret;
}

void hfi1_cdev_cleanup(struct cdev *cdev, struct device **devp)
{
	struct device *device = *devp;

	if (device) {
		device_unregister(device);
		*devp = NULL;

		cdev_del(cdev);
	}
}

static const char *hfi1_class_name = "hfi1";

const char *class_name(void)
{
	return hfi1_class_name;
}

static char *hfi1_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0600;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static const char *hfi1_class_name_user = "hfi1_user";
static const char *class_name_user(void)
{
	return hfi1_class_name_user;
}

static char *hfi1_user_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

int __init dev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&hfi1_dev, 0, HFI1_NMINORS, DRIVER_NAME);
	if (ret < 0) {
		pr_err("Could not allocate chrdev region (err %d)\n", -ret);
		goto done;
	}

	class = class_create(THIS_MODULE, class_name());
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		pr_err("Could not create device class (err %d)\n", -ret);
		unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
		goto done;
	}
	class->devnode = hfi1_devnode;

	user_class = class_create(THIS_MODULE, class_name_user());
	if (IS_ERR(user_class)) {
		ret = PTR_ERR(user_class);
		pr_err("Could not create device class for user accessible files (err %d)\n",
		       -ret);
		class_destroy(class);
		class = NULL;
		user_class = NULL;
		unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
		goto done;
	}
	user_class->devnode = hfi1_user_devnode;

done:
	return ret;
}

void dev_cleanup(void)
{
	class_destroy(class);
	class = NULL;

	class_destroy(user_class);
	user_class = NULL;

	unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
}
