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
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "nd-core.h"
#include "pfn.h"
#include "nd.h"

static void nd_pfn_release(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);

	dev_dbg(dev, "%s\n", __func__);
	nd_detach_ndns(&nd_pfn->dev, &nd_pfn->ndns);
	ida_simple_remove(&nd_region->pfn_ida, nd_pfn->id);
	kfree(nd_pfn->uuid);
	kfree(nd_pfn);
}

static struct device_type nd_pfn_device_type = {
	.name = "nd_pfn",
	.release = nd_pfn_release,
};

bool is_nd_pfn(struct device *dev)
{
	return dev ? dev->type == &nd_pfn_device_type : false;
}
EXPORT_SYMBOL(is_nd_pfn);

struct nd_pfn *to_nd_pfn(struct device *dev)
{
	struct nd_pfn *nd_pfn = container_of(dev, struct nd_pfn, dev);

	WARN_ON(!is_nd_pfn(dev));
	return nd_pfn;
}
EXPORT_SYMBOL(to_nd_pfn);

static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);

	switch (nd_pfn->mode) {
	case PFN_MODE_RAM:
		return sprintf(buf, "ram\n");
	case PFN_MODE_PMEM:
		return sprintf(buf, "pmem\n");
	default:
		return sprintf(buf, "none\n");
	}
}

static ssize_t mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc = 0;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	if (dev->driver)
		rc = -EBUSY;
	else {
		size_t n = len - 1;

		if (strncmp(buf, "pmem\n", n) == 0
				|| strncmp(buf, "pmem", n) == 0) {
			nd_pfn->mode = PFN_MODE_PMEM;
		} else if (strncmp(buf, "ram\n", n) == 0
				|| strncmp(buf, "ram", n) == 0)
			nd_pfn->mode = PFN_MODE_RAM;
		else if (strncmp(buf, "none\n", n) == 0
				|| strncmp(buf, "none", n) == 0)
			nd_pfn->mode = PFN_MODE_NONE;
		else
			rc = -EINVAL;
	}
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(mode);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);

	return sprintf(buf, "%lx\n", nd_pfn->align);
}

static ssize_t __align_store(struct nd_pfn *nd_pfn, const char *buf)
{
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 0, &val);
	if (rc)
		return rc;

	if (!is_power_of_2(val) || val < PAGE_SIZE || val > SZ_1G)
		return -EINVAL;

	if (nd_pfn->dev.driver)
		return -EBUSY;
	else
		nd_pfn->align = val;

	return 0;
}

static ssize_t align_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = __align_store(nd_pfn, buf);
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(align);

static ssize_t uuid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);

	if (nd_pfn->uuid)
		return sprintf(buf, "%pUb\n", nd_pfn->uuid);
	return sprintf(buf, "\n");
}

static ssize_t uuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	device_lock(dev);
	rc = nd_uuid_store(dev, &nd_pfn->uuid, buf, len);
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(uuid);

static ssize_t namespace_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	rc = sprintf(buf, "%s\n", nd_pfn->ndns
			? dev_name(&nd_pfn->ndns->dev) : "");
	nvdimm_bus_unlock(dev);
	return rc;
}

static ssize_t namespace_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = nd_namespace_store(dev, &nd_pfn->ndns, buf, len);
	dev_dbg(dev, "%s: result: %zd wrote: %s%s", __func__,
			rc, buf, buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RW(namespace);

static ssize_t resource_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver) {
		struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
		u64 offset = __le64_to_cpu(pfn_sb->dataoff);
		struct nd_namespace_common *ndns = nd_pfn->ndns;
		u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
		struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);

		rc = sprintf(buf, "%#llx\n", (unsigned long long) nsio->res.start
				+ start_pad + offset);
	} else {
		/* no address to convey if the pfn instance is disabled */
		rc = -ENXIO;
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(resource);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_pfn *nd_pfn = to_nd_pfn(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver) {
		struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
		u64 offset = __le64_to_cpu(pfn_sb->dataoff);
		struct nd_namespace_common *ndns = nd_pfn->ndns;
		u32 start_pad = __le32_to_cpu(pfn_sb->start_pad);
		u32 end_trunc = __le32_to_cpu(pfn_sb->end_trunc);
		struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);

		rc = sprintf(buf, "%llu\n", (unsigned long long)
				resource_size(&nsio->res) - start_pad
				- end_trunc - offset);
	} else {
		/* no size to convey if the pfn instance is disabled */
		rc = -ENXIO;
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(size);

static struct attribute *nd_pfn_attributes[] = {
	&dev_attr_mode.attr,
	&dev_attr_namespace.attr,
	&dev_attr_uuid.attr,
	&dev_attr_align.attr,
	&dev_attr_resource.attr,
	&dev_attr_size.attr,
	NULL,
};

static struct attribute_group nd_pfn_attribute_group = {
	.attrs = nd_pfn_attributes,
};

static const struct attribute_group *nd_pfn_attribute_groups[] = {
	&nd_pfn_attribute_group,
	&nd_device_attribute_group,
	&nd_numa_attribute_group,
	NULL,
};

static struct device *__nd_pfn_create(struct nd_region *nd_region,
		struct nd_namespace_common *ndns)
{
	struct nd_pfn *nd_pfn;
	struct device *dev;

	/* we can only create pages for contiguous ranged of pmem */
	if (!is_nd_pmem(&nd_region->dev))
		return NULL;

	nd_pfn = kzalloc(sizeof(*nd_pfn), GFP_KERNEL);
	if (!nd_pfn)
		return NULL;

	nd_pfn->id = ida_simple_get(&nd_region->pfn_ida, 0, 0, GFP_KERNEL);
	if (nd_pfn->id < 0) {
		kfree(nd_pfn);
		return NULL;
	}

	nd_pfn->mode = PFN_MODE_NONE;
	nd_pfn->align = HPAGE_SIZE;
	dev = &nd_pfn->dev;
	dev_set_name(dev, "pfn%d.%d", nd_region->id, nd_pfn->id);
	dev->parent = &nd_region->dev;
	dev->type = &nd_pfn_device_type;
	dev->groups = nd_pfn_attribute_groups;
	device_initialize(&nd_pfn->dev);
	if (ndns && !__nd_attach_ndns(&nd_pfn->dev, ndns, &nd_pfn->ndns)) {
		dev_dbg(&ndns->dev, "%s failed, already claimed by %s\n",
				__func__, dev_name(ndns->claim));
		put_device(dev);
		return NULL;
	}
	return dev;
}

struct device *nd_pfn_create(struct nd_region *nd_region)
{
	struct device *dev = __nd_pfn_create(nd_region, NULL);

	if (dev)
		__nd_device_register(dev);
	return dev;
}

int nd_pfn_validate(struct nd_pfn *nd_pfn)
{
	u64 checksum, offset;
	struct nd_namespace_io *nsio;
	struct nd_pfn_sb *pfn_sb = nd_pfn->pfn_sb;
	struct nd_namespace_common *ndns = nd_pfn->ndns;
	const u8 *parent_uuid = nd_dev_to_uuid(&ndns->dev);

	if (!pfn_sb || !ndns)
		return -ENODEV;

	if (!is_nd_pmem(nd_pfn->dev.parent))
		return -ENODEV;

	if (nvdimm_read_bytes(ndns, SZ_4K, pfn_sb, sizeof(*pfn_sb)))
		return -ENXIO;

	if (memcmp(pfn_sb->signature, PFN_SIG, PFN_SIG_LEN) != 0)
		return -ENODEV;

	checksum = le64_to_cpu(pfn_sb->checksum);
	pfn_sb->checksum = 0;
	if (checksum != nd_sb_checksum((struct nd_gen_sb *) pfn_sb))
		return -ENODEV;
	pfn_sb->checksum = cpu_to_le64(checksum);

	if (memcmp(pfn_sb->parent_uuid, parent_uuid, 16) != 0)
		return -ENODEV;

	if (__le16_to_cpu(pfn_sb->version_minor) < 1) {
		pfn_sb->start_pad = 0;
		pfn_sb->end_trunc = 0;
	}

	switch (le32_to_cpu(pfn_sb->mode)) {
	case PFN_MODE_RAM:
	case PFN_MODE_PMEM:
		break;
	default:
		return -ENXIO;
	}

	if (!nd_pfn->uuid) {
		/* from probe we allocate */
		nd_pfn->uuid = kmemdup(pfn_sb->uuid, 16, GFP_KERNEL);
		if (!nd_pfn->uuid)
			return -ENOMEM;
	} else {
		/* from init we validate */
		if (memcmp(nd_pfn->uuid, pfn_sb->uuid, 16) != 0)
			return -ENODEV;
	}

	if (nd_pfn->align > nvdimm_namespace_capacity(ndns)) {
		dev_err(&nd_pfn->dev, "alignment: %lx exceeds capacity %llx\n",
				nd_pfn->align, nvdimm_namespace_capacity(ndns));
		return -EINVAL;
	}

	/*
	 * These warnings are verbose because they can only trigger in
	 * the case where the physical address alignment of the
	 * namespace has changed since the pfn superblock was
	 * established.
	 */
	offset = le64_to_cpu(pfn_sb->dataoff);
	nsio = to_nd_namespace_io(&ndns->dev);
	if (offset >= resource_size(&nsio->res)) {
		dev_err(&nd_pfn->dev, "pfn array size exceeds capacity of %s\n",
				dev_name(&ndns->dev));
		return -EBUSY;
	}

	nd_pfn->align = 1UL << ilog2(offset);
	if (!is_power_of_2(offset) || offset < PAGE_SIZE) {
		dev_err(&nd_pfn->dev, "bad offset: %#llx dax disabled\n",
				offset);
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL(nd_pfn_validate);

int nd_pfn_probe(struct nd_namespace_common *ndns, void *drvdata)
{
	int rc;
	struct device *dev;
	struct nd_pfn *nd_pfn;
	struct nd_pfn_sb *pfn_sb;
	struct nd_region *nd_region = to_nd_region(ndns->dev.parent);

	if (ndns->force_raw)
		return -ENODEV;

	nvdimm_bus_lock(&ndns->dev);
	dev = __nd_pfn_create(nd_region, ndns);
	nvdimm_bus_unlock(&ndns->dev);
	if (!dev)
		return -ENOMEM;
	dev_set_drvdata(dev, drvdata);
	pfn_sb = kzalloc(sizeof(*pfn_sb), GFP_KERNEL);
	nd_pfn = to_nd_pfn(dev);
	nd_pfn->pfn_sb = pfn_sb;
	rc = nd_pfn_validate(nd_pfn);
	nd_pfn->pfn_sb = NULL;
	kfree(pfn_sb);
	dev_dbg(&ndns->dev, "%s: pfn: %s\n", __func__,
			rc == 0 ? dev_name(dev) : "<none>");
	if (rc < 0) {
		__nd_detach_ndns(dev, &nd_pfn->ndns);
		put_device(dev);
	} else
		__nd_device_register(&nd_pfn->dev);

	return rc;
}
EXPORT_SYMBOL(nd_pfn_probe);
