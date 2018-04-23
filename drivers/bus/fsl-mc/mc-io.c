// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 */

#include <linux/io.h>
#include <linux/fsl/mc.h>

#include "fsl-mc-private.h"

static int fsl_mc_io_set_dpmcp(struct fsl_mc_io *mc_io,
			       struct fsl_mc_device *dpmcp_dev)
{
	int error;

	if (mc_io->dpmcp_dev)
		return -EINVAL;

	if (dpmcp_dev->mc_io)
		return -EINVAL;

	error = dpmcp_open(mc_io,
			   0,
			   dpmcp_dev->obj_desc.id,
			   &dpmcp_dev->mc_handle);
	if (error < 0)
		return error;

	mc_io->dpmcp_dev = dpmcp_dev;
	dpmcp_dev->mc_io = mc_io;
	return 0;
}

static void fsl_mc_io_unset_dpmcp(struct fsl_mc_io *mc_io)
{
	int error;
	struct fsl_mc_device *dpmcp_dev = mc_io->dpmcp_dev;

	error = dpmcp_close(mc_io,
			    0,
			    dpmcp_dev->mc_handle);
	if (error < 0) {
		dev_err(&dpmcp_dev->dev, "dpmcp_close() failed: %d\n",
			error);
	}

	mc_io->dpmcp_dev = NULL;
	dpmcp_dev->mc_io = NULL;
}

/**
 * Creates an MC I/O object
 *
 * @dev: device to be associated with the MC I/O object
 * @mc_portal_phys_addr: physical address of the MC portal to use
 * @mc_portal_size: size in bytes of the MC portal
 * @dpmcp-dev: Pointer to the DPMCP object associated with this MC I/O
 * object or NULL if none.
 * @flags: flags for the new MC I/O object
 * @new_mc_io: Area to return pointer to newly created MC I/O object
 *
 * Returns '0' on Success; Error code otherwise.
 */
int __must_check fsl_create_mc_io(struct device *dev,
				  phys_addr_t mc_portal_phys_addr,
				  u32 mc_portal_size,
				  struct fsl_mc_device *dpmcp_dev,
				  u32 flags, struct fsl_mc_io **new_mc_io)
{
	int error;
	struct fsl_mc_io *mc_io;
	void __iomem *mc_portal_virt_addr;
	struct resource *res;

	mc_io = devm_kzalloc(dev, sizeof(*mc_io), GFP_KERNEL);
	if (!mc_io)
		return -ENOMEM;

	mc_io->dev = dev;
	mc_io->flags = flags;
	mc_io->portal_phys_addr = mc_portal_phys_addr;
	mc_io->portal_size = mc_portal_size;
	if (flags & FSL_MC_IO_ATOMIC_CONTEXT_PORTAL)
		spin_lock_init(&mc_io->spinlock);
	else
		mutex_init(&mc_io->mutex);

	res = devm_request_mem_region(dev,
				      mc_portal_phys_addr,
				      mc_portal_size,
				      "mc_portal");
	if (!res) {
		dev_err(dev,
			"devm_request_mem_region failed for MC portal %pa\n",
			&mc_portal_phys_addr);
		return -EBUSY;
	}

	mc_portal_virt_addr = devm_ioremap_nocache(dev,
						   mc_portal_phys_addr,
						   mc_portal_size);
	if (!mc_portal_virt_addr) {
		dev_err(dev,
			"devm_ioremap_nocache failed for MC portal %pa\n",
			&mc_portal_phys_addr);
		return -ENXIO;
	}

	mc_io->portal_virt_addr = mc_portal_virt_addr;
	if (dpmcp_dev) {
		error = fsl_mc_io_set_dpmcp(mc_io, dpmcp_dev);
		if (error < 0)
			goto error_destroy_mc_io;
	}

	*new_mc_io = mc_io;
	return 0;

error_destroy_mc_io:
	fsl_destroy_mc_io(mc_io);
	return error;
}

/**
 * Destroys an MC I/O object
 *
 * @mc_io: MC I/O object to destroy
 */
void fsl_destroy_mc_io(struct fsl_mc_io *mc_io)
{
	struct fsl_mc_device *dpmcp_dev = mc_io->dpmcp_dev;

	if (dpmcp_dev)
		fsl_mc_io_unset_dpmcp(mc_io);

	devm_iounmap(mc_io->dev, mc_io->portal_virt_addr);
	devm_release_mem_region(mc_io->dev,
				mc_io->portal_phys_addr,
				mc_io->portal_size);

	mc_io->portal_virt_addr = NULL;
	devm_kfree(mc_io->dev, mc_io);
}

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
		if (!dev_is_fsl_mc(mc_dev->dev.parent))
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

	mc_portal_phys_addr = dpmcp_dev->regions[0].start;
	mc_portal_size = resource_size(dpmcp_dev->regions);

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

	resource = dpmcp_dev->resource;
	if (!resource || resource->type != FSL_MC_POOL_DPMCP)
		return;

	if (resource->data != dpmcp_dev)
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

	error = dpmcp_reset(mc_io, 0, dpmcp_dev->mc_handle);
	if (error < 0) {
		dev_err(&dpmcp_dev->dev, "dpmcp_reset() failed: %d\n", error);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_mc_portal_reset);
