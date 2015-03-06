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
#include "../include/dprc.h"

#define FSL_MC_VENDOR_FREESCALE	0x1957

struct fsl_mc_device;
struct fsl_mc_io;

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
	const struct fsl_mc_device_match_id *match_id_table;
	int (*probe)(struct fsl_mc_device *dev);
	int (*remove)(struct fsl_mc_device *dev);
	void (*shutdown)(struct fsl_mc_device *dev);
	int (*suspend)(struct fsl_mc_device *dev, pm_message_t state);
	int (*resume)(struct fsl_mc_device *dev);
};

#define to_fsl_mc_driver(_drv) \
	container_of(_drv, struct fsl_mc_driver, driver)

/**
 * struct fsl_mc_device_match_id - MC object device Id entry for driver matching
 * @vendor: vendor ID
 * @obj_type: MC object type
 * @ver_major: MC object version major number
 * @ver_minor: MC object version minor number
 *
 * Type of entries in the "device Id" table for MC object devices supported by
 * a MC object device driver. The last entry of the table has vendor set to 0x0
 */
struct fsl_mc_device_match_id {
	uint16_t vendor;
	const char obj_type[16];
	uint32_t ver_major;
	uint32_t ver_minor;
};

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
 *
 * Generic device object for MC object devices that are "attached" to a
 * MC bus.
 *
 * NOTES:
 * - For a non-DPRC object its icid is the same as its parent DPRC's icid.
 * - The SMMU notifier callback gets invoked after device_add() has been
 *   called for an MC object device, but before the device-specific probe
 *   callback gets called.
 */
struct fsl_mc_device {
	struct device dev;
	uint64_t dma_mask;
	uint16_t flags;
	uint16_t icid;
	uint16_t mc_handle;
	struct fsl_mc_io *mc_io;
	struct dprc_obj_desc obj_desc;
	struct resource *regions;
};

#define to_fsl_mc_device(_dev) \
	container_of(_dev, struct fsl_mc_device, dev)

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

extern struct bus_type fsl_mc_bus_type;

#endif /* _FSL_MC_H_ */
