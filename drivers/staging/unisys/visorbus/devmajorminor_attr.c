/* devmajorminor_attr.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* Implement publishing of device node attributes under:
 *
 *     /sys/bus/visorbus<x>/dev<y>/devmajorminor
 *
 */

#include "devmajorminor_attr.h"
#define CURRENT_FILE_PC VISOR_BUS_PC_devmajorminor_attr_c

#define to_devmajorminor_attr(_attr) \
	container_of(_attr, struct devmajorminor_attribute, attr)
#define to_visor_device_from_kobjdevmajorminor(obj) \
	container_of(obj, struct visor_device, kobjdevmajorminor)

struct devmajorminor_attribute {
	struct attribute attr;
	int slot;
	 ssize_t (*show)(struct visor_device *, int slot, char *buf);
	 ssize_t (*store)(struct visor_device *, int slot, const char *buf,
			  size_t count);
};

static ssize_t DEVMAJORMINOR_ATTR(struct visor_device *dev, int slot, char *buf)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);

	if (slot < 0 || slot >= maxdevnodes)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d:%d\n",
			dev->devnodes[slot].major, dev->devnodes[slot].minor);
}

static ssize_t
devmajorminor_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct devmajorminor_attribute *devmajorminor_attr =
	    to_devmajorminor_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjdevmajorminor(kobj);
	ssize_t ret = 0;

	if (devmajorminor_attr->show)
		ret = devmajorminor_attr->show(dev,
					       devmajorminor_attr->slot, buf);
	return ret;
}

static ssize_t
devmajorminor_attr_store(struct kobject *kobj,
			 struct attribute *attr, const char *buf, size_t count)
{
	struct devmajorminor_attribute *devmajorminor_attr =
	    to_devmajorminor_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjdevmajorminor(kobj);
	ssize_t ret = 0;

	if (devmajorminor_attr->store)
		ret = devmajorminor_attr->store(dev,
						devmajorminor_attr->slot,
						buf, count);
	return ret;
}

int
devmajorminor_create_file(struct visor_device *dev, const char *name,
			  int major, int minor)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);
	struct devmajorminor_attribute *myattr = NULL;
	int x = -1, rc = 0, slot = -1;

	register_devmajorminor_attributes(dev);
	for (slot = 0; slot < maxdevnodes; slot++)
		if (!dev->devnodes[slot].attr)
			break;
	if (slot == maxdevnodes) {
		rc = -ENOMEM;
		goto away;
	}
	myattr = kmalloc(sizeof(*myattr), GFP_KERNEL);
	if (!myattr) {
		rc = -ENOMEM;
		goto away;
	}
	memset(myattr, 0, sizeof(struct devmajorminor_attribute));
	myattr->show = DEVMAJORMINOR_ATTR;
	myattr->store = NULL;
	myattr->slot = slot;
	myattr->attr.name = name;
	myattr->attr.mode = S_IRUGO;
	dev->devnodes[slot].attr = myattr;
	dev->devnodes[slot].major = major;
	dev->devnodes[slot].minor = minor;
	x = sysfs_create_file(&dev->kobjdevmajorminor, &myattr->attr);
	if (x < 0) {
		rc = x;
		goto away;
	}
	kobject_uevent(&dev->device.kobj, KOBJ_ONLINE);
away:
	if (rc < 0) {
		kfree(myattr);
		myattr = NULL;
		dev->devnodes[slot].attr = NULL;
	}
	return rc;
}

void
devmajorminor_remove_file(struct visor_device *dev, int slot)
{
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);
	struct devmajorminor_attribute *myattr = NULL;

	if (slot < 0 || slot >= maxdevnodes)
		return;
	myattr = (struct devmajorminor_attribute *)(dev->devnodes[slot].attr);
	if (myattr)
		return;
	sysfs_remove_file(&dev->kobjdevmajorminor, &myattr->attr);
	kobject_uevent(&dev->device.kobj, KOBJ_OFFLINE);
	dev->devnodes[slot].attr = NULL;
	kfree(myattr);
}

void
devmajorminor_remove_all_files(struct visor_device *dev)
{
	int i = 0;
	int maxdevnodes = ARRAY_SIZE(dev->devnodes) / sizeof(dev->devnodes[0]);

	for (i = 0; i < maxdevnodes; i++)
		devmajorminor_remove_file(dev, i);
}

static const struct sysfs_ops devmajorminor_sysfs_ops = {
	.show = devmajorminor_attr_show,
	.store = devmajorminor_attr_store,
};

static struct kobj_type devmajorminor_kobj_type = {
	.sysfs_ops = &devmajorminor_sysfs_ops
};

int
register_devmajorminor_attributes(struct visor_device *dev)
{
	int rc = 0, x = 0;

	if (dev->kobjdevmajorminor.parent)
		goto away;	/* already registered */
	x = kobject_init_and_add(&dev->kobjdevmajorminor,
				 &devmajorminor_kobj_type, &dev->device.kobj,
				 "devmajorminor");
	if (x < 0) {
		rc = x;
		goto away;
	}

	kobject_uevent(&dev->kobjdevmajorminor, KOBJ_ADD);

away:
	return rc;
}

void
unregister_devmajorminor_attributes(struct visor_device *dev)
{
	if (!dev->kobjdevmajorminor.parent)
		return;		/* already unregistered */
	devmajorminor_remove_all_files(dev);

	kobject_del(&dev->kobjdevmajorminor);
	kobject_put(&dev->kobjdevmajorminor);
	dev->kobjdevmajorminor.parent = NULL;
}
