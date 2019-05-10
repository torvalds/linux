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
#include "btt.h"
#include "nd.h"

static void nd_btt_release(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_btt *nd_btt = to_nd_btt(dev);

	dev_dbg(dev, "trace\n");
	nd_detach_ndns(&nd_btt->dev, &nd_btt->ndns);
	ida_simple_remove(&nd_region->btt_ida, nd_btt->id);
	kfree(nd_btt->uuid);
	kfree(nd_btt);
}

static struct device_type nd_btt_device_type = {
	.name = "nd_btt",
	.release = nd_btt_release,
};

bool is_nd_btt(struct device *dev)
{
	return dev->type == &nd_btt_device_type;
}
EXPORT_SYMBOL(is_nd_btt);

struct nd_btt *to_nd_btt(struct device *dev)
{
	struct nd_btt *nd_btt = container_of(dev, struct nd_btt, dev);

	WARN_ON(!is_nd_btt(dev));
	return nd_btt;
}
EXPORT_SYMBOL(to_nd_btt);

static const unsigned long btt_lbasize_supported[] = { 512, 520, 528,
	4096, 4104, 4160, 4224, 0 };

static ssize_t sector_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);

	return nd_size_select_show(nd_btt->lbasize, btt_lbasize_supported, buf);
}

static ssize_t sector_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = nd_size_select_store(dev, buf, &nd_btt->lbasize,
			btt_lbasize_supported);
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(sector_size);

static ssize_t uuid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);

	if (nd_btt->uuid)
		return sprintf(buf, "%pUb\n", nd_btt->uuid);
	return sprintf(buf, "\n");
}

static ssize_t uuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);
	ssize_t rc;

	device_lock(dev);
	rc = nd_uuid_store(dev, &nd_btt->uuid, buf, len);
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	device_unlock(dev);

	return rc ? rc : len;
}
static DEVICE_ATTR_RW(uuid);

static ssize_t namespace_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	rc = sprintf(buf, "%s\n", nd_btt->ndns
			? dev_name(&nd_btt->ndns->dev) : "");
	nvdimm_bus_unlock(dev);
	return rc;
}

static ssize_t namespace_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);
	ssize_t rc;

	device_lock(dev);
	nvdimm_bus_lock(dev);
	rc = nd_namespace_store(dev, &nd_btt->ndns, buf, len);
	dev_dbg(dev, "result: %zd wrote: %s%s", rc, buf,
			buf[len - 1] == '\n' ? "" : "\n");
	nvdimm_bus_unlock(dev);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RW(namespace);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_btt *nd_btt = to_nd_btt(dev);
	ssize_t rc;

	device_lock(dev);
	if (dev->driver)
		rc = sprintf(buf, "%llu\n", nd_btt->size);
	else {
		/* no size to convey if the btt instance is disabled */
		rc = -ENXIO;
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(size);

static ssize_t log_zero_flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Y\n");
}
static DEVICE_ATTR_RO(log_zero_flags);

static struct attribute *nd_btt_attributes[] = {
	&dev_attr_sector_size.attr,
	&dev_attr_namespace.attr,
	&dev_attr_uuid.attr,
	&dev_attr_size.attr,
	&dev_attr_log_zero_flags.attr,
	NULL,
};

static struct attribute_group nd_btt_attribute_group = {
	.attrs = nd_btt_attributes,
};

static const struct attribute_group *nd_btt_attribute_groups[] = {
	&nd_btt_attribute_group,
	&nd_device_attribute_group,
	&nd_numa_attribute_group,
	NULL,
};

static struct device *__nd_btt_create(struct nd_region *nd_region,
		unsigned long lbasize, u8 *uuid,
		struct nd_namespace_common *ndns)
{
	struct nd_btt *nd_btt;
	struct device *dev;

	nd_btt = kzalloc(sizeof(*nd_btt), GFP_KERNEL);
	if (!nd_btt)
		return NULL;

	nd_btt->id = ida_simple_get(&nd_region->btt_ida, 0, 0, GFP_KERNEL);
	if (nd_btt->id < 0)
		goto out_nd_btt;

	nd_btt->lbasize = lbasize;
	if (uuid) {
		uuid = kmemdup(uuid, 16, GFP_KERNEL);
		if (!uuid)
			goto out_put_id;
	}
	nd_btt->uuid = uuid;
	dev = &nd_btt->dev;
	dev_set_name(dev, "btt%d.%d", nd_region->id, nd_btt->id);
	dev->parent = &nd_region->dev;
	dev->type = &nd_btt_device_type;
	dev->groups = nd_btt_attribute_groups;
	device_initialize(&nd_btt->dev);
	if (ndns && !__nd_attach_ndns(&nd_btt->dev, ndns, &nd_btt->ndns)) {
		dev_dbg(&ndns->dev, "failed, already claimed by %s\n",
				dev_name(ndns->claim));
		put_device(dev);
		return NULL;
	}
	return dev;

out_put_id:
	ida_simple_remove(&nd_region->btt_ida, nd_btt->id);

out_nd_btt:
	kfree(nd_btt);
	return NULL;
}

struct device *nd_btt_create(struct nd_region *nd_region)
{
	struct device *dev = __nd_btt_create(nd_region, 0, NULL, NULL);

	__nd_device_register(dev);
	return dev;
}

/**
 * nd_btt_arena_is_valid - check if the metadata layout is valid
 * @nd_btt:	device with BTT geometry and backing device info
 * @super:	pointer to the arena's info block being tested
 *
 * Check consistency of the btt info block with itself by validating
 * the checksum, and with the parent namespace by verifying the
 * parent_uuid contained in the info block with the one supplied in.
 *
 * Returns:
 * false for an invalid info block, true for a valid one
 */
bool nd_btt_arena_is_valid(struct nd_btt *nd_btt, struct btt_sb *super)
{
	const u8 *parent_uuid = nd_dev_to_uuid(&nd_btt->ndns->dev);
	u64 checksum;

	if (memcmp(super->signature, BTT_SIG, BTT_SIG_LEN) != 0)
		return false;

	if (!guid_is_null((guid_t *)&super->parent_uuid))
		if (memcmp(super->parent_uuid, parent_uuid, 16) != 0)
			return false;

	checksum = le64_to_cpu(super->checksum);
	super->checksum = 0;
	if (checksum != nd_sb_checksum((struct nd_gen_sb *) super))
		return false;
	super->checksum = cpu_to_le64(checksum);

	/* TODO: figure out action for this */
	if ((le32_to_cpu(super->flags) & IB_FLAG_ERROR_MASK) != 0)
		dev_info(&nd_btt->dev, "Found arena with an error flag\n");

	return true;
}
EXPORT_SYMBOL(nd_btt_arena_is_valid);

int nd_btt_version(struct nd_btt *nd_btt, struct nd_namespace_common *ndns,
		struct btt_sb *btt_sb)
{
	if (ndns->claim_class == NVDIMM_CCLASS_BTT2) {
		/* Probe/setup for BTT v2.0 */
		nd_btt->initial_offset = 0;
		nd_btt->version_major = 2;
		nd_btt->version_minor = 0;
		if (nvdimm_read_bytes(ndns, 0, btt_sb, sizeof(*btt_sb), 0))
			return -ENXIO;
		if (!nd_btt_arena_is_valid(nd_btt, btt_sb))
			return -ENODEV;
		if ((le16_to_cpu(btt_sb->version_major) != 2) ||
				(le16_to_cpu(btt_sb->version_minor) != 0))
			return -ENODEV;
	} else {
		/*
		 * Probe/setup for BTT v1.1 (NVDIMM_CCLASS_NONE or
		 * NVDIMM_CCLASS_BTT)
		 */
		nd_btt->initial_offset = SZ_4K;
		nd_btt->version_major = 1;
		nd_btt->version_minor = 1;
		if (nvdimm_read_bytes(ndns, SZ_4K, btt_sb, sizeof(*btt_sb), 0))
			return -ENXIO;
		if (!nd_btt_arena_is_valid(nd_btt, btt_sb))
			return -ENODEV;
		if ((le16_to_cpu(btt_sb->version_major) != 1) ||
				(le16_to_cpu(btt_sb->version_minor) != 1))
			return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL(nd_btt_version);

static int __nd_btt_probe(struct nd_btt *nd_btt,
		struct nd_namespace_common *ndns, struct btt_sb *btt_sb)
{
	int rc;

	if (!btt_sb || !ndns || !nd_btt)
		return -ENODEV;

	if (nvdimm_namespace_capacity(ndns) < SZ_16M)
		return -ENXIO;

	rc = nd_btt_version(nd_btt, ndns, btt_sb);
	if (rc < 0)
		return rc;

	nd_btt->lbasize = le32_to_cpu(btt_sb->external_lbasize);
	nd_btt->uuid = kmemdup(btt_sb->uuid, 16, GFP_KERNEL);
	if (!nd_btt->uuid)
		return -ENOMEM;

	__nd_device_register(&nd_btt->dev);

	return 0;
}

int nd_btt_probe(struct device *dev, struct nd_namespace_common *ndns)
{
	int rc;
	struct device *btt_dev;
	struct btt_sb *btt_sb;
	struct nd_region *nd_region = to_nd_region(ndns->dev.parent);

	if (ndns->force_raw)
		return -ENODEV;

	switch (ndns->claim_class) {
	case NVDIMM_CCLASS_NONE:
	case NVDIMM_CCLASS_BTT:
	case NVDIMM_CCLASS_BTT2:
		break;
	default:
		return -ENODEV;
	}

	nvdimm_bus_lock(&ndns->dev);
	btt_dev = __nd_btt_create(nd_region, 0, NULL, ndns);
	nvdimm_bus_unlock(&ndns->dev);
	if (!btt_dev)
		return -ENOMEM;
	btt_sb = devm_kzalloc(dev, sizeof(*btt_sb), GFP_KERNEL);
	rc = __nd_btt_probe(to_nd_btt(btt_dev), ndns, btt_sb);
	dev_dbg(dev, "btt: %s\n", rc == 0 ? dev_name(btt_dev) : "<none>");
	if (rc < 0) {
		struct nd_btt *nd_btt = to_nd_btt(btt_dev);

		nd_detach_ndns(btt_dev, &nd_btt->ndns);
		put_device(btt_dev);
	}

	return rc;
}
EXPORT_SYMBOL(nd_btt_probe);
