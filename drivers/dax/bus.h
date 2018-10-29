// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved. */
#ifndef __DAX_BUS_H__
#define __DAX_BUS_H__
struct device;
struct dev_dax;
struct resource;
struct dax_device;
struct dax_region;
void dax_region_put(struct dax_region *dax_region);
struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct resource *res, unsigned int align, unsigned long flags);
struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region, int id,
		struct dev_pagemap *pgmap);
int __dax_driver_register(struct device_driver *drv,
		struct module *module, const char *mod_name);
#define dax_driver_register(driver) \
	__dax_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
void kill_dev_dax(struct dev_dax *dev_dax);

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
