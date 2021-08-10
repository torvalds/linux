/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2015, 2017, 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/**
 * DOC: Base kernel property query APIs
 */

#ifndef _KBASE_GPUPROPS_H_
#define _KBASE_GPUPROPS_H_

#include "mali_kbase_gpuprops_types.h"

/* Forward definition - see mali_kbase.h */
struct kbase_device;

/**
 * KBASE_UBFX32 - Extracts bits from a 32-bit bitfield.
 * @value:  The value from which to extract bits.
 * @offset: The first bit to extract (0 being the LSB).
 * @size:   The number of bits to extract.
 *
 * Context: @offset + @size <= 32.
 *
 * Return: Bits [@offset, @offset + @size) from @value.
 */
/* from mali_cdsb.h */
#define KBASE_UBFX32(value, offset, size) \
	(((u32)(value) >> (u32)(offset)) & (u32)((1ULL << (u32)(size)) - 1))

/**
 * kbase_gpuprops_set - Set up Kbase GPU properties.
 * @kbdev: The struct kbase_device structure for the device
 *
 * Set up Kbase GPU properties with information from the GPU registers
 */
void kbase_gpuprops_set(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_set_features - Set up Kbase GPU properties
 * @kbdev:   Device pointer
 *
 * This function sets up GPU properties that are dependent on the hardware
 * features bitmask. This function must be preceeded by a call to
 * kbase_hw_set_features_mask().
 *
 * Return: Zero on success, Linux error code on failure
 */
int kbase_gpuprops_set_features(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_update_l2_features - Update GPU property of L2_FEATURES
 * @kbdev:   Device pointer
 *
 * This function updates l2_features and the log2 cache size.
 * The function expects GPU to be powered up and value of pm.active_count
 * to be 1.
 *
 * Return: Zero on success, Linux error code for failure
 */
int kbase_gpuprops_update_l2_features(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_populate_user_buffer - Populate the GPU properties buffer
 * @kbdev: The kbase device
 *
 * Fills prop_buffer with the GPU properties for user space to read.
 */
int kbase_gpuprops_populate_user_buffer(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_free_user_buffer - Free the GPU properties buffer.
 * @kbdev: kbase device pointer
 *
 * Free the GPU properties buffer allocated from
 * kbase_gpuprops_populate_user_buffer.
 */
void kbase_gpuprops_free_user_buffer(struct kbase_device *kbdev);

/**
 * kbase_device_populate_max_freq - Populate max gpu frequency.
 * @kbdev: kbase device pointer
 *
 * Populate the maximum gpu frequency to be used when devfreq is disabled.
 *
 * Return: 0 on success and non-zero value on failure.
 */
int kbase_device_populate_max_freq(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_update_core_props_gpu_id - break down gpu id value
 * @gpu_props: the &base_gpu_props structure
 *
 * Break down gpu_id value stored in base_gpu_props::raw_props.gpu_id into
 * separate fields (version_status, minor_revision, major_revision, product_id)
 * stored in base_gpu_props::core_props.
 */
void kbase_gpuprops_update_core_props_gpu_id(
	struct base_gpu_props * const gpu_props);

/**
 * kbase_gpuprops_set_max_config - Set the max config information
 * @kbdev:       Device pointer
 * @max_config:  Maximum configuration data to be updated
 *
 * This function sets max_config in the kbase_gpu_props.
 */
void kbase_gpuprops_set_max_config(struct kbase_device *kbdev,
	const struct max_config_props *max_config);

/**
 * kbase_gpuprops_get_curr_config_props - Get the current allocated resources
 * @kbdev: The &struct kbase_device structure for the device
 * @curr_config: The &struct curr_config_props structure to receive the result
 *
 * Fill the &struct curr_config_props structure with values from the GPU
 * configuration registers.
 *
 * Return: Zero on success, Linux error code on failure
 */
int kbase_gpuprops_get_curr_config_props(struct kbase_device *kbdev,
	struct curr_config_props * const curr_config);

/**
 * kbase_gpuprops_req_curr_config_update - Request Current Config Update
 * @kbdev: The &struct kbase_device structure for the device
 *
 * Requests the current configuration to be updated next time the
 * kbase_gpuprops_get_curr_config_props() is called.
 *
 * Return: Zero on success, Linux error code on failure
 */
int kbase_gpuprops_req_curr_config_update(struct kbase_device *kbdev);

#endif				/* _KBASE_GPUPROPS_H_ */
