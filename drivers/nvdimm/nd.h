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
#ifndef __ND_H__
#define __ND_H__
#include <linux/libnvdimm.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/ndctl.h>

struct nvdimm_drvdata {
	struct device *dev;
	struct nd_cmd_get_config_size nsarea;
	void *data;
};

struct nd_region_namespaces {
	int count;
	int active;
};

struct nd_region {
	struct device dev;
	u16 ndr_mappings;
	u64 ndr_size;
	u64 ndr_start;
	int id;
	void *provider_data;
	struct nd_interleave_set *nd_set;
	struct nd_mapping mapping[0];
};

enum nd_async_mode {
	ND_SYNC,
	ND_ASYNC,
};

void nd_device_register(struct device *dev);
void nd_device_unregister(struct device *dev, enum nd_async_mode mode);
int __init nvdimm_init(void);
int __init nd_region_init(void);
void nvdimm_exit(void);
void nd_region_exit(void);
int nvdimm_init_nsarea(struct nvdimm_drvdata *ndd);
int nvdimm_init_config_data(struct nvdimm_drvdata *ndd);
struct nd_region *to_nd_region(struct device *dev);
int nd_region_to_nstype(struct nd_region *nd_region);
int nd_region_register_namespaces(struct nd_region *nd_region, int *err);
void nvdimm_bus_lock(struct device *dev);
void nvdimm_bus_unlock(struct device *dev);
bool is_nvdimm_bus_locked(struct device *dev);
#endif /* __ND_H__ */
