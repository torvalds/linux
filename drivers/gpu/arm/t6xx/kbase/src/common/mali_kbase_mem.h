/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_mem.h
 * Base kernel memory APIs
 */

#ifndef _KBASE_MEM_H_
#define _KBASE_MEM_H_

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

#include <malisw/mali_malisw.h>
#include <osk/mali_osk.h>
#ifdef CONFIG_UMP
#include <linux/ump.h>
#endif /* CONFIG_UMP */
#include <kbase/mali_base_kernel.h>
#include <kbase/src/common/mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"

/* Part of the workaround for uTLB invalid pages is to ensure we grow/shrink tmem by 4 pages at a time */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316 (2) /* round to 4 pages */

/* Part of the workaround for PRLAM-9630 requires us to grow/shrink memory by 8 pages.
The MMU reads in 8 page table entries from memory at a time, if we have more than one page fault within the same 8 pages and
page tables are updated accordingly, the MMU does not re-read the page table entries from memory for the subsequent page table
updates and generates duplicate page faults as the page table information used by the MMU is not valid.   */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630 (3) /* round to 8 pages */

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2 (0) /* round to 1 page */

/* This must always be a power of 2 */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_8316 (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_9630 (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630)

/**
 * A CPU mapping
 */
typedef struct kbase_cpu_mapping
{
	osk_dlist_item      link;
	osk_virt_addr       uaddr;
	u32                 nr_pages;
	mali_size64         page_off;
	void                *private; /* Use for VMA */
} kbase_cpu_mapping;

/**
 * A GPU memory region, and attributes for CPU mappings.
 */
typedef struct kbase_va_region
{
	struct rb_node          rblink;
	osk_dlist_item          link;

	kbase_context    *kctx; /* Backlink to base context */

	u64                     start_pfn;  /* The PFN in GPU space */
	u32                     nr_pages;   /* VA size */

#define KBASE_REG_FREE       (1ul << 0) /* Free region */
#define KBASE_REG_CPU_WR     (1ul << 1) /* CPU write access */
#define KBASE_REG_GPU_WR     (1ul << 2) /* GPU write access */
#define KBASE_REG_GPU_NX     (1ul << 3) /* No eXectue flag */
#define KBASE_REG_CPU_CACHED (1ul << 4) /* Is CPU cached? */
#define KBASE_REG_GPU_CACHED (1ul << 5) /* Is GPU cached? */

#define KBASE_REG_GROWABLE   (1ul << 6) /* Is growable? */
#define KBASE_REG_PF_GROW    (1ul << 7) /* Can grow on pf? */

#define KBASE_REG_IS_RB      (1ul << 8) /* Is ringbuffer? */
#define KBASE_REG_IS_MMU_DUMP (1ul << 9) /* Is an MMU dump */
#define KBASE_REG_IS_TB      (1ul << 10) /* Is register trace buffer? */

#define KBASE_REG_SHARE_IN   (1ul << 11) /* inner shareable coherency */
#define KBASE_REG_SHARE_BOTH (1ul << 12) /* inner & outer shareable coherency */

#define KBASE_REG_DELAYED_FREE (1ul << 13) /* kbase_mem_free_region called but mappings still exist */

#define KBASE_REG_ZONE_MASK  (3ul << 14) /* Space for 4 different zones */
#define KBASE_REG_ZONE(x)    (((x) & 3) << 14)

#define KBASE_REG_GPU_RD     (1ul<<16) /* GPU write access */
#define KBASE_REG_CPU_RD     (1ul<<17) /* CPU read access */

#define KBASE_REG_MUST_ZERO  (1ul<<18) /* No zeroing needed */

#define KBASE_REG_FLAGS_NR_BITS    19  /* Number of bits used by kbase_va_region flags */

#define KBASE_REG_ZONE_PMEM  KBASE_REG_ZONE(0)

#ifndef KBASE_REG_ZONE_TMEM  /* To become 0 on a 64bit platform */
/*
 * On a 32bit platform, TMEM should be wired from 4GB to the VA limit
 * of the GPU, which is currently hardcoded at 48 bits. Unfortunately,
 * the Linux mmap() interface limits us to 2^32 pages (2^44 bytes, see
 * mmap64 man page for reference).
 */
#define KBASE_REG_ZONE_EXEC         KBASE_REG_ZONE(1) /* Dedicated 4GB region for shader code */
#define KBASE_REG_ZONE_EXEC_BASE    ((1ULL << 32) >> OSK_PAGE_SHIFT)
#define KBASE_REG_ZONE_EXEC_SIZE    (((1ULL << 33) >> OSK_PAGE_SHIFT) - \
                                    KBASE_REG_ZONE_EXEC_BASE)

#define KBASE_REG_ZONE_TMEM         KBASE_REG_ZONE(2)
#define KBASE_REG_ZONE_TMEM_BASE    ((1ULL << 33) >> OSK_PAGE_SHIFT) /* Starting after KBASE_REG_ZONE_EXEC */
#define KBASE_REG_ZONE_TMEM_SIZE    (((1ULL << 44) >> OSK_PAGE_SHIFT) - \
                                    KBASE_REG_ZONE_TMEM_BASE)
#endif

#define KBASE_REG_COOKIE_MASK       (~((1ul << KBASE_REG_FLAGS_NR_BITS)-1))
#define KBASE_REG_COOKIE(x)         ((x << KBASE_REG_FLAGS_NR_BITS) & KBASE_REG_COOKIE_MASK)

/* The reserved cookie values */
#define KBASE_REG_COOKIE_RB         0
#define KBASE_REG_COOKIE_MMU_DUMP   1
#define KBASE_REG_COOKIE_TB         2
#define KBASE_REG_COOKIE_MTP        3
#define KBASE_REG_COOKIE_FIRST_FREE 4

/* Bit mask of cookies that not used for PMEM but reserved for other uses */
#define KBASE_REG_RESERVED_COOKIES  ((1ULL << (KBASE_REG_COOKIE_FIRST_FREE))-1)

	u32                 flags;

	u32                 nr_alloc_pages; /* nr of pages allocated */
	u32                 extent;         /* nr of pages alloc'd on PF */

	osk_phy_addr        *phy_pages;

	osk_dlist           map_list;

	/* non-NULL if this memory object is a kds_resource */
	struct kds_resource * kds_res;

	base_tmem_import_type imported_type;

	/* member in union valid based on imported_type */
	union
	{
#ifdef CONFIG_UMP
		ump_dd_handle ump_handle;
#endif /* CONFIG_UMP */
#if defined(CONFIG_DMA_SHARED_BUFFER)
		struct
		{
			struct dma_buf *            dma_buf;
			struct dma_buf_attachment * dma_attachment;
			unsigned int                current_mapping_usage_count;
			struct sg_table *           st;
		} umm;
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) */
	} imported_metadata;

} kbase_va_region;

/* Common functions */
static INLINE osk_phy_addr *kbase_get_phy_pages(struct kbase_va_region *reg)
{
	OSK_ASSERT(reg);

	return reg->phy_pages;
}

static INLINE void kbase_set_phy_pages(struct kbase_va_region *reg, osk_phy_addr *phy_pages)
{
	OSK_ASSERT(reg);

	reg->phy_pages = phy_pages;
}

mali_error kbase_mem_init(kbase_device * kbdev);
void       kbase_mem_halt(kbase_device * kbdev);
void       kbase_mem_term(kbase_device * kbdev);

/**
 * @brief Initialize an OS based memory allocator.
 *
 * Initializes a allocator.
 * Must be called before any allocation is attempted.
 * \a kbase_mem_allocator_alloc and \a kbase_mem_allocator_free is used
 * to allocate and free memory.
 * \a kbase_mem_allocator_term must be called to clean up the allocator.
 * All memory obtained via \a kbase_mem_allocator_alloc must have been
 * \a kbase_mem_allocator_free before \a kbase_mem_allocator_term is called.
 *
 * @param allocator Allocator object to initialize
 * @param max_size  Maximum number of pages to keep on the freelist.
 * @return MALI_ERROR_NONE on success, an error code indicating what failed on error.
 */
mali_error kbase_mem_allocator_init(kbase_mem_allocator * allocator, unsigned int max_size);
/**
 * @brief Allocate memory via an OS based memory allocator.
 *
 * @param[in]  allocator Allocator to obtain the memory from
 * @param      nr_pages  Number of pages to allocate
 * @param[out] pages     Pointer to an array where the physical address of the allocated pages will be stored
 * @param      flags     Allocation flag, 0 or KBASE_REG_MUST_ZERO supported.
 * @return MALI_ERROR_NONE if the pages were allocated, an error code indicating what failed on error
 */
mali_error kbase_mem_allocator_alloc(kbase_mem_allocator * allocator, u32 nr_pages, osk_phy_addr *pages, int flags);
/**
 * @brief Free memory obtained for an OS based memory allocator.
 *
 * @param[in] allocator Allocator to free the memory back to
 * @param     nr_pages  Number of pages to free
 * @param[in] pages     Pointer to an array holding the physical address of the paghes to free.
 */
void kbase_mem_allocator_free(kbase_mem_allocator * allocator, u32 nr_pages, osk_phy_addr *pages);
/**
 * @brief Terminate an OS based memory allocator.
 *
 * Frees all cached allocations and clean up internal state.
 * All allocate pages must have been \a kbase_mem_allocator_free before
 * this function is called.
 *
 * @param[in] allocator Allocator to terminate
 */
void kbase_mem_allocator_term(kbase_mem_allocator * allocator);

/**
 * @brief Initializes memory context which tracks memory usage.
 *
 * Function initializes memory context with given max_pages value.
 *
 * @param[in]   usage      usage tracker
 * @param[in]   max_pages  maximum pages allowed to be allocated within this memory context
 *
 * @return  MALI_ERROR_NONE in case of error. Error code otherwise.
 */
mali_error kbase_mem_usage_init(kbasep_mem_usage * usage, u32 max_pages);

/*
 * @brief Terminates given memory context
 *
 * @param[in]    usage  usage tracker
 *
 * @return MALI_ERROR_NONE in case of error. Error code otherwise.
 */
void kbase_mem_usage_term(kbasep_mem_usage *usage);

/*
 * @brief Requests a number of pages from the given context.
 *
 * Function requests a number of pages from the given context. Context is updated only if it contains enough number of
 * free pages. Otherwise function returns error and no pages are claimed.
 *
 * @param[in]    usage     usage tracker
 * @param[in]    nr_pages  number of pages requested
 *
 * @return  MALI_ERROR_NONE when context page request succeeded. Error code otherwise.
 */
mali_error kbase_mem_usage_request_pages(kbasep_mem_usage *usage, u32 nr_pages);

/*
 * @brief Release a number of pages from the given context.
 *
 * @param[in]    usage     usage tracker
 * @param[in]    nr_pages  number of pages to be released
 */
void kbase_mem_usage_release_pages(kbasep_mem_usage *usage, u32 nr_pages);


mali_error kbase_region_tracker_init(kbase_context *kctx);
void kbase_region_tracker_term(kbase_context *kctx);

struct kbase_va_region * kbase_region_tracker_find_region_enclosing_range(
	kbase_context *kctx, u64 start_pgoff, u32 nr_pages );

struct kbase_va_region * kbase_region_tracker_find_region_enclosing_address(
	kbase_context *kctx, mali_addr64 gpu_addr );

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Must be called with context lock held.
 */
struct kbase_va_region * kbase_region_tracker_find_region_base_address(
	kbase_context *kctx, mali_addr64 gpu_addr );


struct kbase_va_region *kbase_alloc_free_region(kbase_context *kctx, u64 start_pfn, u32 nr_pages, u32 zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
mali_error kbase_add_va_region(kbase_context *kctx,
                               struct kbase_va_region *reg,
                               mali_addr64 addr, u32 nr_pages,
                               u32 align);

mali_error kbase_gpu_mmap(kbase_context *kctx,
                          struct kbase_va_region *reg,
                          mali_addr64 addr, u32 nr_pages,
                          u32 align);
mali_bool kbase_check_alloc_flags(u32 flags);
void kbase_update_region_flags(struct kbase_va_region *reg, u32 flags, mali_bool is_growable);

void kbase_gpu_vm_lock(kbase_context *kctx);
void kbase_gpu_vm_unlock(kbase_context *kctx);

void kbase_free_phy_pages(struct kbase_va_region *reg);
int kbase_alloc_phy_pages(struct kbase_va_region *reg, u32 vsize, u32 size);

mali_error kbase_cpu_free_mapping(struct kbase_va_region *reg, const void *ptr);

mali_error kbase_mmu_init(kbase_context *kctx);
void kbase_mmu_term(kbase_context *kctx);

osk_phy_addr kbase_mmu_alloc_pgd(kbase_context *kctx);
void kbase_mmu_free_pgd(kbase_context *kctx);
mali_error kbase_mmu_insert_pages(kbase_context *kctx, u64 vpfn,
                                  osk_phy_addr *phys, u32 nr, u32 flags);
mali_error kbase_mmu_teardown_pages(kbase_context *kctx, u64 vpfn, u32 nr);

/**
 * @brief Register region and map it on the GPU.
 *
 * Call kbase_add_va_region() and map the region on the GPU.
 */
mali_error kbase_gpu_mmap(kbase_context *kctx,
                          struct kbase_va_region *reg,
                          mali_addr64 addr, u32 nr_pages,
                          u32 align);

/**
 * @brief Remove the region from the GPU and unregister it.
 *
 * Must be called with context lock held.
 */
mali_error kbase_gpu_munmap(kbase_context *kctx, struct kbase_va_region *reg);

/**
 * The caller has the following locking conditions:
 * - It must hold kbase_as::transaction_mutex on kctx's address space
 * - It must hold the kbasep_js_device_data::runpool_irq::lock
 */
void kbase_mmu_update(kbase_context *kctx);

/**
 * The caller has the following locking conditions:
 * - It must hold kbase_as::transaction_mutex on kctx's address space
 * - It must hold the kbasep_js_device_data::runpool_irq::lock
 */
void kbase_mmu_disable (kbase_context *kctx);

void kbase_mmu_interrupt(kbase_device * kbdev, u32 irq_stat);

/** Dump the MMU tables to a buffer
 *
 * This function allocates a buffer (of @c nr_pages pages) to hold a dump of the MMU tables and fills it. If the
 * buffer is too small then the return value will be NULL.
 *
 * The GPU vm lock must be held when calling this function.
 *
 * The buffer returned should be freed with @ref vfree when it is no longer required.
 *
 * @param[in]   kctx        The kbase context to dump
 * @param[in]   nr_pages    The number of pages to allocate for the buffer.
 *
 * @return The address of the buffer containing the MMU dump or NULL on error (including if the @c nr_pages is too
 * small)
 */
void *kbase_mmu_dump(kbase_context *kctx,int nr_pages);

mali_error kbase_sync_now(kbase_context *kctx, base_syncset *syncset);
void kbase_pre_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr);
void kbase_post_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr);

struct kbase_va_region *kbase_tmem_alloc(kbase_context *kctx,
                                         u32 vsize, u32 psize,
                                         u32 extent, u32 flags, mali_bool is_growable);

/** Resize a tmem region
 *
 * This function changes the number of physical pages committed to a tmem region.
 *
 * @param[in]   kctx           The kbase context which the tmem belongs to
 * @param[in]   gpu_addr       The base address of the tmem region
 * @param[in]   delta          The number of pages to grow or shrink by
 * @param[out]  size           The number of pages of memory committed after growing/shrinking
 * @param[out]  failure_reason Error code describing reason of failure.
 *
 * @return MALI_ERROR_NONE on success
 */

mali_error kbase_tmem_resize(kbase_context *kctx, mali_addr64 gpu_addr, s32 delta, u32 *size, base_backing_threshold_status * failure_reason);

/** Set the size of a tmem region
 *
 * This function sets the number of physical pages committed to a tmem region upto max region size.
 *
 * @param[in]   kctx           The kbase context which the tmem belongs to
 * @param[in]   gpu_addr       The base address of the tmem region
 * @param[in]   size           The number of pages desired
 * @param[out]  actual_size    The actual number of pages of memory committed to this tmem
 * @param[out]  failure_reason Error code describing reason of failure.
 *
 * @return MALI_ERROR_NONE on success
 */

mali_error kbase_tmem_set_size(kbase_context *kctx, mali_addr64 gpu_addr, u32 size, u32 *actual_size, base_backing_threshold_status * failure_reason);

/** Get a tmem region size
 *
 * This function obtains the number of physical pages committed to a tmem region.
 *
 * @param[in]   kctx           The kbase context which the tmem belongs to
 * @param[in]   gpu_addr       The base address of the tmem region
 * @param[out]  actual_size    The actual number of pages of memory committed to this tmem
 *
 * @return MALI_ERROR_NONE on success
 */

mali_error kbase_tmem_get_size(kbase_context *kctx, mali_addr64 gpu_addr, u32 *actual_size );

/**
 * Import external memory.
 *
 * This function supports importing external memory.
 * If imported a kbase_va_region is created of the tmem type.
 * The region might not be mappable on the CPU depending on the imported type.
 * If not mappable the KBASE_REG_NO_CPU_MAP bit will be set.
 *
 * Import will fail if (but not limited to):
 * @li Unsupported import type
 * @li Handle not valid for the type
 * @li Access to a handle was not valid
 * @li The underlying memory can't be accessed by the GPU
 * @li No VA space found to map the memory
 * @li Resources to track the region was not available
 *
 * @param[in]   kctx    The kbase context which the tmem will be created in
 * @param       type    The type of memory to import
 * @param       handle  Handle to the memory to import
 * @param[out]  pages   Where to store the number of pages imported
 * @return A region pointer on success, NULL on failure
 */
struct kbase_va_region *kbase_tmem_import(kbase_context *kctx, base_tmem_import_type type, int handle, u64 * const pages);


/* OS specific functions */
struct kbase_va_region * kbase_lookup_cookie(kbase_context * kctx, mali_addr64 cookie);
void kbase_unlink_cookie(kbase_context * kctx, mali_addr64 cookie, struct kbase_va_region * reg);
mali_error kbase_mem_free(kbase_context *kctx, mali_addr64 gpu_addr);
mali_error kbase_mem_free_region(kbase_context *kctx,
                                 struct kbase_va_region *reg);
void kbase_os_mem_map_lock(kbase_context * kctx);
void kbase_os_mem_map_unlock(kbase_context * kctx);

/**
 * @brief Update the memory allocation counters for the current process
 *
 * OS specific call to updates the current memory allocation counters for the current process with
 * the supplied delta.
 *
 * @param[in] pages The desired delta to apply to the memory usage counters.
 */

void kbasep_os_process_page_usage_update( struct kbase_context * kctx, int pages );

/**
 * @brief Add to the memory allocation counters for the current process
 *
 * OS specific call to add to the current memory allocation counters for the current process by
 * the supplied amount.
 *
 * @param[in] kctx  The kernel base context used for the allocation.
 * @param[in] pages The desired delta to apply to the memory usage counters.
 */

static INLINE void kbase_process_page_usage_inc( struct kbase_context *kctx, int pages )
{
	kbasep_os_process_page_usage_update( kctx, pages );
}

/**
 * @brief Subtract from the memory allocation counters for the current process
 *
 * OS specific call to subtract from the current memory allocation counters for the current process by
 * the supplied amount.
 *
 * @param[in] kctx  The kernel base context used for the allocation.
 * @param[in] pages The desired delta to apply to the memory usage counters.
 */

static INLINE void kbase_process_page_usage_dec( struct kbase_context *kctx, int pages )
{
	kbasep_os_process_page_usage_update( kctx, 0 - pages );
}

/**
 * @brief Find a CPU mapping of a memory allocation containing a given address range
 *
 * Searches for a CPU mapping of any part of the region starting at @p gpu_addr that
 * fully encloses the CPU virtual address range specified by @p uaddr and @p size.
 * Returns a failure indication if only part of the address range lies within a
 * CPU mapping, or the address range lies within a CPU mapping of a different region.
 *
 * @param[in,out] kctx      The kernel base context used for the allocation.
 * @param[in]     gpu_addr  GPU address of the start of the allocated region
 *                          within which to search.
 * @param[in]     uaddr     Start of the CPU virtual address range.
 * @param[in]     size      Size of the CPU virtual address range (in bytes).
 *
 * @return A pointer to a descriptor of the CPU mapping that fully encloses
 *         the specified address range, or NULL if none was found.
 */
struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping(
                               kbase_context *kctx,
                               mali_addr64           gpu_addr,
                               osk_virt_addr         uaddr,
                               size_t                size );

/**
 * @brief Round TMem Growable no. pages to allow for HW workarounds/block allocators
 *
 * For success, the caller should check that the unsigned return value is
 * higher than the \a nr_pages parameter.
 *
 * @param[in] kbdev      The kernel base context used for the allocation
 * @param[in] nr_pages  Size value (in pages) to round
 *
 * @return the rounded-up number of pages (which may have wraped around to zero)
 */
static INLINE u32 kbasep_tmem_growable_round_size( kbase_device *kbdev, u32 nr_pages )
{
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9630))
	{
		return (nr_pages + KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_9630 - 1) & ~(KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_9630 - 1);
	}	
	else if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
	{
		return (nr_pages + KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_8316 - 1) & ~(KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_8316 - 1);
	}
	else
	{
		return (nr_pages + KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES - 1) & ~(KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES-1);
	}
}

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer * timer);
void kbase_as_poking_timer_retain(kbase_as * as);
void kbase_as_poking_timer_release(kbase_as * as);

/**
 * @brief Allocates physical pages.
 *
 * Allocates \a nr_pages_requested and updates the region object.
 *
 * @param[in]   reg       memory region in which physical pages are supposed to be allocated
 * @param[in]   nr_pages  number of physical pages to allocate
 *
 * @return MALI_ERROR_NONE if all pages have been successfully allocated. Error code otherwise
 */
mali_error kbase_alloc_phy_pages_helper(struct kbase_va_region *reg, u32 nr_pages_requested);

/**
 * @brief Free physical pages.
 *
 * Frees \a nr_pages and updates the region object.
 *
 * @param[in]   reg       memory region in which physical pages are supposed to be allocated
 * @param[in]   nr_pages  number of physical pages to free
 */
void kbase_free_phy_pages_helper(struct kbase_va_region * reg, u32 nr_pages);


#endif /* _KBASE_MEM_H_ */
