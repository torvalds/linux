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
#include <linux/slab.h>
#include <linux/nd.h>
#include "nd.h"

static void namespace_io_release(struct device *dev)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

	kfree(nsio);
}

static struct device_type namespace_io_device_type = {
	.name = "nd_namespace_io",
	.release = namespace_io_release,
};

static ssize_t nstype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);

	return sprintf(buf, "%d\n", nd_region_to_nstype(nd_region));
}
static DEVICE_ATTR_RO(nstype);

static struct attribute *nd_namespace_attributes[] = {
	&dev_attr_nstype.attr,
	NULL,
};

static struct attribute_group nd_namespace_attribute_group = {
	.attrs = nd_namespace_attributes,
};

static const struct attribute_group *nd_namespace_attribute_groups[] = {
	&nd_device_attribute_group,
	&nd_namespace_attribute_group,
	NULL,
};

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

	dev = &nsio->dev;
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

int nd_region_register_namespaces(struct nd_region *nd_region, int *err)
{
	struct device **devs = NULL;
	int i;

	*err = 0;
	switch (nd_region_to_nstype(nd_region)) {
	case ND_DEVICE_NAMESPACE_IO:
		devs = create_namespace_io(nd_region);
		break;
	default:
		break;
	}

	if (!devs)
		return -ENODEV;

	for (i = 0; devs[i]; i++) {
		struct device *dev = devs[i];

		dev_set_name(dev, "namespace%d.%d", nd_region->id, i);
		dev->groups = nd_namespace_attribute_groups;
		nd_device_register(dev);
	}
	kfree(devs);

	return i;
}
