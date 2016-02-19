/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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

#include <linux/kref.h>

#ifdef CONFIG_UMP
#include <linux/ump.h>
#endif				/* CONFIG_UMP */
#include "mali_base_kernel.h"
#include <mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"
#if defined(CONFIG_MALI_GATOR_SUPPORT)
#include "mali_kbase_gator.h"
#endif

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
struct kbase_cpu_mapping {
	struct   list_head mappings_list;
	struct   kbase_mem_phy_alloc *alloc;
	struct   kbase_context *kctx;
	struct   kbase_va_region *region;
	pgoff_t  page_off;
	int      count;
	unsigned long vm_start;
	unsigned long vm_end;
};

enum kbase_memory_type {
	KBASE_MEM_TYPE_NATIVE,
	KBASE_MEM_TYPE_IMPORTED_UMP,
	KBASE_MEM_TYPE_IMPORTED_UMM,
	KBASE_MEM_TYPE_IMPORTED_USER_BUF,
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
struct kbase_mem_phy_alloc {
	struct kref           kref; /* number of users of this alloc */
	atomic_t              gpu_mappings;
	size_t                nents; /* 0..N */
	phys_addr_t           *pages; /* N elements, only 0..nents are valid */

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
			u64 stride;
			size_t nents;
			struct kbase_aliased *aliased;
		} alias;
		/* Used by type = (KBASE_MEM_TYPE_NATIVE, KBASE_MEM_TYPE_TB) */
		struct kbase_context *kctx;
		struct {
			unsigned long address;
			unsigned long size;
			unsigned long nr_pages;
			struct page **pages;
			unsigned int current_mapping_usage_count;
			struct task_struct *owner;
			dma_addr_t *dma_addrs;
		} user_buf;
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

void kbase_mem_kref_free(struct kref *kref);

int kbase_mem_init(struct kbase_device *kbdev);
void kbase_mem_halt(struct kbase_device *kbdev);
void kbase_mem_term(struct kbase_device *kbdev);

static inline struct kbase_mem_phy_alloc *kbase_mem_phy_alloc_get(struct kbase_mem_phy_alloc *alloc)
{
	kref_get(&alloc->kref);
	return alloc;
}

static inline struct kbase_mem_phy_alloc *kbase_mem_phy_alloc_put(struct kbase_mem_phy_alloc *alloc)
{
	kref_put(&alloc->kref, kbase_mem_kref_free);
	return NULL;
}

/**
 * A GPU memory region, and attributes for CPU mappings.
 */
struct kbase_va_region {
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

#define KBASE_REG_SECURE            (1ul << 19)

#define KBASE_REG_ZONE_SAME_VA      KBASE_REG_ZONE(0)

/* only used with 32-bit clients */
/*
 * On a 32bit platform, custom VA should be wired from (4GB + shader region)
 * to the VA limit of the GPU. Unfortunately, the Linux mmap() interface
 * limits us to 2^32 pages (2^44 bytes, see mmap64 man page for reference).
 * So we put the default limit to the maximum possible on Linux and shrink
 * it down, if required by the GPU, during initialization.
 */

/*
 * Dedicated 16MB region for shader code:
 * VA range 0x101000000-0x102000000
 */
#define KBASE_REG_ZONE_EXEC         KBASE_REG_ZONE(1)
#define KBASE_REG_ZONE_EXEC_BASE    (0x101000000ULL >> PAGE_SHIFT)
#define KBASE_REG_ZONE_EXEC_SIZE    ((16ULL * 1024 * 1024) >> PAGE_SHIFT)

#define KBASE_REG_ZONE_CUSTOM_VA         KBASE_REG_ZONE(2)
#define KBASE_REG_ZONE_CUSTOM_VA_BASE    (KBASE_REG_ZONE_EXEC_BASE + KBASE_REG_ZONE_EXEC_SIZE) /* Starting after KBASE_REG_ZONE_EXEC */
#define KBASE_REG_ZONE_CUSTOM_VA_SIZE    (((1ULL << 44) >> PAGE_SHIFT) - KBASE_REG_ZONE_CUSTOM_VA_BASE)
/* end 32-bit clients only */

	unsigned long flags;

	size_t extent; /* nr of pages alloc'd on PF */

	struct kbase_mem_phy_alloc *cpu_alloc; /* the one alloc object we mmap to the CPU when mapping this region */
	struct kbase_mem_phy_alloc *gpu_alloc; /* the one alloc object we mmap to the GPU when mapping this region */

	/* non-NULL if this memory object is a kds_resource */
	struct kds_resource *kds_res;

};

/* Common functions */
static inline phys_addr_t *kbase_get_cpu_phy_pages(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->cpu_alloc->pages;
}

static inline phys_addr_t *kbase_get_gpu_phy_pages(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->gpu_alloc->pages;
}

static inline size_t kbase_reg_current_backed_size(struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	/* if no alloc object the backed size naturally is 0 */
	if (!reg->cpu_alloc)
		return 0;

	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->cpu_alloc->nents;
}

#define KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD ((size_t)(4*1024)) /* size above which vmalloc is used over kmalloc */

static inline struct kbase_mem_phy_alloc *kbase_alloc_create(size_t nr_pages, enum kbase_memory_type type)
{
	struct kbase_mem_phy_alloc *alloc;
	size_t alloc_size = sizeof(*alloc) + sizeof(*alloc->pages) * nr_pages;
	size_t per_page_size = sizeof(*alloc->pages);

	/* Imported pages may have page private data already in use */
	if (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF) {
		alloc_size += nr_pages *
				sizeof(*alloc->imported.user_buf.dma_addrs);
		per_page_size += sizeof(*alloc->imported.user_buf.dma_addrs);
	}

	/*
	 * Prevent nr_pages*per_page_size + sizeof(*alloc) from
	 * wrapping around.
	 */
	if (nr_pages > ((((size_t) -1) - sizeof(*alloc))
			/ per_page_size))
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
	alloc->pages = (void *)(alloc + 1);
	INIT_LIST_HEAD(&alloc->mappings);
	alloc->type = type;

	if (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF)
		alloc->imported.user_buf.dma_addrs =
				(void *) (alloc->pages + nr_pages);

	return alloc;
}

static inline int kbase_reg_prepare_native(struct kbase_va_region *reg,
		struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(!reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(!reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->flags & KBASE_REG_FREE);

	reg->cpu_alloc = kbase_alloc_create(reg->nr_pages,
			KBASE_MEM_TYPE_NATIVE);
	if (IS_ERR(reg->cpu_alloc))
		return PTR_ERR(reg->cpu_alloc);
	else if (!reg->cpu_alloc)
		return -ENOMEM;
	reg->cpu_alloc->imported.kctx = kctx;
	if (kctx->infinite_cache_active && (reg->flags & KBASE_REG_CPU_CACHED)) {
		reg->gpu_alloc = kbase_alloc_create(reg->nr_pages,
				KBASE_MEM_TYPE_NATIVE);
		reg->gpu_alloc->imported.kctx = kctx;
	} else {
		reg->gpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	}

	reg->flags &= ~KBASE_REG_FREE;
	return 0;
}

static inline int kbase_atomic_add_pages(int num_pages, atomic_t *used_pages)
{
	int new_val = atomic_add_return(num_pages, used_pages);
#if defined(CONFIG_MALI_GATOR_SUPPORT)
	kbase_trace_mali_total_alloc_pages_change((long long int)new_val);
#endif
	return new_val;
}

static inline int kbase_atomic_sub_pages(int num_pages, atomic_t *used_pages)
{
	int new_val = atomic_sub_return(num_pages, used_pages);
#if defined(CONFIG_MALI_GATOR_SUPPORT)
	kbase_trace_mali_total_alloc_pages_change((long long int)new_val);
#endif
	return new_val;
}

/*
 * Max size for kbdev memory pool (in pages)
 */
#define KBASE_MEM_POOL_MAX_SIZE_KBDEV (SZ_64M >> PAGE_SHIFT)

/*
 * Max size for kctx memory pool (in pages)
 */
#define KBASE_MEM_POOL_MAX_SIZE_KCTX  (SZ_64M >> PAGE_SHIFT)

/**
 * kbase_mem_pool_init - Create a memory pool for a kbase device
 * @pool:      Memory pool to initialize
 * @max_size:  Maximum number of free pages the pool can hold
 * @kbdev:     Kbase device where memory is used
 * @next_pool: Pointer to the next pool or NULL.
 *
 * Allocations from @pool are in whole pages. Each @pool has a free list where
 * pages can be quickly allocated from. The free list is initially empty and
 * filled whenever pages are freed back to the pool. The number of free pages
 * in the pool will in general not exceed @max_size, but the pool may in
 * certain corner cases grow above @max_size.
 *
 * If @next_pool is not NULL, we will allocate from @next_pool before going to
 * the kernel allocator. Similarily pages can spill over to @next_pool when
 * @pool is full. Pages are zeroed before they spill over to another pool, to
 * prevent leaking information between applications.
 *
 * A shrinker is registered so that Linux mm can reclaim pages from the pool as
 * needed.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_mem_pool_init(struct kbase_mem_pool *pool,
		size_t max_size,
		struct kbase_device *kbdev,
		struct kbase_mem_pool *next_pool);

/**
 * kbase_mem_pool_term - Destroy a memory pool
 * @pool:  Memory pool to destroy
 *
 * Pages in the pool will spill over to @next_pool (if available) or freed to
 * the kernel.
 */
void kbase_mem_pool_term(struct kbase_mem_pool *pool);

/**
 * kbase_mem_pool_alloc - Allocate a page from memory pool
 * @pool:  Memory pool to allocate from
 *
 * Allocations from the pool are made as follows:
 * 1. If there are free pages in the pool, allocate a page from @pool.
 * 2. Otherwise, if @next_pool is not NULL and has free pages, allocate a page
 *    from @next_pool.
 * 3. Finally, allocate a page from the kernel.
 *
 * Return: Pointer to allocated page, or NULL if allocation failed.
 */
struct page *kbase_mem_pool_alloc(struct kbase_mem_pool *pool);

/**
 * kbase_mem_pool_free - Free a page to memory pool
 * @pool:  Memory pool where page should be freed
 * @page:  Page to free to the pool
 * @dirty: Whether some of the page may be dirty in the cache.
 *
 * Pages are freed to the pool as follows:
 * 1. If @pool is not full, add @page to @pool.
 * 2. Otherwise, if @next_pool is not NULL and not full, add @page to
 *    @next_pool.
 * 3. Finally, free @page to the kernel.
 */
void kbase_mem_pool_free(struct kbase_mem_pool *pool, struct page *page,
		bool dirty);

/**
 * kbase_mem_pool_alloc_pages - Allocate pages from memory pool
 * @pool:     Memory pool to allocate from
 * @nr_pages: Number of pages to allocate
 * @pages:    Pointer to array where the physical address of the allocated
 *            pages will be stored.
 *
 * Like kbase_mem_pool_alloc() but optimized for allocating many pages.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		phys_addr_t *pages);

/**
 * kbase_mem_pool_free_pages - Free pages to memory pool
 * @pool:     Memory pool where pages should be freed
 * @nr_pages: Number of pages to free
 * @pages:    Pointer to array holding the physical addresses of the pages to
 *            free.
 * @dirty:    Whether any pages may be dirty in the cache.
 *
 * Like kbase_mem_pool_free() but optimized for freeing many pages.
 */
void kbase_mem_pool_free_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		phys_addr_t *pages, bool dirty);

/**
 * kbase_mem_pool_size - Get number of free pages in memory pool
 * @pool:  Memory pool to inspect
 *
 * Note: the size of the pool may in certain corner cases exceed @max_size!
 *
 * Return: Number of free pages in the pool
 */
static inline size_t kbase_mem_pool_size(struct kbase_mem_pool *pool)
{
	return ACCESS_ONCE(pool->cur_size);
}

/**
 * kbase_mem_pool_max_size - Get maximum number of free pages in memory pool
 * @pool:  Memory pool to inspect
 *
 * Return: Maximum number of free pages in the pool
 */
static inline size_t kbase_mem_pool_max_size(struct kbase_mem_pool *pool)
{
	return pool->max_size;
}


/**
 * kbase_mem_pool_set_max_size - Set maximum number of free pages in memory pool
 * @pool:     Memory pool to inspect
 * @max_size: Maximum number of free pages the pool can hold
 *
 * If @max_size is reduced, the pool will be shrunk to adhere to the new limit.
 * For details see kbase_mem_pool_shrink().
 */
void kbase_mem_pool_set_max_size(struct kbase_mem_pool *pool, size_t max_size);

/**
 * kbase_mem_pool_trim - Grow or shrink the pool to a new size
 * @pool:     Memory pool to trim
 * @new_size: New number of pages in the pool
 *
 * If @new_size > @cur_size, fill the pool with new pages from the kernel, but
 * not above @max_size.
 * If @new_size < @cur_size, shrink the pool by freeing pages to the kernel.
 *
 * Return: The new size of the pool
 */
size_t kbase_mem_pool_trim(struct kbase_mem_pool *pool, size_t new_size);


int kbase_region_tracker_init(struct kbase_context *kctx);
void kbase_region_tracker_term(struct kbase_context *kctx);

struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(struct kbase_context *kctx, u64 gpu_addr);

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Must be called with context lock held.
 */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(struct kbase_context *kctx, u64 gpu_addr);

struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
int kbase_add_va_region(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr, size_t nr_pages, size_t align);

bool kbase_check_alloc_flags(unsigned long flags);
bool kbase_check_import_flags(unsigned long flags);
void kbase_update_region_flags(struct kbase_context *kctx,
		struct kbase_va_region *reg, unsigned long flags);

void kbase_gpu_vm_lock(struct kbase_context *kctx);
void kbase_gpu_vm_unlock(struct kbase_context *kctx);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size);

int kbase_mmu_init(struct kbase_context *kctx);
void kbase_mmu_term(struct kbase_context *kctx);

phys_addr_t kbase_mmu_alloc_pgd(struct kbase_context *kctx);
void kbase_mmu_free_pgd(struct kbase_context *kctx);
int kbase_mmu_insert_pages(struct kbase_context *kctx, u64 vpfn,
				  phys_addr_t *phys, size_t nr,
				  unsigned long flags);
int kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 vpfn,
					phys_addr_t phys, size_t nr,
					unsigned long flags);

int kbase_mmu_teardown_pages(struct kbase_context *kctx, u64 vpfn, size_t nr);
int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn, phys_addr_t *phys, size_t nr, unsigned long flags);

/**
 * @brief Register region and map it on the GPU.
 *
 * Call kbase_add_va_region() and map the region on the GPU.
 */
int kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr, size_t nr_pages, size_t align);

/**
 * @brief Remove the region from the GPU and unregister it.
 *
 * Must be called with context lock held.
 */
int kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg);

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

/**
 * kbase_mmu_disable_as() - set the MMU in unmapped mode for an address space.
 *
 * @kbdev:	Kbase device
 * @as_nr:	Number of the address space for which the MMU
 *		should be set in unmapped mode.
 *
 * The caller must hold kbdev->as[as_nr].transaction_mutex.
 */
void kbase_mmu_disable_as(struct kbase_device *kbdev, int as_nr);

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

int kbase_sync_now(struct kbase_context *kctx, struct base_syncset *syncset);
void kbase_sync_single(struct kbase_context *kctx, phys_addr_t cpu_pa,
		phys_addr_t gpu_pa, off_t offset, size_t size,
		enum kbase_sync_type sync_fn);
void kbase_pre_job_sync(struct kbase_context *kctx, struct base_syncset *syncsets, size_t nr);
void kbase_post_job_sync(struct kbase_context *kctx, struct base_syncset *syncsets, size_t nr);

/* OS specific functions */
int kbase_mem_free(struct kbase_context *kctx, u64 gpu_addr);
int kbase_mem_free_region(struct kbase_context *kctx, struct kbase_va_region *reg);
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

static inline void kbase_process_page_usage_inc(struct kbase_context *kctx, int pages)
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

static inline void kbase_process_page_usage_dec(struct kbase_context *kctx, int pages)
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
 * @return 0 if offset was obtained successfully. Error code
 *         otherwise.
 */
int kbasep_find_enclosing_cpu_mapping_offset(struct kbase_context *kctx,
							u64 gpu_addr,
							unsigned long uaddr,
							size_t size,
							u64 *offset);

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
int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc *alloc, size_t nr_pages_requested);

/**
* @brief Free physical pages.
*
* Frees \a nr_pages and updates the alloc object.
*
* @param[in] alloc allocation object to free pages from
* @param[in] nr_pages_to_free number of physical pages to free
*/
int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc *alloc, size_t nr_pages_to_free);

static inline void kbase_set_dma_addr(struct page *p, dma_addr_t dma_addr)
{
	SetPagePrivate(p);
	if (sizeof(dma_addr_t) > sizeof(p->private)) {
		/* on 32-bit ARM with LPAE dma_addr_t becomes larger, but the
		 * private field stays the same. So we have to be clever and
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

static inline void kbase_clear_dma_addr(struct page *p)
{
	ClearPagePrivate(p);
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

/**
 * @brief Process a page fault.
 *
 * @param[in] data  work_struct passed by queue_work()
 */
void page_fault_worker(struct work_struct *data);

/**
 * @brief Process a bus fault.
 *
 * @param[in] data  work_struct passed by queue_work()
 */
void bus_fault_worker(struct work_struct *data);

/**
 * @brief Flush MMU workqueues.
 *
 * This function will cause any outstanding page or bus faults to be processed.
 * It should be called prior to powering off the GPU.
 *
 * @param[in] kbdev   Device pointer
 */
void kbase_flush_mmu_wqs(struct kbase_device *kbdev);

/**
 * kbase_sync_single_for_device - update physical memory and give GPU ownership
 * @kbdev: Device pointer
 * @handle: DMA address of region
 * @size: Size of region to sync
 * @dir:  DMA data direction
 */

void kbase_sync_single_for_device(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir);

/**
 * kbase_sync_single_for_cpu - update physical memory and give CPU ownership
 * @kbdev: Device pointer
 * @handle: DMA address of region
 * @size: Size of region to sync
 * @dir:  DMA data direction
 */

void kbase_sync_single_for_cpu(struct kbase_device *kbdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir);

#endif				/* _KBASE_MEM_H_ */
