/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2018, 2020-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_GPUPROPS_TYPES_H_
#define _KBASE_GPUPROPS_TYPES_H_

#include <uapi/gpu/arm/bifrost/mali_base_kernel.h>

#define KBASE_GPU_SPEED_MHZ    123
#define KBASE_GPU_PC_SIZE_LOG2 24U

struct kbase_gpuprops_regdump {
	u32 gpu_id;
	u32 l2_features;
	u32 l2_config;
	u32 l2_asn_hash[ASN_HASH_COUNT];
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
	u32 gpu_features_lo;
	u32 gpu_features_hi;
};

/**
 * struct kbase_current_config_regdump - Register dump for current resources
 *                                       allocated to the GPU.
 * @mem_features: Memory system features. Contains information about the
 *                features of the memory system. Used here to get the L2 slice
 *                count.
 * @shader_present_lo: Shader core present bitmap. Low word.
 * @shader_present_hi: Shader core present bitmap. High word.
 * @l2_present_lo: L2 cache present bitmap. Low word.
 * @l2_present_hi: L2 cache present bitmap. High word.
 *
 * Register dump structure used to store the resgisters data realated to the
 * current resources allocated to the GPU.
 */
struct kbase_current_config_regdump {
	u32 mem_features;
	u32 shader_present_lo;
	u32 shader_present_hi;
	u32 l2_present_lo;
	u32 l2_present_hi;
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

/**
 * struct max_config_props - Properties based on the maximum resources
 *                           available.
 * @l2_slices: Maximum number of L2 slices that can be assinged to the GPU
 *             during runtime.
 * @padding:   Padding to a multiple of 64 bits.
 * @core_mask: Largest core mask bitmap that can be assigned to the GPU during
 *             runtime.
 *
 * Properties based on the maximum resources available (not necessarly
 * allocated at that moment). Used to provide the maximum configuration to the
 * userspace allowing the applications to allocate enough resources in case the
 * real allocated resources change.
 */
struct max_config_props {
	u8 l2_slices;
	u8 padding[3];
	u32 core_mask;
};

/**
 * struct curr_config_props - Properties based on the current resources
 *                            allocated to the GPU.
 * @l2_present:     Current L2 present bitmap that is allocated to the GPU.
 * @shader_present: Current shader present bitmap that is allocated to the GPU.
 * @num_cores:      Current number of shader cores allocated to the GPU.
 * @l2_slices:      Current number of L2 slices allocated to the GPU.
 * @update_needed:  Defines if it is necessary to re-read the registers to
 *                  update the current allocated resources.
 * @padding:        Padding to a multiple of 64 bits.
 *
 * Properties based on the current resource available. Used for operations with
 * hardware interactions to avoid using userspace data that can be based on
 * the maximum resource available.
 */
struct curr_config_props {
	u64 l2_present;
	u64 shader_present;
	u16 num_cores;
	u8 l2_slices;
	bool update_needed;
	u8 padding[4];
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

	/* Properties based on the current resource available */
	struct curr_config_props curr_config;

	/* Properties based on the maximum resource available */
	struct max_config_props max_config;

	/* Properties shared with userspace */
	struct base_gpu_props props;

	u32 prop_buffer_size;
	void *prop_buffer;
};

#endif				/* _KBASE_GPUPROPS_TYPES_H_ */
