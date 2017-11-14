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
#include <linux/device.h>
#include <linux/sizes.h>
#include "nd-core.h"
#include "pmem.h"
#include "pfn.h"
#include "btt.h"
#include "nd.h"

void __nd_detach_ndns(struct device *dev, struct nd_namespace_common **_ndns)
{
	struct nd_namespace_common *ndns = *_ndns;
	struct nvdimm_bus *nvdimm_bus;

	if (!ndns)
		return;

	nvdimm_bus = walk_to_nvdimm_bus(&ndns->dev);
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);
	dev_WARN_ONCE(dev, ndns->claim != dev, "%s: invalid claim\n", __func__);
	ndns->claim = NULL;
	*_ndns = NULL;
	put_device(&ndns->dev);
}

void nd_detach_ndns(struct device *dev,
		struct nd_namespace_common **_ndns)
{
	struct nd_namespace_common *ndns = *_ndns;

	if (!ndns)
		return;
	get_device(&ndns->dev);
	nvdimm_bus_lock(&ndns->dev);
	__nd_detach_ndns(dev, _ndns);
	nvdimm_bus_unlock(&ndns->dev);
	put_device(&ndns->dev);
}

bool __nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(&attach->dev);

	if (attach->claim)
		return false;
	lockdep_assert_held(&nvdimm_bus->reconfig_mutex);
	dev_WARN_ONCE(dev, *_ndns, "%s: invalid claim\n", __func__);
	attach->claim = dev;
	*_ndns = attach;
	get_device(&attach->dev);
	return true;
}

bool nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns)
{
	bool claimed;

	nvdimm_bus_lock(&attach->dev);
	claimed = __nd_attach_ndns(dev, attach, _ndns);
	nvdimm_bus_unlock(&attach->dev);
	return claimed;
}

static int namespace_match(struct device *dev, void *data)
{
	char *name = data;

	return strcmp(name, dev_name(dev)) == 0;
}

static bool is_idle(struct device *dev, struct nd_namespace_common *ndns)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct device *seed = NULL;

	if (is_nd_btt(dev))
		seed = nd_region->btt_seed;
	else if (is_nd_pfn(dev))
		seed = nd_region->pfn_seed;
	else if (is_nd_dax(dev))
		seed = nd_region->dax_seed;

	if (seed == dev || ndns || dev->driver)
		return false;
	return true;
}

struct nd_pfn *to_nd_pfn_safe(struct device *dev)
{
	/*
	 * pfn device attributes are re-used by dax device instances, so we
	 * need to be careful to correct device-to-nd_pfn conversion.
	 */
	if (is_nd_pfn(dev))
		return to_nd_pfn(dev);

	if (is_nd_dax(dev)) {
		struct nd_dax *nd_dax = to_nd_dax(dev);

		return &nd_dax->nd_pfn;
	}

	WARN_ON(1);
	return NULL;
}

static void nd_detach_and_reset(struct device *dev,
		struct nd_namespace_common **_ndns)
{
	/* detach the namespace and destroy / reset the device */
	__nd_detach_ndns(dev, _ndns);
	if (is_idle(dev, *_ndns)) {
		nd_device_unregister(dev, ND_ASYNC);
	} else if (is_nd_btt(dev)) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		nd_btt->lbasize = 0;
		kfree(nd_btt->uuid);
		nd_btt->uuid = NULL;
	} else if (is_nd_pfn(dev) || is_nd_dax(dev)) {
		struct nd_pfn *nd_pfn = to_nd_pfn_safe(dev);

		kfree(nd_pfn->uuid);
		nd_pfn->uuid = NULL;
		nd_pfn->mode = PFN_MODE_NONE;
	}
}

ssize_t nd_namespace_store(struct device *dev,
		struct nd_namespace_common **_ndns, const char *buf,
		size_t len)
{
	struct nd_namespace_common *ndns;
	struct device *found;
	char *name;

	if (dev->driver) {
		dev_dbg(dev, "%s: -EBUSY\n", __func__);
		return -EBUSY;
	}

	name = kstrndup(buf, len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	strim(name);

	if (strncmp(name, "namespace", 9) == 0 || strcmp(name, "") == 0)
		/* pass */;
	else {
		len = -EINVAL;
		goto out;
	}

	ndns = *_ndns;
	if (strcmp(name, "") == 0) {
		nd_detach_and_reset(dev, _ndns);
		goto out;
	} else if (ndns) {
		dev_dbg(dev, "namespace already set to: %s\n",
				dev_name(&ndns->dev));
		len = -EBUSY;
		goto out;
	}

	found = device_find_child(dev->parent, name, namespace_match);
	if (!found) {
		dev_dbg(dev, "'%s' not found under %s\n", name,
				dev_name(dev->parent));
		len = -ENODEV;
		goto out;
	}

	ndns = to_ndns(found);

	switch (ndns->claim_class) {
	case NVDIMM_CCLASS_NONE:
		break;
	case NVDIMM_CCLASS_BTT:
	case NVDIMM_CCLASS_BTT2:
		if (!is_nd_btt(dev)) {
			len = -EBUSY;
			goto out_attach;
		}
		break;
	case NVDIMM_CCLASS_PFN:
		if (!is_nd_pfn(dev)) {
			len = -EBUSY;
			goto out_attach;
		}
		break;
	case NVDIMM_CCLASS_DAX:
		if (!is_nd_dax(dev)) {
			len = -EBUSY;
			goto out_attach;
		}
		break;
	default:
		len = -EBUSY;
		goto out_attach;
		break;
	}

	if (__nvdimm_namespace_capacity(ndns) < SZ_16M) {
		dev_dbg(dev, "%s too small to host\n", name);
		len = -ENXIO;
		goto out_attach;
	}

	WARN_ON_ONCE(!is_nvdimm_bus_locked(dev));
	if (!__nd_attach_ndns(dev, ndns, _ndns)) {
		dev_dbg(dev, "%s already claimed\n",
				dev_name(&ndns->dev));
		len = -EBUSY;
	}

 out_attach:
	put_device(&ndns->dev); /* from device_find_child */
 out:
	kfree(name);
	return len;
}

/*
 * nd_sb_checksum: compute checksum for a generic info block
 *
 * Returns a fletcher64 checksum of everything in the given info block
 * except the last field (since that's where the checksum lives).
 */
u64 nd_sb_checksum(struct nd_gen_sb *nd_gen_sb)
{
	u64 sum;
	__le64 sum_save;

	BUILD_BUG_ON(sizeof(struct btt_sb) != SZ_4K);
	BUILD_BUG_ON(sizeof(struct nd_pfn_sb) != SZ_4K);
	BUILD_BUG_ON(sizeof(struct nd_gen_sb) != SZ_4K);

	sum_save = nd_gen_sb->checksum;
	nd_gen_sb->checksum = 0;
	sum = nd_fletcher64(nd_gen_sb, sizeof(*nd_gen_sb), 1);
	nd_gen_sb->checksum = sum_save;
	return sum;
}
EXPORT_SYMBOL(nd_sb_checksum);

static int nsio_rw_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *buf, size_t size, int rw,
		unsigned long flags)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	unsigned int sz_align = ALIGN(size + (offset & (512 - 1)), 512);
	sector_t sector = offset >> 9;
	int rc = 0;

	if (unlikely(!size))
		return 0;

	if (unlikely(offset + size > nsio->size)) {
		dev_WARN_ONCE(&ndns->dev, 1, "request out of range\n");
		return -EFAULT;
	}

	if (rw == READ) {
		if (unlikely(is_bad_pmem(&nsio->bb, sector, sz_align)))
			return -EIO;
		return memcpy_mcsafe(buf, nsio->addr + offset, size);
	}

	if (unlikely(is_bad_pmem(&nsio->bb, sector, sz_align))) {
		if (IS_ALIGNED(offset, 512) && IS_ALIGNED(size, 512)
				&& !(flags & NVDIMM_IO_ATOMIC)) {
			long cleared;

			might_sleep();
			cleared = nvdimm_clear_poison(&ndns->dev,
					nsio->res.start + offset, size);
			if (cleared < size)
				rc = -EIO;
			if (cleared > 0 && cleared / 512) {
				cleared /= 512;
				badblocks_clear(&nsio->bb, sector, cleared);
			}
			arch_invalidate_pmem(nsio->addr + offset, size);
		} else
			rc = -EIO;
	}

	memcpy_flushcache(nsio->addr + offset, buf, size);
	nvdimm_flush(to_nd_region(ndns->dev.parent));

	return rc;
}

int devm_nsio_enable(struct device *dev, struct nd_namespace_io *nsio)
{
	struct resource *res = &nsio->res;
	struct nd_namespace_common *ndns = &nsio->common;

	nsio->size = resource_size(res);
	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				dev_name(&ndns->dev))) {
		dev_warn(dev, "could not reserve region %pR\n", res);
		return -EBUSY;
	}

	ndns->rw_bytes = nsio_rw_bytes;
	if (devm_init_badblocks(dev, &nsio->bb))
		return -ENOMEM;
	nvdimm_badblocks_populate(to_nd_region(ndns->dev.parent), &nsio->bb,
			&nsio->res);

	nsio->addr = devm_memremap(dev, res->start, resource_size(res),
			ARCH_MEMREMAP_PMEM);

	return PTR_ERR_OR_ZERO(nsio->addr);
}
EXPORT_SYMBOL_GPL(devm_nsio_enable);

void devm_nsio_disable(struct device *dev, struct nd_namespace_io *nsio)
{
	struct resource *res = &nsio->res;

	devm_memunmap(dev, nsio->addr);
	devm_exit_badblocks(dev, &nsio->bb);
	devm_release_mem_region(dev, res->start, resource_size(res));
}
EXPORT_SYMBOL_GPL(devm_nsio_disable);
