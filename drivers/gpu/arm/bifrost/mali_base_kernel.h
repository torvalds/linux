/*
 *
 * (C) COPYRIGHT 2010-2019 ARM Limited. All rights reserved.
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



/*
 * Base structures shared with the kernel.
 */

#ifndef _BASE_KERNEL_H_
#define _BASE_KERNEL_H_

typedef struct base_mem_handle {
	struct {
		u64 handle;
	} basep;
} base_mem_handle;

#include "mali_base_mem_priv.h"
#include "mali_midg_coherency.h"
#include "mali_kbase_gpu_id.h"

/*
 * Dependency stuff, keep it private for now. May want to expose it if
 * we decide to make the number of semaphores a configurable
 * option.
 */
#define BASE_JD_ATOM_COUNT              256

/* Set/reset values for a software event */
#define BASE_JD_SOFT_EVENT_SET             ((unsigned char)1)
#define BASE_JD_SOFT_EVENT_RESET           ((unsigned char)0)

#define BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS 4

#define BASE_MAX_COHERENT_GROUPS 16

#if defined CDBG_ASSERT
#define LOCAL_ASSERT CDBG_ASSERT
#elif defined KBASE_DEBUG_ASSERT
#define LOCAL_ASSERT KBASE_DEBUG_ASSERT
#else
#error assert macro not defined!
#endif

#if defined(PAGE_MASK) && defined(PAGE_SHIFT)
#define LOCAL_PAGE_SHIFT PAGE_SHIFT
#define LOCAL_PAGE_LSB ~PAGE_MASK
#else
#include <osu/mali_osu.h>

#if defined OSU_CONFIG_CPU_PAGE_SIZE_LOG2
#define LOCAL_PAGE_SHIFT OSU_CONFIG_CPU_PAGE_SIZE_LOG2
#define LOCAL_PAGE_LSB ((1ul << OSU_CONFIG_CPU_PAGE_SIZE_LOG2) - 1)
#else
#error Failed to find page size
#endif
#endif

/**
 * @addtogroup base_user_api User-side Base APIs
 * @{
 */

/**
 * @addtogroup base_user_api_memory User-side Base Memory APIs
 * @{
 */

/* Physical memory group ID for normal usage.
 */
#define BASE_MEM_GROUP_DEFAULT (0)

/* Number of physical memory groups.
 */
#define BASE_MEM_GROUP_COUNT (16)

/**
 * typedef base_mem_alloc_flags - Memory allocation, access/hint flags.
 *
 * A combination of MEM_PROT/MEM_HINT flags must be passed to each allocator
 * in order to determine the best cache policy. Some combinations are
 * of course invalid (e.g. MEM_PROT_CPU_WR | MEM_HINT_CPU_RD),
 * which defines a write-only region on the CPU side, which is
 * heavily read by the CPU...
 * Other flags are only meaningful to a particular allocator.
 * More flags can be added to this list, as long as they don't clash
 * (see BASE_MEM_FLAGS_NR_BITS for the number of the first free bit).
 */
typedef u32 base_mem_alloc_flags;

/* Memory allocation, access/hint flags.
 *
 * See base_mem_alloc_flags.
 */

/* IN */
/* Read access CPU side
 */
#define BASE_MEM_PROT_CPU_RD ((base_mem_alloc_flags)1 << 0)

/* Write access CPU side
 */
#define BASE_MEM_PROT_CPU_WR ((base_mem_alloc_flags)1 << 1)

/* Read access GPU side
 */
#define BASE_MEM_PROT_GPU_RD ((base_mem_alloc_flags)1 << 2)

/* Write access GPU side
 */
#define BASE_MEM_PROT_GPU_WR ((base_mem_alloc_flags)1 << 3)

/* Execute allowed on the GPU side
 */
#define BASE_MEM_PROT_GPU_EX ((base_mem_alloc_flags)1 << 4)

/* Will be permanently mapped in kernel space.
 * Flag is only allowed on allocations originating from kbase.
 */
#define BASEP_MEM_PERMANENT_KERNEL_MAPPING ((base_mem_alloc_flags)1 << 5)

/* The allocation will completely reside within the same 4GB chunk in the GPU
 * virtual space.
 * Since this flag is primarily required only for the TLS memory which will
 * not be used to contain executable code and also not used for Tiler heap,
 * it can't be used along with BASE_MEM_PROT_GPU_EX and TILER_ALIGN_TOP flags.
 */
#define BASE_MEM_GPU_VA_SAME_4GB_PAGE ((base_mem_alloc_flags)1 << 6)

/* Userspace is not allowed to free this memory.
 * Flag is only allowed on allocations originating from kbase.
 */
#define BASEP_MEM_NO_USER_FREE ((base_mem_alloc_flags)1 << 7)

#define BASE_MEM_RESERVED_BIT_8 ((base_mem_alloc_flags)1 << 8)

/* Grow backing store on GPU Page Fault
 */
#define BASE_MEM_GROW_ON_GPF ((base_mem_alloc_flags)1 << 9)

/* Page coherence Outer shareable, if available
 */
#define BASE_MEM_COHERENT_SYSTEM ((base_mem_alloc_flags)1 << 10)

/* Page coherence Inner shareable
 */
#define BASE_MEM_COHERENT_LOCAL ((base_mem_alloc_flags)1 << 11)

/* Should be cached on the CPU
 */
#define BASE_MEM_CACHED_CPU ((base_mem_alloc_flags)1 << 12)

/* IN/OUT */
/* Must have same VA on both the GPU and the CPU
 */
#define BASE_MEM_SAME_VA ((base_mem_alloc_flags)1 << 13)

/* OUT */
/* Must call mmap to acquire a GPU address for the alloc
 */
#define BASE_MEM_NEED_MMAP ((base_mem_alloc_flags)1 << 14)

/* IN */
/* Page coherence Outer shareable, required.
 */
#define BASE_MEM_COHERENT_SYSTEM_REQUIRED ((base_mem_alloc_flags)1 << 15)

/* Protected memory
 */
#define BASE_MEM_PROTECTED ((base_mem_alloc_flags)1 << 16)

/* Not needed physical memory
 */
#define BASE_MEM_DONT_NEED ((base_mem_alloc_flags)1 << 17)

/* Must use shared CPU/GPU zone (SAME_VA zone) but doesn't require the
 * addresses to be the same
 */
#define BASE_MEM_IMPORT_SHARED ((base_mem_alloc_flags)1 << 18)

/**
 * Bit 19 is reserved.
 *
 * Do not remove, use the next unreserved bit for new flags
 */
#define BASE_MEM_RESERVED_BIT_19 ((base_mem_alloc_flags)1 << 19)
#define BASE_MEM_MAYBE_RESERVED_BIT_19 BASE_MEM_RESERVED_BIT_19

/**
 * Memory starting from the end of the initial commit is aligned to 'extent'
 * pages, where 'extent' must be a power of 2 and no more than
 * BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES
 */
#define BASE_MEM_TILER_ALIGN_TOP ((base_mem_alloc_flags)1 << 20)

/* Should be uncached on the GPU, will work only for GPUs using AARCH64 mmu mode.
 * Some components within the GPU might only be able to access memory that is
 * GPU cacheable. Refer to the specific GPU implementation for more details.
 * The 3 shareability flags will be ignored for GPU uncached memory.
 * If used while importing USER_BUFFER type memory, then the import will fail
 * if the memory is not aligned to GPU and CPU cache line width.
 */
#define BASE_MEM_UNCACHED_GPU ((base_mem_alloc_flags)1 << 21)

/*
 * Bits [22:25] for group_id (0~15).
 *
 * base_mem_group_id_set() should be used to pack a memory group ID into a
 * base_mem_alloc_flags value instead of accessing the bits directly.
 * base_mem_group_id_get() should be used to extract the memory group ID from
 * a base_mem_alloc_flags value.
 */
#define BASEP_MEM_GROUP_ID_SHIFT 22
#define BASE_MEM_GROUP_ID_MASK \
	((base_mem_alloc_flags)0xF << BASEP_MEM_GROUP_ID_SHIFT)

/**
 * Number of bits used as flags for base memory management
 *
 * Must be kept in sync with the base_mem_alloc_flags flags
 */
#define BASE_MEM_FLAGS_NR_BITS 26

/* A mask for all output bits, excluding IN/OUT bits.
 */
#define BASE_MEM_FLAGS_OUTPUT_MASK BASE_MEM_NEED_MMAP

/* A mask for all input bits, including IN/OUT bits.
 */
#define BASE_MEM_FLAGS_INPUT_MASK \
	(((1 << BASE_MEM_FLAGS_NR_BITS) - 1) & ~BASE_MEM_FLAGS_OUTPUT_MASK)

/**
 * base_mem_group_id_get() - Get group ID from flags
 * @flags: Flags to pass to base_mem_alloc
 *
 * This inline function extracts the encoded group ID from flags
 * and converts it into numeric value (0~15).
 *
 * Return: group ID(0~15) extracted from the parameter
 */
static inline int base_mem_group_id_get(base_mem_alloc_flags flags)
{
	LOCAL_ASSERT((flags & ~BASE_MEM_FLAGS_INPUT_MASK) == 0);
	return (int)((flags & BASE_MEM_GROUP_ID_MASK) >>
			BASEP_MEM_GROUP_ID_SHIFT);
}

/**
 * base_mem_group_id_set() - Set group ID into base_mem_alloc_flags
 * @id: group ID(0~15) you want to encode
 *
 * This inline function encodes specific group ID into base_mem_alloc_flags.
 * Parameter 'id' should lie in-between 0 to 15.
 *
 * Return: base_mem_alloc_flags with the group ID (id) encoded
 *
 * The return value can be combined with other flags against base_mem_alloc
 * to identify a specific memory group.
 */
static inline base_mem_alloc_flags base_mem_group_id_set(int id)
{
	LOCAL_ASSERT(id >= 0);
	LOCAL_ASSERT(id < BASE_MEM_GROUP_COUNT);

	return ((base_mem_alloc_flags)id << BASEP_MEM_GROUP_ID_SHIFT) &
		BASE_MEM_GROUP_ID_MASK;
}

/* A mask for all the flags which are modifiable via the base_mem_set_flags
 * interface.
 */
#define BASE_MEM_FLAGS_MODIFIABLE \
	(BASE_MEM_DONT_NEED | BASE_MEM_COHERENT_SYSTEM | \
	 BASE_MEM_COHERENT_LOCAL)


/* A mask of all currently reserved flags
 */
#define BASE_MEM_FLAGS_RESERVED \
	(BASE_MEM_RESERVED_BIT_8 | BASE_MEM_MAYBE_RESERVED_BIT_19)

/* A mask of all the flags which are only valid for allocations within kbase,
 * and may not be passed from user space.
 */
#define BASEP_MEM_FLAGS_KERNEL_ONLY \
	(BASEP_MEM_PERMANENT_KERNEL_MAPPING | BASEP_MEM_NO_USER_FREE)

/* A mask of all the flags that can be returned via the base_mem_get_flags()
 * interface.
 */
#define BASE_MEM_FLAGS_QUERYABLE \
	(BASE_MEM_FLAGS_INPUT_MASK & ~(BASE_MEM_SAME_VA | \
		BASE_MEM_COHERENT_SYSTEM_REQUIRED | BASE_MEM_DONT_NEED | \
		BASE_MEM_IMPORT_SHARED | BASE_MEM_FLAGS_RESERVED | \
		BASEP_MEM_FLAGS_KERNEL_ONLY))

/**
 * enum base_mem_import_type - Memory types supported by @a base_mem_import
 *
 * @BASE_MEM_IMPORT_TYPE_INVALID: Invalid type
 * @BASE_MEM_IMPORT_TYPE_UMM: UMM import. Handle type is a file descriptor (int)
 * @BASE_MEM_IMPORT_TYPE_USER_BUFFER: User buffer import. Handle is a
 * base_mem_import_user_buffer
 *
 * Each type defines what the supported handle type is.
 *
 * If any new type is added here ARM must be contacted
 * to allocate a numeric value for it.
 * Do not just add a new type without synchronizing with ARM
 * as future releases from ARM might include other new types
 * which could clash with your custom types.
 */
typedef enum base_mem_import_type {
	BASE_MEM_IMPORT_TYPE_INVALID = 0,
	/**
	 * Import type with value 1 is deprecated.
	 */
	BASE_MEM_IMPORT_TYPE_UMM = 2,
	BASE_MEM_IMPORT_TYPE_USER_BUFFER = 3
} base_mem_import_type;

/**
 * struct base_mem_import_user_buffer - Handle of an imported user buffer
 *
 * @ptr:	address of imported user buffer
 * @length:	length of imported user buffer in bytes
 *
 * This structure is used to represent a handle of an imported user buffer.
 */

struct base_mem_import_user_buffer {
	u64 ptr;
	u64 length;
};

/**
 * @brief Invalid memory handle.
 *
 * Return value from functions returning @ref base_mem_handle on error.
 *
 * @warning @ref base_mem_handle_new_invalid must be used instead of this macro
 *          in C++ code or other situations where compound literals cannot be used.
 */
#define BASE_MEM_INVALID_HANDLE ((base_mem_handle) { {BASEP_MEM_INVALID_HANDLE} })

/**
 * @brief Special write-alloc memory handle.
 *
 * A special handle is used to represent a region where a special page is mapped
 * with a write-alloc cache setup, typically used when the write result of the
 * GPU isn't needed, but the GPU must write anyway.
 *
 * @warning @ref base_mem_handle_new_write_alloc must be used instead of this macro
 *          in C++ code or other situations where compound literals cannot be used.
 */
#define BASE_MEM_WRITE_ALLOC_PAGES_HANDLE ((base_mem_handle) { {BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE} })

#define BASEP_MEM_INVALID_HANDLE               (0ull  << 12)
#define BASE_MEM_MMU_DUMP_HANDLE               (1ull  << 12)
#define BASE_MEM_TRACE_BUFFER_HANDLE           (2ull  << 12)
#define BASE_MEM_MAP_TRACKING_HANDLE           (3ull  << 12)
#define BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE     (4ull  << 12)
/* reserved handles ..-47<<PAGE_SHIFT> for future special handles */
#define BASE_MEM_COOKIE_BASE                   (64ul  << 12)
#define BASE_MEM_FIRST_FREE_ADDRESS            ((BITS_PER_LONG << 12) + \
						BASE_MEM_COOKIE_BASE)

/* Mask to detect 4GB boundary alignment */
#define BASE_MEM_MASK_4GB  0xfffff000UL
/* Mask to detect 4GB boundary (in page units) alignment */
#define BASE_MEM_PFN_MASK_4GB  (BASE_MEM_MASK_4GB >> LOCAL_PAGE_SHIFT)

/**
 * Limit on the 'extent' parameter for an allocation with the
 * BASE_MEM_TILER_ALIGN_TOP flag set
 *
 * This is the same as the maximum limit for a Buffer Descriptor's chunk size
 */
#define BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES_LOG2 \
		(21u - (LOCAL_PAGE_SHIFT))
#define BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES \
		(1ull << (BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES_LOG2))

/* Bit mask of cookies used for for memory allocation setup */
#define KBASE_COOKIE_MASK  ~1UL /* bit 0 is reserved */

/* Maximum size allowed in a single KBASE_IOCTL_MEM_ALLOC call */
#define KBASE_MEM_ALLOC_MAX_SIZE ((8ull << 30) >> PAGE_SHIFT) /* 8 GB */


/**
 * @addtogroup base_user_api_memory_defered User-side Base Defered Memory Coherency APIs
 * @{
 */

/**
 * @brief a basic memory operation (sync-set).
 *
 * The content of this structure is private, and should only be used
 * by the accessors.
 */
typedef struct base_syncset {
	struct basep_syncset basep_sset;
} base_syncset;

/** @} end group base_user_api_memory_defered */

/**
 * Handle to represent imported memory object.
 * Simple opague handle to imported memory, can't be used
 * with anything but base_external_resource_init to bind to an atom.
 */
typedef struct base_import_handle {
	struct {
		u64 handle;
	} basep;
} base_import_handle;

/** @} end group base_user_api_memory */

/**
 * @addtogroup base_user_api_job_dispatch User-side Base Job Dispatcher APIs
 * @{
 */

typedef int platform_fence_type;
#define INVALID_PLATFORM_FENCE ((platform_fence_type)-1)

/**
 * Base stream handle.
 *
 * References an underlying base stream object.
 */
typedef struct base_stream {
	struct {
		int fd;
	} basep;
} base_stream;

/**
 * Base fence handle.
 *
 * References an underlying base fence object.
 */
typedef struct base_fence {
	struct {
		int fd;
		int stream_fd;
	} basep;
} base_fence;

/**
 * @brief Per-job data
 *
 * This structure is used to store per-job data, and is completely unused
 * by the Base driver. It can be used to store things such as callback
 * function pointer, data to handle job completion. It is guaranteed to be
 * untouched by the Base driver.
 */
typedef struct base_jd_udata {
	u64 blob[2];	 /**< per-job data array */
} base_jd_udata;

/**
 * @brief Memory aliasing info
 *
 * Describes a memory handle to be aliased.
 * A subset of the handle can be chosen for aliasing, given an offset and a
 * length.
 * A special handle BASE_MEM_WRITE_ALLOC_PAGES_HANDLE is used to represent a
 * region where a special page is mapped with a write-alloc cache setup,
 * typically used when the write result of the GPU isn't needed, but the GPU
 * must write anyway.
 *
 * Offset and length are specified in pages.
 * Offset must be within the size of the handle.
 * Offset+length must not overrun the size of the handle.
 *
 * @handle Handle to alias, can be BASE_MEM_WRITE_ALLOC_PAGES_HANDLE
 * @offset Offset within the handle to start aliasing from, in pages.
 *         Not used with BASE_MEM_WRITE_ALLOC_PAGES_HANDLE.
 * @length Length to alias, in pages. For BASE_MEM_WRITE_ALLOC_PAGES_HANDLE
 *         specifies the number of times the special page is needed.
 */
struct base_mem_aliasing_info {
	base_mem_handle handle;
	u64 offset;
	u64 length;
};

/**
 * Similar to BASE_MEM_TILER_ALIGN_TOP, memory starting from the end of the
 * initial commit is aligned to 'extent' pages, where 'extent' must be a power
 * of 2 and no more than BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES
 */
#define BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP  (1 << 0)

/**
 * struct base_jit_alloc_info - Structure which describes a JIT allocation
 *                              request.
 * @gpu_alloc_addr:             The GPU virtual address to write the JIT
 *                              allocated GPU virtual address to.
 * @va_pages:                   The minimum number of virtual pages required.
 * @commit_pages:               The minimum number of physical pages which
 *                              should back the allocation.
 * @extent:                     Granularity of physical pages to grow the
 *                              allocation by during a fault.
 * @id:                         Unique ID provided by the caller, this is used
 *                              to pair allocation and free requests.
 *                              Zero is not a valid value.
 * @bin_id:                     The JIT allocation bin, used in conjunction with
 *                              @max_allocations to limit the number of each
 *                              type of JIT allocation.
 * @max_allocations:            The maximum number of allocations allowed within
 *                              the bin specified by @bin_id. Should be the same
 *                              for all JIT allocations within the same bin.
 * @flags:                      flags specifying the special requirements for
 *                              the JIT allocation.
 * @padding:                    Expansion space - should be initialised to zero
 * @usage_id:                   A hint about which allocation should be reused.
 *                              The kernel should attempt to use a previous
 *                              allocation with the same usage_id
 */
struct base_jit_alloc_info {
	u64 gpu_alloc_addr;
	u64 va_pages;
	u64 commit_pages;
	u64 extent;
	u8 id;
	u8 bin_id;
	u8 max_allocations;
	u8 flags;
	u8 padding[2];
	u16 usage_id;
};

/**
 * @brief Job dependency type.
 *
 * A flags field will be inserted into the atom structure to specify whether a dependency is a data or
 * ordering dependency (by putting it before/after 'core_req' in the structure it should be possible to add without
 * changing the structure size).
 * When the flag is set for a particular dependency to signal that it is an ordering only dependency then
 * errors will not be propagated.
 */
typedef u8 base_jd_dep_type;


#define BASE_JD_DEP_TYPE_INVALID  (0)       /**< Invalid dependency */
#define BASE_JD_DEP_TYPE_DATA     (1U << 0) /**< Data dependency */
#define BASE_JD_DEP_TYPE_ORDER    (1U << 1) /**< Order dependency */

/**
 * @brief Job chain hardware requirements.
 *
 * A job chain must specify what GPU features it needs to allow the
 * driver to schedule the job correctly.  By not specifying the
 * correct settings can/will cause an early job termination.  Multiple
 * values can be ORed together to specify multiple requirements.
 * Special case is ::BASE_JD_REQ_DEP, which is used to express complex
 * dependencies, and that doesn't execute anything on the hardware.
 */
typedef u32 base_jd_core_req;

/* Requirements that come from the HW */

/**
 * No requirement, dependency only
 */
#define BASE_JD_REQ_DEP ((base_jd_core_req)0)

/**
 * Requires fragment shaders
 */
#define BASE_JD_REQ_FS  ((base_jd_core_req)1 << 0)

/**
 * Requires compute shaders
 * This covers any of the following Midgard Job types:
 * - Vertex Shader Job
 * - Geometry Shader Job
 * - An actual Compute Shader Job
 *
 * Compare this with @ref BASE_JD_REQ_ONLY_COMPUTE, which specifies that the
 * job is specifically just the "Compute Shader" job type, and not the "Vertex
 * Shader" nor the "Geometry Shader" job type.
 */
#define BASE_JD_REQ_CS  ((base_jd_core_req)1 << 1)
#define BASE_JD_REQ_T   ((base_jd_core_req)1 << 2)   /**< Requires tiling */
#define BASE_JD_REQ_CF  ((base_jd_core_req)1 << 3)   /**< Requires cache flushes */
#define BASE_JD_REQ_V   ((base_jd_core_req)1 << 4)   /**< Requires value writeback */

/* SW-only requirements - the HW does not expose these as part of the job slot capabilities */

/* Requires fragment job with AFBC encoding */
#define BASE_JD_REQ_FS_AFBC  ((base_jd_core_req)1 << 13)

/**
 * SW-only requirement: coalesce completion events.
 * If this bit is set then completion of this atom will not cause an event to
 * be sent to userspace, whether successful or not; completion events will be
 * deferred until an atom completes which does not have this bit set.
 *
 * This bit may not be used in combination with BASE_JD_REQ_EXTERNAL_RESOURCES.
 */
#define BASE_JD_REQ_EVENT_COALESCE ((base_jd_core_req)1 << 5)

/**
 * SW Only requirement: the job chain requires a coherent core group. We don't
 * mind which coherent core group is used.
 */
#define BASE_JD_REQ_COHERENT_GROUP  ((base_jd_core_req)1 << 6)

/**
 * SW Only requirement: The performance counters should be enabled only when
 * they are needed, to reduce power consumption.
 */

#define BASE_JD_REQ_PERMON               ((base_jd_core_req)1 << 7)

/**
 * SW Only requirement: External resources are referenced by this atom.
 * When external resources are referenced no syncsets can be bundled with the atom
 * but should instead be part of a NULL jobs inserted into the dependency tree.
 * The first pre_dep object must be configured for the external resouces to use,
 * the second pre_dep object can be used to create other dependencies.
 *
 * This bit may not be used in combination with BASE_JD_REQ_EVENT_COALESCE and
 * BASE_JD_REQ_SOFT_EVENT_WAIT.
 */
#define BASE_JD_REQ_EXTERNAL_RESOURCES   ((base_jd_core_req)1 << 8)

/**
 * SW Only requirement: Software defined job. Jobs with this bit set will not be submitted
 * to the hardware but will cause some action to happen within the driver
 */
#define BASE_JD_REQ_SOFT_JOB        ((base_jd_core_req)1 << 9)

#define BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME      (BASE_JD_REQ_SOFT_JOB | 0x1)
#define BASE_JD_REQ_SOFT_FENCE_TRIGGER          (BASE_JD_REQ_SOFT_JOB | 0x2)
#define BASE_JD_REQ_SOFT_FENCE_WAIT             (BASE_JD_REQ_SOFT_JOB | 0x3)

/* 0x4 RESERVED for now */

/**
 * SW only requirement: event wait/trigger job.
 *
 * - BASE_JD_REQ_SOFT_EVENT_WAIT: this job will block until the event is set.
 * - BASE_JD_REQ_SOFT_EVENT_SET: this job sets the event, thus unblocks the
 *   other waiting jobs. It completes immediately.
 * - BASE_JD_REQ_SOFT_EVENT_RESET: this job resets the event, making it
 *   possible for other jobs to wait upon. It completes immediately.
 */
#define BASE_JD_REQ_SOFT_EVENT_WAIT             (BASE_JD_REQ_SOFT_JOB | 0x5)
#define BASE_JD_REQ_SOFT_EVENT_SET              (BASE_JD_REQ_SOFT_JOB | 0x6)
#define BASE_JD_REQ_SOFT_EVENT_RESET            (BASE_JD_REQ_SOFT_JOB | 0x7)

#define BASE_JD_REQ_SOFT_DEBUG_COPY             (BASE_JD_REQ_SOFT_JOB | 0x8)

/**
 * SW only requirement: Just In Time allocation
 *
 * This job requests a single or multiple JIT allocations through a list
 * of @base_jit_alloc_info structure which is passed via the jc element of
 * the atom. The number of @base_jit_alloc_info structures present in the
 * list is passed via the nr_extres element of the atom
 *
 * It should be noted that the id entry in @base_jit_alloc_info must not
 * be reused until it has been released via @BASE_JD_REQ_SOFT_JIT_FREE.
 *
 * Should this soft job fail it is expected that a @BASE_JD_REQ_SOFT_JIT_FREE
 * soft job to free the JIT allocation is still made.
 *
 * The job will complete immediately.
 */
#define BASE_JD_REQ_SOFT_JIT_ALLOC              (BASE_JD_REQ_SOFT_JOB | 0x9)
/**
 * SW only requirement: Just In Time free
 *
 * This job requests a single or multiple JIT allocations created by
 * @BASE_JD_REQ_SOFT_JIT_ALLOC to be freed. The ID list of the JIT
 * allocations is passed via the jc element of the atom.
 *
 * The job will complete immediately.
 */
#define BASE_JD_REQ_SOFT_JIT_FREE               (BASE_JD_REQ_SOFT_JOB | 0xa)

/**
 * SW only requirement: Map external resource
 *
 * This job requests external resource(s) are mapped once the dependencies
 * of the job have been satisfied. The list of external resources are
 * passed via the jc element of the atom which is a pointer to a
 * @base_external_resource_list.
 */
#define BASE_JD_REQ_SOFT_EXT_RES_MAP            (BASE_JD_REQ_SOFT_JOB | 0xb)
/**
 * SW only requirement: Unmap external resource
 *
 * This job requests external resource(s) are unmapped once the dependencies
 * of the job has been satisfied. The list of external resources are
 * passed via the jc element of the atom which is a pointer to a
 * @base_external_resource_list.
 */
#define BASE_JD_REQ_SOFT_EXT_RES_UNMAP          (BASE_JD_REQ_SOFT_JOB | 0xc)

/**
 * HW Requirement: Requires Compute shaders (but not Vertex or Geometry Shaders)
 *
 * This indicates that the Job Chain contains Midgard Jobs of the 'Compute Shaders' type.
 *
 * In contrast to @ref BASE_JD_REQ_CS, this does \b not indicate that the Job
 * Chain contains 'Geometry Shader' or 'Vertex Shader' jobs.
 */
#define BASE_JD_REQ_ONLY_COMPUTE    ((base_jd_core_req)1 << 10)

/**
 * HW Requirement: Use the base_jd_atom::device_nr field to specify a
 * particular core group
 *
 * If both @ref BASE_JD_REQ_COHERENT_GROUP and this flag are set, this flag takes priority
 *
 * This is only guaranteed to work for @ref BASE_JD_REQ_ONLY_COMPUTE atoms.
 *
 * If the core availability policy is keeping the required core group turned off, then
 * the job will fail with a @ref BASE_JD_EVENT_PM_EVENT error code.
 */
#define BASE_JD_REQ_SPECIFIC_COHERENT_GROUP ((base_jd_core_req)1 << 11)

/**
 * SW Flag: If this bit is set then the successful completion of this atom
 * will not cause an event to be sent to userspace
 */
#define BASE_JD_REQ_EVENT_ONLY_ON_FAILURE   ((base_jd_core_req)1 << 12)

/**
 * SW Flag: If this bit is set then completion of this atom will not cause an
 * event to be sent to userspace, whether successful or not.
 */
#define BASEP_JD_REQ_EVENT_NEVER ((base_jd_core_req)1 << 14)

/**
 * SW Flag: Skip GPU cache clean and invalidation before starting a GPU job.
 *
 * If this bit is set then the GPU's cache will not be cleaned and invalidated
 * until a GPU job starts which does not have this bit set or a job completes
 * which does not have the @ref BASE_JD_REQ_SKIP_CACHE_END bit set. Do not use if
 * the CPU may have written to memory addressed by the job since the last job
 * without this bit set was submitted.
 */
#define BASE_JD_REQ_SKIP_CACHE_START ((base_jd_core_req)1 << 15)

/**
 * SW Flag: Skip GPU cache clean and invalidation after a GPU job completes.
 *
 * If this bit is set then the GPU's cache will not be cleaned and invalidated
 * until a GPU job completes which does not have this bit set or a job starts
 * which does not have the @ref BASE_JD_REQ_SKIP_CACHE_START bti set. Do not use if
 * the CPU may read from or partially overwrite memory addressed by the job
 * before the next job without this bit set completes.
 */
#define BASE_JD_REQ_SKIP_CACHE_END ((base_jd_core_req)1 << 16)

/**
 * Request the atom be executed on a specific job slot.
 *
 * When this flag is specified, it takes precedence over any existing job slot
 * selection logic.
 */
#define BASE_JD_REQ_JOB_SLOT ((base_jd_core_req)1 << 17)

/**
 * These requirement bits are currently unused in base_jd_core_req
 */
#define BASEP_JD_REQ_RESERVED \
	(~(BASE_JD_REQ_ATOM_TYPE | BASE_JD_REQ_EXTERNAL_RESOURCES | \
	BASE_JD_REQ_EVENT_ONLY_ON_FAILURE | BASEP_JD_REQ_EVENT_NEVER | \
	BASE_JD_REQ_EVENT_COALESCE | \
	BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP | \
	BASE_JD_REQ_FS_AFBC | BASE_JD_REQ_PERMON | \
	BASE_JD_REQ_SKIP_CACHE_START | BASE_JD_REQ_SKIP_CACHE_END | \
	BASE_JD_REQ_JOB_SLOT))

/**
 * Mask of all bits in base_jd_core_req that control the type of the atom.
 *
 * This allows dependency only atoms to have flags set
 */
#define BASE_JD_REQ_ATOM_TYPE \
	(BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T | BASE_JD_REQ_CF | \
	BASE_JD_REQ_V | BASE_JD_REQ_SOFT_JOB | BASE_JD_REQ_ONLY_COMPUTE)

/**
 * Mask of all bits in base_jd_core_req that control the type of a soft job.
 */
#define BASE_JD_REQ_SOFT_JOB_TYPE (BASE_JD_REQ_SOFT_JOB | 0x1f)

/*
 * Returns non-zero value if core requirements passed define a soft job or
 * a dependency only job.
 */
#define BASE_JD_REQ_SOFT_JOB_OR_DEP(core_req) \
	((core_req & BASE_JD_REQ_SOFT_JOB) || \
	(core_req & BASE_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP)

/*
 * Base Atom priority
 *
 * Only certain priority levels are actually implemented, as specified by the
 * BASE_JD_PRIO_<...> definitions below. It is undefined to use a priority
 * level that is not one of those defined below.
 *
 * Priority levels only affect scheduling after the atoms have had dependencies
 * resolved. For example, a low priority atom that has had its dependencies
 * resolved might run before a higher priority atom that has not had its
 * dependencies resolved.
 *
 * In general, fragment atoms do not affect non-fragment atoms with
 * lower priorities, and vice versa. One exception is that there is only one
 * priority value for each context. So a high-priority (e.g.) fragment atom
 * could increase its context priority, causing its non-fragment atoms to also
 * be scheduled sooner.
 *
 * The atoms are scheduled as follows with respect to their priorities:
 * - Let atoms 'X' and 'Y' be for the same job slot who have dependencies
 *   resolved, and atom 'X' has a higher priority than atom 'Y'
 * - If atom 'Y' is currently running on the HW, then it is interrupted to
 *   allow atom 'X' to run soon after
 * - If instead neither atom 'Y' nor atom 'X' are running, then when choosing
 *   the next atom to run, atom 'X' will always be chosen instead of atom 'Y'
 * - Any two atoms that have the same priority could run in any order with
 *   respect to each other. That is, there is no ordering constraint between
 *   atoms of the same priority.
 *
 * The sysfs file 'js_ctx_scheduling_mode' is used to control how atoms are
 * scheduled between contexts. The default value, 0, will cause higher-priority
 * atoms to be scheduled first, regardless of their context. The value 1 will
 * use a round-robin algorithm when deciding which context's atoms to schedule
 * next, so higher-priority atoms can only preempt lower priority atoms within
 * the same context. See KBASE_JS_SYSTEM_PRIORITY_MODE and
 * KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE for more details.
 */
typedef u8 base_jd_prio;

/* Medium atom priority. This is a priority higher than BASE_JD_PRIO_LOW */
#define BASE_JD_PRIO_MEDIUM  ((base_jd_prio)0)
/* High atom priority. This is a priority higher than BASE_JD_PRIO_MEDIUM and
 * BASE_JD_PRIO_LOW */
#define BASE_JD_PRIO_HIGH    ((base_jd_prio)1)
/* Low atom priority. */
#define BASE_JD_PRIO_LOW     ((base_jd_prio)2)

/* Count of the number of priority levels. This itself is not a valid
 * base_jd_prio setting */
#define BASE_JD_NR_PRIO_LEVELS 3

enum kbase_jd_atom_state {
	/** Atom is not used */
	KBASE_JD_ATOM_STATE_UNUSED,
	/** Atom is queued in JD */
	KBASE_JD_ATOM_STATE_QUEUED,
	/** Atom has been given to JS (is runnable/running) */
	KBASE_JD_ATOM_STATE_IN_JS,
	/** Atom has been completed, but not yet handed back to job dispatcher
	 *  for dependency resolution */
	KBASE_JD_ATOM_STATE_HW_COMPLETED,
	/** Atom has been completed, but not yet handed back to userspace */
	KBASE_JD_ATOM_STATE_COMPLETED
};

typedef u8 base_atom_id; /**< Type big enough to store an atom number in */

struct base_dependency {
	base_atom_id  atom_id;               /**< An atom number */
	base_jd_dep_type dependency_type;    /**< Dependency type */
};

/* This structure has changed since UK 10.2 for which base_jd_core_req was a u16 value.
 * In order to keep the size of the structure same, padding field has been adjusted
 * accordingly and core_req field of a u32 type (to which UK 10.3 base_jd_core_req defines)
 * is added at the end of the structure. Place in the structure previously occupied by u16 core_req
 * is kept but renamed to compat_core_req and as such it can be used in ioctl call for job submission
 * as long as UK 10.2 legacy is supported. Once when this support ends, this field can be left
 * for possible future use. */
typedef struct base_jd_atom_v2 {
	u64 jc;			    /**< job-chain GPU address */
	struct base_jd_udata udata;		    /**< user data */
	u64 extres_list;	    /**< list of external resources */
	u16 nr_extres;			    /**< nr of external resources or JIT allocations */
	u16 compat_core_req;	            /**< core requirements which correspond to the legacy support for UK 10.2 */
	struct base_dependency pre_dep[2];  /**< pre-dependencies, one need to use SETTER function to assign this field,
	this is done in order to reduce possibility of improper assigment of a dependency field */
	base_atom_id atom_number;	    /**< unique number to identify the atom */
	base_jd_prio prio;                  /**< Atom priority. Refer to @ref base_jd_prio for more details */
	u8 device_nr;			    /**< coregroup when BASE_JD_REQ_SPECIFIC_COHERENT_GROUP specified */
	u8 jobslot;			    /**< Job slot to use when BASE_JD_REQ_JOB_SLOT is specified */
	base_jd_core_req core_req;          /**< core requirements */
} base_jd_atom_v2;

typedef enum base_external_resource_access {
	BASE_EXT_RES_ACCESS_SHARED,
	BASE_EXT_RES_ACCESS_EXCLUSIVE
} base_external_resource_access;

typedef struct base_external_resource {
	u64 ext_resource;
} base_external_resource;


/**
 * The maximum number of external resources which can be mapped/unmapped
 * in a single request.
 */
#define BASE_EXT_RES_COUNT_MAX 10

/**
 * struct base_external_resource_list - Structure which describes a list of
 *                                      external resources.
 * @count:                              The number of resources.
 * @ext_res:                            Array of external resources which is
 *                                      sized at allocation time.
 */
struct base_external_resource_list {
	u64 count;
	struct base_external_resource ext_res[1];
};

struct base_jd_debug_copy_buffer {
	u64 address;
	u64 size;
	struct base_external_resource extres;
};

/**
 * @brief Setter for a dependency structure
 *
 * @param[in] dep          The kbase jd atom dependency to be initialized.
 * @param     id           The atom_id to be assigned.
 * @param     dep_type     The dep_type to be assigned.
 *
 */
static inline void base_jd_atom_dep_set(struct base_dependency *dep,
		base_atom_id id, base_jd_dep_type dep_type)
{
	LOCAL_ASSERT(dep != NULL);

	/*
	 * make sure we don't set not allowed combinations
	 * of atom_id/dependency_type.
	 */
	LOCAL_ASSERT((id == 0 && dep_type == BASE_JD_DEP_TYPE_INVALID) ||
			(id > 0 && dep_type != BASE_JD_DEP_TYPE_INVALID));

	dep->atom_id = id;
	dep->dependency_type = dep_type;
}

/**
 * @brief Make a copy of a dependency structure
 *
 * @param[in,out] dep          The kbase jd atom dependency to be written.
 * @param[in]     from         The dependency to make a copy from.
 *
 */
static inline void base_jd_atom_dep_copy(struct base_dependency *dep,
		const struct base_dependency *from)
{
	LOCAL_ASSERT(dep != NULL);

	base_jd_atom_dep_set(dep, from->atom_id, from->dependency_type);
}

/**
 * @brief Soft-atom fence trigger setup.
 *
 * Sets up an atom to be a SW-only atom signaling a fence
 * when it reaches the run state.
 *
 * Using the existing base dependency system the fence can
 * be set to trigger when a GPU job has finished.
 *
 * The base fence object must not be terminated until the atom
 * has been submitted to @ref base_jd_submit and @ref base_jd_submit
 * has returned.
 *
 * @a fence must be a valid fence set up with @a base_fence_init.
 * Calling this function with a uninitialized fence results in undefined behavior.
 *
 * @param[out] atom A pre-allocated atom to configure as a fence trigger SW atom
 * @param[in] fence The base fence object to trigger.
 *
 * @pre @p fence must reference a @ref base_fence successfully initialized by
 *      calling @ref base_fence_init.
 * @pre @p fence was @e not initialized by calling @ref base_fence_import, nor
 *      is it associated with a fence-trigger job that was already submitted
 *      by calling @ref base_jd_submit.
 * @post @p atom can be submitted by calling @ref base_jd_submit.
 */
static inline void base_jd_fence_trigger_setup_v2(struct base_jd_atom_v2 *atom, struct base_fence *fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd == INVALID_PLATFORM_FENCE);
	LOCAL_ASSERT(fence->basep.stream_fd >= 0);
	atom->jc = (uintptr_t) fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_TRIGGER;
}

/**
 * @brief Soft-atom fence wait setup.
 *
 * Sets up an atom to be a SW-only atom waiting on a fence.
 * When the fence becomes triggered the atom becomes runnable
 * and completes immediately.
 *
 * Using the existing base dependency system the fence can
 * be set to block a GPU job until it has been triggered.
 *
 * The base fence object must not be terminated until the atom
 * has been submitted to @ref base_jd_submit and
 * @ref base_jd_submit has returned.
 *
 * @param[out] atom A pre-allocated atom to configure as a fence wait SW atom
 * @param[in] fence The base fence object to wait on
 *
 * @pre @p fence must reference a @ref base_fence successfully initialized by
 *      calling @ref base_fence_import, or it must be associated with a
 *      fence-trigger job that was already submitted by calling
 *      @ref base_jd_submit.
 * @post @p atom can be submitted by calling @ref base_jd_submit.
 */
static inline void base_jd_fence_wait_setup_v2(struct base_jd_atom_v2 *atom, struct base_fence *fence)
{
	LOCAL_ASSERT(atom);
	LOCAL_ASSERT(fence);
	LOCAL_ASSERT(fence->basep.fd >= 0);
	atom->jc = (uintptr_t) fence;
	atom->core_req = BASE_JD_REQ_SOFT_FENCE_WAIT;
}

/**
 * @brief External resource info initialization.
 *
 * Sets up an external resource object to reference
 * a memory allocation and the type of access requested.
 *
 * @param[in] res     The resource object to initialize
 * @param     handle  The handle to the imported memory object, must be
 *                    obtained by calling @ref base_mem_as_import_handle().
 * @param     access  The type of access requested
 */
static inline void base_external_resource_init(struct base_external_resource *res, struct base_import_handle handle, base_external_resource_access access)
{
	u64 address;

	address = handle.basep.handle;

	LOCAL_ASSERT(res != NULL);
	LOCAL_ASSERT(0 == (address & LOCAL_PAGE_LSB));
	LOCAL_ASSERT(access == BASE_EXT_RES_ACCESS_SHARED || access == BASE_EXT_RES_ACCESS_EXCLUSIVE);

	res->ext_resource = address | (access & LOCAL_PAGE_LSB);
}

/**
 * @brief Job chain event code bits
 * Defines the bits used to create ::base_jd_event_code
 */
enum {
	BASE_JD_SW_EVENT_KERNEL = (1u << 15), /**< Kernel side event */
	BASE_JD_SW_EVENT = (1u << 14), /**< SW defined event */
	BASE_JD_SW_EVENT_SUCCESS = (1u << 13), /**< Event idicates success (SW events only) */
	BASE_JD_SW_EVENT_JOB = (0u << 11), /**< Job related event */
	BASE_JD_SW_EVENT_BAG = (1u << 11), /**< Bag related event */
	BASE_JD_SW_EVENT_INFO = (2u << 11), /**< Misc/info event */
	BASE_JD_SW_EVENT_RESERVED = (3u << 11),	/**< Reserved event type */
	BASE_JD_SW_EVENT_TYPE_MASK = (3u << 11)	    /**< Mask to extract the type from an event code */
};

/**
 * @brief Job chain event codes
 *
 * HW and low-level SW events are represented by event codes.
 * The status of jobs which succeeded are also represented by
 * an event code (see ::BASE_JD_EVENT_DONE).
 * Events are usually reported as part of a ::base_jd_event.
 *
 * The event codes are encoded in the following way:
 * @li 10:0  - subtype
 * @li 12:11 - type
 * @li 13    - SW success (only valid if the SW bit is set)
 * @li 14    - SW event (HW event if not set)
 * @li 15    - Kernel event (should never be seen in userspace)
 *
 * Events are split up into ranges as follows:
 * - BASE_JD_EVENT_RANGE_\<description\>_START
 * - BASE_JD_EVENT_RANGE_\<description\>_END
 *
 * \a code is in \<description\>'s range when:
 * - <tt>BASE_JD_EVENT_RANGE_\<description\>_START <= code < BASE_JD_EVENT_RANGE_\<description\>_END </tt>
 *
 * Ranges can be asserted for adjacency by testing that the END of the previous
 * is equal to the START of the next. This is useful for optimizing some tests
 * for range.
 *
 * A limitation is that the last member of this enum must explicitly be handled
 * (with an assert-unreachable statement) in switch statements that use
 * variables of this type. Otherwise, the compiler warns that we have not
 * handled that enum value.
 */
typedef enum base_jd_event_code {
	/* HW defined exceptions */

	/** Start of HW Non-fault status codes
	 *
	 * @note Obscurely, BASE_JD_EVENT_TERMINATED indicates a real fault,
	 * because the job was hard-stopped
	 */
	BASE_JD_EVENT_RANGE_HW_NONFAULT_START = 0,

	/* non-fatal exceptions */
	BASE_JD_EVENT_NOT_STARTED = 0x00, /**< Can't be seen by userspace, treated as 'previous job done' */
	BASE_JD_EVENT_DONE = 0x01,
	BASE_JD_EVENT_STOPPED = 0x03,	  /**< Can't be seen by userspace, becomes TERMINATED, DONE or JOB_CANCELLED */
	BASE_JD_EVENT_TERMINATED = 0x04,  /**< This is actually a fault status code - the job was hard stopped */
	BASE_JD_EVENT_ACTIVE = 0x08,	  /**< Can't be seen by userspace, jobs only returned on complete/fail/cancel */

	/** End of HW Non-fault status codes
	 *
	 * @note Obscurely, BASE_JD_EVENT_TERMINATED indicates a real fault,
	 * because the job was hard-stopped
	 */
	BASE_JD_EVENT_RANGE_HW_NONFAULT_END = 0x40,

	/** Start of HW fault and SW Error status codes */
	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_START = 0x40,

	/* job exceptions */
	BASE_JD_EVENT_JOB_CONFIG_FAULT = 0x40,
	BASE_JD_EVENT_JOB_POWER_FAULT = 0x41,
	BASE_JD_EVENT_JOB_READ_FAULT = 0x42,
	BASE_JD_EVENT_JOB_WRITE_FAULT = 0x43,
	BASE_JD_EVENT_JOB_AFFINITY_FAULT = 0x44,
	BASE_JD_EVENT_JOB_BUS_FAULT = 0x48,
	BASE_JD_EVENT_INSTR_INVALID_PC = 0x50,
	BASE_JD_EVENT_INSTR_INVALID_ENC = 0x51,
	BASE_JD_EVENT_INSTR_TYPE_MISMATCH = 0x52,
	BASE_JD_EVENT_INSTR_OPERAND_FAULT = 0x53,
	BASE_JD_EVENT_INSTR_TLS_FAULT = 0x54,
	BASE_JD_EVENT_INSTR_BARRIER_FAULT = 0x55,
	BASE_JD_EVENT_INSTR_ALIGN_FAULT = 0x56,
	BASE_JD_EVENT_DATA_INVALID_FAULT = 0x58,
	BASE_JD_EVENT_TILE_RANGE_FAULT = 0x59,
	BASE_JD_EVENT_STATE_FAULT = 0x5A,
	BASE_JD_EVENT_OUT_OF_MEMORY = 0x60,
	BASE_JD_EVENT_UNKNOWN = 0x7F,

	/* GPU exceptions */
	BASE_JD_EVENT_DELAYED_BUS_FAULT = 0x80,
	BASE_JD_EVENT_SHAREABILITY_FAULT = 0x88,

	/* MMU exceptions */
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL1 = 0xC1,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL2 = 0xC2,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL3 = 0xC3,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL4 = 0xC4,
	BASE_JD_EVENT_PERMISSION_FAULT = 0xC8,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL1 = 0xD1,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL2 = 0xD2,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL3 = 0xD3,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL4 = 0xD4,
	BASE_JD_EVENT_ACCESS_FLAG = 0xD8,

	/* SW defined exceptions */
	BASE_JD_EVENT_MEM_GROWTH_FAILED	= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x000,
	BASE_JD_EVENT_TIMED_OUT		= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x001,
	BASE_JD_EVENT_JOB_CANCELLED	= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x002,
	BASE_JD_EVENT_JOB_INVALID	= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x003,
	BASE_JD_EVENT_PM_EVENT		= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x004,

	BASE_JD_EVENT_BAG_INVALID	= BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_BAG | 0x003,

	/** End of HW fault and SW Error status codes */
	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	/** Start of SW Success status codes */
	BASE_JD_EVENT_RANGE_SW_SUCCESS_START = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | 0x000,

	BASE_JD_EVENT_PROGRESS_REPORT = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_JOB | 0x000,
	BASE_JD_EVENT_BAG_DONE = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_BAG | 0x000,
	BASE_JD_EVENT_DRV_TERMINATED = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_INFO | 0x000,

	/** End of SW Success status codes */
	BASE_JD_EVENT_RANGE_SW_SUCCESS_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	/** Start of Kernel-only status codes. Such codes are never returned to user-space */
	BASE_JD_EVENT_RANGE_KERNEL_ONLY_START = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | 0x000,
	BASE_JD_EVENT_REMOVED_FROM_NEXT = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_JOB | 0x000,

	/** End of Kernel-only status codes. */
	BASE_JD_EVENT_RANGE_KERNEL_ONLY_END = BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_RESERVED | 0x3FF
} base_jd_event_code;

/**
 * @brief Event reporting structure
 *
 * This structure is used by the kernel driver to report information
 * about GPU events. The can either be HW-specific events or low-level
 * SW events, such as job-chain completion.
 *
 * The event code contains an event type field which can be extracted
 * by ANDing with ::BASE_JD_SW_EVENT_TYPE_MASK.
 *
 * Based on the event type base_jd_event::data holds:
 * @li ::BASE_JD_SW_EVENT_JOB : the offset in the ring-buffer for the completed
 * job-chain
 * @li ::BASE_JD_SW_EVENT_BAG : The address of the ::base_jd_bag that has
 * been completed (ie all contained job-chains have been completed).
 * @li ::BASE_JD_SW_EVENT_INFO : base_jd_event::data not used
 */
typedef struct base_jd_event_v2 {
	base_jd_event_code event_code;  /**< event code */
	base_atom_id atom_number;       /**< the atom number that has completed */
	struct base_jd_udata udata;     /**< user data */
} base_jd_event_v2;

/**
 * @brief Structure for BASE_JD_REQ_SOFT_DUMP_CPU_GPU_COUNTERS jobs.
 *
 * This structure is stored into the memory pointed to by the @c jc field
 * of @ref base_jd_atom.
 *
 * It must not occupy the same CPU cache line(s) as any neighboring data.
 * This is to avoid cases where access to pages containing the structure
 * is shared between cached and un-cached memory regions, which would
 * cause memory corruption.
 */

typedef struct base_dump_cpu_gpu_counters {
	u64 system_time;
	u64 cycle_counter;
	u64 sec;
	u32 usec;
	u8 padding[36];
} base_dump_cpu_gpu_counters;

/** @} end group base_user_api_job_dispatch */

#define GPU_MAX_JOB_SLOTS 16

/**
 * @page page_base_user_api_gpuprops User-side Base GPU Property Query API
 *
 * The User-side Base GPU Property Query API encapsulates two
 * sub-modules:
 *
 * - @ref base_user_api_gpuprops_dyn "Dynamic GPU Properties"
 * - @ref base_plat_config_gpuprops "Base Platform Config GPU Properties"
 *
 * There is a related third module outside of Base, which is owned by the MIDG
 * module:
 * - @ref gpu_props_static "Midgard Compile-time GPU Properties"
 *
 * Base only deals with properties that vary between different Midgard
 * implementations - the Dynamic GPU properties and the Platform Config
 * properties.
 *
 * For properties that are constant for the Midgard Architecture, refer to the
 * MIDG module. However, we will discuss their relevance here <b>just to
 * provide background information.</b>
 *
 * @section sec_base_user_api_gpuprops_about About the GPU Properties in Base and MIDG modules
 *
 * The compile-time properties (Platform Config, Midgard Compile-time
 * properties) are exposed as pre-processor macros.
 *
 * Complementing the compile-time properties are the Dynamic GPU
 * Properties, which act as a conduit for the Midgard Configuration
 * Discovery.
 *
 * In general, the dynamic properties are present to verify that the platform
 * has been configured correctly with the right set of Platform Config
 * Compile-time Properties.
 *
 * As a consistent guide across the entire DDK, the choice for dynamic or
 * compile-time should consider the following, in order:
 * -# Can the code be written so that it doesn't need to know the
 * implementation limits at all?
 * -# If you need the limits, get the information from the Dynamic Property
 * lookup. This should be done once as you fetch the context, and then cached
 * as part of the context data structure, so it's cheap to access.
 * -# If there's a clear and arguable inefficiency in using Dynamic Properties,
 * then use a Compile-Time Property (Platform Config, or Midgard Compile-time
 * property). Examples of where this might be sensible follow:
 *  - Part of a critical inner-loop
 *  - Frequent re-use throughout the driver, causing significant extra load
 * instructions or control flow that would be worthwhile optimizing out.
 *
 * We cannot provide an exhaustive set of examples, neither can we provide a
 * rule for every possible situation. Use common sense, and think about: what
 * the rest of the driver will be doing; how the compiler might represent the
 * value if it is a compile-time constant; whether an OEM shipping multiple
 * devices would benefit much more from a single DDK binary, instead of
 * insignificant micro-optimizations.
 *
 * @section sec_base_user_api_gpuprops_dyn Dynamic GPU Properties
 *
 * Dynamic GPU properties are presented in two sets:
 * -# the commonly used properties in @ref base_gpu_props, which have been
 * unpacked from GPU register bitfields.
 * -# The full set of raw, unprocessed properties in @ref gpu_raw_gpu_props
 * (also a member of @ref base_gpu_props). All of these are presented in
 * the packed form, as presented by the GPU  registers themselves.
 *
 * @usecase The raw properties in @ref gpu_raw_gpu_props are necessary to
 * allow a user of the Mali Tools (e.g. PAT) to determine "Why is this device
 * behaving differently?". In this case, all information about the
 * configuration is potentially useful, but it <b>does not need to be processed
 * by the driver</b>. Instead, the raw registers can be processed by the Mali
 * Tools software on the host PC.
 *
 * The properties returned extend the Midgard Configuration Discovery
 * registers. For example, GPU clock speed is not specified in the Midgard
 * Architecture, but is <b>necessary for OpenCL's clGetDeviceInfo() function</b>.
 *
 * The GPU properties are obtained by a call to
 * base_get_gpu_props(). This simply returns a pointer to a const
 * base_gpu_props structure. It is constant for the life of a base
 * context. Multiple calls to base_get_gpu_props() to a base context
 * return the same pointer to a constant structure. This avoids cache pollution
 * of the common data.
 *
 * This pointer must not be freed, because it does not point to the start of a
 * region allocated by the memory allocator; instead, just close the @ref
 * base_context.
 *
 *
 * @section sec_base_user_api_gpuprops_kernel Kernel Operation
 *
 * During Base Context Create time, user-side makes a single kernel call:
 * - A call to fill user memory with GPU information structures
 *
 * The kernel-side will fill the provided the entire processed @ref base_gpu_props
 * structure, because this information is required in both
 * user and kernel side; it does not make sense to decode it twice.
 *
 * Coherency groups must be derived from the bitmasks, but this can be done
 * kernel side, and just once at kernel startup: Coherency groups must already
 * be known kernel-side, to support chains that specify a 'Only Coherent Group'
 * SW requirement, or 'Only Coherent Group with Tiler' SW requirement.
 *
 * @section sec_base_user_api_gpuprops_cocalc Coherency Group calculation
 * Creation of the coherent group data is done at device-driver startup, and so
 * is one-time. This will most likely involve a loop with CLZ, shifting, and
 * bit clearing on the L2_PRESENT mask, depending on whether the
 * system is L2 Coherent. The number of shader cores is done by a
 * population count, since faulty cores may be disabled during production,
 * producing a non-contiguous mask.
 *
 * The memory requirements for this algorithm can be determined either by a u64
 * population count on the L2_PRESENT mask (a LUT helper already is
 * required for the above), or simple assumption that there can be no more than
 * 16 coherent groups, since core groups are typically 4 cores.
 */

/**
 * @addtogroup base_user_api_gpuprops User-side Base GPU Property Query APIs
 * @{
 */

/**
 * @addtogroup base_user_api_gpuprops_dyn Dynamic HW Properties
 * @{
 */

#define BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS 4

#define BASE_MAX_COHERENT_GROUPS 16

struct mali_base_gpu_core_props {
	/**
	 * Product specific value.
	 */
	u32 product_id;

	/**
	 * Status of the GPU release.
	 * No defined values, but starts at 0 and increases by one for each
	 * release status (alpha, beta, EAC, etc.).
	 * 4 bit values (0-15).
	 */
	u16 version_status;

	/**
	 * Minor release number of the GPU. "P" part of an "RnPn" release number.
     * 8 bit values (0-255).
	 */
	u16 minor_revision;

	/**
	 * Major release number of the GPU. "R" part of an "RnPn" release number.
     * 4 bit values (0-15).
	 */
	u16 major_revision;

	u16 padding;

	/* The maximum GPU frequency. Reported to applications by
	 * clGetDeviceInfo()
	 */
	u32 gpu_freq_khz_max;

	/**
	 * Size of the shader program counter, in bits.
	 */
	u32 log2_program_counter_size;

	/**
	 * TEXTURE_FEATURES_x registers, as exposed by the GPU. This is a
	 * bitpattern where a set bit indicates that the format is supported.
	 *
	 * Before using a texture format, it is recommended that the corresponding
	 * bit be checked.
	 */
	u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];

	/**
	 * Theoretical maximum memory available to the GPU. It is unlikely that a
	 * client will be able to allocate all of this memory for their own
	 * purposes, but this at least provides an upper bound on the memory
	 * available to the GPU.
	 *
	 * This is required for OpenCL's clGetDeviceInfo() call when
	 * CL_DEVICE_GLOBAL_MEM_SIZE is requested, for OpenCL GPU devices. The
	 * client will not be expecting to allocate anywhere near this value.
	 */
	u64 gpu_available_memory_size;

	/**
	 * The number of execution engines.
	 */
	u8 num_exec_engines;
};

/**
 *
 * More information is possible - but associativity and bus width are not
 * required by upper-level apis.
 */
struct mali_base_gpu_l2_cache_props {
	u8 log2_line_size;
	u8 log2_cache_size;
	u8 num_l2_slices; /* Number of L2C slices. 1 or higher */
	u8 padding[5];
};

struct mali_base_gpu_tiler_props {
	u32 bin_size_bytes;	/* Max is 4*2^15 */
	u32 max_active_levels;	/* Max is 2^15 */
};

/**
 * GPU threading system details.
 */
struct mali_base_gpu_thread_props {
	u32 max_threads;            /* Max. number of threads per core */
	u32 max_workgroup_size;     /* Max. number of threads per workgroup */
	u32 max_barrier_size;       /* Max. number of threads that can synchronize on a simple barrier */
	u16 max_registers;          /* Total size [1..65535] of the register file available per core. */
	u8  max_task_queue;         /* Max. tasks [1..255] which may be sent to a core before it becomes blocked. */
	u8  max_thread_group_split; /* Max. allowed value [1..15] of the Thread Group Split field. */
	u8  impl_tech;              /* 0 = Not specified, 1 = Silicon, 2 = FPGA, 3 = SW Model/Emulation */
	u8  padding[3];
	u32 tls_alloc;              /* Number of threads per core that TLS must
				     * be allocated for
				     */
};

/**
 * @brief descriptor for a coherent group
 *
 * \c core_mask exposes all cores in that coherent group, and \c num_cores
 * provides a cached population-count for that mask.
 *
 * @note Whilst all cores are exposed in the mask, not all may be available to
 * the application, depending on the Kernel Power policy.
 *
 * @note if u64s must be 8-byte aligned, then this structure has 32-bits of wastage.
 */
struct mali_base_gpu_coherent_group {
	u64 core_mask;	       /**< Core restriction mask required for the group */
	u16 num_cores;	       /**< Number of cores in the group */
	u16 padding[3];
};

/**
 * @brief Coherency group information
 *
 * Note that the sizes of the members could be reduced. However, the \c group
 * member might be 8-byte aligned to ensure the u64 core_mask is 8-byte
 * aligned, thus leading to wastage if the other members sizes were reduced.
 *
 * The groups are sorted by core mask. The core masks are non-repeating and do
 * not intersect.
 */
struct mali_base_gpu_coherent_group_info {
	u32 num_groups;

	/**
	 * Number of core groups (coherent or not) in the GPU. Equivalent to the number of L2 Caches.
	 *
	 * The GPU Counter dumping writes 2048 bytes per core group, regardless of
	 * whether the core groups are coherent or not. Hence this member is needed
	 * to calculate how much memory is required for dumping.
	 *
	 * @note Do not use it to work out how many valid elements are in the
	 * group[] member. Use num_groups instead.
	 */
	u32 num_core_groups;

	/**
	 * Coherency features of the memory, accessed by @ref gpu_mem_features
	 * methods
	 */
	u32 coherency;

	u32 padding;

	/**
	 * Descriptors of coherent groups
	 */
	struct mali_base_gpu_coherent_group group[BASE_MAX_COHERENT_GROUPS];
};

/**
 * A complete description of the GPU's Hardware Configuration Discovery
 * registers.
 *
 * The information is presented inefficiently for access. For frequent access,
 * the values should be better expressed in an unpacked form in the
 * base_gpu_props structure.
 *
 * @usecase The raw properties in @ref gpu_raw_gpu_props are necessary to
 * allow a user of the Mali Tools (e.g. PAT) to determine "Why is this device
 * behaving differently?". In this case, all information about the
 * configuration is potentially useful, but it <b>does not need to be processed
 * by the driver</b>. Instead, the raw registers can be processed by the Mali
 * Tools software on the host PC.
 *
 */
struct gpu_raw_gpu_props {
	u64 shader_present;
	u64 tiler_present;
	u64 l2_present;
	u64 stack_present;

	u32 l2_features;
	u32 core_features;
	u32 mem_features;
	u32 mmu_features;

	u32 as_present;

	u32 js_present;
	u32 js_features[GPU_MAX_JOB_SLOTS];
	u32 tiler_features;
	u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];

	u32 gpu_id;

	u32 thread_max_threads;
	u32 thread_max_workgroup_size;
	u32 thread_max_barrier_size;
	u32 thread_features;

	/*
	 * Note: This is the _selected_ coherency mode rather than the
	 * available modes as exposed in the coherency_features register.
	 */
	u32 coherency_mode;

	u32 thread_tls_alloc;
};

/**
 * Return structure for base_get_gpu_props().
 *
 * NOTE: the raw_props member in this data structure contains the register
 * values from which the value of the other members are derived. The derived
 * members exist to allow for efficient access and/or shielding the details
 * of the layout of the registers.
 *
 */
typedef struct base_gpu_props {
	struct mali_base_gpu_core_props core_props;
	struct mali_base_gpu_l2_cache_props l2_props;
	u64 unused_1; /* keep for backwards compatibility */
	struct mali_base_gpu_tiler_props tiler_props;
	struct mali_base_gpu_thread_props thread_props;

	/** This member is large, likely to be 128 bytes */
	struct gpu_raw_gpu_props raw_props;

	/** This must be last member of the structure */
	struct mali_base_gpu_coherent_group_info coherency_info;
} base_gpu_props;

/** @} end group base_user_api_gpuprops_dyn */

/** @} end group base_user_api_gpuprops */

/**
 * @addtogroup base_user_api_core User-side Base core APIs
 * @{
 */

/**
 * Flags to pass to ::base_context_init.
 * Flags can be ORed together to enable multiple things.
 *
 * These share the same space as BASEP_CONTEXT_FLAG_*, and so must
 * not collide with them.
 */
typedef u32 base_context_create_flags;

/** No flags set */
#define BASE_CONTEXT_CREATE_FLAG_NONE ((base_context_create_flags)0)

/** Base context is embedded in a cctx object (flag used for CINSTR
 * software counter macros)
 */
#define BASE_CONTEXT_CCTX_EMBEDDED ((base_context_create_flags)1 << 0)

/** Base context is a 'System Monitor' context for Hardware counters.
 *
 * One important side effect of this is that job submission is disabled.
 */
#define BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED \
	((base_context_create_flags)1 << 1)


/* Bit-shift used to encode a memory group ID in base_context_create_flags
 */
#define BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (3)

/* Bitmask used to encode a memory group ID in base_context_create_flags
 */
#define BASEP_CONTEXT_MMU_GROUP_ID_MASK \
	((base_context_create_flags)0xF << BASEP_CONTEXT_MMU_GROUP_ID_SHIFT)

/* Bitpattern describing the base_context_create_flags that can be
 * passed to the kernel
 */
#define BASEP_CONTEXT_CREATE_KERNEL_FLAGS \
	(BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED | \
	 BASEP_CONTEXT_MMU_GROUP_ID_MASK)

/* Bitpattern describing the ::base_context_create_flags that can be
 * passed to base_context_init()
 */
#define BASEP_CONTEXT_CREATE_ALLOWED_FLAGS \
	(BASE_CONTEXT_CCTX_EMBEDDED | BASEP_CONTEXT_CREATE_KERNEL_FLAGS)

/*
 * Private flags used on the base context
 *
 * These start at bit 31, and run down to zero.
 *
 * They share the same space as @ref base_context_create_flags, and so must
 * not collide with them.
 */
/** Private flag tracking whether job descriptor dumping is disabled */
#define BASEP_CONTEXT_FLAG_JOB_DUMP_DISABLED \
	((base_context_create_flags)(1 << 31))

/**
 * base_context_mmu_group_id_set - Encode a memory group ID in
 *                                 base_context_create_flags
 *
 * Memory allocated for GPU page tables will come from the specified group.
 *
 * @group_id: Physical memory group ID. Range is 0..(BASE_MEM_GROUP_COUNT-1).
 *
 * Return: Bitmask of flags to pass to base_context_init.
 */
static inline base_context_create_flags base_context_mmu_group_id_set(
	int const group_id)
{
	LOCAL_ASSERT(group_id >= 0);
	LOCAL_ASSERT(group_id < BASE_MEM_GROUP_COUNT);
	return BASEP_CONTEXT_MMU_GROUP_ID_MASK &
		((base_context_create_flags)group_id <<
		BASEP_CONTEXT_MMU_GROUP_ID_SHIFT);
}

/**
 * base_context_mmu_group_id_get - Decode a memory group ID from
 *                                 base_context_create_flags
 *
 * Memory allocated for GPU page tables will come from the returned group.
 *
 * @flags: Bitmask of flags to pass to base_context_init.
 *
 * Return: Physical memory group ID. Valid range is 0..(BASE_MEM_GROUP_COUNT-1).
 */
static inline int base_context_mmu_group_id_get(
	base_context_create_flags const flags)
{
	LOCAL_ASSERT(flags == (flags & BASEP_CONTEXT_CREATE_ALLOWED_FLAGS));
	return (int)((flags & BASEP_CONTEXT_MMU_GROUP_ID_MASK) >>
			BASEP_CONTEXT_MMU_GROUP_ID_SHIFT);
}

/** @} end group base_user_api_core */

/** @} end group base_user_api */

/**
 * @addtogroup base_plat_config_gpuprops Base Platform Config GPU Properties
 * @{
 *
 * C Pre-processor macros are exposed here to do with Platform
 * Config.
 *
 * These include:
 * - GPU Properties that are constant on a particular Midgard Family
 * Implementation e.g. Maximum samples per pixel on Mali-T600.
 * - General platform config for the GPU, such as the GPU major and minor
 * revison.
 */

/** @} end group base_plat_config_gpuprops */

/**
 * @addtogroup base_api Base APIs
 * @{
 */

/** @} end group base_api */

/* Enable additional tracepoints for latency measurements (TL_ATOM_READY,
 * TL_ATOM_DONE, TL_ATOM_PRIO_CHANGE, TL_ATOM_EVENT_POST) */
#define BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS (1 << 0)

/* Indicate that job dumping is enabled. This could affect certain timers
 * to account for the performance impact. */
#define BASE_TLSTREAM_JOB_DUMPING_ENABLED (1 << 1)

#define BASE_TLSTREAM_FLAGS_MASK (BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS | \
		BASE_TLSTREAM_JOB_DUMPING_ENABLED)

/**
 * A number of bit flags are defined for requesting cpu_gpu_timeinfo. These
 * flags are also used, where applicable, for specifying which fields
 * are valid following the request operation.
 */

/* For monotonic (counter) timefield */
#define BASE_TIMEINFO_MONOTONIC_FLAG (1UL << 0)
/* For system wide timestamp */
#define BASE_TIMEINFO_TIMESTAMP_FLAG (1UL << 1)
/* For GPU cycle counter */
#define BASE_TIMEINFO_CYCLE_COUNTER_FLAG (1UL << 2)
/* Specify kernel GPU register timestamp */
#define BASE_TIMEINFO_KERNEL_SOURCE_FLAG (1UL << 30)
/* Specify userspace cntvct_el0 timestamp source */
#define BASE_TIMEINFO_USER_SOURCE_FLAG (1UL << 31)

#define BASE_TIMEREQUEST_ALLOWED_FLAGS (\
		BASE_TIMEINFO_MONOTONIC_FLAG | \
		BASE_TIMEINFO_TIMESTAMP_FLAG | \
		BASE_TIMEINFO_CYCLE_COUNTER_FLAG | \
		BASE_TIMEINFO_KERNEL_SOURCE_FLAG | \
		BASE_TIMEINFO_USER_SOURCE_FLAG)


#endif				/* _BASE_KERNEL_H_ */
