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
#if MALI_USE_UMP
#ifndef __KERNEL__
#include <ump/src/library/common/ump_user.h>
#endif
#include <ump/ump_kernel_interface.h>
#endif /* MALI_USE_UMP */
#include <kbase/mali_base_kernel.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"

#if BASE_HW_ISSUE_8316 != 0
/* Part of the workaround for uTLB invalid pages is to ensure we grow/shrink tmem by 4 pages at a time */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2 2 /* round to 4 pages */
#else
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2 0 /* round to 1 page */
#endif /* BASE_HW_ISSUE_8316 != 0 */

/* This must always be a power of 2 */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2)

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
 * A physical memory (sub-)commit
 */
typedef struct kbase_mem_commit
{
	osk_phy_allocator *           allocator;
	u32                           nr_pages;
	struct kbase_mem_commit *     prev;
	/* 
	 * The offset of the commit is implict by
	 * the prev_commit link position of this node 
	 */
} kbase_mem_commit;

/**
 * A GPU memory region, and attributes for CPU mappings.
 */  
typedef struct kbase_va_region
{
	osk_dlist_item          link;

	struct kbase_context    *kctx; /* Backlink to base context */

	u64                     start_pfn;  /* The PFN in GPU space */
	u32                     nr_pages;   /* VA size */

#define KBASE_REG_FREE       (1ul << 0) /* Free region */
#define KBASE_REG_CPU_RW     (1ul << 1) /* CPU write access */
#define KBASE_REG_GPU_RW     (1ul << 2) /* GPU write access */
#define KBASE_REG_GPU_NX     (1ul << 3) /* No eXectue flag */
#define KBASE_REG_CPU_CACHED (1ul << 4) /* Is CPU cached? */
#define KBASE_REG_GPU_CACHED (1ul << 5) /* Is GPU cached? */

#define KBASE_REG_GROWABLE   (1ul << 6) /* Is growable? */
#define KBASE_REG_PF_GROW    (1ul << 7) /* Can grow on pf? */

#define KBASE_REG_IS_RB      (1ul << 8) /* Is ringbuffer? */
#define KBASE_REG_IS_UMP     (1ul << 9) /* Is UMP? */
#define KBASE_REG_IS_MMU_DUMP (1ul << 10) /* Is an MMU dump */
#define KBASE_REG_IS_TB      (1ul << 11) /* Is register trace buffer? */

#define KBASE_REG_SHARE_IN   (1ul << 12) /* inner shareable coherency */
#define KBASE_REG_SHARE_BOTH (1ul << 13) /* inner & outer shareable coherency */

#define KBASE_REG_ZONE_MASK  (3ul << 14) /* Space for 4 different zones. Only use 3 for now */
#define KBASE_REG_ZONE(x)    (((x) & 3) << 14)

#define KBASE_REG_FLAGS_NR_BITS      16  /* Number of bits used by kbase_va_region flags */

#define KBASE_REG_ZONE_PMEM  KBASE_REG_ZONE(0)

#ifndef KBASE_REG_ZONE_TMEM  /* To become 0 on a 64bit platform */
/*
 * On a 32bit platform, TMEM should be wired from 4GB to the VA limit
 * of the GPU, which is currently hardcoded at 48 bits. Unfortunately,
 * the Linux mmap() interface limits us to 2^32 pages (2^44 bytes, see
 * mmap64 man page for reference).
 */
#define KBASE_REG_ZONE_TMEM         KBASE_REG_ZONE(1)
#define KBASE_REG_ZONE_TMEM_BASE    ((1ULL << 32) >> OSK_PAGE_SHIFT)
#define KBASE_REG_ZONE_TMEM_SIZE    (((1ULL << 44) >> OSK_PAGE_SHIFT) - \
                                    KBASE_REG_ZONE_TMEM_BASE)
#endif

#define KBASE_REG_COOKIE_MASK       (0xFFFF << 16)
#define KBASE_REG_COOKIE(x)         (((x) & 0xFFFF) << 16)
/* Bit mask of cookies that not used for PMEM but reserved for other uses */
#define KBASE_REG_RESERVED_COOKIES  7ULL
/* The reserved cookie values */
#define KBASE_REG_COOKIE_RB         0
#define KBASE_REG_COOKIE_MMU_DUMP   1
#define KBASE_REG_COOKIE_TB         2

	u32                 flags;

	u32                 nr_alloc_pages; /* nr of pages allocated */
	u32                 extent;         /* nr of pages alloc'd on PF */

	/* two variables to track our physical commits: */

	/* We always have a root commit.
	 * Most allocation will only have this one.
	 * */
	kbase_mem_commit    root_commit;

	/* This one is initialized to point to the root_commit,
	 * but if a new and separate commit is needed it will point
	 * to the last (still valid) commit we've done */
	kbase_mem_commit *  last_commit;

	void                *ump_handle;
	osk_phy_addr        *phy_pages;

	osk_dlist           map_list;
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

/**
 * @brief Allocate physical memory and track shared OS memory usage.
 *
 * This function is kbase wrapper of osk_phy_pages_alloc. Apart from allocating memory it also tracks shared OS memory
 * usage and fails whenever shared memory limits would be exceeded.
 *
 * @param[in] kbdev     pointer to kbase_device structure for which memory is allocated
 * @param[in] allocator initialized physical allocator
 * @param[in] nr_pages  number of physical pages to allocate
 * @param[out] pages    array of \a nr_pages elements storing the physical
 *                      address of an allocated page
 * @return The number of pages successfully allocated,
 * which might be lower than requested, including zero pages.
 *
 * @see ::osk_phy_pages_alloc
 */
u32 kbase_phy_pages_alloc(struct kbase_device *kbdev, osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages);

/**
 * @brief Free physical memory and track shared memory usage
 *
 * This function, like osk_phy_pages_free, frees physical memory but also tracks shared OS memory usage.
 *
 * @param[in] kbdev     pointer to kbase_device for which memory is allocated
 * @param[in] allocator initialized physical allocator
 * @param[in] nr_pages  number of physical pages to allocate
 * @param[out] pages    array of \a nr_pages elements storing the physical
 *                      address of an allocated page
 *
 * @see ::osk_phy_pages_free
 */
void kbase_phy_pages_free(struct kbase_device *kbdev, osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages);

/**
 * @brief Register shared and dedicated memory regions
 *
 * Function registers shared and dedicated memory regions (registers physical allocator for each region)
 * using given configuration attributes. Additionally, several ordered lists of physical allocators are created with
 * different sort order (based on CPU, GPU, CPU+GPU performance and order in config). If there are many memory regions
 * with the same performance, then order in which they appeared in config is important. Shared OS memory is treated as if
 * it's defined after dedicated memory regions, so unless it matches region's performance flags better, it's chosen last.
 *
 * @param[in] kbdev       pointer to kbase_device for which regions are registered
 * @param[in] attributes  array of configuration attributes. It must be terminated with KBASE_CONFIG_ATTR_END attribute
 *
 * @return MALI_ERROR_NONE if no error occurred. Error code otherwise
 *
 * @see ::kbase_alloc_phy_pages_helper
 */
mali_error kbase_register_memory_regions(kbase_device * kbdev, const kbase_attribute *attributes);

/**
 * @brief Frees memory regions registered for the given device.
 *
 * @param[in] kbdev       pointer to kbase device for which memory regions are to be freed
 */
void kbase_free_memory_regions(kbase_device * kbdev);

mali_error kbase_mem_init(kbase_device * kbdev);
void       kbase_mem_halt(kbase_device * kbdev);
void       kbase_mem_term(kbase_device * kbdev);


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

struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, u32 nr_pages, u32 zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
mali_error kbase_add_va_region(struct kbase_context *kctx,
                               struct kbase_va_region *reg,
                               mali_addr64 addr, u32 nr_pages,
                               u32 align);
kbase_va_region *kbase_region_lookup(kbase_context *kctx, mali_addr64 gpu_addr);

mali_error kbase_gpu_mmap(struct kbase_context *kctx,
                          struct kbase_va_region *reg,
                          mali_addr64 addr, u32 nr_pages,
                          u32 align);
mali_bool kbase_check_alloc_flags(u32 flags);
void kbase_update_region_flags(struct kbase_va_region *reg, u32 flags, mali_bool is_growable);

void kbase_gpu_vm_lock(struct kbase_context *kctx);
void kbase_gpu_vm_unlock(struct kbase_context *kctx);

void kbase_free_phy_pages(struct kbase_va_region *reg);
int kbase_alloc_phy_pages(struct kbase_va_region *reg, u32 vsize, u32 size);

mali_error kbase_cpu_free_mapping(struct kbase_va_region *reg, const void *ptr);

mali_error kbase_mmu_init(struct kbase_context *kctx);
void kbase_mmu_term(struct kbase_context *kctx);

osk_phy_addr kbase_mmu_alloc_pgd(kbase_context *kctx);
void kbase_mmu_free_pgd(struct kbase_context *kctx);
mali_error kbase_mmu_insert_pages(struct kbase_context *kctx, u64 vpfn,
                                  osk_phy_addr *phys, u32 nr, u16 flags);
mali_error kbase_mmu_teardown_pages(struct kbase_context *kctx, u64 vpfn, u32 nr);

/**
 * The caller has the following locking conditions:
 * - It must hold kbase_as::transaction_mutex on kctx's address space
 * - It must hold the kbasep_js_device_data::runpool_irq::lock
 */
void kbase_mmu_update(struct kbase_context *kctx);

/**
 * The caller has the following locking conditions:
 * - It must hold kbase_as::transaction_mutex on kctx's address space
 * - It must hold the kbasep_js_device_data::runpool_irq::lock
 */
void kbase_mmu_disable (kbase_context *kctx);

void kbase_mmu_interrupt(kbase_device * kbdev, u32 irq_stat);

/**
 * @brief Allocates physical pages using registered physical allocators.
 *
 * Function allocates physical pages using registered physical allocators. Allocator list is iterated until all pages
 * are successfully allocated. Function tries to match the most appropriate order of iteration basing on
 * KBASE_REG_CPU_CACHED and KBASE_REG_GPU_CACHED flags of the region.
 *
 * @param[in]   reg       memory region in which physical pages are supposed to be allocated
 * @param[in]   nr_pages  number of physical pages to allocate
 *
 * @return MALI_ERROR_NONE if all pages have been successfully allocated. Error code otherwise
 *
 * @see kbase_register_memory_regions
 */
mali_error kbase_alloc_phy_pages_helper(kbase_va_region *reg, u32 nr_pages);

/** Dump the MMU tables to a buffer
 *
 * This function allocates a buffer (of @c nr_pages pages) to hold a dump of the MMU tables and fills it. If the 
 * buffer is too small then the return value will be NULL.
 *
 * The GPU vm lock must be held when calling this function.
 *
 * The buffer returned should be freed with @ref osk_vfree when it is no longer required.
 *
 * @param[in]   kctx        The kbase context to dump
 * @param[in]   nr_pages    The number of pages to allocate for the buffer.
 *
 * @return The address of the buffer containing the MMU dump or NULL on error (including if the @c nr_pages is too 
 * small)
 */
void *kbase_mmu_dump(struct kbase_context *kctx,int nr_pages);

mali_error kbase_sync_now(kbase_context *kctx, base_syncset *syncset);
void kbase_pre_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr);
void kbase_post_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr);

struct kbase_va_region *kbase_tmem_alloc(struct kbase_context *kctx,
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
mali_error kbase_tmem_resize(struct kbase_context *kctx, mali_addr64 gpu_addr, s32 delta, u32 *size, base_backing_threshold_status * failure_reason);

#if MALI_USE_UMP
struct kbase_va_region *kbase_tmem_from_ump(struct kbase_context *kctx, ump_secure_id id, u64 * const pages);
#endif /* MALI_USE_UMP */


/* OS specific functions */
struct kbase_va_region * kbase_lookup_cookie(struct kbase_context * kctx, mali_addr64 cookie);
void kbase_unlink_cookie(struct kbase_context * kctx, mali_addr64 cookie, struct kbase_va_region * reg);
mali_error kbase_mem_free(struct kbase_context *kctx, mali_addr64 gpu_addr);
mali_error kbase_mem_free_region(struct kbase_context *kctx,
                                 struct kbase_va_region *reg);
void kbase_os_mem_map_lock(struct kbase_context * kctx);
void kbase_os_mem_map_unlock(struct kbase_context * kctx);

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
                               struct kbase_context *kctx,
                               mali_addr64           gpu_addr,
                               osk_virt_addr         uaddr,
                               size_t                size );

/**
 * @brief Round TMem Growable no. pages to allow for HW workarounds/block allocators
 *
 * For success, the caller should check that the unsigned return value is
 * higher than the \a nr_pages parameter.
 *
 * @param[in] nr_pages Size value (in pages) to round
 * @return the rounded-up number of pages (which may have wraped around to zero)
 */
static INLINE u32 kbasep_tmem_growable_round_size( u32 nr_pages )
{
	return (nr_pages + KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES - 1) & ~(KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES-1);
}

#if BASE_HW_ISSUE_8316
void kbasep_as_poke_timer_callback(void* arg);
void kbase_as_poking_timer_retain(kbase_as * as);
void kbase_as_poking_timer_release(kbase_as * as);
#endif /* BASE_HW_ISSUE_8316 */


#endif /* _KBASE_MEM_H_ */
