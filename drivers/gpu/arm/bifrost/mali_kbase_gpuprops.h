/*
 *
 * (C) COPYRIGHT 2011-2015,2017 ARM Limited. All rights reserved.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_gpuprops.h
 * Base kernel property query APIs
 */

#ifndef _KBASE_GPUPROPS_H_
#define _KBASE_GPUPROPS_H_

#include "mali_kbase_gpuprops_types.h"

/* Forward definition - see mali_kbase.h */
struct kbase_device;

/**
 * @brief Set up Kbase GPU properties.
 *
 * Set up Kbase GPU properties with information from the GPU registers
 *
 * @param kbdev		The struct kbase_device structure for the device
 */
void kbase_gpuprops_set(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_set_features - Set up Kbase GPU properties
 * @kbdev:   Device pointer
 *
 * This function sets up GPU properties that are dependent on the hardware
 * features bitmask. This function must be preceeded by a call to
 * kbase_hw_set_features_mask().
 */
void kbase_gpuprops_set_features(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_populate_user_buffer - Populate the GPU properties buffer
 * @kbdev: The kbase device
 *
 * Fills kbdev->gpu_props->prop_buffer with the GPU properties for user
 * space to read.
 */
int kbase_gpuprops_populate_user_buffer(struct kbase_device *kbdev);

/**
 * kbase_gpuprops_update_core_props_gpu_id - break down gpu id value
 * @gpu_props: the &base_gpu_props structure
 *
 * Break down gpu_id value stored in base_gpu_props::raw_props.gpu_id into
 * separate fields (version_status, minor_revision, major_revision, product_id)
 * stored in base_gpu_props::core_props.
 */
void kbase_gpuprops_update_core_props_gpu_id(base_gpu_props * const gpu_props);


#endif				/* _KBASE_GPUPROPS_H_ */
