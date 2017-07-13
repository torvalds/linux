// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017-2018 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include "dax-private.h"
#include "bus.h"

static int dax_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/*
	 * We only ever expect to handle device-dax instances, i.e. the
	 * @type argument to MODULE_ALIAS_DAX_DEVICE() is always zero
	 */
	return add_uevent_var(env, "MODALIAS=" DAX_DEVICE_MODALIAS_FMT, 0);
}

static int dax_bus_match(struct device *dev, struct device_driver *drv);

static struct bus_type dax_bus_type = {
	.name = "dax",
	.uevent = dax_bus_uevent,
	.match = dax_bus_match,
};

static int dax_bus_match(struct device *dev, struct device_driver *drv)
{
	/*
	 * The drivers that can register on the 'dax' bus are private to
	 * drivers/dax/ so any device and driver on the bus always
	 * match.
	 */
	return 1;
}

/*
 * Rely on the fact that drvdata is set before the attributes are
 * registered, and that the attributes are unregistered before drvdata
 * is cleared to assume that drvdata is always valid.
 */
static ssize_t id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dax_region->id);
}
static DEVICE_ATTR_RO(id);

static ssize_t region_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%llu\n", (unsigned long long)
			resource_size(&dax_region->res));
}
static struct device_attribute dev_attr_region_size = __ATTR(size, 0444,
		region_size_show, NULL);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", dax_region->align);
}
static DEVICE_ATTR_RO(align);

static struct attribute *dax_region_attributes[] = {
	&dev_attr_region_size.attr,
	&dev_attr_align.attr,
	&dev_attr_id.attr,
	NULL,
};

static const struct attribute_group dax_region_attribute_group = {
	.name = "dax_region",
	.attrs = dax_region_attributes,
};

static const struct attribute_group *dax_region_attribute_groups[] = {
	&dax_region_attribute_group,
	NULL,
};

static void dax_region_free(struct kref *kref)
{
	struct dax_region *dax_region;

	dax_region = container_of(kref, struct dax_region, kref);
	kfree(dax_region);
}

void dax_region_put(struct dax_region *dax_region)
{
	kref_put(&dax_region->kref, dax_region_free);
}
EXPORT_SYMBOL_GPL(dax_region_put);

static void dax_region_unregister(void *region)
{
	struct dax_region *dax_region = region;

	sysfs_remove_groups(&dax_region->dev->kobj,
			dax_region_attribute_groups);
	dax_region_put(dax_region);
}

struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct resource *res, unsigned int align,
		unsigned long pfn_flags)
{
	struct dax_region *dax_region;

	/*
	 * The DAX core assumes that it can store its private data in
	 * parent->driver_data. This WARN is a reminder / safeguard for
	 * developers of device-dax drivers.
	 */
	if (dev_get_drvdata(parent)) {
		dev_WARN(parent, "dax core failed to setup private data\n");
		return NULL;
	}

	if (!IS_ALIGNED(res->start, align)
			|| !IS_ALIGNED(resource_size(res), align))
		return NULL;

	dax_region = kzalloc(sizeof(*dax_region), GFP_KERNEL);
	if (!dax_region)
		return NULL;

	dev_set_drvdata(parent, dax_region);
	memcpy(&dax_region->res, res, sizeof(*res));
	dax_region->pfn_flags = pfn_flags;
	kref_init(&dax_region->kref);
	dax_region->id = region_id;
	dax_region->align = align;
	dax_region->dev = parent;
	if (sysfs_create_groups(&parent->kobj, dax_region_attribute_groups)) {
		kfree(dax_region);
		return NULL;
	}

	kref_get(&dax_region->kref);
	if (devm_add_action_or_reset(parent, dax_region_unregister, dax_region))
		return NULL;
	return dax_region;
}
EXPORT_SYMBOL_GPL(alloc_dax_region);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	unsigned long long size = resource_size(&dev_dax->region->res);

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(size);

static struct attribute *dev_dax_attributes[] = {
	&dev_attr_size.attr,
	NULL,
};

static const struct attribute_group dev_dax_attribute_group = {
	.attrs = dev_dax_attributes,
};

static const struct attribute_group *dax_attribute_groups[] = {
	&dev_dax_attribute_group,
	NULL,
};

void kill_dev_dax(struct dev_dax *dev_dax)
{
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct inode *inode = dax_inode(dax_dev);

	kill_dax(dax_dev);
	unmap_mapping_range(inode->i_mapping, 0, 0, 1);
}
EXPORT_SYMBOL_GPL(kill_dev_dax);

static void dev_dax_release(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	struct dax_device *dax_dev = dev_dax->dax_dev;

	dax_region_put(dax_region);
	put_dax(dax_dev);
	kfree(dev_dax);
}

static void unregister_dev_dax(void *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);

	dev_dbg(dev, "%s\n", __func__);

	kill_dev_dax(dev_dax);
	device_del(dev);
	put_device(dev);
}

struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region, int id)
{
	struct device *parent = dax_region->dev;
	struct dax_device *dax_dev;
	struct dev_dax *dev_dax;
	struct inode *inode;
	struct device *dev;
	int rc = -ENOMEM;

	if (id < 0)
		return ERR_PTR(-EINVAL);

	dev_dax = kzalloc(sizeof(*dev_dax), GFP_KERNEL);
	if (!dev_dax)
		return ERR_PTR(-ENOMEM);

	/*
	 * No 'host' or dax_operations since there is no access to this
	 * device outside of mmap of the resulting character device.
	 */
	dax_dev = alloc_dax(dev_dax, NULL, NULL);
	if (!dax_dev)
		goto err;

	/* a device_dax instance is dead while the driver is not attached */
	kill_dax(dax_dev);

	/* from here on we're committed to teardown via dax_dev_release() */
	dev = &dev_dax->dev;
	device_initialize(dev);

	dev_dax->dax_dev = dax_dev;
	dev_dax->region = dax_region;
	kref_get(&dax_region->kref);

	inode = dax_inode(dax_dev);
	dev->devt = inode->i_rdev;
	dev->bus = &dax_bus_type;
	dev->parent = parent;
	dev->groups = dax_attribute_groups;
	dev->release = dev_dax_release;
	dev_set_name(dev, "dax%d.%d", dax_region->id, id);

	rc = device_add(dev);
	if (rc) {
		kill_dev_dax(dev_dax);
		put_device(dev);
		return ERR_PTR(rc);
	}

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dev_dax, dev);
	if (rc)
		return ERR_PTR(rc);

	return dev_dax;

 err:
	kfree(dev_dax);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_create_dev_dax);

int __dax_driver_register(struct device_driver *drv,
		struct module *module, const char *mod_name)
{
	drv->owner = module;
	drv->name = mod_name;
	drv->mod_name = mod_name;
	drv->bus = &dax_bus_type;
	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(__dax_driver_register);

int __init dax_bus_init(void)
{
	return bus_register(&dax_bus_type);
}

void __exit dax_bus_exit(void)
{
	bus_unregister(&dax_bus_type);
}
