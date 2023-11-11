// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)    "iommu: " fmt

#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/host1x_context_bus.h>
#include <linux/iommu.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/fsl/mc.h>
#include <linux/module.h>
#include <linux/cc_platform.h>
#include <trace/events/iommu.h>
#include <linux/sched/mm.h>
#include <trace/hooks/iommu.h>

#include "dma-iommu.h"

#include "iommu-sva.h"

static struct kset *iommu_group_kset;
static DEFINE_IDA(iommu_group_ida);

static unsigned int iommu_def_domain_type __read_mostly;
static bool iommu_dma_strict __read_mostly = IS_ENABLED(CONFIG_IOMMU_DEFAULT_DMA_STRICT);
static u32 iommu_cmd_line __read_mostly;

struct iommu_group {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct list_head devices;
	struct xarray pasid_array;
	struct mutex mutex;
	void *iommu_data;
	void (*iommu_data_release)(void *iommu_data);
	char *name;
	int id;
	struct iommu_domain *default_domain;
	struct iommu_domain *blocking_domain;
	struct iommu_domain *domain;
	struct list_head entry;
	unsigned int owner_cnt;
	void *owner;
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
#define IOMMU_CMD_LINE_STRICT		BIT(1)

static int iommu_bus_notifier(struct notifier_block *nb,
			      unsigned long action, void *data);
static int iommu_alloc_default_domain(struct iommu_group *group,
				      struct device *dev);
static struct iommu_domain *__iommu_domain_alloc(struct bus_type *bus,
						 unsigned type);
static int __iommu_attach_device(struct iommu_domain *domain,
				 struct device *dev);
static int __iommu_attach_group(struct iommu_domain *domain,
				struct iommu_group *group);
static int __iommu_group_set_domain(struct iommu_group *group,
				    struct iommu_domain *new_domain);
static int iommu_create_device_direct_mappings(struct iommu_group *group,
					       struct device *dev);
static struct iommu_group *iommu_group_get_for_dev(struct device *dev);
static ssize_t iommu_group_store_type(struct iommu_group *group,
				      const char *buf, size_t count);

#define IOMMU_GROUP_ATTR(_name, _mode, _show, _store)		\
struct iommu_group_attribute iommu_group_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

#define to_iommu_group_attr(_attr)	\
	container_of(_attr, struct iommu_group_attribute, attr)
#define to_iommu_group(_kobj)		\
	container_of(_kobj, struct iommu_group, kobj)

static LIST_HEAD(iommu_device_list);
static DEFINE_SPINLOCK(iommu_device_lock);

static struct bus_type * const iommu_buses[] = {
	&platform_bus_type,
#ifdef CONFIG_PCI
	&pci_bus_type,
#endif
#ifdef CONFIG_ARM_AMBA
	&amba_bustype,
#endif
#ifdef CONFIG_FSL_MC_BUS
	&fsl_mc_bus_type,
#endif
#ifdef CONFIG_TEGRA_HOST1X_CONTEXT_BUS
	&host1x_context_device_bus_type,
#endif
};

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
	case IOMMU_DOMAIN_DMA_FQ:
		return "Translated";
	default:
		return "Unknown";
	}
}

static int __init iommu_subsys_init(void)
{
	struct notifier_block *nb;

	if (!(iommu_cmd_line & IOMMU_CMD_LINE_DMA_API)) {
		if (IS_ENABLED(CONFIG_IOMMU_DEFAULT_PASSTHROUGH))
			iommu_set_default_passthrough(false);
		else
			iommu_set_default_translated(false);

		if (iommu_default_passthrough() && cc_platform_has(CC_ATTR_MEM_ENCRYPT)) {
			pr_info("Memory encryption detected - Disabling default IOMMU Passthrough\n");
			iommu_set_default_translated(false);
		}
	}

	if (!iommu_default_passthrough() && !iommu_dma_strict)
		iommu_def_domain_type = IOMMU_DOMAIN_DMA_FQ;

	pr_info("Default domain type: %s %s\n",
		iommu_domain_type_str(iommu_def_domain_type),
		(iommu_cmd_line & IOMMU_CMD_LINE_DMA_API) ?
			"(set via kernel command line)" : "");

	if (!iommu_default_passthrough())
		pr_info("DMA domain TLB invalidation policy: %s mode %s\n",
			iommu_dma_strict ? "strict" : "lazy",
			(iommu_cmd_line & IOMMU_CMD_LINE_STRICT) ?
				"(set via kernel command line)" : "");

	nb = kcalloc(ARRAY_SIZE(iommu_buses), sizeof(*nb), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	for (int i = 0; i < ARRAY_SIZE(iommu_buses); i++) {
		nb[i].notifier_call = iommu_bus_notifier;
		bus_register_notifier(iommu_buses[i], &nb[i]);
	}

	return 0;
}
subsys_initcall(iommu_subsys_init);

static int remove_iommu_group(struct device *dev, void *data)
{
	if (dev->iommu && dev->iommu->iommu_dev == data)
		iommu_release_device(dev);

	return 0;
}

/**
 * iommu_device_register() - Register an IOMMU hardware instance
 * @iommu: IOMMU handle for the instance
 * @ops:   IOMMU ops to associate with the instance
 * @hwdev: (optional) actual instance device, used for fwnode lookup
 *
 * Return: 0 on success, or an error.
 */
int iommu_device_register(struct iommu_device *iommu,
			  const struct iommu_ops *ops, struct device *hwdev)
{
	int err = 0;

	/* We need to be able to take module references appropriately */
	if (WARN_ON(is_module_address((unsigned long)ops) && !ops->owner))
		return -EINVAL;
	/*
	 * Temporarily enforce global restriction to a single driver. This was
	 * already the de-facto behaviour, since any possible combination of
	 * existing drivers would compete for at least the PCI or platform bus.
	 */
	if (iommu_buses[0]->iommu_ops && iommu_buses[0]->iommu_ops != ops
				&& !trace_android_vh_bus_iommu_probe_enabled())
		return -EBUSY;

	iommu->ops = ops;
	if (hwdev)
		iommu->fwnode = dev_fwnode(hwdev);

	spin_lock(&iommu_device_lock);
	list_add_tail(&iommu->list, &iommu_device_list);
	spin_unlock(&iommu_device_lock);

	for (int i = 0; i < ARRAY_SIZE(iommu_buses) && !err; i++) {
		bool skip = false;

		trace_android_vh_bus_iommu_probe(iommu, iommu_buses[i], &skip);
		if (skip)
			continue;
		iommu_buses[i]->iommu_ops = ops;
		err = bus_iommu_probe(iommu_buses[i]);
	}
	if (err)
		iommu_device_unregister(iommu);
	return err;
}
EXPORT_SYMBOL_GPL(iommu_device_register);

void iommu_device_unregister(struct iommu_device *iommu)
{
	for (int i = 0; i < ARRAY_SIZE(iommu_buses); i++)
		bus_for_each_dev(iommu_buses[i], NULL, iommu, remove_iommu_group);

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
	struct dev_iommu *param = dev->iommu;

	dev->iommu = NULL;
	if (param->fwspec) {
		fwnode_handle_put(param->fwspec->iommu_fwnode);
		kfree(param->fwspec);
	}
	kfree(param);
}

static u32 dev_iommu_get_max_pasids(struct device *dev)
{
	u32 max_pasids = 0, bits = 0;
	int ret;

	if (dev_is_pci(dev)) {
		ret = pci_max_pasids(to_pci_dev(dev));
		if (ret > 0)
			max_pasids = ret;
	} else {
		ret = device_property_read_u32(dev, "pasid-num-bits", &bits);
		if (!ret)
			max_pasids = 1UL << bits;
	}

	return min_t(u32, max_pasids, dev->iommu->iommu_dev->max_pasids);
}

static int __iommu_probe_device(struct device *dev, struct list_head *group_list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_device *iommu_dev;
	struct iommu_group *group;
	static DEFINE_MUTEX(iommu_probe_device_lock);
	int ret;

	if (!ops)
		return -ENODEV;
	/*
	 * Serialise to avoid races between IOMMU drivers registering in
	 * parallel and/or the "replay" calls from ACPI/OF code via client
	 * driver probe. Once the latter have been cleaned up we should
	 * probably be able to use device_lock() here to minimise the scope,
	 * but for now enforcing a simple global ordering is fine.
	 */
	mutex_lock(&iommu_probe_device_lock);
	if (!dev_iommu_get(dev)) {
		ret = -ENOMEM;
		goto err_unlock;
	}

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
	dev->iommu->max_pasids = dev_iommu_get_max_pasids(dev);

	group = iommu_group_get_for_dev(dev);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_release;
	}

	mutex_lock(&group->mutex);
	if (group_list && !group->default_domain && list_empty(&group->entry))
		list_add_tail(&group->entry, group_list);
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	mutex_unlock(&iommu_probe_device_lock);
	iommu_device_link(iommu_dev, dev);

	return 0;

out_release:
	if (ops->release_device)
		ops->release_device(dev);

out_module_put:
	module_put(ops->owner);

err_free:
	dev_iommu_free(dev);

err_unlock:
	mutex_unlock(&iommu_probe_device_lock);

	return ret;
}

int iommu_probe_device(struct device *dev)
{
	const struct iommu_ops *ops;
	struct iommu_group *group;
	int ret;

	ret = __iommu_probe_device(dev, NULL);
	if (ret)
		goto err_out;

	group = iommu_group_get(dev);
	if (!group) {
		ret = -ENODEV;
		goto err_release;
	}

	/*
	 * Try to allocate a default domain - needs support from the
	 * IOMMU driver. There are still some drivers which don't
	 * support default domains, so the return value is not yet
	 * checked.
	 */
	mutex_lock(&group->mutex);
	iommu_alloc_default_domain(group, dev);

	/*
	 * If device joined an existing group which has been claimed, don't
	 * attach the default domain.
	 */
	if (group->default_domain && !group->owner) {
		ret = __iommu_attach_device(group->default_domain, dev);
		if (ret) {
			mutex_unlock(&group->mutex);
			iommu_group_put(group);
			goto err_release;
		}
	}

	iommu_create_device_direct_mappings(group, dev);

	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	ops = dev_iommu_ops(dev);
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
	const struct iommu_ops *ops;

	if (!dev->iommu)
		return;

	iommu_device_unlink(dev->iommu->iommu_dev, dev);

	ops = dev_iommu_ops(dev);
	if (ops->release_device)
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
	int ret = kstrtobool(str, &iommu_dma_strict);

	if (!ret)
		iommu_cmd_line |= IOMMU_CMD_LINE_STRICT;
	return ret;
}
early_param("iommu.strict", iommu_dma_setup);

void iommu_set_dma_strict(void)
{
	iommu_dma_strict = true;
	if (iommu_def_domain_type == IOMMU_DOMAIN_DMA_FQ)
		iommu_def_domain_type = IOMMU_DOMAIN_DMA;
}

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
				     new->prot, new->type, GFP_KERNEL);
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

		/*
		 * Non-API groups still expose reserved_regions in sysfs,
		 * so filter out calls that get here that way.
		 */
		if (!device->dev->iommu)
			break;

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

	mutex_lock(&group->mutex);
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
		case IOMMU_DOMAIN_DMA_FQ:
			type = "DMA-FQ\n";
			break;
		}
	}
	mutex_unlock(&group->mutex);
	strcpy(buf, type);

	return strlen(type);
}

static IOMMU_GROUP_ATTR(name, S_IRUGO, iommu_group_show_name, NULL);

static IOMMU_GROUP_ATTR(reserved_regions, 0444,
			iommu_group_show_resv_regions, NULL);

static IOMMU_GROUP_ATTR(type, 0644, iommu_group_show_type,
			iommu_group_store_type);

static void iommu_group_release(struct kobject *kobj)
{
	struct iommu_group *group = to_iommu_group(kobj);

	pr_debug("Releasing group %d\n", group->id);

	if (group->iommu_data_release)
		group->iommu_data_release(group->iommu_data);

	ida_free(&iommu_group_ida, group->id);

	if (group->default_domain)
		iommu_domain_free(group->default_domain);
	if (group->blocking_domain)
		iommu_domain_free(group->blocking_domain);

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
	xa_init(&group->pasid_array);

	ret = ida_alloc(&iommu_group_ida, GFP_KERNEL);
	if (ret < 0) {
		kfree(group);
		return ERR_PTR(ret);
	}
	group->id = ret;

	ret = kobject_init_and_add(&group->kobj, &iommu_group_ktype,
				   NULL, "%d", group->id);
	if (ret) {
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
	if (ret) {
		kobject_put(group->devices_kobj);
		return ERR_PTR(ret);
	}

	ret = iommu_group_create_file(group, &iommu_group_attr_type);
	if (ret) {
		kobject_put(group->devices_kobj);
		return ERR_PTR(ret);
	}

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

	if (!domain || !iommu_is_dma_domain(domain))
		return 0;

	BUG_ON(!domain->pgsize_bitmap);

	pg_size = 1UL << __ffs(domain->pgsize_bitmap);
	INIT_LIST_HEAD(&mappings);

	iommu_get_resv_regions(dev, &mappings);

	/* We need to consider overlapping regions for different devices */
	list_for_each_entry(entry, &mappings, list) {
		dma_addr_t start, end, addr;
		size_t map_size = 0;

		start = ALIGN(entry->start, pg_size);
		end   = ALIGN(entry->start + entry->length, pg_size);

		if (entry->type != IOMMU_RESV_DIRECT &&
		    entry->type != IOMMU_RESV_DIRECT_RELAXABLE)
			continue;

		for (addr = start; addr <= end; addr += pg_size) {
			phys_addr_t phys_addr;

			if (addr == end)
				goto map_end;

			phys_addr = iommu_iova_to_phys(domain, addr);
			if (!phys_addr) {
				map_size += pg_size;
				continue;
			}

map_end:
			if (map_size) {
				ret = iommu_map(domain, addr - map_size,
						addr - map_size, map_size,
						entry->prot);
				if (ret)
					goto out;
				map_size = 0;
			}
		}

	}

	iommu_flush_iotlb_all(domain);

out:
	iommu_put_resv_regions(dev, &mappings);

	return ret;
}

static bool iommu_is_attach_deferred(struct device *dev)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (ops->is_attach_deferred)
		return ops->is_attach_deferred(dev);

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
	if (group->domain  && !iommu_is_attach_deferred(dev))
		ret = __iommu_attach_device(group->domain, dev);
	mutex_unlock(&group->mutex);
	if (ret)
		goto err_put_group;

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

	if (!group)
		return;

	dev_info(dev, "Removing from iommu group %d\n", group->id);

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
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	bool has_pasid = msg->flags & IOMMU_PAGE_RESP_PASID_VALID;

	if (!ops->page_response)
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

		ret = ops->page_response(dev, evt, msg);
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
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (dev_is_pci(dev) && to_pci_dev(dev)->untrusted)
		return IOMMU_DOMAIN_DMA;

	if (ops->def_domain_type)
		return ops->def_domain_type(dev);

	return 0;
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
	return 0;
}

static int iommu_alloc_default_domain(struct iommu_group *group,
				      struct device *dev)
{
	unsigned int type;

	if (group->default_domain)
		return 0;

	type = iommu_get_def_domain_type(dev) ? : iommu_def_domain_type;

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
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	struct iommu_group *group;
	int ret;

	group = iommu_group_get(dev);
	if (group)
		return group;

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

static int iommu_bus_notifier(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct device *dev = data;

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		int ret;

		ret = iommu_probe_device(dev);
		return (ret) ? NOTIFY_DONE : NOTIFY_OK;
	} else if (action == BUS_NOTIFY_REMOVED_DEVICE) {
		iommu_release_device(dev);
		return NOTIFY_OK;
	}

	return 0;
}

struct __group_domain_type {
	struct device *dev;
	unsigned int type;
};

static int probe_get_default_domain_type(struct device *dev, void *data)
{
	struct __group_domain_type *gtype = data;
	unsigned int type = iommu_get_def_domain_type(dev);

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

	if (!iommu_is_attach_deferred(dev))
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
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (ops->probe_finalize)
		ops->probe_finalize(dev);

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
		mutex_lock(&group->mutex);

		/* Remove item from the list */
		list_del_init(&group->entry);

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

bool iommu_present(struct bus_type *bus)
{
	return bus->iommu_ops != NULL;
}
EXPORT_SYMBOL_GPL(iommu_present);

/**
 * device_iommu_capable() - check for a general IOMMU capability
 * @dev: device to which the capability would be relevant, if available
 * @cap: IOMMU capability
 *
 * Return: true if an IOMMU is present and supports the given capability
 * for the given device, otherwise false.
 */
bool device_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	const struct iommu_ops *ops;

	if (!dev->iommu || !dev->iommu->iommu_dev)
		return false;

	ops = dev_iommu_ops(dev);
	if (!ops->capable)
		return false;

	return ops->capable(dev, cap);
}
EXPORT_SYMBOL_GPL(device_iommu_capable);

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

	domain->type = type;
	/*
	 * If not already set, assume all sizes by default; the driver
	 * may override this later
	 */
	if (!domain->pgsize_bitmap)
		domain->pgsize_bitmap = bus->iommu_ops->pgsize_bitmap;

	if (!domain->ops)
		domain->ops = bus->iommu_ops->default_domain_ops;

	if (iommu_is_dma_domain(domain) && iommu_get_dma_cookie(domain)) {
		iommu_domain_free(domain);
		domain = NULL;
	}
	return domain;
}

struct iommu_domain *iommu_domain_alloc(struct bus_type *bus)
{
	return __iommu_domain_alloc(bus, IOMMU_DOMAIN_UNMANAGED);
}
EXPORT_SYMBOL_GPL(iommu_domain_alloc);

void iommu_domain_free(struct iommu_domain *domain)
{
	if (domain->type == IOMMU_DOMAIN_SVA)
		mmdrop(domain->mm);
	iommu_put_dma_cookie(domain);
	domain->ops->free(domain);
}
EXPORT_SYMBOL_GPL(iommu_domain_free);

/*
 * Put the group's domain back to the appropriate core-owned domain - either the
 * standard kernel-mode DMA configuration or an all-DMA-blocked domain.
 */
static void __iommu_group_set_core_domain(struct iommu_group *group)
{
	struct iommu_domain *new_domain;
	int ret;

	if (group->owner)
		new_domain = group->blocking_domain;
	else
		new_domain = group->default_domain;

	ret = __iommu_group_set_domain(group, new_domain);
	WARN(ret, "iommu driver failed to attach the default/blocking domain");
}

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

int iommu_deferred_attach(struct device *dev, struct iommu_domain *domain)
{
	if (iommu_is_attach_deferred(dev))
		return __iommu_attach_device(domain, dev);

	return 0;
}

static void __iommu_detach_device(struct iommu_domain *domain,
				  struct device *dev)
{
	if (iommu_is_attach_deferred(dev))
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
	if (WARN_ON(domain != group->domain) ||
	    WARN_ON(iommu_group_device_count(group) != 1))
		goto out_unlock;
	__iommu_group_set_core_domain(group);

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

	if (group->domain && group->domain != group->default_domain &&
	    group->domain != group->blocking_domain)
		return -EBUSY;

	ret = __iommu_group_for_each_dev(group, domain,
					 iommu_group_do_attach_device);
	if (ret == 0) {
		group->domain = domain;
	} else {
		/*
		 * To recover from the case when certain device within the
		 * group fails to attach to the new domain, we need force
		 * attaching all devices back to the old domain. The old
		 * domain is compatible for all devices in the group,
		 * hence the iommu driver should always return success.
		 */
		struct iommu_domain *old_domain = group->domain;

		group->domain = NULL;
		WARN(__iommu_group_set_domain(group, old_domain),
		     "iommu driver failed to attach a compatible domain");
	}

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

static int __iommu_group_set_domain(struct iommu_group *group,
				    struct iommu_domain *new_domain)
{
	int ret;

	if (group->domain == new_domain)
		return 0;

	/*
	 * New drivers should support default domains and so the detach_dev() op
	 * will never be called. Otherwise the NULL domain represents some
	 * platform specific behavior.
	 */
	if (!new_domain) {
		if (WARN_ON(!group->domain->ops->detach_dev))
			return -EINVAL;
		__iommu_group_for_each_dev(group, group->domain,
					   iommu_group_do_detach_device);
		group->domain = NULL;
		return 0;
	}

	/*
	 * Changing the domain is done by calling attach_dev() on the new
	 * domain. This switch does not have to be atomic and DMA can be
	 * discarded during the transition. DMA must only be able to access
	 * either new_domain or group->domain, never something else.
	 *
	 * Note that this is called in error unwind paths, attaching to a
	 * domain that has already been attached cannot fail.
	 */
	ret = __iommu_group_for_each_dev(group, new_domain,
					 iommu_group_do_attach_device);
	if (ret)
		return ret;
	group->domain = new_domain;
	return 0;
}

void iommu_detach_group(struct iommu_domain *domain, struct iommu_group *group)
{
	mutex_lock(&group->mutex);
	__iommu_group_set_core_domain(group);
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_detach_group);

phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	if (domain->type == IOMMU_DOMAIN_IDENTITY)
		return iova;

	if (domain->type == IOMMU_DOMAIN_BLOCKED)
		return 0;

	return domain->ops->iova_to_phys(domain, iova);
}
EXPORT_SYMBOL_GPL(iommu_iova_to_phys);

static size_t iommu_pgsize(struct iommu_domain *domain, unsigned long iova,
			   phys_addr_t paddr, size_t size, size_t *count)
{
	unsigned int pgsize_idx, pgsize_idx_next;
	unsigned long pgsizes;
	size_t offset, pgsize, pgsize_next;
	unsigned long addr_merge = paddr | iova;

	/* Page sizes supported by the hardware and small enough for @size */
	pgsizes = domain->pgsize_bitmap & GENMASK(__fls(size), 0);

	/* Constrain the page sizes further based on the maximum alignment */
	if (likely(addr_merge))
		pgsizes &= GENMASK(__ffs(addr_merge), 0);

	/* Make sure we have at least one suitable page size */
	BUG_ON(!pgsizes);

	/* Pick the biggest page size remaining */
	pgsize_idx = __fls(pgsizes);
	pgsize = BIT(pgsize_idx);
	if (!count)
		return pgsize;

	/* Find the next biggest support page size, if it exists */
	pgsizes = domain->pgsize_bitmap & ~GENMASK(pgsize_idx, 0);
	if (!pgsizes)
		goto out_set_count;

	pgsize_idx_next = __ffs(pgsizes);
	pgsize_next = BIT(pgsize_idx_next);

	/*
	 * There's no point trying a bigger page size unless the virtual
	 * and physical addresses are similarly offset within the larger page.
	 */
	if ((iova ^ paddr) & (pgsize_next - 1))
		goto out_set_count;

	/* Calculate the offset to the next page size alignment boundary */
	offset = pgsize_next - (addr_merge & (pgsize_next - 1));

	/*
	 * If size is big enough to accommodate the larger page, reduce
	 * the number of smaller pages.
	 */
	if (offset + pgsize_next <= size)
		size = offset;

out_set_count:
	*count = size >> pgsize_idx;
	return pgsize;
}

static int __iommu_map_pages(struct iommu_domain *domain, unsigned long iova,
			     phys_addr_t paddr, size_t size, int prot,
			     gfp_t gfp, size_t *mapped)
{
	const struct iommu_domain_ops *ops = domain->ops;
	size_t pgsize, count;
	int ret;

	pgsize = iommu_pgsize(domain, iova, paddr, size, &count);

	pr_debug("mapping: iova 0x%lx pa %pa pgsize 0x%zx count %zu\n",
		 iova, &paddr, pgsize, count);

	if (ops->map_pages) {
		ret = ops->map_pages(domain, iova, paddr, pgsize, count, prot,
				     gfp, mapped);
	} else {
		ret = ops->map(domain, iova, paddr, pgsize, prot, gfp);
		*mapped = ret ? 0 : pgsize;
	}

	return ret;
}

static int __iommu_map(struct iommu_domain *domain, unsigned long iova,
		       phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;
	size_t orig_size = size;
	phys_addr_t orig_paddr = paddr;
	int ret = 0;

	if (unlikely(!(ops->map || ops->map_pages) ||
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
		size_t mapped = 0;

		ret = __iommu_map_pages(domain, iova, paddr, size, prot, gfp,
					&mapped);
		/*
		 * Some pages may have been mapped, even if an error occurred,
		 * so we should account for those so they can be unmapped.
		 */
		size -= mapped;

		if (ret)
			break;

		iova += mapped;
		paddr += mapped;
	}

	/* unroll mapping in case something went wrong */
	if (ret)
		iommu_unmap(domain, orig_iova, orig_size - size);
	else
		trace_map(orig_iova, orig_paddr, orig_size);

	return ret;
}

static int _iommu_map(struct iommu_domain *domain, unsigned long iova,
		      phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
	int ret;

	ret = __iommu_map(domain, iova, paddr, size, prot, gfp);
	if (ret == 0 && ops->iotlb_sync_map)
		ops->iotlb_sync_map(domain, iova, size);

	return ret;
}

int iommu_map(struct iommu_domain *domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot)
{
	might_sleep();
	return _iommu_map(domain, iova, paddr, size, prot, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(iommu_map);

int iommu_map_atomic(struct iommu_domain *domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot)
{
	return _iommu_map(domain, iova, paddr, size, prot, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(iommu_map_atomic);

static size_t __iommu_unmap_pages(struct iommu_domain *domain,
				  unsigned long iova, size_t size,
				  struct iommu_iotlb_gather *iotlb_gather)
{
	const struct iommu_domain_ops *ops = domain->ops;
	size_t pgsize, count;

	pgsize = iommu_pgsize(domain, iova, iova, size, &count);
	return ops->unmap_pages ?
	       ops->unmap_pages(domain, iova, pgsize, count, iotlb_gather) :
	       ops->unmap(domain, iova, pgsize, iotlb_gather);
}

static size_t __iommu_unmap(struct iommu_domain *domain,
			    unsigned long iova, size_t size,
			    struct iommu_iotlb_gather *iotlb_gather)
{
	const struct iommu_domain_ops *ops = domain->ops;
	size_t unmapped_page, unmapped = 0;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;

	if (unlikely(!(ops->unmap || ops->unmap_pages) ||
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
		unmapped_page = __iommu_unmap_pages(domain, iova,
						    size - unmapped,
						    iotlb_gather);
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

static ssize_t __iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
		struct scatterlist *sg, unsigned int nents, int prot,
		gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
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

		if (sg_is_dma_bus_address(sg))
			goto next;

		if (len) {
			len += sg->length;
		} else {
			len = sg->length;
			start = s_phys;
		}

next:
		if (++i < nents)
			sg = sg_next(sg);
	}

	if (ops->iotlb_sync_map)
		ops->iotlb_sync_map(domain, iova, mapped);
	return mapped;

out_err:
	/* undo mappings already done */
	iommu_unmap(domain, iova, mapped);

	return ret;
}

ssize_t iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
		     struct scatterlist *sg, unsigned int nents, int prot)
{
	might_sleep();
	return __iommu_map_sg(domain, iova, sg, nents, prot, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(iommu_map_sg);

ssize_t iommu_map_sg_atomic(struct iommu_domain *domain, unsigned long iova,
		    struct scatterlist *sg, unsigned int nents, int prot)
{
	return __iommu_map_sg(domain, iova, sg, nents, prot, GFP_ATOMIC);
}

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

int iommu_enable_nesting(struct iommu_domain *domain)
{
	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;
	if (!domain->ops->enable_nesting)
		return -EINVAL;
	return domain->ops->enable_nesting(domain);
}
EXPORT_SYMBOL_GPL(iommu_enable_nesting);

int iommu_set_pgtable_quirks(struct iommu_domain *domain,
		unsigned long quirk)
{
	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;
	if (!domain->ops->set_pgtable_quirks)
		return -EINVAL;
	return domain->ops->set_pgtable_quirks(domain, quirk);
}
EXPORT_SYMBOL_GPL(iommu_set_pgtable_quirks);

void iommu_get_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (ops->get_resv_regions)
		ops->get_resv_regions(dev, list);
}

/**
 * iommu_put_resv_regions - release resered regions
 * @dev: device for which to free reserved regions
 * @list: reserved region list for device
 *
 * This releases a reserved region list acquired by iommu_get_resv_regions().
 */
void iommu_put_resv_regions(struct device *dev, struct list_head *list)
{
	struct iommu_resv_region *entry, *next;

	list_for_each_entry_safe(entry, next, list, list) {
		if (entry->free)
			entry->free(dev, entry);
		else
			kfree(entry);
	}
}
EXPORT_SYMBOL(iommu_put_resv_regions);

struct iommu_resv_region *iommu_alloc_resv_region(phys_addr_t start,
						  size_t length, int prot,
						  enum iommu_resv_type type,
						  gfp_t gfp)
{
	struct iommu_resv_region *region;

	region = kzalloc(sizeof(*region), gfp);
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
		iommu_cmd_line |= IOMMU_CMD_LINE_DMA_API;
	iommu_def_domain_type = IOMMU_DOMAIN_IDENTITY;
}

void iommu_set_default_translated(bool cmd_line)
{
	if (cmd_line)
		iommu_cmd_line |= IOMMU_CMD_LINE_DMA_API;
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
int iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features feat)
{
	if (dev->iommu && dev->iommu->iommu_dev) {
		const struct iommu_ops *ops = dev->iommu->iommu_dev->ops;

		if (ops->dev_enable_feat)
			return ops->dev_enable_feat(dev, feat);
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(iommu_dev_enable_feature);

/*
 * The device drivers should do the necessary cleanups before calling this.
 */
int iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features feat)
{
	if (dev->iommu && dev->iommu->iommu_dev) {
		const struct iommu_ops *ops = dev->iommu->iommu_dev->ops;

		if (ops->dev_disable_feat)
			return ops->dev_disable_feat(dev, feat);
	}

	return -EBUSY;
}
EXPORT_SYMBOL_GPL(iommu_dev_disable_feature);

/*
 * Changes the default domain of an iommu group that has *only* one device
 *
 * @group: The group for which the default domain should be changed
 * @prev_dev: The device in the group (this is used to make sure that the device
 *	 hasn't changed after the caller has called this function)
 * @type: The type of the new default domain that gets associated with the group
 *
 * Returns 0 on success and error code on failure
 *
 * Note:
 * 1. Presently, this function is called only when user requests to change the
 *    group's default domain type through /sys/kernel/iommu_groups/<grp_id>/type
 *    Please take a closer look if intended to use for other purposes.
 */
static int iommu_change_dev_def_domain(struct iommu_group *group,
				       struct device *prev_dev, int type)
{
	struct iommu_domain *prev_dom;
	struct group_device *grp_dev;
	int ret, dev_def_dom;
	struct device *dev;

	mutex_lock(&group->mutex);

	if (group->default_domain != group->domain) {
		dev_err_ratelimited(prev_dev, "Group not assigned to default domain\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * iommu group wasn't locked while acquiring device lock in
	 * iommu_group_store_type(). So, make sure that the device count hasn't
	 * changed while acquiring device lock.
	 *
	 * Changing default domain of an iommu group with two or more devices
	 * isn't supported because there could be a potential deadlock. Consider
	 * the following scenario. T1 is trying to acquire device locks of all
	 * the devices in the group and before it could acquire all of them,
	 * there could be another thread T2 (from different sub-system and use
	 * case) that has already acquired some of the device locks and might be
	 * waiting for T1 to release other device locks.
	 */
	if (iommu_group_device_count(group) != 1) {
		dev_err_ratelimited(prev_dev, "Cannot change default domain: Group has more than one device\n");
		ret = -EINVAL;
		goto out;
	}

	/* Since group has only one device */
	grp_dev = list_first_entry(&group->devices, struct group_device, list);
	dev = grp_dev->dev;

	if (prev_dev != dev) {
		dev_err_ratelimited(prev_dev, "Cannot change default domain: Device has been changed\n");
		ret = -EBUSY;
		goto out;
	}

	prev_dom = group->default_domain;
	if (!prev_dom) {
		ret = -EINVAL;
		goto out;
	}

	dev_def_dom = iommu_get_def_domain_type(dev);
	if (!type) {
		/*
		 * If the user hasn't requested any specific type of domain and
		 * if the device supports both the domains, then default to the
		 * domain the device was booted with
		 */
		type = dev_def_dom ? : iommu_def_domain_type;
	} else if (dev_def_dom && type != dev_def_dom) {
		dev_err_ratelimited(prev_dev, "Device cannot be in %s domain\n",
				    iommu_domain_type_str(type));
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Switch to a new domain only if the requested domain type is different
	 * from the existing default domain type
	 */
	if (prev_dom->type == type) {
		ret = 0;
		goto out;
	}

	/* We can bring up a flush queue without tearing down the domain */
	if (type == IOMMU_DOMAIN_DMA_FQ && prev_dom->type == IOMMU_DOMAIN_DMA) {
		ret = iommu_dma_init_fq(prev_dom);
		if (!ret)
			prev_dom->type = IOMMU_DOMAIN_DMA_FQ;
		goto out;
	}

	/* Sets group->default_domain to the newly allocated domain */
	ret = iommu_group_alloc_default_domain(dev->bus, group, type);
	if (ret)
		goto out;

	ret = iommu_create_device_direct_mappings(group, dev);
	if (ret)
		goto free_new_domain;

	ret = __iommu_attach_device(group->default_domain, dev);
	if (ret)
		goto free_new_domain;

	group->domain = group->default_domain;

	/*
	 * Release the mutex here because ops->probe_finalize() call-back of
	 * some vendor IOMMU drivers calls arm_iommu_attach_device() which
	 * in-turn might call back into IOMMU core code, where it tries to take
	 * group->mutex, resulting in a deadlock.
	 */
	mutex_unlock(&group->mutex);

	/* Make sure dma_ops is appropriatley set */
	iommu_group_do_probe_finalize(dev, group->default_domain);
	iommu_domain_free(prev_dom);
	return 0;

free_new_domain:
	iommu_domain_free(group->default_domain);
	group->default_domain = prev_dom;
	group->domain = prev_dom;

out:
	mutex_unlock(&group->mutex);

	return ret;
}

/*
 * Changing the default domain through sysfs requires the users to unbind the
 * drivers from the devices in the iommu group, except for a DMA -> DMA-FQ
 * transition. Return failure if this isn't met.
 *
 * We need to consider the race between this and the device release path.
 * device_lock(dev) is used here to guarantee that the device release path
 * will not be entered at the same time.
 */
static ssize_t iommu_group_store_type(struct iommu_group *group,
				      const char *buf, size_t count)
{
	struct group_device *grp_dev;
	struct device *dev;
	int ret, req_type;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;

	if (WARN_ON(!group) || !group->default_domain)
		return -EINVAL;

	if (sysfs_streq(buf, "identity"))
		req_type = IOMMU_DOMAIN_IDENTITY;
	else if (sysfs_streq(buf, "DMA"))
		req_type = IOMMU_DOMAIN_DMA;
	else if (sysfs_streq(buf, "DMA-FQ"))
		req_type = IOMMU_DOMAIN_DMA_FQ;
	else if (sysfs_streq(buf, "auto"))
		req_type = 0;
	else
		return -EINVAL;

	/*
	 * Lock/Unlock the group mutex here before device lock to
	 * 1. Make sure that the iommu group has only one device (this is a
	 *    prerequisite for step 2)
	 * 2. Get struct *dev which is needed to lock device
	 */
	mutex_lock(&group->mutex);
	if (iommu_group_device_count(group) != 1) {
		mutex_unlock(&group->mutex);
		pr_err_ratelimited("Cannot change default domain: Group has more than one device\n");
		return -EINVAL;
	}

	/* Since group has only one device */
	grp_dev = list_first_entry(&group->devices, struct group_device, list);
	dev = grp_dev->dev;
	get_device(dev);

	/*
	 * Don't hold the group mutex because taking group mutex first and then
	 * the device lock could potentially cause a deadlock as below. Assume
	 * two threads T1 and T2. T1 is trying to change default domain of an
	 * iommu group and T2 is trying to hot unplug a device or release [1] VF
	 * of a PCIe device which is in the same iommu group. T1 takes group
	 * mutex and before it could take device lock assume T2 has taken device
	 * lock and is yet to take group mutex. Now, both the threads will be
	 * waiting for the other thread to release lock. Below, lock order was
	 * suggested.
	 * device_lock(dev);
	 *	mutex_lock(&group->mutex);
	 *		iommu_change_dev_def_domain();
	 *	mutex_unlock(&group->mutex);
	 * device_unlock(dev);
	 *
	 * [1] Typical device release path
	 * device_lock() from device/driver core code
	 *  -> bus_notifier()
	 *   -> iommu_bus_notifier()
	 *    -> iommu_release_device()
	 *     -> ops->release_device() vendor driver calls back iommu core code
	 *      -> mutex_lock() from iommu core code
	 */
	mutex_unlock(&group->mutex);

	/* Check if the device in the group still has a driver bound to it */
	device_lock(dev);
	if (device_is_bound(dev) && !(req_type == IOMMU_DOMAIN_DMA_FQ &&
	    group->default_domain->type == IOMMU_DOMAIN_DMA)) {
		pr_err_ratelimited("Device is still bound to driver\n");
		ret = -EBUSY;
		goto out;
	}

	ret = iommu_change_dev_def_domain(group, dev, req_type);
	ret = ret ?: count;

out:
	device_unlock(dev);
	put_device(dev);

	return ret;
}

static bool iommu_is_default_domain(struct iommu_group *group)
{
	if (group->domain == group->default_domain)
		return true;

	/*
	 * If the default domain was set to identity and it is still an identity
	 * domain then we consider this a pass. This happens because of
	 * amd_iommu_init_device() replacing the default idenytity domain with an
	 * identity domain that has a different configuration for AMDGPU.
	 */
	if (group->default_domain &&
	    group->default_domain->type == IOMMU_DOMAIN_IDENTITY &&
	    group->domain && group->domain->type == IOMMU_DOMAIN_IDENTITY)
		return true;
	return false;
}

/**
 * iommu_device_use_default_domain() - Device driver wants to handle device
 *                                     DMA through the kernel DMA API.
 * @dev: The device.
 *
 * The device driver about to bind @dev wants to do DMA through the kernel
 * DMA API. Return 0 if it is allowed, otherwise an error.
 */
int iommu_device_use_default_domain(struct device *dev)
{
	struct iommu_group *group = iommu_group_get(dev);
	int ret = 0;

	if (!group)
		return 0;

	mutex_lock(&group->mutex);
	if (group->owner_cnt) {
		if (group->owner || !iommu_is_default_domain(group) ||
		    !xa_empty(&group->pasid_array)) {
			ret = -EBUSY;
			goto unlock_out;
		}
	}

	group->owner_cnt++;

unlock_out:
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	return ret;
}

/**
 * iommu_device_unuse_default_domain() - Device driver stops handling device
 *                                       DMA through the kernel DMA API.
 * @dev: The device.
 *
 * The device driver doesn't want to do DMA through kernel DMA API anymore.
 * It must be called after iommu_device_use_default_domain().
 */
void iommu_device_unuse_default_domain(struct device *dev)
{
	struct iommu_group *group = iommu_group_get(dev);

	if (!group)
		return;

	mutex_lock(&group->mutex);
	if (!WARN_ON(!group->owner_cnt || !xa_empty(&group->pasid_array)))
		group->owner_cnt--;

	mutex_unlock(&group->mutex);
	iommu_group_put(group);
}

static int __iommu_group_alloc_blocking_domain(struct iommu_group *group)
{
	struct group_device *dev =
		list_first_entry(&group->devices, struct group_device, list);

	if (group->blocking_domain)
		return 0;

	group->blocking_domain =
		__iommu_domain_alloc(dev->dev->bus, IOMMU_DOMAIN_BLOCKED);
	if (!group->blocking_domain) {
		/*
		 * For drivers that do not yet understand IOMMU_DOMAIN_BLOCKED
		 * create an empty domain instead.
		 */
		group->blocking_domain = __iommu_domain_alloc(
			dev->dev->bus, IOMMU_DOMAIN_UNMANAGED);
		if (!group->blocking_domain)
			return -EINVAL;
	}
	return 0;
}

/**
 * iommu_group_claim_dma_owner() - Set DMA ownership of a group
 * @group: The group.
 * @owner: Caller specified pointer. Used for exclusive ownership.
 *
 * This is to support backward compatibility for vfio which manages
 * the dma ownership in iommu_group level. New invocations on this
 * interface should be prohibited.
 */
int iommu_group_claim_dma_owner(struct iommu_group *group, void *owner)
{
	int ret = 0;

	mutex_lock(&group->mutex);
	if (group->owner_cnt) {
		ret = -EPERM;
		goto unlock_out;
	} else {
		if ((group->domain && group->domain != group->default_domain) ||
		    !xa_empty(&group->pasid_array)) {
			ret = -EBUSY;
			goto unlock_out;
		}

		ret = __iommu_group_alloc_blocking_domain(group);
		if (ret)
			goto unlock_out;

		ret = __iommu_group_set_domain(group, group->blocking_domain);
		if (ret)
			goto unlock_out;
		group->owner = owner;
	}

	group->owner_cnt++;
unlock_out:
	mutex_unlock(&group->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_group_claim_dma_owner);

/**
 * iommu_group_release_dma_owner() - Release DMA ownership of a group
 * @group: The group.
 *
 * Release the DMA ownership claimed by iommu_group_claim_dma_owner().
 */
void iommu_group_release_dma_owner(struct iommu_group *group)
{
	int ret;

	mutex_lock(&group->mutex);
	if (WARN_ON(!group->owner_cnt || !group->owner ||
		    !xa_empty(&group->pasid_array)))
		goto unlock_out;

	group->owner_cnt = 0;
	group->owner = NULL;
	ret = __iommu_group_set_domain(group, group->default_domain);
	WARN(ret, "iommu driver failed to attach the default domain");

unlock_out:
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_group_release_dma_owner);

/**
 * iommu_group_dma_owner_claimed() - Query group dma ownership status
 * @group: The group.
 *
 * This provides status query on a given group. It is racy and only for
 * non-binding status reporting.
 */
bool iommu_group_dma_owner_claimed(struct iommu_group *group)
{
	unsigned int user;

	mutex_lock(&group->mutex);
	user = group->owner_cnt;
	mutex_unlock(&group->mutex);

	return user;
}
EXPORT_SYMBOL_GPL(iommu_group_dma_owner_claimed);

static int __iommu_set_group_pasid(struct iommu_domain *domain,
				   struct iommu_group *group, ioasid_t pasid)
{
	struct group_device *device;
	int ret = 0;

	list_for_each_entry(device, &group->devices, list) {
		ret = domain->ops->set_dev_pasid(domain, device->dev, pasid);
		if (ret)
			break;
	}

	return ret;
}

static void __iommu_remove_group_pasid(struct iommu_group *group,
				       ioasid_t pasid)
{
	struct group_device *device;
	const struct iommu_ops *ops;

	list_for_each_entry(device, &group->devices, list) {
		ops = dev_iommu_ops(device->dev);
		ops->remove_dev_pasid(device->dev, pasid);
	}
}

/*
 * iommu_attach_device_pasid() - Attach a domain to pasid of device
 * @domain: the iommu domain.
 * @dev: the attached device.
 * @pasid: the pasid of the device.
 *
 * Return: 0 on success, or an error.
 */
int iommu_attach_device_pasid(struct iommu_domain *domain,
			      struct device *dev, ioasid_t pasid)
{
	struct iommu_group *group;
	void *curr;
	int ret;

	if (!domain->ops->set_dev_pasid)
		return -EOPNOTSUPP;

	group = iommu_group_get(dev);
	if (!group)
		return -ENODEV;

	mutex_lock(&group->mutex);
	curr = xa_cmpxchg(&group->pasid_array, pasid, NULL, domain, GFP_KERNEL);
	if (curr) {
		ret = xa_err(curr) ? : -EBUSY;
		goto out_unlock;
	}

	ret = __iommu_set_group_pasid(domain, group, pasid);
	if (ret) {
		__iommu_remove_group_pasid(group, pasid);
		xa_erase(&group->pasid_array, pasid);
	}
out_unlock:
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_attach_device_pasid);

/*
 * iommu_detach_device_pasid() - Detach the domain from pasid of device
 * @domain: the iommu domain.
 * @dev: the attached device.
 * @pasid: the pasid of the device.
 *
 * The @domain must have been attached to @pasid of the @dev with
 * iommu_attach_device_pasid().
 */
void iommu_detach_device_pasid(struct iommu_domain *domain, struct device *dev,
			       ioasid_t pasid)
{
	struct iommu_group *group = iommu_group_get(dev);

	mutex_lock(&group->mutex);
	__iommu_remove_group_pasid(group, pasid);
	WARN_ON(xa_erase(&group->pasid_array, pasid) != domain);
	mutex_unlock(&group->mutex);

	iommu_group_put(group);
}
EXPORT_SYMBOL_GPL(iommu_detach_device_pasid);

/*
 * iommu_get_domain_for_dev_pasid() - Retrieve domain for @pasid of @dev
 * @dev: the queried device
 * @pasid: the pasid of the device
 * @type: matched domain type, 0 for any match
 *
 * This is a variant of iommu_get_domain_for_dev(). It returns the existing
 * domain attached to pasid of a device. Callers must hold a lock around this
 * function, and both iommu_attach/detach_dev_pasid() whenever a domain of
 * type is being manipulated. This API does not internally resolve races with
 * attach/detach.
 *
 * Return: attached domain on success, NULL otherwise.
 */
struct iommu_domain *iommu_get_domain_for_dev_pasid(struct device *dev,
						    ioasid_t pasid,
						    unsigned int type)
{
	struct iommu_domain *domain;
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		return NULL;

	xa_lock(&group->pasid_array);
	domain = xa_load(&group->pasid_array, pasid);
	if (type && domain && domain->type != type)
		domain = ERR_PTR(-EBUSY);
	xa_unlock(&group->pasid_array);
	iommu_group_put(group);

	return domain;
}
EXPORT_SYMBOL_GPL(iommu_get_domain_for_dev_pasid);

struct iommu_domain *iommu_sva_domain_alloc(struct device *dev,
					    struct mm_struct *mm)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	struct iommu_domain *domain;

	domain = ops->domain_alloc(IOMMU_DOMAIN_SVA);
	if (!domain)
		return NULL;

	domain->type = IOMMU_DOMAIN_SVA;
	mmgrab(mm);
	domain->mm = mm;
	domain->iopf_handler = iommu_sva_handle_iopf;
	domain->fault_data = mm;

	return domain;
}
