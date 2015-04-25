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
#include <linux/libnvdimm.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "nd-core.h"

LIST_HEAD(nvdimm_bus_list);
DEFINE_MUTEX(nvdimm_bus_list_mutex);
static DEFINE_IDA(nd_ida);

static void nvdimm_bus_release(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	ida_simple_remove(&nd_ida, nvdimm_bus->id);
	kfree(nvdimm_bus);
}

struct nvdimm_bus *to_nvdimm_bus(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	WARN_ON(nvdimm_bus->dev.release != nvdimm_bus_release);
	return nvdimm_bus;
}
EXPORT_SYMBOL_GPL(to_nvdimm_bus);

struct nvdimm_bus_descriptor *to_nd_desc(struct nvdimm_bus *nvdimm_bus)
{
	/* struct nvdimm_bus definition is private to libnvdimm */
	return nvdimm_bus->nd_desc;
}
EXPORT_SYMBOL_GPL(to_nd_desc);

struct nvdimm_bus *walk_to_nvdimm_bus(struct device *nd_dev)
{
	struct device *dev;

	for (dev = nd_dev; dev; dev = dev->parent)
		if (dev->release == nvdimm_bus_release)
			break;
	dev_WARN_ONCE(nd_dev, !dev, "invalid dev, not on nd bus\n");
	if (dev)
		return to_nvdimm_bus(dev);
	return NULL;
}

static const char *nvdimm_bus_provider(struct nvdimm_bus *nvdimm_bus)
{
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	struct device *parent = nvdimm_bus->dev.parent;

	if (nd_desc->provider_name)
		return nd_desc->provider_name;
	else if (parent)
		return dev_name(parent);
	else
		return "unknown";
}

static ssize_t provider_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);

	return sprintf(buf, "%s\n", nvdimm_bus_provider(nvdimm_bus));
}
static DEVICE_ATTR_RO(provider);

static struct attribute *nvdimm_bus_attributes[] = {
	&dev_attr_provider.attr,
	NULL,
};

struct attribute_group nvdimm_bus_attribute_group = {
	.attrs = nvdimm_bus_attributes,
};
EXPORT_SYMBOL_GPL(nvdimm_bus_attribute_group);

struct nvdimm_bus *nvdimm_bus_register(struct device *parent,
		struct nvdimm_bus_descriptor *nd_desc)
{
	struct nvdimm_bus *nvdimm_bus;
	int rc;

	nvdimm_bus = kzalloc(sizeof(*nvdimm_bus), GFP_KERNEL);
	if (!nvdimm_bus)
		return NULL;
	INIT_LIST_HEAD(&nvdimm_bus->list);
	nvdimm_bus->id = ida_simple_get(&nd_ida, 0, 0, GFP_KERNEL);
	if (nvdimm_bus->id < 0) {
		kfree(nvdimm_bus);
		return NULL;
	}
	nvdimm_bus->nd_desc = nd_desc;
	nvdimm_bus->dev.parent = parent;
	nvdimm_bus->dev.release = nvdimm_bus_release;
	nvdimm_bus->dev.groups = nd_desc->attr_groups;
	dev_set_name(&nvdimm_bus->dev, "ndbus%d", nvdimm_bus->id);
	rc = device_register(&nvdimm_bus->dev);
	if (rc) {
		dev_dbg(&nvdimm_bus->dev, "registration failed: %d\n", rc);
		goto err;
	}

	rc = nvdimm_bus_create_ndctl(nvdimm_bus);
	if (rc)
		goto err;

	mutex_lock(&nvdimm_bus_list_mutex);
	list_add_tail(&nvdimm_bus->list, &nvdimm_bus_list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	return nvdimm_bus;
 err:
	put_device(&nvdimm_bus->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(nvdimm_bus_register);

static int child_unregister(struct device *dev, void *data)
{
	/*
	 * the singular ndctl class device per bus needs to be
	 * "device_destroy"ed, so skip it here
	 *
	 * i.e. remove classless children
	 */
	if (dev->class)
		/* pass */;
	else
		device_unregister(dev);
	return 0;
}

void nvdimm_bus_unregister(struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;

	mutex_lock(&nvdimm_bus_list_mutex);
	list_del_init(&nvdimm_bus->list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	device_for_each_child(&nvdimm_bus->dev, NULL, child_unregister);
	nvdimm_bus_destroy_ndctl(nvdimm_bus);

	device_unregister(&nvdimm_bus->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_bus_unregister);

static __init int libnvdimm_init(void)
{
	return nvdimm_bus_init();
}

static __exit void libnvdimm_exit(void)
{
	WARN_ON(!list_empty(&nvdimm_bus_list));
	nvdimm_bus_exit();
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
subsys_initcall(libnvdimm_init);
module_exit(libnvdimm_exit);
