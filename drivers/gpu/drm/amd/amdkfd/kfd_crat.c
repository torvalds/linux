/*
 * Copyright 2015-2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include "kfd_crat.h"
#include "kfd_priv.h"
#include "kfd_topology.h"
#include "kfd_iommu.h"
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"

/* GPU Processor ID base for dGPUs for which VCRAT needs to be created.
 * GPU processor ID are expressed with Bit[31]=1.
 * The base is set to 0x8000_0000 + 0x1000 to avoid collision with GPU IDs
 * used in the CRAT.
 */
static uint32_t gpu_processor_id_low = 0x80001000;

/* Return the next available gpu_processor_id and increment it for next GPU
 *	@total_cu_count - Total CUs present in the GPU including ones
 *			  masked off
 */
static inline unsigned int get_and_inc_gpu_processor_id(
				unsigned int total_cu_count)
{
	int current_id = gpu_processor_id_low;

	gpu_processor_id_low += total_cu_count;
	return current_id;
}

/* Static table to describe GPU Cache information */
struct kfd_gpu_cache_info {
	uint32_t	cache_size;
	uint32_t	cache_level;
	uint32_t	flags;
	/* Indicates how many Compute Units share this cache
	 * within a SA. Value = 1 indicates the cache is not shared
	 */
	uint32_t	num_cu_shared;
};

static struct kfd_gpu_cache_info kaveri_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache (in SQC module) per bank */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache (in SQC module) per bank */
		.cache_size = 8,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},

	/* TODO: Add L2 Cache information */
};


static struct kfd_gpu_cache_info carrizo_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache (in SQC module) per bank */
		.cache_size = 8,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 4,
	},
	{
		/* Scalar L1 Data Cache (in SQC module) per bank. */
		.cache_size = 4,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 4,
	},

	/* TODO: Add L2 Cache information */
};

#define hawaii_cache_info kaveri_cache_info
#define tonga_cache_info carrizo_cache_info
#define fiji_cache_info  carrizo_cache_info
#define polaris10_cache_info carrizo_cache_info
#define polaris11_cache_info carrizo_cache_info
#define polaris12_cache_info carrizo_cache_info
#define vegam_cache_info carrizo_cache_info

/* NOTE: L1 cache information has been updated and L2/L3
 * cache information has been added for Vega10 and
 * newer ASICs. The unit for cache_size is KiB.
 * In future,  check & update cache details
 * for every new ASIC is required.
 */

static struct kfd_gpu_cache_info vega10_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 4096,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 16,
	},
};

static struct kfd_gpu_cache_info raven_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 1024,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 11,
	},
};

static struct kfd_gpu_cache_info renoir_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 1024,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
};

static struct kfd_gpu_cache_info vega12_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 2048,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 5,
	},
};

static struct kfd_gpu_cache_info vega20_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 3,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 8192,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 16,
	},
};

static struct kfd_gpu_cache_info aldebaran_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 8192,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 14,
	},
};

static struct kfd_gpu_cache_info navi10_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 4096,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
};

static struct kfd_gpu_cache_info vangogh_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 1024,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
};

static struct kfd_gpu_cache_info navi14_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 12,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 2048,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 12,
	},
};

static struct kfd_gpu_cache_info sienna_cichlid_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 4096,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
	{
		/* L3 Data Cache per GPU */
		.cache_size = 128*1024,
		.cache_level = 3,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
};

static struct kfd_gpu_cache_info navy_flounder_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 3072,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
	{
		/* L3 Data Cache per GPU */
		.cache_size = 96*1024,
		.cache_level = 3,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 10,
	},
};

static struct kfd_gpu_cache_info dimgrey_cavefish_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 2048,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
	{
		/* L3 Data Cache per GPU */
		.cache_size = 32*1024,
		.cache_level = 3,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
};

static struct kfd_gpu_cache_info beige_goby_cache_info[] = {
	{
		/* TCP L1 Cache per CU */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 1,
	},
	{
		/* Scalar L1 Instruction Cache per SQC */
		.cache_size = 32,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_INST_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* Scalar L1 Data Cache per SQC */
		.cache_size = 16,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 2,
	},
	{
		/* GL1 Data Cache per SA */
		.cache_size = 128,
		.cache_level = 1,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
	{
		/* L2 Data Cache per GPU (Total Tex Cache) */
		.cache_size = 1024,
		.cache_level = 2,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
	{
		/* L3 Data Cache per GPU */
		.cache_size = 16*1024,
		.cache_level = 3,
		.flags = (CRAT_CACHE_FLAGS_ENABLED |
				CRAT_CACHE_FLAGS_DATA_CACHE |
				CRAT_CACHE_FLAGS_SIMD_CACHE),
		.num_cu_shared = 8,
	},
};

static void kfd_populated_cu_info_cpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	dev->node_props.cpu_cores_count = cu->num_cpu_cores;
	dev->node_props.cpu_core_id_base = cu->processor_id_low;
	if (cu->hsa_capability & CRAT_CU_FLAGS_IOMMU_PRESENT)
		dev->node_props.capability |= HSA_CAP_ATS_PRESENT;

	pr_debug("CU CPU: cores=%d id_base=%d\n", cu->num_cpu_cores,
			cu->processor_id_low);
}

static void kfd_populated_cu_info_gpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	dev->node_props.simd_id_base = cu->processor_id_low;
	dev->node_props.simd_count = cu->num_simd_cores;
	dev->node_props.lds_size_in_kb = cu->lds_size_in_kb;
	dev->node_props.max_waves_per_simd = cu->max_waves_simd;
	dev->node_props.wave_front_size = cu->wave_front_size;
	dev->node_props.array_count = cu->array_count;
	dev->node_props.cu_per_simd_array = cu->num_cu_per_array;
	dev->node_props.simd_per_cu = cu->num_simd_per_cu;
	dev->node_props.max_slots_scratch_cu = cu->max_slots_scatch_cu;
	if (cu->hsa_capability & CRAT_CU_FLAGS_HOT_PLUGGABLE)
		dev->node_props.capability |= HSA_CAP_HOT_PLUGGABLE;
	pr_debug("CU GPU: id_base=%d\n", cu->processor_id_low);
}

/* kfd_parse_subtype_cu - parse compute unit subtypes and attach it to correct
 * topology device present in the device_list
 */
static int kfd_parse_subtype_cu(struct crat_subtype_computeunit *cu,
				struct list_head *device_list)
{
	struct kfd_topology_device *dev;

	pr_debug("Found CU entry in CRAT table with proximity_domain=%d caps=%x\n",
			cu->proximity_domain, cu->hsa_capability);
	list_for_each_entry(dev, device_list, list) {
		if (cu->proximity_domain == dev->proximity_domain) {
			if (cu->flags & CRAT_CU_FLAGS_CPU_PRESENT)
				kfd_populated_cu_info_cpu(dev, cu);

			if (cu->flags & CRAT_CU_FLAGS_GPU_PRESENT)
				kfd_populated_cu_info_gpu(dev, cu);
			break;
		}
	}

	return 0;
}

static struct kfd_mem_properties *
find_subtype_mem(uint32_t heap_type, uint32_t flags, uint32_t width,
		struct kfd_topology_device *dev)
{
	struct kfd_mem_properties *props;

	list_for_each_entry(props, &dev->mem_props, list) {
		if (props->heap_type == heap_type
				&& props->flags == flags
				&& props->width == width)
			return props;
	}

	return NULL;
}
/* kfd_parse_subtype_mem - parse memory subtypes and attach it to correct
 * topology device present in the device_list
 */
static int kfd_parse_subtype_mem(struct crat_subtype_memory *mem,
				struct list_head *device_list)
{
	struct kfd_mem_properties *props;
	struct kfd_topology_device *dev;
	uint32_t heap_type;
	uint64_t size_in_bytes;
	uint32_t flags = 0;
	uint32_t width;

	pr_debug("Found memory entry in CRAT table with proximity_domain=%d\n",
			mem->proximity_domain);
	list_for_each_entry(dev, device_list, list) {
		if (mem->proximity_domain == dev->proximity_domain) {
			/* We're on GPU node */
			if (dev->node_props.cpu_cores_count == 0) {
				/* APU */
				if (mem->visibility_type == 0)
					heap_type =
						HSA_MEM_HEAP_TYPE_FB_PRIVATE;
				/* dGPU */
				else
					heap_type = mem->visibility_type;
			} else
				heap_type = HSA_MEM_HEAP_TYPE_SYSTEM;

			if (mem->flags & CRAT_MEM_FLAGS_HOT_PLUGGABLE)
				flags |= HSA_MEM_FLAGS_HOT_PLUGGABLE;
			if (mem->flags & CRAT_MEM_FLAGS_NON_VOLATILE)
				flags |= HSA_MEM_FLAGS_NON_VOLATILE;

			size_in_bytes =
				((uint64_t)mem->length_high << 32) +
							mem->length_low;
			width = mem->width;

			/* Multiple banks of the same type are aggregated into
			 * one. User mode doesn't care about multiple physical
			 * memory segments. It's managed as a single virtual
			 * heap for user mode.
			 */
			props = find_subtype_mem(heap_type, flags, width, dev);
			if (props) {
				props->size_in_bytes += size_in_bytes;
				break;
			}

			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			props->heap_type = heap_type;
			props->flags = flags;
			props->size_in_bytes = size_in_bytes;
			props->width = width;

			dev->node_props.mem_banks_count++;
			list_add_tail(&props->list, &dev->mem_props);

			break;
		}
	}

	return 0;
}

/* kfd_parse_subtype_cache - parse cache subtypes and attach it to correct
 * topology device present in the device_list
 */
static int kfd_parse_subtype_cache(struct crat_subtype_cache *cache,
			struct list_head *device_list)
{
	struct kfd_cache_properties *props;
	struct kfd_topology_device *dev;
	uint32_t id;
	uint32_t total_num_of_cu;

	id = cache->processor_id_low;

	pr_debug("Found cache entry in CRAT table with processor_id=%d\n", id);
	list_for_each_entry(dev, device_list, list) {
		total_num_of_cu = (dev->node_props.array_count *
					dev->node_props.cu_per_simd_array);

		/* Cache infomration in CRAT doesn't have proximity_domain
		 * information as it is associated with a CPU core or GPU
		 * Compute Unit. So map the cache using CPU core Id or SIMD
		 * (GPU) ID.
		 * TODO: This works because currently we can safely assume that
		 *  Compute Units are parsed before caches are parsed. In
		 *  future, remove this dependency
		 */
		if ((id >= dev->node_props.cpu_core_id_base &&
			id <= dev->node_props.cpu_core_id_base +
				dev->node_props.cpu_cores_count) ||
			(id >= dev->node_props.simd_id_base &&
			id < dev->node_props.simd_id_base +
				total_num_of_cu)) {
			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			props->processor_id_low = id;
			props->cache_level = cache->cache_level;
			props->cache_size = cache->cache_size;
			props->cacheline_size = cache->cache_line_size;
			props->cachelines_per_tag = cache->lines_per_tag;
			props->cache_assoc = cache->associativity;
			props->cache_latency = cache->cache_latency;
			memcpy(props->sibling_map, cache->sibling_map,
					sizeof(props->sibling_map));

			if (cache->flags & CRAT_CACHE_FLAGS_DATA_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_DATA;
			if (cache->flags & CRAT_CACHE_FLAGS_INST_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_INSTRUCTION;
			if (cache->flags & CRAT_CACHE_FLAGS_CPU_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_CPU;
			if (cache->flags & CRAT_CACHE_FLAGS_SIMD_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_HSACU;

			dev->cache_count++;
			dev->node_props.caches_count++;
			list_add_tail(&props->list, &dev->cache_props);

			break;
		}
	}

	return 0;
}

/* kfd_parse_subtype_iolink - parse iolink subtypes and attach it to correct
 * topology device present in the device_list
 */
static int kfd_parse_subtype_iolink(struct crat_subtype_iolink *iolink,
					struct list_head *device_list)
{
	struct kfd_iolink_properties *props = NULL, *props2;
	struct kfd_topology_device *dev, *to_dev;
	uint32_t id_from;
	uint32_t id_to;

	id_from = iolink->proximity_domain_from;
	id_to = iolink->proximity_domain_to;

	pr_debug("Found IO link entry in CRAT table with id_from=%d, id_to %d\n",
			id_from, id_to);
	list_for_each_entry(dev, device_list, list) {
		if (id_from == dev->proximity_domain) {
			props = kfd_alloc_struct(props);
			if (!props)
				return -ENOMEM;

			props->node_from = id_from;
			props->node_to = id_to;
			props->ver_maj = iolink->version_major;
			props->ver_min = iolink->version_minor;
			props->iolink_type = iolink->io_interface_type;

			if (props->iolink_type == CRAT_IOLINK_TYPE_PCIEXPRESS)
				props->weight = 20;
			else if (props->iolink_type == CRAT_IOLINK_TYPE_XGMI)
				props->weight = 15 * iolink->num_hops_xgmi;
			else
				props->weight = node_distance(id_from, id_to);

			props->min_latency = iolink->minimum_latency;
			props->max_latency = iolink->maximum_latency;
			props->min_bandwidth = iolink->minimum_bandwidth_mbs;
			props->max_bandwidth = iolink->maximum_bandwidth_mbs;
			props->rec_transfer_size =
					iolink->recommended_transfer_size;

			dev->io_link_count++;
			dev->node_props.io_links_count++;
			list_add_tail(&props->list, &dev->io_link_props);
			break;
		}
	}

	/* CPU topology is created before GPUs are detected, so CPU->GPU
	 * links are not built at that time. If a PCIe type is discovered, it
	 * means a GPU is detected and we are adding GPU->CPU to the topology.
	 * At this time, also add the corresponded CPU->GPU link if GPU
	 * is large bar.
	 * For xGMI, we only added the link with one direction in the crat
	 * table, add corresponded reversed direction link now.
	 */
	if (props && (iolink->flags & CRAT_IOLINK_FLAGS_BI_DIRECTIONAL)) {
		to_dev = kfd_topology_device_by_proximity_domain(id_to);
		if (!to_dev)
			return -ENODEV;
		/* same everything but the other direction */
		props2 = kmemdup(props, sizeof(*props2), GFP_KERNEL);
		props2->node_from = id_to;
		props2->node_to = id_from;
		props2->kobj = NULL;
		to_dev->io_link_count++;
		to_dev->node_props.io_links_count++;
		list_add_tail(&props2->list, &to_dev->io_link_props);
	}

	return 0;
}

/* kfd_parse_subtype - parse subtypes and attach it to correct topology device
 * present in the device_list
 *	@sub_type_hdr - subtype section of crat_image
 *	@device_list - list of topology devices present in this crat_image
 */
static int kfd_parse_subtype(struct crat_subtype_generic *sub_type_hdr,
				struct list_head *device_list)
{
	struct crat_subtype_computeunit *cu;
	struct crat_subtype_memory *mem;
	struct crat_subtype_cache *cache;
	struct crat_subtype_iolink *iolink;
	int ret = 0;

	switch (sub_type_hdr->type) {
	case CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY:
		cu = (struct crat_subtype_computeunit *)sub_type_hdr;
		ret = kfd_parse_subtype_cu(cu, device_list);
		break;
	case CRAT_SUBTYPE_MEMORY_AFFINITY:
		mem = (struct crat_subtype_memory *)sub_type_hdr;
		ret = kfd_parse_subtype_mem(mem, device_list);
		break;
	case CRAT_SUBTYPE_CACHE_AFFINITY:
		cache = (struct crat_subtype_cache *)sub_type_hdr;
		ret = kfd_parse_subtype_cache(cache, device_list);
		break;
	case CRAT_SUBTYPE_TLB_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_debug("Found TLB entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_CCOMPUTE_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_debug("Found CCOMPUTE entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_IOLINK_AFFINITY:
		iolink = (struct crat_subtype_iolink *)sub_type_hdr;
		ret = kfd_parse_subtype_iolink(iolink, device_list);
		break;
	default:
		pr_warn("Unknown subtype %d in CRAT\n",
				sub_type_hdr->type);
	}

	return ret;
}

/* kfd_parse_crat_table - parse CRAT table. For each node present in CRAT
 * create a kfd_topology_device and add in to device_list. Also parse
 * CRAT subtypes and attach it to appropriate kfd_topology_device
 *	@crat_image - input image containing CRAT
 *	@device_list - [OUT] list of kfd_topology_device generated after
 *		       parsing crat_image
 *	@proximity_domain - Proximity domain of the first device in the table
 *
 *	Return - 0 if successful else -ve value
 */
int kfd_parse_crat_table(void *crat_image, struct list_head *device_list,
			 uint32_t proximity_domain)
{
	struct kfd_topology_device *top_dev = NULL;
	struct crat_subtype_generic *sub_type_hdr;
	uint16_t node_id;
	int ret = 0;
	struct crat_header *crat_table = (struct crat_header *)crat_image;
	uint16_t num_nodes;
	uint32_t image_len;

	if (!crat_image)
		return -EINVAL;

	if (!list_empty(device_list)) {
		pr_warn("Error device list should be empty\n");
		return -EINVAL;
	}

	num_nodes = crat_table->num_domains;
	image_len = crat_table->length;

	pr_debug("Parsing CRAT table with %d nodes\n", num_nodes);

	for (node_id = 0; node_id < num_nodes; node_id++) {
		top_dev = kfd_create_topology_device(device_list);
		if (!top_dev)
			break;
		top_dev->proximity_domain = proximity_domain++;
	}

	if (!top_dev) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy(top_dev->oem_id, crat_table->oem_id, CRAT_OEMID_LENGTH);
	memcpy(top_dev->oem_table_id, crat_table->oem_table_id,
			CRAT_OEMTABLEID_LENGTH);
	top_dev->oem_revision = crat_table->oem_revision;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table+1);
	while ((char *)sub_type_hdr + sizeof(struct crat_subtype_generic) <
			((char *)crat_image) + image_len) {
		if (sub_type_hdr->flags & CRAT_SUBTYPE_FLAGS_ENABLED) {
			ret = kfd_parse_subtype(sub_type_hdr, device_list);
			if (ret)
				break;
		}

		sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
				sub_type_hdr->length);
	}

err:
	if (ret)
		kfd_release_topology_device_list(device_list);

	return ret;
}

/* Helper function. See kfd_fill_gpu_cache_info for parameter description */
static int fill_in_l1_pcache(struct crat_subtype_cache *pcache,
				struct kfd_gpu_cache_info *pcache_info,
				struct kfd_cu_info *cu_info,
				int mem_available,
				int cu_bitmask,
				int cache_type, unsigned int cu_processor_id,
				int cu_block)
{
	unsigned int cu_sibling_map_mask;
	int first_active_cu;

	/* First check if enough memory is available */
	if (sizeof(struct crat_subtype_cache) > mem_available)
		return -ENOMEM;

	cu_sibling_map_mask = cu_bitmask;
	cu_sibling_map_mask >>= cu_block;
	cu_sibling_map_mask &=
		((1 << pcache_info[cache_type].num_cu_shared) - 1);
	first_active_cu = ffs(cu_sibling_map_mask);

	/* CU could be inactive. In case of shared cache find the first active
	 * CU. and incase of non-shared cache check if the CU is inactive. If
	 * inactive active skip it
	 */
	if (first_active_cu) {
		memset(pcache, 0, sizeof(struct crat_subtype_cache));
		pcache->type = CRAT_SUBTYPE_CACHE_AFFINITY;
		pcache->length = sizeof(struct crat_subtype_cache);
		pcache->flags = pcache_info[cache_type].flags;
		pcache->processor_id_low = cu_processor_id
					 + (first_active_cu - 1);
		pcache->cache_level = pcache_info[cache_type].cache_level;
		pcache->cache_size = pcache_info[cache_type].cache_size;

		/* Sibling map is w.r.t processor_id_low, so shift out
		 * inactive CU
		 */
		cu_sibling_map_mask =
			cu_sibling_map_mask >> (first_active_cu - 1);

		pcache->sibling_map[0] = (uint8_t)(cu_sibling_map_mask & 0xFF);
		pcache->sibling_map[1] =
				(uint8_t)((cu_sibling_map_mask >> 8) & 0xFF);
		pcache->sibling_map[2] =
				(uint8_t)((cu_sibling_map_mask >> 16) & 0xFF);
		pcache->sibling_map[3] =
				(uint8_t)((cu_sibling_map_mask >> 24) & 0xFF);
		return 0;
	}
	return 1;
}

/* Helper function. See kfd_fill_gpu_cache_info for parameter description */
static int fill_in_l2_l3_pcache(struct crat_subtype_cache *pcache,
				struct kfd_gpu_cache_info *pcache_info,
				struct kfd_cu_info *cu_info,
				int mem_available,
				int cache_type, unsigned int cu_processor_id)
{
	unsigned int cu_sibling_map_mask;
	int first_active_cu;
	int i, j, k;

	/* First check if enough memory is available */
	if (sizeof(struct crat_subtype_cache) > mem_available)
		return -ENOMEM;

	cu_sibling_map_mask = cu_info->cu_bitmap[0][0];
	cu_sibling_map_mask &=
		((1 << pcache_info[cache_type].num_cu_shared) - 1);
	first_active_cu = ffs(cu_sibling_map_mask);

	/* CU could be inactive. In case of shared cache find the first active
	 * CU. and incase of non-shared cache check if the CU is inactive. If
	 * inactive active skip it
	 */
	if (first_active_cu) {
		memset(pcache, 0, sizeof(struct crat_subtype_cache));
		pcache->type = CRAT_SUBTYPE_CACHE_AFFINITY;
		pcache->length = sizeof(struct crat_subtype_cache);
		pcache->flags = pcache_info[cache_type].flags;
		pcache->processor_id_low = cu_processor_id
					 + (first_active_cu - 1);
		pcache->cache_level = pcache_info[cache_type].cache_level;
		pcache->cache_size = pcache_info[cache_type].cache_size;

		/* Sibling map is w.r.t processor_id_low, so shift out
		 * inactive CU
		 */
		cu_sibling_map_mask =
			cu_sibling_map_mask >> (first_active_cu - 1);
		k = 0;
		for (i = 0; i < cu_info->num_shader_engines; i++) {
			for (j = 0; j < cu_info->num_shader_arrays_per_engine;
				j++) {
				pcache->sibling_map[k] =
				 (uint8_t)(cu_sibling_map_mask & 0xFF);
				pcache->sibling_map[k+1] =
				 (uint8_t)((cu_sibling_map_mask >> 8) & 0xFF);
				pcache->sibling_map[k+2] =
				 (uint8_t)((cu_sibling_map_mask >> 16) & 0xFF);
				pcache->sibling_map[k+3] =
				 (uint8_t)((cu_sibling_map_mask >> 24) & 0xFF);
				k += 4;
				cu_sibling_map_mask =
					cu_info->cu_bitmap[i % 4][j + i / 4];
				cu_sibling_map_mask &= (
				 (1 << pcache_info[cache_type].num_cu_shared)
				 - 1);
			}
		}
		return 0;
	}
	return 1;
}

/* kfd_fill_gpu_cache_info - Fill GPU cache info using kfd_gpu_cache_info
 * tables
 *
 *	@kdev - [IN] GPU device
 *	@gpu_processor_id - [IN] GPU processor ID to which these caches
 *			    associate
 *	@available_size - [IN] Amount of memory available in pcache
 *	@cu_info - [IN] Compute Unit info obtained from KGD
 *	@pcache - [OUT] memory into which cache data is to be filled in.
 *	@size_filled - [OUT] amount of data used up in pcache.
 *	@num_of_entries - [OUT] number of caches added
 */
static int kfd_fill_gpu_cache_info(struct kfd_dev *kdev,
			int gpu_processor_id,
			int available_size,
			struct kfd_cu_info *cu_info,
			struct crat_subtype_cache *pcache,
			int *size_filled,
			int *num_of_entries)
{
	struct kfd_gpu_cache_info *pcache_info;
	int num_of_cache_types = 0;
	int i, j, k;
	int ct = 0;
	int mem_available = available_size;
	unsigned int cu_processor_id;
	int ret;
	unsigned int num_cu_shared;

	switch (kdev->device_info->asic_family) {
	case CHIP_KAVERI:
		pcache_info = kaveri_cache_info;
		num_of_cache_types = ARRAY_SIZE(kaveri_cache_info);
		break;
	case CHIP_HAWAII:
		pcache_info = hawaii_cache_info;
		num_of_cache_types = ARRAY_SIZE(hawaii_cache_info);
		break;
	case CHIP_CARRIZO:
		pcache_info = carrizo_cache_info;
		num_of_cache_types = ARRAY_SIZE(carrizo_cache_info);
		break;
	case CHIP_TONGA:
		pcache_info = tonga_cache_info;
		num_of_cache_types = ARRAY_SIZE(tonga_cache_info);
		break;
	case CHIP_FIJI:
		pcache_info = fiji_cache_info;
		num_of_cache_types = ARRAY_SIZE(fiji_cache_info);
		break;
	case CHIP_POLARIS10:
		pcache_info = polaris10_cache_info;
		num_of_cache_types = ARRAY_SIZE(polaris10_cache_info);
		break;
	case CHIP_POLARIS11:
		pcache_info = polaris11_cache_info;
		num_of_cache_types = ARRAY_SIZE(polaris11_cache_info);
		break;
	case CHIP_POLARIS12:
		pcache_info = polaris12_cache_info;
		num_of_cache_types = ARRAY_SIZE(polaris12_cache_info);
		break;
	case CHIP_VEGAM:
		pcache_info = vegam_cache_info;
		num_of_cache_types = ARRAY_SIZE(vegam_cache_info);
		break;
	case CHIP_VEGA10:
		pcache_info = vega10_cache_info;
		num_of_cache_types = ARRAY_SIZE(vega10_cache_info);
		break;
	case CHIP_VEGA12:
		pcache_info = vega12_cache_info;
		num_of_cache_types = ARRAY_SIZE(vega12_cache_info);
		break;
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		pcache_info = vega20_cache_info;
		num_of_cache_types = ARRAY_SIZE(vega20_cache_info);
		break;
	case CHIP_ALDEBARAN:
		pcache_info = aldebaran_cache_info;
		num_of_cache_types = ARRAY_SIZE(aldebaran_cache_info);
		break;
	case CHIP_RAVEN:
		pcache_info = raven_cache_info;
		num_of_cache_types = ARRAY_SIZE(raven_cache_info);
		break;
	case CHIP_RENOIR:
		pcache_info = renoir_cache_info;
		num_of_cache_types = ARRAY_SIZE(renoir_cache_info);
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI12:
		pcache_info = navi10_cache_info;
		num_of_cache_types = ARRAY_SIZE(navi10_cache_info);
		break;
	case CHIP_NAVI14:
		pcache_info = navi14_cache_info;
		num_of_cache_types = ARRAY_SIZE(navi14_cache_info);
		break;
	case CHIP_SIENNA_CICHLID:
		pcache_info = sienna_cichlid_cache_info;
		num_of_cache_types = ARRAY_SIZE(sienna_cichlid_cache_info);
		break;
	case CHIP_NAVY_FLOUNDER:
		pcache_info = navy_flounder_cache_info;
		num_of_cache_types = ARRAY_SIZE(navy_flounder_cache_info);
		break;
	case CHIP_DIMGREY_CAVEFISH:
		pcache_info = dimgrey_cavefish_cache_info;
		num_of_cache_types = ARRAY_SIZE(dimgrey_cavefish_cache_info);
		break;
	case CHIP_VANGOGH:
		pcache_info = vangogh_cache_info;
		num_of_cache_types = ARRAY_SIZE(vangogh_cache_info);
		break;
	case CHIP_BEIGE_GOBY:
		pcache_info = beige_goby_cache_info;
		num_of_cache_types = ARRAY_SIZE(beige_goby_cache_info);
		break;
	default:
		return -EINVAL;
	}

	*size_filled = 0;
	*num_of_entries = 0;

	/* For each type of cache listed in the kfd_gpu_cache_info table,
	 * go through all available Compute Units.
	 * The [i,j,k] loop will
	 *		if kfd_gpu_cache_info.num_cu_shared = 1
	 *			will parse through all available CU
	 *		If (kfd_gpu_cache_info.num_cu_shared != 1)
	 *			then it will consider only one CU from
	 *			the shared unit
	 */

	for (ct = 0; ct < num_of_cache_types; ct++) {
	  cu_processor_id = gpu_processor_id;
	  if (pcache_info[ct].cache_level == 1) {
	    for (i = 0; i < cu_info->num_shader_engines; i++) {
	      for (j = 0; j < cu_info->num_shader_arrays_per_engine; j++) {
	        for (k = 0; k < cu_info->num_cu_per_sh;
		  k += pcache_info[ct].num_cu_shared) {
		  ret = fill_in_l1_pcache(pcache,
					pcache_info,
					cu_info,
					mem_available,
					cu_info->cu_bitmap[i % 4][j + i / 4],
					ct,
					cu_processor_id,
					k);

		  if (ret < 0)
			break;

		  if (!ret) {
				pcache++;
				(*num_of_entries)++;
				mem_available -= sizeof(*pcache);
				(*size_filled) += sizeof(*pcache);
		  }

		  /* Move to next CU block */
		  num_cu_shared = ((k + pcache_info[ct].num_cu_shared) <=
					cu_info->num_cu_per_sh) ?
					pcache_info[ct].num_cu_shared :
					(cu_info->num_cu_per_sh - k);
		  cu_processor_id += num_cu_shared;
		}
	      }
	    }
	  } else {
			ret = fill_in_l2_l3_pcache(pcache,
				pcache_info,
				cu_info,
				mem_available,
				ct,
				cu_processor_id);

			if (ret < 0)
				break;

			if (!ret) {
				pcache++;
				(*num_of_entries)++;
				mem_available -= sizeof(*pcache);
				(*size_filled) += sizeof(*pcache);
			}
	  }
	}

	pr_debug("Added [%d] GPU cache entries\n", *num_of_entries);

	return 0;
}

static bool kfd_ignore_crat(void)
{
	bool ret;

	if (ignore_crat)
		return true;

#ifndef KFD_SUPPORT_IOMMU_V2
	ret = true;
#else
	ret = false;
#endif

	return ret;
}

/*
 * kfd_create_crat_image_acpi - Allocates memory for CRAT image and
 * copies CRAT from ACPI (if available).
 * NOTE: Call kfd_destroy_crat_image to free CRAT image memory
 *
 *	@crat_image: CRAT read from ACPI. If no CRAT in ACPI then
 *		     crat_image will be NULL
 *	@size: [OUT] size of crat_image
 *
 *	Return 0 if successful else return error code
 */
int kfd_create_crat_image_acpi(void **crat_image, size_t *size)
{
	struct acpi_table_header *crat_table;
	acpi_status status;
	void *pcrat_image;
	int rc = 0;

	if (!crat_image)
		return -EINVAL;

	*crat_image = NULL;

	if (kfd_ignore_crat()) {
		pr_info("CRAT table disabled by module option\n");
		return -ENODATA;
	}

	/* Fetch the CRAT table from ACPI */
	status = acpi_get_table(CRAT_SIGNATURE, 0, &crat_table);
	if (status == AE_NOT_FOUND) {
		pr_warn("CRAT table not found\n");
		return -ENODATA;
	} else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);

		pr_err("CRAT table error: %s\n", err);
		return -EINVAL;
	}

	pcrat_image = kvmalloc(crat_table->length, GFP_KERNEL);
	if (!pcrat_image) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(pcrat_image, crat_table, crat_table->length);
	*crat_image = pcrat_image;
	*size = crat_table->length;
out:
	acpi_put_table(crat_table);
	return rc;
}

/* Memory required to create Virtual CRAT.
 * Since there is no easy way to predict the amount of memory required, the
 * following amount is allocated for GPU Virtual CRAT. This is
 * expected to cover all known conditions. But to be safe additional check
 * is put in the code to ensure we don't overwrite.
 */
#define VCRAT_SIZE_FOR_GPU	(4 * PAGE_SIZE)

/* kfd_fill_cu_for_cpu - Fill in Compute info for the given CPU NUMA node
 *
 *	@numa_node_id: CPU NUMA node id
 *	@avail_size: Available size in the memory
 *	@sub_type_hdr: Memory into which compute info will be filled in
 *
 *	Return 0 if successful else return -ve value
 */
static int kfd_fill_cu_for_cpu(int numa_node_id, int *avail_size,
				int proximity_domain,
				struct crat_subtype_computeunit *sub_type_hdr)
{
	const struct cpumask *cpumask;

	*avail_size -= sizeof(struct crat_subtype_computeunit);
	if (*avail_size < 0)
		return -ENOMEM;

	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_computeunit));

	/* Fill in subtype header data */
	sub_type_hdr->type = CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_computeunit);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	cpumask = cpumask_of_node(numa_node_id);

	/* Fill in CU data */
	sub_type_hdr->flags |= CRAT_CU_FLAGS_CPU_PRESENT;
	sub_type_hdr->proximity_domain = proximity_domain;
	sub_type_hdr->processor_id_low = kfd_numa_node_to_apic_id(numa_node_id);
	if (sub_type_hdr->processor_id_low == -1)
		return -EINVAL;

	sub_type_hdr->num_cpu_cores = cpumask_weight(cpumask);

	return 0;
}

/* kfd_fill_mem_info_for_cpu - Fill in Memory info for the given CPU NUMA node
 *
 *	@numa_node_id: CPU NUMA node id
 *	@avail_size: Available size in the memory
 *	@sub_type_hdr: Memory into which compute info will be filled in
 *
 *	Return 0 if successful else return -ve value
 */
static int kfd_fill_mem_info_for_cpu(int numa_node_id, int *avail_size,
			int proximity_domain,
			struct crat_subtype_memory *sub_type_hdr)
{
	uint64_t mem_in_bytes = 0;
	pg_data_t *pgdat;
	int zone_type;

	*avail_size -= sizeof(struct crat_subtype_memory);
	if (*avail_size < 0)
		return -ENOMEM;

	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_memory));

	/* Fill in subtype header data */
	sub_type_hdr->type = CRAT_SUBTYPE_MEMORY_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_memory);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill in Memory Subunit data */

	/* Unlike si_meminfo, si_meminfo_node is not exported. So
	 * the following lines are duplicated from si_meminfo_node
	 * function
	 */
	pgdat = NODE_DATA(numa_node_id);
	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++)
		mem_in_bytes += zone_managed_pages(&pgdat->node_zones[zone_type]);
	mem_in_bytes <<= PAGE_SHIFT;

	sub_type_hdr->length_low = lower_32_bits(mem_in_bytes);
	sub_type_hdr->length_high = upper_32_bits(mem_in_bytes);
	sub_type_hdr->proximity_domain = proximity_domain;

	return 0;
}

#ifdef CONFIG_X86_64
static int kfd_fill_iolink_info_for_cpu(int numa_node_id, int *avail_size,
				uint32_t *num_entries,
				struct crat_subtype_iolink *sub_type_hdr)
{
	int nid;
	struct cpuinfo_x86 *c = &cpu_data(0);
	uint8_t link_type;

	if (c->x86_vendor == X86_VENDOR_AMD)
		link_type = CRAT_IOLINK_TYPE_HYPERTRANSPORT;
	else
		link_type = CRAT_IOLINK_TYPE_QPI_1_1;

	*num_entries = 0;

	/* Create IO links from this node to other CPU nodes */
	for_each_online_node(nid) {
		if (nid == numa_node_id) /* node itself */
			continue;

		*avail_size -= sizeof(struct crat_subtype_iolink);
		if (*avail_size < 0)
			return -ENOMEM;

		memset(sub_type_hdr, 0, sizeof(struct crat_subtype_iolink));

		/* Fill in subtype header data */
		sub_type_hdr->type = CRAT_SUBTYPE_IOLINK_AFFINITY;
		sub_type_hdr->length = sizeof(struct crat_subtype_iolink);
		sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

		/* Fill in IO link data */
		sub_type_hdr->proximity_domain_from = numa_node_id;
		sub_type_hdr->proximity_domain_to = nid;
		sub_type_hdr->io_interface_type = link_type;

		(*num_entries)++;
		sub_type_hdr++;
	}

	return 0;
}
#endif

/* kfd_create_vcrat_image_cpu - Create Virtual CRAT for CPU
 *
 *	@pcrat_image: Fill in VCRAT for CPU
 *	@size:	[IN] allocated size of crat_image.
 *		[OUT] actual size of data filled in crat_image
 */
static int kfd_create_vcrat_image_cpu(void *pcrat_image, size_t *size)
{
	struct crat_header *crat_table = (struct crat_header *)pcrat_image;
	struct acpi_table_header *acpi_table;
	acpi_status status;
	struct crat_subtype_generic *sub_type_hdr;
	int avail_size = *size;
	int numa_node_id;
#ifdef CONFIG_X86_64
	uint32_t entries = 0;
#endif
	int ret = 0;

	if (!pcrat_image)
		return -EINVAL;

	/* Fill in CRAT Header.
	 * Modify length and total_entries as subunits are added.
	 */
	avail_size -= sizeof(struct crat_header);
	if (avail_size < 0)
		return -ENOMEM;

	memset(crat_table, 0, sizeof(struct crat_header));
	memcpy(&crat_table->signature, CRAT_SIGNATURE,
			sizeof(crat_table->signature));
	crat_table->length = sizeof(struct crat_header);

	status = acpi_get_table("DSDT", 0, &acpi_table);
	if (status != AE_OK)
		pr_warn("DSDT table not found for OEM information\n");
	else {
		crat_table->oem_revision = acpi_table->revision;
		memcpy(crat_table->oem_id, acpi_table->oem_id,
				CRAT_OEMID_LENGTH);
		memcpy(crat_table->oem_table_id, acpi_table->oem_table_id,
				CRAT_OEMTABLEID_LENGTH);
		acpi_put_table(acpi_table);
	}
	crat_table->total_entries = 0;
	crat_table->num_domains = 0;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table+1);

	for_each_online_node(numa_node_id) {
		if (kfd_numa_node_to_apic_id(numa_node_id) == -1)
			continue;

		/* Fill in Subtype: Compute Unit */
		ret = kfd_fill_cu_for_cpu(numa_node_id, &avail_size,
			crat_table->num_domains,
			(struct crat_subtype_computeunit *)sub_type_hdr);
		if (ret < 0)
			return ret;
		crat_table->length += sub_type_hdr->length;
		crat_table->total_entries++;

		sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
			sub_type_hdr->length);

		/* Fill in Subtype: Memory */
		ret = kfd_fill_mem_info_for_cpu(numa_node_id, &avail_size,
			crat_table->num_domains,
			(struct crat_subtype_memory *)sub_type_hdr);
		if (ret < 0)
			return ret;
		crat_table->length += sub_type_hdr->length;
		crat_table->total_entries++;

		sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
			sub_type_hdr->length);

		/* Fill in Subtype: IO Link */
#ifdef CONFIG_X86_64
		ret = kfd_fill_iolink_info_for_cpu(numa_node_id, &avail_size,
				&entries,
				(struct crat_subtype_iolink *)sub_type_hdr);
		if (ret < 0)
			return ret;

		if (entries) {
			crat_table->length += (sub_type_hdr->length * entries);
			crat_table->total_entries += entries;

			sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
					sub_type_hdr->length * entries);
		}
#else
		pr_info("IO link not available for non x86 platforms\n");
#endif

		crat_table->num_domains++;
	}

	/* TODO: Add cache Subtype for CPU.
	 * Currently, CPU cache information is available in function
	 * detect_cache_attributes(cpu) defined in the file
	 * ./arch/x86/kernel/cpu/intel_cacheinfo.c. This function is not
	 * exported and to get the same information the code needs to be
	 * duplicated.
	 */

	*size = crat_table->length;
	pr_info("Virtual CRAT table created for CPU\n");

	return 0;
}

static int kfd_fill_gpu_memory_affinity(int *avail_size,
		struct kfd_dev *kdev, uint8_t type, uint64_t size,
		struct crat_subtype_memory *sub_type_hdr,
		uint32_t proximity_domain,
		const struct kfd_local_mem_info *local_mem_info)
{
	*avail_size -= sizeof(struct crat_subtype_memory);
	if (*avail_size < 0)
		return -ENOMEM;

	memset((void *)sub_type_hdr, 0, sizeof(struct crat_subtype_memory));
	sub_type_hdr->type = CRAT_SUBTYPE_MEMORY_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_memory);
	sub_type_hdr->flags |= CRAT_SUBTYPE_FLAGS_ENABLED;

	sub_type_hdr->proximity_domain = proximity_domain;

	pr_debug("Fill gpu memory affinity - type 0x%x size 0x%llx\n",
			type, size);

	sub_type_hdr->length_low = lower_32_bits(size);
	sub_type_hdr->length_high = upper_32_bits(size);

	sub_type_hdr->width = local_mem_info->vram_width;
	sub_type_hdr->visibility_type = type;

	return 0;
}

#ifdef CONFIG_ACPI_NUMA
static void kfd_find_numa_node_in_srat(struct kfd_dev *kdev)
{
	struct acpi_table_header *table_header = NULL;
	struct acpi_subtable_header *sub_header = NULL;
	unsigned long table_end, subtable_len;
	u32 pci_id = pci_domain_nr(kdev->pdev->bus) << 16 |
			pci_dev_id(kdev->pdev);
	u32 bdf;
	acpi_status status;
	struct acpi_srat_cpu_affinity *cpu;
	struct acpi_srat_generic_affinity *gpu;
	int pxm = 0, max_pxm = 0;
	int numa_node = NUMA_NO_NODE;
	bool found = false;

	/* Fetch the SRAT table from ACPI */
	status = acpi_get_table(ACPI_SIG_SRAT, 0, &table_header);
	if (status == AE_NOT_FOUND) {
		pr_warn("SRAT table not found\n");
		return;
	} else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);
		pr_err("SRAT table error: %s\n", err);
		return;
	}

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */
	sub_header = (struct acpi_subtable_header *)
			((unsigned long)table_header +
			sizeof(struct acpi_table_srat));
	subtable_len = sub_header->length;

	while (((unsigned long)sub_header) + subtable_len  < table_end) {
		/*
		 * If length is 0, break from this loop to avoid
		 * infinite loop.
		 */
		if (subtable_len == 0) {
			pr_err("SRAT invalid zero length\n");
			break;
		}

		switch (sub_header->type) {
		case ACPI_SRAT_TYPE_CPU_AFFINITY:
			cpu = (struct acpi_srat_cpu_affinity *)sub_header;
			pxm = *((u32 *)cpu->proximity_domain_hi) << 8 |
					cpu->proximity_domain_lo;
			if (pxm > max_pxm)
				max_pxm = pxm;
			break;
		case ACPI_SRAT_TYPE_GENERIC_AFFINITY:
			gpu = (struct acpi_srat_generic_affinity *)sub_header;
			bdf = *((u16 *)(&gpu->device_handle[0])) << 16 |
					*((u16 *)(&gpu->device_handle[2]));
			if (bdf == pci_id) {
				found = true;
				numa_node = pxm_to_node(gpu->proximity_domain);
			}
			break;
		default:
			break;
		}

		if (found)
			break;

		sub_header = (struct acpi_subtable_header *)
				((unsigned long)sub_header + subtable_len);
		subtable_len = sub_header->length;
	}

	acpi_put_table(table_header);

	/* Workaround bad cpu-gpu binding case */
	if (found && (numa_node < 0 ||
			numa_node > pxm_to_node(max_pxm)))
		numa_node = 0;

	if (numa_node != NUMA_NO_NODE)
		set_dev_node(&kdev->pdev->dev, numa_node);
}
#endif

/* kfd_fill_gpu_direct_io_link - Fill in direct io link from GPU
 * to its NUMA node
 *	@avail_size: Available size in the memory
 *	@kdev - [IN] GPU device
 *	@sub_type_hdr: Memory into which io link info will be filled in
 *	@proximity_domain - proximity domain of the GPU node
 *
 *	Return 0 if successful else return -ve value
 */
static int kfd_fill_gpu_direct_io_link_to_cpu(int *avail_size,
			struct kfd_dev *kdev,
			struct crat_subtype_iolink *sub_type_hdr,
			uint32_t proximity_domain)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kdev->kgd;

	*avail_size -= sizeof(struct crat_subtype_iolink);
	if (*avail_size < 0)
		return -ENOMEM;

	memset((void *)sub_type_hdr, 0, sizeof(struct crat_subtype_iolink));

	/* Fill in subtype header data */
	sub_type_hdr->type = CRAT_SUBTYPE_IOLINK_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_iolink);
	sub_type_hdr->flags |= CRAT_SUBTYPE_FLAGS_ENABLED;
	if (kfd_dev_is_large_bar(kdev))
		sub_type_hdr->flags |= CRAT_IOLINK_FLAGS_BI_DIRECTIONAL;

	/* Fill in IOLINK subtype.
	 * TODO: Fill-in other fields of iolink subtype
	 */
	if (adev->gmc.xgmi.connected_to_cpu) {
		/*
		 * with host gpu xgmi link, host can access gpu memory whether
		 * or not pcie bar type is large, so always create bidirectional
		 * io link.
		 */
		sub_type_hdr->flags |= CRAT_IOLINK_FLAGS_BI_DIRECTIONAL;
		sub_type_hdr->io_interface_type = CRAT_IOLINK_TYPE_XGMI;
		sub_type_hdr->num_hops_xgmi = 1;
	} else {
		sub_type_hdr->io_interface_type = CRAT_IOLINK_TYPE_PCIEXPRESS;
	}

	sub_type_hdr->proximity_domain_from = proximity_domain;

#ifdef CONFIG_ACPI_NUMA
	if (kdev->pdev->dev.numa_node == NUMA_NO_NODE)
		kfd_find_numa_node_in_srat(kdev);
#endif
#ifdef CONFIG_NUMA
	if (kdev->pdev->dev.numa_node == NUMA_NO_NODE)
		sub_type_hdr->proximity_domain_to = 0;
	else
		sub_type_hdr->proximity_domain_to = kdev->pdev->dev.numa_node;
#else
	sub_type_hdr->proximity_domain_to = 0;
#endif
	return 0;
}

static int kfd_fill_gpu_xgmi_link_to_gpu(int *avail_size,
			struct kfd_dev *kdev,
			struct kfd_dev *peer_kdev,
			struct crat_subtype_iolink *sub_type_hdr,
			uint32_t proximity_domain_from,
			uint32_t proximity_domain_to)
{
	*avail_size -= sizeof(struct crat_subtype_iolink);
	if (*avail_size < 0)
		return -ENOMEM;

	memset((void *)sub_type_hdr, 0, sizeof(struct crat_subtype_iolink));

	sub_type_hdr->type = CRAT_SUBTYPE_IOLINK_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_iolink);
	sub_type_hdr->flags |= CRAT_SUBTYPE_FLAGS_ENABLED |
			       CRAT_IOLINK_FLAGS_BI_DIRECTIONAL;

	sub_type_hdr->io_interface_type = CRAT_IOLINK_TYPE_XGMI;
	sub_type_hdr->proximity_domain_from = proximity_domain_from;
	sub_type_hdr->proximity_domain_to = proximity_domain_to;
	sub_type_hdr->num_hops_xgmi =
		amdgpu_amdkfd_get_xgmi_hops_count(kdev->kgd, peer_kdev->kgd);
	return 0;
}

/* kfd_create_vcrat_image_gpu - Create Virtual CRAT for CPU
 *
 *	@pcrat_image: Fill in VCRAT for GPU
 *	@size:	[IN] allocated size of crat_image.
 *		[OUT] actual size of data filled in crat_image
 */
static int kfd_create_vcrat_image_gpu(void *pcrat_image,
				      size_t *size, struct kfd_dev *kdev,
				      uint32_t proximity_domain)
{
	struct crat_header *crat_table = (struct crat_header *)pcrat_image;
	struct crat_subtype_generic *sub_type_hdr;
	struct kfd_local_mem_info local_mem_info;
	struct kfd_topology_device *peer_dev;
	struct crat_subtype_computeunit *cu;
	struct kfd_cu_info cu_info;
	int avail_size = *size;
	uint32_t total_num_of_cu;
	int num_of_cache_entries = 0;
	int cache_mem_filled = 0;
	uint32_t nid = 0;
	int ret = 0;

	if (!pcrat_image || avail_size < VCRAT_SIZE_FOR_GPU)
		return -EINVAL;

	/* Fill the CRAT Header.
	 * Modify length and total_entries as subunits are added.
	 */
	avail_size -= sizeof(struct crat_header);
	if (avail_size < 0)
		return -ENOMEM;

	memset(crat_table, 0, sizeof(struct crat_header));

	memcpy(&crat_table->signature, CRAT_SIGNATURE,
			sizeof(crat_table->signature));
	/* Change length as we add more subtypes*/
	crat_table->length = sizeof(struct crat_header);
	crat_table->num_domains = 1;
	crat_table->total_entries = 0;

	/* Fill in Subtype: Compute Unit
	 * First fill in the sub type header and then sub type data
	 */
	avail_size -= sizeof(struct crat_subtype_computeunit);
	if (avail_size < 0)
		return -ENOMEM;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table + 1);
	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_computeunit));

	sub_type_hdr->type = CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_computeunit);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill CU subtype data */
	cu = (struct crat_subtype_computeunit *)sub_type_hdr;
	cu->flags |= CRAT_CU_FLAGS_GPU_PRESENT;
	cu->proximity_domain = proximity_domain;

	amdgpu_amdkfd_get_cu_info(kdev->kgd, &cu_info);
	cu->num_simd_per_cu = cu_info.simd_per_cu;
	cu->num_simd_cores = cu_info.simd_per_cu * cu_info.cu_active_number;
	cu->max_waves_simd = cu_info.max_waves_per_simd;

	cu->wave_front_size = cu_info.wave_front_size;
	cu->array_count = cu_info.num_shader_arrays_per_engine *
		cu_info.num_shader_engines;
	total_num_of_cu = (cu->array_count * cu_info.num_cu_per_sh);
	cu->processor_id_low = get_and_inc_gpu_processor_id(total_num_of_cu);
	cu->num_cu_per_array = cu_info.num_cu_per_sh;
	cu->max_slots_scatch_cu = cu_info.max_scratch_slots_per_cu;
	cu->num_banks = cu_info.num_shader_engines;
	cu->lds_size_in_kb = cu_info.lds_size;

	cu->hsa_capability = 0;

	/* Check if this node supports IOMMU. During parsing this flag will
	 * translate to HSA_CAP_ATS_PRESENT
	 */
	if (!kfd_iommu_check_device(kdev))
		cu->hsa_capability |= CRAT_CU_FLAGS_IOMMU_PRESENT;

	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;

	/* Fill in Subtype: Memory. Only on systems with large BAR (no
	 * private FB), report memory as public. On other systems
	 * report the total FB size (public+private) as a single
	 * private heap.
	 */
	amdgpu_amdkfd_get_local_mem_info(kdev->kgd, &local_mem_info);
	sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
			sub_type_hdr->length);

	if (debug_largebar)
		local_mem_info.local_mem_size_private = 0;

	if (local_mem_info.local_mem_size_private == 0)
		ret = kfd_fill_gpu_memory_affinity(&avail_size,
				kdev, HSA_MEM_HEAP_TYPE_FB_PUBLIC,
				local_mem_info.local_mem_size_public,
				(struct crat_subtype_memory *)sub_type_hdr,
				proximity_domain,
				&local_mem_info);
	else
		ret = kfd_fill_gpu_memory_affinity(&avail_size,
				kdev, HSA_MEM_HEAP_TYPE_FB_PRIVATE,
				local_mem_info.local_mem_size_public +
				local_mem_info.local_mem_size_private,
				(struct crat_subtype_memory *)sub_type_hdr,
				proximity_domain,
				&local_mem_info);
	if (ret < 0)
		return ret;

	crat_table->length += sizeof(struct crat_subtype_memory);
	crat_table->total_entries++;

	/* TODO: Fill in cache information. This information is NOT readily
	 * available in KGD
	 */
	sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
		sub_type_hdr->length);
	ret = kfd_fill_gpu_cache_info(kdev, cu->processor_id_low,
				avail_size,
				&cu_info,
				(struct crat_subtype_cache *)sub_type_hdr,
				&cache_mem_filled,
				&num_of_cache_entries);

	if (ret < 0)
		return ret;

	crat_table->length += cache_mem_filled;
	crat_table->total_entries += num_of_cache_entries;
	avail_size -= cache_mem_filled;

	/* Fill in Subtype: IO_LINKS
	 *  Only direct links are added here which is Link from GPU to
	 *  to its NUMA node. Indirect links are added by userspace.
	 */
	sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
		cache_mem_filled);
	ret = kfd_fill_gpu_direct_io_link_to_cpu(&avail_size, kdev,
		(struct crat_subtype_iolink *)sub_type_hdr, proximity_domain);

	if (ret < 0)
		return ret;

	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;


	/* Fill in Subtype: IO_LINKS
	 * Direct links from GPU to other GPUs through xGMI.
	 * We will loop GPUs that already be processed (with lower value
	 * of proximity_domain), add the link for the GPUs with same
	 * hive id (from this GPU to other GPU) . The reversed iolink
	 * (from other GPU to this GPU) will be added
	 * in kfd_parse_subtype_iolink.
	 */
	if (kdev->hive_id) {
		for (nid = 0; nid < proximity_domain; ++nid) {
			peer_dev = kfd_topology_device_by_proximity_domain(nid);
			if (!peer_dev->gpu)
				continue;
			if (peer_dev->gpu->hive_id != kdev->hive_id)
				continue;
			sub_type_hdr = (typeof(sub_type_hdr))(
				(char *)sub_type_hdr +
				sizeof(struct crat_subtype_iolink));
			ret = kfd_fill_gpu_xgmi_link_to_gpu(
				&avail_size, kdev, peer_dev->gpu,
				(struct crat_subtype_iolink *)sub_type_hdr,
				proximity_domain, nid);
			if (ret < 0)
				return ret;
			crat_table->length += sub_type_hdr->length;
			crat_table->total_entries++;
		}
	}
	*size = crat_table->length;
	pr_info("Virtual CRAT table created for GPU\n");

	return ret;
}

/* kfd_create_crat_image_virtual - Allocates memory for CRAT image and
 *		creates a Virtual CRAT (VCRAT) image
 *
 * NOTE: Call kfd_destroy_crat_image to free CRAT image memory
 *
 *	@crat_image: VCRAT image created because ACPI does not have a
 *		     CRAT for this device
 *	@size: [OUT] size of virtual crat_image
 *	@flags:	COMPUTE_UNIT_CPU - Create VCRAT for CPU device
 *		COMPUTE_UNIT_GPU - Create VCRAT for GPU
 *		(COMPUTE_UNIT_CPU | COMPUTE_UNIT_GPU) - Create VCRAT for APU
 *			-- this option is not currently implemented.
 *			The assumption is that all AMD APUs will have CRAT
 *	@kdev: Valid kfd_device required if flags contain COMPUTE_UNIT_GPU
 *
 *	Return 0 if successful else return -ve value
 */
int kfd_create_crat_image_virtual(void **crat_image, size_t *size,
				  int flags, struct kfd_dev *kdev,
				  uint32_t proximity_domain)
{
	void *pcrat_image = NULL;
	int ret = 0, num_nodes;
	size_t dyn_size;

	if (!crat_image)
		return -EINVAL;

	*crat_image = NULL;

	/* Allocate the CPU Virtual CRAT size based on the number of online
	 * nodes. Allocate VCRAT_SIZE_FOR_GPU for GPU virtual CRAT image.
	 * This should cover all the current conditions. A check is put not
	 * to overwrite beyond allocated size for GPUs
	 */
	switch (flags) {
	case COMPUTE_UNIT_CPU:
		num_nodes = num_online_nodes();
		dyn_size = sizeof(struct crat_header) +
			num_nodes * (sizeof(struct crat_subtype_computeunit) +
			sizeof(struct crat_subtype_memory) +
			(num_nodes - 1) * sizeof(struct crat_subtype_iolink));
		pcrat_image = kvmalloc(dyn_size, GFP_KERNEL);
		if (!pcrat_image)
			return -ENOMEM;
		*size = dyn_size;
		pr_debug("CRAT size is %ld", dyn_size);
		ret = kfd_create_vcrat_image_cpu(pcrat_image, size);
		break;
	case COMPUTE_UNIT_GPU:
		if (!kdev)
			return -EINVAL;
		pcrat_image = kvmalloc(VCRAT_SIZE_FOR_GPU, GFP_KERNEL);
		if (!pcrat_image)
			return -ENOMEM;
		*size = VCRAT_SIZE_FOR_GPU;
		ret = kfd_create_vcrat_image_gpu(pcrat_image, size, kdev,
						 proximity_domain);
		break;
	case (COMPUTE_UNIT_CPU | COMPUTE_UNIT_GPU):
		/* TODO: */
		ret = -EINVAL;
		pr_err("VCRAT not implemented for APU\n");
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret)
		*crat_image = pcrat_image;
	else
		kvfree(pcrat_image);

	return ret;
}


/* kfd_destroy_crat_image
 *
 *	@crat_image: [IN] - crat_image from kfd_create_crat_image_xxx(..)
 *
 */
void kfd_destroy_crat_image(void *crat_image)
{
	kvfree(crat_image);
}
