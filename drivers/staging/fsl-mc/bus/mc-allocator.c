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
	bool mutex_locked = false;

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
	mutex_locked = true;

	if (WARN_ON(res_pool->max_count < 0))
		goto out;
	if (WARN_ON(res_pool->free_count < 0 ||
		    res_pool->free_count > res_pool->max_count))
		goto out;

	resource = devm_kzalloc(&mc_bus_dev->dev, sizeof(*resource),
				GFP_KERNEL);
	if (!resource) {
		error = -ENOMEM;
		dev_err(&mc_bus_dev->dev,
			"Failed to allocate memory for fsl_mc_resource\n");
		goto out;
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
out:
	if (mutex_locked)
		mutex_unlock(&res_pool->mutex);

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
	bool mutex_locked = false;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		goto out;

	resource = mc_dev->resource;
	if (WARN_ON(resource->data != mc_dev))
		goto out;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	res_pool = resource->parent_pool;
	if (WARN_ON(res_pool != &mc_bus->resource_pools[resource->type]))
		goto out;

	mutex_lock(&res_pool->mutex);
	mutex_locked = true;

	if (WARN_ON(res_pool->max_count <= 0))
		goto out;
	if (WARN_ON(res_pool->free_count <= 0 ||
		    res_pool->free_count > res_pool->max_count))
		goto out;

	/*
	 * If the device is currently allocated, its resource is not
	 * in the free list and thus, the device cannot be removed.
	 */
	if (list_empty(&resource->node)) {
		error = -EBUSY;
		dev_err(&mc_bus_dev->dev,
			"Device %s cannot be removed from resource pool\n",
			dev_name(&mc_dev->dev));
		goto out;
	}

	list_del(&resource->node);
	INIT_LIST_HEAD(&resource->node);
	res_pool->free_count--;
	res_pool->max_count--;

	devm_kfree(&mc_bus_dev->dev, resource);
	mc_dev->resource = NULL;
	error = 0;
out:
	if (mutex_locked)
		mutex_unlock(&res_pool->mutex);

	return error;
}

static const char *const fsl_mc_pool_type_strings[] = {
	[FSL_MC_POOL_DPMCP] = "dpmcp",
	[FSL_MC_POOL_DPBP] = "dpbp",
	[FSL_MC_POOL_DPCON] = "dpcon",
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
	bool mutex_locked = false;

	BUILD_BUG_ON(ARRAY_SIZE(fsl_mc_pool_type_strings) !=
		     FSL_MC_NUM_POOL_TYPES);

	*new_resource = NULL;
	if (WARN_ON(pool_type < 0 || pool_type >= FSL_MC_NUM_POOL_TYPES))
		goto error;

	res_pool = &mc_bus->resource_pools[pool_type];
	if (WARN_ON(res_pool->mc_bus != mc_bus))
		goto error;

	mutex_lock(&res_pool->mutex);
	mutex_locked = true;
	resource = list_first_entry_or_null(&res_pool->free_list,
					    struct fsl_mc_resource, node);

	if (!resource) {
		WARN_ON(res_pool->free_count != 0);
		error = -ENXIO;
		dev_err(&mc_bus_dev->dev,
			"No more resources of type %s left\n",
			fsl_mc_pool_type_strings[pool_type]);
		goto error;
	}

	if (WARN_ON(resource->type != pool_type))
		goto error;
	if (WARN_ON(resource->parent_pool != res_pool))
		goto error;
	if (WARN_ON(res_pool->free_count <= 0 ||
		    res_pool->free_count > res_pool->max_count))
		goto error;

	list_del(&resource->node);
	INIT_LIST_HEAD(&resource->node);

	res_pool->free_count--;
	mutex_unlock(&res_pool->mutex);
	*new_resource = resource;
	return 0;
error:
	if (mutex_locked)
		mutex_unlock(&res_pool->mutex);

	return error;
}
EXPORT_SYMBOL_GPL(fsl_mc_resource_allocate);

void fsl_mc_resource_free(struct fsl_mc_resource *resource)
{
	struct fsl_mc_resource_pool *res_pool;
	bool mutex_locked = false;

	res_pool = resource->parent_pool;
	if (WARN_ON(resource->type != res_pool->type))
		goto out;

	mutex_lock(&res_pool->mutex);
	mutex_locked = true;
	if (WARN_ON(res_pool->free_count < 0 ||
		    res_pool->free_count >= res_pool->max_count))
		goto out;

	if (WARN_ON(!list_empty(&resource->node)))
		goto out;

	list_add_tail(&resource->node, &res_pool->free_list);
	res_pool->free_count++;
out:
	if (mutex_locked)
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
					uint16_t mc_io_flags,
					struct fsl_mc_io **new_mc_io)
{
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	phys_addr_t mc_portal_phys_addr;
	size_t mc_portal_size;
	struct fsl_mc_device *mc_adev;
	int error = -EINVAL;
	struct fsl_mc_resource *resource = NULL;
	struct fsl_mc_io *mc_io = NULL;

	if (mc_dev->flags & FSL_MC_IS_DPRC) {
		mc_bus_dev = mc_dev;
	} else {
		if (WARN_ON(mc_dev->dev.parent->bus != &fsl_mc_bus_type))
			return error;

		mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	}

	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	*new_mc_io = NULL;
	error = fsl_mc_resource_allocate(mc_bus, FSL_MC_POOL_DPMCP, &resource);
	if (error < 0)
		return error;

	mc_adev = resource->data;
	if (WARN_ON(!mc_adev))
		goto error_cleanup_resource;

	if (WARN_ON(mc_adev->obj_desc.region_count == 0))
		goto error_cleanup_resource;

	mc_portal_phys_addr = mc_adev->regions[0].start;
	mc_portal_size = mc_adev->regions[0].end -
			 mc_adev->regions[0].start + 1;

	if (WARN_ON(mc_portal_size != mc_bus_dev->mc_io->portal_size))
		goto error_cleanup_resource;

	error = fsl_create_mc_io(&mc_bus_dev->dev,
				 mc_portal_phys_addr,
				 mc_portal_size, resource,
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
	struct fsl_mc_resource *resource;

	resource = mc_io->resource;
	if (WARN_ON(resource->type != FSL_MC_POOL_DPMCP))
		return;
	if (WARN_ON(!resource->data))
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
	uint16_t token;
	struct fsl_mc_resource *resource = mc_io->resource;
	struct fsl_mc_device *mc_dev = resource->data;

	if (WARN_ON(resource->type != FSL_MC_POOL_DPMCP))
		return -EINVAL;

	if (WARN_ON(!mc_dev))
		return -EINVAL;

	error = dpmcp_open(mc_io, mc_dev->obj_desc.id, &token);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dpmcp_open() failed: %d\n", error);
		return error;
	}

	error = dpmcp_reset(mc_io, token);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dpmcp_reset() failed: %d\n", error);
		return error;
	}

	error = dpmcp_close(mc_io, token);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dpmcp_close() failed: %d\n", error);
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

	if (WARN_ON(mc_dev->dev.parent->bus != &fsl_mc_bus_type))
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

/**
 * fsl_mc_allocator_probe - callback invoked when an allocatable device is
 * being added to the system
 */
static int fsl_mc_allocator_probe(struct fsl_mc_device *mc_dev)
{
	enum fsl_mc_pool_type pool_type;
	struct fsl_mc_device *mc_bus_dev;
	struct fsl_mc_bus *mc_bus;
	int error = -EINVAL;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		goto error;

	mc_bus_dev = to_fsl_mc_device(mc_dev->dev.parent);
	if (WARN_ON(mc_bus_dev->dev.bus != &fsl_mc_bus_type))
		goto error;

	mc_bus = to_fsl_mc_bus(mc_bus_dev);
	error = object_type_to_pool_type(mc_dev->obj_desc.type, &pool_type);
	if (error < 0)
		goto error;

	error = fsl_mc_resource_pool_add_device(mc_bus, pool_type, mc_dev);
	if (error < 0)
		goto error;

	dev_info(&mc_dev->dev,
		 "Allocatable MC object device bound to fsl_mc_allocator driver");
	return 0;
error:

	return error;
}

/**
 * fsl_mc_allocator_remove - callback invoked when an allocatable device is
 * being removed from the system
 */
static int fsl_mc_allocator_remove(struct fsl_mc_device *mc_dev)
{
	int error = -EINVAL;

	if (WARN_ON(!FSL_MC_IS_ALLOCATABLE(mc_dev->obj_desc.type)))
		goto out;

	error = fsl_mc_resource_pool_remove_device(mc_dev);
	if (error < 0)
		goto out;

	dev_info(&mc_dev->dev,
		 "Allocatable MC object device unbound from fsl_mc_allocator driver");
	error = 0;
out:
	return error;
}

static const struct fsl_mc_device_match_id match_id_table[] = {
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpbp",
	 .ver_major = DPBP_VER_MAJOR,
	 .ver_minor = DPBP_VER_MINOR
	},
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpmcp",
	 .ver_major = DPMCP_VER_MAJOR,
	 .ver_minor = DPMCP_VER_MINOR
	},
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dpcon",
	 .ver_major = DPCON_VER_MAJOR,
	 .ver_minor = DPCON_VER_MINOR
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

void __exit fsl_mc_allocator_driver_exit(void)
{
	fsl_mc_driver_unregister(&fsl_mc_allocator_driver);
}
