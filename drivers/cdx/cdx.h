/* SPDX-License-Identifier: GPL-2.0
 *
 * Header file for the CDX Bus
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef _CDX_H_
#define _CDX_H_

#include <linux/cdx/cdx_bus.h>

/**
 * struct cdx_dev_params - CDX device parameters
 * @cdx: CDX controller associated with the device
 * @parent: Associated CDX controller
 * @vendor: Vendor ID for CDX device
 * @device: Device ID for CDX device
 * @bus_num: Bus number for this CDX device
 * @dev_num: Device number for this device
 * @res: array of MMIO region entries
 * @res_count: number of valid MMIO regions
 * @req_id: Requestor ID associated with CDX device
 */
struct cdx_dev_params {
	struct cdx_controller *cdx;
	u16 vendor;
	u16 device;
	u8 bus_num;
	u8 dev_num;
	struct resource res[MAX_CDX_DEV_RESOURCES];
	u8 res_count;
	u32 req_id;
};

/**
 * cdx_register_controller - Register a CDX controller and its ports
 *		on the CDX bus.
 * @cdx: The CDX controller to register
 *
 * Return: -errno on failure, 0 on success.
 */
int cdx_register_controller(struct cdx_controller *cdx);

/**
 * cdx_unregister_controller - Unregister a CDX controller
 * @cdx: The CDX controller to unregister
 */
void cdx_unregister_controller(struct cdx_controller *cdx);

/**
 * cdx_device_add - Add a CDX device. This function adds a CDX device
 *		on the CDX bus as per the device parameters provided
 *		by caller. It also creates and registers an associated
 *		Linux generic device.
 * @dev_params: device parameters associated with the device to be created.
 *
 * Return: -errno on failure, 0 on success.
 */
int cdx_device_add(struct cdx_dev_params *dev_params);

#endif /* _CDX_H_ */
