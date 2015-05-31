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
#include <linux/slab.h>
#include <linux/io.h>
#include "nd-core.h"
#include "nd.h"

static DEFINE_IDA(region_ida);

static void nd_region_release(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev);
	u16 i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		put_device(&nvdimm->dev);
	}
	ida_simple_remove(&region_ida, nd_region->id);
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

struct nd_region *to_nd_region(struct device *dev)
{
	struct nd_region *nd_region = container_of(dev, struct nd_region, dev);

	WARN_ON(dev->type->release != nd_region_release);
	return nd_region;
}
EXPORT_SYMBOL_GPL(to_nd_region);

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
	if (is_nd_pmem(&nd_region->dev)) {
		u16 i, alias;

		for (i = 0, alias = 0; i < nd_region->ndr_mappings; i++) {
			struct nd_mapping *nd_mapping = &nd_region->mapping[i];
			struct nvdimm *nvdimm = nd_mapping->nvdimm;

			if (nvdimm->flags & NDD_ALIASING)
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

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	unsigned long long size = 0;

	if (is_nd_pmem(dev)) {
		size = nd_region->ndr_size;
	} else if (nd_region->ndr_mappings == 1) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[0];

		size = nd_mapping->size;
	}

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(size);

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

static ssize_t init_namespaces_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region_namespaces *num_ns = dev_get_drvdata(dev);
	ssize_t rc;

	nvdimm_bus_lock(dev);
	if (num_ns)
		rc = sprintf(buf, "%d/%d\n", num_ns->active, num_ns->count);
	else
		rc = -ENXIO;
	nvdimm_bus_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(init_namespaces);

static struct attribute *nd_region_attributes[] = {
	&dev_attr_size.attr,
	&dev_attr_nstype.attr,
	&dev_attr_mappings.attr,
	&dev_attr_init_namespaces.attr,
	NULL,
};

struct attribute_group nd_region_attribute_group = {
	.attrs = nd_region_attributes,
};
EXPORT_SYMBOL_GPL(nd_region_attribute_group);

static ssize_t mappingN(struct device *dev, char *buf, int n)
{
	struct nd_region *nd_region = to_nd_region(dev);
	struct nd_mapping *nd_mapping;
	struct nvdimm *nvdimm;

	if (n >= nd_region->ndr_mappings)
		return -ENXIO;
	nd_mapping = &nd_region->mapping[n];
	nvdimm = nd_mapping->nvdimm;

	return sprintf(buf, "%s,%llu,%llu\n", dev_name(&nvdimm->dev),
			nd_mapping->start, nd_mapping->size);
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

void *nd_region_provider_data(struct nd_region *nd_region)
{
	return nd_region->provider_data;
}
EXPORT_SYMBOL_GPL(nd_region_provider_data);

static struct nd_region *nd_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc, struct device_type *dev_type,
		const char *caller)
{
	struct nd_region *nd_region;
	struct device *dev;
	u16 i;

	for (i = 0; i < ndr_desc->num_mappings; i++) {
		struct nd_mapping *nd_mapping = &ndr_desc->nd_mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		if ((nd_mapping->start | nd_mapping->size) % SZ_4K) {
			dev_err(&nvdimm_bus->dev, "%s: %s mapping%d is not 4K aligned\n",
					caller, dev_name(&nvdimm->dev), i);

			return NULL;
		}
	}

	nd_region = kzalloc(sizeof(struct nd_region)
			+ sizeof(struct nd_mapping) * ndr_desc->num_mappings,
			GFP_KERNEL);
	if (!nd_region)
		return NULL;
	nd_region->id = ida_simple_get(&region_ida, 0, 0, GFP_KERNEL);
	if (nd_region->id < 0) {
		kfree(nd_region);
		return NULL;
	}

	memcpy(nd_region->mapping, ndr_desc->nd_mapping,
			sizeof(struct nd_mapping) * ndr_desc->num_mappings);
	for (i = 0; i < ndr_desc->num_mappings; i++) {
		struct nd_mapping *nd_mapping = &ndr_desc->nd_mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;

		get_device(&nvdimm->dev);
	}
	nd_region->ndr_mappings = ndr_desc->num_mappings;
	nd_region->provider_data = ndr_desc->provider_data;
	dev = &nd_region->dev;
	dev_set_name(dev, "region%d", nd_region->id);
	dev->parent = &nvdimm_bus->dev;
	dev->type = dev_type;
	dev->groups = ndr_desc->attr_groups;
	nd_region->ndr_size = resource_size(ndr_desc->res);
	nd_region->ndr_start = ndr_desc->res->start;
	nd_device_register(dev);

	return nd_region;
}

struct nd_region *nvdimm_pmem_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_pmem_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_pmem_region_create);

struct nd_region *nvdimm_blk_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	if (ndr_desc->num_mappings > 1)
		return NULL;
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_blk_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_blk_region_create);

struct nd_region *nvdimm_volatile_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc)
{
	return nd_region_create(nvdimm_bus, ndr_desc, &nd_volatile_device_type,
			__func__);
}
EXPORT_SYMBOL_GPL(nvdimm_volatile_region_create);
