/*
 * IOMMU sysfs class support
 *
 * Copyright (C) 2014 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/slab.h>

/*
 * We provide a common class "devices" group which initially has no attributes.
 * As devices are added to the IOMMU, we'll add links to the group.
 */
static struct attribute *devices_attr[] = {
	NULL,
};

static const struct attribute_group iommu_devices_attr_group = {
	.name = "devices",
	.attrs = devices_attr,
};

static const struct attribute_group *iommu_dev_groups[] = {
	&iommu_devices_attr_group,
	NULL,
};

static void iommu_release_device(struct device *dev)
{
	kfree(dev);
}

static struct class iommu_class = {
	.name = "iommu",
	.dev_release = iommu_release_device,
	.dev_groups = iommu_dev_groups,
};

static int __init iommu_dev_init(void)
{
	return class_register(&iommu_class);
}
postcore_initcall(iommu_dev_init);

/*
 * Init the struct device for the IOMMU. IOMMU specific attributes can
 * be provided as an attribute group, allowing a unique namespace per
 * IOMMU type.
 */
int iommu_device_sysfs_add(struct iommu_device *iommu,
			   struct device *parent,
			   const struct attribute_group **groups,
			   const char *fmt, ...)
{
	va_list vargs;
	int ret;

	device_initialize(&iommu->dev);

	iommu->dev.class = &iommu_class;
	iommu->dev.parent = parent;
	iommu->dev.groups = groups;

	va_start(vargs, fmt);
	ret = kobject_set_name_vargs(&iommu->dev.kobj, fmt, vargs);
	va_end(vargs);
	if (ret)
		goto error;

	ret = device_add(&iommu->dev);
	if (ret)
		goto error;

	return 0;

error:
	put_device(&iommu->dev);
	return ret;
}

void iommu_device_sysfs_remove(struct iommu_device *iommu)
{
	device_unregister(&iommu->dev);
}
/*
 * IOMMU drivers can indicate a device is managed by a given IOMMU using
 * this interface.  A link to the device will be created in the "devices"
 * directory of the IOMMU device in sysfs and an "iommu" link will be
 * created under the linked device, pointing back at the IOMMU device.
 */
int iommu_device_link(struct device *dev, struct device *link)
{
	int ret;

	if (!dev || IS_ERR(dev))
		return -ENODEV;

	ret = sysfs_add_link_to_group(&dev->kobj, "devices",
				      &link->kobj, dev_name(link));
	if (ret)
		return ret;

	ret = sysfs_create_link_nowarn(&link->kobj, &dev->kobj, "iommu");
	if (ret)
		sysfs_remove_link_from_group(&dev->kobj, "devices",
					     dev_name(link));

	return ret;
}

void iommu_device_unlink(struct device *dev, struct device *link)
{
	if (!dev || IS_ERR(dev))
		return;

	sysfs_remove_link(&link->kobj, "iommu");
	sysfs_remove_link_from_group(&dev->kobj, "devices", dev_name(link));
}
