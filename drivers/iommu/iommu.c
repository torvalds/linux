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
#include <linux/cdx/cdx_bus.h>
#include <trace/events/iommu.h>
#include <linux/sched/mm.h>
#include <linux/msi.h>

#include "dma-iommu.h"
#include "iommu-priv.h"

static struct kset *iommu_group_kset;
static DEFINE_IDA(iommu_group_ida);
static DEFINE_IDA(iommu_global_pasid_ida);

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

/* Iterate over each struct group_device in a struct iommu_group */
#define for_each_group_device(group, pos) \
	list_for_each_entry(pos, &(group)->devices, list)

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
static void iommu_release_device(struct device *dev);
static struct iommu_domain *
__iommu_group_domain_alloc(struct iommu_group *group, unsigned int type);
static int __iommu_attach_device(struct iommu_domain *domain,
				 struct device *dev);
static int __iommu_attach_group(struct iommu_domain *domain,
				struct iommu_group *group);

enum {
	IOMMU_SET_DOMAIN_MUST_SUCCEED = 1 << 0,
};

static int __iommu_device_set_domain(struct iommu_group *group,
				     struct device *dev,
				     struct iommu_domain *new_domain,
				     unsigned int flags);
static int __iommu_group_set_domain_internal(struct iommu_group *group,
					     struct iommu_domain *new_domain,
					     unsigned int flags);
static int __iommu_group_set_domain(struct iommu_group *group,
				    struct iommu_domain *new_domain)
{
	return __iommu_group_set_domain_internal(group, new_domain, 0);
}
static void __iommu_group_set_domain_nofail(struct iommu_group *group,
					    struct iommu_domain *new_domain)
{
	WARN_ON(__iommu_group_set_domain_internal(
		group, new_domain, IOMMU_SET_DOMAIN_MUST_SUCCEED));
}

static int iommu_setup_default_domain(struct iommu_group *group,
				      int target_type);
static int iommu_create_device_direct_mappings(struct iommu_domain *domain,
					       struct device *dev);
static ssize_t iommu_group_store_type(struct iommu_group *group,
				      const char *buf, size_t count);
static struct group_device *iommu_group_alloc_device(struct iommu_group *group,
						     struct device *dev);
static void __iommu_group_free_device(struct iommu_group *group,
				      struct group_device *grp_dev);

#define IOMMU_GROUP_ATTR(_name, _mode, _show, _store)		\
struct iommu_group_attribute iommu_group_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

#define to_iommu_group_attr(_attr)	\
	container_of(_attr, struct iommu_group_attribute, attr)
#define to_iommu_group(_kobj)		\
	container_of(_kobj, struct iommu_group, kobj)

static LIST_HEAD(iommu_device_list);
static DEFINE_SPINLOCK(iommu_device_lock);

static const struct bus_type * const iommu_buses[] = {
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
#ifdef CONFIG_CDX_BUS
	&cdx_bus_type,
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
	case IOMMU_DOMAIN_PLATFORM:
		return "Platform";
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

	pr_info("Default domain type: %s%s\n",
		iommu_domain_type_str(iommu_def_domain_type),
		(iommu_cmd_line & IOMMU_CMD_LINE_DMA_API) ?
			" (set via kernel command line)" : "");

	if (!iommu_default_passthrough())
		pr_info("DMA domain TLB invalidation policy: %s mode%s\n",
			iommu_dma_strict ? "strict" : "lazy",
			(iommu_cmd_line & IOMMU_CMD_LINE_STRICT) ?
				" (set via kernel command line)" : "");

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

	iommu->ops = ops;
	if (hwdev)
		iommu->fwnode = dev_fwnode(hwdev);

	spin_lock(&iommu_device_lock);
	list_add_tail(&iommu->list, &iommu_device_list);
	spin_unlock(&iommu_device_lock);

	for (int i = 0; i < ARRAY_SIZE(iommu_buses) && !err; i++)
		err = bus_iommu_probe(iommu_buses[i]);
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

	/* Pairs with the alloc in generic_single_device_group() */
	iommu_group_put(iommu->singleton_group);
	iommu->singleton_group = NULL;
}
EXPORT_SYMBOL_GPL(iommu_device_unregister);

#if IS_ENABLED(CONFIG_IOMMUFD_TEST)
void iommu_device_unregister_bus(struct iommu_device *iommu,
				 const struct bus_type *bus,
				 struct notifier_block *nb)
{
	bus_unregister_notifier(bus, nb);
	iommu_device_unregister(iommu);
}
EXPORT_SYMBOL_GPL(iommu_device_unregister_bus);

/*
 * Register an iommu driver against a single bus. This is only used by iommufd
 * selftest to create a mock iommu driver. The caller must provide
 * some memory to hold a notifier_block.
 */
int iommu_device_register_bus(struct iommu_device *iommu,
			      const struct iommu_ops *ops,
			      const struct bus_type *bus,
			      struct notifier_block *nb)
{
	int err;

	iommu->ops = ops;
	nb->notifier_call = iommu_bus_notifier;
	err = bus_register_notifier(bus, nb);
	if (err)
		return err;

	spin_lock(&iommu_device_lock);
	list_add_tail(&iommu->list, &iommu_device_list);
	spin_unlock(&iommu_device_lock);

	err = bus_iommu_probe(bus);
	if (err) {
		iommu_device_unregister_bus(iommu, bus, nb);
		return err;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(iommu_device_register_bus);
#endif

static struct dev_iommu *dev_iommu_get(struct device *dev)
{
	struct dev_iommu *param = dev->iommu;

	lockdep_assert_held(&iommu_probe_device_lock);

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

/*
 * Internal equivalent of device_iommu_mapped() for when we care that a device
 * actually has API ops, and don't want false positives from VFIO-only groups.
 */
static bool dev_has_iommu(struct device *dev)
{
	return dev->iommu && dev->iommu->iommu_dev;
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

void dev_iommu_priv_set(struct device *dev, void *priv)
{
	/* FSL_PAMU does something weird */
	if (!IS_ENABLED(CONFIG_FSL_PAMU))
		lockdep_assert_held(&iommu_probe_device_lock);
	dev->iommu->priv = priv;
}
EXPORT_SYMBOL_GPL(dev_iommu_priv_set);

/*
 * Init the dev->iommu and dev->iommu_group in the struct device and get the
 * driver probed
 */
static int iommu_init_device(struct device *dev, const struct iommu_ops *ops)
{
	struct iommu_device *iommu_dev;
	struct iommu_group *group;
	int ret;

	if (!dev_iommu_get(dev))
		return -ENOMEM;

	if (!try_module_get(ops->owner)) {
		ret = -EINVAL;
		goto err_free;
	}

	iommu_dev = ops->probe_device(dev);
	if (IS_ERR(iommu_dev)) {
		ret = PTR_ERR(iommu_dev);
		goto err_module_put;
	}
	dev->iommu->iommu_dev = iommu_dev;

	ret = iommu_device_link(iommu_dev, dev);
	if (ret)
		goto err_release;

	group = ops->device_group(dev);
	if (WARN_ON_ONCE(group == NULL))
		group = ERR_PTR(-EINVAL);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto err_unlink;
	}
	dev->iommu_group = group;

	dev->iommu->max_pasids = dev_iommu_get_max_pasids(dev);
	if (ops->is_attach_deferred)
		dev->iommu->attach_deferred = ops->is_attach_deferred(dev);
	return 0;

err_unlink:
	iommu_device_unlink(iommu_dev, dev);
err_release:
	if (ops->release_device)
		ops->release_device(dev);
err_module_put:
	module_put(ops->owner);
err_free:
	dev->iommu->iommu_dev = NULL;
	dev_iommu_free(dev);
	return ret;
}

static void iommu_deinit_device(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	lockdep_assert_held(&group->mutex);

	iommu_device_unlink(dev->iommu->iommu_dev, dev);

	/*
	 * release_device() must stop using any attached domain on the device.
	 * If there are still other devices in the group, they are not affected
	 * by this callback.
	 *
	 * If the iommu driver provides release_domain, the core code ensures
	 * that domain is attached prior to calling release_device. Drivers can
	 * use this to enforce a translation on the idle iommu. Typically, the
	 * global static blocked_domain is a good choice.
	 *
	 * Otherwise, the iommu driver must set the device to either an identity
	 * or a blocking translation in release_device() and stop using any
	 * domain pointer, as it is going to be freed.
	 *
	 * Regardless, if a delayed attach never occurred, then the release
	 * should still avoid touching any hardware configuration either.
	 */
	if (!dev->iommu->attach_deferred && ops->release_domain)
		ops->release_domain->ops->attach_dev(ops->release_domain, dev);

	if (ops->release_device)
		ops->release_device(dev);

	/*
	 * If this is the last driver to use the group then we must free the
	 * domains before we do the module_put().
	 */
	if (list_empty(&group->devices)) {
		if (group->default_domain) {
			iommu_domain_free(group->default_domain);
			group->default_domain = NULL;
		}
		if (group->blocking_domain) {
			iommu_domain_free(group->blocking_domain);
			group->blocking_domain = NULL;
		}
		group->domain = NULL;
	}

	/* Caller must put iommu_group */
	dev->iommu_group = NULL;
	module_put(ops->owner);
	dev_iommu_free(dev);
}

DEFINE_MUTEX(iommu_probe_device_lock);

static int __iommu_probe_device(struct device *dev, struct list_head *group_list)
{
	const struct iommu_ops *ops;
	struct iommu_fwspec *fwspec;
	struct iommu_group *group;
	struct group_device *gdev;
	int ret;

	/*
	 * For FDT-based systems and ACPI IORT/VIOT, drivers register IOMMU
	 * instances with non-NULL fwnodes, and client devices should have been
	 * identified with a fwspec by this point. Otherwise, we can currently
	 * assume that only one of Intel, AMD, s390, PAMU or legacy SMMUv2 can
	 * be present, and that any of their registered instances has suitable
	 * ops for probing, and thus cheekily co-opt the same mechanism.
	 */
	fwspec = dev_iommu_fwspec_get(dev);
	if (fwspec && fwspec->ops)
		ops = fwspec->ops;
	else
		ops = iommu_ops_from_fwnode(NULL);

	if (!ops)
		return -ENODEV;
	/*
	 * Serialise to avoid races between IOMMU drivers registering in
	 * parallel and/or the "replay" calls from ACPI/OF code via client
	 * driver probe. Once the latter have been cleaned up we should
	 * probably be able to use device_lock() here to minimise the scope,
	 * but for now enforcing a simple global ordering is fine.
	 */
	lockdep_assert_held(&iommu_probe_device_lock);

	/* Device is probed already if in a group */
	if (dev->iommu_group)
		return 0;

	ret = iommu_init_device(dev, ops);
	if (ret)
		return ret;

	group = dev->iommu_group;
	gdev = iommu_group_alloc_device(group, dev);
	mutex_lock(&group->mutex);
	if (IS_ERR(gdev)) {
		ret = PTR_ERR(gdev);
		goto err_put_group;
	}

	/*
	 * The gdev must be in the list before calling
	 * iommu_setup_default_domain()
	 */
	list_add_tail(&gdev->list, &group->devices);
	WARN_ON(group->default_domain && !group->domain);
	if (group->default_domain)
		iommu_create_device_direct_mappings(group->default_domain, dev);
	if (group->domain) {
		ret = __iommu_device_set_domain(group, dev, group->domain, 0);
		if (ret)
			goto err_remove_gdev;
	} else if (!group->default_domain && !group_list) {
		ret = iommu_setup_default_domain(group, 0);
		if (ret)
			goto err_remove_gdev;
	} else if (!group->default_domain) {
		/*
		 * With a group_list argument we defer the default_domain setup
		 * to the caller by providing a de-duplicated list of groups
		 * that need further setup.
		 */
		if (list_empty(&group->entry))
			list_add_tail(&group->entry, group_list);
	}
	mutex_unlock(&group->mutex);

	if (dev_is_pci(dev))
		iommu_dma_set_pci_32bit_workaround(dev);

	return 0;

err_remove_gdev:
	list_del(&gdev->list);
	__iommu_group_free_device(group, gdev);
err_put_group:
	iommu_deinit_device(dev);
	mutex_unlock(&group->mutex);
	iommu_group_put(group);

	return ret;
}

int iommu_probe_device(struct device *dev)
{
	const struct iommu_ops *ops;
	int ret;

	mutex_lock(&iommu_probe_device_lock);
	ret = __iommu_probe_device(dev, NULL);
	mutex_unlock(&iommu_probe_device_lock);
	if (ret)
		return ret;

	ops = dev_iommu_ops(dev);
	if (ops->probe_finalize)
		ops->probe_finalize(dev);

	return 0;
}

static void __iommu_group_free_device(struct iommu_group *group,
				      struct group_device *grp_dev)
{
	struct device *dev = grp_dev->dev;

	sysfs_remove_link(group->devices_kobj, grp_dev->name);
	sysfs_remove_link(&dev->kobj, "iommu_group");

	trace_remove_device_from_group(group->id, dev);

	/*
	 * If the group has become empty then ownership must have been
	 * released, and the current domain must be set back to NULL or
	 * the default domain.
	 */
	if (list_empty(&group->devices))
		WARN_ON(group->owner_cnt ||
			group->domain != group->default_domain);

	kfree(grp_dev->name);
	kfree(grp_dev);
}

/* Remove the iommu_group from the struct device. */
static void __iommu_group_remove_device(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;
	struct group_device *device;

	mutex_lock(&group->mutex);
	for_each_group_device(group, device) {
		if (device->dev != dev)
			continue;

		list_del(&device->list);
		__iommu_group_free_device(group, device);
		if (dev_has_iommu(dev))
			iommu_deinit_device(dev);
		else
			dev->iommu_group = NULL;
		break;
	}
	mutex_unlock(&group->mutex);

	/*
	 * Pairs with the get in iommu_init_device() or
	 * iommu_group_add_device()
	 */
	iommu_group_put(group);
}

static void iommu_release_device(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;

	if (group)
		__iommu_group_remove_device(dev);

	/* Free any fwspec if no iommu_driver was ever attached */
	if (dev->iommu)
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
	return sysfs_emit(buf, "%s\n", group->name);
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
	for_each_group_device(group, device) {
		struct list_head dev_resv_regions;

		/*
		 * Non-API groups still expose reserved_regions in sysfs,
		 * so filter out calls that get here that way.
		 */
		if (!dev_has_iommu(device->dev))
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
	int offset = 0;

	INIT_LIST_HEAD(&group_resv_regions);
	iommu_get_group_resv_regions(group, &group_resv_regions);

	list_for_each_entry_safe(region, next, &group_resv_regions, list) {
		offset += sysfs_emit_at(buf, offset, "0x%016llx 0x%016llx %s\n",
					(long long)region->start,
					(long long)(region->start +
						    region->length - 1),
					iommu_group_resv_type_string[region->type]);
		kfree(region);
	}

	return offset;
}

static ssize_t iommu_group_show_type(struct iommu_group *group,
				     char *buf)
{
	char *type = "unknown";

	mutex_lock(&group->mutex);
	if (group->default_domain) {
		switch (group->default_domain->type) {
		case IOMMU_DOMAIN_BLOCKED:
			type = "blocked";
			break;
		case IOMMU_DOMAIN_IDENTITY:
			type = "identity";
			break;
		case IOMMU_DOMAIN_UNMANAGED:
			type = "unmanaged";
			break;
		case IOMMU_DOMAIN_DMA:
			type = "DMA";
			break;
		case IOMMU_DOMAIN_DMA_FQ:
			type = "DMA-FQ";
			break;
		}
	}
	mutex_unlock(&group->mutex);

	return sysfs_emit(buf, "%s\n", type);
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

	/* Domains are free'd by iommu_deinit_device() */
	WARN_ON(group->default_domain);
	WARN_ON(group->blocking_domain);

	kfree(group->name);
	kfree(group);
}

static const struct kobj_type iommu_group_ktype = {
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

static int iommu_create_device_direct_mappings(struct iommu_domain *domain,
					       struct device *dev)
{
	struct iommu_resv_region *entry;
	struct list_head mappings;
	unsigned long pg_size;
	int ret = 0;

	pg_size = domain->pgsize_bitmap ? 1UL << __ffs(domain->pgsize_bitmap) : 0;
	INIT_LIST_HEAD(&mappings);

	if (WARN_ON_ONCE(iommu_is_dma_domain(domain) && !pg_size))
		return -EINVAL;

	iommu_get_resv_regions(dev, &mappings);

	/* We need to consider overlapping regions for different devices */
	list_for_each_entry(entry, &mappings, list) {
		dma_addr_t start, end, addr;
		size_t map_size = 0;

		if (entry->type == IOMMU_RESV_DIRECT)
			dev->iommu->require_direct = 1;

		if ((entry->type != IOMMU_RESV_DIRECT &&
		     entry->type != IOMMU_RESV_DIRECT_RELAXABLE) ||
		    !iommu_is_dma_domain(domain))
			continue;

		start = ALIGN(entry->start, pg_size);
		end   = ALIGN(entry->start + entry->length, pg_size);

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
						entry->prot, GFP_KERNEL);
				if (ret)
					goto out;
				map_size = 0;
			}
		}

	}

	if (!list_empty(&mappings) && iommu_is_dma_domain(domain))
		iommu_flush_iotlb_all(domain);

out:
	iommu_put_resv_regions(dev, &mappings);

	return ret;
}

/* This is undone by __iommu_group_free_device() */
static struct group_device *iommu_group_alloc_device(struct iommu_group *group,
						     struct device *dev)
{
	int ret, i = 0;
	struct group_device *device;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return ERR_PTR(-ENOMEM);

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

	trace_add_device_to_group(group->id, dev);

	dev_info(dev, "Adding to iommu group %d\n", group->id);

	return device;

err_free_name:
	kfree(device->name);
err_remove_link:
	sysfs_remove_link(&dev->kobj, "iommu_group");
err_free_device:
	kfree(device);
	dev_err(dev, "Failed to add to iommu group %d: %d\n", group->id, ret);
	return ERR_PTR(ret);
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
	struct group_device *gdev;

	gdev = iommu_group_alloc_device(group, dev);
	if (IS_ERR(gdev))
		return PTR_ERR(gdev);

	iommu_group_ref_get(group);
	dev->iommu_group = group;

	mutex_lock(&group->mutex);
	list_add_tail(&gdev->list, &group->devices);
	mutex_unlock(&group->mutex);
	return 0;
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

	if (!group)
		return;

	dev_info(dev, "Removing from iommu group %d\n", group->id);

	__iommu_group_remove_device(dev);
}
EXPORT_SYMBOL_GPL(iommu_group_remove_device);

#if IS_ENABLED(CONFIG_LOCKDEP) && IS_ENABLED(CONFIG_IOMMU_API)
/**
 * iommu_group_mutex_assert - Check device group mutex lock
 * @dev: the device that has group param set
 *
 * This function is called by an iommu driver to check whether it holds
 * group mutex lock for the given device or not.
 *
 * Note that this function must be called after device group param is set.
 */
void iommu_group_mutex_assert(struct device *dev)
{
	struct iommu_group *group = dev->iommu_group;

	lockdep_assert_held(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_group_mutex_assert);
#endif

static struct device *iommu_group_first_dev(struct iommu_group *group)
{
	lockdep_assert_held(&group->mutex);
	return list_first_entry(&group->devices, struct group_device, list)->dev;
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
	struct group_device *device;
	int ret = 0;

	mutex_lock(&group->mutex);
	for_each_group_device(group, device) {
		ret = fn(device->dev, data);
		if (ret)
			break;
	}
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
 * Generic device_group call-back function. It just allocates one
 * iommu-group per iommu driver instance shared by every device
 * probed by that iommu driver.
 */
struct iommu_group *generic_single_device_group(struct device *dev)
{
	struct iommu_device *iommu = dev->iommu->iommu_dev;

	if (!iommu->singleton_group) {
		struct iommu_group *group;

		group = iommu_group_alloc();
		if (IS_ERR(group))
			return group;
		iommu->singleton_group = group;
	}
	return iommu_group_ref_get(iommu->singleton_group);
}
EXPORT_SYMBOL_GPL(generic_single_device_group);

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

static struct iommu_domain *
__iommu_group_alloc_default_domain(struct iommu_group *group, int req_type)
{
	if (group->default_domain && group->default_domain->type == req_type)
		return group->default_domain;
	return __iommu_group_domain_alloc(group, req_type);
}

/*
 * req_type of 0 means "auto" which means to select a domain based on
 * iommu_def_domain_type or what the driver actually supports.
 */
static struct iommu_domain *
iommu_group_alloc_default_domain(struct iommu_group *group, int req_type)
{
	const struct iommu_ops *ops = dev_iommu_ops(iommu_group_first_dev(group));
	struct iommu_domain *dom;

	lockdep_assert_held(&group->mutex);

	/*
	 * Allow legacy drivers to specify the domain that will be the default
	 * domain. This should always be either an IDENTITY/BLOCKED/PLATFORM
	 * domain. Do not use in new drivers.
	 */
	if (ops->default_domain) {
		if (req_type != ops->default_domain->type)
			return ERR_PTR(-EINVAL);
		return ops->default_domain;
	}

	if (req_type)
		return __iommu_group_alloc_default_domain(group, req_type);

	/* The driver gave no guidance on what type to use, try the default */
	dom = __iommu_group_alloc_default_domain(group, iommu_def_domain_type);
	if (!IS_ERR(dom))
		return dom;

	/* Otherwise IDENTITY and DMA_FQ defaults will try DMA */
	if (iommu_def_domain_type == IOMMU_DOMAIN_DMA)
		return ERR_PTR(-EINVAL);
	dom = __iommu_group_alloc_default_domain(group, IOMMU_DOMAIN_DMA);
	if (IS_ERR(dom))
		return dom;

	pr_warn("Failed to allocate default IOMMU domain of type %u for group %s - Falling back to IOMMU_DOMAIN_DMA",
		iommu_def_domain_type, group->name);
	return dom;
}

struct iommu_domain *iommu_group_default_domain(struct iommu_group *group)
{
	return group->default_domain;
}

static int probe_iommu_group(struct device *dev, void *data)
{
	struct list_head *group_list = data;
	int ret;

	mutex_lock(&iommu_probe_device_lock);
	ret = __iommu_probe_device(dev, group_list);
	mutex_unlock(&iommu_probe_device_lock);
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

/*
 * Combine the driver's chosen def_domain_type across all the devices in a
 * group. Drivers must give a consistent result.
 */
static int iommu_get_def_domain_type(struct iommu_group *group,
				     struct device *dev, int cur_type)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	int type;

	if (ops->default_domain) {
		/*
		 * Drivers that declare a global static default_domain will
		 * always choose that.
		 */
		type = ops->default_domain->type;
	} else {
		if (ops->def_domain_type)
			type = ops->def_domain_type(dev);
		else
			return cur_type;
	}
	if (!type || cur_type == type)
		return cur_type;
	if (!cur_type)
		return type;

	dev_err_ratelimited(
		dev,
		"IOMMU driver error, requesting conflicting def_domain_type, %s and %s, for devices in group %u.\n",
		iommu_domain_type_str(cur_type), iommu_domain_type_str(type),
		group->id);

	/*
	 * Try to recover, drivers are allowed to force IDENITY or DMA, IDENTITY
	 * takes precedence.
	 */
	if (type == IOMMU_DOMAIN_IDENTITY)
		return type;
	return cur_type;
}

/*
 * A target_type of 0 will select the best domain type. 0 can be returned in
 * this case meaning the global default should be used.
 */
static int iommu_get_default_domain_type(struct iommu_group *group,
					 int target_type)
{
	struct device *untrusted = NULL;
	struct group_device *gdev;
	int driver_type = 0;

	lockdep_assert_held(&group->mutex);

	/*
	 * ARM32 drivers supporting CONFIG_ARM_DMA_USE_IOMMU can declare an
	 * identity_domain and it will automatically become their default
	 * domain. Later on ARM_DMA_USE_IOMMU will install its UNMANAGED domain.
	 * Override the selection to IDENTITY.
	 */
	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)) {
		static_assert(!(IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU) &&
				IS_ENABLED(CONFIG_IOMMU_DMA)));
		driver_type = IOMMU_DOMAIN_IDENTITY;
	}

	for_each_group_device(group, gdev) {
		driver_type = iommu_get_def_domain_type(group, gdev->dev,
							driver_type);

		if (dev_is_pci(gdev->dev) && to_pci_dev(gdev->dev)->untrusted) {
			/*
			 * No ARM32 using systems will set untrusted, it cannot
			 * work.
			 */
			if (WARN_ON(IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)))
				return -1;
			untrusted = gdev->dev;
		}
	}

	/*
	 * If the common dma ops are not selected in kconfig then we cannot use
	 * IOMMU_DOMAIN_DMA at all. Force IDENTITY if nothing else has been
	 * selected.
	 */
	if (!IS_ENABLED(CONFIG_IOMMU_DMA)) {
		if (WARN_ON(driver_type == IOMMU_DOMAIN_DMA))
			return -1;
		if (!driver_type)
			driver_type = IOMMU_DOMAIN_IDENTITY;
	}

	if (untrusted) {
		if (driver_type && driver_type != IOMMU_DOMAIN_DMA) {
			dev_err_ratelimited(
				untrusted,
				"Device is not trusted, but driver is overriding group %u to %s, refusing to probe.\n",
				group->id, iommu_domain_type_str(driver_type));
			return -1;
		}
		driver_type = IOMMU_DOMAIN_DMA;
	}

	if (target_type) {
		if (driver_type && target_type != driver_type)
			return -1;
		return target_type;
	}
	return driver_type;
}

static void iommu_group_do_probe_finalize(struct device *dev)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (ops->probe_finalize)
		ops->probe_finalize(dev);
}

int bus_iommu_probe(const struct bus_type *bus)
{
	struct iommu_group *group, *next;
	LIST_HEAD(group_list);
	int ret;

	ret = bus_for_each_dev(bus, NULL, &group_list, probe_iommu_group);
	if (ret)
		return ret;

	list_for_each_entry_safe(group, next, &group_list, entry) {
		struct group_device *gdev;

		mutex_lock(&group->mutex);

		/* Remove item from the list */
		list_del_init(&group->entry);

		/*
		 * We go to the trouble of deferred default domain creation so
		 * that the cross-group default domain type and the setup of the
		 * IOMMU_RESV_DIRECT will work correctly in non-hotpug scenarios.
		 */
		ret = iommu_setup_default_domain(group, 0);
		if (ret) {
			mutex_unlock(&group->mutex);
			return ret;
		}
		mutex_unlock(&group->mutex);

		/*
		 * FIXME: Mis-locked because the ops->probe_finalize() call-back
		 * of some IOMMU drivers calls arm_iommu_attach_device() which
		 * in-turn might call back into IOMMU core code, where it tries
		 * to take group->mutex, resulting in a deadlock.
		 */
		for_each_group_device(group, gdev)
			iommu_group_do_probe_finalize(gdev->dev);
	}

	return 0;
}

/**
 * iommu_present() - make platform-specific assumptions about an IOMMU
 * @bus: bus to check
 *
 * Do not use this function. You want device_iommu_mapped() instead.
 *
 * Return: true if some IOMMU is present and aware of devices on the given bus;
 * in general it may not be the only IOMMU, and it may not have anything to do
 * with whatever device you are ultimately interested in.
 */
bool iommu_present(const struct bus_type *bus)
{
	bool ret = false;

	for (int i = 0; i < ARRAY_SIZE(iommu_buses); i++) {
		if (iommu_buses[i] == bus) {
			spin_lock(&iommu_device_lock);
			ret = !list_empty(&iommu_device_list);
			spin_unlock(&iommu_device_lock);
		}
	}
	return ret;
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

	if (!dev_has_iommu(dev))
		return false;

	ops = dev_iommu_ops(dev);
	if (!ops->capable)
		return false;

	return ops->capable(dev, cap);
}
EXPORT_SYMBOL_GPL(device_iommu_capable);

/**
 * iommu_group_has_isolated_msi() - Compute msi_device_has_isolated_msi()
 *       for a group
 * @group: Group to query
 *
 * IOMMU groups should not have differing values of
 * msi_device_has_isolated_msi() for devices in a group. However nothing
 * directly prevents this, so ensure mistakes don't result in isolation failures
 * by checking that all the devices are the same.
 */
bool iommu_group_has_isolated_msi(struct iommu_group *group)
{
	struct group_device *group_dev;
	bool ret = true;

	mutex_lock(&group->mutex);
	for_each_group_device(group, group_dev)
		ret &= msi_device_has_isolated_msi(group_dev->dev);
	mutex_unlock(&group->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_group_has_isolated_msi);

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

static struct iommu_domain *__iommu_domain_alloc(const struct iommu_ops *ops,
						 struct device *dev,
						 unsigned int type)
{
	struct iommu_domain *domain;
	unsigned int alloc_type = type & IOMMU_DOMAIN_ALLOC_FLAGS;

	if (alloc_type == IOMMU_DOMAIN_IDENTITY && ops->identity_domain)
		return ops->identity_domain;
	else if (alloc_type == IOMMU_DOMAIN_BLOCKED && ops->blocked_domain)
		return ops->blocked_domain;
	else if (type & __IOMMU_DOMAIN_PAGING && ops->domain_alloc_paging)
		domain = ops->domain_alloc_paging(dev);
	else if (ops->domain_alloc)
		domain = ops->domain_alloc(alloc_type);
	else
		return ERR_PTR(-EOPNOTSUPP);

	/*
	 * Many domain_alloc ops now return ERR_PTR, make things easier for the
	 * driver by accepting ERR_PTR from all domain_alloc ops instead of
	 * having two rules.
	 */
	if (IS_ERR(domain))
		return domain;
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->type = type;
	domain->owner = ops;
	/*
	 * If not already set, assume all sizes by default; the driver
	 * may override this later
	 */
	if (!domain->pgsize_bitmap)
		domain->pgsize_bitmap = ops->pgsize_bitmap;

	if (!domain->ops)
		domain->ops = ops->default_domain_ops;

	if (iommu_is_dma_domain(domain)) {
		int rc;

		rc = iommu_get_dma_cookie(domain);
		if (rc) {
			iommu_domain_free(domain);
			return ERR_PTR(rc);
		}
	}
	return domain;
}

static struct iommu_domain *
__iommu_group_domain_alloc(struct iommu_group *group, unsigned int type)
{
	struct device *dev = iommu_group_first_dev(group);

	return __iommu_domain_alloc(dev_iommu_ops(dev), dev, type);
}

static int __iommu_domain_alloc_dev(struct device *dev, void *data)
{
	const struct iommu_ops **ops = data;

	if (!dev_has_iommu(dev))
		return 0;

	if (WARN_ONCE(*ops && *ops != dev_iommu_ops(dev),
		      "Multiple IOMMU drivers present for bus %s, which the public IOMMU API can't fully support yet. You will still need to disable one or more for this to work, sorry!\n",
		      dev_bus_name(dev)))
		return -EBUSY;

	*ops = dev_iommu_ops(dev);
	return 0;
}

struct iommu_domain *iommu_domain_alloc(const struct bus_type *bus)
{
	const struct iommu_ops *ops = NULL;
	int err = bus_for_each_dev(bus, NULL, &ops, __iommu_domain_alloc_dev);
	struct iommu_domain *domain;

	if (err || !ops)
		return NULL;

	domain = __iommu_domain_alloc(ops, NULL, IOMMU_DOMAIN_UNMANAGED);
	if (IS_ERR(domain))
		return NULL;
	return domain;
}
EXPORT_SYMBOL_GPL(iommu_domain_alloc);

void iommu_domain_free(struct iommu_domain *domain)
{
	if (domain->type == IOMMU_DOMAIN_SVA)
		mmdrop(domain->mm);
	iommu_put_dma_cookie(domain);
	if (domain->ops->free)
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

	if (group->owner)
		new_domain = group->blocking_domain;
	else
		new_domain = group->default_domain;

	__iommu_group_set_domain_nofail(group, new_domain);
}

static int __iommu_attach_device(struct iommu_domain *domain,
				 struct device *dev)
{
	int ret;

	if (unlikely(domain->ops->attach_dev == NULL))
		return -ENODEV;

	ret = domain->ops->attach_dev(domain, dev);
	if (ret)
		return ret;
	dev->iommu->attach_deferred = 0;
	trace_attach_device_to_domain(dev);
	return 0;
}

/**
 * iommu_attach_device - Attach an IOMMU domain to a device
 * @domain: IOMMU domain to attach
 * @dev: Device that will be attached
 *
 * Returns 0 on success and error code on failure
 *
 * Note that EINVAL can be treated as a soft failure, indicating
 * that certain configuration of the domain is incompatible with
 * the device. In this case attaching a different domain to the
 * device may succeed.
 */
int iommu_attach_device(struct iommu_domain *domain, struct device *dev)
{
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;
	int ret;

	if (!group)
		return -ENODEV;

	/*
	 * Lock the group to make sure the device-count doesn't
	 * change while we are attaching
	 */
	mutex_lock(&group->mutex);
	ret = -EINVAL;
	if (list_count_nodes(&group->devices) != 1)
		goto out_unlock;

	ret = __iommu_attach_group(domain, group);

out_unlock:
	mutex_unlock(&group->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_attach_device);

int iommu_deferred_attach(struct device *dev, struct iommu_domain *domain)
{
	if (dev->iommu && dev->iommu->attach_deferred)
		return __iommu_attach_device(domain, dev);

	return 0;
}

void iommu_detach_device(struct iommu_domain *domain, struct device *dev)
{
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;

	if (!group)
		return;

	mutex_lock(&group->mutex);
	if (WARN_ON(domain != group->domain) ||
	    WARN_ON(list_count_nodes(&group->devices) != 1))
		goto out_unlock;
	__iommu_group_set_core_domain(group);

out_unlock:
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_detach_device);

struct iommu_domain *iommu_get_domain_for_dev(struct device *dev)
{
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;

	if (!group)
		return NULL;

	return group->domain;
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

static int __iommu_attach_group(struct iommu_domain *domain,
				struct iommu_group *group)
{
	struct device *dev;

	if (group->domain && group->domain != group->default_domain &&
	    group->domain != group->blocking_domain)
		return -EBUSY;

	dev = iommu_group_first_dev(group);
	if (!dev_has_iommu(dev) || dev_iommu_ops(dev) != domain->owner)
		return -EINVAL;

	return __iommu_group_set_domain(group, domain);
}

/**
 * iommu_attach_group - Attach an IOMMU domain to an IOMMU group
 * @domain: IOMMU domain to attach
 * @group: IOMMU group that will be attached
 *
 * Returns 0 on success and error code on failure
 *
 * Note that EINVAL can be treated as a soft failure, indicating
 * that certain configuration of the domain is incompatible with
 * the group. In this case attaching a different domain to the
 * group may succeed.
 */
int iommu_attach_group(struct iommu_domain *domain, struct iommu_group *group)
{
	int ret;

	mutex_lock(&group->mutex);
	ret = __iommu_attach_group(domain, group);
	mutex_unlock(&group->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_attach_group);

/**
 * iommu_group_replace_domain - replace the domain that a group is attached to
 * @new_domain: new IOMMU domain to replace with
 * @group: IOMMU group that will be attached to the new domain
 *
 * This API allows the group to switch domains without being forced to go to
 * the blocking domain in-between.
 *
 * If the currently attached domain is a core domain (e.g. a default_domain),
 * it will act just like the iommu_attach_group().
 */
int iommu_group_replace_domain(struct iommu_group *group,
			       struct iommu_domain *new_domain)
{
	int ret;

	if (!new_domain)
		return -EINVAL;

	mutex_lock(&group->mutex);
	ret = __iommu_group_set_domain(group, new_domain);
	mutex_unlock(&group->mutex);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(iommu_group_replace_domain, IOMMUFD_INTERNAL);

static int __iommu_device_set_domain(struct iommu_group *group,
				     struct device *dev,
				     struct iommu_domain *new_domain,
				     unsigned int flags)
{
	int ret;

	/*
	 * If the device requires IOMMU_RESV_DIRECT then we cannot allow
	 * the blocking domain to be attached as it does not contain the
	 * required 1:1 mapping. This test effectively excludes the device
	 * being used with iommu_group_claim_dma_owner() which will block
	 * vfio and iommufd as well.
	 */
	if (dev->iommu->require_direct &&
	    (new_domain->type == IOMMU_DOMAIN_BLOCKED ||
	     new_domain == group->blocking_domain)) {
		dev_warn(dev,
			 "Firmware has requested this device have a 1:1 IOMMU mapping, rejecting configuring the device without a 1:1 mapping. Contact your platform vendor.\n");
		return -EINVAL;
	}

	if (dev->iommu->attach_deferred) {
		if (new_domain == group->default_domain)
			return 0;
		dev->iommu->attach_deferred = 0;
	}

	ret = __iommu_attach_device(new_domain, dev);
	if (ret) {
		/*
		 * If we have a blocking domain then try to attach that in hopes
		 * of avoiding a UAF. Modern drivers should implement blocking
		 * domains as global statics that cannot fail.
		 */
		if ((flags & IOMMU_SET_DOMAIN_MUST_SUCCEED) &&
		    group->blocking_domain &&
		    group->blocking_domain != new_domain)
			__iommu_attach_device(group->blocking_domain, dev);
		return ret;
	}
	return 0;
}

/*
 * If 0 is returned the group's domain is new_domain. If an error is returned
 * then the group's domain will be set back to the existing domain unless
 * IOMMU_SET_DOMAIN_MUST_SUCCEED, otherwise an error is returned and the group's
 * domains is left inconsistent. This is a driver bug to fail attach with a
 * previously good domain. We try to avoid a kernel UAF because of this.
 *
 * IOMMU groups are really the natural working unit of the IOMMU, but the IOMMU
 * API works on domains and devices.  Bridge that gap by iterating over the
 * devices in a group.  Ideally we'd have a single device which represents the
 * requestor ID of the group, but we also allow IOMMU drivers to create policy
 * defined minimum sets, where the physical hardware may be able to distiguish
 * members, but we wish to group them at a higher level (ex. untrusted
 * multi-function PCI devices).  Thus we attach each device.
 */
static int __iommu_group_set_domain_internal(struct iommu_group *group,
					     struct iommu_domain *new_domain,
					     unsigned int flags)
{
	struct group_device *last_gdev;
	struct group_device *gdev;
	int result;
	int ret;

	lockdep_assert_held(&group->mutex);

	if (group->domain == new_domain)
		return 0;

	if (WARN_ON(!new_domain))
		return -EINVAL;

	/*
	 * Changing the domain is done by calling attach_dev() on the new
	 * domain. This switch does not have to be atomic and DMA can be
	 * discarded during the transition. DMA must only be able to access
	 * either new_domain or group->domain, never something else.
	 */
	result = 0;
	for_each_group_device(group, gdev) {
		ret = __iommu_device_set_domain(group, gdev->dev, new_domain,
						flags);
		if (ret) {
			result = ret;
			/*
			 * Keep trying the other devices in the group. If a
			 * driver fails attach to an otherwise good domain, and
			 * does not support blocking domains, it should at least
			 * drop its reference on the current domain so we don't
			 * UAF.
			 */
			if (flags & IOMMU_SET_DOMAIN_MUST_SUCCEED)
				continue;
			goto err_revert;
		}
	}
	group->domain = new_domain;
	return result;

err_revert:
	/*
	 * This is called in error unwind paths. A well behaved driver should
	 * always allow us to attach to a domain that was already attached.
	 */
	last_gdev = gdev;
	for_each_group_device(group, gdev) {
		/*
		 * A NULL domain can happen only for first probe, in which case
		 * we leave group->domain as NULL and let release clean
		 * everything up.
		 */
		if (group->domain)
			WARN_ON(__iommu_device_set_domain(
				group, gdev->dev, group->domain,
				IOMMU_SET_DOMAIN_MUST_SUCCEED));
		if (gdev == last_gdev)
			break;
	}
	return ret;
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

static int __iommu_map(struct iommu_domain *domain, unsigned long iova,
		       phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;
	size_t orig_size = size;
	phys_addr_t orig_paddr = paddr;
	int ret = 0;

	if (unlikely(!(domain->type & __IOMMU_DOMAIN_PAGING)))
		return -EINVAL;

	if (WARN_ON(!ops->map_pages || domain->pgsize_bitmap == 0UL))
		return -ENODEV;

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
		size_t pgsize, count, mapped = 0;

		pgsize = iommu_pgsize(domain, iova, paddr, size, &count);

		pr_debug("mapping: iova 0x%lx pa %pa pgsize 0x%zx count %zu\n",
			 iova, &paddr, pgsize, count);
		ret = ops->map_pages(domain, iova, paddr, pgsize, count, prot,
				     gfp, &mapped);
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

int iommu_map(struct iommu_domain *domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
	int ret;

	might_sleep_if(gfpflags_allow_blocking(gfp));

	/* Discourage passing strange GFP flags */
	if (WARN_ON_ONCE(gfp & (__GFP_COMP | __GFP_DMA | __GFP_DMA32 |
				__GFP_HIGHMEM)))
		return -EINVAL;

	ret = __iommu_map(domain, iova, paddr, size, prot, gfp);
	if (ret == 0 && ops->iotlb_sync_map) {
		ret = ops->iotlb_sync_map(domain, iova, size);
		if (ret)
			goto out_err;
	}

	return ret;

out_err:
	/* undo mappings already done */
	iommu_unmap(domain, iova, size);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_map);

static size_t __iommu_unmap(struct iommu_domain *domain,
			    unsigned long iova, size_t size,
			    struct iommu_iotlb_gather *iotlb_gather)
{
	const struct iommu_domain_ops *ops = domain->ops;
	size_t unmapped_page, unmapped = 0;
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;

	if (unlikely(!(domain->type & __IOMMU_DOMAIN_PAGING)))
		return 0;

	if (WARN_ON(!ops->unmap_pages || domain->pgsize_bitmap == 0UL))
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
		size_t pgsize, count;

		pgsize = iommu_pgsize(domain, iova, iova, size - unmapped, &count);
		unmapped_page = ops->unmap_pages(domain, iova, pgsize, count, iotlb_gather);
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

ssize_t iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
		     struct scatterlist *sg, unsigned int nents, int prot,
		     gfp_t gfp)
{
	const struct iommu_domain_ops *ops = domain->ops;
	size_t len = 0, mapped = 0;
	phys_addr_t start;
	unsigned int i = 0;
	int ret;

	might_sleep_if(gfpflags_allow_blocking(gfp));

	/* Discourage passing strange GFP flags */
	if (WARN_ON_ONCE(gfp & (__GFP_COMP | __GFP_DMA | __GFP_DMA32 |
				__GFP_HIGHMEM)))
		return -EINVAL;

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

		if (sg_dma_is_bus_address(sg))
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

	if (ops->iotlb_sync_map) {
		ret = ops->iotlb_sync_map(domain, iova, mapped);
		if (ret)
			goto out_err;
	}
	return mapped;

out_err:
	/* undo mappings already done */
	iommu_unmap(domain, iova, mapped);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_map_sg);

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

/**
 * iommu_get_resv_regions - get reserved regions
 * @dev: device for which to get reserved regions
 * @list: reserved region list for device
 *
 * This returns a list of reserved IOVA regions specific to this device.
 * A domain user should not map IOVA in these ranges.
 */
void iommu_get_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (ops->get_resv_regions)
		ops->get_resv_regions(dev, list);
}
EXPORT_SYMBOL_GPL(iommu_get_resv_regions);

/**
 * iommu_put_resv_regions - release reserved regions
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

const struct iommu_ops *iommu_ops_from_fwnode(const struct fwnode_handle *fwnode)
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

int iommu_fwspec_add_ids(struct device *dev, const u32 *ids, int num_ids)
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
	if (dev_has_iommu(dev)) {
		const struct iommu_ops *ops = dev_iommu_ops(dev);

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
	if (dev_has_iommu(dev)) {
		const struct iommu_ops *ops = dev_iommu_ops(dev);

		if (ops->dev_disable_feat)
			return ops->dev_disable_feat(dev, feat);
	}

	return -EBUSY;
}
EXPORT_SYMBOL_GPL(iommu_dev_disable_feature);

/**
 * iommu_setup_default_domain - Set the default_domain for the group
 * @group: Group to change
 * @target_type: Domain type to set as the default_domain
 *
 * Allocate a default domain and set it as the current domain on the group. If
 * the group already has a default domain it will be changed to the target_type.
 * When target_type is 0 the default domain is selected based on driver and
 * system preferences.
 */
static int iommu_setup_default_domain(struct iommu_group *group,
				      int target_type)
{
	struct iommu_domain *old_dom = group->default_domain;
	struct group_device *gdev;
	struct iommu_domain *dom;
	bool direct_failed;
	int req_type;
	int ret;

	lockdep_assert_held(&group->mutex);

	req_type = iommu_get_default_domain_type(group, target_type);
	if (req_type < 0)
		return -EINVAL;

	dom = iommu_group_alloc_default_domain(group, req_type);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	if (group->default_domain == dom)
		return 0;

	/*
	 * IOMMU_RESV_DIRECT and IOMMU_RESV_DIRECT_RELAXABLE regions must be
	 * mapped before their device is attached, in order to guarantee
	 * continuity with any FW activity
	 */
	direct_failed = false;
	for_each_group_device(group, gdev) {
		if (iommu_create_device_direct_mappings(dom, gdev->dev)) {
			direct_failed = true;
			dev_warn_once(
				gdev->dev->iommu->iommu_dev->dev,
				"IOMMU driver was not able to establish FW requested direct mapping.");
		}
	}

	/* We must set default_domain early for __iommu_device_set_domain */
	group->default_domain = dom;
	if (!group->domain) {
		/*
		 * Drivers are not allowed to fail the first domain attach.
		 * The only way to recover from this is to fail attaching the
		 * iommu driver and call ops->release_device. Put the domain
		 * in group->default_domain so it is freed after.
		 */
		ret = __iommu_group_set_domain_internal(
			group, dom, IOMMU_SET_DOMAIN_MUST_SUCCEED);
		if (WARN_ON(ret))
			goto out_free_old;
	} else {
		ret = __iommu_group_set_domain(group, dom);
		if (ret)
			goto err_restore_def_domain;
	}

	/*
	 * Drivers are supposed to allow mappings to be installed in a domain
	 * before device attachment, but some don't. Hack around this defect by
	 * trying again after attaching. If this happens it means the device
	 * will not continuously have the IOMMU_RESV_DIRECT map.
	 */
	if (direct_failed) {
		for_each_group_device(group, gdev) {
			ret = iommu_create_device_direct_mappings(dom, gdev->dev);
			if (ret)
				goto err_restore_domain;
		}
	}

out_free_old:
	if (old_dom)
		iommu_domain_free(old_dom);
	return ret;

err_restore_domain:
	if (old_dom)
		__iommu_group_set_domain_internal(
			group, old_dom, IOMMU_SET_DOMAIN_MUST_SUCCEED);
err_restore_def_domain:
	if (old_dom) {
		iommu_domain_free(dom);
		group->default_domain = old_dom;
	}
	return ret;
}

/*
 * Changing the default domain through sysfs requires the users to unbind the
 * drivers from the devices in the iommu group, except for a DMA -> DMA-FQ
 * transition. Return failure if this isn't met.
 *
 * We need to consider the race between this and the device release path.
 * group->mutex is used here to guarantee that the device release path
 * will not be entered at the same time.
 */
static ssize_t iommu_group_store_type(struct iommu_group *group,
				      const char *buf, size_t count)
{
	struct group_device *gdev;
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

	mutex_lock(&group->mutex);
	/* We can bring up a flush queue without tearing down the domain. */
	if (req_type == IOMMU_DOMAIN_DMA_FQ &&
	    group->default_domain->type == IOMMU_DOMAIN_DMA) {
		ret = iommu_dma_init_fq(group->default_domain);
		if (ret)
			goto out_unlock;

		group->default_domain->type = IOMMU_DOMAIN_DMA_FQ;
		ret = count;
		goto out_unlock;
	}

	/* Otherwise, ensure that device exists and no driver is bound. */
	if (list_empty(&group->devices) || group->owner_cnt) {
		ret = -EPERM;
		goto out_unlock;
	}

	ret = iommu_setup_default_domain(group, req_type);
	if (ret)
		goto out_unlock;

	/*
	 * Release the mutex here because ops->probe_finalize() call-back of
	 * some vendor IOMMU drivers calls arm_iommu_attach_device() which
	 * in-turn might call back into IOMMU core code, where it tries to take
	 * group->mutex, resulting in a deadlock.
	 */
	mutex_unlock(&group->mutex);

	/* Make sure dma_ops is appropriatley set */
	for_each_group_device(group, gdev)
		iommu_group_do_probe_finalize(gdev->dev);
	return count;

out_unlock:
	mutex_unlock(&group->mutex);
	return ret ?: count;
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
	/* Caller is the driver core during the pre-probe path */
	struct iommu_group *group = dev->iommu_group;
	int ret = 0;

	if (!group)
		return 0;

	mutex_lock(&group->mutex);
	if (group->owner_cnt) {
		if (group->domain != group->default_domain || group->owner ||
		    !xa_empty(&group->pasid_array)) {
			ret = -EBUSY;
			goto unlock_out;
		}
	}

	group->owner_cnt++;

unlock_out:
	mutex_unlock(&group->mutex);
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
	/* Caller is the driver core during the post-probe path */
	struct iommu_group *group = dev->iommu_group;

	if (!group)
		return;

	mutex_lock(&group->mutex);
	if (!WARN_ON(!group->owner_cnt || !xa_empty(&group->pasid_array)))
		group->owner_cnt--;

	mutex_unlock(&group->mutex);
}

static int __iommu_group_alloc_blocking_domain(struct iommu_group *group)
{
	struct iommu_domain *domain;

	if (group->blocking_domain)
		return 0;

	domain = __iommu_group_domain_alloc(group, IOMMU_DOMAIN_BLOCKED);
	if (IS_ERR(domain)) {
		/*
		 * For drivers that do not yet understand IOMMU_DOMAIN_BLOCKED
		 * create an empty domain instead.
		 */
		domain = __iommu_group_domain_alloc(group,
						    IOMMU_DOMAIN_UNMANAGED);
		if (IS_ERR(domain))
			return PTR_ERR(domain);
	}
	group->blocking_domain = domain;
	return 0;
}

static int __iommu_take_dma_ownership(struct iommu_group *group, void *owner)
{
	int ret;

	if ((group->domain && group->domain != group->default_domain) ||
	    !xa_empty(&group->pasid_array))
		return -EBUSY;

	ret = __iommu_group_alloc_blocking_domain(group);
	if (ret)
		return ret;
	ret = __iommu_group_set_domain(group, group->blocking_domain);
	if (ret)
		return ret;

	group->owner = owner;
	group->owner_cnt++;
	return 0;
}

/**
 * iommu_group_claim_dma_owner() - Set DMA ownership of a group
 * @group: The group.
 * @owner: Caller specified pointer. Used for exclusive ownership.
 *
 * This is to support backward compatibility for vfio which manages the dma
 * ownership in iommu_group level. New invocations on this interface should be
 * prohibited. Only a single owner may exist for a group.
 */
int iommu_group_claim_dma_owner(struct iommu_group *group, void *owner)
{
	int ret = 0;

	if (WARN_ON(!owner))
		return -EINVAL;

	mutex_lock(&group->mutex);
	if (group->owner_cnt) {
		ret = -EPERM;
		goto unlock_out;
	}

	ret = __iommu_take_dma_ownership(group, owner);
unlock_out:
	mutex_unlock(&group->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_group_claim_dma_owner);

/**
 * iommu_device_claim_dma_owner() - Set DMA ownership of a device
 * @dev: The device.
 * @owner: Caller specified pointer. Used for exclusive ownership.
 *
 * Claim the DMA ownership of a device. Multiple devices in the same group may
 * concurrently claim ownership if they present the same owner value. Returns 0
 * on success and error code on failure
 */
int iommu_device_claim_dma_owner(struct device *dev, void *owner)
{
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;
	int ret = 0;

	if (WARN_ON(!owner))
		return -EINVAL;

	if (!group)
		return -ENODEV;

	mutex_lock(&group->mutex);
	if (group->owner_cnt) {
		if (group->owner != owner) {
			ret = -EPERM;
			goto unlock_out;
		}
		group->owner_cnt++;
		goto unlock_out;
	}

	ret = __iommu_take_dma_ownership(group, owner);
unlock_out:
	mutex_unlock(&group->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_device_claim_dma_owner);

static void __iommu_release_dma_ownership(struct iommu_group *group)
{
	if (WARN_ON(!group->owner_cnt || !group->owner ||
		    !xa_empty(&group->pasid_array)))
		return;

	group->owner_cnt = 0;
	group->owner = NULL;
	__iommu_group_set_domain_nofail(group, group->default_domain);
}

/**
 * iommu_group_release_dma_owner() - Release DMA ownership of a group
 * @group: The group
 *
 * Release the DMA ownership claimed by iommu_group_claim_dma_owner().
 */
void iommu_group_release_dma_owner(struct iommu_group *group)
{
	mutex_lock(&group->mutex);
	__iommu_release_dma_ownership(group);
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_group_release_dma_owner);

/**
 * iommu_device_release_dma_owner() - Release DMA ownership of a device
 * @dev: The device.
 *
 * Release the DMA ownership claimed by iommu_device_claim_dma_owner().
 */
void iommu_device_release_dma_owner(struct device *dev)
{
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;

	mutex_lock(&group->mutex);
	if (group->owner_cnt > 1)
		group->owner_cnt--;
	else
		__iommu_release_dma_ownership(group);
	mutex_unlock(&group->mutex);
}
EXPORT_SYMBOL_GPL(iommu_device_release_dma_owner);

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
	struct group_device *device, *last_gdev;
	int ret;

	for_each_group_device(group, device) {
		ret = domain->ops->set_dev_pasid(domain, device->dev, pasid);
		if (ret)
			goto err_revert;
	}

	return 0;

err_revert:
	last_gdev = device;
	for_each_group_device(group, device) {
		const struct iommu_ops *ops = dev_iommu_ops(device->dev);

		if (device == last_gdev)
			break;
		ops->remove_dev_pasid(device->dev, pasid);
	}
	return ret;
}

static void __iommu_remove_group_pasid(struct iommu_group *group,
				       ioasid_t pasid)
{
	struct group_device *device;
	const struct iommu_ops *ops;

	for_each_group_device(group, device) {
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
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;
	struct group_device *device;
	void *curr;
	int ret;

	if (!domain->ops->set_dev_pasid)
		return -EOPNOTSUPP;

	if (!group)
		return -ENODEV;

	if (!dev_has_iommu(dev) || dev_iommu_ops(dev) != domain->owner ||
	    pasid == IOMMU_NO_PASID)
		return -EINVAL;

	mutex_lock(&group->mutex);
	for_each_group_device(group, device) {
		if (pasid >= device->dev->iommu->max_pasids) {
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	curr = xa_cmpxchg(&group->pasid_array, pasid, NULL, domain, GFP_KERNEL);
	if (curr) {
		ret = xa_err(curr) ? : -EBUSY;
		goto out_unlock;
	}

	ret = __iommu_set_group_pasid(domain, group, pasid);
	if (ret)
		xa_erase(&group->pasid_array, pasid);
out_unlock:
	mutex_unlock(&group->mutex);
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
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;

	mutex_lock(&group->mutex);
	__iommu_remove_group_pasid(group, pasid);
	WARN_ON(xa_erase(&group->pasid_array, pasid) != domain);
	mutex_unlock(&group->mutex);
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
	/* Caller must be a probed driver on dev */
	struct iommu_group *group = dev->iommu_group;
	struct iommu_domain *domain;

	if (!group)
		return NULL;

	xa_lock(&group->pasid_array);
	domain = xa_load(&group->pasid_array, pasid);
	if (type && domain && domain->type != type)
		domain = ERR_PTR(-EBUSY);
	xa_unlock(&group->pasid_array);

	return domain;
}
EXPORT_SYMBOL_GPL(iommu_get_domain_for_dev_pasid);

ioasid_t iommu_alloc_global_pasid(struct device *dev)
{
	int ret;

	/* max_pasids == 0 means that the device does not support PASID */
	if (!dev->iommu->max_pasids)
		return IOMMU_PASID_INVALID;

	/*
	 * max_pasids is set up by vendor driver based on number of PASID bits
	 * supported but the IDA allocation is inclusive.
	 */
	ret = ida_alloc_range(&iommu_global_pasid_ida, IOMMU_FIRST_GLOBAL_PASID,
			      dev->iommu->max_pasids - 1, GFP_KERNEL);
	return ret < 0 ? IOMMU_PASID_INVALID : ret;
}
EXPORT_SYMBOL_GPL(iommu_alloc_global_pasid);

void iommu_free_global_pasid(ioasid_t pasid)
{
	if (WARN_ON(pasid == IOMMU_PASID_INVALID))
		return;

	ida_free(&iommu_global_pasid_ida, pasid);
}
EXPORT_SYMBOL_GPL(iommu_free_global_pasid);
