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
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/sort.h>
#include <linux/io.h>
#include <linux/nd.h>
#include "nd-core.h"
#include "nd.h"

/*
 * For readq() and writeq() on 32-bit builds, the hi-lo, lo-hi order is
 * irrelevant.
 */
#include <linux/io-64-nonatomic-hi-lo.h>

static DEFINE_IDA(region_ida);
static DEFINE_PER_CPU(int, flush_idx);

static int nvdimm_map_flush(struct device *dev, struct nvdimm *nvdimm, int dimm,
		struct nd_region_data *ndrd)
{
	int i, j;

	dev_dbg(dev, "%s: map %d flush address%s\n", nvdimm_name(nvdimm),
			nvdimm->num_flush, nvdimm->num_flush == 1 ? "" : "es");
	for (i = 0; i < (1 << ndrd->hints_shift); i++) {
		struct resource *res = &nvdimm->flush_wpq[i];
		unsigned long pfn = PHYS_PFN(res->start);
		void __iomem *flush_page;

		/* check if flush hints share a page */
		for (j = 0; j < i; j++) {
			struct resource *res_j = &nvdimm->flush_wpq[j];
			unsigned long pfn_j = PHYS_PFN(res_j->start);

			if (pfn == pfn_j)
				break;
		}

		if (j < i)
			flush_page = (void __iomem *) ((unsigned long)
					ndrd_get_flush_wpq(ndrd, dimm, j)
					& PAGE_MASK);
		else
			flush_page = devm_nvdimm_ioremap(dev,
					PFN_PHYS(pfn), PAGE_SIZE);
		if (!flush_page)
			return -ENXIO;
		ndrd_set_flush_wpq(ndrd, dimm, i, flush_page
				+ (res->start & ~PAGE_MASK));
	}

	return 0;
}

int nd_region_activate(struct nd_region *nd_region)
{
	int i, j, num_flush = 0;
	struct nd_region_data *ndrd;
	struct device *dev = &nd_region->dev;
	size_t flush_data_size = sizeof(void *);

	nvdimm_bus_lock(&nd_region->dev);
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		/* at least one null hint slot per-dimm for the "no-hint" case */
		flush_data_size += sizeof(void *);
		num_flush = min_not_zero(num_flush, nvdimm->num_flush);
		if (!nvdimm->num_flush)
			continue;
		flush_data_size += nvdimm->num_flush * sizeof(void *);
	}
	nvdimm_bus_unlock(&nd_region->dev);

	ndrd = devm_kzalloc(dev, sizeof(*ndrd) + flush_data_size, GFP_KERNEL);
	if (!ndrd)
		return -ENOMEM;
	dev_set_drvdata(dev, ndrd);

	if (!num_flush)
		return 0;

	ndrd->hints_shift = ilog2(num_flush);
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;
		int rc = nvdimm_map_flush(&nd_region->dev, nvdimm, i, ndrd);

		if (rc)
			return rc;
	}

	/*
	 * Clear out entries that are duplicates. This should prevent the
	 * extra flushings.
	 */
	for (i = 0; i < nd_region->ndr_mappings - 1; i++) {
		/* ignore if NULL already */
		if (!ndrd_get_flush_wpq(ndrd, i, 0))
			continue;

		for (j = i + 1; j < nd_region->ndr_mappings; j++)
			if (ndrd_get_flush_wpq(ndrd, i, 0) ==
			    ndrd_get_flush_wpq(ndrd, j, 0))
				ndrd_set_flush_wpq(ndrd, j, 0, NULL);
	}

	return 0;
}

static void nd_region_release(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev);
	u16 i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		put_device(&nvdimm->dev);
	}
	free_percpu(nd_region->lane);
	ida_simple_remove(&region_ida, nd_region->id);
	if (is_nd_blk(dev))
		kfree(to_nd_blk_region(dev));
	else
		kfree(nd_region);
}

static struct device_type nd_blk_device_type = {
	.name = "nd_blk",
	.release = nd_region_release,
};

static struct device_type nd_pmem_device_type = {
	.name = "nd_pmem",
	.release = nd_region_release,
};

static struct device_type nd_volatile_device_type = {
	.name = "nd_volatile",
	.release = nd_region_release,
};

bool is_nd_pmem(struct device *dev)
{
	return dev ? dev->type == &nd_pmem_device_type : false;
}

bool is_nd_blk(struct device *dev)
{
	return dev ? dev->type == &nd_blk_device_type : false;
}

bool is_nd_volatile(struct device *dev)
{
	return dev ? dev->type == &nd_volatile_device_type : false;
}

struct nd_region *to_nd_region(struct device *dev)
{
	struct nd_region *nd_region = container_of(dev, struct nd_region, dev);

	WARN_ON(dev->type->release != nd_region_release);
	return nd_region;
}
EXPORT_SYMBOL_GPL(to_nd_region);

struct device *nd_region_dev(struct nd_region *nd_region)
{
	if (!nd_region)
		return NULL;
	return &nd_region->dev;
}
EXPORT_SYMBOL_GPL(nd_region_dev);

struct nd_blk_region *to_nd_blk_region(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev);

	WARN_ON(!is_nd_blk(dev));
	return container_of(nd_region, struct nd_blk_region, nd_region);
}
EXPORT_SYMBOL_GPL(to_nd_blk_region);

void *nd_region_provider_data(struct nd_region *nd_region)
{
	return nd_region->provider_data;
}
EXPORT_SYMBOL_GPL(nd_region_provider_data);

void *nd_blk_region_provider_data(struct nd_blk_region *ndbr)
{
	return ndbr->blk_provider_data;
}
EXPORT_SYMBOL_GPL(nd_blk_region_provider_data);

void nd_blk_region_set_provider_data(struct nd_blk_region *ndbr, void *data)
{
	ndbr->blk_provider_data = data;
}
EXPORT_SYMBOL_GPL(nd_blk_region_set_provider_data);

/**
 * nd_region_to_nstype() - region to an integer namespace type
 * @nd_region: region-device to interrogate
 *
 * This is the 'nstype' attribute of a region as well, an input to the
 * MODALIAS for namespace devices, and bit number for a nvdimm_bus to match
 * namespace devices with namespace drivers.
 */
int nd_region_to_nstype(struct nd_region *nd_region)
{
	if (is_memory(&nd_region->dev)) {
		u16 i, alias;

		for (i = 0, alias = 0; i < nd_region->ndr_mappings; i++) {
			struct nd_mapping *nd_mapping = &nd_region->mapping[i];
			struct nvdimm *nvdimm = nd_mapping->nvdimm;

			if (test_bit(NDD_ALIASING, &nvdimm->flags))
				alias++;
		}
		if (alias)
			return ND_DEVICE_NAMESPACE_PMEM;
		else
			return ND_DEVICE_NAMESPACE_IO;
	} else if (is_nd_blk(&nd_region->dev)) {
		return ND_DEVICE_NAMESPACE_BLK;
	}

	return 0;
}
EXPORT_SYMBOL(nd_region_to_nstype);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	unsigned long long size = 0;

	if (is_memory(dev)) {
		size = nd_region->ndr_size;
	} else if (nd_region->ndr_mappings == 1) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[0];

		size = nd_mapping->size;
	}

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(size);

static ssize_t deep_flush_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	/*
	 * NOTE: in the nvdimm_has_flush() error case this attribute is
	 * not visible.
	 */
	return sprintf(buf, "%d\n", nvdimm_has_flush(nd_region));
}

static ssize_t deep_flush_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	bool flush;
	int rc = strtobool(buf, &flush);
	struct nd_region *nd_region = to_nd_region(dev);

	if (rc)
		return rc;
	if (!flush)
		return -EINVAL;
	nvdimm_flush(nd_region);

	return len;
}
static DEVICE_ATTR_RW(deep_flush);

static ssize_t mappings_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	return sprintf(buf, "%d\n", nd_region->ndr_mappings);
}
static DEVICE_ATTR_RO(mappings);

static ssize_t nstype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	return sprintf(buf, "%d\n", nd_region_to_nstype(nd_region));
}
static DEVICE_ATTR_RO(nstype);

static ssize_t set_cookie_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	struct nd_interleave_set *nd_set = nd_region->nd_set;
	ssize_t rc = 0;

	if (is_memory(dev) && nd_set)
		/* pass, should be precluded by region_visible */;
	else
		return -ENXIO;

	/*
	 * The cookie to show depends on which specification of the
	 * labels we are using. If there are not labels then default to
	 * the v1.1 namespace label cookie definition. To read all this
	 * data we need to wait for probing to settle.
	 */
	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	if (nd_region->ndr_mappings) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[0];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);

		if (ndd) {
			struct nd_namespace_index *nsindex;

			nsindex = to_namespace_index(ndd, ndd->ns_current);
			rc = sprintf(buf, "%#llx\n",
					nd_region_interleave_set_cookie(nd_region,
						nsindex));
		}
	}
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	if (rc)
		return rc;
	return sprintf(buf, "%#llx\n", nd_set->cookie1);
}
static DEVICE_ATTR_RO(set_cookie);

resource_size_t nd_region_available_dpa(struct nd_region *nd_region)
{
	resource_size_t blk_max_overlap = 0, available, overlap;
	int i;

	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));

 retry:
	available = 0;
	overlap = blk_max_overlap;
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);

		/* if a dimm is disabled the available capacity is zero */
		if (!ndd)
			return 0;

		if (is_memory(&nd_region->dev)) {
			available += nd_pmem_available_dpa(nd_region,
					nd_mapping, &overlap);
			if (overlap > blk_max_overlap) {
				blk_max_overlap = overlap;
				goto retry;
			}
		} else if (is_nd_blk(&nd_region->dev))
			available += nd_blk_available_dpa(nd_region);
	}

	return available;
}

resource_size_t nd_region_allocatable_dpa(struct nd_region *nd_region)
{
	resource_size_t available = 0;
	int i;

	if (is_memory(&nd_region->dev))
		available = PHYS_ADDR_MAX;

	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];

		if (is_memory(&nd_region->dev))
			available = min(available,
					nd_pmem_max_contiguous_dpa(nd_region,
								   nd_mapping));
		else if (is_nd_blk(&nd_region->dev))
			available += nd_blk_available_dpa(nd_region);
	}
	if (is_memory(&nd_region->dev))
		return available * nd_region->ndr_mappings;
	return available;
}

static ssize_t available_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	unsigned long long available = 0;

	/*
	 * Flush in-flight updates and grab a snapshot of the available
	 * size.  Of course, this value is potentially invalidated the
	 * memory nvdimm_bus_lock() is dropped, but that's userspace's
	 * problem to not race itself.
	 */
	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	available = nd_region_available_dpa(nd_region);
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return sprintf(buf, "%llu\n", available);
}
static DEVICE_ATTR_RO(available_size);

static ssize_t max_available_extent_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	unsigned long long available = 0;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	available = nd_region_allocatable_dpa(nd_region);
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return sprintf(buf, "%llu\n", available);
}
static DEVICE_ATTR_RO(max_available_extent);

static ssize_t init_namespaces_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region_data *ndrd = dev_get_drvdata(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (ndrd)
		rc = sprintf(buf, "%d/%d\n", ndrd->ns_active, ndrd->ns_count);
	else
		rc = -ENXIO;
	nvdimm_bus_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(init_namespaces);

static ssize_t namespace_seed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (nd_region->ns_seed)
		rc = sprintf(buf, "%s\n", dev_name(nd_region->ns_seed));
	else
		rc = sprintf(buf, "\n");
	nvdimm_bus_unlock(dev);
	return rc;
}
static DEVICE_ATTR_RO(namespace_seed);

static ssize_t btt_seed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (nd_region->btt_seed)
		rc = sprintf(buf, "%s\n", dev_name(nd_region->btt_seed));
	else
		rc = sprintf(buf, "\n");
	nvdimm_bus_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(btt_seed);

static ssize_t pfn_seed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (nd_region->pfn_seed)
		rc = sprintf(buf, "%s\n", dev_name(nd_region->pfn_seed));
	else
		rc = sprintf(buf, "\n");
	nvdimm_bus_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(pfn_seed);

static ssize_t dax_seed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (nd_region->dax_seed)
		rc = sprintf(buf, "%s\n", dev_name(nd_region->dax_seed));
	else
		rc = sprintf(buf, "\n");
	nvdimm_bus_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(dax_seed);

static ssize_t read_only_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	return sprintf(buf, "%d\n", nd_region->ro);
}

static ssize_t read_only_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	bool ro;
	int rc = strtobool(buf, &ro);
	struct nd_region *nd_region = to_nd_region(dev);

	if (rc)
		return rc;

	nd_region->ro = ro;
	return len;
}
static DEVICE_ATTR_RW(read_only);

static ssize_t region_badblocks_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver)
		rc = badblocks_show(&nd_region->bb, buf, 0);
	else
		rc = -ENXIO;
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR(badblocks, 0444, region_badblocks_show, NULL);

static ssize_t resource_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	return sprintf(buf, "%#llx\n", nd_region->ndr_start);
}
static DEVICE_ATTR_RO(resource);

static ssize_t persistence_domain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);

	if (test_bit(ND_REGION_PERSIST_CACHE, &nd_region->flags))
		return sprintf(buf, "cpu_cache\n");
	else if (test_bit(ND_REGION_PERSIST_MEMCTRL, &nd_region->flags))
		return sprintf(buf, "memory_controller\n");
	else
		return sprintf(buf, "\n");
}
static DEVICE_ATTR_RO(persistence_domain);

static struct attribute *nd_region_attributes[] = {
	&dev_attr_size.attr,
	&dev_attr_nstype.attr,
	&dev_attr_mappings.attr,
	&dev_attr_btt_seed.attr,
	&dev_attr_pfn_seed.attr,
	&dev_attr_dax_seed.attr,
	&dev_attr_deep_flush.attr,
	&dev_attr_read_only.attr,
	&dev_attr_set_cookie.attr,
	&dev_attr_available_size.attr,
	&dev_attr_max_available_extent.attr,
	&dev_attr_namespace_seed.attr,
	&dev_attr_init_namespaces.attr,
	&dev_attr_badblocks.attr,
	&dev_attr_resource.attr,
	&dev_attr_persistence_domain.attr,
	NULL,
};

static umode_t region_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);
	struct nd_region *nd_region = to_nd_region(dev);
	struct nd_interleave_set *nd_set = nd_region->nd_set;
	int type = nd_region_to_nstype(nd_region);

	if (!is_memory(dev) && a == &dev_attr_pfn_seed.attr)
		return 0;

	if (!is_memory(dev) && a == &dev_attr_dax_seed.attr)
		return 0;

	if (!is_memory(dev) && a == &dev_attr_badblocks.attr)
		return 0;

	if (a == &dev_attr_resource.attr) {
		if (is_memory(dev))
			return 0400;
		else
			return 0;
	}

	if (a == &dev_attr_deep_flush.attr) {
		int has_flush = nvdimm_has_flush(nd_region);

		if (has_flush == 1)
			return a->mode;
		else if (has_flush == 0)
			return 0444;
		else
			return 0;
	}

	if (a == &dev_attr_persistence_domain.attr) {
		if ((nd_region->flags & (BIT(ND_REGION_PERSIST_CACHE)
					| BIT(ND_REGION_PERSIST_MEMCTRL))) == 0)
			return 0;
		return a->mode;
	}

	if (a != &dev_attr_set_cookie.attr
			&& a != &dev_attr_available_size.attr)
		return a->mode;

	if ((type == ND_DEVICE_NAMESPACE_PMEM
				|| type == ND_DEVICE_NAMESPACE_BLK)
			&& a == &dev_attr_available_size.attr)
		return a->mode;
	else if (is_memory(dev) && nd_set)
		return a->mode;

	return 0;
}

struct attribute_group nd_region_attribute_group = {
	.attrs = nd_region_attributes,
	.is_visible = region_visible,
};
EXPORT_SYMBOL_GPL(nd_region_attribute_group);

u64 nd_region_interleave_set_cookie(struct nd_region *nd_region,
		struct nd_namespace_index *nsindex)
{
	struct nd_interleave_set *nd_set = nd_region->nd_set;

	if (!nd_set)
		return 0;

	if (nsindex && __le16_to_cpu(nsindex->major) == 1
			&& __le16_to_cpu(nsindex->minor) == 1)
		return nd_set->cookie1;
	return nd_set->cookie2;
}

u64 nd_region_interleave_set_altcookie(struct nd_region *nd_region)
{
	struct nd_interleave_set *nd_set = nd_region->nd_set;

	if (nd_set)
		return nd_set->altcookie;
	return 0;
}

void nd_mapping_free_labels(struct nd_mapping *nd_mapping)
{
	struct nd_label_ent *label_ent, *e;

	lockdep_assert_held(&nd_mapping->lock);
	list_for_each_entry_safe(label_ent, e, &nd_mapping->labels, list) {
		list_del(&label_ent->list);
		kfree(label_ent);
	}
}

/*
 * Upon successful probe/remove, take/release a reference on the
 * associated interleave set (if present), and plant new btt + namespace
 * seeds.  Also, on the removal of a BLK region, notify the provider to
 * disable the region.
 */
static void nd_region_notify_driver_action(struct nvdimm_bus *nvdimm_bus,
		struct device *dev, bool probe)
{
	struct nd_region *nd_region;

	if (!probe && is_nd_region(dev)) {
		int i;

		nd_region = to_nd_region(dev);
		for (i = 0; i < nd_region->ndr_mappings; i++) {
			struct nd_mapping *nd_mapping = &nd_region->mapping[i];
			struct nvdimm_drvdata *ndd = nd_mapping->ndd;
			struct nvdimm *nvdimm = nd_mapping->nvdimm;

			mutex_lock(&nd_mapping->lock);
			nd_mapping_free_labels(nd_mapping);
			mutex_unlock(&nd_mapping->lock);

			put_ndd(ndd);
			nd_mapping->ndd = NULL;
			if (ndd)
				atomic_dec(&nvdimm->busy);
		}
	}
	if (dev->parent && is_nd_region(dev->parent) && probe) {
		nd_region = to_nd_region(dev->parent);
		nvdimm_bus_lock(dev);
		if (nd_region->ns_seed == dev)
			nd_region_create_ns_seed(nd_region);
		nvdimm_bus_unlock(dev);
	}
	if (is_nd_btt(dev) && probe) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		nd_region = to_nd_region(dev->parent);
		nvdimm_bus_lock(dev);
		if (nd_region->btt_seed == dev)
			nd_region_create_btt_seed(nd_region);
		if (nd_region->ns_seed == &nd_btt->ndns->dev)
			nd_region_create_ns_seed(nd_region);
		nvdimm_bus_unlock(dev);
	}
	if (is_nd_pfn(dev) && probe) {
		struct nd_pfn *nd_pfn = to_nd_pfn(dev);

		nd_region = to_nd_region(dev->parent);
		nvdimm_bus_lock(dev);
		if (nd_region->pfn_seed == dev)
			nd_region_create_pfn_seed(nd_region);
		if (nd_region->ns_seed == &nd_pfn->ndns->dev)
			nd_region_create_ns_seed(nd_region);
		nvdimm_bus_unlock(dev);
	}
	if (is_nd_dax(dev) && probe) {
		struct nd_dax *nd_dax = to_nd_dax(dev);

		nd_region = to_nd_region(dev->parent);
		nvdimm_bus_lock(dev);
		if (nd_region->dax_seed == dev)
			nd_region_create_dax_seed(nd_region);
		if (nd_region->ns_seed == &nd_dax->nd_pfn.ndns->dev)
			nd_region_create_ns_seed(nd_region);
		nvdimm_bus_unlock(dev);
	}
}

void nd_region_probe_success(struct nvdimm_bus *nvdimm_bus, struct device *dev)
{
	nd_region_notify_driver_action(nvdimm_bus, dev, true);
}

void nd_region_disable(struct nvdimm_bus *nvdimm_bus, struct device *dev)
{
	nd_region_notify_driver_action(nvdimm_bus, dev, false);
}

static ssize_t mappingN(struct device *dev, char *buf, int n)
{
	struct nd_region *nd_region = to_nd_region(dev);
	struct nd_mapping *nd_mapping;
	struct nvdimm *nvdimm;

	if (n >= nd_region->ndr_mappings)
		return -ENXIO;
	nd_mapping = &nd_region->mapping[n];
	nvdimm = nd_mapping->nvdimm;

	return sprintf(buf, "%s,%llu,%llu,%d\n", dev_name(&nvdimm->dev),
			nd_mapping->start, nd_mapping->size,
			nd_mapping->position);
}

#define REGION_MAPPING(idx) \
static ssize_t mapping##idx##_show(struct device *dev,		\
		struct device_attribute *attr, char *buf)	\
{								\
	return mappingN(dev, buf, idx);				\
}								\
static DEVICE_ATTR_RO(mapping##idx)

/*
 * 32 should be enough for a while, even in the presence of socket
 * interleave a 32-way interleave set is a degenerate case.
 */
REGION_MAPPING(0);
REGION_MAPPING(1);
REGION_MAPPING(2);
REGION_MAPPING(3);
REGION_MAPPING(4);
REGION_MAPPING(5);
REGION_MAPPING(6);
REGION_MAPPING(7);
REGION_MAPPING(8);
REGION_MAPPING(9);
REGION_MAPPING(10);
REGION_MAPPING(11);
REGION_MAPPING(12);
REGION_MAPPING(13);
REGION_MAPPING(14);
REGION_MAPPING(15);
REGION_MAPPING(16);
REGION_MAPPING(17);
REGION_MAPPING(18);
REGION_MAPPING(19);
REGION_MAPPING(20);
REGION_MAPPING(21);
REGION_MAPPING(22);
REGION_MAPPING(23);
REGION_MAPPING(24);
REGION_MAPPING(25);
REGION_MAPPING(26);
REGION_MAPPING(27);
REGION_MAPPING(28);
REGION_MAPPING(29);
REGION_MAPPING(30);
REGION_MAPPING(31);

static umode_t mapping_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nd_region *nd_region = to_nd_region(dev);

	if (n < nd_region->ndr_mappings)
		return a->mode;
	return 0;
}

static struct attribute *mapping_attributes[] = {
	&dev_attr_mapping0.attr,
	&dev_attr_mapping1.attr,
	&dev_attr_mapping2.attr,
	&dev_attr_mapping3.attr,
	&dev_attr_mapping4.attr,
	&dev_attr_mapping5.attr,
	&dev_attr_mapping6.attr,
	&dev_attr_mapping7.attr,
	&dev_attr_mapping8.attr,
	&dev_attr_mapping9.attr,
	&dev_attr_mapping10.attr,
	&dev_attr_mapping11.attr,
	&dev_attr_mapping12.attr,
	&dev_attr_mapping13.attr,
	&dev_attr_mapping14.attr,
	&dev_attr_mapping15.attr,
	&dev_attr_mapping16.attr,
	&dev_attr_mapping17.attr,
	&dev_attr_mapping18.attr,
	&dev_attr_mapping19.attr,
	&dev_attr_mapping20.attr,
	&dev_attr_mapping21.attr,
	&dev_attr_mapping22.attr,
	&dev_attr_mapping23.attr,
	&dev_attr_mapping24.attr,
	&dev_attr_mapping25.attr,
	&dev_attr_mapping26.attr,
	&dev_attr_mapping27.attr,
	&dev_attr_mapping28.attr,
	&dev_attr_mapping29.attr,
	&dev_attr_mapping30.attr,
	&dev_attr_mapping31.attr,
	NULL,
};

struct attribute_group nd_mapping_attribute_group = {
	.is_visible = mapping_visible,
	.attrs = mapping_attributes,
};
EXPORT_SYMBOL_GPL(nd_mapping_attribute_group);

int nd_blk_region_init(struct nd_region *nd_region)
{
	struct device *dev = &nd_region->dev;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	if (!is_nd_blk(dev))
		return 0;

	if (nd_region->ndr_mappings < 1) {
		dev_dbg(dev, "invalid BLK region\n");
		return -ENXIO;
	}

	return to_nd_blk_region(dev)->enable(nvdimm_bus, dev);
}

/**
 * nd_region_acquire_lane - allocate and lock a lane
 * @nd_region: region id and number of lanes possible
 *
 * A lane correlates to a BLK-data-window and/or a log slot in the BTT.
 * We optimize for the common case where there are 256 lanes, one
 * per-cpu.  For larger systems we need to lock to share lanes.  For now
 * this implementation assumes the cost of maintaining an allocator for
 * free lanes is on the order of the lock hold time, so it implements a
 * static lane = cpu % num_lanes mapping.
 *
 * In the case of a BTT instance on top of a BLK namespace a lane may be
 * acquired recursively.  We lock on the first instance.
 *
 * In the case of a BTT instance on top of PMEM, we only acquire a lane
 * for the BTT metadata updates.
 */
unsigned int nd_region_acquire_lane(struct nd_region *nd_region)
{
	unsigned int cpu, lane;

	cpu = get_cpu();
	if (nd_region->num_lanes < nr_cpu_ids) {
		struct nd_percpu_lane *ndl_lock, *ndl_count;

		lane = cpu % nd_region->num_lanes;
		ndl_count = per_cpu_ptr(nd_region->lane, cpu);
		ndl_lock = per_cpu_ptr(nd_region->lane, lane);
		if (ndl_count->count++ == 0)
			spin_lock(&ndl_lock->lock);
	} else
		lane = cpu;

	return lane;
}
EXPORT_SYMBOL(nd_region_acquire_lane);

void nd_region_release_lane(struct nd_region *nd_region, unsigned int lane)
{
	if (nd_region->num_lanes < nr_cpu_ids) {
		unsigned int cpu = get_cpu();
		struct nd_percpu_lane *ndl_lock, *ndl_count;

		ndl_count = per_cpu_ptr(nd_region->lane, cpu);
		ndl_lock = per_cpu_ptr(nd_region->lane, lane);
		if (--ndl_count->count == 0)
			spin_unlock(&ndl_lock->lock);
		put_cpu();
	}
	put_cpu();
}
EXPORT_SYMBOL(nd_region_release_lane);

static struct nd_region *nd_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc, struct device_type *dev_type,
		const char *caller)
{
	struct nd_region *nd_region;
	struct device *dev;
	void *region_buf;
	unsigned int i;
	int ro = 0;

	for (i = 0; i < ndr_desc->num_mappings; i++) {
		struct nd_mapping_desc *mapping = &ndr_desc->mapping[i];
		struct nvdimm *nvdimm = mapping->nvdimm;

		if ((mapping->start | mapping->size) % SZ_4K) {
			dev_err(&nvdimm_bus->dev, "%s: %s mapping%d is not 4K aligned\n",
					caller, dev_name(&nvdimm->dev), i);

			return NULL;
		}

		if (test_bit(NDD_UNARMED, &nvdimm->flags))
			ro = 1;
	}

	if (dev_type == &nd_blk_device_type) {
		struct nd_blk_region_desc *ndbr_desc;
		struct nd_blk_region *ndbr;

		ndbr_desc = to_blk_region_desc(ndr_desc);
		ndbr = kzalloc(sizeof(*ndbr) + sizeof(struct nd_mapping)
				* ndr_desc->num_mappings,
				GFP_KERNEL);
		if (ndbr) {
			nd_region = &ndbr->nd_region;
			ndbr->enable = ndbr_desc->enable;
			ndbr->do_io = ndbr_desc->do_io;
		}
		region_buf = ndbr;
	} else {
		nd_region = kzalloc(sizeof(struct nd_region)
				+ sizeof(struct nd_mapping)
				* ndr_desc->num_mappings,
				GFP_KERNEL);
		region_buf = nd_region;
	}

	if (!region_buf)
		return NULL;
	nd_region->id = ida_simple_get(&region_ida, 0, 0, GFP_KERNEL);
	if (nd_region->id < 0)
		goto err_id;

	nd_region->lane = alloc_percpu(struct nd_percpu_lane);
	if (!nd_region->lane)
		goto err_percpu;

        for (i = 0; i < nr_cpu_ids; i++) {
		struct nd_percpu_lane *ndl;

		ndl = per_cpu_ptr(nd_region->lane, i);
		spin_lock_init(&ndl->lock);
		ndl->count = 0;
	}

	for (i = 0; i < ndr_desc->num_mappings; i++) {
		struct nd_mapping_desc *mapping = &ndr_desc->mapping[i];
		struct nvdimm *nvdimm = mapping->nvdimm;

		nd_region->mapping[i].nvdimm = nvdimm;
		nd_region->mapping[i].start = mapping->start;
		nd_region->mapping[i].size = mapping->size;
		nd_region->mapping[i].position = mapping->position;
		INIT_LIST_HEAD(&nd_region->mapping[i].labels);
		mutex_init(&nd_region->mapping[i].lock);

		get_device(&nvdimm->dev);
	}
	nd_region->ndr_mappings = ndr_desc->num_mappings;
	nd_region->provider_data = ndr_desc->provider_data;
	nd_region->nd_set = ndr_desc->nd_set;
	nd_region->num_lanes = ndr_desc->num_lanes;
	nd_region->flags = ndr_desc->flags;
	nd_region->ro = ro;
	nd_region->numa_node = ndr_desc->numa_node;
	ida_init(&nd_region->ns_ida);
	ida_init(&nd_region->btt_ida);
	ida_init(&nd_region->pfn_ida);
	ida_init(&nd_region->dax_ida);
	dev = &nd_region->dev;
	dev_set_name(dev, "region%d", nd_region->id);
	dev->parent = &nvdimm_bus->dev;
	dev->type = dev_type;
	dev->groups = ndr_desc->attr_groups;
	dev->of_node = ndr_desc->of_node;
	nd_region->ndr_size = resource_size(ndr_desc->res);
	nd_region->ndr_start = ndr_desc->res->start;
	nd_device_register(dev);

	return nd_region;

 err_percpu:
	ida_simple_remove(&region_ida, nd_region->id);
 err_id:
	kfree(region_buf);
	return NULL;
}

struct nd_region *nvdimm_pmem_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	ndr_desc->num_lanes = ND_MAX_LANES;
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_pmem_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_pmem_region_create);

struct nd_region *nvdimm_blk_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	if (ndr_desc->num_mappings > 1)
		return NULL;
	ndr_desc->num_lanes = min(ndr_desc->num_lanes, ND_MAX_LANES);
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_blk_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_blk_region_create);

struct nd_region *nvdimm_volatile_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	ndr_desc->num_lanes = ND_MAX_LANES;
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_volatile_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_volatile_region_create);

/**
 * nvdimm_flush - flush any posted write queues between the cpu and pmem media
 * @nd_region: blk or interleaved pmem region
 */
void nvdimm_flush(struct nd_region *nd_region)
{
	struct nd_region_data *ndrd = dev_get_drvdata(&nd_region->dev);
	int i, idx;

	/*
	 * Try to encourage some diversity in flush hint addresses
	 * across cpus assuming a limited number of flush hints.
	 */
	idx = this_cpu_read(flush_idx);
	idx = this_cpu_add_return(flush_idx, hash_32(current->pid + idx, 8));

	/*
	 * The first wmb() is needed to 'sfence' all previous writes
	 * such that they are architecturally visible for the platform
	 * buffer flush.  Note that we've already arranged for pmem
	 * writes to avoid the cache via memcpy_flushcache().  The final
	 * wmb() ensures ordering for the NVDIMM flush write.
	 */
	wmb();
	for (i = 0; i < nd_region->ndr_mappings; i++)
		if (ndrd_get_flush_wpq(ndrd, i, 0))
			writeq(1, ndrd_get_flush_wpq(ndrd, i, idx));
	wmb();
}
EXPORT_SYMBOL_GPL(nvdimm_flush);

/**
 * nvdimm_has_flush - determine write flushing requirements
 * @nd_region: blk or interleaved pmem region
 *
 * Returns 1 if writes require flushing
 * Returns 0 if writes do not require flushing
 * Returns -ENXIO if flushing capability can not be determined
 */
int nvdimm_has_flush(struct nd_region *nd_region)
{
	int i;

	/* no nvdimm or pmem api == flushing capability unknown */
	if (nd_region->ndr_mappings == 0
			|| !IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API))
		return -ENXIO;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		/* flush hints present / available */
		if (nvdimm->num_flush)
			return 1;
	}

	/*
	 * The platform defines dimm devices without hints, assume
	 * platform persistence mechanism like ADR
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(nvdimm_has_flush);

int nvdimm_has_cache(struct nd_region *nd_region)
{
	return is_nd_pmem(&nd_region->dev) &&
		!test_bit(ND_REGION_PERSIST_CACHE, &nd_region->flags);
}
EXPORT_SYMBOL_GPL(nvdimm_has_cache);

struct conflict_context {
	struct nd_region *nd_region;
	resource_size_t start, size;
};

static int region_conflict(struct device *dev, void *data)
{
	struct nd_region *nd_region;
	struct conflict_context *ctx = data;
	resource_size_t res_end, region_end, region_start;

	if (!is_memory(dev))
		return 0;

	nd_region = to_nd_region(dev);
	if (nd_region == ctx->nd_region)
		return 0;

	res_end = ctx->start + ctx->size;
	region_start = nd_region->ndr_start;
	region_end = region_start + nd_region->ndr_size;
	if (ctx->start >= region_start && ctx->start < region_end)
		return -EBUSY;
	if (res_end > region_start && res_end <= region_end)
		return -EBUSY;
	return 0;
}

int nd_region_conflict(struct nd_region *nd_region, resource_size_t start,
		resource_size_t size)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(&nd_region->dev);
	struct conflict_context ctx = {
		.nd_region = nd_region,
		.start = start,
		.size = size,
	};

	return device_for_each_child(&nvdimm_bus->dev, &ctx, region_conflict);
}

void __exit nd_region_devs_exit(void)
{
	ida_destroy(&region_ida);
}
