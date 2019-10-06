/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 */
#ifndef __DAX_PRIVATE_H__
#define __DAX_PRIVATE_H__

#include <linux/device.h>
#include <linux/cdev.h>

/* private routines between core files */
struct dax_device;
struct dax_device *inode_dax(struct inode *inode);
struct inode *dax_inode(struct dax_device *dax_dev);
int dax_bus_init(void);
void dax_bus_exit(void);

/**
 * struct dax_region - mapping infrastructure for dax devices
 * @id: kernel-wide unique region for a memory range
 * @target_node: effective numa node if this memory range is onlined
 * @kref: to pin while other agents have a need to do lookups
 * @dev: parent device backing this region
 * @align: allocation and mapping alignment for child dax devices
 * @res: physical address range of the region
 * @pfn_flags: identify whether the pfns are paged back or not
 */
struct dax_region {
	int id;
	int target_node;
	struct kref kref;
	struct device *dev;
	unsigned int align;
	struct resource res;
	unsigned long pfn_flags;
};

/**
 * struct dev_dax - instance data for a subdivision of a dax region, and
 * data while the device is activated in the driver.
 * @region - parent region
 * @dax_dev - core dax functionality
 * @target_node: effective numa node if dev_dax memory range is onlined
 * @dev - device core
 * @pgmap - pgmap for memmap setup / lifetime (driver owned)
 * @dax_mem_res: physical address range of hotadded DAX memory
 */
struct dev_dax {
	struct dax_region *region;
	struct dax_device *dax_dev;
	int target_node;
	struct device dev;
	struct dev_pagemap pgmap;
	struct resource *dax_kmem_res;
};

static inline struct dev_dax *to_dev_dax(struct device *dev)
{
	return container_of(dev, struct dev_dax, dev);
}
#endif
