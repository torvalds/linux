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
#include <linux/kref.h>

#ifdef CONFIG_UMP
#include <linux/ump.h>
#endif				/* CONFIG_UMP */
#include <kbase/mali_base_kernel.h>
#include <kbase/src/common/mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"

/* Part of the workaround for uTLB invalid pages is to ensure we grow/shrink tmem by 4 pages at a time */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316 (2)	/* round to 4 pages */

/* Part of the workaround for PRLAM-9630 requires us to grow/shrink memory by 8 pages.
The MMU reads in 8 page table entries from memory at a time, if we have more than one page fault within the same 8 pages and
page tables are updated accordingly, the MMU does not re-read the page table entries from memory for the subsequent page table
updates and generates duplicate page faults as the page table information used by the MMU is not valid.   */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630 (3)	/* round to 8 pages */

#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2 (0)	/* round to 1 page */

/* This must always be a power of 2 */
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_8316 (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_8316)
#define KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_HW_ISSUE_9630 (1u << KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES_LOG2_HW_ISSUE_9630)
/**
 * A CPU mapping
 */
typedef struct kbase_cpu_mapping {
	struct  list_head mappings_list;
	struct  kbase_mem_phy_alloc *alloc;
	struct  vm_area_struct *vma;
	struct  kbase_context *kctx;
	struct  kbase_va_region *region;
	pgoff_t page_off;
	int     count;
} kbase_cpu_mapping;

enum kbase_memory_type {
	KBASE_MEM_TYPE_NATIVE,
	KBASE_MEM_TYPE_IMPORTED_UMP,
	KBASE_MEM_TYPE_IMPORTED_UMM,
	KBASE_MEM_TYPE_TB,
	KBASE_MEM_TYPE_RAW
};

/* physical pages tracking object.
 * Set up to track N pages.
 * N not stored here, the creator holds that info.
 * This object only tracks how many elements are actually valid (present).
 * Changing of nents or *pages should only happen if the kbase_mem_phy_alloc is not
 * shared with another region or client. CPU mappings are OK to exist when changing, as
 * long as the tracked mappings objects are updated as part of the change.
 */
struct kbase_mem_phy_alloc
{
	struct kref           kref; /* number of users of this alloc */
	size_t                nents; /* 0..N */
	phys_addr_t *         pages; /* N elements, only 0..nents are valid */

	/* kbase_cpu_mappings */
	struct list_head      mappings;

	/* type of buffer */
	enum kbase_memory_type type;

	int accessed_cached;

	/* member in union valid based on @a type */
	union {
#ifdef CONFIG_UMP
		ump_dd_handle ump_handle;
#endif /* CONFIG_UMP */
#if defined(CONFIG_DMA_SHARED_BUFFER)
		struct {
			struct dma_buf *dma_buf;
			struct dma_buf_attachment *dma_attachment;
			unsigned int current_mapping_usage_count;
			struct sg_table *sgt;
		} umm;
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) */
		/* Used by type = (KBASE_MEM_TYPE_NATIVE, KBASE_MEM_TYPE_TB) */
		struct kbase_context *kctx;
	} imported;
};

void kbase_mem_kref_free(struct kref * kref);

mali_error kbase_mem_init(kbase_device * kbdev);
void kbase_mem_halt(kbase_device * kbdev);
void kbase_mem_term(kbase_device * kbdev);

static inline struct kbase_mem_phy_alloc * kbase_mem_phy_alloc_get(struct kbase_mem_phy_alloc * alloc)
{
	kref_get(&alloc->kref);
	return alloc;
}

static inline struct kbase_mem_phy_alloc * kbase_mem_phy_alloc_put(struct kbase_mem_phy_alloc * alloc)
{
	kref_put(&alloc->kref, kbase_mem_kref_free);
	return NULL;
}


/**
 * A GPU memory region, and attributes for CPU mappings.
 */
typedef struct kbase_va_region {
	struct rb_node rblink;
	struct list_head link;

	kbase_context *kctx;	/* Backlink to base context */

	u64 start_pfn;		/* The PFN in GPU space */
	size_t nr_pages;

#define KBASE_REG_FREE       (1ul << 0)	/* Free region */
#define KBASE_REG_CPU_WR     (1ul << 1)	/* CPU write access */
#define KBASE_REG_GPU_WR     (1ul << 2)	/* GPU write access */
#define KBASE_REG_GPU_NX     (1ul << 3)	/* No eXecute flag */
#define KBASE_REG_CPU_CACHED (1ul << 4)	/* Is CPU cached? */
#define KBASE_REG_GPU_CACHED (1ul << 5)	/* Is GPU cached? */

#define KBASE_REG_GROWABLE   (1ul << 6)
#define KBASE_REG_PF_GROW    (1ul << 7)	/* Can grow on pf? */

#define KBASE_REG_CUSTOM_VA  (1ul << 8) /* VA managed by us */

#define KBASE_REG_SHARE_IN   (1ul << 9)	/* inner shareable coherency */
#define KBASE_REG_SHARE_BOTH (1ul << 10)	/* inner & outer shareable coherency */

#define KBASE_REG_ZONE_MASK  (3ul << 11)	/* Space for 4 different zones */
#define KBASE_REG_ZONE(x)    (((x) & 3) << 11)

#define KBASE_REG_GPU_RD     (1ul<<13)	/* GPU write access */
#define KBASE_REG_CPU_RD     (1ul<<14)	/* CPU read access */

#define KBASE_REG_ALIGNED    (1ul<<15) /* Aligned for GPU EX in SAME_VA */

#define KBASE_REG_FLAGS_NR_BITS    16	/* Number of bits used by kbase_va_region flags */

#define KBASE_REG_ZONE_SAME_VA  KBASE_REG_ZONE(0)

/* only used with 32-bit clients */
/*
 * On a 32bit platform, custom VA should be wired from (4GB + shader region)
 * to the VA limit of the GPU. Unfortunately, the Linux mmap() interface 
 * limits us to 2^32 pages (2^44 bytes, see mmap64 man page for reference).
 * So we put the default limit to the maximum possible on Linux and shrink
 * it down, if required by the GPU, during initialization.
 */
#define KBASE_REG_ZONE_EXEC         KBASE_REG_ZONE(1)	/* Dedicated 16MB region for shader code */
#define KBASE_REG_ZONE_EXEC_BASE    ((1ULL << 32) >> PAGE_SHIFT)
#define KBASE_REG_ZONE_EXEC_SIZE    ((16ULL * 1024 * 1024) >> PAGE_SHIFT)

#define KBASE_REG_ZONE_CUSTOM_VA         KBASE_REG_ZONE(2)
#define KBASE_REG_ZONE_CUSTOM_VA_BASE    (KBASE_REG_ZONE_EXEC_BASE + KBASE_REG_ZONE_EXEC_SIZE) /* Starting after KBASE_REG_ZONE_EXEC */
#define KBASE_REG_ZONE_CUSTOM_VA_SIZE    (((1ULL << 44) >> PAGE_SHIFT) - KBASE_REG_ZONE_CUSTOM_VA_BASE)
/* end 32-bit clients only */

#define KBASE_REG_COOKIE_MASK       (~((1ul << KBASE_REG_FLAGS_NR_BITS)-1))
#define KBASE_REG_COOKIE(x)         ((x << KBASE_REG_FLAGS_NR_BITS) & KBASE_REG_COOKIE_MASK)

/* The reserved cookie values */
#define KBASE_REG_COOKIE_RB         0
#define KBASE_REG_COOKIE_MMU_DUMP   1
#define KBASE_REG_COOKIE_TB         2
#define KBASE_REG_COOKIE_MTP        3
#define KBASE_REG_COOKIE_FIRST_FREE 4

/* Bit mask of cookies reserved for other uses */
#define KBASE_REG_RESERVED_COOKIES  ((1ULL << (KBASE_REG_COOKIE_FIRST_FREE))-1)

	unsigned long flags;

	size_t extent; /* nr of pages alloc'd on PF */

	struct kbase_mem_phy_alloc * alloc; /* the one alloc object we mmap to the GPU and CPU when mapping this region */

	/* non-NULL if this memory object is a kds_resource */
	struct kds_resource *kds_res;

} kbase_va_region;

/* Common functions */
static INLINE phys_addr_t *kbase_get_phy_pages(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->alloc);

	return reg->alloc->pages;
}

static INLINE size_t kbase_reg_current_backed_size(struct kbase_va_region * reg)
{
	KBASE_DEBUG_ASSERT(reg);
	/* if no alloc object the backed size naturally is 0 */
	if (reg->alloc)
		return reg->alloc->nents;
	else
		return 0;
}

static INLINE struct kbase_mem_phy_alloc * kbase_alloc_create(size_t nr_pages, enum kbase_memory_type type)
{
	struct kbase_mem_phy_alloc * alloc;
	const size_t extra_pages = (sizeof(*alloc) + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	/* Prevent nr_pages*sizeof + sizeof(*alloc) from wrapping around. */
	if (nr_pages > (((size_t) -1 / sizeof(*alloc->pages))) - extra_pages)
		return ERR_PTR(-ENOMEM);

	alloc = vzalloc(sizeof(*alloc) + sizeof(*alloc->pages) * nr_pages);
	if (!alloc) 
		return ERR_PTR(-ENOMEM);

	kref_init(&alloc->kref);
	alloc->nents = 0;
	alloc->pages = (void*)(alloc + 1);
	INIT_LIST_HEAD(&alloc->mappings);
	alloc->type = type;

	return alloc;
}

static INLINE int kbase_reg_prepare_native(struct kbase_va_region * reg, struct kbase_context * kctx)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(!reg->alloc);
	KBASE_DEBUG_ASSERT(reg->flags & KBASE_REG_FREE);

	reg->alloc = kbase_alloc_create(reg->nr_pages, KBASE_MEM_TYPE_NATIVE);
	if (IS_ERR(reg->alloc))
		return PTR_ERR(reg->alloc);
	else if (!reg->alloc)
		return -ENOMEM;
	reg->alloc->imported.kctx = kctx;
	reg->flags &= ~KBASE_REG_FREE;
	return 0;
}




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
 * @param max_size Maximum number of pages to keep on the freelist.
 * @return MALI_ERROR_NONE on success, an error code indicating what failed on error.
 */
mali_error kbase_mem_allocator_init(kbase_mem_allocator * allocator, unsigned int max_size);

/**
 * @brief Allocate memory via an OS based memory allocator.
 *
 * @param[in] allocator Allocator to obtain the memory from
 * @param nr_pages Number of pages to allocate
 * @param[out] pages Pointer to an array where the physical address of the allocated pages will be stored
 * @return MALI_ERROR_NONE if the pages were allocated, an error code indicating what failed on error
 */
mali_error kbase_mem_allocator_alloc(kbase_mem_allocator * allocator, size_t nr_pages, phys_addr_t *pages);

/**
 * @brief Free memory obtained for an OS based memory allocator.
 *
 * @param[in] allocator Allocator to free the memory back to
 * @param nr_pages Number of pages to free
 * @param[in] pages Pointer to an array holding the physical address of the paghes to free.
 * @param[in] sync_back MALI_TRUE case the memory should be synced back
 */
void kbase_mem_allocator_free(kbase_mem_allocator * allocator, size_t nr_pages, phys_addr_t *pages, mali_bool sync_back);

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
mali_error kbase_mem_usage_init(kbasep_mem_usage *usage, size_t max_pages);

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
mali_error kbase_mem_usage_request_pages(kbasep_mem_usage *usage, size_t nr_pages);

/*
 * @brief Release a number of pages from the given context.
 *
 * @param[in]    usage     usage tracker
 * @param[in]    nr_pages  number of pages to be released
 */
void kbase_mem_usage_release_pages(kbasep_mem_usage *usage, size_t nr_pages);

mali_error kbase_region_tracker_init(kbase_context *kctx);
void kbase_region_tracker_term(kbase_context *kctx);

struct kbase_va_region *kbase_region_tracker_find_region_enclosing_range(kbase_context *kctx, u64 start_pgoff, size_t nr_pages);

struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(kbase_context *kctx, mali_addr64 gpu_addr);

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Must be called with context lock held.
 */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(kbase_context *kctx, mali_addr64 gpu_addr);

struct kbase_va_region *kbase_alloc_free_region(kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
mali_error kbase_add_va_region(kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);

mali_error kbase_gpu_mmap(kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);
mali_bool kbase_check_alloc_flags(unsigned long flags);
void kbase_update_region_flags(struct kbase_va_region *reg, unsigned long flags);

void kbase_gpu_vm_lock(kbase_context *kctx);
void kbase_gpu_vm_unlock(kbase_context *kctx);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size);

mali_error kbase_mmu_init(kbase_context *kctx);
void kbase_mmu_term(kbase_context *kctx);

phys_addr_t kbase_mmu_alloc_pgd(kbase_context *kctx);
void kbase_mmu_free_pgd(kbase_context *kctx);
mali_error kbase_mmu_insert_pages(kbase_context *kctx, u64 vpfn, phys_addr_t *phys, size_t nr, unsigned long flags);
mali_error kbase_mmu_teardown_pages(kbase_context *kctx, u64 vpfn, size_t nr);
mali_error kbase_mmu_update_pages(kbase_context* kctx, u64 vpfn, phys_addr_t* phys, size_t nr, unsigned long flags);

/**
 * @brief Register region and map it on the GPU.
 *
 * Call kbase_add_va_region() and map the region on the GPU.
 */
mali_error kbase_gpu_mmap(kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);

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
void kbase_mmu_disable(kbase_context *kctx);

void kbase_mmu_interrupt(kbase_device *kbdev, u32 irq_stat);

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
void *kbase_mmu_dump(kbase_context *kctx, int nr_pages);

mali_error kbase_sync_now(kbase_context *kctx, base_syncset *syncset);
void kbase_pre_job_sync(kbase_context *kctx, base_syncset *syncsets, size_t nr);
void kbase_post_job_sync(kbase_context *kctx, base_syncset *syncsets, size_t nr);

/**
 * Set attributes for imported tmem region
 *
 * This function sets (extends with) requested attributes for given region
 * of imported external memory
 *
 * @param[in]  kctx  	    The kbase context which the tmem belongs to
 * @param[in]  gpu_addr     The base address of the tmem region
 * @param[in]  attributes   The attributes of tmem region to be set
 *
 * @return MALI_ERROR_NONE on success.  Any other value indicates failure.
 */
mali_error kbase_tmem_set_attributes(kbase_context *kctx, mali_addr64 gpu_adr, u32  attributes );

/**
 * Get attributes of imported tmem region
 *
 * This function retrieves the attributes of imported external memory
 *
 * @param[in]  kctx  	    The kbase context which the tmem belongs to
 * @param[in]  gpu_addr     The base address of the tmem region
 * @param[out] attributes   The actual attributes of tmem region
 *
 * @return MALI_ERROR_NONE on success.  Any other value indicates failure.
 */
mali_error kbase_tmem_get_attributes(kbase_context *kctx, mali_addr64 gpu_adr, u32 * const attributes );

/* OS specific functions */
struct kbase_va_region *kbase_lookup_cookie(kbase_context *kctx, mali_addr64 cookie);
void kbase_unlink_cookie(kbase_context *kctx, mali_addr64 cookie, struct kbase_va_region *reg);
mali_error kbase_mem_free(kbase_context *kctx, mali_addr64 gpu_addr);
mali_error kbase_mem_free_region(kbase_context *kctx, struct kbase_va_region *reg);
void kbase_os_mem_map_lock(kbase_context *kctx);
void kbase_os_mem_map_unlock(kbase_context *kctx);

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
struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping(kbase_context *kctx, mali_addr64 gpu_addr, unsigned long uaddr, size_t size);

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer *timer);
void kbase_as_poking_timer_retain_atom(kbase_device *kbdev, kbase_context *kctx, kbase_jd_atom *katom);
void kbase_as_poking_timer_release_atom(kbase_device *kbdev, kbase_context *kctx, kbase_jd_atom *katom);

/**
* @brief Allocates physical pages.
*
* Allocates \a nr_pages_requested and updates the alloc object.
*
* @param[in] alloc allocation object to add pages to
* @param[in] nr_pages number of physical pages to allocate
*
* @return 0 if all pages have been successfully allocated. Error code otherwise
*/
int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc * alloc, size_t nr_pages_requested);

/**
* @brief Free physical pages.
*
* Frees \a nr_pages and updates the alloc object.
*
* @param[in] alloc allocation object to free pages from
* @param[in] nr_pages number of physical pages to free
*/
int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc * alloc, size_t nr_pages_to_free);

#ifdef CONFIG_MALI_NO_MALI
static inline void kbase_wait_write_flush(kbase_context *kctx)
{
}
#else
void kbase_wait_write_flush(kbase_context *kctx);
#endif


#endif				/* _KBASE_MEM_H_ */
