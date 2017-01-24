/*
 * Mediated device interal definitions
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MDEV_PRIVATE_H
#define MDEV_PRIVATE_H

int  mdev_bus_register(void);
void mdev_bus_unregister(void);

struct mdev_parent {
	struct device *dev;
	const struct mdev_parent_ops *ops;
	struct kref ref;
	struct mutex lock;
	struct list_head next;
	struct kset *mdev_types_kset;
	struct list_head type_list;
};

struct mdev_device {
	struct device dev;
	struct mdev_parent *parent;
	uuid_le uuid;
	void *driver_data;
	struct kref ref;
	struct list_head next;
	struct kobject *type_kobj;
};

#define to_mdev_device(dev)	container_of(dev, struct mdev_device, dev)
#define dev_is_mdev(d)		((d)->bus == &mdev_bus_type)

struct mdev_type {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct mdev_parent *parent;
	struct list_head next;
	struct attribute_group *group;
};

#define to_mdev_type_attr(_attr)	\
	container_of(_attr, struct mdev_type_attribute, attr)
#define to_mdev_type(_kobj)		\
	container_of(_kobj, struct mdev_type, kobj)

int  parent_create_sysfs_files(struct mdev_parent *parent);
void parent_remove_sysfs_files(struct mdev_parent *parent);

int  mdev_create_sysfs_files(struct device *dev, struct mdev_type *type);
void mdev_remove_sysfs_files(struct device *dev, struct mdev_type *type);

int  mdev_device_create(struct kobject *kobj, struct device *dev, uuid_le uuid);
int  mdev_device_remove(struct device *dev, bool force_remove);

#endif /* MDEV_PRIVATE_H */
