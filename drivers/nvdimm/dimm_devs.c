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
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/ndctl.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "nd-core.h"
#include "nd.h"

static DEFINE_IDA(dimm_ida);

/*
 * Retrieve bus and dimm handle and return if this bus supports
 * get_config_data commands
 */
static int __validate_dimm(struct nvdimm_drvdata *ndd)
{
	struct nvdimm *nvdimm;

	if (!ndd)
		return -EINVAL;

	nvdimm = to_nvdimm(ndd->dev);

	if (!nvdimm->dsm_mask)
		return -ENXIO;
	if (!test_bit(ND_CMD_GET_CONFIG_DATA, nvdimm->dsm_mask))
		return -ENXIO;

	return 0;
}

static int validate_dimm(struct nvdimm_drvdata *ndd)
{
	int rc = __validate_dimm(ndd);

	if (rc && ndd)
		dev_dbg(ndd->dev, "%pf: %s error: %d\n",
				__builtin_return_address(0), __func__, rc);
	return rc;
}

/**
 * nvdimm_init_nsarea - determine the geometry of a dimm's namespace area
 * @nvdimm: dimm to initialize
 */
int nvdimm_init_nsarea(struct nvdimm_drvdata *ndd)
{
	struct nd_cmd_get_config_size *cmd = &ndd->nsarea;
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	struct nvdimm_bus_descriptor *nd_desc;
	int rc = validate_dimm(ndd);

	if (rc)
		return rc;

	if (cmd->config_size)
		return 0; /* already valid */

	memset(cmd, 0, sizeof(*cmd));
	nd_desc = nvdimm_bus->nd_desc;
	return nd_desc->ndctl(nd_desc, to_nvdimm(ndd->dev),
			ND_CMD_GET_CONFIG_SIZE, cmd, sizeof(*cmd));
}

int nvdimm_init_config_data(struct nvdimm_drvdata *ndd)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(ndd->dev);
	struct nd_cmd_get_config_data_hdr *cmd;
	struct nvdimm_bus_descriptor *nd_desc;
	int rc = validate_dimm(ndd);
	u32 max_cmd_size, config_size;
	size_t offset;

	if (rc)
		return rc;

	if (ndd->data)
		return 0;

	if (ndd->nsarea.status || ndd->nsarea.max_xfer == 0)
		return -ENXIO;

	ndd->data = kmalloc(ndd->nsarea.config_size, GFP_KERNEL);
	if (!ndd->data)
		ndd->data = vmalloc(ndd->nsarea.config_size);

	if (!ndd->data)
		return -ENOMEM;

	max_cmd_size = min_t(u32, PAGE_SIZE, ndd->nsarea.max_xfer);
	cmd = kzalloc(max_cmd_size + sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	nd_desc = nvdimm_bus->nd_desc;
	for (config_size = ndd->nsarea.config_size, offset = 0;
			config_size; config_size -= cmd->in_length,
			offset += cmd->in_length) {
		cmd->in_length = min(config_size, max_cmd_size);
		cmd->in_offset = offset;
		rc = nd_desc->ndctl(nd_desc, to_nvdimm(ndd->dev),
				ND_CMD_GET_CONFIG_DATA, cmd,
				cmd->in_length + sizeof(*cmd));
		if (rc || cmd->status) {
			rc = -ENXIO;
			break;
		}
		memcpy(ndd->data + offset, cmd->out_buf, cmd->in_length);
	}
	dev_dbg(ndd->dev, "%s: len: %zu rc: %d\n", __func__, offset, rc);
	kfree(cmd);

	return rc;
}

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

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	/*
	 * The state may be in the process of changing, userspace should
	 * quiesce probing if it wants a static answer
	 */
	nvdimm_bus_lock(dev);
	nvdimm_bus_unlock(dev);
	return sprintf(buf, "%s\n", atomic_read(&nvdimm->busy)
			? "active" : "idle");
}
static DEVICE_ATTR_RO(state);

static struct attribute *nvdimm_attributes[] = {
	&dev_attr_state.attr,
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
	atomic_set(&nvdimm->busy, 0);
	dev = &nvdimm->dev;
	dev_set_name(dev, "nmem%d", nvdimm->id);
	dev->parent = &nvdimm_bus->dev;
	dev->type = &nvdimm_device_type;
	dev->devt = MKDEV(nvdimm_major, nvdimm->id);
	dev->groups = groups;
	nd_device_register(dev);

	return nvdimm;
}
EXPORT_SYMBOL_GPL(nvdimm_create);

static int count_dimms(struct device *dev, void *c)
{
	int *count = c;

	if (is_nvdimm(dev))
		(*count)++;
	return 0;
}

int nvdimm_bus_check_dimm_count(struct nvdimm_bus *nvdimm_bus, int dimm_count)
{
	int count = 0;
	/* Flush any possible dimm registration failures */
	nd_synchronize();

	device_for_each_child(&nvdimm_bus->dev, &count, count_dimms);
	dev_dbg(&nvdimm_bus->dev, "%s: count: %d\n", __func__, count);
	if (count != dimm_count)
		return -ENXIO;
	return 0;
}
EXPORT_SYMBOL_GPL(nvdimm_bus_check_dimm_count);
