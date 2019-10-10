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
#include "mali_base_kernel.h"
#include <mali_kbase_hw.h>
#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"
/* Required for kbase_mem_evictable_unmake */
#include "mali_kbase_mem_linux.h"

static inline void kbase_process_page_usage_inc(struct kbase_context *kctx,
		int pages);

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
	int      count;
	int      free_on_close;
};

enum kbase_memory_type {
	KBASE_MEM_TYPE_NATIVE,
	KBASE_MEM_TYPE_IMPORTED_UMM,
	KBASE_MEM_TYPE_IMPORTED_USER_BUF,
	KBASE_MEM_TYPE_ALIAS,
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
#define KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED  (1u << 0)
#define KBASE_MEM_PHY_ALLOC_LARGE            (1u << 1)

/* struct kbase_mem_phy_alloc - Physical pages tracking object.
 *
 * Set up to track N pages.
 * N not stored here, the creator holds that info.
 * This object only tracks how many elements are actually valid (present).
 * Changing of nents or *pages should only happen if the kbase_mem_phy_alloc
 * is not shared with another region or client. CPU mappings are OK to
 * exist when changing, as long as the tracked mappings objects are
 * updated as part of the change.
 *
 * @kref: number of users of this alloc
 * @gpu_mappings: count number of times mapped on the GPU
 * @nents: 0..N
 * @pages: N elements, only 0..nents are valid
 * @mappings: List of CPU mappings of this physical memory allocation.
 * @evict_node: Node used to store this allocation on the eviction list
 * @evicted: Physical backing size when the pages where evicted
 * @reg: Back reference to the region structure which created this
 *       allocation, or NULL if it has been freed.
 * @type: type of buffer
 * @permanent_map: Kernel side mapping of the alloc, shall never be
 *                 referred directly. kbase_phy_alloc_mapping_get() &
 *                 kbase_phy_alloc_mapping_put() pair should be used
 *                 around access to the kernel-side CPU mapping so that
 *                 mapping doesn't disappear whilst it is being accessed.
 * @properties: Bitmask of properties, e.g. KBASE_MEM_PHY_ALLOC_LARGE.
 * @group_id: A memory group ID to be passed to a platform-specific
 *            memory group manager, if present.
 *            Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @imported: member in union valid based on @a type
 */
struct kbase_mem_phy_alloc {
	struct kref           kref;
	atomic_t              gpu_mappings;
	size_t                nents;
	struct tagged_addr    *pages;
	struct list_head      mappings;
	struct list_head      evict_node;
	size_t                evicted;
	struct kbase_va_region *reg;
	enum kbase_memory_type type;
	struct kbase_vmap_struct *permanent_map;
	u8 properties;
	u8 group_id;

	union {
		struct {
			struct dma_buf *dma_buf;
			struct dma_buf_attachment *dma_attachment;
			unsigned int current_mapping_usage_count;
			struct sg_table *sgt;
		} umm;
		struct {
			u64 stride;
			size_t nents;
			struct kbase_aliased *aliased;
		} alias;
		struct {
			struct kbase_context *kctx;
			/* Number of pages in this structure, including *pages.
			 * Used for kernel memory tracking.
			 */
			size_t nr_struct_pages;
		} native;
		struct kbase_alloc_import_user_buf {
			unsigned long address;
			unsigned long size;
			unsigned long nr_pages;
			struct page **pages;
			/* top bit (1<<31) of current_mapping_usage_count
			 * specifies that this import was pinned on import
			 * See PINNED_ON_IMPORT
			 */
			u32 current_mapping_usage_count;
			struct mm_struct *mm;
			dma_addr_t *dma_addrs;
		} user_buf;
	} imported;
};

/* The top bit of kbase_alloc_import_user_buf::current_mapping_usage_count is
 * used to signify that a buffer was pinned when it was imported. Since the
 * reference count is limited by the number of atoms that can be submitted at
 * once there should be no danger of overflowing into this bit.
 * Stealing the top bit also has the benefit that
 * current_mapping_usage_count != 0 if and only if the buffer is mapped.
 */
#define PINNED_ON_IMPORT	(1<<31)

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

/**
 * kbase_mem_is_imported - Indicate whether a memory type is imported
 *
 * @type: the memory type
 *
 * Return: true if the memory type is imported, false otherwise
 */
static inline bool kbase_mem_is_imported(enum kbase_memory_type type)
{
	return (type == KBASE_MEM_TYPE_IMPORTED_UMM) ||
		(type == KBASE_MEM_TYPE_IMPORTED_USER_BUF);
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

	struct rb_root *rbtree;	/* Backlink to rb tree */

	u64 start_pfn;		/* The PFN in GPU space */
	size_t nr_pages;
	/* Initial commit, for aligning the start address and correctly growing
	 * KBASE_REG_TILER_ALIGN_TOP regions */
	size_t initial_commit;

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
/* Is GPU cached?
 * Some components within the GPU might only be able to access memory that is
 * GPU cacheable. Refer to the specific GPU implementation for more details.
 */
#define KBASE_REG_GPU_CACHED        (1ul << 5)

#define KBASE_REG_GROWABLE          (1ul << 6)
/* Can grow on pf? */
#define KBASE_REG_PF_GROW           (1ul << 7)

/* Allocation doesn't straddle the 4GB boundary in GPU virtual space */
#define KBASE_REG_GPU_VA_SAME_4GB_PAGE (1ul << 8)

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

/* Index of chosen MEMATTR for this region (0..7) */
#define KBASE_REG_MEMATTR_MASK      (7ul << 16)
#define KBASE_REG_MEMATTR_INDEX(x)  (((x) & 7) << 16)
#define KBASE_REG_MEMATTR_VALUE(x)  (((x) & KBASE_REG_MEMATTR_MASK) >> 16)

#define KBASE_REG_PROTECTED         (1ul << 19)

#define KBASE_REG_DONT_NEED         (1ul << 20)

/* Imported buffer is padded? */
#define KBASE_REG_IMPORT_PAD        (1ul << 21)

/* Bit 22 is reserved.
 *
 * Do not remove, use the next unreserved bit for new flags */
#define KBASE_REG_RESERVED_BIT_22   (1ul << 22)

/* The top of the initial commit is aligned to extent pages.
 * Extent must be a power of 2 */
#define KBASE_REG_TILER_ALIGN_TOP   (1ul << 23)

/* Whilst this flag is set the GPU allocation is not supposed to be freed by
 * user space. The flag will remain set for the lifetime of JIT allocations.
 */
#define KBASE_REG_NO_USER_FREE      (1ul << 24)

/* Memory has permanent kernel side mapping */
#define KBASE_REG_PERMANENT_KERNEL_MAPPING (1ul << 25)

/* GPU VA region has been freed by the userspace, but still remains allocated
 * due to the reference held by CPU mappings created on the GPU VA region.
 *
 * A region with this flag set has had kbase_gpu_munmap() called on it, but can
 * still be looked-up in the region tracker as a non-free region. Hence must
 * not create or update any more GPU mappings on such regions because they will
 * not be unmapped when the region is finally destroyed.
 *
 * Since such regions are still present in the region tracker, new allocations
 * attempted with BASE_MEM_SAME_VA might fail if their address intersects with
 * a region with this flag set.
 *
 * In addition, this flag indicates the gpu_alloc member might no longer valid
 * e.g. in infinite cache simulation.
 */
#define KBASE_REG_VA_FREED (1ul << 26)

#define KBASE_REG_ZONE_SAME_VA      KBASE_REG_ZONE(0)

/* only used with 32-bit clients */
/*
 * On a 32bit platform, custom VA should be wired from 4GB
 * to the VA limit of the GPU. Unfortunately, the Linux mmap() interface
 * limits us to 2^32 pages (2^44 bytes, see mmap64 man page for reference).
 * So we put the default limit to the maximum possible on Linux and shrink
 * it down, if required by the GPU, during initialization.
 */

#define KBASE_REG_ZONE_CUSTOM_VA         KBASE_REG_ZONE(1)
#define KBASE_REG_ZONE_CUSTOM_VA_BASE    (0x100000000ULL >> PAGE_SHIFT)
#define KBASE_REG_ZONE_CUSTOM_VA_SIZE    (((1ULL << 44) >> PAGE_SHIFT) - KBASE_REG_ZONE_CUSTOM_VA_BASE)
/* end 32-bit clients only */

/* The starting address and size of the GPU-executable zone are dynamic
 * and depend on the platform and the number of pages requested by the
 * user process, with an upper limit of 4 GB.
 */
#define KBASE_REG_ZONE_EXEC_VA           KBASE_REG_ZONE(2)
#define KBASE_REG_ZONE_EXEC_VA_MAX_PAGES ((1ULL << 32) >> PAGE_SHIFT) /* 4 GB */


	unsigned long flags;

	size_t extent; /* nr of pages alloc'd on PF */

	struct kbase_mem_phy_alloc *cpu_alloc; /* the one alloc object we mmap to the CPU when mapping this region */
	struct kbase_mem_phy_alloc *gpu_alloc; /* the one alloc object we mmap to the GPU when mapping this region */

	/* List head used to store the region in the JIT allocation pool */
	struct list_head jit_node;
	/* The last JIT usage ID for this region */
	u16 jit_usage_id;
	/* The JIT bin this allocation came from */
	u8 jit_bin_id;

	int    va_refcnt; /* number of users of this va */
};

static inline bool kbase_is_region_free(struct kbase_va_region *reg)
{
	return (!reg || reg->flags & KBASE_REG_FREE);
}

static inline bool kbase_is_region_invalid(struct kbase_va_region *reg)
{
	return (!reg || reg->flags & KBASE_REG_VA_FREED);
}

static inline bool kbase_is_region_invalid_or_free(struct kbase_va_region *reg)
{
	/* Possibly not all functions that find regions would be using this
	 * helper, so they need to be checked when maintaining this function.
	 */
	return (kbase_is_region_invalid(reg) ||	kbase_is_region_free(reg));
}

int kbase_remove_va_region(struct kbase_va_region *reg);
static inline void kbase_region_refcnt_free(struct kbase_va_region *reg)
{
	/* If region was mapped then remove va region*/
	if (reg->start_pfn)
		kbase_remove_va_region(reg);

	/* To detect use-after-free in debug builds */
	KBASE_DEBUG_CODE(reg->flags |= KBASE_REG_FREE);
	kfree(reg);
}

static inline struct kbase_va_region *kbase_va_region_alloc_get(
		struct kbase_context *kctx, struct kbase_va_region *region)
{
	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(!region->va_refcnt);

	/* non-atomic as kctx->reg_lock is held */
	region->va_refcnt++;

	return region;
}

static inline struct kbase_va_region *kbase_va_region_alloc_put(
		struct kbase_context *kctx, struct kbase_va_region *region)
{
	lockdep_assert_held(&kctx->reg_lock);

	WARN_ON(region->va_refcnt <= 0);
	WARN_ON(region->flags & KBASE_REG_FREE);

	/* non-atomic as kctx->reg_lock is held */
	region->va_refcnt--;
	if (!region->va_refcnt)
		kbase_region_refcnt_free(region);

	return NULL;
}

/* Common functions */
static inline struct tagged_addr *kbase_get_cpu_phy_pages(
		struct kbase_va_region *reg)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents == reg->gpu_alloc->nents);

	return reg->cpu_alloc->pages;
}

static inline struct tagged_addr *kbase_get_gpu_phy_pages(
		struct kbase_va_region *reg)
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

static inline struct kbase_mem_phy_alloc *kbase_alloc_create(
		struct kbase_context *kctx, size_t nr_pages,
		enum kbase_memory_type type, int group_id)
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

	if (type == KBASE_MEM_TYPE_NATIVE) {
		alloc->imported.native.nr_struct_pages =
				(alloc_size + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		kbase_process_page_usage_inc(kctx,
				alloc->imported.native.nr_struct_pages);
	}

	/* Store allocation method */
	if (alloc_size > KBASE_MEM_PHY_ALLOC_LARGE_THRESHOLD)
		alloc->properties |= KBASE_MEM_PHY_ALLOC_LARGE;

	kref_init(&alloc->kref);
	atomic_set(&alloc->gpu_mappings, 0);
	alloc->nents = 0;
	alloc->pages = (void *)(alloc + 1);
	INIT_LIST_HEAD(&alloc->mappings);
	alloc->type = type;
	alloc->group_id = group_id;

	if (type == KBASE_MEM_TYPE_IMPORTED_USER_BUF)
		alloc->imported.user_buf.dma_addrs =
				(void *) (alloc->pages + nr_pages);

	return alloc;
}

static inline int kbase_reg_prepare_native(struct kbase_va_region *reg,
		struct kbase_context *kctx, int group_id)
{
	KBASE_DEBUG_ASSERT(reg);
	KBASE_DEBUG_ASSERT(!reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(!reg->gpu_alloc);
	KBASE_DEBUG_ASSERT(reg->flags & KBASE_REG_FREE);

	reg->cpu_alloc = kbase_alloc_create(kctx, reg->nr_pages,
			KBASE_MEM_TYPE_NATIVE, group_id);
	if (IS_ERR(reg->cpu_alloc))
		return PTR_ERR(reg->cpu_alloc);
	else if (!reg->cpu_alloc)
		return -ENOMEM;

	reg->cpu_alloc->imported.native.kctx = kctx;
	if (kbase_ctx_flag(kctx, KCTX_INFINITE_CACHE)
	    && (reg->flags & KBASE_REG_CPU_CACHED)) {
		reg->gpu_alloc = kbase_alloc_create(kctx, reg->nr_pages,
				KBASE_MEM_TYPE_NATIVE, group_id);
		if (IS_ERR_OR_NULL(reg->gpu_alloc)) {
			kbase_mem_phy_alloc_put(reg->cpu_alloc);
			return -ENOMEM;
		}
		reg->gpu_alloc->imported.native.kctx = kctx;
	} else {
		reg->gpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	}

	mutex_lock(&kctx->jit_evict_lock);
	INIT_LIST_HEAD(&reg->cpu_alloc->evict_node);
	INIT_LIST_HEAD(&reg->gpu_alloc->evict_node);
	mutex_unlock(&kctx->jit_evict_lock);

	reg->flags &= ~KBASE_REG_FREE;

	return 0;
}

/*
 * Max size for kbdev memory pool (in pages)
 */
#define KBASE_MEM_POOL_MAX_SIZE_KBDEV (SZ_64M >> PAGE_SHIFT)

/*
 * Max size for kctx memory pool (in pages)
 */
#define KBASE_MEM_POOL_MAX_SIZE_KCTX  (SZ_64M >> PAGE_SHIFT)

/*
 * The order required for a 2MB page allocation (2^order * 4KB = 2MB)
 */
#define KBASE_MEM_POOL_2MB_PAGE_TABLE_ORDER	9

/*
 * The order required for a 4KB page allocation
 */
#define KBASE_MEM_POOL_4KB_PAGE_TABLE_ORDER	0

/**
 * kbase_mem_pool_config_set_max_size - Set maximum number of free pages in
 *                                      initial configuration of a memory pool
 *
 * @config:   Initial configuration for a physical memory pool
 * @max_size: Maximum number of free pages that a pool created from
 *            @config can hold
 */
static inline void kbase_mem_pool_config_set_max_size(
	struct kbase_mem_pool_config *const config, size_t const max_size)
{
	WRITE_ONCE(config->max_size, max_size);
}

/**
 * kbase_mem_pool_config_get_max_size - Get maximum number of free pages from
 *                                      initial configuration of a memory pool
 *
 * @config: Initial configuration for a physical memory pool
 *
 * Return: Maximum number of free pages that a pool created from @config
 *         can hold
 */
static inline size_t kbase_mem_pool_config_get_max_size(
	const struct kbase_mem_pool_config *const config)
{
	return READ_ONCE(config->max_size);
}

/**
 * kbase_mem_pool_init - Create a memory pool for a kbase device
 * @pool:      Memory pool to initialize
 * @config:    Initial configuration for the memory pool
 * @order:     Page order for physical page size (order=0=>4kB, order=9=>2MB)
 * @group_id:  A memory group ID to be passed to a platform-specific
 *             memory group manager, if present.
 *             Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
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
 * the memory group manager. Similarly pages can spill over to @next_pool when
 * @pool is full. Pages are zeroed before they spill over to another pool, to
 * prevent leaking information between applications.
 *
 * A shrinker is registered so that Linux mm can reclaim pages from the pool as
 * needed.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_mem_pool_init(struct kbase_mem_pool *pool,
		const struct kbase_mem_pool_config *config,
		unsigned int order,
		int group_id,
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
 * 3. Return NULL if no memory in the pool
 *
 * Return: Pointer to allocated page, or NULL if allocation failed.
 *
 * Note : This function should not be used if the pool lock is held. Use
 * kbase_mem_pool_alloc_locked() instead.
 */
struct page *kbase_mem_pool_alloc(struct kbase_mem_pool *pool);

/**
 * kbase_mem_pool_alloc_locked - Allocate a page from memory pool
 * @pool:  Memory pool to allocate from
 *
 * If there are free pages in the pool, this function allocates a page from
 * @pool. This function does not use @next_pool.
 *
 * Return: Pointer to allocated page, or NULL if allocation failed.
 *
 * Note : Caller must hold the pool lock.
 */
struct page *kbase_mem_pool_alloc_locked(struct kbase_mem_pool *pool);

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
 *
 * Note : This function should not be used if the pool lock is held. Use
 * kbase_mem_pool_free_locked() instead.
 */
void kbase_mem_pool_free(struct kbase_mem_pool *pool, struct page *page,
		bool dirty);

/**
 * kbase_mem_pool_free_locked - Free a page to memory pool
 * @pool:  Memory pool where page should be freed
 * @p:     Page to free to the pool
 * @dirty: Whether some of the page may be dirty in the cache.
 *
 * If @pool is not full, this function adds @page to @pool. Otherwise, @page is
 * freed to the kernel. This function does not use @next_pool.
 *
 * Note : Caller must hold the pool lock.
 */
void kbase_mem_pool_free_locked(struct kbase_mem_pool *pool, struct page *p,
		bool dirty);

/**
 * kbase_mem_pool_alloc_pages - Allocate pages from memory pool
 * @pool:     Memory pool to allocate from
 * @nr_4k_pages: Number of pages to allocate
 * @pages:    Pointer to array where the physical address of the allocated
 *            pages will be stored.
 * @partial_allowed: If fewer pages allocated is allowed
 *
 * Like kbase_mem_pool_alloc() but optimized for allocating many pages.
 *
 * Return:
 * On success number of pages allocated (could be less than nr_pages if
 * partial_allowed).
 * On error an error code.
 *
 * Note : This function should not be used if the pool lock is held. Use
 * kbase_mem_pool_alloc_pages_locked() instead.
 *
 * The caller must not hold vm_lock, as this could cause a deadlock if
 * the kernel OoM killer runs. If the caller must allocate pages while holding
 * this lock, it should use kbase_mem_pool_alloc_pages_locked() instead.
 */
int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_4k_pages,
		struct tagged_addr *pages, bool partial_allowed);

/**
 * kbase_mem_pool_alloc_pages_locked - Allocate pages from memory pool
 * @pool:        Memory pool to allocate from
 * @nr_4k_pages: Number of pages to allocate
 * @pages:       Pointer to array where the physical address of the allocated
 *               pages will be stored.
 *
 * Like kbase_mem_pool_alloc() but optimized for allocating many pages. This
 * version does not allocate new pages from the kernel, and therefore will never
 * trigger the OoM killer. Therefore, it can be run while the vm_lock is held.
 *
 * As new pages can not be allocated, the caller must ensure there are
 * sufficient pages in the pool. Usage of this function should look like :
 *
 *   kbase_gpu_vm_lock(kctx);
 *   kbase_mem_pool_lock(pool)
 *   while (kbase_mem_pool_size(pool) < pages_required) {
 *     kbase_mem_pool_unlock(pool)
 *     kbase_gpu_vm_unlock(kctx);
 *     kbase_mem_pool_grow(pool)
 *     kbase_gpu_vm_lock(kctx);
 *     kbase_mem_pool_lock(pool)
 *   }
 *   kbase_mem_pool_alloc_pages_locked(pool)
 *   kbase_mem_pool_unlock(pool)
 *   Perform other processing that requires vm_lock...
 *   kbase_gpu_vm_unlock(kctx);
 *
 * This ensures that the pool can be grown to the required size and that the
 * allocation can complete without another thread using the newly grown pages.
 *
 * Return:
 * On success number of pages allocated.
 * On error an error code.
 *
 * Note : Caller must hold the pool lock.
 */
int kbase_mem_pool_alloc_pages_locked(struct kbase_mem_pool *pool,
		size_t nr_4k_pages, struct tagged_addr *pages);

/**
 * kbase_mem_pool_free_pages - Free pages to memory pool
 * @pool:     Memory pool where pages should be freed
 * @nr_pages: Number of pages to free
 * @pages:    Pointer to array holding the physical addresses of the pages to
 *            free.
 * @dirty:    Whether any pages may be dirty in the cache.
 * @reclaimed: Whether the pages where reclaimable and thus should bypass
 *             the pool and go straight to the kernel.
 *
 * Like kbase_mem_pool_free() but optimized for freeing many pages.
 */
void kbase_mem_pool_free_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		struct tagged_addr *pages, bool dirty, bool reclaimed);

/**
 * kbase_mem_pool_free_pages_locked - Free pages to memory pool
 * @pool:     Memory pool where pages should be freed
 * @nr_pages: Number of pages to free
 * @pages:    Pointer to array holding the physical addresses of the pages to
 *            free.
 * @dirty:    Whether any pages may be dirty in the cache.
 * @reclaimed: Whether the pages where reclaimable and thus should bypass
 *             the pool and go straight to the kernel.
 *
 * Like kbase_mem_pool_free() but optimized for freeing many pages.
 */
void kbase_mem_pool_free_pages_locked(struct kbase_mem_pool *pool,
		size_t nr_pages, struct tagged_addr *pages, bool dirty,
		bool reclaimed);

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
	return READ_ONCE(pool->cur_size);
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
 * kbase_mem_pool_grow - Grow the pool
 * @pool:       Memory pool to grow
 * @nr_to_grow: Number of pages to add to the pool
 *
 * Adds @nr_to_grow pages to the pool. Note that this may cause the pool to
 * become larger than the maximum size specified.
 *
 * Returns: 0 on success, -ENOMEM if unable to allocate sufficent pages
 */
int kbase_mem_pool_grow(struct kbase_mem_pool *pool, size_t nr_to_grow);

/**
 * kbase_mem_pool_trim - Grow or shrink the pool to a new size
 * @pool:     Memory pool to trim
 * @new_size: New number of pages in the pool
 *
 * If @new_size > @cur_size, fill the pool with new pages from the kernel, but
 * not above the max_size for the pool.
 * If @new_size < @cur_size, shrink the pool by freeing pages to the kernel.
 */
void kbase_mem_pool_trim(struct kbase_mem_pool *pool, size_t new_size);

/**
 * kbase_mem_pool_mark_dying - Mark that this pool is dying
 * @pool:     Memory pool
 *
 * This will cause any ongoing allocation operations (eg growing on page fault)
 * to be terminated.
 */
void kbase_mem_pool_mark_dying(struct kbase_mem_pool *pool);

/**
 * kbase_mem_alloc_page - Allocate a new page for a device
 * @pool:  Memory pool to allocate a page from
 *
 * Most uses should use kbase_mem_pool_alloc to allocate a page. However that
 * function can fail in the event the pool is empty.
 *
 * Return: A new page or NULL if no memory
 */
struct page *kbase_mem_alloc_page(struct kbase_mem_pool *pool);

/**
 * kbase_region_tracker_init - Initialize the region tracker data structure
 * @kctx: kbase context
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_region_tracker_init(struct kbase_context *kctx);

/**
 * kbase_region_tracker_init_jit - Initialize the JIT region
 * @kctx: kbase context
 * @jit_va_pages: Size of the JIT region in pages
 * @max_allocations: Maximum number of allocations allowed for the JIT region
 * @trim_level: Trim level for the JIT region
 * @group_id: The physical group ID from which to allocate JIT memory.
 *            Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_region_tracker_init_jit(struct kbase_context *kctx, u64 jit_va_pages,
		u8 max_allocations, u8 trim_level, int group_id);

/**
 * kbase_region_tracker_init_exec - Initialize the EXEC_VA region
 * @kctx: kbase context
 * @exec_va_pages: Size of the JIT region in pages.
 *                 It must not be greater than 4 GB.
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_region_tracker_init_exec(struct kbase_context *kctx, u64 exec_va_pages);

/**
 * kbase_region_tracker_term - Terminate the JIT region
 * @kctx: kbase context
 */
void kbase_region_tracker_term(struct kbase_context *kctx);

/**
 * kbase_region_tracker_term_rbtree - Free memory for a region tracker
 *
 * This will free all the regions within the region tracker
 *
 * @rbtree: Region tracker tree root
 */
void kbase_region_tracker_term_rbtree(struct rb_root *rbtree);

struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(
		struct kbase_context *kctx, u64 gpu_addr);
struct kbase_va_region *kbase_find_region_enclosing_address(
		struct rb_root *rbtree, u64 gpu_addr);

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Must be called with context lock held.
 */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(
		struct kbase_context *kctx, u64 gpu_addr);
struct kbase_va_region *kbase_find_region_base_address(struct rb_root *rbtree,
		u64 gpu_addr);

struct kbase_va_region *kbase_alloc_free_region(struct rb_root *rbtree,
		u64 start_pfn, size_t nr_pages, int zone);
void kbase_free_alloced_region(struct kbase_va_region *reg);
int kbase_add_va_region(struct kbase_context *kctx, struct kbase_va_region *reg,
		u64 addr, size_t nr_pages, size_t align);
int kbase_add_va_region_rbtree(struct kbase_device *kbdev,
		struct kbase_va_region *reg, u64 addr, size_t nr_pages,
		size_t align);

bool kbase_check_alloc_flags(unsigned long flags);
bool kbase_check_import_flags(unsigned long flags);

/**
 * kbase_check_alloc_sizes - check user space sizes parameters for an
 *                           allocation
 *
 * @kctx:         kbase context
 * @flags:        The flags passed from user space
 * @va_pages:     The size of the requested region, in pages.
 * @commit_pages: Number of pages to commit initially.
 * @extent:       Number of pages to grow by on GPU page fault and/or alignment
 *                (depending on flags)
 *
 * Makes checks on the size parameters passed in from user space for a memory
 * allocation call, with respect to the flags requested.
 *
 * Return: 0 if sizes are valid for these flags, negative error code otherwise
 */
int kbase_check_alloc_sizes(struct kbase_context *kctx, unsigned long flags,
		u64 va_pages, u64 commit_pages, u64 extent);

/**
 * kbase_update_region_flags - Convert user space flags to kernel region flags
 *
 * @kctx:  kbase context
 * @reg:   The region to update the flags on
 * @flags: The flags passed from user space
 *
 * The user space flag BASE_MEM_COHERENT_SYSTEM_REQUIRED will be rejected and
 * this function will fail if the system does not support system coherency.
 *
 * Return: 0 if successful, -EINVAL if the flags are not supported
 */
int kbase_update_region_flags(struct kbase_context *kctx,
		struct kbase_va_region *reg, unsigned long flags);

void kbase_gpu_vm_lock(struct kbase_context *kctx);
void kbase_gpu_vm_unlock(struct kbase_context *kctx);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size);

/**
 * kbase_mmu_init - Initialise an object representing GPU page tables
 *
 * The structure should be terminated using kbase_mmu_term()
 *
 * @kbdev:    Instance of GPU platform device, allocated from the probe method.
 * @mmut:     GPU page tables to be initialized.
 * @kctx:     Optional kbase context, may be NULL if this set of MMU tables
 *            is not associated with a context.
 * @group_id: The physical group ID from which to allocate GPU page tables.
 *            Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * Return:    0 if successful, otherwise a negative error code.
 */
int kbase_mmu_init(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
		struct kbase_context *kctx, int group_id);
/**
 * kbase_mmu_term - Terminate an object representing GPU page tables
 *
 * This will free any page tables that have been allocated
 *
 * @kbdev: Instance of GPU platform device, allocated from the probe method.
 * @mmut:  GPU page tables to be destroyed.
 */
void kbase_mmu_term(struct kbase_device *kbdev, struct kbase_mmu_table *mmut);

/**
 * kbase_mmu_create_ate - Create an address translation entry
 *
 * @kbdev:    Instance of GPU platform device, allocated from the probe method.
 * @phy:      Physical address of the page to be mapped for GPU access.
 * @flags:    Bitmask of attributes of the GPU memory region being mapped.
 * @level:    Page table level for which to build an address translation entry.
 * @group_id: The physical memory group in which the page was allocated.
 *            Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 *
 * This function creates an address translation entry to encode the physical
 * address of a page to be mapped for access by the GPU, along with any extra
 * attributes required for the GPU memory region.
 *
 * Return: An address translation entry, either in LPAE or AArch64 format
 *         (depending on the driver's configuration).
 */
u64 kbase_mmu_create_ate(struct kbase_device *kbdev,
	struct tagged_addr phy, unsigned long flags, int level, int group_id);

int kbase_mmu_insert_pages_no_flush(struct kbase_device *kbdev,
				    struct kbase_mmu_table *mmut,
				    const u64 start_vpfn,
				    struct tagged_addr *phys, size_t nr,
				    unsigned long flags, int group_id);
int kbase_mmu_insert_pages(struct kbase_device *kbdev,
			   struct kbase_mmu_table *mmut, u64 vpfn,
			   struct tagged_addr *phys, size_t nr,
			   unsigned long flags, int as_nr, int group_id);
int kbase_mmu_insert_single_page(struct kbase_context *kctx, u64 vpfn,
					struct tagged_addr phys, size_t nr,
					unsigned long flags, int group_id);

int kbase_mmu_teardown_pages(struct kbase_device *kbdev,
			     struct kbase_mmu_table *mmut, u64 vpfn,
			     size_t nr, int as_nr);
int kbase_mmu_update_pages(struct kbase_context *kctx, u64 vpfn,
			   struct tagged_addr *phys, size_t nr,
			   unsigned long flags, int const group_id);

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
 * kbase_mmu_update - Configure an address space on the GPU to the specified
 *                    MMU tables
 *
 * The caller has the following locking conditions:
 * - It must hold kbase_device->mmu_hw_mutex
 * - It must hold the hwaccess_lock
 *
 * @kbdev: Kbase device structure
 * @mmut:  The set of MMU tables to be configured on the address space
 * @as_nr: The address space to be configured
 */
void kbase_mmu_update(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
		int as_nr);

/**
 * kbase_mmu_disable() - Disable the MMU for a previously active kbase context.
 * @kctx:	Kbase context
 *
 * Disable and perform the required cache maintenance to remove the all
 * data from provided kbase context from the GPU caches.
 *
 * The caller has the following locking conditions:
 * - It must hold kbase_device->mmu_hw_mutex
 * - It must hold the hwaccess_lock
 */
void kbase_mmu_disable(struct kbase_context *kctx);

/**
 * kbase_mmu_disable_as() - Set the MMU to unmapped mode for the specified
 * address space.
 * @kbdev:	Kbase device
 * @as_nr:	The address space number to set to unmapped.
 *
 * This function must only be called during reset/power-up and it used to
 * ensure the registers are in a known state.
 *
 * The caller must hold kbdev->mmu_hw_mutex.
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

/**
 * kbase_sync_now - Perform cache maintenance on a memory region
 *
 * @kctx: The kbase context of the region
 * @sset: A syncset structure describing the region and direction of the
 *        synchronisation required
 *
 * Return: 0 on success or error code
 */
int kbase_sync_now(struct kbase_context *kctx, struct basep_syncset *sset);
void kbase_sync_single(struct kbase_context *kctx, struct tagged_addr cpu_pa,
		struct tagged_addr gpu_pa, off_t offset, size_t size,
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
 * kbasep_find_enclosing_cpu_mapping_offset() - Find the offset of the CPU
 * mapping of a memory allocation containing a given address range
 *
 * Searches for a CPU mapping of any part of any region that fully encloses the
 * CPU virtual address range specified by @uaddr and @size. Returns a failure
 * indication if only part of the address range lies within a CPU mapping.
 *
 * @kctx:      The kernel base context used for the allocation.
 * @uaddr:     Start of the CPU virtual address range.
 * @size:      Size of the CPU virtual address range (in bytes).
 * @offset:    The offset from the start of the allocation to the specified CPU
 *             virtual address.
 *
 * Return: 0 if offset was obtained successfully. Error code otherwise.
 */
int kbasep_find_enclosing_cpu_mapping_offset(
		struct kbase_context *kctx,
		unsigned long uaddr, size_t size, u64 *offset);

/**
 * kbasep_find_enclosing_gpu_mapping_start_and_offset() - Find the address of
 * the start of GPU virtual memory region which encloses @gpu_addr for the
 * @size length in bytes
 *
 * Searches for the memory region in GPU virtual memory space which contains
 * the region defined by the @gpu_addr and @size, where @gpu_addr is the
 * beginning and @size the length in bytes of the provided region. If found,
 * the location of the start address of the GPU virtual memory region is
 * passed in @start pointer and the location of the offset of the region into
 * the GPU virtual memory region is passed in @offset pointer.
 *
 * @kctx:	The kernel base context within which the memory is searched.
 * @gpu_addr:	GPU virtual address for which the region is sought; defines
 *              the beginning of the provided region.
 * @size:       The length (in bytes) of the provided region for which the
 *              GPU virtual memory region is sought.
 * @start:      Pointer to the location where the address of the start of
 *              the found GPU virtual memory region is.
 * @offset:     Pointer to the location where the offset of @gpu_addr into
 *              the found GPU virtual memory region is.
 */
int kbasep_find_enclosing_gpu_mapping_start_and_offset(
		struct kbase_context *kctx,
		u64 gpu_addr, size_t size, u64 *start, u64 *offset);

enum hrtimer_restart kbasep_as_poke_timer_callback(struct hrtimer *timer);
void kbase_as_poking_timer_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom);
void kbase_as_poking_timer_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * kbase_alloc_phy_pages_helper - Allocates physical pages.
 * @alloc:              allocation object to add pages to
 * @nr_pages_requested: number of physical pages to allocate
 *
 * Allocates \a nr_pages_requested and updates the alloc object.
 *
 * Return: 0 if all pages have been successfully allocated. Error code otherwise
 *
 * Note : The caller must not hold vm_lock, as this could cause a deadlock if
 * the kernel OoM killer runs. If the caller must allocate pages while holding
 * this lock, it should use kbase_mem_pool_alloc_pages_locked() instead.
 *
 * This function cannot be used from interrupt context
 */
int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc *alloc,
		size_t nr_pages_requested);

/**
 * kbase_alloc_phy_pages_helper_locked - Allocates physical pages.
 * @alloc:              allocation object to add pages to
 * @pool:               Memory pool to allocate from
 * @nr_pages_requested: number of physical pages to allocate
 * @prealloc_sa:        Information about the partial allocation if the amount
 *                      of memory requested is not a multiple of 2MB. One
 *                      instance of struct kbase_sub_alloc must be allocated by
 *                      the caller iff CONFIG_MALI_2MB_ALLOC is enabled.
 *
 * Allocates \a nr_pages_requested and updates the alloc object. This function
 * does not allocate new pages from the kernel, and therefore will never trigger
 * the OoM killer. Therefore, it can be run while the vm_lock is held.
 *
 * As new pages can not be allocated, the caller must ensure there are
 * sufficient pages in the pool. Usage of this function should look like :
 *
 *   kbase_gpu_vm_lock(kctx);
 *   kbase_mem_pool_lock(pool)
 *   while (kbase_mem_pool_size(pool) < pages_required) {
 *     kbase_mem_pool_unlock(pool)
 *     kbase_gpu_vm_unlock(kctx);
 *     kbase_mem_pool_grow(pool)
 *     kbase_gpu_vm_lock(kctx);
 *     kbase_mem_pool_lock(pool)
 *   }
 *   kbase_alloc_phy_pages_helper_locked(pool)
 *   kbase_mem_pool_unlock(pool)
 *   Perform other processing that requires vm_lock...
 *   kbase_gpu_vm_unlock(kctx);
 *
 * This ensures that the pool can be grown to the required size and that the
 * allocation can complete without another thread using the newly grown pages.
 *
 * If CONFIG_MALI_2MB_ALLOC is defined and the allocation is >= 2MB, then
 * @pool must be alloc->imported.native.kctx->lp_mem_pool. Otherwise it must be
 * alloc->imported.native.kctx->mem_pool.
 * @prealloc_sa is used to manage the non-2MB sub-allocation. It has to be
 * pre-allocated because we must not sleep (due to the usage of kmalloc())
 * whilst holding pool->pool_lock.
 * @prealloc_sa shall be set to NULL if it has been consumed by this function
 * to indicate that the caller must not free it.
 *
 * Return: Pointer to array of allocated pages. NULL on failure.
 *
 * Note : Caller must hold pool->pool_lock
 */
struct tagged_addr *kbase_alloc_phy_pages_helper_locked(
		struct kbase_mem_phy_alloc *alloc, struct kbase_mem_pool *pool,
		size_t nr_pages_requested,
		struct kbase_sub_alloc **prealloc_sa);

/**
* @brief Free physical pages.
*
* Frees \a nr_pages and updates the alloc object.
*
* @param[in] alloc allocation object to free pages from
* @param[in] nr_pages_to_free number of physical pages to free
*
* Return: 0 on success, otherwise a negative error code
*/
int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc *alloc, size_t nr_pages_to_free);

/**
 * kbase_free_phy_pages_helper_locked - Free pages allocated with
 *                                      kbase_alloc_phy_pages_helper_locked()
 * @alloc:            Allocation object to free pages from
 * @pool:             Memory pool to return freed pages to
 * @pages:            Pages allocated by kbase_alloc_phy_pages_helper_locked()
 * @nr_pages_to_free: Number of physical pages to free
 *
 * This function atomically frees pages allocated with
 * kbase_alloc_phy_pages_helper_locked(). @pages is the pointer to the page
 * array that is returned by that function. @pool must be the pool that the
 * pages were originally allocated from.
 *
 * If the mem_pool has been unlocked since the allocation then
 * kbase_free_phy_pages_helper() should be used instead.
 */
void kbase_free_phy_pages_helper_locked(struct kbase_mem_phy_alloc *alloc,
		struct kbase_mem_pool *pool, struct tagged_addr *pages,
		size_t nr_pages_to_free);

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
 * kbase_mmu_interrupt_process - Process a bus or page fault.
 * @kbdev   The kbase_device the fault happened on
 * @kctx    The kbase_context for the faulting address space if one was found.
 * @as      The address space that has the fault
 * @fault   Data relating to the fault
 *
 * This function will process a fault on a specific address space
 */
void kbase_mmu_interrupt_process(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_as *as,
		struct kbase_fault *fault);


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

#ifdef CONFIG_DEBUG_FS
/**
 * kbase_jit_debugfs_init - Add per context debugfs entry for JIT.
 * @kctx: kbase context
 */
void kbase_jit_debugfs_init(struct kbase_context *kctx);
#endif /* CONFIG_DEBUG_FS */

/**
 * kbase_jit_init - Initialize the JIT memory pool management
 * @kctx: kbase context
 *
 * Returns zero on success or negative error number on failure.
 */
int kbase_jit_init(struct kbase_context *kctx);

/**
 * kbase_jit_allocate - Allocate JIT memory
 * @kctx: kbase context
 * @info: JIT allocation information
 *
 * Return: JIT allocation on success or NULL on failure.
 */
struct kbase_va_region *kbase_jit_allocate(struct kbase_context *kctx,
		struct base_jit_alloc_info *info);

/**
 * kbase_jit_free - Free a JIT allocation
 * @kctx: kbase context
 * @reg: JIT allocation
 *
 * Frees a JIT allocation and places it into the free pool for later reuse.
 */
void kbase_jit_free(struct kbase_context *kctx, struct kbase_va_region *reg);

/**
 * kbase_jit_backing_lost - Inform JIT that an allocation has lost backing
 * @reg: JIT allocation
 */
void kbase_jit_backing_lost(struct kbase_va_region *reg);

/**
 * kbase_jit_evict - Evict a JIT allocation from the pool
 * @kctx: kbase context
 *
 * Evict the least recently used JIT allocation from the pool. This can be
 * required if normal VA allocations are failing due to VA exhaustion.
 *
 * Return: True if a JIT allocation was freed, false otherwise.
 */
bool kbase_jit_evict(struct kbase_context *kctx);

/**
 * kbase_jit_term - Terminate the JIT memory pool management
 * @kctx: kbase context
 */
void kbase_jit_term(struct kbase_context *kctx);

/**
 * kbase_has_exec_va_zone - EXEC_VA zone predicate
 *
 * Determine whether an EXEC_VA zone has been created for the GPU address space
 * of the given kbase context.
 *
 * @kctx: kbase context
 *
 * Return: True if the kbase context has an EXEC_VA zone.
 */
bool kbase_has_exec_va_zone(struct kbase_context *kctx);

/**
 * kbase_map_external_resource - Map an external resource to the GPU.
 * @kctx:              kbase context.
 * @reg:               The region to map.
 * @locked_mm:         The mm_struct which has been locked for this operation.
 *
 * Return: The physical allocation which backs the region on success or NULL
 * on failure.
 */
struct kbase_mem_phy_alloc *kbase_map_external_resource(
		struct kbase_context *kctx, struct kbase_va_region *reg,
		struct mm_struct *locked_mm);

/**
 * kbase_unmap_external_resource - Unmap an external resource from the GPU.
 * @kctx:  kbase context.
 * @reg:   The region to unmap or NULL if it has already been released.
 * @alloc: The physical allocation being unmapped.
 */
void kbase_unmap_external_resource(struct kbase_context *kctx,
		struct kbase_va_region *reg, struct kbase_mem_phy_alloc *alloc);


/**
 * kbase_jd_user_buf_pin_pages - Pin the pages of a user buffer.
 * @kctx: kbase context.
 * @reg:  The region associated with the imported user buffer.
 *
 * To successfully pin the pages for a user buffer the current mm_struct must
 * be the same as the mm_struct of the user buffer. After successfully pinning
 * the pages further calls to this function succeed without doing work.
 *
 * Return: zero on success or negative number on failure.
 */
int kbase_jd_user_buf_pin_pages(struct kbase_context *kctx,
		struct kbase_va_region *reg);

/**
 * kbase_sticky_resource_init - Initialize sticky resource management.
 * @kctx: kbase context
 *
 * Returns zero on success or negative error number on failure.
 */
int kbase_sticky_resource_init(struct kbase_context *kctx);

/**
 * kbase_sticky_resource_acquire - Acquire a reference on a sticky resource.
 * @kctx:     kbase context.
 * @gpu_addr: The GPU address of the external resource.
 *
 * Return: The metadata object which represents the binding between the
 * external resource and the kbase context on success or NULL on failure.
 */
struct kbase_ctx_ext_res_meta *kbase_sticky_resource_acquire(
		struct kbase_context *kctx, u64 gpu_addr);

/**
 * kbase_sticky_resource_release - Release a reference on a sticky resource.
 * @kctx:     kbase context.
 * @meta:     Binding metadata.
 * @gpu_addr: GPU address of the external resource.
 *
 * If meta is NULL then gpu_addr will be used to scan the metadata list and
 * find the matching metadata (if any), otherwise the provided meta will be
 * used and gpu_addr will be ignored.
 *
 * Return: True if the release found the metadata and the reference was dropped.
 */
bool kbase_sticky_resource_release(struct kbase_context *kctx,
		struct kbase_ctx_ext_res_meta *meta, u64 gpu_addr);

/**
 * kbase_sticky_resource_term - Terminate sticky resource management.
 * @kctx: kbase context
 */
void kbase_sticky_resource_term(struct kbase_context *kctx);

/**
 * kbase_mem_pool_lock - Lock a memory pool
 * @pool: Memory pool to lock
 */
static inline void kbase_mem_pool_lock(struct kbase_mem_pool *pool)
{
	spin_lock(&pool->pool_lock);
}

/**
 * kbase_mem_pool_lock - Release a memory pool
 * @pool: Memory pool to lock
 */
static inline void kbase_mem_pool_unlock(struct kbase_mem_pool *pool)
{
	spin_unlock(&pool->pool_lock);
}

/**
 * kbase_mem_evictable_mark_reclaim - Mark the pages as reclaimable.
 * @alloc: The physical allocation
 */
void kbase_mem_evictable_mark_reclaim(struct kbase_mem_phy_alloc *alloc);


/**
 * kbase_mem_umm_map - Map dma-buf
 * @kctx: Pointer to the kbase context
 * @reg: Pointer to the region of the imported dma-buf to map
 *
 * Map a dma-buf on the GPU. The mappings are reference counted.
 *
 * Returns 0 on success, or a negative error code.
 */
int kbase_mem_umm_map(struct kbase_context *kctx,
		struct kbase_va_region *reg);

/**
 * kbase_mem_umm_unmap - Unmap dma-buf
 * @kctx: Pointer to the kbase context
 * @reg: Pointer to the region of the imported dma-buf to unmap
 * @alloc: Pointer to the alloc to release
 *
 * Unmap a dma-buf from the GPU. The mappings are reference counted.
 *
 * @reg must be the original region with GPU mapping of @alloc; or NULL. If
 * @reg is NULL, or doesn't match @alloc, the GPU page table entries matching
 * @reg will not be updated.
 *
 * @alloc must be a valid physical allocation of type
 * KBASE_MEM_TYPE_IMPORTED_UMM that was previously mapped by
 * kbase_mem_umm_map(). The dma-buf attachment referenced by @alloc will
 * release it's mapping reference, and if the refcount reaches 0, also be be
 * unmapped, regardless of the value of @reg.
 */
void kbase_mem_umm_unmap(struct kbase_context *kctx,
		struct kbase_va_region *reg, struct kbase_mem_phy_alloc *alloc);

/**
 * kbase_mem_do_sync_imported - Sync caches for imported memory
 * @kctx: Pointer to the kbase context
 * @reg: Pointer to the region with imported memory to sync
 * @sync_fn: The type of sync operation to perform
 *
 * Sync CPU caches for supported (currently only dma-buf (UMM)) memory.
 * Attempting to sync unsupported imported memory types will result in an error
 * code, -EINVAL.
 *
 * Return: 0 on success, or a negative error code.
 */
int kbase_mem_do_sync_imported(struct kbase_context *kctx,
		struct kbase_va_region *reg, enum kbase_sync_type sync_fn);

#endif				/* _KBASE_MEM_H_ */
