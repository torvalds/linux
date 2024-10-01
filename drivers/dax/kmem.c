// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2019 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pfn_t.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory-tiers.h>
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

	/* memory-block align the hotplug range */
	r->start = ALIGN(range->start, memory_block_size_bytes());
	r->end = ALIGN_DOWN(range->end + 1, memory_block_size_bytes()) - 1;
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
	struct resource *res[];
};

static struct memory_dev_type *dax_slowmem_type;
static int dev_dax_kmem_probe(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	unsigned long total_len = 0;
	struct dax_kmem_data *data;
	int i, rc, mapped = 0;
	int numa_node;

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

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

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
	}

	init_node_memory_type(numa_node, dax_slowmem_type);

	rc = -ENOMEM;
	data = kzalloc(struct_size(data, res, dev_dax->nr_range), GFP_KERNEL);
	if (!data)
		goto err_dax_kmem_data;

	data->res_name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!data->res_name)
		goto err_res_name;

	rc = memory_group_register_static(numa_node, PFN_UP(total_len));
	if (rc < 0)
		goto err_reg_mgid;
	data->mgid = rc;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct resource *res;
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		/* Region is permanently reserved if hotremove fails. */
		res = request_mem_region(range.start, range_len(&range), data->res_name);
		if (!res) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve region\n",
					i, range.start, range.end);
			/*
			 * Once some memory has been onlined we can't
			 * assume that it can be un-onlined safely.
			 */
			if (mapped)
				continue;
			rc = -EBUSY;
			goto err_request_mem;
		}
		data->res[i] = res;

		/*
		 * Set flags appropriate for System RAM.  Leave ..._BUSY clear
		 * so that add_memory() can add a child resource.  Do not
		 * inherit flags from the parent since it may set new flags
		 * unknown to us that will break add_memory() below.
		 */
		res->flags = IORESOURCE_SYSTEM_RAM;

		/*
		 * Ensure that future kexec'd kernels will not treat
		 * this as RAM automatically.
		 */
		rc = add_memory_driver_managed(data->mgid, range.start,
				range_len(&range), kmem_name, MHP_NID_IS_MGID);

		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx memory add failed\n",
					i, range.start, range.end);
			remove_resource(res);
			kfree(res);
			data->res[i] = NULL;
			if (mapped)
				continue;
			goto err_request_mem;
		}
		mapped++;
	}

	dev_set_drvdata(dev, data);

	return 0;

err_request_mem:
	memory_group_unregister(data->mgid);
err_reg_mgid:
	kfree(data->res_name);
err_res_name:
	kfree(data);
err_dax_kmem_data:
	clear_node_memory_type(numa_node, dax_slowmem_type);
	return rc;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static void dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	int i, success = 0;
	int node = dev_dax->target_node;
	struct device *dev = &dev_dax->dev;
	struct dax_kmem_data *data = dev_get_drvdata(dev);

	/*
	 * We have one shot for removing memory, if some memory blocks were not
	 * offline prior to calling this function remove_memory() will fail, and
	 * there is no way to hotremove this memory until reboot because device
	 * unbind will succeed even if we return failure.
	 */
	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;
		int rc;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		rc = remove_memory(range.start, range_len(&range));
		if (rc == 0) {
			remove_resource(data->res[i]);
			kfree(data->res[i]);
			data->res[i] = NULL;
			success++;
			continue;
		}
		any_hotremove_failed = true;
		dev_err(dev,
			"mapping%d: %#llx-%#llx cannot be hotremoved until the next reboot\n",
				i, range.start, range.end);
	}

	if (success >= dev_dax->nr_range) {
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
		clear_node_memory_type(node, dax_slowmem_type);
	}
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

static struct dax_device_driver device_dax_kmem_driver = {
	.probe = dev_dax_kmem_probe,
	.remove = dev_dax_kmem_remove,
};

static int __init dax_kmem_init(void)
{
	int rc;

	/* Resource name is permanently allocated if any hotremove fails. */
	kmem_name = kstrdup_const("System RAM (kmem)", GFP_KERNEL);
	if (!kmem_name)
		return -ENOMEM;

	dax_slowmem_type = alloc_memory_type(MEMTIER_DEFAULT_DAX_ADISTANCE);
	if (IS_ERR(dax_slowmem_type)) {
		rc = PTR_ERR(dax_slowmem_type);
		goto err_dax_slowmem_type;
	}

	rc = dax_driver_register(&device_dax_kmem_driver);
	if (rc)
		goto error_dax_driver;

	return rc;

error_dax_driver:
	destroy_memory_type(dax_slowmem_type);
err_dax_slowmem_type:
	kfree_const(kmem_name);
	return rc;
}

static void __exit dax_kmem_exit(void)
{
	dax_driver_unregister(&device_dax_kmem_driver);
	if (!any_hotremove_failed)
		kfree_const(kmem_name);
	destroy_memory_type(dax_slowmem_type);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
module_init(dax_kmem_init);
module_exit(dax_kmem_exit);
MODULE_ALIAS_DAX_DEVICE(0);
