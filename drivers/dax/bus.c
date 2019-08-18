// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017-2018 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include "dax-private.h"
#include "bus.h"

static struct class *dax_class;

static DEFINE_MUTEX(dax_bus_lock);

#define DAX_NAME_LEN 30
struct dax_id {
	struct list_head list;
	char dev_name[DAX_NAME_LEN];
};

static int dax_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/*
	 * We only ever expect to handle device-dax instances, i.e. the
	 * @type argument to MODULE_ALIAS_DAX_DEVICE() is always zero
	 */
	return add_uevent_var(env, "MODALIAS=" DAX_DEVICE_MODALIAS_FMT, 0);
}

static struct dax_device_driver *to_dax_drv(struct device_driver *drv)
{
	return container_of(drv, struct dax_device_driver, drv);
}

static struct dax_id *__dax_match_id(struct dax_device_driver *dax_drv,
		const char *dev_name)
{
	struct dax_id *dax_id;

	lockdep_assert_held(&dax_bus_lock);

	list_for_each_entry(dax_id, &dax_drv->ids, list)
		if (sysfs_streq(dax_id->dev_name, dev_name))
			return dax_id;
	return NULL;
}

static int dax_match_id(struct dax_device_driver *dax_drv, struct device *dev)
{
	int match;

	mutex_lock(&dax_bus_lock);
	match = !!__dax_match_id(dax_drv, dev_name(dev));
	mutex_unlock(&dax_bus_lock);

	return match;
}

enum id_action {
	ID_REMOVE,
	ID_ADD,
};

static ssize_t do_id_store(struct device_driver *drv, const char *buf,
		size_t count, enum id_action action)
{
	struct dax_device_driver *dax_drv = to_dax_drv(drv);
	unsigned int region_id, id;
	char devname[DAX_NAME_LEN];
	struct dax_id *dax_id;
	ssize_t rc = count;
	int fields;

	fields = sscanf(buf, "dax%d.%d", &region_id, &id);
	if (fields != 2)
		return -EINVAL;
	sprintf(devname, "dax%d.%d", region_id, id);
	if (!sysfs_streq(buf, devname))
		return -EINVAL;

	mutex_lock(&dax_bus_lock);
	dax_id = __dax_match_id(dax_drv, buf);
	if (!dax_id) {
		if (action == ID_ADD) {
			dax_id = kzalloc(sizeof(*dax_id), GFP_KERNEL);
			if (dax_id) {
				strncpy(dax_id->dev_name, buf, DAX_NAME_LEN);
				list_add(&dax_id->list, &dax_drv->ids);
			} else
				rc = -ENOMEM;
		} else
			/* nothing to remove */;
	} else if (action == ID_REMOVE) {
		list_del(&dax_id->list);
		kfree(dax_id);
	} else
		/* dax_id already added */;
	mutex_unlock(&dax_bus_lock);

	if (rc < 0)
		return rc;
	if (action == ID_ADD)
		rc = driver_attach(drv);
	if (rc)
		return rc;
	return count;
}

static ssize_t new_id_store(struct device_driver *drv, const char *buf,
		size_t count)
{
	return do_id_store(drv, buf, count, ID_ADD);
}
static DRIVER_ATTR_WO(new_id);

static ssize_t remove_id_store(struct device_driver *drv, const char *buf,
		size_t count)
{
	return do_id_store(drv, buf, count, ID_REMOVE);
}
static DRIVER_ATTR_WO(remove_id);

static struct attribute *dax_drv_attrs[] = {
	&driver_attr_new_id.attr,
	&driver_attr_remove_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dax_drv);

static int dax_bus_match(struct device *dev, struct device_driver *drv);

static struct bus_type dax_bus_type = {
	.name = "dax",
	.uevent = dax_bus_uevent,
	.match = dax_bus_match,
	.drv_groups = dax_drv_groups,
};

static int dax_bus_match(struct device *dev, struct device_driver *drv)
{
	struct dax_device_driver *dax_drv = to_dax_drv(drv);

	/*
	 * All but the 'device-dax' driver, which has 'match_always'
	 * set, requires an exact id match.
	 */
	if (dax_drv->match_always)
		return 1;

	return dax_match_id(dax_drv, dev);
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
		struct resource *res, int target_node, unsigned int align,
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
	dax_region->target_node = target_node;
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

static int dev_dax_target_node(struct dev_dax *dev_dax)
{
	struct dax_region *dax_region = dev_dax->region;

	return dax_region->target_node;
}

static ssize_t target_node_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);

	return sprintf(buf, "%d\n", dev_dax_target_node(dev_dax));
}
static DEVICE_ATTR_RO(target_node);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	/*
	 * We only ever expect to handle device-dax instances, i.e. the
	 * @type argument to MODULE_ALIAS_DAX_DEVICE() is always zero
	 */
	return sprintf(buf, DAX_DEVICE_MODALIAS_FMT "\n", 0);
}
static DEVICE_ATTR_RO(modalias);

static umode_t dev_dax_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dev_dax *dev_dax = to_dev_dax(dev);

	if (a == &dev_attr_target_node.attr && dev_dax_target_node(dev_dax) < 0)
		return 0;
	return a->mode;
}

static struct attribute *dev_dax_attributes[] = {
	&dev_attr_modalias.attr,
	&dev_attr_size.attr,
	&dev_attr_target_node.attr,
	NULL,
};

static const struct attribute_group dev_dax_attribute_group = {
	.attrs = dev_dax_attributes,
	.is_visible = dev_dax_visible,
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

struct dev_dax *__devm_create_dev_dax(struct dax_region *dax_region, int id,
		struct dev_pagemap *pgmap, enum dev_dax_subsys subsys)
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

	memcpy(&dev_dax->pgmap, pgmap, sizeof(*pgmap));

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
	dev_dax->target_node = dax_region->target_node;
	kref_get(&dax_region->kref);

	inode = dax_inode(dax_dev);
	dev->devt = inode->i_rdev;
	if (subsys == DEV_DAX_BUS)
		dev->bus = &dax_bus_type;
	else
		dev->class = dax_class;
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
EXPORT_SYMBOL_GPL(__devm_create_dev_dax);

static int match_always_count;

int __dax_driver_register(struct dax_device_driver *dax_drv,
		struct module *module, const char *mod_name)
{
	struct device_driver *drv = &dax_drv->drv;
	int rc = 0;

	INIT_LIST_HEAD(&dax_drv->ids);
	drv->owner = module;
	drv->name = mod_name;
	drv->mod_name = mod_name;
	drv->bus = &dax_bus_type;

	/* there can only be one default driver */
	mutex_lock(&dax_bus_lock);
	match_always_count += dax_drv->match_always;
	if (match_always_count > 1) {
		match_always_count--;
		WARN_ON(1);
		rc = -EINVAL;
	}
	mutex_unlock(&dax_bus_lock);
	if (rc)
		return rc;
	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(__dax_driver_register);

void dax_driver_unregister(struct dax_device_driver *dax_drv)
{
	struct device_driver *drv = &dax_drv->drv;
	struct dax_id *dax_id, *_id;

	mutex_lock(&dax_bus_lock);
	match_always_count -= dax_drv->match_always;
	list_for_each_entry_safe(dax_id, _id, &dax_drv->ids, list) {
		list_del(&dax_id->list);
		kfree(dax_id);
	}
	mutex_unlock(&dax_bus_lock);
	driver_unregister(drv);
}
EXPORT_SYMBOL_GPL(dax_driver_unregister);

int __init dax_bus_init(void)
{
	int rc;

	if (IS_ENABLED(CONFIG_DEV_DAX_PMEM_COMPAT)) {
		dax_class = class_create(THIS_MODULE, "dax");
		if (IS_ERR(dax_class))
			return PTR_ERR(dax_class);
	}

	rc = bus_register(&dax_bus_type);
	if (rc)
		class_destroy(dax_class);
	return rc;
}

void __exit dax_bus_exit(void)
{
	bus_unregister(&dax_bus_type);
	class_destroy(dax_class);
}
