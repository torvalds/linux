/*
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __DAX_PRIVATE_H__
#define __DAX_PRIVATE_H__

#include <linux/device.h>
#include <linux/cdev.h>

/* private routines between core files */
struct dax_device;
struct dax_device *inode_dax(struct inode *inode);
struct inode *dax_inode(struct dax_device *dax_dev);

/* temporary until devm_create_dax_dev moves to bus.c */
extern const struct attribute_group *dax_attribute_groups[];
void unregister_dev_dax(void *dev);

/**
 * struct dax_region - mapping infrastructure for dax devices
 * @id: kernel-wide unique region for a memory range
 * @kref: to pin while other agents have a need to do lookups
 * @dev: parent device backing this region
 * @align: allocation and mapping alignment for child dax devices
 * @res: physical address range of the region
 * @pfn_flags: identify whether the pfns are paged back or not
 */
struct dax_region {
	int id;
	struct kref kref;
	struct device *dev;
	unsigned int align;
	struct resource res;
	unsigned long pfn_flags;
};

/**
 * struct dev_dax - instance data for a subdivision of a dax region
 * @region - parent region
 * @dax_dev - core dax functionality
 * @dev - device core
 */
struct dev_dax {
	struct dax_region *region;
	struct dax_device *dax_dev;
	struct device dev;
};

static inline struct dev_dax *to_dev_dax(struct device *dev)
{
	return container_of(dev, struct dev_dax, dev);
}
#endif
