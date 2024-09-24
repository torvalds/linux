/*
 *
 * (C) COPYRIGHT 2021-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _ETHOSN_SMC_H_
#define _ETHOSN_SMC_H_

#include "ethosn_device.h"

#include <linux/arm-smccc.h>
#include <linux/types.h>

/**
 * struct ethosn_smc_aux_config - NPU auxiliary control config for SiP service
 */
struct ethosn_smc_aux_config {
	union {
		uint32_t word;
		struct {
			uint32_t level_irq : 1; /* Change to level rising IRQ */
			uint32_t stashing : 1;  /* Enable stashing */
			uint32_t reserved : 30;
		} bits;
	};
};

/**
 * ethosn_smc_version_check() - Check SiP service version compatibility
 * @device:	Pointer to the struct device on which to log the error if any.
 *
 * Checks that the Arm Ethos-N NPU SiP service is available and that it is
 * running a compatible version.
 *
 * Return: 0 on success, else error code.
 */
int ethosn_smc_version_check(const struct device *dev);

/**
 * ethosn_smc_is_secure() - Call SiP service to get the NPU's secure status
 * @device:	Pointer to the struct device on which to log the error if any.
 * @core_addr:	Address to Ethos-N core.
 *
 * Return: 0 if unsecure, 1 if secure or negative error code on failure.
 */
int ethosn_smc_is_secure(const struct device *dev,
			 phys_addr_t core_addr);

/**
 * ethosn_smc_core_reset() - Call SiP service to reset a NPU core
 * @device:	Pointer to the struct device on which to log the error if any.
 * @core_addr:		Address to Ethos-N core.
 * @asset_alloc_idx:	Index of the asset allocator to use.
 * @halt:		Indicates if a halt reset should be performed.
 * @hard_reset:		Indicates if a hard or soft reset should be performed.
 * @is_protected:	Indicates if protected inferences will be used.
 * @aux_config:		Auxiliary control configuration.
 *
 * Return: 0 on success, else error code.
 */
int ethosn_smc_core_reset(const struct device *dev,
			  phys_addr_t core_addr,
			  uint32_t asset_alloc_idx,
			  bool halt,
			  bool hard_reset,
			  bool is_protected,
			  const struct ethosn_smc_aux_config *aux_config);

/**
 * ethosn_smc_core_is_sleeping() - Call SiP service to check if the NPU core is
 * sleeping
 * @device:	Pointer to the struct device on which to log the error if any.
 * @core_addr:	Address to Ethos-N core.
 *
 * Return: 0 if active, 1 if sleeping or negative error code on failure.
 */
int ethosn_smc_core_is_sleeping(const struct device *dev,
				phys_addr_t core_addr);

#ifdef ETHOSN_TZMP1

/**
 * ethosn_smc_get_firmware_version() - Call SiP service to get the NPU firmware
 *                                     version.
 * @device:	Pointer to the struct device on which to log any errors.
 * @out_major:	Location to put the firmware's major version.
 * @out_minor:	Location to put the firmware's minor version.
 * @out_patch:	Location to put the firmware's patch version.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ethosn_smc_get_firmware_version(const struct device *dev,
				    uint32_t *out_major,
				    uint32_t *out_minor,
				    uint32_t *out_patch);

/**
 * ethosn_smc_get_firmware_mem_info() - Call SiP service to get the NPU firmware
 *                                      memory info.
 * @device:	Pointer to the struct device on which to log any errors.
 * @out_addr:	Location to put the firmware's address.
 * @out_size:	Location to put the firmware's size.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ethosn_smc_get_firmware_mem_info(const struct device *dev,
				     phys_addr_t *out_addr,
				     size_t *out_size);

/**
 * ethosn_smc_get_firmware_offsets() - Call SiP service to get the NPU firmware
 *                                     offsets.
 * @device:		Pointer to the struct device on which to log any errors.
 * @out_ple_offset:	Location to put the firmware's PLE offset.
 * @out_stack_offset:	Location to put the firmware's stack offset.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ethosn_smc_get_firmware_offsets(const struct device *dev,
				    uint32_t *out_ple_offset,
				    uint32_t *out_stack_offset);

/**
 * ethosn_smc_get_firmware_va_map() - Call SiP service to get virtual base
 *                                    address map for the NPU firmware.
 * @device:	Pointer to the struct device on which to log any errors.
 * @out_firmware_va:		Location to put firmware base address.
 * @out_working_data_va:	Location to put working data base address.
 * @out_command_stream_va:	Location to put command stream base address.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ethosn_smc_get_firmware_va_map(const struct device *dev,
				   dma_addr_t *out_firmware_va,
				   dma_addr_t *out_working_data_va,
				   dma_addr_t *out_command_stream_va);

/**
 * ethosn_smc_core_boot_firmware() - Call SiP service to boot firmware on a NPU
 *                                   core
 * @device:	Pointer to the struct device on which to log any errors.
 * @core_addr:	Address to Ethos-N core.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ethosn_smc_core_boot_firmware(const struct device *dev,
				  phys_addr_t core_addr);

#endif

#endif /* _ETHOSN_SMC_H_ */
