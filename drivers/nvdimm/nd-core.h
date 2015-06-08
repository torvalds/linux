/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
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
#ifndef __ND_CORE_H__
#define __ND_CORE_H__
#include <linux/libnvdimm.h>
#include <linux/device.h>

extern struct list_head nvdimm_bus_list;
extern struct mutex nvdimm_bus_list_mutex;
extern struct bus_type nvdimm_bus_type;
extern int nvdimm_major;

struct nvdimm_bus {
	struct nvdimm_bus_descriptor *nd_desc;
	struct list_head list;
	struct device dev;
	int id;
};

struct nvdimm {
	unsigned long flags;
	void *provider_data;
	unsigned long *dsm_mask;
	struct device dev;
	int id;
};

bool is_nvdimm(struct device *dev);
struct nvdimm_bus *walk_to_nvdimm_bus(struct device *nd_dev);
int __init nvdimm_bus_init(void);
void __exit nvdimm_bus_exit(void);
int nvdimm_bus_create_ndctl(struct nvdimm_bus *nvdimm_bus);
void nvdimm_bus_destroy_ndctl(struct nvdimm_bus *nvdimm_bus);
#endif /* __ND_CORE_H__ */
