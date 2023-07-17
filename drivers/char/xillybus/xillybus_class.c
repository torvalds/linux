// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the Xillybus class
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "xillybus_class.h"

MODULE_DESCRIPTION("Driver for Xillybus class");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_ALIAS("xillybus_class");
MODULE_LICENSE("GPL v2");

static DEFINE_MUTEX(unit_mutex);
static LIST_HEAD(unit_list);
static struct class *xillybus_class;

#define UNITNAMELEN 16

struct xilly_unit {
	struct list_head list_entry;
	void *private_data;

	struct cdev *cdev;
	char name[UNITNAMELEN];
	int major;
	int lowest_minor;
	int num_nodes;
};

int xillybus_init_chrdev(struct device *dev,
			 const struct file_operations *fops,
			 struct module *owner,
			 void *private_data,
			 unsigned char *idt, unsigned int len,
			 int num_nodes,
			 const char *prefix, bool enumerate)
{
	int rc;
	dev_t mdev;
	int i;
	char devname[48];

	struct device *device;
	size_t namelen;
	struct xilly_unit *unit, *u;

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);

	if (!unit)
		return -ENOMEM;

	mutex_lock(&unit_mutex);

	if (!enumerate)
		snprintf(unit->name, UNITNAMELEN, "%s", prefix);

	for (i = 0; enumerate; i++) {
		snprintf(unit->name, UNITNAMELEN, "%s_%02d",
			 prefix, i);

		enumerate = false;
		list_for_each_entry(u, &unit_list, list_entry)
			if (!strcmp(unit->name, u->name)) {
				enumerate = true;
				break;
			}
	}

	rc = alloc_chrdev_region(&mdev, 0, num_nodes, unit->name);

	if (rc) {
		dev_warn(dev, "Failed to obtain major/minors");
		goto fail_obtain;
	}

	unit->major = MAJOR(mdev);
	unit->lowest_minor = MINOR(mdev);
	unit->num_nodes = num_nodes;
	unit->private_data = private_data;

	unit->cdev = cdev_alloc();
	if (!unit->cdev) {
		rc = -ENOMEM;
		goto unregister_chrdev;
	}
	unit->cdev->ops = fops;
	unit->cdev->owner = owner;

	rc = cdev_add(unit->cdev, MKDEV(unit->major, unit->lowest_minor),
		      unit->num_nodes);
	if (rc) {
		dev_err(dev, "Failed to add cdev.\n");
		/* kobject_put() is normally done by cdev_del() */
		kobject_put(&unit->cdev->kobj);
		goto unregister_chrdev;
	}

	for (i = 0; i < num_nodes; i++) {
		namelen = strnlen(idt, len);

		if (namelen == len) {
			dev_err(dev, "IDT's list of names is too short. This is exceptionally weird, because its CRC is OK\n");
			rc = -ENODEV;
			goto unroll_device_create;
		}

		snprintf(devname, sizeof(devname), "%s_%s",
			 unit->name, idt);

		len -= namelen + 1;
		idt += namelen + 1;

		device = device_create(xillybus_class,
				       NULL,
				       MKDEV(unit->major,
					     i + unit->lowest_minor),
				       NULL,
				       "%s", devname);

		if (IS_ERR(device)) {
			dev_err(dev, "Failed to create %s device. Aborting.\n",
				devname);
			rc = -ENODEV;
			goto unroll_device_create;
		}
	}

	if (len) {
		dev_err(dev, "IDT's list of names is too long. This is exceptionally weird, because its CRC is OK\n");
		rc = -ENODEV;
		goto unroll_device_create;
	}

	list_add_tail(&unit->list_entry, &unit_list);

	dev_info(dev, "Created %d device files.\n", num_nodes);

	mutex_unlock(&unit_mutex);

	return 0;

unroll_device_create:
	for (i--; i >= 0; i--)
		device_destroy(xillybus_class, MKDEV(unit->major,
						     i + unit->lowest_minor));

	cdev_del(unit->cdev);

unregister_chrdev:
	unregister_chrdev_region(MKDEV(unit->major, unit->lowest_minor),
				 unit->num_nodes);

fail_obtain:
	mutex_unlock(&unit_mutex);

	kfree(unit);

	return rc;
}
EXPORT_SYMBOL(xillybus_init_chrdev);

void xillybus_cleanup_chrdev(void *private_data,
			     struct device *dev)
{
	int minor;
	struct xilly_unit *unit = NULL, *iter;

	mutex_lock(&unit_mutex);

	list_for_each_entry(iter, &unit_list, list_entry)
		if (iter->private_data == private_data) {
			unit = iter;
			break;
		}

	if (!unit) {
		dev_err(dev, "Weird bug: Failed to find unit\n");
		mutex_unlock(&unit_mutex);
		return;
	}

	for (minor = unit->lowest_minor;
	     minor < (unit->lowest_minor + unit->num_nodes);
	     minor++)
		device_destroy(xillybus_class, MKDEV(unit->major, minor));

	cdev_del(unit->cdev);

	unregister_chrdev_region(MKDEV(unit->major, unit->lowest_minor),
				 unit->num_nodes);

	dev_info(dev, "Removed %d device files.\n",
		 unit->num_nodes);

	list_del(&unit->list_entry);
	kfree(unit);

	mutex_unlock(&unit_mutex);
}
EXPORT_SYMBOL(xillybus_cleanup_chrdev);

int xillybus_find_inode(struct inode *inode,
			void **private_data, int *index)
{
	int minor = iminor(inode);
	int major = imajor(inode);
	struct xilly_unit *unit = NULL, *iter;

	mutex_lock(&unit_mutex);

	list_for_each_entry(iter, &unit_list, list_entry)
		if (iter->major == major &&
		    minor >= iter->lowest_minor &&
		    minor < (iter->lowest_minor + iter->num_nodes)) {
			unit = iter;
			break;
		}

	if (!unit) {
		mutex_unlock(&unit_mutex);
		return -ENODEV;
	}

	*private_data = unit->private_data;
	*index = minor - unit->lowest_minor;

	mutex_unlock(&unit_mutex);
	return 0;
}
EXPORT_SYMBOL(xillybus_find_inode);

static int __init xillybus_class_init(void)
{
	xillybus_class = class_create("xillybus");

	if (IS_ERR(xillybus_class)) {
		pr_warn("Failed to register xillybus class\n");

		return PTR_ERR(xillybus_class);
	}
	return 0;
}

static void __exit xillybus_class_exit(void)
{
	class_destroy(xillybus_class);
}

module_init(xillybus_class_init);
module_exit(xillybus_class_exit);
