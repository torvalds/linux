/*
 * Freescale MC object device allocator driver
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "../include/mc-private.h"
#include "../include/mc-sys.h"
#include <linux/module.h>
#include "../include/dpbp-cmd.h"
#include "../include/dpcon-cmd.h"
#include "dpmcp-cmd.h"
#include "dpmcp.h"
#include <linux/msi.h>

/**
 * fsl_mc_resource_pool_add_device - add allocatable device to a resource
 * pool of a given MC bus
 *
 * @mc_bus: pointer to the MC bus
 * @pool_type: MC bus pool type
 * @mc_dev: Pointer to allocatable MC object device
 *
 * It adds an allocatable MC object device to a container's resource pool of
 * the given resource type
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

	if (WARN_ON(pool_type < 0 || pool_type >= FSL_MC_NUM_POOL_TYPES))
		goto out;
	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		goto out;
	if (WARN_ON(mc_dev->resource))
		goto out;

	res_pool = &mc_bus->resource_pools[pool_type];
	if (WARN_ON(res_pool->type != pool_type))
		goto out;
	if (WARN_ON(res_pool->mc_bus != mc_bus))
		goto out;

	mutex_lock(&res_pool->mutex);

	if (WARN_ON(res_pool->max_count < 0))
		goto out_unlock;
	if (WARN_ON(res_pool->free_count < 0 ||
		    res_pool->free_count > res_pool->max_count))
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
 * @mc_dev: Pointer to allocatable MC object device
 *
 * It permanently removes an allocatable MC object device from the resource
 * pool, the device is currently in, as long as it is in the pool's free list.
 */
static int __must_check fsl_mc_resource_pool_remove_device(struct fsl_mc_device
								   *mc_dev)
{
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_resource_pool *res_pool;
	struct fsl_mc_resource *resource;
	int error = -EINVAL;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		goto out;

	resource = mc_dev->resource;
	if (WARN_ON(!resource || resource->data != mc_dev))
		goto out;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	res_pool = resource->parent_pool;
	if (WARN_ON(res_pool != &mc_bus->resource_pools[resource->type]))
		goto out;

	mutex_lock(&res_pool->mutex);

	if (WARN_ON(res_pool->max_count <= 0))
		goto out_unlock;
	if (WARN_ON(res_pool->free_count <= 0 ||
		    res_pool->free_count > res_pool->max_count))
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

	list_del(&resource->node);
	INIT_LIST_HEAD(&resource->node);
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
	if (WARN_ON(pool_type < 0 || pool_type >= FSL_MC_NUM_POOL_TYPES))
		goto out;

	res_pool = &mc_bus->resource_pools[pool_type];
	if (WARN_ON(res_pool->mc_bus != mc_bus))
		goto out;

	mutex_lock(&res_pool->mutex);
	resource = list_first_entry_or_null(&res_pool->free_list,
					    struct fsl_mc_resource, node);

	if (!resource) {
		WARN_ON(res_pool->free_count != 0);
		error = -ENXIO;
		dev_err(&mc_bus_dev->dev,
			"No more resources of type %s left\n",
			fsl_mc_pool_type_strings[pool_type]);
		goto out_unlock;
	}

	if (WARN_ON(resource->type != pool_type))
		goto out_unlock;
	if (WARN_ON(resource->parent_pool != res_pool))
		goto out_unlock;
	if (WARN_ON(res_pool->free_count <= 0 ||
		    res_pool->free_count > res_pool->max_count))
		goto out_unlock;

	list_del(&resource->node);
	INIT_LIST_HEAD(&resource->node);

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
	if (WARN_ON(resource->type != res_pool->type))
		return;

	mutex_lock(&res_pool->mutex);
	if (WARN_ON(res_pool->free_count < 0 ||
		    res_pool->free_count >= res_pool->max_count))
		goto out_unlock;

	if (WARN_ON(!list_empty(&resource->node)))
		goto out_unlock;

	list_add_tail(&resource->node, &res_pool->free_list);
	res_pool->free_count++;
out_unlock:
	mutex_unlock(&res_pool->mutex);
}
EXPORT_SYMBOL_GPL(fsl_mc_resource_free);

/**
 * fsl_mc_portal_allocate - Allocates an MC portal
 *
 * @mc_dev: MC device for which the MC portal is to be allocated
 * @mc_io_flags: Flags for the fsl_mc_io object that wraps the allocated
 * MC portal.
 * @new_mc_io: Pointer to area where the pointer to the fsl_mc_io object
 * that wraps the allocated MC portal is to be returned
 *
 * This function allocates an MC portal from the device's parent DPRC,
 * from the corresponding MC bus' pool of MC portals and wraps
 * it in a new fsl_mc_io object. If 'mc_dev' is a DPRC itself, the
 * portal is allocated from its own MC bus.
 */
int __must_check fsl_mc_portal_allocate(struct fsl_mc_device *mc_dev,
					u16 mc_io_flags,
					struct fsl_mc_io **new_mc_io)
{
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	phys_addr_t mc_portal_phys_addr;
	size_t mc_portal_size;
	struct fsl_mc_device *dpmcp_dev;
	int error = -EINVAL;
	struct fsl_mc_resource *resource = NULL;
	struct fsl_mc_io *mc_io = NULL;

	if (mc_dev->flags & FSL_MC_IS_DPRC) {
		mc_bus_dev = mc_dev;
	} else {
		if (WARN_ON(!dev_is_fsl_mc(mc_dev->dev.parent)))
			return error;

		mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	}

	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	*new_mc_io = NULL;
	error = fsl_mc_resource_allocate(mc_bus, FSL_MC_POOL_DPMCP, &resource);
	if (error < 0)
		return error;

	error = -EINVAL;
	dpmcp_dev = resource->data;
	if (WARN_ON(!dpmcp_dev))
		goto error_cleanup_resource;

	if (dpmcp_dev->obj_desc.ver_major < DPMCP_MIN_VER_MAJOR ||
	    (dpmcp_dev->obj_desc.ver_major == DPMCP_MIN_VER_MAJOR &&
	     dpmcp_dev->obj_desc.ver_minor < DPMCP_MIN_VER_MINOR)) {
		dev_err(&dpmcp_dev->dev,
			"ERROR: Version %d.%d of DPMCP not supported.\n",
			dpmcp_dev->obj_desc.ver_major,
			dpmcp_dev->obj_desc.ver_minor);
		error = -ENOTSUPP;
		goto error_cleanup_resource;
	}

	if (WARN_ON(dpmcp_dev->obj_desc.region_count == 0))
		goto error_cleanup_resource;

	mc_portal_phys_addr = dpmcp_dev->regions[0].start;
	mc_portal_size = dpmcp_dev->regions[0].end -
			 dpmcp_dev->regions[0].start + 1;

	if (WARN_ON(mc_portal_size != mc_bus_dev->mc_io->portal_size))
		goto error_cleanup_resource;

	error = fsl_create_mc_io(&mc_bus_dev->dev,
				 mc_portal_phys_addr,
				 mc_portal_size, dpmcp_dev,
				 mc_io_flags, &mc_io);
	if (error < 0)
		goto error_cleanup_resource;

	*new_mc_io = mc_io;
	return 0;

error_cleanup_resource:
	fsl_mc_resource_free(resource);
	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_portal_allocate);

/**
 * fsl_mc_portal_free - Returns an MC portal to the pool of free MC portals
 * of a given MC bus
 *
 * @mc_io: Pointer to the fsl_mc_io object that wraps the MC portal to free
 */
void fsl_mc_portal_free(struct fsl_mc_io *mc_io)
{
	struct fsl_mc_device *dpmcp_dev;
	struct fsl_mc_resource *resource;

	/*
	 * Every mc_io obtained by calling fsl_mc_portal_allocate() is supposed
	 * to have a DPMCP object associated with.
	 */
	dpmcp_dev = mc_io->dpmcp_dev;
	if (WARN_ON(!dpmcp_dev))
		return;

	resource = dpmcp_dev->resource;
	if (WARN_ON(!resource || resource->type != FSL_MC_POOL_DPMCP))
		return;

	if (WARN_ON(resource->data != dpmcp_dev))
		return;

	fsl_destroy_mc_io(mc_io);
	fsl_mc_resource_free(resource);
}
EXPORT_SYMBOL_GPL(fsl_mc_portal_free);

/**
 * fsl_mc_portal_reset - Resets the dpmcp object for a given fsl_mc_io object
 *
 * @mc_io: Pointer to the fsl_mc_io object that wraps the MC portal to free
 */
int fsl_mc_portal_reset(struct fsl_mc_io *mc_io)
{
	int error;
	struct fsl_mc_device *dpmcp_dev = mc_io->dpmcp_dev;

	if (WARN_ON(!dpmcp_dev))
		return -EINVAL;

	error = dpmcp_reset(mc_io, 0, dpmcp_dev->mc_handle);
	if (error < 0) {
		dev_err(&dpmcp_dev->dev, "dpmcp_reset() failed: %d\n", error);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_mc_portal_reset);

/**
 * fsl_mc_object_allocate - Allocates a MC object device of the given
 * pool type from a given MC bus
 *
 * @mc_dev: MC device for which the MC object device is to be allocated
 * @pool_type: MC bus resource pool type
 * @new_mc_dev: Pointer to area where the pointer to the allocated
 * MC object device is to be returned
 *
 * This function allocates a MC object device from the device's parent DPRC,
 * from the corresponding MC bus' pool of allocatable MC object devices of
 * the given resource type. mc_dev cannot be a DPRC itself.
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
	if (WARN_ON(mc_dev->flags & FSL_MC_IS_DPRC))
		goto error;

	if (WARN_ON(!dev_is_fsl_mc(mc_dev->dev.parent)))
		goto error;

	if (WARN_ON(pool_type == FSL_MC_POOL_DPMCP))
		goto error;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	error = fsl_mc_resource_allocate(mc_bus, pool_type, &resource);
	if (error < 0)
		goto error;

	mc_adev = resource->data;
	if (WARN_ON(!mc_adev))
		goto error;

	*new_mc_adev = mc_adev;
	return 0;
error:
	if (resource)
		fsl_mc_resource_free(resource);

	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_object_allocate);

/**
 * fsl_mc_object_free - Returns an allocatable MC object device to the
 * corresponding resource pool of a given MC bus.
 *
 * @mc_adev: Pointer to the MC object device
 */
void fsl_mc_object_free(struct fsl_mc_device *mc_adev)
{
	struct fsl_mc_resource *resource;

	resource = mc_adev->resource;
	if (WARN_ON(resource->type == FSL_MC_POOL_DPMCP))
		return;
	if (WARN_ON(resource->data != mc_adev))
		return;

	fsl_mc_resource_free(resource);
}
EXPORT_SYMBOL_GPL(fsl_mc_object_free);

/*
 * Initialize the interrupt pool associated with a MC bus.
 * It allocates a block of IRQs from the GIC-ITS
 */
int fsl_mc_populate_irq_pool(struct fsl_mc_bus *mc_bus,
			     unsigned int irq_count)
{
	unsigned int i;
	struct msi_desc *msi_desc;
	struct fsl_mc_device_irq *irq_resources;
	struct fsl_mc_device_irq *mc_dev_irq;
	int error;
	struct fsl_mc_device *mc_bus_dev = &mc_bus->mc_dev;
	struct fsl_mc_resource_pool *res_pool =
			&mc_bus->resource_pools[FSL_MC_POOL_IRQ];

	if (WARN_ON(irq_count == 0 ||
		    irq_count > FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS))
		return -EINVAL;

	error = fsl_mc_msi_domain_alloc_irqs(&mc_bus_dev->dev, irq_count);
	if (error < 0)
		return error;

	irq_resources = devm_kzalloc(&mc_bus_dev->dev,
				     sizeof(*irq_resources) * irq_count,
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
		INIT_LIST_HEAD(&mc_dev_irq->resource.node);
		list_add_tail(&mc_dev_irq->resource.node, &res_pool->free_list);
	}

	for_each_msi_entry(msi_desc, &mc_bus_dev->dev) {
		mc_dev_irq = &irq_resources[msi_desc->fsl_mc.msi_index];
		mc_dev_irq->msi_desc = msi_desc;
		mc_dev_irq->resource.id = msi_desc->irq;
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

/**
 * Teardown the interrupt pool associated with an MC bus.
 * It frees the IRQs that were allocated to the pool, back to the GIC-ITS.
 */
void fsl_mc_cleanup_irq_pool(struct fsl_mc_bus *mc_bus)
{
	struct fsl_mc_device *mc_bus_dev = &mc_bus->mc_dev;
	struct fsl_mc_resource_pool *res_pool =
			&mc_bus->resource_pools[FSL_MC_POOL_IRQ];

	if (WARN_ON(!mc_bus->irq_resources))
		return;

	if (WARN_ON(res_pool->max_count == 0))
		return;

	if (WARN_ON(res_pool->free_count != res_pool->max_count))
		return;

	INIT_LIST_HEAD(&res_pool->free_list);
	res_pool->max_count = 0;
	res_pool->free_count = 0;
	mc_bus->irq_resources = NULL;
	fsl_mc_msi_domain_free_irqs(&mc_bus_dev->dev);
}
EXPORT_SYMBOL_GPL(fsl_mc_cleanup_irq_pool);

/**
 * It allocates the IRQs required by a given MC object device. The
 * IRQs are allocated from the interrupt pool associated with the
 * MC bus that contains the device, if the device is not a DPRC device.
 * Otherwise, the IRQs are allocated from the interrupt pool associated
 * with the MC bus that represents the DPRC device itself.
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

	if (WARN_ON(mc_dev->irqs))
		return -EINVAL;

	irq_count = mc_dev->obj_desc.irq_count;
	if (WARN_ON(irq_count == 0))
		return -EINVAL;

	if (strcmp(mc_dev->obj_desc.type, "dprc") == 0)
		mc_bus = to_fsl_mc_bus(mc_dev);
	else
		mc_bus = to_fsl_mc_bus(to_fsl_mc_device(mc_dev->dev.parent));

	if (WARN_ON(!mc_bus->irq_resources))
		return -EINVAL;

	res_pool = &mc_bus->resource_pools[FSL_MC_POOL_IRQ];
	if (res_pool->free_count < irq_count) {
		dev_err(&mc_dev->dev,
			"Not able to allocate %u irqs for device\n", irq_count);
		return -ENOSPC;
	}

	irqs = devm_kzalloc(&mc_dev->dev, irq_count * sizeof(irqs[0]),
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

		WARN_ON(irqs[i]->mc_dev);
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
 * It frees the IRQs that were allocated for a MC object device, by
 * returning them to the corresponding interrupt pool.
 */
void fsl_mc_free_irqs(struct fsl_mc_device *mc_dev)
{
	int i;
	int irq_count;
	struct fsl_mc_bus *mc_bus;
	struct fsl_mc_device_irq **irqs = mc_dev->irqs;

	if (WARN_ON(!irqs))
		return;

	irq_count = mc_dev->obj_desc.irq_count;

	if (strcmp(mc_dev->obj_desc.type, "dprc") == 0)
		mc_bus = to_fsl_mc_bus(mc_dev);
	else
		mc_bus = to_fsl_mc_bus(to_fsl_mc_device(mc_dev->dev.parent));

	if (WARN_ON(!mc_bus->irq_resources))
		return;

	for (i = 0; i < irq_count; i++) {
		WARN_ON(!irqs[i]->mc_dev);
		irqs[i]->mc_dev = NULL;
		fsl_mc_resource_free(&irqs[i]->resource);
	}

	mc_dev->irqs = NULL;
}
EXPORT_SYMBOL_GPL(fsl_mc_free_irqs);

/**
 * fsl_mc_allocator_probe - callback invoked when an allocatable device is
 * being added to the system
 */
static int fsl_mc_allocator_probe(struct fsl_mc_device *mc_dev)
{
	enum fsl_mc_pool_type pool_type;
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	int error;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		return -EINVAL;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	if (WARN_ON(!dev_is_fsl_mc(&mc_bus_dev->dev)))
		return -EINVAL;

	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	error = object_type_to_pool_type(mc_dev->obj_desc.type, &pool_type);
	if (error < 0)
		return error;

	error = fsl_mc_resource_pool_add_device(mc_bus, pool_type, mc_dev);
	if (error < 0)
		return error;

	dev_dbg(&mc_dev->dev,
		"Allocatable MC object device bound to fsl_mc_allocator driver");
	return 0;
}

/**
 * fsl_mc_allocator_remove - callback invoked when an allocatable device is
 * being removed from the system
 */
static int fsl_mc_allocator_remove(struct fsl_mc_device *mc_dev)
{
	int error;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		return -EINVAL;

	if (mc_dev->resource) {
		error = fsl_mc_resource_pool_remove_device(mc_dev);
		if (error < 0)
			return error;
	}

	dev_dbg(&mc_dev->dev,
		"Allocatable MC object device unbound from fsl_mc_allocator driver");
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
		   .owner = THIS_MODULE,
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
