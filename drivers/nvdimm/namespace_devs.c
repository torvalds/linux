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
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/pmem.h>
#include <linux/list.h>
#include <linux/nd.h>
#include "nd-core.h"
#include "nd.h"

static void namespace_io_release(struct device *dev)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

	kfree(nsio);
}

static void namespace_pmem_release(struct device *dev)
{
	struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);
	struct nd_region *nd_region = to_nd_region(dev->parent);

	if (nspm->id >= 0)
		ida_simple_remove(&nd_region->ns_ida, nspm->id);
	kfree(nspm->alt_name);
	kfree(nspm->uuid);
	kfree(nspm);
}

static void namespace_blk_release(struct device *dev)
{
	struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);
	struct nd_region *nd_region = to_nd_region(dev->parent);

	if (nsblk->id >= 0)
		ida_simple_remove(&nd_region->ns_ida, nsblk->id);
	kfree(nsblk->alt_name);
	kfree(nsblk->uuid);
	kfree(nsblk->res);
	kfree(nsblk);
}

static const struct device_type namespace_io_device_type = {
	.name = "nd_namespace_io",
	.release = namespace_io_release,
};

static const struct device_type namespace_pmem_device_type = {
	.name = "nd_namespace_pmem",
	.release = namespace_pmem_release,
};

static const struct device_type namespace_blk_device_type = {
	.name = "nd_namespace_blk",
	.release = namespace_blk_release,
};

static bool is_namespace_pmem(const struct device *dev)
{
	return dev ? dev->type == &namespace_pmem_device_type : false;
}

static bool is_namespace_blk(const struct device *dev)
{
	return dev ? dev->type == &namespace_blk_device_type : false;
}

static bool is_namespace_io(const struct device *dev)
{
	return dev ? dev->type == &namespace_io_device_type : false;
}

static int is_uuid_busy(struct device *dev, void *data)
{
	u8 *uuid1 = data, *uuid2 = NULL;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		uuid2 = nspm->uuid;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		uuid2 = nsblk->uuid;
	} else if (is_nd_btt(dev)) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		uuid2 = nd_btt->uuid;
	} else if (is_nd_pfn(dev)) {
		struct nd_pfn *nd_pfn = to_nd_pfn(dev);

		uuid2 = nd_pfn->uuid;
	}

	if (uuid2 && memcmp(uuid1, uuid2, NSLABEL_UUID_LEN) == 0)
		return -EBUSY;

	return 0;
}

static int is_namespace_uuid_busy(struct device *dev, void *data)
{
	if (is_nd_pmem(dev) || is_nd_blk(dev))
		return device_for_each_child(dev, data, is_uuid_busy);
	return 0;
}

/**
 * nd_is_uuid_unique - verify that no other namespace has @uuid
 * @dev: any device on a nvdimm_bus
 * @uuid: uuid to check
 */
bool nd_is_uuid_unique(struct device *dev, u8 *uuid)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	if (!nvdimm_bus)
		return false;
	WARN_ON_ONCE(!is_nvdimm_bus_locked(&nvdimm_bus->dev));
	if (device_for_each_child(&nvdimm_bus->dev, uuid,
				is_namespace_uuid_busy) != 0)
		return false;
	return true;
}

bool pmem_should_map_pages(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_namespace_io *nsio;

	if (!IS_ENABLED(CONFIG_ZONE_DEVICE))
		return false;

	if (!test_bit(ND_REGION_PAGEMAP, &nd_region->flags))
		return false;

	if (is_nd_pfn(dev) || is_nd_btt(dev))
		return false;

	nsio = to_nd_namespace_io(dev);
	if (region_intersects(nsio->res.start, resource_size(&nsio->res),
				IORESOURCE_SYSTEM_RAM,
				IORES_DESC_NONE) == REGION_MIXED)
		return false;

#ifdef ARCH_MEMREMAP_PMEM
	return ARCH_MEMREMAP_PMEM == MEMREMAP_WB;
#else
	return false;
#endif
}
EXPORT_SYMBOL(pmem_should_map_pages);

const char *nvdimm_namespace_disk_name(struct nd_namespace_common *ndns,
		char *name)
{
	struct nd_region *nd_region = to_nd_region(ndns->dev.parent);
	const char *suffix = NULL;

	if (ndns->claim && is_nd_btt(ndns->claim))
		suffix = "s";

	if (is_namespace_pmem(&ndns->dev) || is_namespace_io(&ndns->dev)) {
		int nsidx = 0;

		if (is_namespace_pmem(&ndns->dev)) {
			struct nd_namespace_pmem *nspm;

			nspm = to_nd_namespace_pmem(&ndns->dev);
			nsidx = nspm->id;
		}

		if (nsidx)
			sprintf(name, "pmem%d.%d%s", nd_region->id, nsidx,
					suffix ? suffix : "");
		else
			sprintf(name, "pmem%d%s", nd_region->id,
					suffix ? suffix : "");
	} else if (is_namespace_blk(&ndns->dev)) {
		struct nd_namespace_blk *nsblk;

		nsblk = to_nd_namespace_blk(&ndns->dev);
		sprintf(name, "ndblk%d.%d%s", nd_region->id, nsblk->id,
				suffix ? suffix : "");
	} else {
		return NULL;
	}

	return name;
}
EXPORT_SYMBOL(nvdimm_namespace_disk_name);

const u8 *nd_dev_to_uuid(struct device *dev)
{
	static const u8 null_uuid[16];

	if (!dev)
		return null_uuid;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		return nspm->uuid;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		return nsblk->uuid;
	} else
		return null_uuid;
}
EXPORT_SYMBOL(nd_dev_to_uuid);

static ssize_t nstype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);

	return sprintf(buf, "%d\n", nd_region_to_nstype(nd_region));
}
static DEVICE_ATTR_RO(nstype);

static ssize_t __alt_name_store(struct device *dev, const char *buf,
		const size_t len)
{
	char *input, *pos, *alt_name, **ns_altname;
	ssize_t rc;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		ns_altname = &nspm->alt_name;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		ns_altname = &nsblk->alt_name;
	} else
		return -ENXIO;

	if (dev->driver || to_ndns(dev)->claim)
		return -EBUSY;

	input = kmemdup(buf, len + 1, GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input[len] = '\0';
	pos = strim(input);
	if (strlen(pos) + 1 > NSLABEL_NAME_LEN) {
		rc = -EINVAL;
		goto out;
	}

	alt_name = kzalloc(NSLABEL_NAME_LEN, GFP_KERNEL);
	if (!alt_name) {
		rc = -ENOMEM;
		goto out;
	}
	kfree(*ns_altname);
	*ns_altname = alt_name;
	sprintf(*ns_altname, "%s", pos);
	rc = len;

out:
	kfree(input);
	return rc;
}

static resource_size_t nd_namespace_blk_size(struct nd_namespace_blk *nsblk)
{
	struct nd_region *nd_region = to_nd_region(nsblk->common.dev.parent);
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_label_id label_id;
	resource_size_t size = 0;
	struct resource *res;

	if (!nsblk->uuid)
		return 0;
	nd_label_gen_id(&label_id, nsblk->uuid, NSLABEL_FLAG_LOCAL);
	for_each_dpa_resource(ndd, res)
		if (strcmp(res->name, label_id.id) == 0)
			size += resource_size(res);
	return size;
}

static bool __nd_namespace_blk_validate(struct nd_namespace_blk *nsblk)
{
	struct nd_region *nd_region = to_nd_region(nsblk->common.dev.parent);
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_label_id label_id;
	struct resource *res;
	int count, i;

	if (!nsblk->uuid || !nsblk->lbasize || !ndd)
		return false;

	count = 0;
	nd_label_gen_id(&label_id, nsblk->uuid, NSLABEL_FLAG_LOCAL);
	for_each_dpa_resource(ndd, res) {
		if (strcmp(res->name, label_id.id) != 0)
			continue;
		/*
		 * Resources with unacknowledged adjustments indicate a
		 * failure to update labels
		 */
		if (res->flags & DPA_RESOURCE_ADJUSTED)
			return false;
		count++;
	}

	/* These values match after a successful label update */
	if (count != nsblk->num_resources)
		return false;

	for (i = 0; i < nsblk->num_resources; i++) {
		struct resource *found = NULL;

		for_each_dpa_resource(ndd, res)
			if (res == nsblk->res[i]) {
				found = res;
				break;
			}
		/* stale resource */
		if (!found)
			return false;
	}

	return true;
}

resource_size_t nd_namespace_blk_validate(struct nd_namespace_blk *nsblk)
{
	resource_size_t size;

	nvdimm_bus_lock(&nsblk->common.dev);
	size = __nd_namespace_blk_validate(nsblk);
	nvdimm_bus_unlock(&nsblk->common.dev);

	return size;
}
EXPORT_SYMBOL(nd_namespace_blk_validate);


static int nd_namespace_label_update(struct nd_region *nd_region,
		struct device *dev)
{
	dev_WARN_ONCE(dev, dev->driver || to_ndns(dev)->claim,
			"namespace must be idle during label update\n");
	if (dev->driver || to_ndns(dev)->claim)
		return 0;

	/*
	 * Only allow label writes that will result in a valid namespace
	 * or deletion of an existing namespace.
	 */
	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);
		resource_size_t size = resource_size(&nspm->nsio.res);

		if (size == 0 && nspm->uuid)
			/* delete allocation */;
		else if (!nspm->uuid)
			return 0;

		return nd_pmem_namespace_label_update(nd_region, nspm, size);
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);
		resource_size_t size = nd_namespace_blk_size(nsblk);

		if (size == 0 && nsblk->uuid)
			/* delete allocation */;
		else if (!nsblk->uuid || !nsblk->lbasize)
			return 0;

		return nd_blk_namespace_label_update(nd_region, nsblk, size);
	} else
		return -ENXIO;
}

static ssize_t alt_name_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	rc = __alt_name_store(dev, buf, len);
	if (rc >= 0)
		rc = nd_namespace_label_update(nd_region, dev);
	dev_dbg(dev, "%s: %s(%zd)\n", __func__, rc < 0 ? "fail " : "", rc);
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc < 0 ? rc : len;
}

static ssize_t alt_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *ns_altname;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		ns_altname = nspm->alt_name;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		ns_altname = nsblk->alt_name;
	} else
		return -ENXIO;

	return sprintf(buf, "%s\n", ns_altname ? ns_altname : "");
}
static DEVICE_ATTR_RW(alt_name);

static int scan_free(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, struct nd_label_id *label_id,
		resource_size_t n)
{
	bool is_blk = strncmp(label_id->id, "blk", 3) == 0;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	int rc = 0;

	while (n) {
		struct resource *res, *last;
		resource_size_t new_start;

		last = NULL;
		for_each_dpa_resource(ndd, res)
			if (strcmp(res->name, label_id->id) == 0)
				last = res;
		res = last;
		if (!res)
			return 0;

		if (n >= resource_size(res)) {
			n -= resource_size(res);
			nd_dbg_dpa(nd_region, ndd, res, "delete %d\n", rc);
			nvdimm_free_dpa(ndd, res);
			/* retry with last resource deleted */
			continue;
		}

		/*
		 * Keep BLK allocations relegated to high DPA as much as
		 * possible
		 */
		if (is_blk)
			new_start = res->start + n;
		else
			new_start = res->start;

		rc = adjust_resource(res, new_start, resource_size(res) - n);
		if (rc == 0)
			res->flags |= DPA_RESOURCE_ADJUSTED;
		nd_dbg_dpa(nd_region, ndd, res, "shrink %d\n", rc);
		break;
	}

	return rc;
}

/**
 * shrink_dpa_allocation - for each dimm in region free n bytes for label_id
 * @nd_region: the set of dimms to reclaim @n bytes from
 * @label_id: unique identifier for the namespace consuming this dpa range
 * @n: number of bytes per-dimm to release
 *
 * Assumes resources are ordered.  Starting from the end try to
 * adjust_resource() the allocation to @n, but if @n is larger than the
 * allocation delete it and find the 'new' last allocation in the label
 * set.
 */
static int shrink_dpa_allocation(struct nd_region *nd_region,
		struct nd_label_id *label_id, resource_size_t n)
{
	int i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		int rc;

		rc = scan_free(nd_region, nd_mapping, label_id, n);
		if (rc)
			return rc;
	}

	return 0;
}

static resource_size_t init_dpa_allocation(struct nd_label_id *label_id,
		struct nd_region *nd_region, struct nd_mapping *nd_mapping,
		resource_size_t n)
{
	bool is_blk = strncmp(label_id->id, "blk", 3) == 0;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	resource_size_t first_dpa;
	struct resource *res;
	int rc = 0;

	/* allocate blk from highest dpa first */
	if (is_blk)
		first_dpa = nd_mapping->start + nd_mapping->size - n;
	else
		first_dpa = nd_mapping->start;

	/* first resource allocation for this label-id or dimm */
	res = nvdimm_allocate_dpa(ndd, label_id, first_dpa, n);
	if (!res)
		rc = -EBUSY;

	nd_dbg_dpa(nd_region, ndd, res, "init %d\n", rc);
	return rc ? n : 0;
}


/**
 * space_valid() - validate free dpa space against constraints
 * @nd_region: hosting region of the free space
 * @ndd: dimm device data for debug
 * @label_id: namespace id to allocate space
 * @prev: potential allocation that precedes free space
 * @next: allocation that follows the given free space range
 * @exist: first allocation with same id in the mapping
 * @n: range that must satisfied for pmem allocations
 * @valid: free space range to validate
 *
 * BLK-space is valid as long as it does not precede a PMEM
 * allocation in a given region. PMEM-space must be contiguous
 * and adjacent to an existing existing allocation (if one
 * exists).  If reserving PMEM any space is valid.
 */
static void space_valid(struct nd_region *nd_region, struct nvdimm_drvdata *ndd,
		struct nd_label_id *label_id, struct resource *prev,
		struct resource *next, struct resource *exist,
		resource_size_t n, struct resource *valid)
{
	bool is_reserve = strcmp(label_id->id, "pmem-reserve") == 0;
	bool is_pmem = strncmp(label_id->id, "pmem", 4) == 0;

	if (valid->start >= valid->end)
		goto invalid;

	if (is_reserve)
		return;

	if (!is_pmem) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[0];
		struct nvdimm_bus *nvdimm_bus;
		struct blk_alloc_info info = {
			.nd_mapping = nd_mapping,
			.available = nd_mapping->size,
			.res = valid,
		};

		WARN_ON(!is_nd_blk(&nd_region->dev));
		nvdimm_bus = walk_to_nvdimm_bus(&nd_region->dev);
		device_for_each_child(&nvdimm_bus->dev, &info, alias_dpa_busy);
		return;
	}

	/* allocation needs to be contiguous, so this is all or nothing */
	if (resource_size(valid) < n)
		goto invalid;

	/* we've got all the space we need and no existing allocation */
	if (!exist)
		return;

	/* allocation needs to be contiguous with the existing namespace */
	if (valid->start == exist->end + 1
			|| valid->end == exist->start - 1)
		return;

 invalid:
	/* truncate @valid size to 0 */
	valid->end = valid->start - 1;
}

enum alloc_loc {
	ALLOC_ERR = 0, ALLOC_BEFORE, ALLOC_MID, ALLOC_AFTER,
};

static resource_size_t scan_allocate(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, struct nd_label_id *label_id,
		resource_size_t n)
{
	resource_size_t mapping_end = nd_mapping->start + nd_mapping->size - 1;
	bool is_pmem = strncmp(label_id->id, "pmem", 4) == 0;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct resource *res, *exist = NULL, valid;
	const resource_size_t to_allocate = n;
	int first;

	for_each_dpa_resource(ndd, res)
		if (strcmp(label_id->id, res->name) == 0)
			exist = res;

	valid.start = nd_mapping->start;
	valid.end = mapping_end;
	valid.name = "free space";
 retry:
	first = 0;
	for_each_dpa_resource(ndd, res) {
		struct resource *next = res->sibling, *new_res = NULL;
		resource_size_t allocate, available = 0;
		enum alloc_loc loc = ALLOC_ERR;
		const char *action;
		int rc = 0;

		/* ignore resources outside this nd_mapping */
		if (res->start > mapping_end)
			continue;
		if (res->end < nd_mapping->start)
			continue;

		/* space at the beginning of the mapping */
		if (!first++ && res->start > nd_mapping->start) {
			valid.start = nd_mapping->start;
			valid.end = res->start - 1;
			space_valid(nd_region, ndd, label_id, NULL, next, exist,
					to_allocate, &valid);
			available = resource_size(&valid);
			if (available)
				loc = ALLOC_BEFORE;
		}

		/* space between allocations */
		if (!loc && next) {
			valid.start = res->start + resource_size(res);
			valid.end = min(mapping_end, next->start - 1);
			space_valid(nd_region, ndd, label_id, res, next, exist,
					to_allocate, &valid);
			available = resource_size(&valid);
			if (available)
				loc = ALLOC_MID;
		}

		/* space at the end of the mapping */
		if (!loc && !next) {
			valid.start = res->start + resource_size(res);
			valid.end = mapping_end;
			space_valid(nd_region, ndd, label_id, res, next, exist,
					to_allocate, &valid);
			available = resource_size(&valid);
			if (available)
				loc = ALLOC_AFTER;
		}

		if (!loc || !available)
			continue;
		allocate = min(available, n);
		switch (loc) {
		case ALLOC_BEFORE:
			if (strcmp(res->name, label_id->id) == 0) {
				/* adjust current resource up */
				rc = adjust_resource(res, res->start - allocate,
						resource_size(res) + allocate);
				action = "cur grow up";
			} else
				action = "allocate";
			break;
		case ALLOC_MID:
			if (strcmp(next->name, label_id->id) == 0) {
				/* adjust next resource up */
				rc = adjust_resource(next, next->start
						- allocate, resource_size(next)
						+ allocate);
				new_res = next;
				action = "next grow up";
			} else if (strcmp(res->name, label_id->id) == 0) {
				action = "grow down";
			} else
				action = "allocate";
			break;
		case ALLOC_AFTER:
			if (strcmp(res->name, label_id->id) == 0)
				action = "grow down";
			else
				action = "allocate";
			break;
		default:
			return n;
		}

		if (strcmp(action, "allocate") == 0) {
			/* BLK allocate bottom up */
			if (!is_pmem)
				valid.start += available - allocate;

			new_res = nvdimm_allocate_dpa(ndd, label_id,
					valid.start, allocate);
			if (!new_res)
				rc = -EBUSY;
		} else if (strcmp(action, "grow down") == 0) {
			/* adjust current resource down */
			rc = adjust_resource(res, res->start, resource_size(res)
					+ allocate);
			if (rc == 0)
				res->flags |= DPA_RESOURCE_ADJUSTED;
		}

		if (!new_res)
			new_res = res;

		nd_dbg_dpa(nd_region, ndd, new_res, "%s(%d) %d\n",
				action, loc, rc);

		if (rc)
			return n;

		n -= allocate;
		if (n) {
			/*
			 * Retry scan with newly inserted resources.
			 * For example, if we did an ALLOC_BEFORE
			 * insertion there may also have been space
			 * available for an ALLOC_AFTER insertion, so we
			 * need to check this same resource again
			 */
			goto retry;
		} else
			return 0;
	}

	/*
	 * If we allocated nothing in the BLK case it may be because we are in
	 * an initial "pmem-reserve pass".  Only do an initial BLK allocation
	 * when none of the DPA space is reserved.
	 */
	if ((is_pmem || !ndd->dpa.child) && n == to_allocate)
		return init_dpa_allocation(label_id, nd_region, nd_mapping, n);
	return n;
}

static int merge_dpa(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, struct nd_label_id *label_id)
{
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct resource *res;

	if (strncmp("pmem", label_id->id, 4) == 0)
		return 0;
 retry:
	for_each_dpa_resource(ndd, res) {
		int rc;
		struct resource *next = res->sibling;
		resource_size_t end = res->start + resource_size(res);

		if (!next || strcmp(res->name, label_id->id) != 0
				|| strcmp(next->name, label_id->id) != 0
				|| end != next->start)
			continue;
		end += resource_size(next);
		nvdimm_free_dpa(ndd, next);
		rc = adjust_resource(res, res->start, end - res->start);
		nd_dbg_dpa(nd_region, ndd, res, "merge %d\n", rc);
		if (rc)
			return rc;
		res->flags |= DPA_RESOURCE_ADJUSTED;
		goto retry;
	}

	return 0;
}

static int __reserve_free_pmem(struct device *dev, void *data)
{
	struct nvdimm *nvdimm = data;
	struct nd_region *nd_region;
	struct nd_label_id label_id;
	int i;

	if (!is_nd_pmem(dev))
		return 0;

	nd_region = to_nd_region(dev);
	if (nd_region->ndr_mappings == 0)
		return 0;

	memset(&label_id, 0, sizeof(label_id));
	strcat(label_id.id, "pmem-reserve");
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		resource_size_t n, rem = 0;

		if (nd_mapping->nvdimm != nvdimm)
			continue;

		n = nd_pmem_available_dpa(nd_region, nd_mapping, &rem);
		if (n == 0)
			return 0;
		rem = scan_allocate(nd_region, nd_mapping, &label_id, n);
		dev_WARN_ONCE(&nd_region->dev, rem,
				"pmem reserve underrun: %#llx of %#llx bytes\n",
				(unsigned long long) n - rem,
				(unsigned long long) n);
		return rem ? -ENXIO : 0;
	}

	return 0;
}

static void release_free_pmem(struct nvdimm_bus *nvdimm_bus,
		struct nd_mapping *nd_mapping)
{
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct resource *res, *_res;

	for_each_dpa_resource_safe(ndd, res, _res)
		if (strcmp(res->name, "pmem-reserve") == 0)
			nvdimm_free_dpa(ndd, res);
}

static int reserve_free_pmem(struct nvdimm_bus *nvdimm_bus,
		struct nd_mapping *nd_mapping)
{
	struct nvdimm *nvdimm = nd_mapping->nvdimm;
	int rc;

	rc = device_for_each_child(&nvdimm_bus->dev, nvdimm,
			__reserve_free_pmem);
	if (rc)
		release_free_pmem(nvdimm_bus, nd_mapping);
	return rc;
}

/**
 * grow_dpa_allocation - for each dimm allocate n bytes for @label_id
 * @nd_region: the set of dimms to allocate @n more bytes from
 * @label_id: unique identifier for the namespace consuming this dpa range
 * @n: number of bytes per-dimm to add to the existing allocation
 *
 * Assumes resources are ordered.  For BLK regions, first consume
 * BLK-only available DPA free space, then consume PMEM-aliased DPA
 * space starting at the highest DPA.  For PMEM regions start
 * allocations from the start of an interleave set and end at the first
 * BLK allocation or the end of the interleave set, whichever comes
 * first.
 */
static int grow_dpa_allocation(struct nd_region *nd_region,
		struct nd_label_id *label_id, resource_size_t n)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(&nd_region->dev);
	bool is_pmem = strncmp(label_id->id, "pmem", 4) == 0;
	int i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		resource_size_t rem = n;
		int rc, j;

		/*
		 * In the BLK case try once with all unallocated PMEM
		 * reserved, and once without
		 */
		for (j = is_pmem; j < 2; j++) {
			bool blk_only = j == 0;

			if (blk_only) {
				rc = reserve_free_pmem(nvdimm_bus, nd_mapping);
				if (rc)
					return rc;
			}
			rem = scan_allocate(nd_region, nd_mapping,
					label_id, rem);
			if (blk_only)
				release_free_pmem(nvdimm_bus, nd_mapping);

			/* try again and allow encroachments into PMEM */
			if (rem == 0)
				break;
		}

		dev_WARN_ONCE(&nd_region->dev, rem,
				"allocation underrun: %#llx of %#llx bytes\n",
				(unsigned long long) n - rem,
				(unsigned long long) n);
		if (rem)
			return -ENXIO;

		rc = merge_dpa(nd_region, nd_mapping, label_id);
		if (rc)
			return rc;
	}

	return 0;
}

static void nd_namespace_pmem_set_resource(struct nd_region *nd_region,
		struct nd_namespace_pmem *nspm, resource_size_t size)
{
	struct resource *res = &nspm->nsio.res;
	resource_size_t offset = 0;

	if (size && !nspm->uuid) {
		WARN_ON_ONCE(1);
		size = 0;
	}

	if (size && nspm->uuid) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[0];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct nd_label_id label_id;
		struct resource *res;

		if (!ndd) {
			size = 0;
			goto out;
		}

		nd_label_gen_id(&label_id, nspm->uuid, 0);

		/* calculate a spa offset from the dpa allocation offset */
		for_each_dpa_resource(ndd, res)
			if (strcmp(res->name, label_id.id) == 0) {
				offset = (res->start - nd_mapping->start)
					* nd_region->ndr_mappings;
				goto out;
			}

		WARN_ON_ONCE(1);
		size = 0;
	}

 out:
	res->start = nd_region->ndr_start + offset;
	res->end = res->start + size - 1;
}

static bool uuid_not_set(const u8 *uuid, struct device *dev, const char *where)
{
	if (!uuid) {
		dev_dbg(dev, "%s: uuid not set\n", where);
		return true;
	}
	return false;
}

static ssize_t __size_store(struct device *dev, unsigned long long val)
{
	resource_size_t allocated = 0, available = 0;
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_namespace_common *ndns = to_ndns(dev);
	struct nd_mapping *nd_mapping;
	struct nvdimm_drvdata *ndd;
	struct nd_label_id label_id;
	u32 flags = 0, remainder;
	int rc, i, id = -1;
	u8 *uuid = NULL;

	if (dev->driver || ndns->claim)
		return -EBUSY;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		uuid = nspm->uuid;
		id = nspm->id;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		uuid = nsblk->uuid;
		flags = NSLABEL_FLAG_LOCAL;
		id = nsblk->id;
	}

	/*
	 * We need a uuid for the allocation-label and dimm(s) on which
	 * to store the label.
	 */
	if (uuid_not_set(uuid, dev, __func__))
		return -ENXIO;
	if (nd_region->ndr_mappings == 0) {
		dev_dbg(dev, "%s: not associated with dimm(s)\n", __func__);
		return -ENXIO;
	}

	div_u64_rem(val, SZ_4K * nd_region->ndr_mappings, &remainder);
	if (remainder) {
		dev_dbg(dev, "%llu is not %dK aligned\n", val,
				(SZ_4K * nd_region->ndr_mappings) / SZ_1K);
		return -EINVAL;
	}

	nd_label_gen_id(&label_id, uuid, flags);
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		nd_mapping = &nd_region->mapping[i];
		ndd = to_ndd(nd_mapping);

		/*
		 * All dimms in an interleave set, or the base dimm for a blk
		 * region, need to be enabled for the size to be changed.
		 */
		if (!ndd)
			return -ENXIO;

		allocated += nvdimm_allocated_dpa(ndd, &label_id);
	}
	available = nd_region_available_dpa(nd_region);

	if (val > available + allocated)
		return -ENOSPC;

	if (val == allocated)
		return 0;

	val = div_u64(val, nd_region->ndr_mappings);
	allocated = div_u64(allocated, nd_region->ndr_mappings);
	if (val < allocated)
		rc = shrink_dpa_allocation(nd_region, &label_id,
				allocated - val);
	else
		rc = grow_dpa_allocation(nd_region, &label_id, val - allocated);

	if (rc)
		return rc;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		nd_namespace_pmem_set_resource(nd_region, nspm,
				val * nd_region->ndr_mappings);
	}

	/*
	 * Try to delete the namespace if we deleted all of its
	 * allocation, this is not the seed or 0th device for the
	 * region, and it is not actively claimed by a btt, pfn, or dax
	 * instance.
	 */
	if (val == 0 && id != 0 && nd_region->ns_seed != dev && !ndns->claim)
		nd_device_unregister(dev, ND_ASYNC);

	return rc;
}

static ssize_t size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	unsigned long long val;
	u8 **uuid = NULL;
	int rc;

	rc = kstrtoull(buf, 0, &val);
	if (rc)
		return rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	rc = __size_store(dev, val);
	if (rc >= 0)
		rc = nd_namespace_label_update(nd_region, dev);

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		uuid = &nspm->uuid;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		uuid = &nsblk->uuid;
	}

	if (rc == 0 && val == 0 && uuid) {
		/* setting size zero == 'delete namespace' */
		kfree(*uuid);
		*uuid = NULL;
	}

	dev_dbg(dev, "%s: %llx %s (%d)\n", __func__, val, rc < 0
			? "fail" : "success", rc);

	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc < 0 ? rc : len;
}

resource_size_t __nvdimm_namespace_capacity(struct nd_namespace_common *ndns)
{
	struct device *dev = &ndns->dev;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		return resource_size(&nspm->nsio.res);
	} else if (is_namespace_blk(dev)) {
		return nd_namespace_blk_size(to_nd_namespace_blk(dev));
	} else if (is_namespace_io(dev)) {
		struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

		return resource_size(&nsio->res);
	} else
		WARN_ONCE(1, "unknown namespace type\n");
	return 0;
}

resource_size_t nvdimm_namespace_capacity(struct nd_namespace_common *ndns)
{
	resource_size_t size;

	nvdimm_bus_lock(&ndns->dev);
	size = __nvdimm_namespace_capacity(ndns);
	nvdimm_bus_unlock(&ndns->dev);

	return size;
}
EXPORT_SYMBOL(nvdimm_namespace_capacity);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", (unsigned long long)
			nvdimm_namespace_capacity(to_ndns(dev)));
}
static DEVICE_ATTR(size, 0444, size_show, size_store);

static u8 *namespace_to_uuid(struct device *dev)
{
	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		return nspm->uuid;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		return nsblk->uuid;
	} else
		return ERR_PTR(-ENXIO);
}

static ssize_t uuid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 *uuid = namespace_to_uuid(dev);

	if (IS_ERR(uuid))
		return PTR_ERR(uuid);
	if (uuid)
		return sprintf(buf, "%pUb\n", uuid);
	return sprintf(buf, "\n");
}

/**
 * namespace_update_uuid - check for a unique uuid and whether we're "renaming"
 * @nd_region: parent region so we can updates all dimms in the set
 * @dev: namespace type for generating label_id
 * @new_uuid: incoming uuid
 * @old_uuid: reference to the uuid storage location in the namespace object
 */
static int namespace_update_uuid(struct nd_region *nd_region,
		struct device *dev, u8 *new_uuid, u8 **old_uuid)
{
	u32 flags = is_namespace_blk(dev) ? NSLABEL_FLAG_LOCAL : 0;
	struct nd_label_id old_label_id;
	struct nd_label_id new_label_id;
	int i;

	if (!nd_is_uuid_unique(dev, new_uuid))
		return -EINVAL;

	if (*old_uuid == NULL)
		goto out;

	/*
	 * If we've already written a label with this uuid, then it's
	 * too late to rename because we can't reliably update the uuid
	 * without losing the old namespace.  Userspace must delete this
	 * namespace to abandon the old uuid.
	 */
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];

		/*
		 * This check by itself is sufficient because old_uuid
		 * would be NULL above if this uuid did not exist in the
		 * currently written set.
		 *
		 * FIXME: can we delete uuid with zero dpa allocated?
		 */
		if (list_empty(&nd_mapping->labels))
			return -EBUSY;
	}

	nd_label_gen_id(&old_label_id, *old_uuid, flags);
	nd_label_gen_id(&new_label_id, new_uuid, flags);
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct resource *res;

		for_each_dpa_resource(ndd, res)
			if (strcmp(res->name, old_label_id.id) == 0)
				sprintf((void *) res->name, "%s",
						new_label_id.id);
	}
	kfree(*old_uuid);
 out:
	*old_uuid = new_uuid;
	return 0;
}

static ssize_t uuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	u8 *uuid = NULL;
	ssize_t rc = 0;
	u8 **ns_uuid;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		ns_uuid = &nspm->uuid;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		ns_uuid = &nsblk->uuid;
	} else
		return -ENXIO;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	wait_nvdimm_bus_probe_idle(dev);
	if (to_ndns(dev)->claim)
		rc = -EBUSY;
	if (rc >= 0)
		rc = nd_uuid_store(dev, &uuid, buf, len);
	if (rc >= 0)
		rc = namespace_update_uuid(nd_region, dev, uuid, ns_uuid);
	if (rc >= 0)
		rc = nd_namespace_label_update(nd_region, dev);
	else
		kfree(uuid);
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc < 0 ? rc : len;
}
static DEVICE_ATTR_RW(uuid);

static ssize_t resource_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct resource *res;

	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		res = &nspm->nsio.res;
	} else if (is_namespace_io(dev)) {
		struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

		res = &nsio->res;
	} else
		return -ENXIO;

	/* no address to convey if the namespace has no allocation */
	if (resource_size(res) == 0)
		return -ENXIO;
	return sprintf(buf, "%#llx\n", (unsigned long long) res->start);
}
static DEVICE_ATTR_RO(resource);

static const unsigned long ns_lbasize_supported[] = { 512, 520, 528,
	4096, 4104, 4160, 4224, 0 };

static ssize_t sector_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

	if (!is_namespace_blk(dev))
		return -ENXIO;

	return nd_sector_size_show(nsblk->lbasize, ns_lbasize_supported, buf);
}

static ssize_t sector_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);
	struct nd_region *nd_region = to_nd_region(dev->parent);
	ssize_t rc = 0;

	if (!is_namespace_blk(dev))
		return -ENXIO;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	if (to_ndns(dev)->claim)
		rc = -EBUSY;
	if (rc >= 0)
		rc = nd_sector_size_store(dev, buf, &nsblk->lbasize,
				ns_lbasize_supported);
	if (rc >= 0)
		rc = nd_namespace_label_update(nd_region, dev);
	dev_dbg(dev, "%s: result: %zd %s: %s%s", __func__,
			rc, rc < 0 ? "tried" : "wrote", buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(sector_size);

static ssize_t dpa_extents_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_label_id label_id;
	int count = 0, i;
	u8 *uuid = NULL;
	u32 flags = 0;

	nvdimm_bus_lock(dev);
	if (is_namespace_pmem(dev)) {
		struct nd_namespace_pmem *nspm = to_nd_namespace_pmem(dev);

		uuid = nspm->uuid;
		flags = 0;
	} else if (is_namespace_blk(dev)) {
		struct nd_namespace_blk *nsblk = to_nd_namespace_blk(dev);

		uuid = nsblk->uuid;
		flags = NSLABEL_FLAG_LOCAL;
	}

	if (!uuid)
		goto out;

	nd_label_gen_id(&label_id, uuid, flags);
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct resource *res;

		for_each_dpa_resource(ndd, res)
			if (strcmp(res->name, label_id.id) == 0)
				count++;
	}
 out:
	nvdimm_bus_unlock(dev);

	return sprintf(buf, "%d\n", count);
}
static DEVICE_ATTR_RO(dpa_extents);

static ssize_t holder_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_namespace_common *ndns = to_ndns(dev);
	ssize_t rc;

	device_lock(dev);
	rc = sprintf(buf, "%s\n", ndns->claim ? dev_name(ndns->claim) : "");
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(holder);

static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_namespace_common *ndns = to_ndns(dev);
	struct device *claim;
	char *mode;
	ssize_t rc;

	device_lock(dev);
	claim = ndns->claim;
	if (claim && is_nd_btt(claim))
		mode = "safe";
	else if (claim && is_nd_pfn(claim))
		mode = "memory";
	else if (claim && is_nd_dax(claim))
		mode = "dax";
	else if (!claim && pmem_should_map_pages(dev))
		mode = "memory";
	else
		mode = "raw";
	rc = sprintf(buf, "%s\n", mode);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(mode);

static ssize_t force_raw_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	bool force_raw;
	int rc = strtobool(buf, &force_raw);

	if (rc)
		return rc;

	to_ndns(dev)->force_raw = force_raw;
	return len;
}

static ssize_t force_raw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_ndns(dev)->force_raw);
}
static DEVICE_ATTR_RW(force_raw);

static struct attribute *nd_namespace_attributes[] = {
	&dev_attr_nstype.attr,
	&dev_attr_size.attr,
	&dev_attr_mode.attr,
	&dev_attr_uuid.attr,
	&dev_attr_holder.attr,
	&dev_attr_resource.attr,
	&dev_attr_alt_name.attr,
	&dev_attr_force_raw.attr,
	&dev_attr_sector_size.attr,
	&dev_attr_dpa_extents.attr,
	NULL,
};

static umode_t namespace_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);

	if (a == &dev_attr_resource.attr) {
		if (is_namespace_blk(dev))
			return 0;
		return a->mode;
	}

	if (is_namespace_pmem(dev) || is_namespace_blk(dev)) {
		if (a == &dev_attr_size.attr)
			return 0644;

		if (is_namespace_pmem(dev) && a == &dev_attr_sector_size.attr)
			return 0;

		return a->mode;
	}

	if (a == &dev_attr_nstype.attr || a == &dev_attr_size.attr
			|| a == &dev_attr_holder.attr
			|| a == &dev_attr_force_raw.attr
			|| a == &dev_attr_mode.attr)
		return a->mode;

	return 0;
}

static struct attribute_group nd_namespace_attribute_group = {
	.attrs = nd_namespace_attributes,
	.is_visible = namespace_visible,
};

static const struct attribute_group *nd_namespace_attribute_groups[] = {
	&nd_device_attribute_group,
	&nd_namespace_attribute_group,
	&nd_numa_attribute_group,
	NULL,
};

struct nd_namespace_common *nvdimm_namespace_common_probe(struct device *dev)
{
	struct nd_btt *nd_btt = is_nd_btt(dev) ? to_nd_btt(dev) : NULL;
	struct nd_pfn *nd_pfn = is_nd_pfn(dev) ? to_nd_pfn(dev) : NULL;
	struct nd_dax *nd_dax = is_nd_dax(dev) ? to_nd_dax(dev) : NULL;
	struct nd_namespace_common *ndns = NULL;
	resource_size_t size;

	if (nd_btt || nd_pfn || nd_dax) {
		if (nd_btt)
			ndns = nd_btt->ndns;
		else if (nd_pfn)
			ndns = nd_pfn->ndns;
		else if (nd_dax)
			ndns = nd_dax->nd_pfn.ndns;

		if (!ndns)
			return ERR_PTR(-ENODEV);

		/*
		 * Flush any in-progess probes / removals in the driver
		 * for the raw personality of this namespace.
		 */
		device_lock(&ndns->dev);
		device_unlock(&ndns->dev);
		if (ndns->dev.driver) {
			dev_dbg(&ndns->dev, "is active, can't bind %s\n",
					dev_name(dev));
			return ERR_PTR(-EBUSY);
		}
		if (dev_WARN_ONCE(&ndns->dev, ndns->claim != dev,
					"host (%s) vs claim (%s) mismatch\n",
					dev_name(dev),
					dev_name(ndns->claim)))
			return ERR_PTR(-ENXIO);
	} else {
		ndns = to_ndns(dev);
		if (ndns->claim) {
			dev_dbg(dev, "claimed by %s, failing probe\n",
				dev_name(ndns->claim));

			return ERR_PTR(-ENXIO);
		}
	}

	size = nvdimm_namespace_capacity(ndns);
	if (size < ND_MIN_NAMESPACE_SIZE) {
		dev_dbg(&ndns->dev, "%pa, too small must be at least %#x\n",
				&size, ND_MIN_NAMESPACE_SIZE);
		return ERR_PTR(-ENODEV);
	}

	if (is_namespace_pmem(&ndns->dev)) {
		struct nd_namespace_pmem *nspm;

		nspm = to_nd_namespace_pmem(&ndns->dev);
		if (uuid_not_set(nspm->uuid, &ndns->dev, __func__))
			return ERR_PTR(-ENODEV);
	} else if (is_namespace_blk(&ndns->dev)) {
		struct nd_namespace_blk *nsblk;

		nsblk = to_nd_namespace_blk(&ndns->dev);
		if (uuid_not_set(nsblk->uuid, &ndns->dev, __func__))
			return ERR_PTR(-ENODEV);
		if (!nsblk->lbasize) {
			dev_dbg(&ndns->dev, "%s: sector size not set\n",
				__func__);
			return ERR_PTR(-ENODEV);
		}
		if (!nd_namespace_blk_validate(nsblk))
			return ERR_PTR(-ENODEV);
	}

	return ndns;
}
EXPORT_SYMBOL(nvdimm_namespace_common_probe);

static struct device **create_namespace_io(struct nd_region *nd_region)
{
	struct nd_namespace_io *nsio;
	struct device *dev, **devs;
	struct resource *res;

	nsio = kzalloc(sizeof(*nsio), GFP_KERNEL);
	if (!nsio)
		return NULL;

	devs = kcalloc(2, sizeof(struct device *), GFP_KERNEL);
	if (!devs) {
		kfree(nsio);
		return NULL;
	}

	dev = &nsio->common.dev;
	dev->type = &namespace_io_device_type;
	dev->parent = &nd_region->dev;
	res = &nsio->res;
	res->name = dev_name(&nd_region->dev);
	res->flags = IORESOURCE_MEM;
	res->start = nd_region->ndr_start;
	res->end = res->start + nd_region->ndr_size - 1;

	devs[0] = dev;
	return devs;
}

static bool has_uuid_at_pos(struct nd_region *nd_region, u8 *uuid,
		u64 cookie, u16 pos)
{
	struct nd_namespace_label *found = NULL;
	int i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nd_label_ent *label_ent;
		bool found_uuid = false;

		list_for_each_entry(label_ent, &nd_mapping->labels, list) {
			struct nd_namespace_label *nd_label = label_ent->label;
			u16 position, nlabel;
			u64 isetcookie;

			if (!nd_label)
				continue;
			isetcookie = __le64_to_cpu(nd_label->isetcookie);
			position = __le16_to_cpu(nd_label->position);
			nlabel = __le16_to_cpu(nd_label->nlabel);

			if (isetcookie != cookie)
				continue;

			if (memcmp(nd_label->uuid, uuid, NSLABEL_UUID_LEN) != 0)
				continue;

			if (found_uuid) {
				dev_dbg(to_ndd(nd_mapping)->dev,
						"%s duplicate entry for uuid\n",
						__func__);
				return false;
			}
			found_uuid = true;
			if (nlabel != nd_region->ndr_mappings)
				continue;
			if (position != pos)
				continue;
			found = nd_label;
			break;
		}
		if (found)
			break;
	}
	return found != NULL;
}

static int select_pmem_id(struct nd_region *nd_region, u8 *pmem_id)
{
	int i;

	if (!pmem_id)
		return -ENODEV;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct nd_namespace_label *nd_label = NULL;
		u64 hw_start, hw_end, pmem_start, pmem_end;
		struct nd_label_ent *label_ent;

		lockdep_assert_held(&nd_mapping->lock);
		list_for_each_entry(label_ent, &nd_mapping->labels, list) {
			nd_label = label_ent->label;
			if (!nd_label)
				continue;
			if (memcmp(nd_label->uuid, pmem_id, NSLABEL_UUID_LEN) == 0)
				break;
			nd_label = NULL;
		}

		if (!nd_label) {
			WARN_ON(1);
			return -EINVAL;
		}

		/*
		 * Check that this label is compliant with the dpa
		 * range published in NFIT
		 */
		hw_start = nd_mapping->start;
		hw_end = hw_start + nd_mapping->size;
		pmem_start = __le64_to_cpu(nd_label->dpa);
		pmem_end = pmem_start + __le64_to_cpu(nd_label->rawsize);
		if (pmem_start >= hw_start && pmem_start < hw_end
				&& pmem_end <= hw_end && pmem_end > hw_start)
			/* pass */;
		else {
			dev_dbg(&nd_region->dev, "%s invalid label for %pUb\n",
					dev_name(ndd->dev), nd_label->uuid);
			return -EINVAL;
		}

		/* move recently validated label to the front of the list */
		list_move(&label_ent->list, &nd_mapping->labels);
	}
	return 0;
}

/**
 * create_namespace_pmem - validate interleave set labelling, retrieve label0
 * @nd_region: region with mappings to validate
 * @nspm: target namespace to create
 * @nd_label: target pmem namespace label to evaluate
 */
struct device *create_namespace_pmem(struct nd_region *nd_region,
		struct nd_namespace_label *nd_label)
{
	u64 altcookie = nd_region_interleave_set_altcookie(nd_region);
	u64 cookie = nd_region_interleave_set_cookie(nd_region);
	struct nd_label_ent *label_ent;
	struct nd_namespace_pmem *nspm;
	struct nd_mapping *nd_mapping;
	resource_size_t size = 0;
	struct resource *res;
	struct device *dev;
	int rc = 0;
	u16 i;

	if (cookie == 0) {
		dev_dbg(&nd_region->dev, "invalid interleave-set-cookie\n");
		return ERR_PTR(-ENXIO);
	}

	if (__le64_to_cpu(nd_label->isetcookie) != cookie) {
		dev_dbg(&nd_region->dev, "invalid cookie in label: %pUb\n",
				nd_label->uuid);
		if (__le64_to_cpu(nd_label->isetcookie) != altcookie)
			return ERR_PTR(-EAGAIN);

		dev_dbg(&nd_region->dev, "valid altcookie in label: %pUb\n",
				nd_label->uuid);
	}

	nspm = kzalloc(sizeof(*nspm), GFP_KERNEL);
	if (!nspm)
		return ERR_PTR(-ENOMEM);

	nspm->id = -1;
	dev = &nspm->nsio.common.dev;
	dev->type = &namespace_pmem_device_type;
	dev->parent = &nd_region->dev;
	res = &nspm->nsio.res;
	res->name = dev_name(&nd_region->dev);
	res->flags = IORESOURCE_MEM;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		if (has_uuid_at_pos(nd_region, nd_label->uuid, cookie, i))
			continue;
		if (has_uuid_at_pos(nd_region, nd_label->uuid, altcookie, i))
			continue;
		break;
	}

	if (i < nd_region->ndr_mappings) {
		struct nvdimm_drvdata *ndd = to_ndd(&nd_region->mapping[i]);

		/*
		 * Give up if we don't find an instance of a uuid at each
		 * position (from 0 to nd_region->ndr_mappings - 1), or if we
		 * find a dimm with two instances of the same uuid.
		 */
		dev_err(&nd_region->dev, "%s missing label for %pUb\n",
				dev_name(ndd->dev), nd_label->uuid);
		rc = -EINVAL;
		goto err;
	}

	/*
	 * Fix up each mapping's 'labels' to have the validated pmem label for
	 * that position at labels[0], and NULL at labels[1].  In the process,
	 * check that the namespace aligns with interleave-set.  We know
	 * that it does not overlap with any blk namespaces by virtue of
	 * the dimm being enabled (i.e. nd_label_reserve_dpa()
	 * succeeded).
	 */
	rc = select_pmem_id(nd_region, nd_label->uuid);
	if (rc)
		goto err;

	/* Calculate total size and populate namespace properties from label0 */
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_namespace_label *label0;

		nd_mapping = &nd_region->mapping[i];
		label_ent = list_first_entry_or_null(&nd_mapping->labels,
				typeof(*label_ent), list);
		label0 = label_ent ? label_ent->label : 0;

		if (!label0) {
			WARN_ON(1);
			continue;
		}

		size += __le64_to_cpu(label0->rawsize);
		if (__le16_to_cpu(label0->position) != 0)
			continue;
		WARN_ON(nspm->alt_name || nspm->uuid);
		nspm->alt_name = kmemdup((void __force *) label0->name,
				NSLABEL_NAME_LEN, GFP_KERNEL);
		nspm->uuid = kmemdup((void __force *) label0->uuid,
				NSLABEL_UUID_LEN, GFP_KERNEL);
	}

	if (!nspm->alt_name || !nspm->uuid) {
		rc = -ENOMEM;
		goto err;
	}

	nd_namespace_pmem_set_resource(nd_region, nspm, size);

	return dev;
 err:
	namespace_pmem_release(dev);
	switch (rc) {
	case -EINVAL:
		dev_dbg(&nd_region->dev, "%s: invalid label(s)\n", __func__);
		break;
	case -ENODEV:
		dev_dbg(&nd_region->dev, "%s: label not found\n", __func__);
		break;
	default:
		dev_dbg(&nd_region->dev, "%s: unexpected err: %d\n",
				__func__, rc);
		break;
	}
	return ERR_PTR(rc);
}

struct resource *nsblk_add_resource(struct nd_region *nd_region,
		struct nvdimm_drvdata *ndd, struct nd_namespace_blk *nsblk,
		resource_size_t start)
{
	struct nd_label_id label_id;
	struct resource *res;

	nd_label_gen_id(&label_id, nsblk->uuid, NSLABEL_FLAG_LOCAL);
	res = krealloc(nsblk->res,
			sizeof(void *) * (nsblk->num_resources + 1),
			GFP_KERNEL);
	if (!res)
		return NULL;
	nsblk->res = (struct resource **) res;
	for_each_dpa_resource(ndd, res)
		if (strcmp(res->name, label_id.id) == 0
				&& res->start == start) {
			nsblk->res[nsblk->num_resources++] = res;
			return res;
		}
	return NULL;
}

static struct device *nd_namespace_blk_create(struct nd_region *nd_region)
{
	struct nd_namespace_blk *nsblk;
	struct device *dev;

	if (!is_nd_blk(&nd_region->dev))
		return NULL;

	nsblk = kzalloc(sizeof(*nsblk), GFP_KERNEL);
	if (!nsblk)
		return NULL;

	dev = &nsblk->common.dev;
	dev->type = &namespace_blk_device_type;
	nsblk->id = ida_simple_get(&nd_region->ns_ida, 0, 0, GFP_KERNEL);
	if (nsblk->id < 0) {
		kfree(nsblk);
		return NULL;
	}
	dev_set_name(dev, "namespace%d.%d", nd_region->id, nsblk->id);
	dev->parent = &nd_region->dev;
	dev->groups = nd_namespace_attribute_groups;

	return &nsblk->common.dev;
}

static struct device *nd_namespace_pmem_create(struct nd_region *nd_region)
{
	struct nd_namespace_pmem *nspm;
	struct resource *res;
	struct device *dev;

	if (!is_nd_pmem(&nd_region->dev))
		return NULL;

	nspm = kzalloc(sizeof(*nspm), GFP_KERNEL);
	if (!nspm)
		return NULL;

	dev = &nspm->nsio.common.dev;
	dev->type = &namespace_pmem_device_type;
	dev->parent = &nd_region->dev;
	res = &nspm->nsio.res;
	res->name = dev_name(&nd_region->dev);
	res->flags = IORESOURCE_MEM;

	nspm->id = ida_simple_get(&nd_region->ns_ida, 0, 0, GFP_KERNEL);
	if (nspm->id < 0) {
		kfree(nspm);
		return NULL;
	}
	dev_set_name(dev, "namespace%d.%d", nd_region->id, nspm->id);
	dev->parent = &nd_region->dev;
	dev->groups = nd_namespace_attribute_groups;
	nd_namespace_pmem_set_resource(nd_region, nspm, 0);

	return dev;
}

void nd_region_create_ns_seed(struct nd_region *nd_region)
{
	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));

	if (nd_region_to_nstype(nd_region) == ND_DEVICE_NAMESPACE_IO)
		return;

	if (is_nd_blk(&nd_region->dev))
		nd_region->ns_seed = nd_namespace_blk_create(nd_region);
	else
		nd_region->ns_seed = nd_namespace_pmem_create(nd_region);

	/*
	 * Seed creation failures are not fatal, provisioning is simply
	 * disabled until memory becomes available
	 */
	if (!nd_region->ns_seed)
		dev_err(&nd_region->dev, "failed to create %s namespace\n",
				is_nd_blk(&nd_region->dev) ? "blk" : "pmem");
	else
		nd_device_register(nd_region->ns_seed);
}

void nd_region_create_dax_seed(struct nd_region *nd_region)
{
	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));
	nd_region->dax_seed = nd_dax_create(nd_region);
	/*
	 * Seed creation failures are not fatal, provisioning is simply
	 * disabled until memory becomes available
	 */
	if (!nd_region->dax_seed)
		dev_err(&nd_region->dev, "failed to create dax namespace\n");
}

void nd_region_create_pfn_seed(struct nd_region *nd_region)
{
	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));
	nd_region->pfn_seed = nd_pfn_create(nd_region);
	/*
	 * Seed creation failures are not fatal, provisioning is simply
	 * disabled until memory becomes available
	 */
	if (!nd_region->pfn_seed)
		dev_err(&nd_region->dev, "failed to create pfn namespace\n");
}

void nd_region_create_btt_seed(struct nd_region *nd_region)
{
	WARN_ON(!is_nvdimm_bus_locked(&nd_region->dev));
	nd_region->btt_seed = nd_btt_create(nd_region);
	/*
	 * Seed creation failures are not fatal, provisioning is simply
	 * disabled until memory becomes available
	 */
	if (!nd_region->btt_seed)
		dev_err(&nd_region->dev, "failed to create btt namespace\n");
}

static int add_namespace_resource(struct nd_region *nd_region,
		struct nd_namespace_label *nd_label, struct device **devs,
		int count)
{
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	int i;

	for (i = 0; i < count; i++) {
		u8 *uuid = namespace_to_uuid(devs[i]);
		struct resource *res;

		if (IS_ERR_OR_NULL(uuid)) {
			WARN_ON(1);
			continue;
		}

		if (memcmp(uuid, nd_label->uuid, NSLABEL_UUID_LEN) != 0)
			continue;
		if (is_namespace_blk(devs[i])) {
			res = nsblk_add_resource(nd_region, ndd,
					to_nd_namespace_blk(devs[i]),
					__le64_to_cpu(nd_label->dpa));
			if (!res)
				return -ENXIO;
			nd_dbg_dpa(nd_region, ndd, res, "%d assign\n", count);
		} else {
			dev_err(&nd_region->dev,
					"error: conflicting extents for uuid: %pUb\n",
					nd_label->uuid);
			return -ENXIO;
		}
		break;
	}

	return i;
}

struct device *create_namespace_blk(struct nd_region *nd_region,
		struct nd_namespace_label *nd_label, int count)
{

	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_namespace_blk *nsblk;
	char name[NSLABEL_NAME_LEN];
	struct device *dev = NULL;
	struct resource *res;

	nsblk = kzalloc(sizeof(*nsblk), GFP_KERNEL);
	if (!nsblk)
		return ERR_PTR(-ENOMEM);
	dev = &nsblk->common.dev;
	dev->type = &namespace_blk_device_type;
	dev->parent = &nd_region->dev;
	nsblk->id = -1;
	nsblk->lbasize = __le64_to_cpu(nd_label->lbasize);
	nsblk->uuid = kmemdup(nd_label->uuid, NSLABEL_UUID_LEN,
			GFP_KERNEL);
	if (!nsblk->uuid)
		goto blk_err;
	memcpy(name, nd_label->name, NSLABEL_NAME_LEN);
	if (name[0])
		nsblk->alt_name = kmemdup(name, NSLABEL_NAME_LEN,
				GFP_KERNEL);
	res = nsblk_add_resource(nd_region, ndd, nsblk,
			__le64_to_cpu(nd_label->dpa));
	if (!res)
		goto blk_err;
	nd_dbg_dpa(nd_region, ndd, res, "%d: assign\n", count);
	return dev;
 blk_err:
	namespace_blk_release(dev);
	return ERR_PTR(-ENXIO);
}

static int cmp_dpa(const void *a, const void *b)
{
	const struct device *dev_a = *(const struct device **) a;
	const struct device *dev_b = *(const struct device **) b;
	struct nd_namespace_blk *nsblk_a, *nsblk_b;
	struct nd_namespace_pmem *nspm_a, *nspm_b;

	if (is_namespace_io(dev_a))
		return 0;

	if (is_namespace_blk(dev_a)) {
		nsblk_a = to_nd_namespace_blk(dev_a);
		nsblk_b = to_nd_namespace_blk(dev_b);

		return memcmp(&nsblk_a->res[0]->start, &nsblk_b->res[0]->start,
				sizeof(resource_size_t));
	}

	nspm_a = to_nd_namespace_pmem(dev_a);
	nspm_b = to_nd_namespace_pmem(dev_b);

	return memcmp(&nspm_a->nsio.res.start, &nspm_b->nsio.res.start,
			sizeof(resource_size_t));
}

static struct device **scan_labels(struct nd_region *nd_region)
{
	int i, count = 0;
	struct device *dev, **devs = NULL;
	struct nd_label_ent *label_ent, *e;
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	resource_size_t map_end = nd_mapping->start + nd_mapping->size - 1;

	/* "safe" because create_namespace_pmem() might list_move() label_ent */
	list_for_each_entry_safe(label_ent, e, &nd_mapping->labels, list) {
		struct nd_namespace_label *nd_label = label_ent->label;
		struct device **__devs;
		u32 flags;

		if (!nd_label)
			continue;
		flags = __le32_to_cpu(nd_label->flags);
		if (is_nd_blk(&nd_region->dev)
				== !!(flags & NSLABEL_FLAG_LOCAL))
			/* pass, region matches label type */;
		else
			continue;

		/* skip labels that describe extents outside of the region */
		if (nd_label->dpa < nd_mapping->start || nd_label->dpa > map_end)
			continue;

		i = add_namespace_resource(nd_region, nd_label, devs, count);
		if (i < 0)
			goto err;
		if (i < count)
			continue;
		__devs = kcalloc(count + 2, sizeof(dev), GFP_KERNEL);
		if (!__devs)
			goto err;
		memcpy(__devs, devs, sizeof(dev) * count);
		kfree(devs);
		devs = __devs;

		if (is_nd_blk(&nd_region->dev)) {
			dev = create_namespace_blk(nd_region, nd_label, count);
			if (IS_ERR(dev))
				goto err;
			devs[count++] = dev;
		} else {
			dev = create_namespace_pmem(nd_region, nd_label);
			if (IS_ERR(dev)) {
				switch (PTR_ERR(dev)) {
				case -EAGAIN:
					/* skip invalid labels */
					continue;
				case -ENODEV:
					/* fallthrough to seed creation */
					break;
				default:
					goto err;
				}
			} else
				devs[count++] = dev;
		}
	}

	dev_dbg(&nd_region->dev, "%s: discovered %d %s namespace%s\n",
			__func__, count, is_nd_blk(&nd_region->dev)
			? "blk" : "pmem", count == 1 ? "" : "s");

	if (count == 0) {
		/* Publish a zero-sized namespace for userspace to configure. */
		nd_mapping_free_labels(nd_mapping);

		devs = kcalloc(2, sizeof(dev), GFP_KERNEL);
		if (!devs)
			goto err;
		if (is_nd_blk(&nd_region->dev)) {
			struct nd_namespace_blk *nsblk;

			nsblk = kzalloc(sizeof(*nsblk), GFP_KERNEL);
			if (!nsblk)
				goto err;
			dev = &nsblk->common.dev;
			dev->type = &namespace_blk_device_type;
		} else {
			struct nd_namespace_pmem *nspm;

			nspm = kzalloc(sizeof(*nspm), GFP_KERNEL);
			if (!nspm)
				goto err;
			dev = &nspm->nsio.common.dev;
			dev->type = &namespace_pmem_device_type;
			nd_namespace_pmem_set_resource(nd_region, nspm, 0);
		}
		dev->parent = &nd_region->dev;
		devs[count++] = dev;
	} else if (is_nd_pmem(&nd_region->dev)) {
		/* clean unselected labels */
		for (i = 0; i < nd_region->ndr_mappings; i++) {
			struct list_head *l, *e;
			LIST_HEAD(list);
			int j;

			nd_mapping = &nd_region->mapping[i];
			if (list_empty(&nd_mapping->labels)) {
				WARN_ON(1);
				continue;
			}

			j = count;
			list_for_each_safe(l, e, &nd_mapping->labels) {
				if (!j--)
					break;
				list_move_tail(l, &list);
			}
			nd_mapping_free_labels(nd_mapping);
			list_splice_init(&list, &nd_mapping->labels);
		}
	}

	if (count > 1)
		sort(devs, count, sizeof(struct device *), cmp_dpa, NULL);

	return devs;

 err:
	if (devs) {
		for (i = 0; devs[i]; i++)
			if (is_nd_blk(&nd_region->dev))
				namespace_blk_release(devs[i]);
			else
				namespace_pmem_release(devs[i]);
		kfree(devs);
	}
	return NULL;
}

static struct device **create_namespaces(struct nd_region *nd_region)
{
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct device **devs;
	int i;

	if (nd_region->ndr_mappings == 0)
		return NULL;

	/* lock down all mappings while we scan labels */
	for (i = 0; i < nd_region->ndr_mappings; i++) {
		nd_mapping = &nd_region->mapping[i];
		mutex_lock_nested(&nd_mapping->lock, i);
	}

	devs = scan_labels(nd_region);

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		int reverse = nd_region->ndr_mappings - 1 - i;

		nd_mapping = &nd_region->mapping[reverse];
		mutex_unlock(&nd_mapping->lock);
	}

	return devs;
}

static int init_active_labels(struct nd_region *nd_region)
{
	int i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct nvdimm *nvdimm = nd_mapping->nvdimm;
		struct nd_label_ent *label_ent;
		int count, j;

		/*
		 * If the dimm is disabled then prevent the region from
		 * being activated if it aliases DPA.
		 */
		if (!ndd) {
			if ((nvdimm->flags & NDD_ALIASING) == 0)
				return 0;
			dev_dbg(&nd_region->dev, "%s: is disabled, failing probe\n",
					dev_name(&nd_mapping->nvdimm->dev));
			return -ENXIO;
		}
		nd_mapping->ndd = ndd;
		atomic_inc(&nvdimm->busy);
		get_ndd(ndd);

		count = nd_label_active_count(ndd);
		dev_dbg(ndd->dev, "%s: %d\n", __func__, count);
		if (!count)
			continue;
		for (j = 0; j < count; j++) {
			struct nd_namespace_label *label;

			label_ent = kzalloc(sizeof(*label_ent), GFP_KERNEL);
			if (!label_ent)
				break;
			label = nd_label_active(ndd, j);
			label_ent->label = label;

			mutex_lock(&nd_mapping->lock);
			list_add_tail(&label_ent->list, &nd_mapping->labels);
			mutex_unlock(&nd_mapping->lock);
		}

		if (j >= count)
			continue;

		mutex_lock(&nd_mapping->lock);
		nd_mapping_free_labels(nd_mapping);
		mutex_unlock(&nd_mapping->lock);
		return -ENOMEM;
	}

	return 0;
}

int nd_region_register_namespaces(struct nd_region *nd_region, int *err)
{
	struct device **devs = NULL;
	int i, rc = 0, type;

	*err = 0;
	nvdimm_bus_lock(&nd_region->dev);
	rc = init_active_labels(nd_region);
	if (rc) {
		nvdimm_bus_unlock(&nd_region->dev);
		return rc;
	}

	type = nd_region_to_nstype(nd_region);
	switch (type) {
	case ND_DEVICE_NAMESPACE_IO:
		devs = create_namespace_io(nd_region);
		break;
	case ND_DEVICE_NAMESPACE_PMEM:
	case ND_DEVICE_NAMESPACE_BLK:
		devs = create_namespaces(nd_region);
		break;
	default:
		break;
	}
	nvdimm_bus_unlock(&nd_region->dev);

	if (!devs)
		return -ENODEV;

	for (i = 0; devs[i]; i++) {
		struct device *dev = devs[i];
		int id;

		if (type == ND_DEVICE_NAMESPACE_BLK) {
			struct nd_namespace_blk *nsblk;

			nsblk = to_nd_namespace_blk(dev);
			id = ida_simple_get(&nd_region->ns_ida, 0, 0,
					GFP_KERNEL);
			nsblk->id = id;
		} else if (type == ND_DEVICE_NAMESPACE_PMEM) {
			struct nd_namespace_pmem *nspm;

			nspm = to_nd_namespace_pmem(dev);
			id = ida_simple_get(&nd_region->ns_ida, 0, 0,
					GFP_KERNEL);
			nspm->id = id;
		} else
			id = i;

		if (id < 0)
			break;
		dev_set_name(dev, "namespace%d.%d", nd_region->id, id);
		dev->groups = nd_namespace_attribute_groups;
		nd_device_register(dev);
	}
	if (i)
		nd_region->ns_seed = devs[0];

	if (devs[i]) {
		int j;

		for (j = i; devs[j]; j++) {
			struct device *dev = devs[j];

			device_initialize(dev);
			put_device(dev);
		}
		*err = j - i;
		/*
		 * All of the namespaces we tried to register failed, so
		 * fail region activation.
		 */
		if (*err == 0)
			rc = -ENODEV;
	}
	kfree(devs);

	if (rc == -ENODEV)
		return rc;

	return i;
}
