/* SPDX-License-Identifier: GPL-2.0
 *
 * Header file for MCDI FW interaction for CDX bus.
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef CDX_MCDI_FUNCTIONS_H
#define CDX_MCDI_FUNCTIONS_H

#include <linux/cdx/mcdi.h>
#include "mcdid.h"
#include "../cdx.h"

/**
 * cdx_mcdi_get_num_buses - Get the total number of buses on
 *	the controller.
 * @cdx: pointer to MCDI interface.
 *
 * Return: total number of buses available on the controller,
 *	<0 on failure
 */
int cdx_mcdi_get_num_buses(struct cdx_mcdi *cdx);

/**
 * cdx_mcdi_get_num_devs - Get the total number of devices on
 *	a particular bus of the controller.
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 *
 * Return: total number of devices available on the bus, <0 on failure
 */
int cdx_mcdi_get_num_devs(struct cdx_mcdi *cdx, int bus_num);

/**
 * cdx_mcdi_get_dev_config - Get configuration for a particular
 *	bus_num:dev_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 * @dev_num: Device number.
 * @dev_params: Pointer to cdx_dev_params, this is populated by this
 *	device with the configuration corresponding to the provided
 *	bus_num:dev_num.
 *
 * Return: 0 total number of devices available on the bus, <0 on failure
 */
int cdx_mcdi_get_dev_config(struct cdx_mcdi *cdx,
			    u8 bus_num, u8 dev_num,
			    struct cdx_dev_params *dev_params);

/**
 * cdx_mcdi_bus_enable - Enable CDX bus represented by bus_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_bus_enable(struct cdx_mcdi *cdx, u8 bus_num);

/**
 * cdx_mcdi_bus_disable - Disable CDX bus represented by bus_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_bus_disable(struct cdx_mcdi *cdx, u8 bus_num);

/**
 * cdx_mcdi_write_msi - Write MSI configuration for CDX device
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 * @dev_num: Device number.
 * @msi_vector: Device-relative MSI vector number.
 *	Must be < MSI_COUNT reported for the device.
 * @msi_address: MSI address to be used by the hardware. Typically, on ARM
 *	systems this address is translated by the IOMMU (if enabled) and
 *	it is the responsibility of the entity managing the IOMMU (APU kernel)
 *	to supply the correct IOVA here.
 * @msi_data: MSI data to be used by the hardware. On versal-net, only the
 *	lower 16-bits are used, the remaining bits are ignored and should be
 *	set to zero.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_write_msi(struct cdx_mcdi *cdx, u8 bus_num, u8 dev_num,
		       u32 msi_vector, u64 msi_address, u32 msi_data);

/**
 * cdx_mcdi_reset_device - Reset cdx device represented by bus_num:dev_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 * @dev_num: Device number.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_reset_device(struct cdx_mcdi *cdx,
			  u8 bus_num, u8 dev_num);

/**
 * cdx_mcdi_bus_master_enable - Set/Reset bus mastering for cdx device
 *				represented by bus_num:dev_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 * @dev_num: Device number.
 * @enable: Enable bus mastering if set, disable otherwise.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_bus_master_enable(struct cdx_mcdi *cdx, u8 bus_num,
			       u8 dev_num, bool enable);

/**
 * cdx_mcdi_msi_enable - Enable/Disable MSIs for cdx device represented
 *			 by bus_num:dev_num
 * @cdx: pointer to MCDI interface.
 * @bus_num: Bus number.
 * @dev_num: Device number.
 * @enable: Enable msi's if set, disable otherwise.
 *
 * Return: 0 on success, <0 on failure
 */
int cdx_mcdi_msi_enable(struct cdx_mcdi *cdx, u8 bus_num,
			u8 dev_num, bool enable);

#endif /* CDX_MCDI_FUNCTIONS_H */
