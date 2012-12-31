/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
struct kbase_context;

/**
 * @brief Set up Kbase GPU properties.
 *
 * Set up Kbase GPU properties with information from the GPU registers
 *
 * @param kbdev 	The kbase_device structure for the device
 */
void kbase_gpuprops_set(struct kbase_device *kbdev);

/**
 * @brief Provide GPU properties to userside through UKU call.
 *
 * Fill the kbase_uk_gpuprops with values from GPU configuration registers.
 *
 * @param kctx 		The kbase_context structure
 * @param kbase_props 	A copy of the kbase_uk_gpuprops structure from userspace
 *
 * @return MALI_ERROR_NONE on success. Any other value indicates failure.
 */
mali_error  kbase_gpuprops_uk_get_props(struct kbase_context *kctx, kbase_uk_gpuprops * kbase_props);

/**
 * @brief Get the GPU configuration
 *
 * Fill the base_gpu_props structure with values from the GPU configuration registers
 *
 * @param gpu_props  The base_gpu_props structure
 * @param kbdev      The kbase_device structure for the device
 */
void kbase_gpuprops_get_props(base_gpu_props * gpu_props, struct kbase_device * kbdev);


#endif /* _KBASE_GPUPROPS_H_ */

