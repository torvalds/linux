/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _KBASE_UKU_H_
#define _KBASE_UKU_H_

#include "mali_uk.h"
#include <malisw/mali_malisw.h>
#include "mali_base_kernel.h"

/* This file needs to support being included from kernel and userside (which use different defines) */
#if defined(CONFIG_MALI_ERROR_INJECT)
#define SUPPORT_MALI_ERROR_INJECT
#elif defined(MALI_ERROR_INJECT)
#if MALI_ERROR_INJECT
#define SUPPORT_MALI_ERROR_INJECT
#endif
#endif
#if defined(CONFIG_MALI_NO_MALI)
#define SUPPORT_MALI_NO_MALI
#elif defined(MALI_NO_MALI)
#if MALI_NO_MALI
#define SUPPORT_MALI_NO_MALI
#endif
#endif

#if defined(SUPPORT_MALI_NO_MALI) || defined(SUPPORT_MALI_ERROR_INJECT)
#include "mali_kbase_model_dummy.h"
#endif

#include "mali_kbase_gpuprops_types.h"

#define BASE_UK_VERSION_MAJOR 8
#define BASE_UK_VERSION_MINOR 0

struct kbase_uk_mem_alloc {
	union uk_header header;
	/* IN */
	u64 va_pages;
	u64 commit_pages;
	u64 extent;
	/* IN/OUT */
	u64 flags;
	/* OUT */
	u64 gpu_va;
	u16 va_alignment;
	u8  padding[6];
};

struct kbase_uk_mem_free {
	union uk_header header;
	/* IN */
	mali_addr64 gpu_addr;
	/* OUT */
};

struct kbase_uk_mem_alias {
	union uk_header header;
	/* IN/OUT */
	u64 flags;
	/* IN */
	u64 stride;
	u64 nents;
	union kbase_pointer ai;
	/* OUT */
	u64         gpu_va;
	u64         va_pages;
};

struct kbase_uk_mem_import {
	union uk_header header;
	/* IN */
	union kbase_pointer phandle;
	u32 type;
	u32 padding;
	/* IN/OUT */
	u64         flags;
	/* OUT */
	mali_addr64 gpu_va;
	u64         va_pages;
};

struct kbase_uk_mem_flags_change {
	union uk_header header;
	/* IN */
	mali_addr64 gpu_va;
	u64 flags;
	u64 mask;
};

struct kbase_uk_job_submit {
	union uk_header header;
	/* IN */
	union kbase_pointer addr;
	u32 nr_atoms;
	u32 stride;		/* bytes between atoms, i.e. sizeof(base_jd_atom_v2) */
	/* OUT */
};

struct kbase_uk_post_term {
	union uk_header header;
};

struct kbase_uk_sync_now {
	union uk_header header;

	/* IN */
	struct base_syncset sset;

	/* OUT */
};

struct kbase_uk_hwcnt_setup {
	union uk_header header;

	/* IN */
	mali_addr64 dump_buffer;
	u32 jm_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 l3_cache_bm;
	u32 mmu_l2_bm;
	u32 padding;
	/* OUT */
};

struct kbase_uk_hwcnt_dump {
	union uk_header header;
};

struct kbase_uk_hwcnt_clear {
	union uk_header header;
};

struct kbase_uk_fence_validate {
	union uk_header header;
	/* IN */
	s32 fd;
	u32 padding;
	/* OUT */
};

struct kbase_uk_stream_create {
	union uk_header header;
	/* IN */
	char name[32];
	/* OUT */
	s32 fd;
	u32 padding;
};

#ifdef BASE_LEGACY_UK7_SUPPORT
/**
 * This structure is kept for the backward compatibility reasons.
 * It shall be removed as soon as KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE
 * (previously KBASE_FUNC_CPU_PROPS_REG_DUMP) ioctl call
 * is removed. Removal of KBASE_FUNC_CPU_PROPS_REG_DUMP is part of having
 * the function for reading cpu properties moved from base to osu.
 */
#define BASE_CPU_PROPERTY_FLAG_LITTLE_ENDIAN F_BIT_0
struct base_cpu_id_props {
	/**
	 * CPU ID
	 */
	u32 id;

	/**
	 * CPU Part number
	 */
	u16 part;
	/**
	 * ASCII code of implementer trademark
	 */
	u8 implementer;

	/**
	 * CPU Variant
	 */
	u8 variant;
	/**
	 * CPU Architecture
	 */
	u8 arch;

	/**
	 * CPU revision
	 */
	u8 rev;

	/**
	 * Validity of CPU id where 0-invalid and
	 * 1-valid only if ALL the cpu_id props are valid
	 */
	u8 valid;

	u8 padding[1];
};

/**
 * This structure is kept for the backward compatibility reasons.
 * It shall be removed as soon as KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE
 * (previously KBASE_FUNC_CPU_PROPS_REG_DUMP) ioctl call
 * is removed. Removal of KBASE_FUNC_CPU_PROPS_REG_DUMP is part of having
 * the function for reading cpu properties moved from base to osu.
 */
struct base_cpu_props {
	u32 nr_cores;        /**< Number of CPU cores */

	/**
	 * CPU page size as a Logarithm to Base 2. The compile-time
	 * equivalent is @ref OSU_CONFIG_CPU_PAGE_SIZE_LOG2
	 */
	u32 cpu_page_size_log2;

	/**
	 * CPU L1 Data cache line size as a Logarithm to Base 2. The compile-time
	 * equivalent is @ref OSU_CONFIG_CPU_L1_DCACHE_LINE_SIZE_LOG2.
	 */
	u32 cpu_l1_dcache_line_size_log2;

	/**
	 * CPU L1 Data cache size, in bytes. The compile-time equivalient is
	 * @ref OSU_CONFIG_CPU_L1_DCACHE_SIZE.
	 *
	 * This CPU Property is mainly provided to implement OpenCL's
	 * clGetDeviceInfo(), which allows the CL_DEVICE_GLOBAL_MEM_CACHE_SIZE
	 * hint to be queried.
	 */
	u32 cpu_l1_dcache_size;

	/**
	 * CPU Property Flags bitpattern.
	 *
	 * This is a combination of bits as specified by the macros prefixed with
	 * 'BASE_CPU_PROPERTY_FLAG_'.
	 */
	u32 cpu_flags;

	/**
	 * Maximum clock speed in MHz.
	 * @usecase 'Maximum' CPU Clock Speed information is required by OpenCL's
	 * clGetDeviceInfo() function for the CL_DEVICE_MAX_CLOCK_FREQUENCY hint.
	 */
	u32 max_cpu_clock_speed_mhz;

	/**
	 * @brief Total memory, in bytes.
	 *
	 * This is the theoretical maximum memory available to the CPU. It is
	 * unlikely that a client will be able to allocate all of this memory for
	 * their own purposes, but this at least provides an upper bound on the
	 * memory available to the CPU.
	 *
	 * This is required for OpenCL's clGetDeviceInfo() call when
	 * CL_DEVICE_GLOBAL_MEM_SIZE is requested, for OpenCL CPU devices.
	 */
	u64 available_memory_size;

	/**
	 * CPU ID detailed info
	 */
	struct base_cpu_id_props cpu_id;

	u32 padding;
};

/**
 * This structure is kept for the backward compatibility reasons.
 * It shall be removed as soon as KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE
 * (previously KBASE_FUNC_CPU_PROPS_REG_DUMP) ioctl call
 * is removed. Removal of KBASE_FUNC_CPU_PROPS_REG_DUMP is part of having
 * the function for reading cpu properties moved from base to osu.
 */
struct kbase_uk_cpuprops {
	union uk_header header;

	/* IN */
	struct base_cpu_props props;
	/* OUT */
};
#endif /* BASE_LEGACY_UK7_SUPPORT */

struct kbase_uk_gpuprops {
	union uk_header header;

	/* IN */
	struct mali_base_gpu_props props;
	/* OUT */
};

struct kbase_uk_mem_query {
	union uk_header header;
	/* IN */
	mali_addr64 gpu_addr;
#define KBASE_MEM_QUERY_COMMIT_SIZE  1
#define KBASE_MEM_QUERY_VA_SIZE      2
#define KBASE_MEM_QUERY_FLAGS        3
	u64         query;
	/* OUT */
	u64         value;
};
	
struct kbase_uk_mem_commit {
	union uk_header header;
	/* IN */
	mali_addr64 gpu_addr;
	u64         pages;
	/* OUT */
	u32 result_subcode;
	u32 padding;
};

struct kbase_uk_find_cpu_offset {
	union uk_header header;
	/* IN */
	mali_addr64 gpu_addr;
	u64 cpu_addr;
	u64 size;
	/* OUT */
	mali_size64 offset;
};

#define KBASE_GET_VERSION_BUFFER_SIZE 64
struct kbase_uk_get_ddk_version {
	union uk_header header;
	/* OUT */
	char version_buffer[KBASE_GET_VERSION_BUFFER_SIZE];
	u32 version_string_size;
	u32 padding;
};

struct kbase_uk_disjoint_query {
	union uk_header header;
	/* OUT */
	u32 counter;
	u32 padding;
};

struct kbase_uk_set_flags {
	union uk_header header;
	/* IN */
	u32 create_flags;
	u32 padding;
};

#if MALI_UNIT_TEST
#define TEST_ADDR_COUNT 4
#define KBASE_TEST_BUFFER_SIZE 128
typedef struct kbase_exported_test_data {
	mali_addr64 test_addr[TEST_ADDR_COUNT];		/**< memory address */
	u32 test_addr_pages[TEST_ADDR_COUNT];		/**<  memory size in pages */
	union kbase_pointer kctx;				/**<  base context created by process */
	union kbase_pointer mm;				/**< pointer to process address space */
	u8 buffer1[KBASE_TEST_BUFFER_SIZE];   /**<  unit test defined parameter */
	u8 buffer2[KBASE_TEST_BUFFER_SIZE];   /**<  unit test defined parameter */
} kbase_exported_test_data;

struct kbase_uk_set_test_data {
	union uk_header header;
	/* IN */
	struct kbase_exported_test_data test_data;
};

#endif				/* MALI_UNIT_TEST */

#ifdef SUPPORT_MALI_ERROR_INJECT
struct kbase_uk_error_params {
	union uk_header header;
	/* IN */
	struct kbase_error_params params;
};
#endif				/* SUPPORT_MALI_ERROR_INJECT */

#ifdef SUPPORT_MALI_NO_MALI
struct kbase_uk_model_control_params {
	union uk_header header;
	/* IN */
	struct kbase_model_control_params params;
};
#endif				/* SUPPORT_MALI_NO_MALI */

#define KBASE_MAXIMUM_EXT_RESOURCES       255

struct kbase_uk_ext_buff_kds_data {
	union uk_header header;
	union kbase_pointer external_resource;
	union kbase_pointer file_descriptor;
	u32 num_res;		/* limited to KBASE_MAXIMUM_EXT_RESOURCES */
	u32 padding;
};

struct kbase_uk_keep_gpu_powered {
	union uk_header header;
	u32       enabled;
	u32       padding;
};

struct kbase_uk_profiling_controls {
	union uk_header header;
	u32 profiling_controls[FBDUMP_CONTROL_MAX];
};

struct kbase_uk_debugfs_mem_profile_add {
	union uk_header header;
	u32 len;
	union kbase_pointer buf;
};

enum kbase_uk_function_id {
	KBASE_FUNC_MEM_ALLOC = (UK_FUNC_ID + 0),
	KBASE_FUNC_MEM_IMPORT = (UK_FUNC_ID + 1),
	KBASE_FUNC_MEM_COMMIT = (UK_FUNC_ID + 2),
	KBASE_FUNC_MEM_QUERY = (UK_FUNC_ID + 3),
	KBASE_FUNC_MEM_FREE = (UK_FUNC_ID + 4),
	KBASE_FUNC_MEM_FLAGS_CHANGE = (UK_FUNC_ID + 5),
	KBASE_FUNC_MEM_ALIAS = (UK_FUNC_ID + 6),

#ifdef BASE_LEGACY_UK6_SUPPORT
	KBASE_FUNC_JOB_SUBMIT_UK6 = (UK_FUNC_ID + 7),
#endif /* BASE_LEGACY_UK6_SUPPORT */

	KBASE_FUNC_SYNC  = (UK_FUNC_ID + 8),

	KBASE_FUNC_POST_TERM = (UK_FUNC_ID + 9),

	KBASE_FUNC_HWCNT_SETUP = (UK_FUNC_ID + 10),
	KBASE_FUNC_HWCNT_DUMP = (UK_FUNC_ID + 11),
	KBASE_FUNC_HWCNT_CLEAR = (UK_FUNC_ID + 12),

#ifdef BASE_LEGACY_UK7_SUPPORT
	KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE = (UK_FUNC_ID + 13),
#endif /* BASE_LEGACY_UK7_SUPPORT */
	KBASE_FUNC_GPU_PROPS_REG_DUMP = (UK_FUNC_ID + 14),

	KBASE_FUNC_FIND_CPU_OFFSET = (UK_FUNC_ID + 15),

	KBASE_FUNC_GET_VERSION = (UK_FUNC_ID + 16),
	KBASE_FUNC_EXT_BUFFER_LOCK = (UK_FUNC_ID + 17),
	KBASE_FUNC_SET_FLAGS = (UK_FUNC_ID + 18),

	KBASE_FUNC_SET_TEST_DATA = (UK_FUNC_ID + 19),
	KBASE_FUNC_INJECT_ERROR = (UK_FUNC_ID + 20),
	KBASE_FUNC_MODEL_CONTROL = (UK_FUNC_ID + 21),

	KBASE_FUNC_KEEP_GPU_POWERED = (UK_FUNC_ID + 22),

	KBASE_FUNC_FENCE_VALIDATE = (UK_FUNC_ID + 23),
	KBASE_FUNC_STREAM_CREATE = (UK_FUNC_ID + 24),
	KBASE_FUNC_GET_PROFILING_CONTROLS = (UK_FUNC_ID + 25),
	KBASE_FUNC_SET_PROFILING_CONTROLS = (UK_FUNC_ID + 26),
					    /* to be used only for testing
					    * purposes, otherwise these controls
					    * are set through gator API */

	KBASE_FUNC_DEBUGFS_MEM_PROFILE_ADD = (UK_FUNC_ID + 27),
	KBASE_FUNC_JOB_SUBMIT = (UK_FUNC_ID + 28),
	KBASE_FUNC_DISJOINT_QUERY = (UK_FUNC_ID + 29)

};

#endif				/* _KBASE_UKU_H_ */
