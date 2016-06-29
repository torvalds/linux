/*
 * Freescale Management Complex (MC) bus public interface
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _FSL_MC_H_
#define _FSL_MC_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include "../include/dprc.h"

#define FSL_MC_VENDOR_FREESCALE	0x1957

struct fsl_mc_device;
struct fsl_mc_io;
struct fsl_mc_bus;

/**
 * struct fsl_mc_driver - MC object device driver object
 * @driver: Generic device driver
 * @match_id_table: table of supported device matching Ids
 * @probe: Function called when a device is added
 * @remove: Function called when a device is removed
 * @shutdown: Function called at shutdown time to quiesce the device
 * @suspend: Function called when a device is stopped
 * @resume: Function called when a device is resumed
 *
 * Generic DPAA device driver object for device drivers that are registered
 * with a DPRC bus. This structure is to be embedded in each device-specific
 * driver structure.
 */
struct fsl_mc_driver {
	struct device_driver driver;
	const struct fsl_mc_device_id *match_id_table;
	int (*probe)(struct fsl_mc_device *dev);
	int (*remove)(struct fsl_mc_device *dev);
	void (*shutdown)(struct fsl_mc_device *dev);
	int (*suspend)(struct fsl_mc_device *dev, pm_message_t state);
	int (*resume)(struct fsl_mc_device *dev);
};

#define to_fsl_mc_driver(_drv) \
	container_of(_drv, struct fsl_mc_driver, driver)

/**
 * enum fsl_mc_pool_type - Types of allocatable MC bus resources
 *
 * Entries in these enum are used as indices in the array of resource
 * pools of an fsl_mc_bus object.
 */
enum fsl_mc_pool_type {
	FSL_MC_POOL_DPMCP = 0x0,    /* corresponds to "dpmcp" in the MC */
	FSL_MC_POOL_DPBP,	    /* corresponds to "dpbp" in the MC */
	FSL_MC_POOL_DPCON,	    /* corresponds to "dpcon" in the MC */
	FSL_MC_POOL_IRQ,

	/*
	 * NOTE: New resource pool types must be added before this entry
	 */
	FSL_MC_NUM_POOL_TYPES
};

/**
 * struct fsl_mc_resource - MC generic resource
 * @type: type of resource
 * @id: unique MC resource Id within the resources of the same type
 * @data: pointer to resource-specific data if the resource is currently
 * allocated, or NULL if the resource is not currently allocated.
 * @parent_pool: pointer to the parent resource pool from which this
 * resource is allocated from.
 * @node: Node in the free list of the corresponding resource pool
 *
 * NOTE: This structure is to be embedded as a field of specific
 * MC resource structures.
 */
struct fsl_mc_resource {
	enum fsl_mc_pool_type type;
	int32_t id;
	void *data;
	struct fsl_mc_resource_pool *parent_pool;
	struct list_head node;
};

/**
 * struct fsl_mc_device_irq - MC object device message-based interrupt
 * @msi_desc: pointer to MSI descriptor allocated by fsl_mc_msi_alloc_descs()
 * @mc_dev: MC object device that owns this interrupt
 * @dev_irq_index: device-relative IRQ index
 * @resource: MC generic resource associated with the interrupt
 */
struct fsl_mc_device_irq {
	struct msi_desc *msi_desc;
	struct fsl_mc_device *mc_dev;
	u8 dev_irq_index;
	struct fsl_mc_resource resource;
};

#define to_fsl_mc_irq(_mc_resource) \
	container_of(_mc_resource, struct fsl_mc_device_irq, resource)

/**
 * Bit masks for a MC object device (struct fsl_mc_device) flags
 */
#define FSL_MC_IS_DPRC	0x0001

/**
 * Default DMA mask for devices on a fsl-mc bus
 */
#define FSL_MC_DEFAULT_DMA_MASK	(~0ULL)

/**
 * struct fsl_mc_device - MC object device object
 * @dev: Linux driver model device object
 * @dma_mask: Default DMA mask
 * @flags: MC object device flags
 * @icid: Isolation context ID for the device
 * @mc_handle: MC handle for the corresponding MC object opened
 * @mc_io: Pointer to MC IO object assigned to this device or
 * NULL if none.
 * @obj_desc: MC description of the DPAA device
 * @regions: pointer to array of MMIO region entries
 * @irqs: pointer to array of pointers to interrupts allocated to this device
 * @resource: generic resource associated with this MC object device, if any.
 *
 * Generic device object for MC object devices that are "attached" to a
 * MC bus.
 *
 * NOTES:
 * - For a non-DPRC object its icid is the same as its parent DPRC's icid.
 * - The SMMU notifier callback gets invoked after device_add() has been
 *   called for an MC object device, but before the device-specific probe
 *   callback gets called.
 * - DP_OBJ_DPRC objects are the only MC objects that have built-in MC
 *   portals. For all other MC objects, their device drivers are responsible for
 *   allocating MC portals for them by calling fsl_mc_portal_allocate().
 * - Some types of MC objects (e.g., DP_OBJ_DPBP, DP_OBJ_DPCON) are
 *   treated as resources that can be allocated/deallocated from the
 *   corresponding resource pool in the object's parent DPRC, using the
 *   fsl_mc_object_allocate()/fsl_mc_object_free() functions. These MC objects
 *   are known as "allocatable" objects. For them, the corresponding
 *   fsl_mc_device's 'resource' points to the associated resource object.
 *   For MC objects that are not allocatable (e.g., DP_OBJ_DPRC, DP_OBJ_DPNI),
 *   'resource' is NULL.
 */
struct fsl_mc_device {
	struct device dev;
	u64 dma_mask;
	u16 flags;
	u16 icid;
	u16 mc_handle;
	struct fsl_mc_io *mc_io;
	struct dprc_obj_desc obj_desc;
	struct resource *regions;
	struct fsl_mc_device_irq **irqs;
	struct fsl_mc_resource *resource;
};

#define to_fsl_mc_device(_dev) \
	container_of(_dev, struct fsl_mc_device, dev)

#ifdef CONFIG_FSL_MC_BUS
#define dev_is_fsl_mc(_dev) ((_dev)->bus == &fsl_mc_bus_type)
#else
/* If fsl-mc bus is not present device cannot belong to fsl-mc bus */
#define dev_is_fsl_mc(_dev) (0)
#endif

/*
 * module_fsl_mc_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_fsl_mc_driver(__fsl_mc_driver) \
	module_driver(__fsl_mc_driver, fsl_mc_driver_register, \
		      fsl_mc_driver_unregister)

/*
 * Macro to avoid include chaining to get THIS_MODULE
 */
#define fsl_mc_driver_register(drv) \
	__fsl_mc_driver_register(drv, THIS_MODULE)

int __must_check __fsl_mc_driver_register(struct fsl_mc_driver *fsl_mc_driver,
					  struct module *owner);

void fsl_mc_driver_unregister(struct fsl_mc_driver *driver);

bool fsl_mc_bus_exists(void);

int __must_check fsl_mc_portal_allocate(struct fsl_mc_device *mc_dev,
					u16 mc_io_flags,
					struct fsl_mc_io **new_mc_io);

void fsl_mc_portal_free(struct fsl_mc_io *mc_io);

int fsl_mc_portal_reset(struct fsl_mc_io *mc_io);

int __must_check fsl_mc_object_allocate(struct fsl_mc_device *mc_dev,
					enum fsl_mc_pool_type pool_type,
					struct fsl_mc_device **new_mc_adev);

void fsl_mc_object_free(struct fsl_mc_device *mc_adev);

int __must_check fsl_mc_allocate_irqs(struct fsl_mc_device *mc_dev);

void fsl_mc_free_irqs(struct fsl_mc_device *mc_dev);

bool fsl_mc_is_root_dprc(struct device *dev);

extern struct bus_type fsl_mc_bus_type;

#endif /* _FSL_MC_H_ */
