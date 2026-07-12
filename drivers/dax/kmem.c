// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2019 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory-tiers.h>
#include <linux/memory_hotplug.h>
#include <linux/string_helpers.h>
#include "dax-private.h"
#include "bus.h"

/*
 * Default abstract distance assigned to the NUMA node onlined
 * by DAX/kmem if the low level platform driver didn't initialize
 * one for this NUMA node.
 */
#define MEMTIER_DEFAULT_DAX_ADISTANCE	(MEMTIER_ADISTANCE_DRAM * 5)

/* Memory resource name used for add_memory_driver_managed(). */
static const char *kmem_name;
/* Set if any memory will remain added when the driver will be unloaded. */
static bool any_hotremove_failed;

static int dax_kmem_range(struct dev_dax *dev_dax, int i, struct range *r)
{
	struct dev_dax_range *dax_range = &dev_dax->ranges[i];
	struct range *range = &dax_range->range;

	*r = memory_block_aligned_range(range);
	if (r->start >= r->end) {
		r->start = range->start;
		r->end = range->end;
		return -ENOSPC;
	}
	return 0;
}

struct dax_kmem_data {
	const char *res_name;
	int mgid;
	int state;
	struct mutex lock; /* protects hotplug state transitions */
	struct resource *res[];
};

static DEFINE_MUTEX(kmem_memory_type_lock);
static LIST_HEAD(kmem_memory_types);

static struct memory_dev_type *kmem_find_alloc_memory_type(int adist)
{
	guard(mutex)(&kmem_memory_type_lock);
	return mt_find_alloc_memory_type(adist, &kmem_memory_types);
}

static void kmem_put_memory_types(void)
{
	guard(mutex)(&kmem_memory_type_lock);
	mt_put_memory_types(&kmem_memory_types);
}

/* True for the online states a kmem dax device can hold. */
static bool dax_kmem_state_is_online(int state)
{
	return state == MMOP_ONLINE ||
	       state == MMOP_ONLINE_KERNEL ||
	       state == MMOP_ONLINE_MOVABLE;
}

/**
 * dax_kmem_do_hotplug - hotplug memory for dax kmem device
 * @dev_dax: the dev_dax instance
 * @data: the dax_kmem_data structure with resource tracking
 * @online_type: the online policy to use for the memory blocks
 *
 * Hotplugs all ranges in the dev_dax region as system memory with the
 * provided online policy (offline, online, online_movable, online_kernel).
 *
 * Returns the number of successfully mapped ranges, or negative error.
 */
static int dax_kmem_do_hotplug(struct dev_dax *dev_dax,
			       struct dax_kmem_data *data,
			       int online_type)
{
	struct device *dev = &dev_dax->dev;
	int i, rc, added = 0;
	mhp_t mhp_flags;

	if (dax_kmem_state_is_online(data->state))
		return -EINVAL;

	if (online_type < MMOP_OFFLINE || online_type > MMOP_ONLINE_MOVABLE)
		return -EINVAL;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		/*
		 * init_resources() is best-effort: if a reservation conflict
		 * occurs it keeps the range but leaves res[i]=NULL. For hotplug
		 * on probe systems, this means kmem will partially online.
		 *
		 * We have to keep this behavior not to break those systems.
		 * For those systems - atomicity only applies to valid ranges.
		 */
		if (!data->res[i])
			continue;

		mhp_flags = MHP_NID_IS_MGID;
		if (dev_dax->memmap_on_memory)
			mhp_flags |= MHP_MEMMAP_ON_MEMORY;

		/*
		 * Ensure that future kexec'd kernels will not treat
		 * this as RAM automatically.
		 */
		rc = __add_memory_driver_managed(data->mgid, range.start,
				range_len(&range), kmem_name, mhp_flags,
				online_type);

		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx memory add failed\n",
				 i, range.start, range.end);
			/*
			 * Release the reservation for the range that failed to
			 * add so a later hotremove does not try to remove memory
			 * that was never added.
			 */
			if (data->res[i]) {
				remove_resource(data->res[i]);
				kfree(data->res[i]);
				data->res[i] = NULL;
			}
			if (added)
				continue;
			return rc;
		}
		added++;
	}

	return added;
}

/**
 * dax_kmem_init_resources - create memory regions for dax kmem
 * @dev_dax: the dev_dax instance
 * @data: the dax_kmem_data structure with resource tracking
 *
 * Initializes all the resources for the DAX
 *
 * Returns the number of successfully mapped ranges, or negative error.
 */
static int dax_kmem_init_resources(struct dev_dax *dev_dax,
				   struct dax_kmem_data *data)
{
	struct device *dev = &dev_dax->dev;
	int i, rc, mapped = 0;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct resource *res;
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		/* Skip ranges already added */
		if (data->res[i])
			continue;

		/* Region is permanently reserved if hotremove fails. */
		res = request_mem_region(range.start, range_len(&range),
					 data->res_name);
		if (!res) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve region\n",
				 i, range.start, range.end);
			/*
			 * Once some memory has been onlined we can't
			 * assume that it can be un-onlined safely.
			 */
			if (mapped)
				continue;
			return -EBUSY;
		}
		data->res[i] = res;
		/*
		 * Set flags appropriate for System RAM.  Leave ..._BUSY clear
		 * so that add_memory() can add a child resource.  Do not
		 * inherit flags from the parent since it may set new flags
		 * unknown to us that will break add_memory() later.
		 */
		res->flags = IORESOURCE_SYSTEM_RAM;
		mapped++;
	}
	return mapped;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
/**
 * dax_kmem_do_hotremove - hot-remove memory for dax kmem device
 * @dev_dax: the dev_dax instance
 * @data: the dax_kmem_data structure with resource tracking
 *
 * Offlines and removes every currently-added range in the dev_dax region
 * atomically: either all ranges are offlined and removed, or none are and
 * the device is returned to its prior state.
 *
 * Returns 0 on success, or a negative errno on failure.
 */
static int dax_kmem_do_hotremove(struct dev_dax *dev_dax,
				 struct dax_kmem_data *data)
{
	struct device *dev = &dev_dax->dev;
	struct range *ranges;
	int i, nr_ranges = 0, rc;

	ranges = kmalloc_objs(*ranges, dev_dax->nr_range);
	if (!ranges)
		return -ENOMEM;

	/* Collect the ranges that were actually added during probe. */
	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		if (!data->res[i])
			continue;
		if (dax_kmem_range(dev_dax, i, &range))
			continue;
		ranges[nr_ranges++] = range;
	}

	/* Nothing added means nothing to remove. */
	if (!nr_ranges) {
		kfree(ranges);
		return 0;
	}

	rc = offline_and_remove_memory_ranges(ranges, nr_ranges);
	kfree(ranges);
	if (rc) {
		/* Recoverable: the ranges rolled back, nothing is leaked yet. */
		dev_err(dev, "hotremove failed, device left online: %d\n", rc);
		return rc;
	}

	/* All ranges removed; release the reserved resources. */
	for (i = 0; i < dev_dax->nr_range; i++) {
		if (!data->res[i])
			continue;
		remove_resource(data->res[i]);
		kfree(data->res[i]);
		data->res[i] = NULL;
	}

	return 0;
}
#else
static int dax_kmem_do_hotremove(struct dev_dax *dev_dax,
				 struct dax_kmem_data *data)
{
	return -EBUSY;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

/**
 * dax_kmem_cleanup_resources - remove the dax memory resources
 * @dev_dax: the dev_dax instance
 * @data: the dax_kmem_data structure with resource tracking
 *
 * Removes all resources in the dev_dax region.
 */
static void dax_kmem_cleanup_resources(struct dev_dax *dev_dax,
				       struct dax_kmem_data *data)
{
	int i;

	/*
	 * If the device unbind occurs before memory is hotremoved, we can never
	 * remove the memory (requires reboot).  Attempting an offline operation
	 * here may cause deadlock and a failure to finish the unbind.
	 *
	 * Note: This leaks the resources.
	 */
	if (WARN(((data->state != DAX_KMEM_UNPLUGGED) &&
		  (data->state != MMOP_OFFLINE)),
		 "Hotplug memory regions stuck online until reboot"))
		return;

	for (i = 0; i < dev_dax->nr_range; i++) {
		if (!data->res[i])
			continue;
		remove_resource(data->res[i]);
		kfree(data->res[i]);
		data->res[i] = NULL;
	}
}

static int dax_kmem_parse_state(const char *buf)
{
	int online_type;

	/* "unplugged" is kmem-specific - the rest map to MMOP_ */
	if (sysfs_streq(buf, "unplugged"))
		return DAX_KMEM_UNPLUGGED;

	online_type = mhp_online_type_from_str(buf);
	/* Disallow "offline": it's not useful and creates race conditions */
	if (online_type == MMOP_OFFLINE)
		return -EINVAL;
	return online_type;
}

static ssize_t state_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct dax_kmem_data *data = dev_get_drvdata(dev);
	const char *state_str;

	if (data->state == DAX_KMEM_UNPLUGGED)
		state_str = "unplugged";
	else
		state_str = mhp_online_type_to_str(data->state);

	return sysfs_emit(buf, "%s\n", state_str ?: "unknown");
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_kmem_data *data = dev_get_drvdata(dev);
	int online_type;
	int rc;

	online_type = dax_kmem_parse_state(buf);
	if (online_type < DAX_KMEM_UNPLUGGED)
		return online_type;

	guard(mutex)(&data->lock);

	/* Already in requested state */
	if (data->state == online_type)
		return len;

	if (online_type == DAX_KMEM_UNPLUGGED) {
		rc = dax_kmem_do_hotremove(dev_dax, data);
		if (rc)
			return rc;
		data->state = DAX_KMEM_UNPLUGGED;
		return len;
	}

	/* Onlining is only allowed from the unplugged state. */
	if (data->state != DAX_KMEM_UNPLUGGED)
		return -EBUSY;

	/* Re-acquire resources if previously unplugged, otherwise no-op */
	rc = dax_kmem_init_resources(dev_dax, data);
	if (rc < 0)
		return rc;

	rc = dax_kmem_do_hotplug(dev_dax, data, online_type);
	if (rc < 0) {
		/* Total failure, drop the reservations we took. */
		dax_kmem_cleanup_resources(dev_dax, data);
		return rc;
	}

	data->state = online_type;
	return len;
}

static int dev_dax_kmem_probe(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	unsigned long total_len = 0, orig_len = 0;
	struct dax_kmem_data *data;
	struct memory_dev_type *mtype;
	int i, rc;
	int numa_node;
	int adist = MEMTIER_DEFAULT_DAX_ADISTANCE;
	int online_type = mhp_get_default_online_type();

	/*
	 * Ensure good NUMA information for the persistent memory.
	 * Without this check, there is a risk that slow memory
	 * could be mixed in a node with faster memory, causing
	 * unavoidable performance issues.
	 */
	numa_node = dev_dax->target_node;
	if (numa_node < 0) {
		dev_warn(dev, "rejecting DAX region with invalid node: %d\n",
				numa_node);
		return -EINVAL;
	}

	mt_calc_adistance(numa_node, &adist);
	mtype = kmem_find_alloc_memory_type(adist);
	if (IS_ERR(mtype))
		return PTR_ERR(mtype);

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		orig_len += range_len(&dev_dax->ranges[i].range);
		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc) {
			dev_info(dev, "mapping%d: %#llx-%#llx too small after alignment\n",
					i, range.start, range.end);
			continue;
		}
		total_len += range_len(&range);
	}

	if (!total_len) {
		dev_warn(dev, "rejecting DAX region without any memory after alignment\n");
		return -EINVAL;
	} else if (total_len != orig_len) {
		char buf[16];

		string_get_size(orig_len - total_len, 1, STRING_UNITS_2,
				buf, sizeof(buf));
		dev_warn(dev, "DAX region truncated by %s due to alignment\n", buf);
	}

	init_node_memory_type(numa_node, mtype);

	rc = -ENOMEM;
	data = kzalloc_flex(*data, res, dev_dax->nr_range);
	if (!data)
		goto err_dax_kmem_data;

	data->res_name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!data->res_name)
		goto err_res_name;

	rc = memory_group_register_static(numa_node, PFN_UP(total_len));
	if (rc < 0)
		goto err_reg_mgid;
	data->mgid = rc;
	data->state = DAX_KMEM_UNPLUGGED;
	mutex_init(&data->lock);

	dev_set_drvdata(dev, data);

	rc = dax_kmem_init_resources(dev_dax, data);
	if (rc < 0)
		goto err_resources;

	rc = dax_kmem_do_hotplug(dev_dax, data, online_type);
	if (rc < 0)
		goto err_hotplug;
	data->state = online_type;

	return 0;

err_hotplug:
	dax_kmem_cleanup_resources(dev_dax, data);
err_resources:
	dev_set_drvdata(dev, NULL);
	memory_group_unregister(data->mgid);
err_reg_mgid:
	kfree(data->res_name);
err_res_name:
	kfree(data);
err_dax_kmem_data:
	clear_node_memory_type(numa_node, mtype);
	return rc;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
/*
 * Remove the device's added ranges with remove_memory().
 * Unlike the sysfs unplug path it never offlines and fails if the blocks are
 * online (-EBUSY), so it is safe from unbind. Failures leak until reboot.
 *
 * Returns 0 only if every added range was removed.
 */
static int dax_kmem_remove_ranges(struct dev_dax *dev_dax,
				  struct dax_kmem_data *data)
{
	struct device *dev = &dev_dax->dev;
	int i, rc = 0;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		if (!data->res[i] || dax_kmem_range(dev_dax, i, &range))
			continue;
		if (remove_memory(range.start, range_len(&range))) {
			dev_warn(dev, "mapping%d: %#llx-%#llx stuck online until reboot\n",
				 i, range.start, range.end);
			rc = -EBUSY;
			continue;
		}
		remove_resource(data->res[i]);
		kfree(data->res[i]);
		data->res[i] = NULL;
	}
	return rc;
}

static void dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	int node = dev_dax->target_node;
	struct device *dev = &dev_dax->dev;
	struct dax_kmem_data *data = dev_get_drvdata(dev);

	/*
	 * Remove every range that is still added.  dax_kmem_remove_ranges()
	 * uses remove_memory(), which never offlines: an online block fails
	 * with -EBUSY rather than deadlocking an uninterruptible unbind.
	 *
	 * data->state only tracks daxX.Y/state writes, so it can be stale if
	 * blocks were toggled via memoryX/state. Do not trust it here and
	 * attempt simply remove_memory() - which reports the true state of
	 * each range anyway. Anything left online is leaked until reboot.
	 */
	if (dax_kmem_remove_ranges(dev_dax, data)) {
		dev_err(dev, "Hotplug regions stuck online until reboot\n");
		any_hotremove_failed = true;
		return;
	}

	memory_group_unregister(data->mgid);
	kfree(data->res_name);
	kfree(data);
	dev_set_drvdata(dev, NULL);
	/*
	 * Clear the memtype association on successful unplug.
	 * If not, we have memory blocks left which can be
	 * offlined/onlined later. We need to keep memory_dev_type
	 * for that. This implies this reference will be around
	 * till next reboot.
	 */
	clear_node_memory_type(node, NULL);
}
#else
static void dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	/*
	 * Without hotremove purposely leak the request_mem_region() for the
	 * device-dax range and return '0' to ->remove() attempts. The removal
	 * of the device from the driver always succeeds, but the region is
	 * permanently pinned as reserved by the unreleased
	 * request_mem_region().
	 */
	any_hotremove_failed = true;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static DEVICE_ATTR_RW(state);

static struct attribute *dev_dax_kmem_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dev_dax_kmem);

static struct dax_device_driver device_dax_kmem_driver = {
	.probe = dev_dax_kmem_probe,
	.remove = dev_dax_kmem_remove,
	.type = DAXDRV_KMEM_TYPE,
	.drv = {
		.dev_groups = dev_dax_kmem_groups,
	},
};

static int __init dax_kmem_init(void)
{
	int rc;

	/* Resource name is permanently allocated if any hotremove fails. */
	kmem_name = kstrdup_const("System RAM (kmem)", GFP_KERNEL);
	if (!kmem_name)
		return -ENOMEM;

	rc = dax_driver_register(&device_dax_kmem_driver);
	if (rc)
		goto error_dax_driver;

	return rc;

error_dax_driver:
	kmem_put_memory_types();
	kfree_const(kmem_name);
	return rc;
}

static void __exit dax_kmem_exit(void)
{
	dax_driver_unregister(&device_dax_kmem_driver);
	if (!any_hotremove_failed)
		kfree_const(kmem_name);
	kmem_put_memory_types();
}

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("KMEM DAX: map dax-devices as System-RAM");
MODULE_LICENSE("GPL v2");
module_init(dax_kmem_init);
module_exit(dax_kmem_exit);
MODULE_ALIAS_DAX_DEVICE(0);
