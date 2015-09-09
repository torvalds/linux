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
#include "pfn.h"
#include "btt.h"
#include "nd.h"

void __nd_detach_ndns(struct device *dev, struct nd_namespace_common **_ndns)
{
	struct nd_namespace_common *ndns = *_ndns;

	dev_WARN_ONCE(dev, !mutex_is_locked(&ndns->dev.mutex)
			|| ndns->claim != dev,
			"%s: invalid claim\n", __func__);
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
	device_lock(&ndns->dev);
	__nd_detach_ndns(dev, _ndns);
	device_unlock(&ndns->dev);
	put_device(&ndns->dev);
}

bool __nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns)
{
	if (attach->claim)
		return false;
	dev_WARN_ONCE(dev, !mutex_is_locked(&attach->dev.mutex)
			|| *_ndns,
			"%s: invalid claim\n", __func__);
	attach->claim = dev;
	*_ndns = attach;
	get_device(&attach->dev);
	return true;
}

bool nd_attach_ndns(struct device *dev, struct nd_namespace_common *attach,
		struct nd_namespace_common **_ndns)
{
	bool claimed;

	device_lock(&attach->dev);
	claimed = __nd_attach_ndns(dev, attach, _ndns);
	device_unlock(&attach->dev);
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

	if (seed == dev || ndns || dev->driver)
		return false;
	return true;
}

static void nd_detach_and_reset(struct device *dev,
		struct nd_namespace_common **_ndns)
{
	/* detach the namespace and destroy / reset the device */
	nd_detach_ndns(dev, _ndns);
	if (is_idle(dev, *_ndns)) {
		nd_device_unregister(dev, ND_ASYNC);
	} else if (is_nd_btt(dev)) {
		struct nd_btt *nd_btt = to_nd_btt(dev);

		nd_btt->lbasize = 0;
		kfree(nd_btt->uuid);
		nd_btt->uuid = NULL;
	} else if (is_nd_pfn(dev)) {
		struct nd_pfn *nd_pfn = to_nd_pfn(dev);

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
	if (__nvdimm_namespace_capacity(ndns) < SZ_16M) {
		dev_dbg(dev, "%s too small to host\n", name);
		len = -ENXIO;
		goto out_attach;
	}

	WARN_ON_ONCE(!is_nvdimm_bus_locked(dev));
	if (!nd_attach_ndns(dev, ndns, _ndns)) {
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
