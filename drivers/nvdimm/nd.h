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
#include <linux/types.h>
#include "label.h"

struct nvdimm_drvdata {
	struct device *dev;
	int nsindex_size;
	struct nd_cmd_get_config_size nsarea;
	void *data;
	int ns_current, ns_next;
	struct resource dpa;
	struct kref kref;
};

struct nd_region_namespaces {
	int count;
	int active;
};

static inline struct nd_namespace_index *to_namespace_index(
		struct nvdimm_drvdata *ndd, int i)
{
	if (i < 0)
		return NULL;

	return ndd->data + sizeof_namespace_index(ndd) * i;
}

static inline struct nd_namespace_index *to_current_namespace_index(
		struct nvdimm_drvdata *ndd)
{
	return to_namespace_index(ndd, ndd->ns_current);
}

static inline struct nd_namespace_index *to_next_namespace_index(
		struct nvdimm_drvdata *ndd)
{
	return to_namespace_index(ndd, ndd->ns_next);
}

#define nd_dbg_dpa(r, d, res, fmt, arg...) \
	dev_dbg((r) ? &(r)->dev : (d)->dev, "%s: %.13s: %#llx @ %#llx " fmt, \
		(r) ? dev_name((d)->dev) : "", res ? res->name : "null", \
		(unsigned long long) (res ? resource_size(res) : 0), \
		(unsigned long long) (res ? res->start : 0), ##arg)

#define for_each_label(l, label, labels) \
	for (l = 0; (label = labels ? labels[l] : NULL); l++)

#define for_each_dpa_resource(ndd, res) \
	for (res = (ndd)->dpa.child; res; res = res->sibling)

#define for_each_dpa_resource_safe(ndd, res, next) \
	for (res = (ndd)->dpa.child, next = res ? res->sibling : NULL; \
			res; res = next, next = next ? next->sibling : NULL)

struct nd_region {
	struct device dev;
	struct ida ns_ida;
	struct device *ns_seed;
	u16 ndr_mappings;
	u64 ndr_size;
	u64 ndr_start;
	int id;
	void *provider_data;
	struct nd_interleave_set *nd_set;
	struct nd_mapping mapping[0];
};

/*
 * Lookup next in the repeating sequence of 01, 10, and 11.
 */
static inline unsigned nd_inc_seq(unsigned seq)
{
	static const unsigned next[] = { 0, 2, 3, 1 };

	return next[seq & 3];
}

enum nd_async_mode {
	ND_SYNC,
	ND_ASYNC,
};

void wait_nvdimm_bus_probe_idle(struct device *dev);
void nd_device_register(struct device *dev);
void nd_device_unregister(struct device *dev, enum nd_async_mode mode);
int nd_uuid_store(struct device *dev, u8 **uuid_out, const char *buf,
		size_t len);
ssize_t nd_sector_size_show(unsigned long current_lbasize,
		const unsigned long *supported, char *buf);
ssize_t nd_sector_size_store(struct device *dev, const char *buf,
		unsigned long *current_lbasize, const unsigned long *supported);
int __init nvdimm_init(void);
int __init nd_region_init(void);
void nvdimm_exit(void);
void nd_region_exit(void);
struct nvdimm;
struct nvdimm_drvdata *to_ndd(struct nd_mapping *nd_mapping);
int nvdimm_init_nsarea(struct nvdimm_drvdata *ndd);
int nvdimm_init_config_data(struct nvdimm_drvdata *ndd);
int nvdimm_set_config_data(struct nvdimm_drvdata *ndd, size_t offset,
		void *buf, size_t len);
struct nd_region *to_nd_region(struct device *dev);
int nd_region_to_nstype(struct nd_region *nd_region);
int nd_region_register_namespaces(struct nd_region *nd_region, int *err);
u64 nd_region_interleave_set_cookie(struct nd_region *nd_region);
void nvdimm_bus_lock(struct device *dev);
void nvdimm_bus_unlock(struct device *dev);
bool is_nvdimm_bus_locked(struct device *dev);
void nvdimm_drvdata_release(struct kref *kref);
void put_ndd(struct nvdimm_drvdata *ndd);
int nd_label_reserve_dpa(struct nvdimm_drvdata *ndd);
void nvdimm_free_dpa(struct nvdimm_drvdata *ndd, struct resource *res);
struct resource *nvdimm_allocate_dpa(struct nvdimm_drvdata *ndd,
		struct nd_label_id *label_id, resource_size_t start,
		resource_size_t n);
#endif /* __ND_H__ */
