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
#ifndef __DEVICE_DAX_H__
#define __DEVICE_DAX_H__
struct device;
struct dev_dax;
struct resource;
struct dax_region;
void dax_region_put(struct dax_region *dax_region);
struct dax_region *alloc_dax_region(struct device *parent,
		int region_id, struct resource *res, unsigned int align,
		void *addr, unsigned long flags);
struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region,
		int id, struct resource *res, int count);
#endif /* __DEVICE_DAX_H__ */
