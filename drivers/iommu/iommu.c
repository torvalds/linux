// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)    "iommu: " fmt

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/iommu.h>
#include <linux/idr.h>
#include <linux/notifier.h>
#include <linux/err.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/property.h>
#include <linux/fsl/mc.h>
#include <linux/module.h>
#include <trace/events/iommu.h>

static struct kset *iommu_group_kset;
static DEFINE_IDA(iommu_group_ida);

static unsigned int iommu_def_domain_type __read_mostly;
static bool iommu_dma_strict __read_mostly = true;
static u32 iommu_cmd_line __read_mostly;

struct iommu_group {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct list_head devices;
	struct mutex mutex;
	struct blocking_notifier_head notifier;
	void *iommu_data;
	void (*iommu_data_release)(void *iommu_data);
	char *name;
	int id;
	struct iommu_domain *default_domain;
	struct iommu_domain *domain;
	struct list_head entry;
};

struct group_device {
	struct list_head list;
	struct device *dev;
	char *name;
};

struct iommu_group_attribute {
	struct attribute attr;
	ssize_t (*show)(struct iommu_group *group, char *buf);
	ssize_t (*store)(struct iommu_group *group,
			 const char *buf, size_t count);
};

static const char * const iommu_group_resv_type_string[] = {
	[IOMMU_RESV_DIRECT]			= "direct",
	[IOMMU_RESV_DIRECT_RELAXABLE]		= "direct-relaxable",
	[IOMMU_RESV_RESERVED]			= "reserved",
	[IOMMU_RESV_MSI]			= "msi",
	[IOMMU_RESV_SW_MSI]			= "msi",
};

#define IOMMU_CMD_LINE_DMA_API		BIT(0)

static void iommu_set_cmd_line_dma_api(void)
{
	iommu_cmd_line |= IOMMU_CMD_LINE_DMA_API;
}

static bool iommu_cmd_line_dma_api(void)
{
	return !!(iommu_cmd_line & IOMMU_CMD_LINE_DMA_API);
}

static int iommu_alloc_default_domain(struct iommu_group *group,
				      struct device *dev);
static struct iommu_domain *__iommu_domain_alloc(struct bus_type *bus,
						 unsigned type);
static int __iommu_attach_device(struct iommu_domain *domain,
				 struct device *dev);
static int __iommu_attach_group(struct iommu_domain *domain,
				struct iommu_group *group);
static void __iommu_detach_group(struct iommu_domain *domain,
				 struct iommu_group *group);
static int iommu_create_device_direct_mappings(struct iommu_group *group,
					       struct device *dev);
static struct iommu_group *iommu_group_get_for_dev(struct device *dev);

#define IOMMU_GROUP_ATTR(_name, _mode, _show, _store)		\
struct iommu_group_attribute iommu_group_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

#define to_iommu_group_attr(_attr)	\
	container_of(_attr, struct iommu_group_attribute, attr)
#define to_iommu_group(_kobj)		\
	container_of(_kobj, struct iommu_group, kobj)

static LIST_HEAD(iommu_device_list);
static DEFINE_SPINLOCK(iommu_device_lock);

/*
 * Use a function instead of an array here because the domain-type is a
 * bit-field, so an array would waste memory.
 */
static const char *iommu_domain_type_str(unsigned int t)
{
	switch (t) {
	case IOMMU_DOMAIN_BLOCKED:
		return "Blocked";
	case IOMMU_DOMAIN_IDENTITY:
		return "Passthrough";
	case IOMMU_DOMAIN_UNMANAGED:
		return "Unmanaged";
	case IOMMU_DOMAIN_DMA:
		return "Translated";
	default:
		return "Unknown";
	}
}

static int __init iommu_subsys_init(void)
{
	bool cmd_line = iommu_cmd_line_dma_api();

	if (!cmd_line) {
		if (IS_ENABLED(CONFIG_IOMMU_DEFAULT_PASSTHROUGH))
			iommu_set_default_passthrough(false);
		else
			iommu_set_default_translated(false);

		if (iommu_default_passthrough() && mem_encrypt_active()) {
			pr_info("Memory encryption detected - Disabling default IOMMU Passthrough\n");
			iommu_set_default_translated(false);
		}
	}

	pr_info("Default domain type: %s %s\n",
		iommu_domain_type_str(iommu_def_domain_type),
		cmd_line ? "(set via kernel command line)" : "");

	return 0;
}
subsys_initcall(iommu_subsys_init);

int iommu_device_register(struct iommu_device *iommu)
{
	spin_lock(&iommu_device_lock);
	list_add_tail(&iommu->list, &iommu_device_list);
	spin_unlock(&iommu_device_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(iommu_device_register);

void iommu_device_unregister(struct iommu_device *iommu)
{
	spin_lock(&iommu_device_lock);
	list_del(&iommu->list);
	spin_unlock(&iommu_device_lock);
}
EXPORT_SYMBOL_GPL(iommu_device_unregister);

static struct dev_iommu *dev_iommu_get(struct device *dev)
{
	struct dev_iommu *param = dev->iommu;

	if (param)
		return param;

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return NULL;

	mutex_init(&param->lock);
	dev->iommu = param;
	return param;
}

static void dev_iommu_free(struct device *dev)
{
	iommu_fwspec_free(dev);
	kfree(dev->iommu);
	dev->iommu = NULL;
}

static int __iommu_probe_device(struct device *dev, struct list_head *group_list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_device *iommu_dev;
	struct iommu_group *group;
	int ret;

	if (!ops)
		return -ENODEV;

	if (!dev_iommu_get(dev))
		return -ENOMEM;

	if (!try_module_get(ops->owner)) {
		ret = -EINVAL;
		goto err_free;
	}

	iommu_dev = ops->probe_device(dev);
	if (IS_ERR(iommu_dev)) {
		ret = PTR_ERR(iommu_dev);
		goto out_module_put;
	}

	dev->iommu->iommu_dev = iommu_dev;

	group = iommu_group_get_for_dev(dev);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_release;
	}
	iommu_group_put(group);

	if (group_list && !group->default_domain && list_empty(&group->entry))
		list_add_tail(&group->entry, group_list);

	iommu_device_link(iommu_dev, dev);

	return 0;

out_release:
	ops->release_device(dev);

out_module_put:
	module_put(ops->owner);

err_free:
	dev_iommu_free(dev);

	return ret;
}

int iommu_probe_device(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_group *group;
	int ret;

	ret = __iommu_probe_device(dev, NULL);
	if (ret)
		goto err_out;

	group = iommu_group_get(dev);
	if (!group)
		goto err_release;

	/*
	 * Try to allocate a default domain - needs support from the
	 * IOMMU driver. There are still some drivers which don't
	 * support default domains, so the return value is not yet
	 * checked.
	 */
	iommu_alloc_default_domain(group, dev);

	if (group->default_domain) {
		ret = __iommu_attach_device(group->default_domain, dev);
		if (ret) {
			iommu_group_put(group);
			goto err_release;
		}
	}

	iommu_create_device_direct_mappings(group, dev);

	iommu_group_put(group);

	if (ops->probe_finalize)
		ops->probe_finalize(dev);

	return 0;

err_release:
	iommu_release_device(dev);

err_out:
	return ret;

}

void iommu_release_device(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (!dev->iommu)
		return;

	iommu_device_unlink(dev->iommu->iommu_dev, dev);

	ops->release_device(dev);

	iommu_group_remove_device(dev);
	module_put(ops->owner);
	dev_iommu_free(dev);
}

static int __init iommu_set_def_domain_type(char *str)
{
	bool pt;
	int ret;

	ret = kstrtobool(str, &pt);
	if (ret)
		return ret;

	if (pt)
		iommu_set_default_passthrough(true);
	else
		iommu_set_default_translated(true);

	return 0;
}
early_param("iommu.passthrough", iommu_set_def_domain_type);

static int __init iommu_dma_setup(char *str)
{
	return kstrtobool(str, &iommu_dma_strict);
}
early_param("iommu.strict", iommu_dma_setup);

static ssize_t iommu_group_attr_show(struct kobject *kobj,
				     struct attribute *__attr, char *buf)
{
	struct iommu_group_attribute *attr = to_iommu_group_attr(__attr);
	struct iommu_group *group = to_iommu_group(kobj);
	ssize_t ret = -EIO;

	if (attr->show)
		ret = attr->show(group, buf);
	return ret;
}

static ssize_t iommu_group_attr_store(struct kobject *kobj,
				      struct attribute *__attr,
				      const char *buf, size_t count)
{
	struct iommu_group_attribute *attr = to_iommu_group_attr(__attr);
	struct iommu_group *group = to_iommu_group(kobj);
	ssize_t ret = -EIO;

	if (attr->store)
		ret = attr->store(group, buf, count);
	return ret;
}

static const struct sysfs_ops iommu_group_sysfs_ops = {
	.show = iommu_group_attr_show,
	.store = iommu_group_attr_store,
};

static int iommu_group_create_file(struct iommu_group *group,
				   struct iommu_group_attribute *attr)
{
	return sysfs_create_file(&group->kobj, &attr->attr);
}

static void iommu_group_remove_file(struct iommu_group *group,
				    struct iommu_group_attribute *attr)
{
	sysfs_remove_file(&group->kobj, &attr->attr);
}

static ssize_t iommu_group_show_name(struct iommu_group *group, char *buf)
{
	return sprintf(buf, "%s\n", group->name);
}

/**
 * iommu_insert_resv_region - Insert a new region in the
 * list of reserved regions.
 * @new: new region to insert
 * @regions: list of regions
 *
 * Elements are sorted by start address and overlapping segments
 * of the same type are merged.
 */
static int iommu_insert_resv_region(struct iommu_resv_region *new,
				    struct list_head *regions)
{
	struct iommu_resv_region *iter, *tmp, *nr, *top;
	LIST_HEAD(stack);

	nr = iommu_alloc_resv_region(new->start, new->length,
				     new->prot, new->type);
	if (!nr)
		return -ENOMEM;

	/* First add the new element based on start address sorting */
	list_for_each_entry(iter, regions, list) {
		if (nr->start < iter->start ||
		    (nr->start == iter->start && nr->type <= iter->type))
			break;
	}
	list_add_tail(&nr->list, &iter->list);

	/* Merge overlapping segments of type nr->type in @regions, if any */
	list_for_each_entry_safe(iter, tmp, regions, list) {
		phys_addr_t top_end, iter_end = iter->start + iter->length - 1;

		/* no merge needed on elements of different types than @new */
		if (iter->type != new->type) {
			list_move_tail(&iter->list, &stack);
			continue;
		}

		/* look for the last stack element of same type as @iter */
		list_for_each_entry_reverse(top, &stack, list)
			if (top->type == iter->type)
				goto check_overlap;

		list_move_tail(&iter->list, &stack);
		continue;

check_overlap:
		top_end = top->start + top->length - 1;

		if (iter->start > top_end + 1) {
			list_move_tail(&iter->list, &stack);
		} else {
			top->length = max(top_end, iter_end) - top->start + 1;
			list_del(&iter->list);
			kfree(iter);
		}
	}
	list_splice(&stack, regions);
	return 0;
}

static int
iommu_insert_device_resv_regions(struct list_head *dev_resv_regions,
				 struct list_head *group_resv_regions)
{
	struct iommu_resv_region *entry;
	int ret = 0;

	list_for_each_entry(entry, dev_resv_regions, list) {
		ret = iommu_insert_resv_region(entry, group_resv_regions);
		if (ret)
			break;
	}
	return ret;
}

int iommu_get_group_resv_regions(struct iommu_group *group,
				 struct list_head *head)
{
	struct group_device *device;
	int ret = 0;

	mutex_lock(&group->mutex);
	list_for_each_entry(device, &group->devices, list) {
		struct list_head dev_resv_regions;

		INIT_LIST_HEAD(&dev_resv_regions);
		iommu_get_resv_regions(device->dev, &dev_resv_regions);
		ret = iommu_insert_device_resv_regions(&dev_resv_regions, head);
		iommu_put_resv_regions(device->dev, &dev_resv_regions);
		if (ret)
			break;
	}
	mutex_unlock(&group->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_get_group_resv_regions);

static ssize_t iommu_group_show_resv_regions(struct iommu_group *group,
					     char *buf)
{
	struct iommu_resv_region *region, *next;
	struct list_head group_resv_regions;
	char *str = buf;

	INIT_LIST_HEAD(&group_resv_regions);
	iommu_get_group_resv_regions(group, &group_resv_regions);

	list_for_each_entry_safe(region, next, &group_resv_regions, list) {
		str += sprintf(str, "0x%016llx 0x%016llx %s\n",
			       (long long int)region->start,
			       (long long int)(region->start +
						region->length - 1),
			       iommu_group_resv_type_string[region->type]);
		kfree(region);
	}

	return (str - buf);
}

static ssize_t iommu_group_show_type(struct iommu_group *group,
				     char *buf)
{
	char *type = "unknown\n";

	if (group->default_domain) {
		switch (group->default_domain->type) {
		case IOMMU_DOMAIN_BLOCKED:
			type = "blocked\n";
			break;
		case IOMMU_DOMAIN_IDENTITY:
			type = "identity\n";
			break;
		case IOMMU_DOMAIN_UNMANAGED:
			type = "unmanaged\n";
			break;
		case IOMMU_DOMAIN_DMA:
			type = "DMA\n";
			break;
		}
	}
	strcpy(buf, type);

	return strlen(type);
}

static IOMMU_GROUP_ATTR(name, S_IRUGO, iommu_group_show_name, NULL);

static IOMMU_GROUP_ATTR(reserved_regions, 0444,
			iommu_group_show_resv_regions, NULL);

static IOMMU_GROUP_ATTR(type, 0444, iommu_group_show_type, NULL);

static void iommu_group_release(struct kobject *kobj)
{
	struct iommu_group *group = to_iommu_group(kobj);

	pr_debug("Releasing group %d\n", group->id);

	if (group->iommu_data_release)
		group->iommu_data_release(group->iommu_data);

	ida_simple_remove(&iommu_group_ida, group->id);

	if (group->default_domain)
		iommu_domain_free(group->default_domain);

	kfree(group->name);
	kfree(group);
}

static struct kobj_type iommu_group_ktype = {
	.sysfs_ops = &iommu_group_sysfs_ops,
	.release = iommu_group_release,
};

/**
 * iommu_group_alloc - Allocate a new group
 *
 * This function is called by an iommu driver to allocate a new iommu
 * group.  The iommu group represents the minimum granularity of the iommu.
 * Upon successful return, the caller holds a reference to the supplied
 * group in order to hold the group until devices are added.  Use
 * iommu_group_put() to release this extra reference count, allowing the
 * group to be automatically reclaimed once it has no devices or external
 * references.
 */
struct iommu_group *iommu_group_alloc(void)
{
	struct iommu_group *group;
	int ret;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	group->kobj.kset = iommu_group_kset;
	mutex_init(&group->mutex);
	INIT_LIST_HEAD(&group->devices);
	INIT_LIST_HEAD(&group->entry);
	BLOCKING_INIT_NOTIFIER_HEAD(&group->notifier);

	ret = ida_simple_get(&iommu_group_ida, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(group);
		return ERR_PTR(ret);
	}
	group->id = ret;

	ret = kobject_init_and_add(&group->kobj, &iommu_group_ktype,
				   NULL, "%d", group->id);
	if (ret) {
		ida_simple_remove(&iommu_group_ida, group->id);
		kobject_put(&group->kobj);
		return ERR_PTR(ret);
	}

	group->devices_kobj = kobject_create_and_add("devices", &group->kobj);
	if (!group->devices_kobj) {
		kobject_put(&group->kobj); /* triggers .release & free */
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * The devices_kobj holds a reference on the group kobject, so
	 * as long as that exists so will the group.  We can therefore
	 * use the devices_kobj for reference counting.
	 */
	kobject_put(&group->kobj);

	ret = iommu_group_create_file(group,
				      &iommu_group_attr_reserved_regions);
	if (ret)
		return ERR_PTR(ret);

	ret = iommu_group_create_file(group, &iommu_group_attr_type);
	if (ret)
		return ERR_PTR(ret);

	pr_debug("Allocated group %d\n", group->id);

	return group;
}
EXPORT_SYMBOL_GPL(iommu_group_alloc);

struct iommu_group *iommu_group_get_by_id(int id)
{
	struct kobject *group_kobj;
	struct iommu_group *group;
	const char *name;

	if (!iommu_group_kset)
		return NULL;

	name = kasprintf(GFP_KERNEL, "%d", id);
	if (!name)
		return NULL;

	group_kobj = kset_find_obj(iommu_group_kset, name);
	kfree(name);

	if (!group_kobj)
		return NULL;

	group = container_of(group_kobj, struct iommu_group, kobj);
	BUG_ON(group->id != id);

	kobject_get(group->devices_kobj);
	kobject_put(&group->kobj);

	return group;
}
EXPORT_SYMBOL_GPL(iommu_group_get_by_id);

/**
 * iommu_group_get_iommudata - retrieve iommu_data registered for a group
 * @group: the group
 *
 * iommu drivers can store data in the group for use when doing iommu
 * operations.  This function provides a way to retrieve it.  Caller
 * should hold a group reference.
 */
void *iommu_group_get_iommudata(struct iommu_group *group)
{
	return group->iommu_data;
}
EXPORT_SYMBOL_GPL(iommu_group_get_iommudata);

/**
 * iommu_group_set_iommudata - set iommu_data for a group
 * @group: the group
 * @iommu_data: new data
 * @release: release function for iommu_data
 *
 * iommu drivers can store data in the group for use when doing iommu
 * operations.  This function provides a way to set the data after
 * the group has been allocated.  Caller should hold a group reference.
 */
void iommu_group_set_iommudata(struct iommu_group *group, void *iommu_data,
			       void (*release)(void *iommu_data))
{
	group->iommu_data = iommu_data;
	group->iommu_data_release = release;
}
EXPORT_SYMBOL_GPL(iommu_group_set_iommudata);

/**
 * iommu_group_set_name - set name for a group
 * @group: the group
 * @name: name
 *
 * Allow iommu driver to set a name for a group.  When set it will
 * appear in a name attribute file under the group in sysfs.
 */
int iommu_group_set_name(struct iommu_group *group, const char *name)
{
	int ret;

	if (group->name) {
		iommu_group_remove_file(group, &iommu_group_attr_name);
		kfree(group->name);
		group->name = NULL;
		if (!name)
			return 0;
	}

	group->name = kstrdup(name, GFP_KERNEL);
	if (!group->name)
		return -ENOMEM;

	ret = iommu_group_create_file(group, &iommu_group_attr_name);
	if (ret) {
		kfree(group->name);
		group->name = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iommu_group_set_name);

static int iommu_create_device_direct_mappings(struct iommu_group *group,
					       struct device *dev)
{
	struct iommu_domain *domain = group->default_domain;
	struct iommu_resv_region *entry;
	struct list_head mappings;
	unsigned long pg_size;
	int ret = 0;

	if (!domain || domain->type != IOMMU_DOMAIN_DMA)
		return 0;

	BUG_ON(!domain->pgsize_bitmap);

	pg_size = 1UL << __ffs(domain->pgsize_bitmap);
	INIT_LIST_HEAD(&mappings);

	iommu_get_resv_regions(dev, &mappings);

	/* We need to consider overlapping regions for different devices */
	list_for_each_entry(entry, &mappings, list) {
		dma_addr_t start, end, addr;

		if (domain->ops->apply_resv_region)
			domain->ops->apply_resv_region(dev, domain, entry);

		start = ALIGN(entry->start, pg_size);
		end   = ALIGN(entry->start + entry->length, pg_size);

		if (entry->type != IOMMU_RESV_DIRECT &&
		    entry->type != IOMMU_RESV_DIRECT_RELAXABLE)
			continue;

		for (addr = start; addr < end; addr += pg_size) {
			phys_addr_t phys_addr;

			phys_addr = iommu_iova_to_phys(domain, addr);
			if (phys_addr)
				continue;

			ret = iommu_map(domain, addr, addr, pg_size, entry->prot);
			if (ret)
				goto out;
		}

	}

	iommu_flush_iotlb_all(domain);

out:
	iommu_put_resv_regions(dev, &mappings);

	return ret;
}

static bool iommu_is_attach_deferred(struct iommu_domain *domain,
				     struct device *dev)
{
	if (domain->ops->is_attach_deferred)
		return domain->ops->is_attach_deferred(domain, dev);

	return false;
}

/**
 * iommu_group_add_device - add a device to an iommu group
 * @group: the group into which to add the device (reference should be held)
 * @dev: the device
 *
 * This function is called by an iommu driver to add a device into a
 * group.  Adding a device increments the group reference count.
 */
int iommu_group_add_device(struct iommu_group *group, struct device *dev)
{
	int ret, i = 0;
	struct group_device *device;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	device->dev = dev;

	ret = sysfs_create_link(&dev->kobj, &group->kobj, "iommu_group");
	if (ret)
		goto err_free_device;

	device->name = kasprintf(GFP_KERNEL, "%s", kobject_name(&dev->kobj));
rename:
	if (!device->name) {
		ret = -ENOMEM;
		goto err_remove_link;
	}

	ret = sysfs_create_link_nowarn(group->devices_kobj,
				       &dev->kobj, device->name);
	if (ret) {
		if (ret == -EEXIST && i >= 0) {
			/*
			 * Account for the slim chance of collision
			 * and append an instance to the name.
			 */
			kfree(device->name);
			device->name = kasprintf(GFP_KERNEL, "%s.%d",
						 kobject_name(&dev->kobj), i++);
			goto rename;
		}
		goto err_free_name;
	}

	kobject_get(group->devices_kobj);

	dev->iommu_group = group;

	mutex_lock(&group->mutex);
	list_add_tail(&device->list, &group->devices);
	if (group->domain  && !iommu_is_attach_deferred(group->domain, dev))
		ret = __iommu_attach_device(group->domain, dev);
	mutex_unlock(&group->mutex);
	if (ret)
		goto err_put_group;

	/* Notify any listeners about change to group. */
	blocking_notifier_call_chain(&group->notifier,
				     IOMMU_GROUP_NOTIFY_ADD_DEVICE, dev);

	trace_add_device_to_group(group->id, dev);

	dev_info(dev, "Adding to iommu group %d\n", group->id);

	return 0;

err_put_group:
	mutex_lock(&group->mutex);
	list_del(&device->list);
	mutex_unlock(&group->mutex);
	dev->iommu_group = NULL;
	kobject_put(group->devices_kobj);
	sysfs_remove_link(group->devices_kobj, device->name);
err_free_name:
	kfree(device->name);
err_remove_link:
	sysfs_remove_link(&dev->kobj, "iommu_group");
err_free_device:
	kfree(device);
	dev_err(dev, "Failed to add to iommu group %d: %d\n", group->id, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_group_add_device);

/**
 * iommu_group_remove_device - remove a device from it's current group
 * @dev: device to be removed
 *
 * This function is called by an iommu driver to remove the device from
 * it's current group.  This decrements the iommu group reference count.
 */
void iommu_group_remove_device(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;
	struct group_device *tmp_device, *device = NULL;

	dev_info(dev, "Removing from iommu group %d\n", group->id);

	/* Pre-notify listeners that a device is being removed. */
	blocking_notifier_call_chain(&group->notifier,
				     IOMMU_GROUP_NOTIFY_DEL_DEVICE, dev);

	mutex_lock(&group->mutex);
	list_for_each_entry(tmp_device, &group->devices, list) {
		if (tmp_device->dev == dev) {
			device = tmp_device;
			list_del(&device->list);
			break;
		}
	}
	mutex_unlock(&group->mutex);

	if (!device)
		return;

	sysfs_remove_link(group->devices_kobj, device->name);
	sysfs_remove_link(&dev->kobj, "iommu_group");

	trace_remove_device_from_group(group->id, dev);

	kfree(device->name);
	kfree(device);
	dev->iommu_group = NULL;
	kobject_put(group->devices_kobj);
}
EXPORT_SYMBOL_GPL(iommu_group_remove_device);

static int iommu_group_device_count(struct iommu_group *group)
{
	struct group_device *entry;
	int ret = 0;

	list_for_each_entry(entry, &group->devices, list)
		ret++;

	return ret;
}

/**
 * iommu_group_for_each_dev - iterate over each device in the group
 * @group: the group
 * @data: caller opaque data to be passed to callback function
 * @fn: caller supplied callback function
 *
 * This function is called by group users to iterate over group devices.
 * Callers should hold a reference count to the group during callback.
 * The group->mutex is held across callbacks, which will block calls to
 * iommu_group_add/remove_device.
 */
static int __iommu_group_for_each_dev(struct iommu_group *group, void *data,
				      int (*fn)(struct device *, void *))
{
	struct group_device *device;
	int ret = 0;

	list_for_each_entry(device, &group->devices, list) {
		ret = fn(device->dev, data);
		if (ret)
			break;
	}
	return ret;
}


int iommu_group_for_each_dev(struct iommu_group *group, void *data,
			     int (*fn)(struct device *, void *))
{
	int ret;

	mutex_lock(&group->mutex);
	ret = __iommu_group_for_each_dev(group, data, fn);
	mutex_unlock(&group->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_group_for_each_dev);

/**
 * iommu_group_get - Return the group for a device and increment reference
 * @dev: get the group that this device belongs to
 *
 * This function is called by iommu drivers and users to get the group
 * for the specified device.  If found, the group is returned and the group
 * reference in incremented, else NULL.
 */
struct iommu_group *iommu_group_get(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;

	if (group)
		kobject_get(group->devices_kobj);

	return group;
}
EXPORT_SYMBOL_GPL(iommu_group_get);

/**
 * iommu_group_ref_get - Increment reference on a group
 * @group: the group to use, must not be NULL
 *
 * This function is called by iommu drivers to take additional references on an
 * existing group.  Returns the given group for convenience.
 */
struct iommu_group *iommu_group_ref_get(struct iommu_group *group)
{
	kobject_get(group->devices_kobj);
	return group;
}
EXPORT_SYMBOL_GPL(iommu_group_ref_get);

/**
 * iommu_group_put - Decrement group reference
 * @group: the group to use
 *
 * This function is called by iommu drivers and users to release the
 * iommu group.  Once the reference count is zero, the group is released.
 */
void iommu_group_put(struct iommu_group *group)
{
	if (group)
		kobject_put(group->devices_kobj);
}
EXPORT_SYMBOL_GPL(iommu_group_put);

/**
 * iommu_group_register_notifier - Register a notifier for group changes
 * @group: the group to watch
 * @nb: notifier block to signal
 *
 * This function allows iommu group users to track changes in a group.
 * See include/linux/iommu.h for actions sent via this notifier.  Caller
 * should hold a reference to the group throughout notifier registration.
 */
int iommu_group_register_notifier(struct iommu_group *group,
				  struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&group->notifier, nb);
}
EXPORT_SYMBOL_GPL(iommu_group_register_notifier);

/**
 * iommu_group_unregister_notifier - Unregister a notifier
 * @group: the group to watch
 * @nb: notifier block to signal
 *
 * Unregister a previously registered group notifier block.
 */
int iommu_group_unregister_notifier(struct iommu_group *group,
				    struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&group->notifier, nb);
}
EXPORT_SYMBOL_GPL(iommu_group_unregister_notifier);

/**
 * iommu_register_device_fault_handler() - Register a device fault handler
 * @dev: the device
 * @handler: the fault handler
 * @data: private data passed as argument to the handler
 *
 * When an IOMMU fault event is received, this handler gets called with the
 * fault event and data as argument. The handler should return 0 on success. If
 * the fault is recoverable (IOMMU_FAULT_PAGE_REQ), the consumer should also
 * complete the fault by calling iommu_page_response() with one of the following
 * response code:
 * - IOMMU_PAGE_RESP_SUCCESS: retry the translation
 * - IOMMU_PAGE_RESP_INVALID: terminate the fault
 * - IOMMU_PAGE_RESP_FAILURE: terminate the fault and stop reporting
 *   page faults if possible.
 *
 * Return 0 if the fault handler was installed successfully, or an error.
 */
int iommu_register_device_fault_handler(struct device *dev,
					iommu_dev_fault_handler_t handler,
					void *data)
{
	struct dev_iommu *param = dev->iommu;
	int ret = 0;

	if (!param)
		return -EINVAL;

	mutex_lock(&param->lock);
	/* Only allow one fault handler registered for each device */
	if (param->fault_param) {
		ret = -EBUSY;
		goto done_unlock;
	}

	get_device(dev);
	param->fault_param = kzalloc(sizeof(*param->fault_param), GFP_KERNEL);
	if (!param->fault_param) {
		put_device(dev);
		ret = -ENOMEM;
		goto done_unlock;
	}
	param->fault_param->handler = handler;
	param->fault_param->data = data;
	mutex_init(&param->fault_param->lock);
	INIT_LIST_HEAD(&param->fault_param->faults);

done_unlock:
	mutex_unlock(&param->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_register_device_fault_handler);

/**
 * iommu_unregister_device_fault_handler() - Unregister the device fault handler
 * @dev: the device
 *
 * Remove the device fault handler installed with
 * iommu_register_device_fault_handler().
 *
 * Return 0 on success, or an error.
 */
int iommu_unregister_device_fault_handler(struct device *dev)
{
	struct dev_iommu *param = dev->iommu;
	int ret = 0;

	if (!param)
		return -EINVAL;

	mutex_lock(&param->lock);

	if (!param->fault_param)
		goto unlock;

	/* we cannot unregister handler if there are pending faults */
	if (!list_empty(&param->fault_param->faults)) {
		ret = -EBUSY;
		goto unlock;
	}

	kfree(param->fault_param);
	param->fault_param = NULL;
	put_device(dev);
unlock:
	mutex_unlock(&param->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_unregister_device_fault_handler);

/**
 * iommu_report_device_fault() - Report fault event to device driver
 * @dev: the device
 * @evt: fault event data
 *
 * Called by IOMMU drivers when a fault is detected, typically in a threaded IRQ
 * handler. When this function fails and the fault is recoverable, it is the
 * caller's responsibility to complete the fault.
 *
 * Return 0 on success, or an error.
 */
int iommu_report_device_fault(struct device *dev, struct iommu_fault_event *evt)
{
	struct dev_iommu *param = dev->iommu;
	struct iommu_fault_event *evt_pending = NULL;
	struct iommu_fault_param *fparam;
	int ret = 0;

	if (!param || !evt)
		return -EINVAL;

	/* we only report device fault if there is a handler registered */
	mutex_lock(&param->lock);
	fparam = param->fault_param;
	if (!fparam || !fparam->handler) {
		ret = -EINVAL;
		goto done_unlock;
	}

	if (evt->fault.type == IOMMU_FAULT_PAGE_REQ &&
	    (evt->fault.prm.flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE)) {
		evt_pending = kmemdup(evt, sizeof(struct iommu_fault_event),
				      GFP_KERNEL);
		if (!evt_pending) {
			ret = -ENOMEM;
			goto done_unlock;
		}
		mutex_lock(&fparam->lock);
		list_add_tail(&evt_pending->list, &fparam->faults);
		mutex_unlock(&fparam->lock);
	}

	ret = fparam->handler(&evt->fault, fparam->data);
	if (ret && evt_pending) {
		mutex_lock(&fparam->lock);
		list_del(&evt_pending->list);
		mutex_unlock(&fparam->lock);
		kfree(evt_pending);
	}
done_unlock:
	mutex_unlock(&param->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_report_device_fault);

int iommu_page_response(struct device *dev,
			struct iommu_page_response *msg)
{
	bool needs_pasid;
	int ret = -EINVAL;
	struct iommu_fault_event *evt;
	struct iommu_fault_page_request *prm;
	struct dev_iommu *param = dev->iommu;
	bool has_pasid = msg->flags & IOMMU_PAGE_RESP_PASID_VALID;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);

	if (!domain || !domain->ops->page_response)
		return -ENODEV;

	if (!param || !param->fault_param)
		return -EINVAL;

	if (msg->version != IOMMU_PAGE_RESP_VERSION_1 ||
	    msg->flags & ~IOMMU_PAGE_RESP_PASID_VALID)
		return -EINVAL;

	/* Only send response if there is a fault report pending */
	mutex_lock(&param->fault_param->lock);
	if (list_empty(&param->fault_param->faults)) {
		dev_warn_ratelimited(dev, "no pending PRQ, drop response\n");
		goto done_unlock;
	}
	/*
	 * Check if we have a matching page request pending to respond,
	 * otherwise return -EINVAL
	 */
	list_for_each_entry(evt, &param->fault_param->faults, list) {
		prm = &evt->fault.prm;
		if (prm->grpid != msg->grpid)
			continue;

		/*
		 * If the PASID is required, the corresponding request is
		 * matched using the group ID, the PASID valid bit and the PASID
		 * value. Otherwise only the group ID matches request and
		 * response.
		 */
		needs_pasid = prm->flags & IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID;
		if (needs_pasid && (!has_pasid || msg->pasid != prm->pasid))
			continue;

		if (!needs_pasid && has_pasid) {
			/* No big deal, just clear it. */
			msg->flags &= ~IOMMU_PAGE_RESP_PASID_VALID;
			msg->pasid = 0;
		}

		ret = domain->ops->page_response(dev, evt, msg);
		list_del(&evt->list);
		kfree(evt);
		break;
	}

done_unlock:
	mutex_unlock(&param->fault_param->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_page_response);

/**
 * iommu_group_id - Return ID for a group
 * @group: the group to ID
 *
 * Return the unique ID for the group matching the sysfs group number.
 */
int iommu_group_id(struct iommu_group *group)
{
	return group->id;
}
EXPORT_SYMBOL_GPL(iommu_group_id);

static struct iommu_group *get_pci_alias_group(struct pci_dev *pdev,
					       unsigned long *devfns);

/*
 * To consider a PCI device isolated, we require ACS to support Source
 * Validation, Request Redirection, Completer Redirection, and Upstream
 * Forwarding.  This effectively means that devices cannot spoof their
 * requester ID, requests and completions cannot be redirected, and all
 * transactions are forwarded upstream, even as it passes through a
 * bridge where the target device is downstream.
 */
#define REQ_ACS_FLAGS   (PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF)

/*
 * For multifunction devices which are not isolated from each other, find
 * all the other non-isolated functions and look for existing groups.  For
 * each function, we also need to look for aliases to or from other devices
 * that may already have a group.
 */
static struct iommu_group *get_pci_function_alias_group(struct pci_dev *pdev,
							unsigned long *devfns)
{
	struct pci_dev *tmp = NULL;
	struct iommu_group *group;

	if (!pdev->multifunction || pci_acs_enabled(pdev, REQ_ACS_FLAGS))
		return NULL;

	for_each_pci_dev(tmp) {
		if (tmp == pdev || tmp->bus != pdev->bus ||
		    PCI_SLOT(tmp->devfn) != PCI_SLOT(pdev->devfn) ||
		    pci_acs_enabled(tmp, REQ_ACS_FLAGS))
			continue;

		group = get_pci_alias_group(tmp, devfns);
		if (group) {
			pci_dev_put(tmp);
			return group;
		}
	}

	return NULL;
}

/*
 * Look for aliases to or from the given device for existing groups. DMA
 * aliases are only supported on the same bus, therefore the search
 * space is quite small (especially since we're really only looking at pcie
 * device, and therefore only expect multiple slots on the root complex or
 * downstream switch ports).  It's conceivable though that a pair of
 * multifunction devices could have aliases between them that would cause a
 * loop.  To prevent this, we use a bitmap to track where we've been.
 */
static struct iommu_group *get_pci_alias_group(struct pci_dev *pdev,
					       unsigned long *devfns)
{
	struct pci_dev *tmp = NULL;
	struct iommu_group *group;

	if (test_and_set_bit(pdev->devfn & 0xff, devfns))
		return NULL;

	group = iommu_group_get(&pdev->dev);
	if (group)
		return group;

	for_each_pci_dev(tmp) {
		if (tmp == pdev || tmp->bus != pdev->bus)
			continue;

		/* We alias them or they alias us */
		if (pci_devs_are_dma_aliases(pdev, tmp)) {
			group = get_pci_alias_group(tmp, devfns);
			if (group) {
				pci_dev_put(tmp);
				return group;
			}

			group = get_pci_function_alias_group(tmp, devfns);
			if (group) {
				pci_dev_put(tmp);
				return group;
			}
		}
	}

	return NULL;
}

struct group_for_pci_data {
	struct pci_dev *pdev;
	struct iommu_group *group;
};

/*
 * DMA alias iterator callback, return the last seen device.  Stop and return
 * the IOMMU group if we find one along the way.
 */
static int get_pci_alias_or_group(struct pci_dev *pdev, u16 alias, void *opaque)
{
	struct group_for_pci_data *data = opaque;

	data->pdev = pdev;
	data->group = iommu_group_get(&pdev->dev);

	return data->group != NULL;
}

/*
 * Generic device_group call-back function. It just allocates one
 * iommu-group per device.
 */
struct iommu_group *generic_device_group(struct device *dev)
{
	return iommu_group_alloc();
}
EXPORT_SYMBOL_GPL(generic_device_group);

/*
 * Use standard PCI bus topology, isolation features, and DMA alias quirks
 * to find or create an IOMMU group for a device.
 */
struct iommu_group *pci_device_group(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct group_for_pci_data data;
	struct pci_bus *bus;
	struct iommu_group *group = NULL;
	u64 devfns[4] = { 0 };

	if (WARN_ON(!dev_is_pci(dev)))
		return ERR_PTR(-EINVAL);

	/*
	 * Find the upstream DMA alias for the device.  A device must not
	 * be aliased due to topology in order to have its own IOMMU group.
	 * If we find an alias along the way that already belongs to a
	 * group, use it.
	 */
	if (pci_for_each_dma_alias(pdev, get_pci_alias_or_group, &data))
		return data.group;

	pdev = data.pdev;

	/*
	 * Continue upstream from the point of minimum IOMMU granularity
	 * due to aliases to the point where devices are protected from
	 * peer-to-peer DMA by PCI ACS.  Again, if we find an existing
	 * group, use it.
	 */
	for (bus = pdev->bus; !pci_is_root_bus(bus); bus = bus->parent) {
		if (!bus->self)
			continue;

		if (pci_acs_path_enabled(bus->self, NULL, REQ_ACS_FLAGS))
			break;

		pdev = bus->self;

		group = iommu_group_get(&pdev->dev);
		if (group)
			return group;
	}

	/*
	 * Look for existing groups on device aliases.  If we alias another
	 * device or another device aliases us, use the same group.
	 */
	group = get_pci_alias_group(pdev, (unsigned long *)devfns);
	if (group)
		return group;

	/*
	 * Look for existing groups on non-isolated functions on the same
	 * slot and aliases of those funcions, if any.  No need to clear
	 * the search bitmap, the tested devfns are still valid.
	 */
	group = get_pci_function_alias_group(pdev, (unsigned long *)devfns);
	if (group)
		return group;

	/* No shared group found, allocate new */
	return iommu_group_alloc();
}
EXPORT_SYMBOL_GPL(pci_device_group);

/* Get the IOMMU group for device on fsl-mc bus */
struct iommu_group *fsl_mc_device_group(struct device *dev)
{
	struct device *cont_dev = fsl_mc_cont_dev(dev);
	struct iommu_group *group;

	group = iommu_group_get(cont_dev);
	if (!group)
		group = iommu_group_alloc();
	return group;
}
EXPORT_SYMBOL_GPL(fsl_mc_device_group);

static int iommu_get_def_domain_type(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	unsigned int type = 0;

	if (ops->def_domain_type)
		type = ops->def_domain_type(dev);

	return (type == 0) ? iommu_def_domain_type : type;
}

static int iommu_group_alloc_default_domain(struct bus_type *bus,
					    struct iommu_group *group,
					    unsigned int type)
{
	struct iommu_domain *dom;

	dom = __iommu_domain_alloc(bus, type);
	if (!dom && type != IOMMU_DOMAIN_DMA) {
		dom = __iommu_domain_alloc(bus, IOMMU_DOMAIN_DMA);
		if (dom)
			pr_warn("Failed to allocate default IOMMU domain of type %u for group %s - Falling back to IOMMU_DOMAIN_DMA",
				type, group->name);
	}

	if (!dom)
		return -ENOMEM;

	group->default_domain = dom;
	if (!group->domain)
		group->domain = dom;

	if (!iommu_dma_strict) {
		int attr = 1;
		iommu_domain_set_attr(dom,
				      DOMAIN_ATTR_DMA_USE_FLUSH_QUEUE,
				      &attr);
	}

	return 0;
}

static int iommu_alloc_default_domain(struct iommu_group *group,
				      struct device *dev)
{
	unsigned int type;

	if (group->default_domain)
		return 0;

	type = iommu_get_def_domain_type(dev);

	return iommu_group_alloc_default_domain(dev->bus, group, type);
}

/**
 * iommu_group_get_for_dev - Find or create the IOMMU group for a device
 * @dev: target device
 *
 * This function is intended to be called by IOMMU drivers and extended to
 * support common, bus-defined algorithms when determining or creating the
 * IOMMU group for a device.  On success, the caller will hold a reference
 * to the returned IOMMU group, which will already include the provided
 * device.  The reference should be released with iommu_group_put().
 */
static struct iommu_group *iommu_group_get_for_dev(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_group *group;
	int ret;

	group = iommu_group_get(dev);
	if (group)
		return group;

	if (!ops)
		return ERR_PTR(-EINVAL);

	group = ops->device_group(dev);
	if (WARN_ON_ONCE(group == NULL))
		return ERR_PTR(-EINVAL);

	if (IS_ERR(group))
		return group;

	ret = iommu_group_add_device(group, dev);
	if (ret)
		goto out_put_group;

	return group;

out_put_group:
	iommu_group_put(group);

	return ERR_PTR(ret);
}

struct iommu_domain *iommu_group_default_domain(struct iommu_group *group)
{
	return group->default_domain;
}

static int probe_iommu_group(struct device *dev, void *data)
{
	struct list_head *group_list = data;
	struct iommu_group *group;
	int ret;

	/* Device is probed already if in a group */
	group = iommu_group_get(dev);
	if (group) {
		iommu_group_put(group);
		return 0;
	}

	ret = __iommu_probe_device(dev, group_list);
	if (ret == -ENODEV)
		ret = 0;

	return ret;
}

static int remove_iommu_group(struct device *dev, void *data)
{
	iommu_release_device(dev);

	return 0;
}

static int iommu_bus_notifier(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	unsigned long group_action = 0;
	struct device *dev = data;
	struct iommu_group *group;

	/*
	 * ADD/DEL call into iommu driver ops if provided, which may
	 * result in ADD/DEL notifiers to group->notifier
	 */
	if (action == BUS_NOTIFY_ADD_DEVICE) {
		int ret;

		ret = iommu_probe_device(dev);
		return (ret) ? NOTIFY_DONE : NOTIFY_OK;
	} else if (action == BUS_NOTIFY_REMOVED_DEVICE) {
		iommu_release_device(dev);
		return NOTIFY_OK;
	}

	/*
	 * Remaining BUS_NOTIFYs get filtered and republished to the
	 * group, if anyone is listening
	 */
	group = iommu_group_get(dev);
	if (!group)
		return 0;

	switch (action) {
	case BUS_NOTIFY_BIND_DRIVER:
		group_action = IOMMU_GROUP_NOTIFY_BIND_DRIVER;
		break;
	case BUS_NOTIFY_BOUND_DRIVER:
		group_action = IOMMU_GROUP_NOTIFY_BOUND_DRIVER;
		break;
	case BUS_NOTIFY_UNBIND_DRIVER:
		group_action = IOMMU_GROUP_NOTIFY_UNBIND_DRIVER;
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		group_action = IOMMU_GROUP_NOTIFY_UNBOUND_DRIVER;
		break;
	}

	if (group_action)
		blocking_notifier_call_chain(&group->notifier,
					     group_action, dev);

	iommu_group_put(group);
	return 0;
}

struct __group_domain_type {
	struct device *dev;
	unsigned int type;
};

static int probe_get_default_domain_type(struct device *dev, void *data)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct __group_domain_type *gtype = data;
	unsigned int type = 0;

	if (ops->def_domain_type)
		type = ops->def_domain_type(dev);

	if (type) {
		if (gtype->type && gtype->type != type) {
			dev_warn(dev, "Device needs domain type %s, but device %s in the same iommu group requires type %s - using default\n",
				 iommu_domain_type_str(type),
				 dev_name(gtype->dev),
				 iommu_domain_type_str(gtype->type));
			gtype->type = 0;
		}

		if (!gtype->dev) {
			gtype->dev  = dev;
			gtype->type = type;
		}
	}

	return 0;
}

static void probe_alloc_default_domain(struct bus_type *bus,
				       struct iommu_group *group)
{
	struct __group_domain_type gtype;

	memset(&gtype, 0, sizeof(gtype));

	/* Ask for default domain requirements of all devices in the group */
	__iommu_group_for_each_dev(group, &gtype,
				   probe_get_default_domain_type);

	if (!gtype.type)
		gtype.type = iommu_def_domain_type;

	iommu_group_alloc_default_domain(bus, group, gtype.type);

}

static int iommu_group_do_dma_attach(struct device *dev, void *data)
{
	struct iommu_domain *domain = data;
	int ret = 0;

	if (!iommu_is_attach_deferred(domain, dev))
		ret = __iommu_attach_device(domain, dev);

	return ret;
}

static int __iommu_group_dma_attach(struct iommu_group *group)
{
	return __iommu_group_for_each_dev(group, group->default_domain,
					  iommu_group_do_dma_attach);
}

static int iommu_group_do_probe_finalize(struct device *dev, void *data)
{
	struct iommu_domain *domain = data;

	if (domain->ops->probe_finalize)
		domain->ops->probe_finalize(dev);

	return 0;
}

static void __iommu_group_dma_finalize(struct iommu_group *group)
{
	__iommu_group_for_each_dev(group, group->default_domain,
				   iommu_group_do_probe_finalize);
}

static int iommu_do_create_direct_mappings(struct device *dev, void *data)
{
	struct iommu_group *group = data;

	iommu_create_device_direct_mappings(group, dev);

	return 0;
}

static int iommu_group_create_direct_mappings(struct iommu_group *group)
{
	return __iommu_group_for_each_dev(group, group,
					  iommu_do_create_direct_mappings);
}

int bus_iommu_probe(struct bus_type *bus)
{
	struct iommu_group *group, *next;
	LIST_HEAD(group_list);
	int ret;

	/*
	 * This code-path does not allocate the default domain when
	 * creating the iommu group, so do it after the groups are
	 * created.
	 */
	ret = bus_for_each_dev(bus, NULL, &group_list, probe_iommu_group);
	if (ret)
		return ret;

	list_for_each_entry_safe(group, next, &group_list, entry) {
		/* Remove item from the list */
		list_del_init(&group->entry);

		mutex_lock(&group->mutex);

		/* Try to allocate default domain */
		probe_alloc_default_domain(bus, group);

		if (!group->default_domain) {
			mutex_unlock(&group->mutex);
			continue;
		}

		iommu_group_create_direct_mappings(group);

		ret = __iommu_group_dma_attach(group);

		mutex_unlock(&group->mutex);

		if (ret)
			break;

		__iommu_group_dma_finalize(group);
	}

	return ret;
}

static int iommu_bus_init(struct bus_type *bus, const struct iommu_ops *ops)
{
	struct notifier_block *nb;
	int err;

	nb = kzalloc(sizeof(struct notifier_block), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	nb->notifier_call = iommu_bus_notifier;

	err = bus_register_notifier(bus, nb);
	if (err)
		goto out_free;

	err = bus_iommu_probe(bus);
	if (err)
		goto out_err;


	return 0;

out_err:
	/* Clean up */
	bus_for_each_dev(bus, NULL, NULL, remove_iommu_group);
	bus_unregister_notifier(bus, nb);

out_free:
	kfree(nb);

	return err;
}

/**
 * bus_set_iommu - set iommu-callbacks for the bus
 * @bus: bus.
 * @ops: the callbacks provided by the iommu-driver
 *
 * This function is called by an iommu driver to set the iommu methods
 * used for a particular bus. Drivers for devices on that bus can use
 * the iommu-api after these ops are registered.
 * This special function is needed because IOMMUs are usually devices on
 * the bus itself, so the iommu drivers are not initialized when the bus
 * is set up. With this function the iommu-driver can set the iommu-ops
 * afterwards.
 */
int bus_set_iommu(struct bus_type *bus, const struct iommu_ops *ops)
{
	int err;

	if (ops == NULL) {
		bus->iommu_ops = NULL;
		return 0;
	}

	if (bus->iommu_ops != NULL)
		return -EBUSY;

	bus->iommu_ops = ops;

	/* Do IOMMU specific setup for this bus-type */
	err = iommu_bus_init(bus, ops);
	if (err)
		bus->iommu_ops = NULL;

	return err;
}
EXPORT_SYMBOL_GPL(bus_set_iommu);

bool iommu_present(struct bus_type *bus)
{
	return bus->iommu_ops != NULL;
}
EXPORT_SYMBOL_GPL(iommu_present);

bool iommu_capable(struct bus_type *bus, enum iommu_cap cap)
{
	if (!bus->iommu_ops || !bus->iommu_ops->capable)
		return false;

	return bus->iommu_ops->capable(cap);
}
EXPORT_SYMBOL_GPL(iommu_capable);

/**
 * iommu_set_fault_handler() - set a fault handler for an iommu domain
 * @domain: iommu domain
 * @handler: fault handler
 * @token: user data, will be passed back to the fault handler
 *
 * This function should be used by IOMMU users which want to be notified
 * whenever an IOMMU fault happens.
 *
 * The fault handler itself should return 0 on success, and an appropriate
 * error code otherwise.
 */
void iommu_set_fault_handler(struct iommu_domain *domain,
					iommu_fault_handler_t handler,
					void *token)
{
	BUG_ON(!domain);

	domain->handler = handler;
	domain->handler_token = token;
}
EXPORT_SYMBOL_GPL(iommu_set_fault_handler);

static struct iommu_domain *__iommu_domain_alloc(struct bus_type *bus,
						 unsigned type)
{
	struct iommu_domain *domain;

	if (bus == NULL || bus->iommu_ops == NULL)
		return NULL;

	domain = bus->iommu_ops->domain_alloc(type);
	if (!domain)
		return NULL;

	domain->ops  = bus->iommu_ops;
	domain->type = type;
	/* Assume all sizes by default; the driver may override this later */
	domain->pgsize_bitmap  = bus->iommu_ops->pgsize_bitmap;

	return domain;
}

struct iommu_domain *iommu_domain_alloc(struct bus_type *bus)
{
	return __iommu_domain_alloc(bus, IOMMU_DOMAIN_UNMANAGED);
}
EXPORT_SYMBOL_GPL(iommu_domain_alloc);

void iommu_domain_free(struct iommu_domain *domain)
{
	domain->ops->domain_free(domain);
}
EXPORT_SYMBOL_GPL(iommu_domain_free);

static int __iommu_attach_device(struct iommu_domain *domain,
				 struct device *dev)
{
	int ret;

	if (unlikely(domain->ops->attach_dev == NULL))
		return -ENODEV;

	ret = domain->ops->attach_dev(domain, dev);
	if (!ret)
		trace_attach_device_to_domain(dev);
	return ret;
}

int iommu_attach_device(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_group *group;
	int ret;

	group = iommu_group_get(dev);
	if (!group)
		return -ENODEV;

	/*
	 * Lock the group to make sure the device-count doesn't
	 * change while we are attaching
	 */
	mutex_lock(&group->mutex);
	ret = -EINVAL;
	if (iommu_group_device_count(group) != 1)
		goto out_unlock;

	ret = __iommu_attach_group(domain, group);

out_unlock:
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_attach_device);

/*
 * Check flags and other user provided data for valid combinations. We also
 * make sure no reserved fields or unused flags are set. This is to ensure
 * not breaking userspace in the future when these fields or flags are used.
 */
static int iommu_check_cache_invl_data(struct iommu_cache_invalidate_info *info)
{
	u32 mask;
	int i;

	if (info->version != IOMMU_CACHE_INVALIDATE_INFO_VERSION_1)
		return -EINVAL;

	mask = (1 << IOMMU_CACHE_INV_TYPE_NR) - 1;
	if (info->cache & ~mask)
		return -EINVAL;

	if (info->granularity >= IOMMU_INV_GRANU_NR)
		return -EINVAL;

	switch (info->granularity) {
	case IOMMU_INV_GRANU_ADDR:
		if (info->cache & IOMMU_CACHE_INV_TYPE_PASID)
			return -EINVAL;

		mask = IOMMU_INV_ADDR_FLAGS_PASID |
			IOMMU_INV_ADDR_FLAGS_ARCHID |
			IOMMU_INV_ADDR_FLAGS_LEAF;

		if (info->granu.addr_info.flags & ~mask)
			return -EINVAL;
		break;
	case IOMMU_INV_GRANU_PASID:
		mask = IOMMU_INV_PASID_FLAGS_PASID |
			IOMMU_INV_PASID_FLAGS_ARCHID;
		if (info->granu.pasid_info.flags & ~mask)
			return -EINVAL;

		break;
	case IOMMU_INV_GRANU_DOMAIN:
		if (info->cache & IOMMU_CACHE_INV_TYPE_DEV_IOTLB)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* Check reserved padding fields */
	for (i = 0; i < sizeof(info->padding); i++) {
		if (info->padding[i])
			return -EINVAL;
	}

	return 0;
}

int iommu_uapi_cache_invalidate(struct iommu_domain *domain, struct device *dev,
				void __user *uinfo)
{
	struct iommu_cache_invalidate_info inv_info = { 0 };
	u32 minsz;
	int ret;

	if (unlikely(!domain->ops->cache_invalidate))
		return -ENODEV;

	/*
	 * No new spaces can be added before the variable sized union, the
	 * minimum size is the offset to the union.
	 */
	minsz = offsetof(struct iommu_cache_invalidate_info, granu);

	/* Copy minsz from user to get flags and argsz */
	if (copy_from_user(&inv_info, uinfo, minsz))
		return -EFAULT;

	/* Fields before the variable size union are mandatory */
	if (inv_info.argsz < minsz)
		return -EINVAL;

	/* PASID and address granu require additional info beyond minsz */
	if (inv_info.granularity == IOMMU_INV_GRANU_PASID &&
	    inv_info.argsz < offsetofend(struct iommu_cache_invalidate_info, granu.pasid_info))
		return -EINVAL;

	if (inv_info.granularity == IOMMU_INV_GRANU_ADDR &&
	    inv_info.argsz < offsetofend(struct iommu_cache_invalidate_info, granu.addr_info))
		return -EINVAL;

	/*
	 * User might be using a newer UAPI header which has a larger data
	 * size, we shall support the existing flags within the current
	 * size. Copy the remaining user data _after_ minsz but not more
	 * than the current kernel supported size.
	 */
	if (copy_from_user((void *)&inv_info + minsz, uinfo + minsz,
			   min_t(u32, inv_info.argsz, sizeof(inv_info)) - minsz))
		return -EFAULT;

	/* Now the argsz is validated, check the content */
	ret = iommu_check_cache_invl_data(&inv_info);
	if (ret)
		return ret;

	return domain->ops->cache_invalidate(domain, dev, &inv_info);
}
EXPORT_SYMBOL_GPL(iommu_uapi_cache_invalidate);

static int iommu_check_bind_data(struct iommu_gpasid_bind_data *data)
{
	u64 mask;
	int i;

	if (data->version != IOMMU_GPASID_BIND_VERSION_1)
		return -EINVAL;

	/* Check the range of supported formats */
	if (data->format >= IOMMU_PASID_FORMAT_LAST)
		return -EINVAL;

	/* Check all flags */
	mask = IOMMU_SVA_GPASID_VAL;
	if (data->flags & ~mask)
		return -EINVAL;

	/* Check reserved padding fields */
	for (i = 0; i < sizeof(data->padding); i++) {
		if (data->padding[i])
			return -EINVAL;
	}

	return 0;
}

static int iommu_sva_prepare_bind_data(void __user *udata,
				       struct iommu_gpasid_bind_data *data)
{
	u32 minsz;

	/*
	 * No new spaces can be added before the variable sized union, the
	 * minimum size is the offset to the union.
	 */
	minsz = offsetof(struct iommu_gpasid_bind_data, vendor);

	/* Copy minsz from user to get flags and argsz */
	if (copy_from_user(data, udata, minsz))
		return -EFAULT;

	/* Fields before the variable size union are mandatory */
	if (data->argsz < minsz)
		return -EINVAL;
	/*
	 * User might be using a newer UAPI header, we shall let IOMMU vendor
	 * driver decide on what size it needs. Since the guest PASID bind data
	 * can be vendor specific, larger argsz could be the result of extension
	 * for one vendor but it should not affect another vendor.
	 * Copy the remaining user data _after_ minsz
	 */
	if (copy_from_user((void *)data + minsz, udata + minsz,
			   min_t(u32, data->argsz, sizeof(*data)) - minsz))
		return -EFAULT;

	return iommu_check_bind_data(data);
}

int iommu_uapi_sva_bind_gpasid(struct iommu_domain *domain, struct device *dev,
			       void __user *udata)
{
	struct iommu_gpasid_bind_data data = { 0 };
	int ret;

	if (unlikely(!domain->ops->sva_bind_gpasid))
		return -ENODEV;

	ret = iommu_sva_prepare_bind_data(udata, &data);
	if (ret)
		return ret;

	return domain->ops->sva_bind_gpasid(domain, dev, &data);
}
EXPORT_SYMBOL_GPL(iommu_uapi_sva_bind_gpasid);

int iommu_sva_unbind_gpasid(struct iommu_domain *domain, struct device *dev,
			     ioasid_t pasid)
{
	if (unlikely(!domain->ops->sva_unbind_gpasid))
		return -ENODEV;

	return domain->ops->sva_unbind_gpasid(dev, pasid);
}
EXPORT_SYMBOL_GPL(iommu_sva_unbind_gpasid);

int iommu_uapi_sva_unbind_gpasid(struct iommu_domain *domain, struct device *dev,
				 void __user *udata)
{
	struct iommu_gpasid_bind_data data = { 0 };
	int ret;

	if (unlikely(!domain->ops->sva_bind_gpasid))
		return -ENODEV;

	ret = iommu_sva_prepare_bind_data(udata, &data);
	if (ret)
		return ret;

	return iommu_sva_unbind_gpasid(domain, dev, data.hpasid);
}
EXPORT_SYMBOL_GPL(iommu_uapi_sva_unbind_gpasid);

static void __iommu_detach_device(struct iommu_domain *domain,
				  struct device *dev)
{
	if (iommu_is_attach_deferred(domain, dev))
		return;

	if (unlikely(domain->ops->detach_dev == NULL))
		return;

	domain->ops->detach_dev(domain, dev);
	trace_detach_device_from_domain(dev);
}

void iommu_detach_device(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		return;

	mutex_lock(&group->mutex);
	if (iommu_group_device_count(group) != 1) {
		WARN_ON(1);
		goto out_unlock;
	}

	__iommu_detach_group(domain, group);

out_unlock:
	mutex_unlock(&group->mutex);
	iommu_group_put(group);
}
EXPORT_SYMBOL_GPL(iommu_detach_device);

struct iommu_domain *iommu_get_domain_for_dev(struct device *dev)
{
	struct iommu_domain *domain;
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		return NULL;

	domain = group->domain;

	iommu_group_put(group);

	return domain;
}
EXPORT_SYMBOL_GPL(iommu_get_domain_for_dev);

/*
 * For IOMMU_DOMAIN_DMA implementations which already provide their own
 * guarantees that the group and its default domain are valid and correct.
 */
struct iommu_domain *iommu_get_dma_domain(struct device *dev)
{
	return dev->iommu_group->default_domain;
}

/*
 * IOMMU groups are really the natural working unit of the IOMMU, but
 * the IOMMU API works on domains and devices.  Bridge that gap by
 * iterating over the devices in a group.  Ideally we'd have a single
 * device which represents the requestor ID of the group, but we also
 * allow IOMMU drivers to create policy defined minimum sets, where
 * the physical hardware may be able to distiguish members, but we
 * wish to group them at a higher level (ex. untrusted multi-function
 * PCI devices).  Thus we attach each device.
 */
static int iommu_group_do_attach_device(struct device *dev, void *data)
{
	struct iommu_domain *domain = data;

	return __iommu_attach_device(domain, dev);
}

static int __iommu_attach_group(struct iommu_domain *domain,
				struct iommu_group *group)
{
	int ret;

	if (group->default_domain && group->domain != group->default_domain)
		return -EBUSY;

	ret = __iommu_group_for_each_dev(group, domain,
					 iommu_group_do_attach_device);
	if (ret == 0)
		group->domain = domain;

	return ret;
}

int iommu_attach_group(struct iommu_domain *domain, struct iommu_group *group)
{
	int ret;

	mutex_lock(&group->mutex);
	ret = __iommu_attach_group(domain, group);
	mutex_unlock(&group->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_attach_group);

static int iommu_group_do_detach_device(struct device *dev, void *data)
{
	struct iommu_domain *domain = data;

	__iommu_detach_device(domain, dev);

	return 0;
}

static void __iommu_detach_group(struct iommu_domain *domain,
				 struct iommu_group *group)
{
	int ret;

	if (!group->default_domain) {
		__iommu_group_for_each_dev(group, domain,
					   iommu_group_do_detach_device);
		group->domain = NULL;
		return;
	}

	if (group->domain == group->default_domain)
		return;

	/* Detach by re-attaching to the default domain */
	ret = __iommu_group_for_each_dev(group, group->default_domain,
					 iommu_group_do_attach_device);
	if (ret != 0)
		WARN_ON(1);
	else
		group->domain = group->default_domain;
}

void iommu_detach_group(struct iommu_domain *domain, struct iommu_group *group)
{
	mutex_lock(&group->mutex);
	__iommu_detach_group(domain, group);
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_detach_group);

phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	if (unlikely(domain->ops->iova_to_phys == NULL))
		return 0;

	return domain->ops->iova_to_phys(domain, iova);
}
EXPORT_SYMBOL_GPL(iommu_iova_to_phys);

static size_t iommu_pgsize(struct iommu_domain *domain,
			   unsigned long addr_merge, size_t size)
{
	unsigned int pgsize_idx;
	size_t pgsize;

	/* Max page size that still fits into 'size' */
	pgsize_idx = __fls(size);

	/* need to consider alignment requirements ? */
	if (likely(addr_merge)) {
		/* Max page size allowed by address */
		unsigned int align_pgsize_idx = __ffs(addr_merge);
		pgsize_idx = min(pgsize_idx, align_pgsize_idx);
	}

	/* build a mask of acceptable page sizes */
	pgsize = (1UL << (pgsize_idx + 1)) - 1;

	/* throw away page sizes not supported by the hardware */
	pgsize &= domain->pgsize_bitmap;

	/* make sure we're still sane */
	BUG_ON(!pgsize);

	/* pick the biggest page */
	pgsize_idx = __fls(pgsize);
	pgsize = 1UL << pgsize_idx;

	return pgsize;
}

static int __iommu_map(struct iommu_domain *domain, unsigned long iova,
		       phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	const struct iommu_ops *ops = domain->ops;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;
	size_t orig_size = size;
	phys_addr_t orig_paddr = paddr;
	int ret = 0;

	if (unlikely(ops->map == NULL ||
		     domain->pgsize_bitmap == 0UL))
		return -ENODEV;

	if (unlikely(!(domain->type & __IOMMU_DOMAIN_PAGING)))
		return -EINVAL;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(domain->pgsize_bitmap);

	/*
	 * both the virtual address and the physical one, as well as
	 * the size of the mapping, must be aligned (at least) to the
	 * size of the smallest page supported by the hardware
	 */
	if (!IS_ALIGNED(iova | paddr | size, min_pagesz)) {
		pr_err("unaligned: iova 0x%lx pa %pa size 0x%zx min_pagesz 0x%x\n",
		       iova, &paddr, size, min_pagesz);
		return -EINVAL;
	}

	pr_debug("map: iova 0x%lx pa %pa size 0x%zx\n", iova, &paddr, size);

	while (size) {
		size_t pgsize = iommu_pgsize(domain, iova | paddr, size);

		pr_debug("mapping: iova 0x%lx pa %pa pgsize 0x%zx\n",
			 iova, &paddr, pgsize);
		ret = ops->map(domain, iova, paddr, pgsize, prot, gfp);

		if (ret)
			break;

		iova += pgsize;
		paddr += pgsize;
		size -= pgsize;
	}

	if (ops->iotlb_sync_map)
		ops->iotlb_sync_map(domain);

	/* unroll mapping in case something went wrong */
	if (ret)
		iommu_unmap(domain, orig_iova, orig_size - size);
	else
		trace_map(orig_iova, orig_paddr, orig_size);

	return ret;
}

int iommu_map(struct iommu_domain *domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot)
{
	might_sleep();
	return __iommu_map(domain, iova, paddr, size, prot, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(iommu_map);

int iommu_map_atomic(struct iommu_domain *domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot)
{
	return __iommu_map(domain, iova, paddr, size, prot, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(iommu_map_atomic);

static size_t __iommu_unmap(struct iommu_domain *domain,
			    unsigned long iova, size_t size,
			    struct iommu_iotlb_gather *iotlb_gather)
{
	const struct iommu_ops *ops = domain->ops;
	size_t unmapped_page, unmapped = 0;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;

	if (unlikely(ops->unmap == NULL ||
		     domain->pgsize_bitmap == 0UL))
		return 0;

	if (unlikely(!(domain->type & __IOMMU_DOMAIN_PAGING)))
		return 0;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(domain->pgsize_bitmap);

	/*
	 * The virtual address, as well as the size of the mapping, must be
	 * aligned (at least) to the size of the smallest page supported
	 * by the hardware
	 */
	if (!IS_ALIGNED(iova | size, min_pagesz)) {
		pr_err("unaligned: iova 0x%lx size 0x%zx min_pagesz 0x%x\n",
		       iova, size, min_pagesz);
		return 0;
	}

	pr_debug("unmap this: iova 0x%lx size 0x%zx\n", iova, size);

	/*
	 * Keep iterating until we either unmap 'size' bytes (or more)
	 * or we hit an area that isn't mapped.
	 */
	while (unmapped < size) {
		size_t pgsize = iommu_pgsize(domain, iova, size - unmapped);

		unmapped_page = ops->unmap(domain, iova, pgsize, iotlb_gather);
		if (!unmapped_page)
			break;

		pr_debug("unmapped: iova 0x%lx size 0x%zx\n",
			 iova, unmapped_page);

		iova += unmapped_page;
		unmapped += unmapped_page;
	}

	trace_unmap(orig_iova, size, unmapped);
	return unmapped;
}

size_t iommu_unmap(struct iommu_domain *domain,
		   unsigned long iova, size_t size)
{
	struct iommu_iotlb_gather iotlb_gather;
	size_t ret;

	iommu_iotlb_gather_init(&iotlb_gather);
	ret = __iommu_unmap(domain, iova, size, &iotlb_gather);
	iommu_iotlb_sync(domain, &iotlb_gather);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_unmap);

size_t iommu_unmap_fast(struct iommu_domain *domain,
			unsigned long iova, size_t size,
			struct iommu_iotlb_gather *iotlb_gather)
{
	return __iommu_unmap(domain, iova, size, iotlb_gather);
}
EXPORT_SYMBOL_GPL(iommu_unmap_fast);

static size_t __iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
			     struct scatterlist *sg, unsigned int nents, int prot,
			     gfp_t gfp)
{
	size_t len = 0, mapped = 0;
	phys_addr_t start;
	unsigned int i = 0;
	int ret;

	while (i <= nents) {
		phys_addr_t s_phys = sg_phys(sg);

		if (len && s_phys != start + len) {
			ret = __iommu_map(domain, iova + mapped, start,
					len, prot, gfp);

			if (ret)
				goto out_err;

			mapped += len;
			len = 0;
		}

		if (len) {
			len += sg->length;
		} else {
			len = sg->length;
			start = s_phys;
		}

		if (++i < nents)
			sg = sg_next(sg);
	}

	return mapped;

out_err:
	/* undo mappings already done */
	iommu_unmap(domain, iova, mapped);

	return 0;

}

size_t iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
		    struct scatterlist *sg, unsigned int nents, int prot)
{
	might_sleep();
	return __iommu_map_sg(domain, iova, sg, nents, prot, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(iommu_map_sg);

size_t iommu_map_sg_atomic(struct iommu_domain *domain, unsigned long iova,
		    struct scatterlist *sg, unsigned int nents, int prot)
{
	return __iommu_map_sg(domain, iova, sg, nents, prot, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(iommu_map_sg_atomic);

int iommu_domain_window_enable(struct iommu_domain *domain, u32 wnd_nr,
			       phys_addr_t paddr, u64 size, int prot)
{
	if (unlikely(domain->ops->domain_window_enable == NULL))
		return -ENODEV;

	return domain->ops->domain_window_enable(domain, wnd_nr, paddr, size,
						 prot);
}
EXPORT_SYMBOL_GPL(iommu_domain_window_enable);

void iommu_domain_window_disable(struct iommu_domain *domain, u32 wnd_nr)
{
	if (unlikely(domain->ops->domain_window_disable == NULL))
		return;

	return domain->ops->domain_window_disable(domain, wnd_nr);
}
EXPORT_SYMBOL_GPL(iommu_domain_window_disable);

/**
 * report_iommu_fault() - report about an IOMMU fault to the IOMMU framework
 * @domain: the iommu domain where the fault has happened
 * @dev: the device where the fault has happened
 * @iova: the faulting address
 * @flags: mmu fault flags (e.g. IOMMU_FAULT_READ/IOMMU_FAULT_WRITE/...)
 *
 * This function should be called by the low-level IOMMU implementations
 * whenever IOMMU faults happen, to allow high-level users, that are
 * interested in such events, to know about them.
 *
 * This event may be useful for several possible use cases:
 * - mere logging of the event
 * - dynamic TLB/PTE loading
 * - if restarting of the faulting device is required
 *
 * Returns 0 on success and an appropriate error code otherwise (if dynamic
 * PTE/TLB loading will one day be supported, implementations will be able
 * to tell whether it succeeded or not according to this return value).
 *
 * Specifically, -ENOSYS is returned if a fault handler isn't installed
 * (though fault handlers can also return -ENOSYS, in case they want to
 * elicit the default behavior of the IOMMU drivers).
 */
int report_iommu_fault(struct iommu_domain *domain, struct device *dev,
		       unsigned long iova, int flags)
{
	int ret = -ENOSYS;

	/*
	 * if upper layers showed interest and installed a fault handler,
	 * invoke it.
	 */
	if (domain->handler)
		ret = domain->handler(domain, dev, iova, flags,
						domain->handler_token);

	trace_io_page_fault(dev, iova, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(report_iommu_fault);

static int __init iommu_init(void)
{
	iommu_group_kset = kset_create_and_add("iommu_groups",
					       NULL, kernel_kobj);
	BUG_ON(!iommu_group_kset);

	iommu_debugfs_setup();

	return 0;
}
core_initcall(iommu_init);

int iommu_domain_get_attr(struct iommu_domain *domain,
			  enum iommu_attr attr, void *data)
{
	struct iommu_domain_geometry *geometry;
	bool *paging;
	int ret = 0;

	switch (attr) {
	case DOMAIN_ATTR_GEOMETRY:
		geometry  = data;
		*geometry = domain->geometry;

		break;
	case DOMAIN_ATTR_PAGING:
		paging  = data;
		*paging = (domain->pgsize_bitmap != 0UL);
		break;
	default:
		if (!domain->ops->domain_get_attr)
			return -EINVAL;

		ret = domain->ops->domain_get_attr(domain, attr, data);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_domain_get_attr);

int iommu_domain_set_attr(struct iommu_domain *domain,
			  enum iommu_attr attr, void *data)
{
	int ret = 0;

	switch (attr) {
	default:
		if (domain->ops->domain_set_attr == NULL)
			return -EINVAL;

		ret = domain->ops->domain_set_attr(domain, attr, data);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_domain_set_attr);

void iommu_get_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->get_resv_regions)
		ops->get_resv_regions(dev, list);
}

void iommu_put_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->put_resv_regions)
		ops->put_resv_regions(dev, list);
}

/**
 * generic_iommu_put_resv_regions - Reserved region driver helper
 * @dev: device for which to free reserved regions
 * @list: reserved region list for device
 *
 * IOMMU drivers can use this to implement their .put_resv_regions() callback
 * for simple reservations. Memory allocated for each reserved region will be
 * freed. If an IOMMU driver allocates additional resources per region, it is
 * going to have to implement a custom callback.
 */
void generic_iommu_put_resv_regions(struct device *dev, struct list_head *list)
{
	struct iommu_resv_region *entry, *next;

	list_for_each_entry_safe(entry, next, list, list)
		kfree(entry);
}
EXPORT_SYMBOL(generic_iommu_put_resv_regions);

struct iommu_resv_region *iommu_alloc_resv_region(phys_addr_t start,
						  size_t length, int prot,
						  enum iommu_resv_type type)
{
	struct iommu_resv_region *region;

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	INIT_LIST_HEAD(&region->list);
	region->start = start;
	region->length = length;
	region->prot = prot;
	region->type = type;
	return region;
}
EXPORT_SYMBOL_GPL(iommu_alloc_resv_region);

void iommu_set_default_passthrough(bool cmd_line)
{
	if (cmd_line)
		iommu_set_cmd_line_dma_api();

	iommu_def_domain_type = IOMMU_DOMAIN_IDENTITY;
}

void iommu_set_default_translated(bool cmd_line)
{
	if (cmd_line)
		iommu_set_cmd_line_dma_api();

	iommu_def_domain_type = IOMMU_DOMAIN_DMA;
}

bool iommu_default_passthrough(void)
{
	return iommu_def_domain_type == IOMMU_DOMAIN_IDENTITY;
}
EXPORT_SYMBOL_GPL(iommu_default_passthrough);

const struct iommu_ops *iommu_ops_from_fwnode(struct fwnode_handle *fwnode)
{
	const struct iommu_ops *ops = NULL;
	struct iommu_device *iommu;

	spin_lock(&iommu_device_lock);
	list_for_each_entry(iommu, &iommu_device_list, list)
		if (iommu->fwnode == fwnode) {
			ops = iommu->ops;
			break;
		}
	spin_unlock(&iommu_device_lock);
	return ops;
}

int iommu_fwspec_init(struct device *dev, struct fwnode_handle *iommu_fwnode,
		      const struct iommu_ops *ops)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec)
		return ops == fwspec->ops ? 0 : -EINVAL;

	if (!dev_iommu_get(dev))
		return -ENOMEM;

	/* Preallocate for the overwhelmingly common case of 1 ID */
	fwspec = kzalloc(struct_size(fwspec, ids, 1), GFP_KERNEL);
	if (!fwspec)
		return -ENOMEM;

	of_node_get(to_of_node(iommu_fwnode));
	fwspec->iommu_fwnode = iommu_fwnode;
	fwspec->ops = ops;
	dev_iommu_fwspec_set(dev, fwspec);
	return 0;
}
EXPORT_SYMBOL_GPL(iommu_fwspec_init);

void iommu_fwspec_free(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec) {
		fwnode_handle_put(fwspec->iommu_fwnode);
		kfree(fwspec);
		dev_iommu_fwspec_set(dev, NULL);
	}
}
EXPORT_SYMBOL_GPL(iommu_fwspec_free);

int iommu_fwspec_add_ids(struct device *dev, u32 *ids, int num_ids)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int i, new_num;

	if (!fwspec)
		return -EINVAL;

	new_num = fwspec->num_ids + num_ids;
	if (new_num > 1) {
		fwspec = krealloc(fwspec, struct_size(fwspec, ids, new_num),
				  GFP_KERNEL);
		if (!fwspec)
			return -ENOMEM;

		dev_iommu_fwspec_set(dev, fwspec);
	}

	for (i = 0; i < num_ids; i++)
		fwspec->ids[fwspec->num_ids + i] = ids[i];

	fwspec->num_ids = new_num;
	return 0;
}
EXPORT_SYMBOL_GPL(iommu_fwspec_add_ids);

/*
 * Per device IOMMU features.
 */
bool iommu_dev_has_feature(struct device *dev, enum iommu_dev_features feat)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->dev_has_feat)
		return ops->dev_has_feat(dev, feat);

	return false;
}
EXPORT_SYMBOL_GPL(iommu_dev_has_feature);

int iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features feat)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->dev_enable_feat)
		return ops->dev_enable_feat(dev, feat);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(iommu_dev_enable_feature);

/*
 * The device drivers should do the necessary cleanups before calling this.
 * For example, before disabling the aux-domain feature, the device driver
 * should detach all aux-domains. Otherwise, this will return -EBUSY.
 */
int iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features feat)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->dev_disable_feat)
		return ops->dev_disable_feat(dev, feat);

	return -EBUSY;
}
EXPORT_SYMBOL_GPL(iommu_dev_disable_feature);

bool iommu_dev_feature_enabled(struct device *dev, enum iommu_dev_features feat)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->dev_feat_enabled)
		return ops->dev_feat_enabled(dev, feat);

	return false;
}
EXPORT_SYMBOL_GPL(iommu_dev_feature_enabled);

/*
 * Aux-domain specific attach/detach.
 *
 * Only works if iommu_dev_feature_enabled(dev, IOMMU_DEV_FEAT_AUX) returns
 * true. Also, as long as domains are attached to a device through this
 * interface, any tries to call iommu_attach_device() should fail
 * (iommu_detach_device() can't fail, so we fail when trying to re-attach).
 * This should make us safe against a device being attached to a guest as a
 * whole while there are still pasid users on it (aux and sva).
 */
int iommu_aux_attach_device(struct iommu_domain *domain, struct device *dev)
{
	int ret = -ENODEV;

	if (domain->ops->aux_attach_dev)
		ret = domain->ops->aux_attach_dev(domain, dev);

	if (!ret)
		trace_attach_device_to_domain(dev);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_aux_attach_device);

void iommu_aux_detach_device(struct iommu_domain *domain, struct device *dev)
{
	if (domain->ops->aux_detach_dev) {
		domain->ops->aux_detach_dev(domain, dev);
		trace_detach_device_from_domain(dev);
	}
}
EXPORT_SYMBOL_GPL(iommu_aux_detach_device);

int iommu_aux_get_pasid(struct iommu_domain *domain, struct device *dev)
{
	int ret = -ENODEV;

	if (domain->ops->aux_get_pasid)
		ret = domain->ops->aux_get_pasid(domain, dev);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_aux_get_pasid);

/**
 * iommu_sva_bind_device() - Bind a process address space to a device
 * @dev: the device
 * @mm: the mm to bind, caller must hold a reference to it
 *
 * Create a bond between device and address space, allowing the device to access
 * the mm using the returned PASID. If a bond already exists between @device and
 * @mm, it is returned and an additional reference is taken. Caller must call
 * iommu_sva_unbind_device() to release each reference.
 *
 * iommu_dev_enable_feature(dev, IOMMU_DEV_FEAT_SVA) must be called first, to
 * initialize the required SVA features.
 *
 * On error, returns an ERR_PTR value.
 */
struct iommu_sva *
iommu_sva_bind_device(struct device *dev, struct mm_struct *mm, void *drvdata)
{
	struct iommu_group *group;
	struct iommu_sva *handle = ERR_PTR(-EINVAL);
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (!ops || !ops->sva_bind)
		return ERR_PTR(-ENODEV);

	group = iommu_group_get(dev);
	if (!group)
		return ERR_PTR(-ENODEV);

	/* Ensure device count and domain don't change while we're binding */
	mutex_lock(&group->mutex);

	/*
	 * To keep things simple, SVA currently doesn't support IOMMU groups
	 * with more than one device. Existing SVA-capable systems are not
	 * affected by the problems that required IOMMU groups (lack of ACS
	 * isolation, device ID aliasing and other hardware issues).
	 */
	if (iommu_group_device_count(group) != 1)
		goto out_unlock;

	handle = ops->sva_bind(dev, mm, drvdata);

out_unlock:
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	return handle;
}
EXPORT_SYMBOL_GPL(iommu_sva_bind_device);

/**
 * iommu_sva_unbind_device() - Remove a bond created with iommu_sva_bind_device
 * @handle: the handle returned by iommu_sva_bind_device()
 *
 * Put reference to a bond between device and address space. The device should
 * not be issuing any more transaction for this PASID. All outstanding page
 * requests for this PASID must have been flushed to the IOMMU.
 *
 * Returns 0 on success, or an error value
 */
void iommu_sva_unbind_device(struct iommu_sva *handle)
{
	struct iommu_group *group;
	struct device *dev = handle->dev;
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (!ops || !ops->sva_unbind)
		return;

	group = iommu_group_get(dev);
	if (!group)
		return;

	mutex_lock(&group->mutex);
	ops->sva_unbind(handle);
	mutex_unlock(&group->mutex);

	iommu_group_put(group);
}
EXPORT_SYMBOL_GPL(iommu_sva_unbind_device);

u32 iommu_sva_get_pasid(struct iommu_sva *handle)
{
	const struct iommu_ops *ops = handle->dev->bus->iommu_ops;

	if (!ops || !ops->sva_get_pasid)
		return IOMMU_PASID_INVALID;

	return ops->sva_get_pasid(handle);
}
EXPORT_SYMBOL_GPL(iommu_sva_get_pasid);
