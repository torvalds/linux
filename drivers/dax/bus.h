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
struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region, int id);
void kill_dev_dax(struct dev_dax *dev_dax);
#endif /* __DAX_BUS_H__ */
