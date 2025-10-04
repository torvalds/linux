// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

#include "hfi.h"
#include "device.h"

static char *hfi1_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0600;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static const struct class class = {
	.name = "hfi1",
	.devnode = hfi1_devnode,
};

static char *hfi1_user_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static const struct class user_class = {
	.name = "hfi1_user",
	.devnode = hfi1_user_devnode,
};
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
		device = device_create(&user_class, NULL, dev, NULL, "%s", name);
	else
		device = device_create(&class, NULL, dev, NULL, "%s", name);

	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		pr_err("Could not create device for minor %d, %s (err %pe)\n",
		       minor, name, device);
		device = NULL;
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

int __init dev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&hfi1_dev, 0, HFI1_NMINORS, DRIVER_NAME);
	if (ret < 0) {
		pr_err("Could not allocate chrdev region (err %d)\n", -ret);
		goto done;
	}

	ret = class_register(&class);
	if (ret) {
		pr_err("Could not create device class (err %d)\n", -ret);
		unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
		goto done;
	}

	ret = class_register(&user_class);
	if (ret) {
		pr_err("Could not create device class for user accessible files (err %d)\n",
		       -ret);
		class_unregister(&class);
		unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
		goto done;
	}

done:
	return ret;
}

void dev_cleanup(void)
{
	class_unregister(&class);
	class_unregister(&user_class);

	unregister_chrdev_region(hfi1_dev, HFI1_NMINORS);
}
