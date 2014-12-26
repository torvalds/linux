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
#include "mali_base_kernel.h"
#include <mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"
#ifdef CONFIG_MALI_GATOR_SUPPORT
#include "mali_kbase_gator.h"
#endif  /*CONFIG_MALI_GATOR_SUPPORT*/

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
	struct   list_head mappings_list;
	struct   kbase_mem_phy_alloc *alloc;
	struct   kbase_context *kctx;
	struct   kbase_va_region *region;
	pgoff_t  page_off;
	int      count;
	unsigned long vm_start;
	unsigned long vm_end;
} kbase_cpu_mapping;

enum kbase_memory_type {
	KBASE_MEM_TYPE_NATIVE,
	KBASE_MEM_TYPE_IMPORTED_UMP,
	KBASE_MEM_TYPE_IMPORTED_UMM,
	KBASE_MEM_TYPE_ALIAS,
	KBASE_MEM_TYPE_TB,
	KBASE_MEM_TYPE_RAW
};

/* internal structure, mirroring base_mem_aliasing_info,
 * but with alloc instead of a gpu va (handle) */
struct kbase_aliased {
	struct kbase_mem_phy_alloc *alloc; /* NULL for special, non-NULL for native */
	u64 offset; /* in pages */
	u64 length; /* in pages */
};

/**
 * @brief Physical pages tracking object properties
  */
#define KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED  (1ul << 0)
#define KBASE_MEM_PHY_ALLOC_LARGE            (1ul << 1)

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
	atomic_t              gpu_mappings;
	size_t                nents; /* 0..N */
	phys_addr_t *         pages; /* N elements, only 0..nents are valid */

	/* kbase_cpu_mappings */
	struct list_head      mappings;

	/* type of buffer */
	enum kbase_memory_type type;

	unsigned long properties;

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
		struct {
			mali_size64 stride;
			size_t nents;
			struct kbase_aliased *aliased;
		} alias;
		/* Used by type = (KBASE_MEM_TYPE_NATIVE, KBASE_MEM_TYPE_TB) */
		struct kbase_context *kctx;
	} imported;
};

static inline void kbase_mem_phy_alloc_gpu_mapped(struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(alloc);
	/* we only track mappings of NATIVE buffers */
	if (alloc->type == KBASE_MEM_TYPE_NATIVE)
		atomic_inc(&alloc->gpu_mappings);
}

static inline void kbase_mem_phy_alloc_gpu_unmapped(struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(alloc);
	/* we only track mappings of NATIVE buffers */
	if (alloc->type == KBASE_MEM_TYPE_NATIVE)
		if (0 > atomic_dec_return(&alloc->gpu_mappings)) {
			pr_err("Mismatched %s:\n", __func__);
			dump_stack();
		}
}

void kbase_mem_kref_free(struct kref * kref);

mali_error kbase_mem_init(struct kbase_device * kbdev);
void kbase_mem_halt(struct kbase_device * kbdev);
void kbase_mem_term(struct kbase_device * kbdev);

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

	struct kbase_context *kctx;	/* Backlink to base context */

	u64 start_pfn;		/* The PFN in GPU space */
	size_t nr_pages;

/* Free region */
#define KBASE_REG_FREE              (1ul << 0)
/* CPU write access */
#define KBASE_REG_CPU_WR            (1ul << 1)
/* GPU write access */
#define KBASE_REG_GPU_WR            (1ul << 2)
/* No eXecute flag */
#define KBASE_REG_GPU_NX            (1ul << 3)
/* Is CPU cached? */
#define KBASE_REG_CPU_CACHED        (1ul << 4)
/* Is GPU cached? */
#define KBASE_REG_GPU_CACHED        (1ul << 5)

#define KBASE_REG_GROWABLE          (1ul << 6)
/* Can grow on pf? */
#define KBASE_REG_PF_GROW           (1ul << 7)

/* VA managed by us */
#define KBASE_REG_CUSTOM_VA         (1ul << 8)

/* inner shareable coherency */
#define KBASE_REG_SHARE_IN          (1ul << 9)
/* inner & outer shareable coherency */
#define KBASE_REG_SHARE_BOTH        (1ul << 10)

/* Space for 4 different zones */
#define KBASE_REG_ZONE_MASK         (3ul << 11)
#define KBASE_REG_ZONE(x)           (((x) & 3) << 11)

/* GPU read access */
#define KBASE_REG_GPU_RD            (1ul<<13)
/* CPU read access */
#define KBASE_REG_CPU_RD            (1ul<<14)

/* Aligned for GPU EX in SAME_VA */
#define KBASE_REG_ALIGNED           (1ul<<15)

/* Index of chosen MEMATTR for this region (0..7) */
#define KBASE_REG_MEMATTR_MASK      (7ul << 16)
#define KBASE_REG_MEMATTR_INDEX(x)  (((x) & 7) << 16)
#define KBASE_REG_MEMATTR_VALUE(x)  (((x) & KBASE_REG_MEMATTR_MASK) >> 16)

#define KBASE_REG_ZONE_SAME_VA      KBASE_REG_ZONE(0)

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

#define KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD ((size_t)(4*1024)) /* size above which vmalloc is used over kmalloc */

static INLINE struct kbase_mem_phy_alloc * kbase_alloc_create(size_t nr_pages, enum kbase_memory_type type)
{
	struct kbase_mem_phy_alloc *alloc;
	const size_t alloc_size =
			sizeof(*alloc) + sizeof(*alloc->pages) * nr_pages;

	/* Prevent nr_pages*sizeof + sizeof(*alloc) from wrapping around. */
	if (nr_pages > ((((size_t) -1) - sizeof(*alloc))
			/ sizeof(*alloc->pages)))
		return ERR_PTR(-ENOMEM);

	/* Allocate based on the size to reduce internal fragmentation of vmem */
	if (alloc_size > KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD)
		alloc = vzalloc(alloc_size);
	else
		alloc = kzalloc(alloc_size, GFP_KERNEL);

	if (!alloc)
		return ERR_PTR(-ENOMEM);

	/* Store allocation method */
	if (alloc_size > KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD)
		alloc->properties |= KBASE_MEM_PHY_ALLOC_LARGE;

	kref_init(&alloc->kref);
	atomic_set(&alloc->gpu_mappings, 0);
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

static inline int kbase_atomic_add_pages(int num_pages, atomic_t *used_pages)
{
	int new_val = atomic_add_return(num_pages, used_pages);
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_total_alloc_pages_change((long long int)new_val);
#endif
	return new_val;
}

static inline int kbase_atomic_sub_pages(int num_pages, atomic_t *used_pages)
{
	int new_val = atomic_sub_return(num_pages, used_pages);
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_total_alloc_pages_change((long long int)new_val);
#endif
	return new_val;
}

/**
 * @brief Initialize low-level memory access for a kbase device
 *
 * Performs any low-level setup needed for a kbase device to access memory on
 * the device.
 *
 * @param kbdev kbase device to initialize memory access for
 * @return 0 on success, Linux error code on failure
 */
int kbase_mem_lowlevel_init(struct kbase_device *kbdev);


/**
 * @brief Terminate low-level memory access for a kbase device
 *
 * Perform any low-level cleanup needed to clean
 * after @ref kbase_mem_lowlevel_init
 *
 * @param kbdev kbase device to clean up for
 */
void kbase_mem_lowlevel_term(struct kbase_device *kbdev);

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
 * @param kbdev The kbase device this allocator is used with
 * @return MALI_ERROR_NONE on success, an error code indicating what failed on
 * error.
 */
mali_error kbase_mem_allocator_init(struct kbase_mem_allocator *allocator,
				    unsigned int max_size,
				    struct kbase_device *kbdev);

/**
 * @brief Allocate memory via an OS based memory allocator.
 *
 * @param[in] allocator Allocator to obtain the memory from
 * @param nr_pages Number of pages to allocate
 * @param[out] pages Pointer to an array where the physical address of the allocated pages will be stored
 * @return MALI_ERROR_NONE if the pages were allocated, an error code indicating what failed on error
 */
mali_error kbase_mem_allocator_alloc(struct kbase_mem_allocator * allocator, size_t nr_pages, phys_addr_t *pages);

/**
 * @brief Free memory obtained for an OS based memory allocator.
 *
 * @param[in] allocator Allocator to free the memory back to
 * @param nr_pages Number of pages to free
 * @param[in] pages Pointer to an array holding the physical address of the paghes to free.
 * @param[in] sync_back MALI_TRUE case the memory should be synced back
 */
void kbase_mem_allocator_free(struct kbase_mem_allocator * allocator, size_t nr_pages, phys_addr_t *pages, mali_bool sync_back);

/**
 * @brief Terminate an OS based memory allocator.
 *
 * Frees all cached allocations and clean up internal state.
 * All allocate pages must have been \a kbase_mem_allocator_free before
 * this function is called.
 *
 * @param[in] allocator Allocator to terminate
 */
void kbase_mem_allocator_term(struct kbase_mem_allocator * allocator);



mali_error kbase_region_tracker_init(struct kbase_context *kctx);
void kbase_region_tracker_term(struct kbase_context *kctx);

struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(struct kbase_context *kctx, mali_addr64 gpu_addr);

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Must be called with context lock held.
 */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(struct kbase_context *kctx, mali_addr64 gpu_addr);

struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
mali_error kbase_add_va_region(struct kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);

mali_error kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);
mali_bool kbase_check_alloc_flags(unsigned long flags);
void kbase_update_region_flags(struct kbase_va_region *reg, unsigned long flags);

void kbase_gpu_vm_lock(struct kbase_context *kctx);
void kbase_gpu_vm_unlock(struct kbase_context *kctx);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size);

mali_error kbase_mmu_init(struct kbase_context *kctx);
void kbase_mmu_term(struct kbase_context *kctx);

phys_addr_t kbase_mmu_alloc_pgd(struct kbase_context *kctx);
void kbase_mmu_free_pgd(struct kbase_context *kctx);
mali_error kbase_mmu_insert_pages(struct kbase_context *kctx, u64 vpfn,
				  phys_addr_t *phys, size_t nr,
				  unsigned long flags);
mali_error kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 vpfn,
					phys_addr_t phys, size_t nr,
					unsigned long flags);

mali_error kbase_mmu_teardown_pages(struct kbase_context *kctx, u64 vpfn, size_t nr);
mali_error kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, phys_addr_t* phys, size_t nr, unsigned long flags);

/**
 * @brief Register region and map it on the GPU.
 *
 * Call kbase_add_va_region() and map the region on the GPU.
 */
mali_error kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align);

/**
 * @brief Remove the region from the GPU and unregister it.
 *
 * Must be called with context lock held.
 */
mali_error kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg);

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
void kbase_mmu_disable(struct kbase_context *kctx);

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat);

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
void *kbase_mmu_dump(struct kbase_context *kctx, int nr_pages);

mali_error kbase_sync_now(struct kbase_context *kctx, struct base_syncset *syncset);
void kbase_sync_single(struct kbase_context *kctx, phys_addr_t pa,
		size_t size, kbase_sync_kmem_fn sync_fn);
void kbase_pre_job_sync(struct kbase_context *kctx, struct base_syncset *syncsets, size_t nr);
void kbase_post_job_sync(struct kbase_context *kctx, struct base_syncset *syncsets, size_t nr);

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
mali_error kbase_tmem_set_attributes(struct kbase_context *kctx, mali_addr64 gpu_addr, u32  attributes);

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
mali_error kbase_tmem_get_attributes(struct kbase_context *kctx, mali_addr64 gpu_addr, u32 * const attributes);

/* OS specific functions */
mali_error kbase_mem_free(struct kbase_context *kctx, mali_addr64 gpu_addr);
mali_error kbase_mem_free_region(struct kbase_context *kctx, struct kbase_va_region *reg);
void kbase_os_mem_map_lock(struct kbase_context *kctx);
void kbase_os_mem_map_unlock(struct kbase_context *kctx);

/**
 * @brief Update the memory allocation counters for the current process
 *
 * OS specific call to updates the current memory allocation counters for the current process with
 * the supplied delta.
 *
 * @param[in] kctx  The kbase context 
 * @param[in] pages The desired delta to apply to the memory usage counters.
 */

void kbasep_os_process_page_usage_update(struct kbase_context *kctx, int pages);

/**
 * @brief Add to the memory allocation counters for the current process
 *
 * OS specific call to add to the current memory allocation counters for the current process by
 * the supplied amount.
 *
 * @param[in] kctx  The kernel base context used for the allocation.
 * @param[in] pages The desired delta to apply to the memory usage counters.
 */

static INLINE void kbase_process_page_usage_inc(struct kbase_context *kctx, int pages)
{
	kbasep_os_process_page_usage_update(kctx, pages);
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

static INLINE void kbase_process_page_usage_dec(struct kbase_context *kctx, int pages)
{
	kbasep_os_process_page_usage_update(kctx, 0 - pages);
}

/**
 * @brief Find the offset of the CPU mapping of a memory allocation containing
 *        a given address range
 *
 * Searches for a CPU mapping of any part of the region starting at @p gpu_addr
 * that fully encloses the CPU virtual address range specified by @p uaddr and
 * @p size. Returns a failure indication if only part of the address range lies
 * within a CPU mapping, or the address range lies within a CPU mapping of a
 * different region.
 *
 * @param[in,out] kctx      The kernel base context used for the allocation.
 * @param[in]     gpu_addr  GPU address of the start of the allocated region
 *                          within which to search.
 * @param[in]     uaddr     Start of the CPU virtual address range.
 * @param[in]     size      Size of the CPU virtual address range (in bytes).
 * @param[out]    offset    The offset from the start of the allocation to the
 *                          specified CPU virtual address.
 *
 * @return MALI_ERROR_NONE if offset was obtained successfully. Error code
 *         otherwise.
 */
mali_error kbasep_find_enclosing_cpu_mapping_offset(struct kbase_context *kctx,
							mali_addr64 gpu_addr,
							unsigned long uaddr,
							size_t size,
							mali_size64 *offset);

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer *timer);
void kbase_as_poking_timer_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom);
void kbase_as_poking_timer_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
* @brief Allocates physical pages.
*
* Allocates \a nr_pages_requested and updates the alloc object.
*
* @param[in] alloc allocation object to add pages to
* @param[in] nr_pages_requested number of physical pages to allocate
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
* @param[in] nr_pages_to_free number of physical pages to free
*/
int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc * alloc, size_t nr_pages_to_free);

#ifdef CONFIG_MALI_NO_MALI
static inline void kbase_wait_write_flush(struct kbase_context *kctx)
{
}
#else
void kbase_wait_write_flush(struct kbase_context *kctx);
#endif

static inline void kbase_set_dma_addr(struct page *p, dma_addr_t dma_addr)
{
	SetPagePrivate(p);
	if (sizeof(dma_addr_t) > sizeof(p->private)) {
		/* on 32-bit ARM with LPAE dma_addr_t becomes larger, but the
		 * private filed stays the same. So we have to be clever and
		 * use the fact that we only store DMA addresses of whole pages,
		 * so the low bits should be zero */
		KBASE_DEBUG_ASSERT(!(dma_addr & (PAGE_SIZE - 1)));
		set_page_private(p, dma_addr >> PAGE_SHIFT);
	} else {
		set_page_private(p, dma_addr);
	}
}

static inline dma_addr_t kbase_dma_addr(struct page *p)
{
	if (sizeof(dma_addr_t) > sizeof(p->private))
		return ((dma_addr_t)page_private(p)) << PAGE_SHIFT;

	return (dma_addr_t)page_private(p);
}

/**
* @brief Process a bus or page fault.
*
* This function will process a fault on a specific address space
*
* @param[in] kbdev   The @ref kbase_device the fault happened on
* @param[in] kctx    The @ref kbase_context for the faulting address space if
*                    one was found.
* @param[in] as      The address space that has the fault
*/
void kbase_mmu_interrupt_process(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_as *as);

#endif				/* _KBASE_MEM_H_ */
