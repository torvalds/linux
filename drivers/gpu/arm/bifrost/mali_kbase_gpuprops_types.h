/*
 *
 * (C) COPYRIGHT 2011-2018, 2020 ARM Limited. All rights reserved.
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
 * @file mali_kbase_gpuprops_types.h
 * Base kernel property query APIs
 */

#ifndef _KBASE_GPUPROPS_TYPES_H_
#define _KBASE_GPUPROPS_TYPES_H_

#include "mali_base_kernel.h"

#define KBASE_GPU_SPEED_MHZ    123
#define KBASE_GPU_PC_SIZE_LOG2 24U

struct kbase_gpuprops_regdump {
	u32 gpu_id;
	u32 l2_features;
	u32 core_features;
	u32 tiler_features;
	u32 mem_features;
	u32 mmu_features;
	u32 as_present;
	u32 js_present;
	u32 thread_max_threads;
	u32 thread_max_workgroup_size;
	u32 thread_max_barrier_size;
	u32 thread_features;
	u32 thread_tls_alloc;
	u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];
	u32 js_features[GPU_MAX_JOB_SLOTS];
	u32 shader_present_lo;
	u32 shader_present_hi;
	u32 tiler_present_lo;
	u32 tiler_present_hi;
	u32 l2_present_lo;
	u32 l2_present_hi;
	u32 stack_present_lo;
	u32 stack_present_hi;
	u32 coherency_features;
};

struct kbase_gpu_cache_props {
	u8 associativity;
	u8 external_bus_width;
};

struct kbase_gpu_mem_props {
	u8 core_group;
};

struct kbase_gpu_mmu_props {
	u8 va_bits;
	u8 pa_bits;
};

struct kbase_gpu_props {
	/* kernel-only properties */
	u8 num_cores;
	u8 num_core_groups;
	u8 num_address_spaces;
	u8 num_job_slots;

	struct kbase_gpu_cache_props l2_props;

	struct kbase_gpu_mem_props mem;
	struct kbase_gpu_mmu_props mmu;

	/* Properties shared with userspace */
	struct base_gpu_props props;

	u32 prop_buffer_size;
	void *prop_buffer;
};

#endif				/* _KBASE_GPUPROPS_TYPES_H_ */
