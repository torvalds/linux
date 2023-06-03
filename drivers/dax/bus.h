// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#ifndef __DAX_BUS_H__
#define __DAX_BUS_H__
#include <linux/device.h>
#include <linux/range.h>

struct dev_dax;
struct resource;
struct dax_device;
struct dax_region;

/* dax bus specific ioresource flags */
#define IORESOURCE_DAX_STATIC BIT(0)
#define IORESOURCE_DAX_KMEM BIT(1)

struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct range *range, int target_node, unsigned int align,
		unsigned long flags);

struct dev_dax_data {
	struct dax_region *dax_region;
	struct dev_pagemap *pgmap;
	resource_size_t size;
	int id;
};

struct dev_dax *devm_create_dev_dax(struct dev_dax_data *data);

enum dax_driver_type {
	DAXDRV_KMEM_TYPE,
	DAXDRV_DEVICE_TYPE,
};

struct dax_device_driver {
	struct device_driver drv;
	struct list_head ids;
	enum dax_driver_type type;
	int (*probe)(struct dev_dax *dev);
	void (*remove)(struct dev_dax *dev);
};

int __dax_driver_register(struct dax_device_driver *dax_drv,
		struct module *module, const char *mod_name);
#define dax_driver_register(driver) \
	__dax_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
void dax_driver_unregister(struct dax_device_driver *dax_drv);
void kill_dev_dax(struct dev_dax *dev_dax);
bool static_dev_dax(struct dev_dax *dev_dax);

#define MODULE_ALIAS_DAX_DEVICE(type) \
	MODULE_ALIAS("dax:t" __stringify(type) "*")
#define DAX_DEVICE_MODALIAS_FMT "dax:t%d"

#endif /* __DAX_BUS_H__ */
