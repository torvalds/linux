/*
 *
 * (C) COPYRIGHT 2008-2016 ARM Limited. All rights reserved.
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
#include "mali_base_kernel.h"

/* This file needs to support being included from kernel and userside (which use different defines) */
#if defined(CONFIG_MALI_ERROR_INJECT) || MALI_ERROR_INJECT_ON
#define SUPPORT_MALI_ERROR_INJECT
#endif /* defined(CONFIG_MALI_ERROR_INJECT) || MALI_ERROR_INJECT_ON */
#if defined(CONFIG_MALI_NO_MALI)
#define SUPPORT_MALI_NO_MALI
#elif defined(MALI_NO_MALI)
#if MALI_NO_MALI
#define SUPPORT_MALI_NO_MALI
#endif
#endif

#if defined(SUPPORT_MALI_NO_MALI) || defined(SUPPORT_MALI_ERROR_INJECT)
#include "backend/gpu/mali_kbase_model_dummy.h"
#endif

#include "mali_kbase_gpuprops_types.h"

/*
 * 10.1:
 * - Do mmap in kernel for SAME_VA memory allocations rather then
 *   calling back into the kernel as a 2nd stage of the allocation request.
 *
 * 10.2:
 * - Add KBASE_FUNC_MEM_JIT_INIT which allows clients to request a custom VA
 *   region for use with JIT (ignored on 32-bit platforms)
 *
 * 10.3:
 * - base_jd_core_req typedef-ed to u32 (instead of to u16)
 * - two flags added: BASE_JD_REQ_SKIP_CACHE_STAT / _END
 *
 * 10.4:
 * - Removed KBASE_FUNC_EXT_BUFFER_LOCK used only in internal tests
 *
 * 10.5:
 * - Reverted to performing mmap in user space so that tools like valgrind work.
 *
 * 10.6:
 * - Add flags input variable to KBASE_FUNC_TLSTREAM_ACQUIRE
 */
#define BASE_UK_VERSION_MAJOR 10
#define BASE_UK_VERSION_MINOR 6

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
	u64 gpu_addr;
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
	u64 gpu_va;
	u64         va_pages;
};

struct kbase_uk_mem_flags_change {
	union uk_header header;
	/* IN */
	u64 gpu_va;
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
	u64 dump_buffer;
	u32 jm_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 unused_1; /* keep for backwards compatibility */
	u32 mmu_l2_bm;
	u32 padding;
	/* OUT */
};

/**
 * struct kbase_uk_hwcnt_reader_setup - User/Kernel space data exchange structure
 * @header:       UK structure header
 * @buffer_count: requested number of dumping buffers
 * @jm_bm:        counters selection bitmask (JM)
 * @shader_bm:    counters selection bitmask (Shader)
 * @tiler_bm:     counters selection bitmask (Tiler)
 * @mmu_l2_bm:    counters selection bitmask (MMU_L2)
 * @fd:           dumping notification file descriptor
 *
 * This structure sets up HWC dumper/reader for this context.
 * Multiple instances can be created for single context.
 */
struct kbase_uk_hwcnt_reader_setup {
	union uk_header header;

	/* IN */
	u32 buffer_count;
	u32 jm_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;

	/* OUT */
	s32 fd;
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

struct kbase_uk_gpuprops {
	union uk_header header;

	/* IN */
	struct mali_base_gpu_props props;
	/* OUT */
};

struct kbase_uk_mem_query {
	union uk_header header;
	/* IN */
	u64 gpu_addr;
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
	u64 gpu_addr;
	u64         pages;
	/* OUT */
	u32 result_subcode;
	u32 padding;
};

struct kbase_uk_find_cpu_offset {
	union uk_header header;
	/* IN */
	u64 gpu_addr;
	u64 cpu_addr;
	u64 size;
	/* OUT */
	u64 offset;
};

#define KBASE_GET_VERSION_BUFFER_SIZE 64
struct kbase_uk_get_ddk_version {
	union uk_header header;
	/* OUT */
	char version_buffer[KBASE_GET_VERSION_BUFFER_SIZE];
	u32 version_string_size;
	u32 padding;
	u32 rk_version;
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
struct kbase_exported_test_data {
	u64 test_addr[TEST_ADDR_COUNT];		/**< memory address */
	u32 test_addr_pages[TEST_ADDR_COUNT];		/**<  memory size in pages */
	union kbase_pointer kctx;				/**<  base context created by process */
	union kbase_pointer mm;				/**< pointer to process address space */
	u8 buffer1[KBASE_TEST_BUFFER_SIZE];   /**<  unit test defined parameter */
	u8 buffer2[KBASE_TEST_BUFFER_SIZE];   /**<  unit test defined parameter */
};

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

#ifdef BASE_LEGACY_UK8_SUPPORT
struct kbase_uk_keep_gpu_powered {
	union uk_header header;
	u32       enabled;
	u32       padding;
};
#endif /* BASE_LEGACY_UK8_SUPPORT */

struct kbase_uk_profiling_controls {
	union uk_header header;
	u32 profiling_controls[FBDUMP_CONTROL_MAX];
};

struct kbase_uk_debugfs_mem_profile_add {
	union uk_header header;
	u32 len;
	u32 padding;
	union kbase_pointer buf;
};

struct kbase_uk_context_id {
	union uk_header header;
	/* OUT */
	int id;
};

#if (defined(MALI_MIPE_ENABLED) && MALI_MIPE_ENABLED) || \
	!defined(MALI_MIPE_ENABLED)
/**
 * struct kbase_uk_tlstream_acquire - User/Kernel space data exchange structure
 * @header: UK structure header
 * @flags:  timeline stream flags
 * @fd:     timeline stream file descriptor
 *
 * This structure is used when performing a call to acquire kernel side timeline
 * stream file descriptor.
 */
struct kbase_uk_tlstream_acquire {
	union uk_header header;
	/* IN */
	u32 flags;
	/* OUT */
	s32  fd;
};

/**
 * struct kbase_uk_tlstream_acquire_v10_4 - User/Kernel space data exchange
 *                                          structure
 * @header: UK structure header
 * @fd:     timeline stream file descriptor
 *
 * This structure is used when performing a call to acquire kernel side timeline
 * stream file descriptor.
 */
struct kbase_uk_tlstream_acquire_v10_4 {
	union uk_header header;
	/* IN */
	/* OUT */
	s32  fd;
};

/**
 * struct kbase_uk_tlstream_flush - User/Kernel space data exchange structure
 * @header: UK structure header
 *
 * This structure is used when performing a call to flush kernel side
 * timeline streams.
 */
struct kbase_uk_tlstream_flush {
	union uk_header header;
	/* IN */
	/* OUT */
};

#if MALI_UNIT_TEST
/**
 * struct kbase_uk_tlstream_test - User/Kernel space data exchange structure
 * @header:    UK structure header
 * @tpw_count: number of trace point writers in each context
 * @msg_delay: time delay between tracepoints from one writer in milliseconds
 * @msg_count: number of trace points written by one writer
 * @aux_msg:   if non-zero aux messages will be included
 *
 * This structure is used when performing a call to start timeline stream test
 * embedded in kernel.
 */
struct kbase_uk_tlstream_test {
	union uk_header header;
	/* IN */
	u32 tpw_count;
	u32 msg_delay;
	u32 msg_count;
	u32 aux_msg;
	/* OUT */
};

/**
 * struct kbase_uk_tlstream_stats - User/Kernel space data exchange structure
 * @header:          UK structure header
 * @bytes_collected: number of bytes read by user
 * @bytes_generated: number of bytes generated by tracepoints
 *
 * This structure is used when performing a call to obtain timeline stream
 * statistics.
 */
struct kbase_uk_tlstream_stats {
	union uk_header header; /**< UK structure header. */
	/* IN */
	/* OUT */
	u32 bytes_collected;
	u32 bytes_generated;
};
#endif /* MALI_UNIT_TEST */
#endif /* MALI_MIPE_ENABLED */

/**
 * struct struct kbase_uk_prfcnt_value for the KBASE_FUNC_SET_PRFCNT_VALUES ioctl
 * @header:          UK structure header
 * @data:            Counter samples for the dummy model
 * @size:............Size of the counter sample data
 */
struct kbase_uk_prfcnt_values {
	union uk_header header;
	/* IN */
	u32 *data;
	u32 size;
};

/**
 * struct kbase_uk_soft_event_update - User/Kernel space data exchange structure
 * @header:     UK structure header
 * @evt:        the GPU address containing the event
 * @new_status: the new event status, must be either BASE_JD_SOFT_EVENT_SET or
 *              BASE_JD_SOFT_EVENT_RESET
 * @flags:      reserved for future uses, must be set to 0
 *
 * This structure is used to update the status of a software event. If the
 * event's status is set to BASE_JD_SOFT_EVENT_SET, any job currently waiting
 * on this event will complete.
 */
struct kbase_uk_soft_event_update {
	union uk_header header;
	/* IN */
	u64 evt;
	u32 new_status;
	u32 flags;
};

/**
 * struct kbase_uk_mem_jit_init - User/Kernel space data exchange structure
 * @header:     UK structure header
 * @va_pages:   Number of virtual pages required for JIT
 *
 * This structure is used when requesting initialization of JIT.
 */
struct kbase_uk_mem_jit_init {
	union uk_header header;
	/* IN */
	u64 va_pages;
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

	KBASE_FUNC_GPU_PROPS_REG_DUMP = (UK_FUNC_ID + 14),

	KBASE_FUNC_FIND_CPU_OFFSET = (UK_FUNC_ID + 15),

	KBASE_FUNC_GET_VERSION = (UK_FUNC_ID + 16),
	KBASE_FUNC_SET_FLAGS = (UK_FUNC_ID + 18),

	KBASE_FUNC_SET_TEST_DATA = (UK_FUNC_ID + 19),
	KBASE_FUNC_INJECT_ERROR = (UK_FUNC_ID + 20),
	KBASE_FUNC_MODEL_CONTROL = (UK_FUNC_ID + 21),

#ifdef BASE_LEGACY_UK8_SUPPORT
	KBASE_FUNC_KEEP_GPU_POWERED = (UK_FUNC_ID + 22),
#endif /* BASE_LEGACY_UK8_SUPPORT */

	KBASE_FUNC_FENCE_VALIDATE = (UK_FUNC_ID + 23),
	KBASE_FUNC_STREAM_CREATE = (UK_FUNC_ID + 24),
	KBASE_FUNC_GET_PROFILING_CONTROLS = (UK_FUNC_ID + 25),
	KBASE_FUNC_SET_PROFILING_CONTROLS = (UK_FUNC_ID + 26),
					    /* to be used only for testing
					    * purposes, otherwise these controls
					    * are set through gator API */

	KBASE_FUNC_DEBUGFS_MEM_PROFILE_ADD = (UK_FUNC_ID + 27),
	KBASE_FUNC_JOB_SUBMIT = (UK_FUNC_ID + 28),
	KBASE_FUNC_DISJOINT_QUERY = (UK_FUNC_ID + 29),

	KBASE_FUNC_GET_CONTEXT_ID = (UK_FUNC_ID + 31),

#if (defined(MALI_MIPE_ENABLED) && MALI_MIPE_ENABLED) || \
	!defined(MALI_MIPE_ENABLED)
	KBASE_FUNC_TLSTREAM_ACQUIRE_V10_4 = (UK_FUNC_ID + 32),
#if MALI_UNIT_TEST
	KBASE_FUNC_TLSTREAM_TEST = (UK_FUNC_ID + 33),
	KBASE_FUNC_TLSTREAM_STATS = (UK_FUNC_ID + 34),
#endif /* MALI_UNIT_TEST */
	KBASE_FUNC_TLSTREAM_FLUSH = (UK_FUNC_ID + 35),
#endif /* MALI_MIPE_ENABLED */

	KBASE_FUNC_HWCNT_READER_SETUP = (UK_FUNC_ID + 36),

#ifdef SUPPORT_MALI_NO_MALI
	KBASE_FUNC_SET_PRFCNT_VALUES = (UK_FUNC_ID + 37),
#endif

	KBASE_FUNC_SOFT_EVENT_UPDATE = (UK_FUNC_ID + 38),

	KBASE_FUNC_MEM_JIT_INIT = (UK_FUNC_ID + 39),

#if (defined(MALI_MIPE_ENABLED) && MALI_MIPE_ENABLED) || \
	!defined(MALI_MIPE_ENABLED)
	KBASE_FUNC_TLSTREAM_ACQUIRE = (UK_FUNC_ID + 40),
#endif /* MALI_MIPE_ENABLED */

	KBASE_FUNC_MAX
};

#endif				/* _KBASE_UKU_H_ */

