/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _KBASE_UKU_H_
#define _KBASE_UKU_H_

#include <uk/mali_uk.h>
#include <ump/ump_common.h>
#include <malisw/mali_malisw.h>
#include <kbase/mali_base_kernel.h>
#if (MALI_ERROR_INJECT_ON || MALI_NO_MALI)
#include <kbase/src/common/mali_kbase_model_dummy.h>
#endif

#include "mali_kbase_gpuprops_types.h"

#define BASE_UK_VERSION_MAJOR 0
#define BASE_UK_VERSION_MINOR 1

typedef struct kbase_uk_tmem_alloc
{
	uk_header   header;
	/* IN */
	u32         vsize;
	u32         psize;
	u32         extent;
	u32         flags;
	mali_bool   is_growable;
	/* OUT */
	mali_addr64 gpu_addr;
} kbase_uk_tmem_alloc;

typedef struct kbase_uk_tmem_from_ump
{
	uk_header   header;
	/* IN */
	ump_secure_id id;
	/* OUT */
	mali_addr64 gpu_addr;
	u64 pages;
} kbase_uk_tmem_from_ump;

typedef struct kbase_uk_pmem_alloc
{
	uk_header   header;
	/* IN */
	u32         vsize;
	u32         flags;
	/* OUT */
	u16         cookie;
} kbase_uk_pmem_alloc;

typedef struct kbase_uk_mem_free
{
	uk_header   header;
	/* IN */
	mali_addr64 gpu_addr;
	/* OUT */
} kbase_uk_mem_free;

typedef struct kbase_uk_job_submit
{
	uk_header   header;
	/* IN */
	u64         bag_uaddr;
	u64         core_restriction;
	u32         offset;
	u32         size;
	u32         nr_atoms;
	/* OUT */
} kbase_uk_job_submit;

typedef struct kbase_uk_post_term
{
	uk_header   header;
} kbase_uk_post_term;

typedef struct kbase_uk_sync_now
{
	uk_header   header;

	/* IN */
	base_syncset sset;

	/* OUT */
} kbase_uk_sync_now;

typedef struct kbase_uk_hwcnt_setup
{
	uk_header   header;

	/* IN */
	mali_addr64 dump_buffer;
	u32         jm_bm;
	u32         shader_bm;
	u32         tiler_bm;
	u32         l3_cache_bm;
	u32         mmu_l2_bm;
	/* OUT */
} kbase_uk_hwcnt_setup;

typedef struct kbase_uk_hwcnt_dump
{
	uk_header   header;
} kbase_uk_hwcnt_dump;

typedef struct kbase_uk_hwcnt_clear
{
	uk_header   header;
} kbase_uk_hwcnt_clear;

typedef struct kbase_uk_gpuprops
{
	uk_header header;

	/* IN */
	struct mali_base_gpu_props props;
	/* OUT */
}kbase_uk_gpuprops;

typedef struct kbase_uk_tmem_resize
{
	uk_header   header;
	/* IN */
	mali_addr64 gpu_addr;
	s32         delta;
	/* OUT */
	u32         size;
	base_backing_threshold_status result_subcode;
} kbase_uk_tmem_resize;

typedef struct kbase_uk_find_cpu_mapping
{
	uk_header     header;
	/* IN */
	mali_addr64   gpu_addr;
	u64           cpu_addr;
	u64           size;
	/* OUT */
	u64           uaddr;
	u32           nr_pages;
	mali_size64   page_off;
} kbase_uk_find_cpu_mapping;

#define KBASE_GET_VERSION_BUFFER_SIZE 64
typedef struct kbase_uk_get_ddk_version
{
	uk_header header;
	/* OUT */
	char version_buffer[KBASE_GET_VERSION_BUFFER_SIZE];
	u32  version_string_size;
} kbase_uk_get_ddk_version;


#if MALI_DEBUG
#define TEST_ADDR_COUNT 4
#define KBASE_TEST_BUFFER_SIZE 128
typedef struct kbase_exported_test_data
{
	mali_addr64           test_addr[TEST_ADDR_COUNT];		/* memory address */
	u32                   test_addr_pages[TEST_ADDR_COUNT]; /* memory size in pages */
	struct kbase_context *kctx;								/* base context created by process */
	void *mm;												/* pointer to process address space */
	u8 buffer1[KBASE_TEST_BUFFER_SIZE];   /* unit test defined parameter */
	u8 buffer2[KBASE_TEST_BUFFER_SIZE];   /* unit test defined parameter */
} kbase_exported_test_data;

typedef struct kbase_uk_set_test_data
{
	uk_header header;
	/* IN */
	kbase_exported_test_data test_data;
} kbase_uk_set_test_data;

#endif /* MALI_DEBUG */
#if MALI_ERROR_INJECT_ON
typedef struct kbase_uk_error_params
{
	uk_header header;
	/* IN */
	kbase_error_params params;
} kbase_uk_error_params;
#endif

#if MALI_NO_MALI
typedef struct kbase_uk_model_control_params
{
	uk_header header;
	/* IN */
	kbase_model_control_params params;
} kbase_uk_model_control_params;
#endif /* MALI_NO_MALI */

typedef enum kbase_uk_function_id
{
	KBASE_FUNC_TMEM_ALLOC = (UK_FUNC_ID + 0),
	KBASE_FUNC_TMEM_FROM_UMP,
	KBASE_FUNC_PMEM_ALLOC,
	KBASE_FUNC_MEM_FREE,

	KBASE_FUNC_JOB_SUBMIT,

	KBASE_FUNC_SYNC,

	KBASE_FUNC_POST_TERM,

	KBASE_FUNC_HWCNT_SETUP,
	KBASE_FUNC_HWCNT_DUMP,
	KBASE_FUNC_HWCNT_CLEAR,

	KBASE_FUNC_GPU_PROPS_REG_DUMP,

	KBASE_FUNC_TMEM_RESIZE,

	KBASE_FUNC_FIND_CPU_MAPPING,

	KBASE_FUNC_GET_VERSION

#if MALI_DEBUG
	, KBASE_FUNC_SET_TEST_DATA
#endif /* MALI_DEBUG */
#if MALI_ERROR_INJECT_ON
	, KBASE_FUNC_INJECT_ERROR
#endif
#if MALI_NO_MALI
	, KBASE_FUNC_MODEL_CONTROL
#endif /* MALI_NO_MALI */

} kbase_uk_function_id;

#endif /* _KBASE_UKU_H_ */
