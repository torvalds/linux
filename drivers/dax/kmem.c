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

int dev_dax_kmem_probe(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct resource *res = &dev_dax->region->res;
	resource_size_t kmem_start;
	resource_size_t kmem_size;
	resource_size_t kmem_end;
	struct resource *new_res;
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

	/* Region is permanently reserved.  Hot-remove not yet implemented. */
	new_res = request_mem_region(kmem_start, kmem_size, dev_name(dev));
	if (!new_res) {
		dev_warn(dev, "could not reserve region [%pa-%pa]\n",
			 &kmem_start, &kmem_end);
		return -EBUSY;
	}

	/*
	 * Set flags appropriate for System RAM.  Leave ..._BUSY clear
	 * so that add_memory() can add a child resource.  Do not
	 * inherit flags from the parent since it may set new flags
	 * unknown to us that will break add_memory() below.
	 */
	new_res->flags = IORESOURCE_SYSTEM_RAM;
	new_res->name = dev_name(dev);

	rc = add_memory(numa_node, new_res->start, resource_size(new_res));
	if (rc) {
		release_resource(new_res);
		kfree(new_res);
		return rc;
	}

	return 0;
}

static int dev_dax_kmem_remove(struct device *dev)
{
	/*
	 * Purposely leak the request_mem_region() for the device-dax
	 * range and return '0' to ->remove() attempts. The removal of
	 * the device from the driver always succeeds, but the region
	 * is permanently pinned as reserved by the unreleased
	 * request_mem_region().
	 */
	return 0;
}

static struct dax_device_driver device_dax_kmem_driver = {
	.drv = {
		.probe = dev_dax_kmem_probe,
		.remove = dev_dax_kmem_remove,
	},
};

static int __init dax_kmem_init(void)
{
	return dax_driver_register(&device_dax_kmem_driver);
}

static void __exit dax_kmem_exit(void)
{
	dax_driver_unregister(&device_dax_kmem_driver);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
module_init(dax_kmem_init);
module_exit(dax_kmem_exit);
MODULE_ALIAS_DAX_DEVICE(0);
