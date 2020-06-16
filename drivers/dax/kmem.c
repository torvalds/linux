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
#include "dax-private.h"
#include "bus.h"

/* Memory resource name used for add_memory_driver_managed(). */
static const char *kmem_name;
/* Set if any memory will remain added when the driver will be unloaded. */
static bool any_hotremove_failed;

int dev_dax_kmem_probe(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct resource *res = &dev_dax->region->res;
	resource_size_t kmem_start;
	resource_size_t kmem_size;
	resource_size_t kmem_end;
	struct resource *new_res;
	const char *new_res_name;
	int numa_node;
	int rc;

	/*
	 * Ensure good NUMA information for the persistent memory.
	 * Without this check, there is a risk that slow memory
	 * could be mixed in a node with faster memory, causing
	 * unavoidable performance issues.
	 */
	numa_node = dev_dax->target_node;
	if (numa_node < 0) {
		dev_warn(dev, "rejecting DAX region %pR with invalid node: %d\n",
			 res, numa_node);
		return -EINVAL;
	}

	/* Hotplug starting at the beginning of the next block: */
	kmem_start = ALIGN(res->start, memory_block_size_bytes());

	kmem_size = resource_size(res);
	/* Adjust the size down to compensate for moving up kmem_start: */
	kmem_size -= kmem_start - res->start;
	/* Align the size down to cover only complete blocks: */
	kmem_size &= ~(memory_block_size_bytes() - 1);
	kmem_end = kmem_start + kmem_size;

	new_res_name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!new_res_name)
		return -ENOMEM;

	/* Region is permanently reserved if hotremove fails. */
	new_res = request_mem_region(kmem_start, kmem_size, new_res_name);
	if (!new_res) {
		dev_warn(dev, "could not reserve region [%pa-%pa]\n",
			 &kmem_start, &kmem_end);
		kfree(new_res_name);
		return -EBUSY;
	}

	/*
	 * Set flags appropriate for System RAM.  Leave ..._BUSY clear
	 * so that add_memory() can add a child resource.  Do not
	 * inherit flags from the parent since it may set new flags
	 * unknown to us that will break add_memory() below.
	 */
	new_res->flags = IORESOURCE_SYSTEM_RAM;

	/*
	 * Ensure that future kexec'd kernels will not treat this as RAM
	 * automatically.
	 */
	rc = add_memory_driver_managed(numa_node, new_res->start,
				       resource_size(new_res), kmem_name);
	if (rc) {
		release_resource(new_res);
		kfree(new_res);
		kfree(new_res_name);
		return rc;
	}
	dev_dax->dax_kmem_res = new_res;

	return 0;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static int dev_dax_kmem_remove(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct resource *res = dev_dax->dax_kmem_res;
	resource_size_t kmem_start = res->start;
	resource_size_t kmem_size = resource_size(res);
	const char *res_name = res->name;
	int rc;

	/*
	 * We have one shot for removing memory, if some memory blocks were not
	 * offline prior to calling this function remove_memory() will fail, and
	 * there is no way to hotremove this memory until reboot because device
	 * unbind will succeed even if we return failure.
	 */
	rc = remove_memory(dev_dax->target_node, kmem_start, kmem_size);
	if (rc) {
		any_hotremove_failed = true;
		dev_err(dev,
			"DAX region %pR cannot be hotremoved until the next reboot\n",
			res);
		return rc;
	}

	/* Release and free dax resources */
	release_resource(res);
	kfree(res);
	kfree(res_name);
	dev_dax->dax_kmem_res = NULL;

	return 0;
}
#else
static int dev_dax_kmem_remove(struct device *dev)
{
	/*
	 * Without hotremove purposely leak the request_mem_region() for the
	 * device-dax range and return '0' to ->remove() attempts. The removal
	 * of the device from the driver always succeeds, but the region is
	 * permanently pinned as reserved by the unreleased
	 * request_mem_region().
	 */
	any_hotremove_failed = true;
	return 0;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static struct dax_device_driver device_dax_kmem_driver = {
	.drv = {
		.probe = dev_dax_kmem_probe,
		.remove = dev_dax_kmem_remove,
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
		kfree_const(kmem_name);
	return rc;
}

static void __exit dax_kmem_exit(void)
{
	dax_driver_unregister(&device_dax_kmem_driver);
	if (!any_hotremove_failed)
		kfree_const(kmem_name);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
module_init(dax_kmem_init);
module_exit(dax_kmem_exit);
MODULE_ALIAS_DAX_DEVICE(0);
