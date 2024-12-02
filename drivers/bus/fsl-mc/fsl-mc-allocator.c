// SPDX-License-Identifier: GPL-2.0
/*
 * fsl-mc object allocator driver
 *
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 *
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <linux/fsl/mc.h>

#include "fsl-mc-private.h"

static bool __must_check fsl_mc_is_allocatable(struct fsl_mc_device *mc_dev)
{
	return is_fsl_mc_bus_dpbp(mc_dev) ||
	       is_fsl_mc_bus_dpmcp(mc_dev) ||
	       is_fsl_mc_bus_dpcon(mc_dev);
}

/**
 * fsl_mc_resource_pool_add_device - add allocatable object to a resource
 * pool of a given fsl-mc bus
 *
 * @mc_bus: pointer to the fsl-mc bus
 * @pool_type: pool type
 * @mc_dev: pointer to allocatable fsl-mc device
 */
static int __must_check fsl_mc_resource_pool_add_device(struct fsl_mc_bus
								*mc_bus,
							enum fsl_mc_pool_type
								pool_type,
							struct fsl_mc_device
								*mc_dev)
{
	struct fsl_mc_resource_pool *res_pool;
	struct fsl_mc_resource *resource;
	struct fsl_mc_device *mc_bus_dev = &mc_bus->mc_dev;
	int error = -EINVAL;

	if (pool_type < 0 || pool_type >= FSL_MC_NUM_POOL_TYPES)
		goto out;
	if (!fsl_mc_is_allocatable(mc_dev))
		goto out;
	if (mc_dev->resource)
		goto out;

	res_pool = &mc_bus->resource_pools[pool_type];
	if (res_pool->type != pool_type)
		goto out;
	if (res_pool->mc_bus != mc_bus)
		goto out;

	mutex_lock(&res_pool->mutex);

	if (res_pool->max_count < 0)
		goto out_unlock;
	if (res_pool->free_count < 0 ||
	    res_pool->free_count > res_pool->max_count)
		goto out_unlock;

	resource = devm_kzalloc(&mc_bus_dev->dev, sizeof(*resource),
				GFP_KERNEL);
	if (!resource) {
		error = -ENOMEM;
		dev_err(&mc_bus_dev->dev,
			"Failed to allocate memory for fsl_mc_resource\n");
		goto out_unlock;
	}

	resource->type = pool_type;
	resource->id = mc_dev->obj_desc.id;
	resource->data = mc_dev;
	resource->parent_pool = res_pool;
	INIT_LIST_HEAD(&resource->node);
	list_add_tail(&resource->node, &res_pool->free_list);
	mc_dev->resource = resource;
	res_pool->free_count++;
	res_pool->max_count++;
	error = 0;
out_unlock:
	mutex_unlock(&res_pool->mutex);
out:
	return error;
}

/**
 * fsl_mc_resource_pool_remove_device - remove an allocatable device from a
 * resource pool
 *
 * @mc_dev: pointer to allocatable fsl-mc device
 *
 * It permanently removes an allocatable fsl-mc device from the resource
 * pool. It's an error if the device is in use.
 */
static int __must_check fsl_mc_resource_pool_remove_device(struct fsl_mc_device
								   *mc_dev)
{
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_resource_pool *res_pool;
	struct fsl_mc_resource *resource;
	int error = -EINVAL;

	if (!fsl_mc_is_allocatable(mc_dev))
		goto out;

	resource = mc_dev->resource;
	if (!resource || resource->data != mc_dev)
		goto out;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	res_pool = resource->parent_pool;
	if (res_pool != &mc_bus->resource_pools[resource->type])
		goto out;

	mutex_lock(&res_pool->mutex);

	if (res_pool->max_count <= 0)
		goto out_unlock;
	if (res_pool->free_count <= 0 ||
	    res_pool->free_count > res_pool->max_count)
		goto out_unlock;

	/*
	 * If the device is currently allocated, its resource is not
	 * in the free list and thus, the device cannot be removed.
	 */
	if (list_empty(&resource->node)) {
		error = -EBUSY;
		dev_err(&mc_bus_dev->dev,
			"Device %s cannot be removed from resource pool\n",
			dev_name(&mc_dev->dev));
		goto out_unlock;
	}

	list_del_init(&resource->node);
	res_pool->free_count--;
	res_pool->max_count--;

	devm_kfree(&mc_bus_dev->dev, resource);
	mc_dev->resource = NULL;
	error = 0;
out_unlock:
	mutex_unlock(&res_pool->mutex);
out:
	return error;
}

static const char *const fsl_mc_pool_type_strings[] = {
	[FSL_MC_POOL_DPMCP] = "dpmcp",
	[FSL_MC_POOL_DPBP] = "dpbp",
	[FSL_MC_POOL_DPCON] = "dpcon",
	[FSL_MC_POOL_IRQ] = "irq",
};

static int __must_check object_type_to_pool_type(const char *object_type,
						 enum fsl_mc_pool_type
								*pool_type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fsl_mc_pool_type_strings); i++) {
		if (strcmp(object_type, fsl_mc_pool_type_strings[i]) == 0) {
			*pool_type = i;
			return 0;
		}
	}

	return -EINVAL;
}

int __must_check fsl_mc_resource_allocate(struct fsl_mc_bus *mc_bus,
					  enum fsl_mc_pool_type pool_type,
					  struct fsl_mc_resource **new_resource)
{
	struct fsl_mc_resource_pool *res_pool;
	struct fsl_mc_resource *resource;
	struct fsl_mc_device *mc_bus_dev = &mc_bus->mc_dev;
	int error = -EINVAL;

	BUILD_BUG_ON(ARRAY_SIZE(fsl_mc_pool_type_strings) !=
		     FSL_MC_NUM_POOL_TYPES);

	*new_resource = NULL;
	if (pool_type < 0 || pool_type >= FSL_MC_NUM_POOL_TYPES)
		goto out;

	res_pool = &mc_bus->resource_pools[pool_type];
	if (res_pool->mc_bus != mc_bus)
		goto out;

	mutex_lock(&res_pool->mutex);
	resource = list_first_entry_or_null(&res_pool->free_list,
					    struct fsl_mc_resource, node);

	if (!resource) {
		error = -ENXIO;
		dev_err(&mc_bus_dev->dev,
			"No more resources of type %s left\n",
			fsl_mc_pool_type_strings[pool_type]);
		goto out_unlock;
	}

	if (resource->type != pool_type)
		goto out_unlock;
	if (resource->parent_pool != res_pool)
		goto out_unlock;
	if (res_pool->free_count <= 0 ||
	    res_pool->free_count > res_pool->max_count)
		goto out_unlock;

	list_del_init(&resource->node);

	res_pool->free_count--;
	error = 0;
out_unlock:
	mutex_unlock(&res_pool->mutex);
	*new_resource = resource;
out:
	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_resource_allocate);

void fsl_mc_resource_free(struct fsl_mc_resource *resource)
{
	struct fsl_mc_resource_pool *res_pool;

	res_pool = resource->parent_pool;
	if (resource->type != res_pool->type)
		return;

	mutex_lock(&res_pool->mutex);
	if (res_pool->free_count < 0 ||
	    res_pool->free_count >= res_pool->max_count)
		goto out_unlock;

	if (!list_empty(&resource->node))
		goto out_unlock;

	list_add_tail(&resource->node, &res_pool->free_list);
	res_pool->free_count++;
out_unlock:
	mutex_unlock(&res_pool->mutex);
}
EXPORT_SYMBOL_GPL(fsl_mc_resource_free);

/**
 * fsl_mc_object_allocate - Allocates an fsl-mc object of the given
 * pool type from a given fsl-mc bus instance
 *
 * @mc_dev: fsl-mc device which is used in conjunction with the
 * allocated object
 * @pool_type: pool type
 * @new_mc_adev: pointer to area where the pointer to the allocated device
 * is to be returned
 *
 * Allocatable objects are always used in conjunction with some functional
 * device.  This function allocates an object of the specified type from
 * the DPRC containing the functional device.
 *
 * NOTE: pool_type must be different from FSL_MC_POOL_MCP, since MC
 * portals are allocated using fsl_mc_portal_allocate(), instead of
 * this function.
 */
int __must_check fsl_mc_object_allocate(struct fsl_mc_device *mc_dev,
					enum fsl_mc_pool_type pool_type,
					struct fsl_mc_device **new_mc_adev)
{
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_device *mc_adev;
	int error = -EINVAL;
	struct fsl_mc_resource *resource = NULL;

	*new_mc_adev = NULL;
	if (mc_dev->flags & FSL_MC_IS_DPRC)
		goto error;

	if (!dev_is_fsl_mc(mc_dev->dev.parent))
		goto error;

	if (pool_type == FSL_MC_POOL_DPMCP)
		goto error;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	error = fsl_mc_resource_allocate(mc_bus, pool_type, &resource);
	if (error < 0)
		goto error;

	mc_adev = resource->data;
	if (!mc_adev) {
		error = -EINVAL;
		goto error;
	}

	mc_adev->consumer_link = device_link_add(&mc_dev->dev,
						 &mc_adev->dev,
						 DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!mc_adev->consumer_link) {
		error = -EINVAL;
		goto error;
	}

	*new_mc_adev = mc_adev;
	return 0;
error:
	if (resource)
		fsl_mc_resource_free(resource);

	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_object_allocate);

/**
 * fsl_mc_object_free - Returns an fsl-mc object to the resource
 * pool where it came from.
 * @mc_adev: Pointer to the fsl-mc device
 */
void fsl_mc_object_free(struct fsl_mc_device *mc_adev)
{
	struct fsl_mc_resource *resource;

	resource = mc_adev->resource;
	if (resource->type == FSL_MC_POOL_DPMCP)
		return;
	if (resource->data != mc_adev)
		return;

	fsl_mc_resource_free(resource);

	mc_adev->consumer_link = NULL;
}
EXPORT_SYMBOL_GPL(fsl_mc_object_free);

/*
 * A DPRC and the devices in the DPRC all share the same GIC-ITS device
 * ID.  A block of IRQs is pre-allocated and maintained in a pool
 * from which devices can allocate them when needed.
 */

/*
 * Initialize the interrupt pool associated with an fsl-mc bus.
 * It allocates a block of IRQs from the GIC-ITS.
 */
int fsl_mc_populate_irq_pool(struct fsl_mc_device *mc_bus_dev,
			     unsigned int irq_count)
{
	unsigned int i;
	struct fsl_mc_device_irq *irq_resources;
	struct fsl_mc_device_irq *mc_dev_irq;
	int error;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_resource_pool *res_pool =
			&mc_bus->resource_pools[FSL_MC_POOL_IRQ];

	/* do nothing if the IRQ pool is already populated */
	if (mc_bus->irq_resources)
		return 0;

	if (irq_count == 0 ||
	    irq_count > FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS)
		return -EINVAL;

	error = fsl_mc_msi_domain_alloc_irqs(&mc_bus_dev->dev, irq_count);
	if (error < 0)
		return error;

	irq_resources = devm_kcalloc(&mc_bus_dev->dev,
				     irq_count, sizeof(*irq_resources),
				     GFP_KERNEL);
	if (!irq_resources) {
		error = -ENOMEM;
		goto cleanup_msi_irqs;
	}

	for (i = 0; i < irq_count; i++) {
		mc_dev_irq = &irq_resources[i];

		/*
		 * NOTE: This mc_dev_irq's MSI addr/value pair will be set
		 * by the fsl_mc_msi_write_msg() callback
		 */
		mc_dev_irq->resource.type = res_pool->type;
		mc_dev_irq->resource.data = mc_dev_irq;
		mc_dev_irq->resource.parent_pool = res_pool;
		mc_dev_irq->virq = msi_get_virq(&mc_bus_dev->dev, i);
		mc_dev_irq->resource.id = mc_dev_irq->virq;
		INIT_LIST_HEAD(&mc_dev_irq->resource.node);
		list_add_tail(&mc_dev_irq->resource.node, &res_pool->free_list);
	}

	res_pool->max_count = irq_count;
	res_pool->free_count = irq_count;
	mc_bus->irq_resources = irq_resources;
	return 0;

cleanup_msi_irqs:
	fsl_mc_msi_domain_free_irqs(&mc_bus_dev->dev);
	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_populate_irq_pool);

/*
 * Teardown the interrupt pool associated with an fsl-mc bus.
 * It frees the IRQs that were allocated to the pool, back to the GIC-ITS.
 */
void fsl_mc_cleanup_irq_pool(struct fsl_mc_device *mc_bus_dev)
{
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_resource_pool *res_pool =
			&mc_bus->resource_pools[FSL_MC_POOL_IRQ];

	if (!mc_bus->irq_resources)
		return;

	if (res_pool->max_count == 0)
		return;

	if (res_pool->free_count != res_pool->max_count)
		return;

	INIT_LIST_HEAD(&res_pool->free_list);
	res_pool->max_count = 0;
	res_pool->free_count = 0;
	mc_bus->irq_resources = NULL;
	fsl_mc_msi_domain_free_irqs(&mc_bus_dev->dev);
}
EXPORT_SYMBOL_GPL(fsl_mc_cleanup_irq_pool);

/*
 * Allocate the IRQs required by a given fsl-mc device.
 */
int __must_check fsl_mc_allocate_irqs(struct fsl_mc_device *mc_dev)
{
	int i;
	int irq_count;
	int res_allocated_count = 0;
	int error = -EINVAL;
	struct fsl_mc_device_irq **irqs = NULL;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_resource_pool *res_pool;

	if (mc_dev->irqs)
		return -EINVAL;

	irq_count = mc_dev->obj_desc.irq_count;
	if (irq_count == 0)
		return -EINVAL;

	if (is_fsl_mc_bus_dprc(mc_dev))
		mc_bus = to_fsl_mc_bus(mc_dev);
	else
		mc_bus = to_fsl_mc_bus(to_fsl_mc_device(mc_dev->dev.parent));

	if (!mc_bus->irq_resources)
		return -EINVAL;

	res_pool = &mc_bus->resource_pools[FSL_MC_POOL_IRQ];
	if (res_pool->free_count < irq_count) {
		dev_err(&mc_dev->dev,
			"Not able to allocate %u irqs for device\n", irq_count);
		return -ENOSPC;
	}

	irqs = devm_kcalloc(&mc_dev->dev, irq_count, sizeof(irqs[0]),
			    GFP_KERNEL);
	if (!irqs)
		return -ENOMEM;

	for (i = 0; i < irq_count; i++) {
		struct fsl_mc_resource *resource;

		error = fsl_mc_resource_allocate(mc_bus, FSL_MC_POOL_IRQ,
						 &resource);
		if (error < 0)
			goto error_resource_alloc;

		irqs[i] = to_fsl_mc_irq(resource);
		res_allocated_count++;

		irqs[i]->mc_dev = mc_dev;
		irqs[i]->dev_irq_index = i;
	}

	mc_dev->irqs = irqs;
	return 0;

error_resource_alloc:
	for (i = 0; i < res_allocated_count; i++) {
		irqs[i]->mc_dev = NULL;
		fsl_mc_resource_free(&irqs[i]->resource);
	}

	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_allocate_irqs);

/*
 * Frees the IRQs that were allocated for an fsl-mc device.
 */
void fsl_mc_free_irqs(struct fsl_mc_device *mc_dev)
{
	int i;
	int irq_count;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_device_irq **irqs = mc_dev->irqs;

	if (!irqs)
		return;

	irq_count = mc_dev->obj_desc.irq_count;

	if (is_fsl_mc_bus_dprc(mc_dev))
		mc_bus = to_fsl_mc_bus(mc_dev);
	else
		mc_bus = to_fsl_mc_bus(to_fsl_mc_device(mc_dev->dev.parent));

	if (!mc_bus->irq_resources)
		return;

	for (i = 0; i < irq_count; i++) {
		irqs[i]->mc_dev = NULL;
		fsl_mc_resource_free(&irqs[i]->resource);
	}

	mc_dev->irqs = NULL;
}
EXPORT_SYMBOL_GPL(fsl_mc_free_irqs);

void fsl_mc_init_all_resource_pools(struct fsl_mc_device *mc_bus_dev)
{
	int pool_type;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);

	for (pool_type = 0; pool_type < FSL_MC_NUM_POOL_TYPES; pool_type++) {
		struct fsl_mc_resource_pool *res_pool =
		    &mc_bus->resource_pools[pool_type];

		res_pool->type = pool_type;
		res_pool->max_count = 0;
		res_pool->free_count = 0;
		res_pool->mc_bus = mc_bus;
		INIT_LIST_HEAD(&res_pool->free_list);
		mutex_init(&res_pool->mutex);
	}
}

static void fsl_mc_cleanup_resource_pool(struct fsl_mc_device *mc_bus_dev,
					 enum fsl_mc_pool_type pool_type)
{
	struct fsl_mc_resource *resource;
	struct fsl_mc_resource *next;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_resource_pool *res_pool =
					&mc_bus->resource_pools[pool_type];
	int free_count = 0;

	list_for_each_entry_safe(resource, next, &res_pool->free_list, node) {
		free_count++;
		devm_kfree(&mc_bus_dev->dev, resource);
	}
}

void fsl_mc_cleanup_all_resource_pools(struct fsl_mc_device *mc_bus_dev)
{
	int pool_type;

	for (pool_type = 0; pool_type < FSL_MC_NUM_POOL_TYPES; pool_type++)
		fsl_mc_cleanup_resource_pool(mc_bus_dev, pool_type);
}

/*
 * fsl_mc_allocator_probe - callback invoked when an allocatable device is
 * being added to the system
 */
static int fsl_mc_allocator_probe(struct fsl_mc_device *mc_dev)
{
	enum fsl_mc_pool_type pool_type;
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	int error;

	if (!fsl_mc_is_allocatable(mc_dev))
		return -EINVAL;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	if (!dev_is_fsl_mc(&mc_bus_dev->dev))
		return -EINVAL;

	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	error = object_type_to_pool_type(mc_dev->obj_desc.type, &pool_type);
	if (error < 0)
		return error;

	error = fsl_mc_resource_pool_add_device(mc_bus, pool_type, mc_dev);
	if (error < 0)
		return error;

	dev_dbg(&mc_dev->dev,
		"Allocatable fsl-mc device bound to fsl_mc_allocator driver");
	return 0;
}

/*
 * fsl_mc_allocator_remove - callback invoked when an allocatable device is
 * being removed from the system
 */
static int fsl_mc_allocator_remove(struct fsl_mc_device *mc_dev)
{
	int error;

	if (!fsl_mc_is_allocatable(mc_dev))
		return -EINVAL;

	if (mc_dev->resource) {
		error = fsl_mc_resource_pool_remove_device(mc_dev);
		if (error < 0)
			return error;
	}

	dev_dbg(&mc_dev->dev,
		"Allocatable fsl-mc device unbound from fsl_mc_allocator driver");
	return 0;
}

static const struct fsl_mc_device_id match_id_table[] = {
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpbp",
	},
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpmcp",
	},
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpcon",
	},
	{.vendor = 0x0},
};

static struct fsl_mc_driver fsl_mc_allocator_driver = {
	.driver = {
		   .name = "fsl_mc_allocator",
		   .pm = NULL,
		   },
	.match_id_table = match_id_table,
	.probe = fsl_mc_allocator_probe,
	.remove = fsl_mc_allocator_remove,
};

int __init fsl_mc_allocator_driver_init(void)
{
	return fsl_mc_driver_register(&fsl_mc_allocator_driver);
}

void fsl_mc_allocator_driver_exit(void)
{
	fsl_mc_driver_unregister(&fsl_mc_allocator_driver);
}
