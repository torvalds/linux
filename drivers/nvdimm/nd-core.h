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
#include <linux/libnvdimm.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/nd.h>

extern struct list_head nvdimm_bus_list;
extern struct mutex nvdimm_bus_list_mutex;
extern int nvdimm_major;

struct nvdimm_bus {
	struct nvdimm_bus_descriptor *nd_desc;
	wait_queue_head_t probe_wait;
	struct module *module;
	struct list_head list;
	struct device dev;
	int id, probe_active;
	struct mutex reconfig_mutex;
};

struct nvdimm {
	unsigned long flags;
	void *provider_data;
	unsigned long *dsm_mask;
	struct device dev;
	atomic_t busy;
	int id;
};

bool is_nvdimm(struct device *dev);
bool is_nd_pmem(struct device *dev);
bool is_nd_blk(struct device *dev);
struct nvdimm_bus *walk_to_nvdimm_bus(struct device *nd_dev);
int __init nvdimm_bus_init(void);
void nvdimm_bus_exit(void);
void nd_region_probe_success(struct nvdimm_bus *nvdimm_bus, struct device *dev);
struct nd_region;
void nd_region_create_blk_seed(struct nd_region *nd_region);
void nd_region_create_btt_seed(struct nd_region *nd_region);
void nd_region_disable(struct nvdimm_bus *nvdimm_bus, struct device *dev);
int nvdimm_bus_create_ndctl(struct nvdimm_bus *nvdimm_bus);
void nvdimm_bus_destroy_ndctl(struct nvdimm_bus *nvdimm_bus);
void nd_synchronize(void);
int nvdimm_bus_register_dimms(struct nvdimm_bus *nvdimm_bus);
int nvdimm_bus_register_regions(struct nvdimm_bus *nvdimm_bus);
int nvdimm_bus_init_interleave_sets(struct nvdimm_bus *nvdimm_bus);
void __nd_device_register(struct device *dev);
int nd_match_dimm(struct device *dev, void *data);
struct nd_label_id;
char *nd_label_gen_id(struct nd_label_id *label_id, u8 *uuid, u32 flags);
bool nd_is_uuid_unique(struct device *dev, u8 *uuid);
struct nd_region;
struct nvdimm_drvdata;
struct nd_mapping;
resource_size_t nd_pmem_available_dpa(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, resource_size_t *overlap);
resource_size_t nd_blk_available_dpa(struct nd_mapping *nd_mapping);
resource_size_t nd_region_available_dpa(struct nd_region *nd_region);
resource_size_t nvdimm_allocated_dpa(struct nvdimm_drvdata *ndd,
		struct nd_label_id *label_id);
struct nd_mapping;
struct resource *nsblk_add_resource(struct nd_region *nd_region,
		struct nvdimm_drvdata *ndd, struct nd_namespace_blk *nsblk,
		resource_size_t start);
int nvdimm_num_label_slots(struct nvdimm_drvdata *ndd);
void get_ndd(struct nvdimm_drvdata *ndd);
resource_size_t __nvdimm_namespace_capacity(struct nd_namespace_common *ndns);
void nd_detach_ndns(struct device *dev, struct nd_namespace_common **_ndns);
void __nd_detach_ndns(struct device *dev, struct nd_namespace_common **_ndns);
bool nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns);
bool __nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns);
ssize_t nd_namespace_store(struct device *dev,
		struct nd_namespace_common **_ndns, const char *buf,
		size_t len);
#endif /* __ND_CORE_H__ */
