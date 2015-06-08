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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/device.h>
#include <linux/ndctl.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "nd-core.h"

static DEFINE_IDA(dimm_ida);

static void nvdimm_release(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	ida_simple_remove(&dimm_ida, nvdimm->id);
	kfree(nvdimm);
}

static struct device_type nvdimm_device_type = {
	.name = "nvdimm",
	.release = nvdimm_release,
};

bool is_nvdimm(struct device *dev)
{
	return dev->type == &nvdimm_device_type;
}

struct nvdimm *to_nvdimm(struct device *dev)
{
	struct nvdimm *nvdimm = container_of(dev, struct nvdimm, dev);

	WARN_ON(!is_nvdimm(dev));
	return nvdimm;
}
EXPORT_SYMBOL_GPL(to_nvdimm);

const char *nvdimm_name(struct nvdimm *nvdimm)
{
	return dev_name(&nvdimm->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_name);

void *nvdimm_provider_data(struct nvdimm *nvdimm)
{
	if (nvdimm)
		return nvdimm->provider_data;
	return NULL;
}
EXPORT_SYMBOL_GPL(nvdimm_provider_data);

static ssize_t commands_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	int cmd, len = 0;

	if (!nvdimm->dsm_mask)
		return sprintf(buf, "\n");

	for_each_set_bit(cmd, nvdimm->dsm_mask, BITS_PER_LONG)
		len += sprintf(buf + len, "%s ", nvdimm_cmd_name(cmd));
	len += sprintf(buf + len, "\n");
	return len;
}
static DEVICE_ATTR_RO(commands);

static struct attribute *nvdimm_attributes[] = {
	&dev_attr_commands.attr,
	NULL,
};

struct attribute_group nvdimm_attribute_group = {
	.attrs = nvdimm_attributes,
};
EXPORT_SYMBOL_GPL(nvdimm_attribute_group);

struct nvdimm *nvdimm_create(struct nvdimm_bus *nvdimm_bus, void *provider_data,
		const struct attribute_group **groups, unsigned long flags,
		unsigned long *dsm_mask)
{
	struct nvdimm *nvdimm = kzalloc(sizeof(*nvdimm), GFP_KERNEL);
	struct device *dev;

	if (!nvdimm)
		return NULL;

	nvdimm->id = ida_simple_get(&dimm_ida, 0, 0, GFP_KERNEL);
	if (nvdimm->id < 0) {
		kfree(nvdimm);
		return NULL;
	}
	nvdimm->provider_data = provider_data;
	nvdimm->flags = flags;
	nvdimm->dsm_mask = dsm_mask;

	dev = &nvdimm->dev;
	dev_set_name(dev, "nmem%d", nvdimm->id);
	dev->parent = &nvdimm_bus->dev;
	dev->type = &nvdimm_device_type;
	dev->bus = &nvdimm_bus_type;
	dev->devt = MKDEV(nvdimm_major, nvdimm->id);
	dev->groups = groups;
	if (device_register(dev) != 0) {
		put_device(dev);
		return NULL;
	}

	return nvdimm;
}
EXPORT_SYMBOL_GPL(nvdimm_create);
