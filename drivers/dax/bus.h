// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#ifndef __DAX_BUS_H__
#define __DAX_BUS_H__
#include <linux/device.h>

struct dev_dax;
struct resource;
struct dax_device;
struct dax_region;
void dax_region_put(struct dax_region *dax_region);
struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct resource *res, int target_node, unsigned int align,
		unsigned long long flags);

enum dev_dax_subsys {
	DEV_DAX_BUS,
	DEV_DAX_CLASS,
};

struct dev_dax *__devm_create_dev_dax(struct dax_region *dax_region, int id,
		struct dev_pagemap *pgmap, enum dev_dax_subsys subsys);

static inline struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region,
		int id, struct dev_pagemap *pgmap)
{
	return __devm_create_dev_dax(dax_region, id, pgmap, DEV_DAX_BUS);
}

/* to be deleted when DEV_DAX_CLASS is removed */
struct dev_dax *__dax_pmem_probe(struct device *dev, enum dev_dax_subsys subsys);

struct dax_device_driver {
	struct device_driver drv;
	struct list_head ids;
	int match_always;
};

int __dax_driver_register(struct dax_device_driver *dax_drv,
		struct module *module, const char *mod_name);
#define dax_driver_register(driver) \
	__dax_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
void dax_driver_unregister(struct dax_device_driver *dax_drv);
void kill_dev_dax(struct dev_dax *dev_dax);

#if IS_ENABLED(CONFIG_DEV_DAX_PMEM_COMPAT)
int dev_dax_probe(struct device *dev);
#endif

/*
 * While run_dax() is potentially a generic operation that could be
 * defined in include/linux/dax.h we don't want to grow any users
 * outside of drivers/dax/
 */
void run_dax(struct dax_device *dax_dev);

#define MODULE_ALIAS_DAX_DEVICE(type) \
	MODULE_ALIAS("dax:t" __stringify(type) "*")
#define DAX_DEVICE_MODALIAS_FMT "dax:t%d"

#endif /* __DAX_BUS_H__ */
