// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017-2018 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/io.h>
#include "dax-private.h"
#include "bus.h"

static DEFINE_MUTEX(dax_bus_lock);

#define DAX_NAME_LEN 30
struct dax_id {
	struct list_head list;
	char dev_name[DAX_NAME_LEN];
};

static int dax_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
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

static int dax_match_type(struct dax_device_driver *dax_drv, struct device *dev)
{
	enum dax_driver_type type = DAXDRV_DEVICE_TYPE;
	struct dev_dax *dev_dax = to_dev_dax(dev);

	if (dev_dax->region->res.flags & IORESOURCE_DAX_KMEM)
		type = DAXDRV_KMEM_TYPE;

	if (dax_drv->type == type)
		return 1;

	/* default to device mode if dax_kmem is disabled */
	if (dax_drv->type == DAXDRV_DEVICE_TYPE &&
	    !IS_ENABLED(CONFIG_DEV_DAX_KMEM))
		return 1;

	return 0;
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
		}
	} else if (action == ID_REMOVE) {
		list_del(&dax_id->list);
		kfree(dax_id);
	}
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

/*
 * Static dax regions are regions created by an external subsystem
 * nvdimm where a single range is assigned. Its boundaries are by the external
 * subsystem and are usually limited to one physical memory range. For example,
 * for PMEM it is usually defined by NVDIMM Namespace boundaries (i.e. a
 * single contiguous range)
 *
 * On dynamic dax regions, the assigned region can be partitioned by dax core
 * into multiple subdivisions. A subdivision is represented into one
 * /dev/daxN.M device composed by one or more potentially discontiguous ranges.
 *
 * When allocating a dax region, drivers must set whether it's static
 * (IORESOURCE_DAX_STATIC).  On static dax devices, the @pgmap is pre-assigned
 * to dax core when calling devm_create_dev_dax(), whereas in dynamic dax
 * devices it is NULL but afterwards allocated by dax core on device ->probe().
 * Care is needed to make sure that dynamic dax devices are torn down with a
 * cleared @pgmap field (see kill_dev_dax()).
 */
static bool is_static(struct dax_region *dax_region)
{
	return (dax_region->res.flags & IORESOURCE_DAX_STATIC) != 0;
}

bool static_dev_dax(struct dev_dax *dev_dax)
{
	return is_static(dev_dax->region);
}
EXPORT_SYMBOL_GPL(static_dev_dax);

static u64 dev_dax_size(struct dev_dax *dev_dax)
{
	u64 size = 0;
	int i;

	device_lock_assert(&dev_dax->dev);

	for (i = 0; i < dev_dax->nr_range; i++)
		size += range_len(&dev_dax->ranges[i].range);

	return size;
}

static int dax_bus_probe(struct device *dev)
{
	struct dax_device_driver *dax_drv = to_dax_drv(dev->driver);
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	int rc;

	if (dev_dax_size(dev_dax) == 0 || dev_dax->id < 0)
		return -ENXIO;

	rc = dax_drv->probe(dev_dax);

	if (rc || is_static(dax_region))
		return rc;

	/*
	 * Track new seed creation only after successful probe of the
	 * previous seed.
	 */
	if (dax_region->seed == dev)
		dax_region->seed = NULL;

	return 0;
}

static void dax_bus_remove(struct device *dev)
{
	struct dax_device_driver *dax_drv = to_dax_drv(dev->driver);
	struct dev_dax *dev_dax = to_dev_dax(dev);

	if (dax_drv->remove)
		dax_drv->remove(dev_dax);
}

static struct bus_type dax_bus_type = {
	.name = "dax",
	.uevent = dax_bus_uevent,
	.match = dax_bus_match,
	.probe = dax_bus_probe,
	.remove = dax_bus_remove,
	.drv_groups = dax_drv_groups,
};

static int dax_bus_match(struct device *dev, struct device_driver *drv)
{
	struct dax_device_driver *dax_drv = to_dax_drv(drv);

	if (dax_match_id(dax_drv, dev))
		return 1;
	return dax_match_type(dax_drv, dev);
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

static ssize_t region_align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", dax_region->align);
}
static struct device_attribute dev_attr_region_align =
		__ATTR(align, 0400, region_align_show, NULL);

#define for_each_dax_region_resource(dax_region, res) \
	for (res = (dax_region)->res.child; res; res = res->sibling)

static unsigned long long dax_region_avail_size(struct dax_region *dax_region)
{
	resource_size_t size = resource_size(&dax_region->res);
	struct resource *res;

	device_lock_assert(dax_region->dev);

	for_each_dax_region_resource(dax_region, res)
		size -= resource_size(res);
	return size;
}

static ssize_t available_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);
	unsigned long long size;

	device_lock(dev);
	size = dax_region_avail_size(dax_region);
	device_unlock(dev);

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(available_size);

static ssize_t seed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);
	struct device *seed;
	ssize_t rc;

	if (is_static(dax_region))
		return -EINVAL;

	device_lock(dev);
	seed = dax_region->seed;
	rc = sprintf(buf, "%s\n", seed ? dev_name(seed) : "");
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(seed);

static ssize_t create_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);
	struct device *youngest;
	ssize_t rc;

	if (is_static(dax_region))
		return -EINVAL;

	device_lock(dev);
	youngest = dax_region->youngest;
	rc = sprintf(buf, "%s\n", youngest ? dev_name(youngest) : "");
	device_unlock(dev);

	return rc;
}

static ssize_t create_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);
	unsigned long long avail;
	ssize_t rc;
	int val;

	if (is_static(dax_region))
		return -EINVAL;

	rc = kstrtoint(buf, 0, &val);
	if (rc)
		return rc;
	if (val != 1)
		return -EINVAL;

	device_lock(dev);
	avail = dax_region_avail_size(dax_region);
	if (avail == 0)
		rc = -ENOSPC;
	else {
		struct dev_dax_data data = {
			.dax_region = dax_region,
			.size = 0,
			.id = -1,
		};
		struct dev_dax *dev_dax = devm_create_dev_dax(&data);

		if (IS_ERR(dev_dax))
			rc = PTR_ERR(dev_dax);
		else {
			/*
			 * In support of crafting multiple new devices
			 * simultaneously multiple seeds can be created,
			 * but only the first one that has not been
			 * successfully bound is tracked as the region
			 * seed.
			 */
			if (!dax_region->seed)
				dax_region->seed = &dev_dax->dev;
			dax_region->youngest = &dev_dax->dev;
			rc = len;
		}
	}
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RW(create);

void kill_dev_dax(struct dev_dax *dev_dax)
{
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct inode *inode = dax_inode(dax_dev);

	kill_dax(dax_dev);
	unmap_mapping_range(inode->i_mapping, 0, 0, 1);

	/*
	 * Dynamic dax region have the pgmap allocated via dev_kzalloc()
	 * and thus freed by devm. Clear the pgmap to not have stale pgmap
	 * ranges on probe() from previous reconfigurations of region devices.
	 */
	if (!static_dev_dax(dev_dax))
		dev_dax->pgmap = NULL;
}
EXPORT_SYMBOL_GPL(kill_dev_dax);

static void trim_dev_dax_range(struct dev_dax *dev_dax)
{
	int i = dev_dax->nr_range - 1;
	struct range *range = &dev_dax->ranges[i].range;
	struct dax_region *dax_region = dev_dax->region;

	device_lock_assert(dax_region->dev);
	dev_dbg(&dev_dax->dev, "delete range[%d]: %#llx:%#llx\n", i,
		(unsigned long long)range->start,
		(unsigned long long)range->end);

	__release_region(&dax_region->res, range->start, range_len(range));
	if (--dev_dax->nr_range == 0) {
		kfree(dev_dax->ranges);
		dev_dax->ranges = NULL;
	}
}

static void free_dev_dax_ranges(struct dev_dax *dev_dax)
{
	while (dev_dax->nr_range)
		trim_dev_dax_range(dev_dax);
}

static void unregister_dev_dax(void *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);

	dev_dbg(dev, "%s\n", __func__);

	kill_dev_dax(dev_dax);
	device_del(dev);
	free_dev_dax_ranges(dev_dax);
	put_device(dev);
}

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

/* a return value >= 0 indicates this invocation invalidated the id */
static int __free_dev_dax_id(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	struct dax_region *dax_region;
	int rc = dev_dax->id;

	device_lock_assert(dev);

	if (!dev_dax->dyn_id || dev_dax->id < 0)
		return -1;
	dax_region = dev_dax->region;
	ida_free(&dax_region->ida, dev_dax->id);
	dax_region_put(dax_region);
	dev_dax->id = -1;
	return rc;
}

static int free_dev_dax_id(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	int rc;

	device_lock(dev);
	rc = __free_dev_dax_id(dev_dax);
	device_unlock(dev);
	return rc;
}

static int alloc_dev_dax_id(struct dev_dax *dev_dax)
{
	struct dax_region *dax_region = dev_dax->region;
	int id;

	id = ida_alloc(&dax_region->ida, GFP_KERNEL);
	if (id < 0)
		return id;
	kref_get(&dax_region->kref);
	dev_dax->dyn_id = true;
	dev_dax->id = id;
	return id;
}

static ssize_t delete_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);
	struct dev_dax *dev_dax;
	struct device *victim;
	bool do_del = false;
	int rc;

	if (is_static(dax_region))
		return -EINVAL;

	victim = device_find_child_by_name(dax_region->dev, buf);
	if (!victim)
		return -ENXIO;

	device_lock(dev);
	device_lock(victim);
	dev_dax = to_dev_dax(victim);
	if (victim->driver || dev_dax_size(dev_dax))
		rc = -EBUSY;
	else {
		/*
		 * Invalidate the device so it does not become active
		 * again, but always preserve device-id-0 so that
		 * /sys/bus/dax/ is guaranteed to be populated while any
		 * dax_region is registered.
		 */
		if (dev_dax->id > 0) {
			do_del = __free_dev_dax_id(dev_dax) >= 0;
			rc = len;
			if (dax_region->seed == victim)
				dax_region->seed = NULL;
			if (dax_region->youngest == victim)
				dax_region->youngest = NULL;
		} else
			rc = -EBUSY;
	}
	device_unlock(victim);

	/* won the race to invalidate the device, clean it up */
	if (do_del)
		devm_release_action(dev, unregister_dev_dax, victim);
	device_unlock(dev);
	put_device(victim);

	return rc;
}
static DEVICE_ATTR_WO(delete);

static umode_t dax_region_visible(struct kobject *kobj, struct attribute *a,
		int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dax_region *dax_region = dev_get_drvdata(dev);

	if (is_static(dax_region))
		if (a == &dev_attr_available_size.attr
				|| a == &dev_attr_create.attr
				|| a == &dev_attr_seed.attr
				|| a == &dev_attr_delete.attr)
			return 0;
	return a->mode;
}

static struct attribute *dax_region_attributes[] = {
	&dev_attr_available_size.attr,
	&dev_attr_region_size.attr,
	&dev_attr_region_align.attr,
	&dev_attr_create.attr,
	&dev_attr_seed.attr,
	&dev_attr_delete.attr,
	&dev_attr_id.attr,
	NULL,
};

static const struct attribute_group dax_region_attribute_group = {
	.name = "dax_region",
	.attrs = dax_region_attributes,
	.is_visible = dax_region_visible,
};

static const struct attribute_group *dax_region_attribute_groups[] = {
	&dax_region_attribute_group,
	NULL,
};

static void dax_region_unregister(void *region)
{
	struct dax_region *dax_region = region;

	sysfs_remove_groups(&dax_region->dev->kobj,
			dax_region_attribute_groups);
	dax_region_put(dax_region);
}

struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct range *range, int target_node, unsigned int align,
		unsigned long flags)
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

	if (!IS_ALIGNED(range->start, align)
			|| !IS_ALIGNED(range_len(range), align))
		return NULL;

	dax_region = kzalloc(sizeof(*dax_region), GFP_KERNEL);
	if (!dax_region)
		return NULL;

	dev_set_drvdata(parent, dax_region);
	kref_init(&dax_region->kref);
	dax_region->id = region_id;
	dax_region->align = align;
	dax_region->dev = parent;
	dax_region->target_node = target_node;
	ida_init(&dax_region->ida);
	dax_region->res = (struct resource) {
		.start = range->start,
		.end = range->end,
		.flags = IORESOURCE_MEM | flags,
	};

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

static void dax_mapping_release(struct device *dev)
{
	struct dax_mapping *mapping = to_dax_mapping(dev);
	struct device *parent = dev->parent;
	struct dev_dax *dev_dax = to_dev_dax(parent);

	ida_free(&dev_dax->ida, mapping->id);
	kfree(mapping);
	put_device(parent);
}

static void unregister_dax_mapping(void *data)
{
	struct device *dev = data;
	struct dax_mapping *mapping = to_dax_mapping(dev);
	struct dev_dax *dev_dax = to_dev_dax(dev->parent);
	struct dax_region *dax_region = dev_dax->region;

	dev_dbg(dev, "%s\n", __func__);

	device_lock_assert(dax_region->dev);

	dev_dax->ranges[mapping->range_id].mapping = NULL;
	mapping->range_id = -1;

	device_unregister(dev);
}

static struct dev_dax_range *get_dax_range(struct device *dev)
{
	struct dax_mapping *mapping = to_dax_mapping(dev);
	struct dev_dax *dev_dax = to_dev_dax(dev->parent);
	struct dax_region *dax_region = dev_dax->region;

	device_lock(dax_region->dev);
	if (mapping->range_id < 0) {
		device_unlock(dax_region->dev);
		return NULL;
	}

	return &dev_dax->ranges[mapping->range_id];
}

static void put_dax_range(struct dev_dax_range *dax_range)
{
	struct dax_mapping *mapping = dax_range->mapping;
	struct dev_dax *dev_dax = to_dev_dax(mapping->dev.parent);
	struct dax_region *dax_region = dev_dax->region;

	device_unlock(dax_region->dev);
}

static ssize_t start_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax_range *dax_range;
	ssize_t rc;

	dax_range = get_dax_range(dev);
	if (!dax_range)
		return -ENXIO;
	rc = sprintf(buf, "%#llx\n", dax_range->range.start);
	put_dax_range(dax_range);

	return rc;
}
static DEVICE_ATTR(start, 0400, start_show, NULL);

static ssize_t end_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax_range *dax_range;
	ssize_t rc;

	dax_range = get_dax_range(dev);
	if (!dax_range)
		return -ENXIO;
	rc = sprintf(buf, "%#llx\n", dax_range->range.end);
	put_dax_range(dax_range);

	return rc;
}
static DEVICE_ATTR(end, 0400, end_show, NULL);

static ssize_t pgoff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax_range *dax_range;
	ssize_t rc;

	dax_range = get_dax_range(dev);
	if (!dax_range)
		return -ENXIO;
	rc = sprintf(buf, "%#lx\n", dax_range->pgoff);
	put_dax_range(dax_range);

	return rc;
}
static DEVICE_ATTR(page_offset, 0400, pgoff_show, NULL);

static struct attribute *dax_mapping_attributes[] = {
	&dev_attr_start.attr,
	&dev_attr_end.attr,
	&dev_attr_page_offset.attr,
	NULL,
};

static const struct attribute_group dax_mapping_attribute_group = {
	.attrs = dax_mapping_attributes,
};

static const struct attribute_group *dax_mapping_attribute_groups[] = {
	&dax_mapping_attribute_group,
	NULL,
};

static struct device_type dax_mapping_type = {
	.release = dax_mapping_release,
	.groups = dax_mapping_attribute_groups,
};

static int devm_register_dax_mapping(struct dev_dax *dev_dax, int range_id)
{
	struct dax_region *dax_region = dev_dax->region;
	struct dax_mapping *mapping;
	struct device *dev;
	int rc;

	device_lock_assert(dax_region->dev);

	if (dev_WARN_ONCE(&dev_dax->dev, !dax_region->dev->driver,
				"region disabled\n"))
		return -ENXIO;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;
	mapping->range_id = range_id;
	mapping->id = ida_alloc(&dev_dax->ida, GFP_KERNEL);
	if (mapping->id < 0) {
		kfree(mapping);
		return -ENOMEM;
	}
	dev_dax->ranges[range_id].mapping = mapping;
	dev = &mapping->dev;
	device_initialize(dev);
	dev->parent = &dev_dax->dev;
	get_device(dev->parent);
	dev->type = &dax_mapping_type;
	dev_set_name(dev, "mapping%d", mapping->id);
	rc = device_add(dev);
	if (rc) {
		put_device(dev);
		return rc;
	}

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dax_mapping,
			dev);
	if (rc)
		return rc;
	return 0;
}

static int alloc_dev_dax_range(struct dev_dax *dev_dax, u64 start,
		resource_size_t size)
{
	struct dax_region *dax_region = dev_dax->region;
	struct resource *res = &dax_region->res;
	struct device *dev = &dev_dax->dev;
	struct dev_dax_range *ranges;
	unsigned long pgoff = 0;
	struct resource *alloc;
	int i, rc;

	device_lock_assert(dax_region->dev);

	/* handle the seed alloc special case */
	if (!size) {
		if (dev_WARN_ONCE(dev, dev_dax->nr_range,
					"0-size allocation must be first\n"))
			return -EBUSY;
		/* nr_range == 0 is elsewhere special cased as 0-size device */
		return 0;
	}

	alloc = __request_region(res, start, size, dev_name(dev), 0);
	if (!alloc)
		return -ENOMEM;

	ranges = krealloc(dev_dax->ranges, sizeof(*ranges)
			* (dev_dax->nr_range + 1), GFP_KERNEL);
	if (!ranges) {
		__release_region(res, alloc->start, resource_size(alloc));
		return -ENOMEM;
	}

	for (i = 0; i < dev_dax->nr_range; i++)
		pgoff += PHYS_PFN(range_len(&ranges[i].range));
	dev_dax->ranges = ranges;
	ranges[dev_dax->nr_range++] = (struct dev_dax_range) {
		.pgoff = pgoff,
		.range = {
			.start = alloc->start,
			.end = alloc->end,
		},
	};

	dev_dbg(dev, "alloc range[%d]: %pa:%pa\n", dev_dax->nr_range - 1,
			&alloc->start, &alloc->end);
	/*
	 * A dev_dax instance must be registered before mapping device
	 * children can be added. Defer to devm_create_dev_dax() to add
	 * the initial mapping device.
	 */
	if (!device_is_registered(&dev_dax->dev))
		return 0;

	rc = devm_register_dax_mapping(dev_dax, dev_dax->nr_range - 1);
	if (rc)
		trim_dev_dax_range(dev_dax);

	return rc;
}

static int adjust_dev_dax_range(struct dev_dax *dev_dax, struct resource *res, resource_size_t size)
{
	int last_range = dev_dax->nr_range - 1;
	struct dev_dax_range *dax_range = &dev_dax->ranges[last_range];
	struct dax_region *dax_region = dev_dax->region;
	bool is_shrink = resource_size(res) > size;
	struct range *range = &dax_range->range;
	struct device *dev = &dev_dax->dev;
	int rc;

	device_lock_assert(dax_region->dev);

	if (dev_WARN_ONCE(dev, !size, "deletion is handled by dev_dax_shrink\n"))
		return -EINVAL;

	rc = adjust_resource(res, range->start, size);
	if (rc)
		return rc;

	*range = (struct range) {
		.start = range->start,
		.end = range->start + size - 1,
	};

	dev_dbg(dev, "%s range[%d]: %#llx:%#llx\n", is_shrink ? "shrink" : "extend",
			last_range, (unsigned long long) range->start,
			(unsigned long long) range->end);

	return 0;
}

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	unsigned long long size;

	device_lock(dev);
	size = dev_dax_size(dev_dax);
	device_unlock(dev);

	return sprintf(buf, "%llu\n", size);
}

static bool alloc_is_aligned(struct dev_dax *dev_dax, resource_size_t size)
{
	/*
	 * The minimum mapping granularity for a device instance is a
	 * single subsection, unless the arch says otherwise.
	 */
	return IS_ALIGNED(size, max_t(unsigned long, dev_dax->align, memremap_compat_align()));
}

static int dev_dax_shrink(struct dev_dax *dev_dax, resource_size_t size)
{
	resource_size_t to_shrink = dev_dax_size(dev_dax) - size;
	struct dax_region *dax_region = dev_dax->region;
	struct device *dev = &dev_dax->dev;
	int i;

	for (i = dev_dax->nr_range - 1; i >= 0; i--) {
		struct range *range = &dev_dax->ranges[i].range;
		struct dax_mapping *mapping = dev_dax->ranges[i].mapping;
		struct resource *adjust = NULL, *res;
		resource_size_t shrink;

		shrink = min_t(u64, to_shrink, range_len(range));
		if (shrink >= range_len(range)) {
			devm_release_action(dax_region->dev,
					unregister_dax_mapping, &mapping->dev);
			trim_dev_dax_range(dev_dax);
			to_shrink -= shrink;
			if (!to_shrink)
				break;
			continue;
		}

		for_each_dax_region_resource(dax_region, res)
			if (strcmp(res->name, dev_name(dev)) == 0
					&& res->start == range->start) {
				adjust = res;
				break;
			}

		if (dev_WARN_ONCE(dev, !adjust || i != dev_dax->nr_range - 1,
					"failed to find matching resource\n"))
			return -ENXIO;
		return adjust_dev_dax_range(dev_dax, adjust, range_len(range)
				- shrink);
	}
	return 0;
}

/*
 * Only allow adjustments that preserve the relative pgoff of existing
 * allocations. I.e. the dev_dax->ranges array is ordered by increasing pgoff.
 */
static bool adjust_ok(struct dev_dax *dev_dax, struct resource *res)
{
	struct dev_dax_range *last;
	int i;

	if (dev_dax->nr_range == 0)
		return false;
	if (strcmp(res->name, dev_name(&dev_dax->dev)) != 0)
		return false;
	last = &dev_dax->ranges[dev_dax->nr_range - 1];
	if (last->range.start != res->start || last->range.end != res->end)
		return false;
	for (i = 0; i < dev_dax->nr_range - 1; i++) {
		struct dev_dax_range *dax_range = &dev_dax->ranges[i];

		if (dax_range->pgoff > last->pgoff)
			return false;
	}

	return true;
}

static ssize_t dev_dax_resize(struct dax_region *dax_region,
		struct dev_dax *dev_dax, resource_size_t size)
{
	resource_size_t avail = dax_region_avail_size(dax_region), to_alloc;
	resource_size_t dev_size = dev_dax_size(dev_dax);
	struct resource *region_res = &dax_region->res;
	struct device *dev = &dev_dax->dev;
	struct resource *res, *first;
	resource_size_t alloc = 0;
	int rc;

	if (dev->driver)
		return -EBUSY;
	if (size == dev_size)
		return 0;
	if (size > dev_size && size - dev_size > avail)
		return -ENOSPC;
	if (size < dev_size)
		return dev_dax_shrink(dev_dax, size);

	to_alloc = size - dev_size;
	if (dev_WARN_ONCE(dev, !alloc_is_aligned(dev_dax, to_alloc),
			"resize of %pa misaligned\n", &to_alloc))
		return -ENXIO;

	/*
	 * Expand the device into the unused portion of the region. This
	 * may involve adjusting the end of an existing resource, or
	 * allocating a new resource.
	 */
retry:
	first = region_res->child;
	if (!first)
		return alloc_dev_dax_range(dev_dax, dax_region->res.start, to_alloc);

	rc = -ENOSPC;
	for (res = first; res; res = res->sibling) {
		struct resource *next = res->sibling;

		/* space at the beginning of the region */
		if (res == first && res->start > dax_region->res.start) {
			alloc = min(res->start - dax_region->res.start, to_alloc);
			rc = alloc_dev_dax_range(dev_dax, dax_region->res.start, alloc);
			break;
		}

		alloc = 0;
		/* space between allocations */
		if (next && next->start > res->end + 1)
			alloc = min(next->start - (res->end + 1), to_alloc);

		/* space at the end of the region */
		if (!alloc && !next && res->end < region_res->end)
			alloc = min(region_res->end - res->end, to_alloc);

		if (!alloc)
			continue;

		if (adjust_ok(dev_dax, res)) {
			rc = adjust_dev_dax_range(dev_dax, res, resource_size(res) + alloc);
			break;
		}
		rc = alloc_dev_dax_range(dev_dax, res->end + 1, alloc);
		break;
	}
	if (rc)
		return rc;
	to_alloc -= alloc;
	if (to_alloc)
		goto retry;
	return 0;
}

static ssize_t size_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	ssize_t rc;
	unsigned long long val;
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;

	rc = kstrtoull(buf, 0, &val);
	if (rc)
		return rc;

	if (!alloc_is_aligned(dev_dax, val)) {
		dev_dbg(dev, "%s: size: %lld misaligned\n", __func__, val);
		return -EINVAL;
	}

	device_lock(dax_region->dev);
	if (!dax_region->dev->driver) {
		device_unlock(dax_region->dev);
		return -ENXIO;
	}
	device_lock(dev);
	rc = dev_dax_resize(dax_region, dev_dax, val);
	device_unlock(dev);
	device_unlock(dax_region->dev);

	return rc == 0 ? len : rc;
}
static DEVICE_ATTR_RW(size);

static ssize_t range_parse(const char *opt, size_t len, struct range *range)
{
	unsigned long long addr = 0;
	char *start, *end, *str;
	ssize_t rc = -EINVAL;

	str = kstrdup(opt, GFP_KERNEL);
	if (!str)
		return rc;

	end = str;
	start = strsep(&end, "-");
	if (!start || !end)
		goto err;

	rc = kstrtoull(start, 16, &addr);
	if (rc)
		goto err;
	range->start = addr;

	rc = kstrtoull(end, 16, &addr);
	if (rc)
		goto err;
	range->end = addr;

err:
	kfree(str);
	return rc;
}

static ssize_t mapping_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	size_t to_alloc;
	struct range r;
	ssize_t rc;

	rc = range_parse(buf, len, &r);
	if (rc)
		return rc;

	rc = -ENXIO;
	device_lock(dax_region->dev);
	if (!dax_region->dev->driver) {
		device_unlock(dax_region->dev);
		return rc;
	}
	device_lock(dev);

	to_alloc = range_len(&r);
	if (alloc_is_aligned(dev_dax, to_alloc))
		rc = alloc_dev_dax_range(dev_dax, r.start, to_alloc);
	device_unlock(dev);
	device_unlock(dax_region->dev);

	return rc == 0 ? len : rc;
}
static DEVICE_ATTR_WO(mapping);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);

	return sprintf(buf, "%d\n", dev_dax->align);
}

static ssize_t dev_dax_validate_align(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	int i;

	for (i = 0; i < dev_dax->nr_range; i++) {
		size_t len = range_len(&dev_dax->ranges[i].range);

		if (!alloc_is_aligned(dev_dax, len)) {
			dev_dbg(dev, "%s: align %u invalid for range %d\n",
				__func__, dev_dax->align, i);
			return -EINVAL;
		}
	}

	return 0;
}

static ssize_t align_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	unsigned long val, align_save;
	ssize_t rc;

	rc = kstrtoul(buf, 0, &val);
	if (rc)
		return -ENXIO;

	if (!dax_align_valid(val))
		return -EINVAL;

	device_lock(dax_region->dev);
	if (!dax_region->dev->driver) {
		device_unlock(dax_region->dev);
		return -ENXIO;
	}

	device_lock(dev);
	if (dev->driver) {
		rc = -EBUSY;
		goto out_unlock;
	}

	align_save = dev_dax->align;
	dev_dax->align = val;
	rc = dev_dax_validate_align(dev_dax);
	if (rc)
		dev_dax->align = align_save;
out_unlock:
	device_unlock(dev);
	device_unlock(dax_region->dev);
	return rc == 0 ? len : rc;
}
static DEVICE_ATTR_RW(align);

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

static ssize_t resource_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	unsigned long long start;

	if (dev_dax->nr_range < 1)
		start = dax_region->res.start;
	else
		start = dev_dax->ranges[0].range.start;

	return sprintf(buf, "%#llx\n", start);
}
static DEVICE_ATTR(resource, 0400, resource_show, NULL);

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

static ssize_t numa_node_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev_to_node(dev));
}
static DEVICE_ATTR_RO(numa_node);

static umode_t dev_dax_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;

	if (a == &dev_attr_target_node.attr && dev_dax_target_node(dev_dax) < 0)
		return 0;
	if (a == &dev_attr_numa_node.attr && !IS_ENABLED(CONFIG_NUMA))
		return 0;
	if (a == &dev_attr_mapping.attr && is_static(dax_region))
		return 0;
	if ((a == &dev_attr_align.attr ||
	     a == &dev_attr_size.attr) && is_static(dax_region))
		return 0444;
	return a->mode;
}

static struct attribute *dev_dax_attributes[] = {
	&dev_attr_modalias.attr,
	&dev_attr_size.attr,
	&dev_attr_mapping.attr,
	&dev_attr_target_node.attr,
	&dev_attr_align.attr,
	&dev_attr_resource.attr,
	&dev_attr_numa_node.attr,
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

static void dev_dax_release(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_device *dax_dev = dev_dax->dax_dev;

	put_dax(dax_dev);
	free_dev_dax_id(dev_dax);
	kfree(dev_dax->pgmap);
	kfree(dev_dax);
}

static const struct device_type dev_dax_type = {
	.release = dev_dax_release,
	.groups = dax_attribute_groups,
};

struct dev_dax *devm_create_dev_dax(struct dev_dax_data *data)
{
	struct dax_region *dax_region = data->dax_region;
	struct device *parent = dax_region->dev;
	struct dax_device *dax_dev;
	struct dev_dax *dev_dax;
	struct inode *inode;
	struct device *dev;
	int rc;

	dev_dax = kzalloc(sizeof(*dev_dax), GFP_KERNEL);
	if (!dev_dax)
		return ERR_PTR(-ENOMEM);

	dev_dax->region = dax_region;
	if (is_static(dax_region)) {
		if (dev_WARN_ONCE(parent, data->id < 0,
				"dynamic id specified to static region\n")) {
			rc = -EINVAL;
			goto err_id;
		}

		dev_dax->id = data->id;
	} else {
		if (dev_WARN_ONCE(parent, data->id >= 0,
				"static id specified to dynamic region\n")) {
			rc = -EINVAL;
			goto err_id;
		}

		rc = alloc_dev_dax_id(dev_dax);
		if (rc < 0)
			goto err_id;
	}

	dev = &dev_dax->dev;
	device_initialize(dev);
	dev_set_name(dev, "dax%d.%d", dax_region->id, dev_dax->id);

	rc = alloc_dev_dax_range(dev_dax, dax_region->res.start, data->size);
	if (rc)
		goto err_range;

	if (data->pgmap) {
		dev_WARN_ONCE(parent, !is_static(dax_region),
			"custom dev_pagemap requires a static dax_region\n");

		dev_dax->pgmap = kmemdup(data->pgmap,
				sizeof(struct dev_pagemap), GFP_KERNEL);
		if (!dev_dax->pgmap) {
			rc = -ENOMEM;
			goto err_pgmap;
		}
	}

	/*
	 * No dax_operations since there is no access to this device outside of
	 * mmap of the resulting character device.
	 */
	dax_dev = alloc_dax(dev_dax, NULL);
	if (IS_ERR(dax_dev)) {
		rc = PTR_ERR(dax_dev);
		goto err_alloc_dax;
	}
	set_dax_synchronous(dax_dev);
	set_dax_nocache(dax_dev);
	set_dax_nomc(dax_dev);

	/* a device_dax instance is dead while the driver is not attached */
	kill_dax(dax_dev);

	dev_dax->dax_dev = dax_dev;
	dev_dax->target_node = dax_region->target_node;
	dev_dax->align = dax_region->align;
	ida_init(&dev_dax->ida);

	inode = dax_inode(dax_dev);
	dev->devt = inode->i_rdev;
	dev->bus = &dax_bus_type;
	dev->parent = parent;
	dev->type = &dev_dax_type;

	rc = device_add(dev);
	if (rc) {
		kill_dev_dax(dev_dax);
		put_device(dev);
		return ERR_PTR(rc);
	}

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dev_dax, dev);
	if (rc)
		return ERR_PTR(rc);

	/* register mapping device for the initial allocation range */
	if (dev_dax->nr_range && range_len(&dev_dax->ranges[0].range)) {
		rc = devm_register_dax_mapping(dev_dax, 0);
		if (rc)
			return ERR_PTR(rc);
	}

	return dev_dax;

err_alloc_dax:
	kfree(dev_dax->pgmap);
err_pgmap:
	free_dev_dax_ranges(dev_dax);
err_range:
	free_dev_dax_id(dev_dax);
err_id:
	kfree(dev_dax);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_create_dev_dax);

int __dax_driver_register(struct dax_device_driver *dax_drv,
		struct module *module, const char *mod_name)
{
	struct device_driver *drv = &dax_drv->drv;

	/*
	 * dax_bus_probe() calls dax_drv->probe() unconditionally.
	 * So better be safe than sorry and ensure it is provided.
	 */
	if (!dax_drv->probe)
		return -EINVAL;

	INIT_LIST_HEAD(&dax_drv->ids);
	drv->owner = module;
	drv->name = mod_name;
	drv->mod_name = mod_name;
	drv->bus = &dax_bus_type;

	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(__dax_driver_register);

void dax_driver_unregister(struct dax_device_driver *dax_drv)
{
	struct device_driver *drv = &dax_drv->drv;
	struct dax_id *dax_id, *_id;

	mutex_lock(&dax_bus_lock);
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
	return bus_register(&dax_bus_type);
}

void __exit dax_bus_exit(void)
{
	bus_unregister(&dax_bus_type);
}
