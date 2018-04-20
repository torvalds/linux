/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
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
 * @file mali_kbase_mem_linux.c
 * Base kernel memory APIs, Linux implementation.
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0))
#include <linux/dma-attrs.h>
#endif /* LINUX_VERSION_CODE >= 3.5.0 && < 4.8.0 */
#ifdef CONFIG_DMA_SHARED_BUFFER
#include <linux/dma-buf.h>
#endif				/* defined(CONFIG_DMA_SHARED_BUFFER) */
#include <linux/shrinker.h>
#include <linux/cache.h>

#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_config_defaults.h>
#include <mali_kbase_tlstream.h>

static int kbase_tracking_page_setup(struct kbase_context *kctx, struct vm_area_struct *vma);

/**
 * kbase_mem_shrink_cpu_mapping - Shrink the CPU mapping(s) of an allocation
 * @kctx:      Context the region belongs to
 * @reg:       The GPU region
 * @new_pages: The number of pages after the shrink
 * @old_pages: The number of pages before the shrink
 *
 * Shrink (or completely remove) all CPU mappings which reference the shrunk
 * part of the allocation.
 *
 * Note: Caller must be holding the processes mmap_sem lock.
 */
static void kbase_mem_shrink_cpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages);

/**
 * kbase_mem_shrink_gpu_mapping - Shrink the GPU mapping of an allocation
 * @kctx:      Context the region belongs to
 * @reg:       The GPU region or NULL if there isn't one
 * @new_pages: The number of pages after the shrink
 * @old_pages: The number of pages before the shrink
 *
 * Return: 0 on success, negative -errno on error
 *
 * Unmap the shrunk pages from the GPU mapping. Note that the size of the region
 * itself is unmodified as we still need to reserve the VA, only the page tables
 * will be modified by this function.
 */
static int kbase_mem_shrink_gpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages);

struct kbase_va_region *kbase_mem_alloc(struct kbase_context *kctx,
		u64 va_pages, u64 commit_pages, u64 extent, u64 *flags,
		u64 *gpu_va)
{
	int zone;
	int gpu_pc_bits;
	struct kbase_va_region *reg;
	struct device *dev;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(flags);
	KBASE_DEBUG_ASSERT(gpu_va);

	dev = kctx->kbdev->dev;
	*gpu_va = 0; /* return 0 on failure */

	gpu_pc_bits = kctx->kbdev->gpu_props.props.core_props.log2_program_counter_size;

	if (0 == va_pages) {
		dev_warn(dev, "kbase_mem_alloc called with 0 va_pages!");
		goto bad_size;
	}

	if (va_pages > (U64_MAX / PAGE_SIZE))
		/* 64-bit address range is the max */
		goto bad_size;

	if (!kbase_check_alloc_flags(*flags)) {
		dev_warn(dev,
				"kbase_mem_alloc called with bad flags (%llx)",
				(unsigned long long)*flags);
		goto bad_flags;
	}

	if ((*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0 &&
			!kbase_device_is_cpu_coherent(kctx->kbdev)) {
		dev_warn(dev, "kbase_mem_alloc call required coherent mem when unavailable");
		goto bad_flags;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM) != 0 &&
			!kbase_device_is_cpu_coherent(kctx->kbdev)) {
		/* Remove COHERENT_SYSTEM flag if coherent mem is unavailable */
		*flags &= ~BASE_MEM_COHERENT_SYSTEM;
	}

	/* Limit GPU executable allocs to GPU PC size */
	if ((*flags & BASE_MEM_PROT_GPU_EX) &&
	    (va_pages > (1ULL << gpu_pc_bits >> PAGE_SHIFT)))
		goto bad_ex_size;

	/* find out which VA zone to use */
	if (*flags & BASE_MEM_SAME_VA)
		zone = KBASE_REG_ZONE_SAME_VA;
	else if (*flags & BASE_MEM_PROT_GPU_EX)
		zone = KBASE_REG_ZONE_EXEC;
	else
		zone = KBASE_REG_ZONE_CUSTOM_VA;

	reg = kbase_alloc_free_region(kctx, 0, va_pages, zone);
	if (!reg) {
		dev_err(dev, "Failed to allocate free region");
		goto no_region;
	}

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	if (kbase_reg_prepare_native(reg, kctx) != 0) {
		dev_err(dev, "Failed to prepare region");
		goto prepare_failed;
	}

	if (*flags & BASE_MEM_GROW_ON_GPF)
		reg->extent = extent;
	else
		reg->extent = 0;

	if (kbase_alloc_phy_pages(reg, va_pages, commit_pages) != 0) {
		dev_warn(dev, "Failed to allocate %lld pages (va_pages=%lld)",
				(unsigned long long)commit_pages,
				(unsigned long long)va_pages);
		goto no_mem;
	}

	kbase_gpu_vm_lock(kctx);

	/* mmap needed to setup VA? */
	if (*flags & BASE_MEM_SAME_VA) {
		unsigned long prot = PROT_NONE;
		unsigned long va_size = va_pages << PAGE_SHIFT;
		unsigned long va_map = va_size;
		unsigned long cookie, cookie_nr;
		unsigned long cpu_addr;

		/* Bind to a cookie */
		if (!kctx->cookies) {
			dev_err(dev, "No cookies available for allocation!");
			kbase_gpu_vm_unlock(kctx);
			goto no_cookie;
		}
		/* return a cookie */
		cookie_nr = __ffs(kctx->cookies);
		kctx->cookies &= ~(1UL << cookie_nr);
		BUG_ON(kctx->pending_regions[cookie_nr]);
		kctx->pending_regions[cookie_nr] = reg;

		kbase_gpu_vm_unlock(kctx);

		/* relocate to correct base */
		cookie = cookie_nr + PFN_DOWN(BASE_MEM_COOKIE_BASE);
		cookie <<= PAGE_SHIFT;

		/*
		 * 10.1-10.4 UKU userland relies on the kernel to call mmap.
		 * For all other versions we can just return the cookie
		 */
		if (kctx->api_version < KBASE_API_VERSION(10, 1) ||
		    kctx->api_version > KBASE_API_VERSION(10, 4)) {
			*gpu_va = (u64) cookie;
			return reg;
		}
		if (*flags & BASE_MEM_PROT_CPU_RD)
			prot |= PROT_READ;
		if (*flags & BASE_MEM_PROT_CPU_WR)
			prot |= PROT_WRITE;

		cpu_addr = vm_mmap(kctx->filp, 0, va_map, prot,
				MAP_SHARED, cookie);

		if (IS_ERR_VALUE(cpu_addr)) {
			kbase_gpu_vm_lock(kctx);
			kctx->pending_regions[cookie_nr] = NULL;
			kctx->cookies |= (1UL << cookie_nr);
			kbase_gpu_vm_unlock(kctx);
			goto no_mmap;
		}

		*gpu_va = (u64) cpu_addr;
	} else /* we control the VA */ {
		if (kbase_gpu_mmap(kctx, reg, 0, va_pages, 1) != 0) {
			dev_warn(dev, "Failed to map memory on GPU");
			kbase_gpu_vm_unlock(kctx);
			goto no_mmap;
		}
		/* return real GPU VA */
		*gpu_va = reg->start_pfn << PAGE_SHIFT;

		kbase_gpu_vm_unlock(kctx);
	}

	return reg;

no_mmap:
no_cookie:
no_mem:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
invalid_flags:
prepare_failed:
	kfree(reg);
no_region:
bad_ex_size:
bad_flags:
bad_size:
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_mem_alloc);

int kbase_mem_query(struct kbase_context *kctx, u64 gpu_addr, int query, u64 * const out)
{
	struct kbase_va_region *reg;
	int ret = -EINVAL;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(out);

	if (gpu_addr & ~PAGE_MASK) {
		dev_warn(kctx->kbdev->dev, "mem_query: gpu_addr: passed parameter is invalid");
		return -EINVAL;
	}

	kbase_gpu_vm_lock(kctx);

	/* Validate the region */
	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (!reg || (reg->flags & KBASE_REG_FREE))
		goto out_unlock;

	switch (query) {
	case KBASE_MEM_QUERY_COMMIT_SIZE:
		if (reg->cpu_alloc->type != KBASE_MEM_TYPE_ALIAS) {
			*out = kbase_reg_current_backed_size(reg);
		} else {
			size_t i;
			struct kbase_aliased *aliased;
			*out = 0;
			aliased = reg->cpu_alloc->imported.alias.aliased;
			for (i = 0; i < reg->cpu_alloc->imported.alias.nents; i++)
				*out += aliased[i].length;
		}
		break;
	case KBASE_MEM_QUERY_VA_SIZE:
		*out = reg->nr_pages;
		break;
	case KBASE_MEM_QUERY_FLAGS:
	{
		*out = 0;
		if (KBASE_REG_CPU_WR & reg->flags)
			*out |= BASE_MEM_PROT_CPU_WR;
		if (KBASE_REG_CPU_RD & reg->flags)
			*out |= BASE_MEM_PROT_CPU_RD;
		if (KBASE_REG_CPU_CACHED & reg->flags)
			*out |= BASE_MEM_CACHED_CPU;
		if (KBASE_REG_GPU_WR & reg->flags)
			*out |= BASE_MEM_PROT_GPU_WR;
		if (KBASE_REG_GPU_RD & reg->flags)
			*out |= BASE_MEM_PROT_GPU_RD;
		if (!(KBASE_REG_GPU_NX & reg->flags))
			*out |= BASE_MEM_PROT_GPU_EX;
		if (KBASE_REG_SHARE_BOTH & reg->flags)
			*out |= BASE_MEM_COHERENT_SYSTEM;
		if (KBASE_REG_SHARE_IN & reg->flags)
			*out |= BASE_MEM_COHERENT_LOCAL;
		break;
	}
	default:
		*out = 0;
		goto out_unlock;
	}

	ret = 0;

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;
}

/**
 * kbase_mem_evictable_reclaim_count_objects - Count number of pages in the
 * Ephemeral memory eviction list.
 * @s:        Shrinker
 * @sc:       Shrinker control
 *
 * Return: Number of pages which can be freed.
 */
static
unsigned long kbase_mem_evictable_reclaim_count_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_context *kctx;
	struct kbase_mem_phy_alloc *alloc;
	unsigned long pages = 0;

	kctx = container_of(s, struct kbase_context, reclaim);

	mutex_lock(&kctx->jit_evict_lock);

	list_for_each_entry(alloc, &kctx->evict_list, evict_node)
		pages += alloc->nents;

	mutex_unlock(&kctx->jit_evict_lock);
	return pages;
}

/**
 * kbase_mem_evictable_reclaim_scan_objects - Scan the Ephemeral memory eviction
 * list for pages and try to reclaim them.
 * @s:        Shrinker
 * @sc:       Shrinker control
 *
 * Return: Number of pages freed (can be less then requested) or -1 if the
 * shrinker failed to free pages in its pool.
 *
 * Note:
 * This function accesses region structures without taking the region lock,
 * this is required as the OOM killer can call the shrinker after the region
 * lock has already been held.
 * This is safe as we can guarantee that a region on the eviction list will
 * not be freed (kbase_mem_free_region removes the allocation from the list
 * before destroying it), or modified by other parts of the driver.
 * The eviction list itself is guarded by the eviction lock and the MMU updates
 * are protected by their own lock.
 */
static
unsigned long kbase_mem_evictable_reclaim_scan_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_context *kctx;
	struct kbase_mem_phy_alloc *alloc;
	struct kbase_mem_phy_alloc *tmp;
	unsigned long freed = 0;

	kctx = container_of(s, struct kbase_context, reclaim);
	mutex_lock(&kctx->jit_evict_lock);

	list_for_each_entry_safe(alloc, tmp, &kctx->evict_list, evict_node) {
		int err;

		err = kbase_mem_shrink_gpu_mapping(kctx, alloc->reg,
				0, alloc->nents);
		if (err != 0) {
			/*
			 * Failed to remove GPU mapping, tell the shrinker
			 * to stop trying to shrink our slab even though we
			 * have pages in it.
			 */
			freed = -1;
			goto out_unlock;
		}

		/*
		 * Update alloc->evicted before freeing the backing so the
		 * helper can determine that it needs to bypass the accounting
		 * and memory pool.
		 */
		alloc->evicted = alloc->nents;

		kbase_free_phy_pages_helper(alloc, alloc->evicted);
		freed += alloc->evicted;
		list_del_init(&alloc->evict_node);

		/*
		 * Inform the JIT allocator this region has lost backing
		 * as it might need to free the allocation.
		 */
		kbase_jit_backing_lost(alloc->reg);

		/* Enough pages have been freed so stop now */
		if (freed > sc->nr_to_scan)
			break;
	}
out_unlock:
	mutex_unlock(&kctx->jit_evict_lock);

	return freed;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
static int kbase_mem_evictable_reclaim_shrink(struct shrinker *s,
		struct shrink_control *sc)
{
	if (sc->nr_to_scan == 0)
		return kbase_mem_evictable_reclaim_count_objects(s, sc);

	return kbase_mem_evictable_reclaim_scan_objects(s, sc);
}
#endif

int kbase_mem_evictable_init(struct kbase_context *kctx)
{
	INIT_LIST_HEAD(&kctx->evict_list);
	mutex_init(&kctx->jit_evict_lock);

	/* Register shrinker */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	kctx->reclaim.shrink = kbase_mem_evictable_reclaim_shrink;
#else
	kctx->reclaim.count_objects = kbase_mem_evictable_reclaim_count_objects;
	kctx->reclaim.scan_objects = kbase_mem_evictable_reclaim_scan_objects;
#endif
	kctx->reclaim.seeks = DEFAULT_SEEKS;
	/* Kernel versions prior to 3.1 :
	 * struct shrinker does not define batch */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	kctx->reclaim.batch = 0;
#endif
	register_shrinker(&kctx->reclaim);
	return 0;
}

void kbase_mem_evictable_deinit(struct kbase_context *kctx)
{
	unregister_shrinker(&kctx->reclaim);
}

struct kbase_mem_zone_cache_entry {
	/* List head used to link the cache entry to the memory allocation. */
	struct list_head zone_node;
	/* The zone the cacheline is for. */
	struct zone *zone;
	/* The number of pages in the allocation which belong to this zone. */
	u64 count;
};

static bool kbase_zone_cache_builder(struct kbase_mem_phy_alloc *alloc,
		size_t start_offset)
{
	struct kbase_mem_zone_cache_entry *cache = NULL;
	size_t i;
	int ret = 0;

	for (i = start_offset; i < alloc->nents; i++) {
		struct page *p = phys_to_page(as_phys_addr_t(alloc->pages[i]));
		struct zone *zone = page_zone(p);
		bool create = true;

		if (cache && (cache->zone == zone)) {
			/*
			 * Fast path check as most of the time adjacent
			 * pages come from the same zone.
			 */
			create = false;
		} else {
			/*
			 * Slow path check, walk all the cache entries to see
			 * if we already know about this zone.
			 */
			list_for_each_entry(cache, &alloc->zone_cache, zone_node) {
				if (cache->zone == zone) {
					create = false;
					break;
				}
			}
		}

		/* This zone wasn't found in the cache, create an entry for it */
		if (create) {
			cache = kmalloc(sizeof(*cache), GFP_KERNEL);
			if (!cache) {
				ret = -ENOMEM;
				goto bail;
			}
			cache->zone = zone;
			cache->count = 0;
			list_add(&cache->zone_node, &alloc->zone_cache);
		}

		cache->count++;
	}
	return 0;

bail:
	return ret;
}

int kbase_zone_cache_update(struct kbase_mem_phy_alloc *alloc,
		size_t start_offset)
{
	/*
	 * Bail if the zone cache is empty, only update the cache if it
	 * existed in the first place.
	 */
	if (list_empty(&alloc->zone_cache))
		return 0;

	return kbase_zone_cache_builder(alloc, start_offset);
}

int kbase_zone_cache_build(struct kbase_mem_phy_alloc *alloc)
{
	/* Bail if the zone cache already exists */
	if (!list_empty(&alloc->zone_cache))
		return 0;

	return kbase_zone_cache_builder(alloc, 0);
}

void kbase_zone_cache_clear(struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_mem_zone_cache_entry *walker;

	while(!list_empty(&alloc->zone_cache)){
		walker = list_first_entry(&alloc->zone_cache,
				struct kbase_mem_zone_cache_entry, zone_node);
		list_del(&walker->zone_node);
		kfree(walker);
	}
}

/**
 * kbase_mem_evictable_mark_reclaim - Mark the pages as reclaimable.
 * @alloc: The physical allocation
 */
static void kbase_mem_evictable_mark_reclaim(struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_context *kctx = alloc->imported.kctx;
	struct kbase_mem_zone_cache_entry *zone_cache;
	int __maybe_unused new_page_count;
	int err;

	/* Attempt to build a zone cache of tracking */
	err = kbase_zone_cache_build(alloc);
	if (err == 0) {
		/* Bulk update all the zones */
		list_for_each_entry(zone_cache, &alloc->zone_cache, zone_node) {
			zone_page_state_add(zone_cache->count,
					zone_cache->zone, NR_SLAB_RECLAIMABLE);
		}
	} else {
		/* Fall-back to page by page updates */
		int i;

		for (i = 0; i < alloc->nents; i++) {
			struct page *p;
			struct zone *zone;

			p = phys_to_page(as_phys_addr_t(alloc->pages[i]));
			zone = page_zone(p);

			zone_page_state_add(1, zone, NR_SLAB_RECLAIMABLE);
		}
	}

	kbase_process_page_usage_dec(kctx, alloc->nents);
	new_page_count = kbase_atomic_sub_pages(alloc->nents,
						&kctx->used_pages);
	kbase_atomic_sub_pages(alloc->nents, &kctx->kbdev->memdev.used_pages);

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kctx->id,
			(u64)new_page_count);
}

/**
 * kbase_mem_evictable_unmark_reclaim - Mark the pages as no longer reclaimable.
 * @alloc: The physical allocation
 */
static
void kbase_mem_evictable_unmark_reclaim(struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_context *kctx = alloc->imported.kctx;
	struct kbase_mem_zone_cache_entry *zone_cache;
	int __maybe_unused new_page_count;
	int err;

	new_page_count = kbase_atomic_add_pages(alloc->nents,
						&kctx->used_pages);
	kbase_atomic_add_pages(alloc->nents, &kctx->kbdev->memdev.used_pages);

	/* Increase mm counters so that the allocation is accounted for
	 * against the process and thus is visible to the OOM killer,
	 * then remove it from the reclaimable accounting. */
	kbase_process_page_usage_inc(kctx, alloc->nents);

	/* Attempt to build a zone cache of tracking */
	err = kbase_zone_cache_build(alloc);
	if (err == 0) {
		/* Bulk update all the zones */
		list_for_each_entry(zone_cache, &alloc->zone_cache, zone_node) {
			zone_page_state_add(-zone_cache->count,
					zone_cache->zone, NR_SLAB_RECLAIMABLE);
		}
	} else {
		/* Fall-back to page by page updates */
		int i;

		for (i = 0; i < alloc->nents; i++) {
			struct page *p;
			struct zone *zone;

			p = phys_to_page(as_phys_addr_t(alloc->pages[i]));
			zone = page_zone(p);
			zone_page_state_add(-1, zone, NR_SLAB_RECLAIMABLE);
		}
	}

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kctx->id,
			(u64)new_page_count);
}

int kbase_mem_evictable_make(struct kbase_mem_phy_alloc *gpu_alloc)
{
	struct kbase_context *kctx = gpu_alloc->imported.kctx;

	lockdep_assert_held(&kctx->reg_lock);

	/* This alloction can't already be on a list. */
	WARN_ON(!list_empty(&gpu_alloc->evict_node));

	kbase_mem_shrink_cpu_mapping(kctx, gpu_alloc->reg,
			0, gpu_alloc->nents);

	/*
	 * Add the allocation to the eviction list, after this point the shrink
	 * can reclaim it.
	 */
	mutex_lock(&kctx->jit_evict_lock);
	list_add(&gpu_alloc->evict_node, &kctx->evict_list);
	mutex_unlock(&kctx->jit_evict_lock);
	kbase_mem_evictable_mark_reclaim(gpu_alloc);

	gpu_alloc->reg->flags |= KBASE_REG_DONT_NEED;
	return 0;
}

bool kbase_mem_evictable_unmake(struct kbase_mem_phy_alloc *gpu_alloc)
{
	struct kbase_context *kctx = gpu_alloc->imported.kctx;
	int err = 0;

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * First remove the allocation from the eviction list as it's no
	 * longer eligible for eviction.
	 */
	list_del_init(&gpu_alloc->evict_node);

	if (gpu_alloc->evicted == 0) {
		/*
		 * The backing is still present, update the VM stats as it's
		 * in use again.
		 */
		kbase_mem_evictable_unmark_reclaim(gpu_alloc);
	} else {
		/* If the region is still alive ... */
		if (gpu_alloc->reg) {
			/* ... allocate replacement backing ... */
			err = kbase_alloc_phy_pages_helper(gpu_alloc,
					gpu_alloc->evicted);

			/*
			 * ... and grow the mapping back to its
			 * pre-eviction size.
			 */
			if (!err)
				err = kbase_mem_grow_gpu_mapping(kctx,
						gpu_alloc->reg,
						gpu_alloc->evicted, 0);

			gpu_alloc->evicted = 0;
		}
	}

	/* If the region is still alive remove the DONT_NEED attribute. */
	if (gpu_alloc->reg)
		gpu_alloc->reg->flags &= ~KBASE_REG_DONT_NEED;

	return (err == 0);
}

int kbase_mem_flags_change(struct kbase_context *kctx, u64 gpu_addr, unsigned int flags, unsigned int mask)
{
	struct kbase_va_region *reg;
	int ret = -EINVAL;
	unsigned int real_flags = 0;
	unsigned int prev_flags = 0;
	bool prev_needed, new_needed;

	KBASE_DEBUG_ASSERT(kctx);

	if (!gpu_addr)
		return -EINVAL;

	if ((gpu_addr & ~PAGE_MASK) && (gpu_addr >= PAGE_SIZE))
		return -EINVAL;

	/* nuke other bits */
	flags &= mask;

	/* check for only supported flags */
	if (flags & ~(BASE_MEM_FLAGS_MODIFIABLE))
		goto out;

	/* mask covers bits we don't support? */
	if (mask & ~(BASE_MEM_FLAGS_MODIFIABLE))
		goto out;

	/* convert flags */
	if (BASE_MEM_COHERENT_SYSTEM & flags)
		real_flags |= KBASE_REG_SHARE_BOTH;
	else if (BASE_MEM_COHERENT_LOCAL & flags)
		real_flags |= KBASE_REG_SHARE_IN;

	/* now we can lock down the context, and find the region */
	down_write(&current->mm->mmap_sem);
	kbase_gpu_vm_lock(kctx);

	/* Validate the region */
	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (!reg || (reg->flags & KBASE_REG_FREE))
		goto out_unlock;

	/* Is the region being transitioning between not needed and needed? */
	prev_needed = (KBASE_REG_DONT_NEED & reg->flags) == KBASE_REG_DONT_NEED;
	new_needed = (BASE_MEM_DONT_NEED & flags) == BASE_MEM_DONT_NEED;
	if (prev_needed != new_needed) {
		/* Aliased allocations can't be made ephemeral */
		if (atomic_read(&reg->cpu_alloc->gpu_mappings) > 1)
			goto out_unlock;

		if (new_needed) {
			/* Only native allocations can be marked not needed */
			if (reg->cpu_alloc->type != KBASE_MEM_TYPE_NATIVE) {
				ret = -EINVAL;
				goto out_unlock;
			}
			ret = kbase_mem_evictable_make(reg->gpu_alloc);
			if (ret)
				goto out_unlock;
		} else {
			kbase_mem_evictable_unmake(reg->gpu_alloc);
		}
	}

	/* limit to imported memory */
	if ((reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMP) &&
	     (reg->gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM))
		goto out_unlock;

	/* no change? */
	if (real_flags == (reg->flags & (KBASE_REG_SHARE_IN | KBASE_REG_SHARE_BOTH))) {
		ret = 0;
		goto out_unlock;
	}

	/* save for roll back */
	prev_flags = reg->flags;
	reg->flags &= ~(KBASE_REG_SHARE_IN | KBASE_REG_SHARE_BOTH);
	reg->flags |= real_flags;

	/* Currently supporting only imported memory */
	switch (reg->gpu_alloc->type) {
#ifdef CONFIG_UMP
	case KBASE_MEM_TYPE_IMPORTED_UMP:
		ret = kbase_mmu_update_pages(kctx, reg->start_pfn,
					     kbase_get_gpu_phy_pages(reg),
				             reg->gpu_alloc->nents, reg->flags);
		break;
#endif
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM:
		/* Future use will use the new flags, existing mapping will NOT be updated
		 * as memory should not be in use by the GPU when updating the flags.
		 */
		ret = 0;
		WARN_ON(reg->gpu_alloc->imported.umm.current_mapping_usage_count);
		break;
#endif
	default:
		break;
	}

	/* roll back on error, i.e. not UMP */
	if (ret)
		reg->flags = prev_flags;

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	up_write(&current->mm->mmap_sem);
out:
	return ret;
}

#define KBASE_MEM_IMPORT_HAVE_PAGES (1UL << BASE_MEM_FLAGS_NR_BITS)

#ifdef CONFIG_UMP
static struct kbase_va_region *kbase_mem_from_ump(struct kbase_context *kctx, ump_secure_id id, u64 *va_pages, u64 *flags)
{
	struct kbase_va_region *reg;
	ump_dd_handle umph;
	u64 block_count;
	const ump_dd_physical_block_64 *block_array;
	u64 i, j;
	int page = 0;
	ump_alloc_flags ump_flags;
	ump_alloc_flags cpu_flags;
	ump_alloc_flags gpu_flags;

	if (*flags & BASE_MEM_SECURE)
		goto bad_flags;

	umph = ump_dd_from_secure_id(id);
	if (UMP_DD_INVALID_MEMORY_HANDLE == umph)
		goto bad_id;

	ump_flags = ump_dd_allocation_flags_get(umph);
	cpu_flags = (ump_flags >> UMP_DEVICE_CPU_SHIFT) & UMP_DEVICE_MASK;
	gpu_flags = (ump_flags >> DEFAULT_UMP_GPU_DEVICE_SHIFT) &
			UMP_DEVICE_MASK;

	*va_pages = ump_dd_size_get_64(umph);
	*va_pages >>= PAGE_SHIFT;

	if (!*va_pages)
		goto bad_size;

	if (*va_pages > (U64_MAX / PAGE_SIZE))
		/* 64-bit address range is the max */
		goto bad_size;

	if (*flags & BASE_MEM_SAME_VA)
		reg = kbase_alloc_free_region(kctx, 0, *va_pages, KBASE_REG_ZONE_SAME_VA);
	else
		reg = kbase_alloc_free_region(kctx, 0, *va_pages, KBASE_REG_ZONE_CUSTOM_VA);

	if (!reg)
		goto no_region;

	/* we've got pages to map now, and support SAME_VA */
	*flags |= KBASE_MEM_IMPORT_HAVE_PAGES;

	reg->gpu_alloc = kbase_alloc_create(*va_pages, KBASE_MEM_TYPE_IMPORTED_UMP);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	reg->gpu_alloc->imported.ump_handle = umph;

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_GPU_NX;	/* UMP is always No eXecute */
	reg->flags &= ~KBASE_REG_GROWABLE;	/* UMP cannot be grown */

	/* Override import flags based on UMP flags */
	*flags &= ~(BASE_MEM_CACHED_CPU);
	*flags &= ~(BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR);
	*flags &= ~(BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR);

	if ((cpu_flags & (UMP_HINT_DEVICE_RD | UMP_HINT_DEVICE_WR)) ==
	    (UMP_HINT_DEVICE_RD | UMP_HINT_DEVICE_WR)) {
		reg->flags |= KBASE_REG_CPU_CACHED;
		*flags |= BASE_MEM_CACHED_CPU;
	}

	if (cpu_flags & UMP_PROT_CPU_WR) {
		reg->flags |= KBASE_REG_CPU_WR;
		*flags |= BASE_MEM_PROT_CPU_WR;
	}

	if (cpu_flags & UMP_PROT_CPU_RD) {
		reg->flags |= KBASE_REG_CPU_RD;
		*flags |= BASE_MEM_PROT_CPU_RD;
	}

	if ((gpu_flags & (UMP_HINT_DEVICE_RD | UMP_HINT_DEVICE_WR)) ==
	    (UMP_HINT_DEVICE_RD | UMP_HINT_DEVICE_WR))
		reg->flags |= KBASE_REG_GPU_CACHED;

	if (gpu_flags & UMP_PROT_DEVICE_WR) {
		reg->flags |= KBASE_REG_GPU_WR;
		*flags |= BASE_MEM_PROT_GPU_WR;
	}

	if (gpu_flags & UMP_PROT_DEVICE_RD) {
		reg->flags |= KBASE_REG_GPU_RD;
		*flags |= BASE_MEM_PROT_GPU_RD;
	}

	/* ump phys block query */
	ump_dd_phys_blocks_get_64(umph, &block_count, &block_array);

	for (i = 0; i < block_count; i++) {
		for (j = 0; j < (block_array[i].size >> PAGE_SHIFT); j++) {
			struct tagged_addr tagged;

			tagged = as_tagged(block_array[i].addr +
					   (j << PAGE_SHIFT));
			reg->gpu_alloc->pages[page] = tagged;
			page++;
		}
	}
	reg->gpu_alloc->nents = *va_pages;
	reg->extent = 0;

	return reg;

no_alloc_obj:
	kfree(reg);
no_region:
bad_size:
	ump_dd_release(umph);
bad_id:
bad_flags:
	return NULL;
}
#endif				/* CONFIG_UMP */

#ifdef CONFIG_DMA_SHARED_BUFFER
static struct kbase_va_region *kbase_mem_from_umm(struct kbase_context *kctx,
		int fd, u64 *va_pages, u64 *flags, u32 padding)
{
	struct kbase_va_region *reg;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *dma_attachment;
	bool shared_zone = false;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma_buf))
		goto no_buf;

	dma_attachment = dma_buf_attach(dma_buf, kctx->kbdev->dev);
	if (!dma_attachment)
		goto no_attachment;

	*va_pages = (PAGE_ALIGN(dma_buf->size) >> PAGE_SHIFT) + padding;
	if (!*va_pages)
		goto bad_size;

	if (*va_pages > (U64_MAX / PAGE_SIZE))
		/* 64-bit address range is the max */
		goto bad_size;

	/* ignore SAME_VA */
	*flags &= ~BASE_MEM_SAME_VA;

	if (*flags & BASE_MEM_IMPORT_SHARED)
		shared_zone = true;

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		/*
		 * 64-bit tasks require us to reserve VA on the CPU that we use
		 * on the GPU.
		 */
		shared_zone = true;
	}
#endif

	if (shared_zone) {
		*flags |= BASE_MEM_NEED_MMAP;
		reg = kbase_alloc_free_region(kctx, 0, *va_pages, KBASE_REG_ZONE_SAME_VA);
	} else {
		reg = kbase_alloc_free_region(kctx, 0, *va_pages, KBASE_REG_ZONE_CUSTOM_VA);
	}

	if (!reg)
		goto no_region;

	reg->gpu_alloc = kbase_alloc_create(*va_pages, KBASE_MEM_TYPE_IMPORTED_UMM);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	/* No pages to map yet */
	reg->gpu_alloc->nents = 0;

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_GPU_NX;	/* UMM is always No eXecute */
	reg->flags &= ~KBASE_REG_GROWABLE;	/* UMM cannot be grown */
	reg->flags |= KBASE_REG_GPU_CACHED;

	if (*flags & BASE_MEM_SECURE)
		reg->flags |= KBASE_REG_SECURE;

	if (padding)
		reg->flags |= KBASE_REG_IMPORT_PAD;

	reg->gpu_alloc->type = KBASE_MEM_TYPE_IMPORTED_UMM;
	reg->gpu_alloc->imported.umm.sgt = NULL;
	reg->gpu_alloc->imported.umm.dma_buf = dma_buf;
	reg->gpu_alloc->imported.umm.dma_attachment = dma_attachment;
	reg->gpu_alloc->imported.umm.current_mapping_usage_count = 0;
	reg->extent = 0;

	return reg;

invalid_flags:
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc_obj:
	kfree(reg);
no_region:
bad_size:
	dma_buf_detach(dma_buf, dma_attachment);
no_attachment:
	dma_buf_put(dma_buf);
no_buf:
	return NULL;
}
#endif  /* CONFIG_DMA_SHARED_BUFFER */

static u32 kbase_get_cache_line_alignment(struct kbase_context *kctx)
{
	u32 cpu_cache_line_size = cache_line_size();
	u32 gpu_cache_line_size =
		(1UL << kctx->kbdev->gpu_props.props.l2_props.log2_line_size);

	return ((cpu_cache_line_size > gpu_cache_line_size) ?
				cpu_cache_line_size :
				gpu_cache_line_size);
}

static struct kbase_va_region *kbase_mem_from_user_buffer(
		struct kbase_context *kctx, unsigned long address,
		unsigned long size, u64 *va_pages, u64 *flags)
{
	long i;
	struct kbase_va_region *reg;
	long faulted_pages;
	int zone = KBASE_REG_ZONE_CUSTOM_VA;
	bool shared_zone = false;
	u32 cache_line_alignment = kbase_get_cache_line_alignment(kctx);
	struct kbase_alloc_import_user_buf *user_buf;
	struct page **pages = NULL;

	if ((address & (cache_line_alignment - 1)) != 0 ||
			(size & (cache_line_alignment - 1)) != 0) {
		/* Coherency must be enabled to handle partial cache lines */
		if (*flags & (BASE_MEM_COHERENT_SYSTEM |
			BASE_MEM_COHERENT_SYSTEM_REQUIRED)) {
			/* Force coherent system required flag, import will
			 * then fail if coherency isn't available
			 */
			*flags |= BASE_MEM_COHERENT_SYSTEM_REQUIRED;
		} else {
			dev_warn(kctx->kbdev->dev,
					"User buffer is not cache line aligned and no coherency enabled\n");
			goto bad_size;
		}
	}

	*va_pages = (PAGE_ALIGN(address + size) >> PAGE_SHIFT) -
		PFN_DOWN(address);
	if (!*va_pages)
		goto bad_size;

	if (*va_pages > (UINT64_MAX / PAGE_SIZE))
		/* 64-bit address range is the max */
		goto bad_size;

	/* SAME_VA generally not supported with imported memory (no known use cases) */
	*flags &= ~BASE_MEM_SAME_VA;

	if (*flags & BASE_MEM_IMPORT_SHARED)
		shared_zone = true;

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		/*
		 * 64-bit tasks require us to reserve VA on the CPU that we use
		 * on the GPU.
		 */
		shared_zone = true;
	}
#endif

	if (shared_zone) {
		*flags |= BASE_MEM_NEED_MMAP;
		zone = KBASE_REG_ZONE_SAME_VA;
	}

	reg = kbase_alloc_free_region(kctx, 0, *va_pages, zone);

	if (!reg)
		goto no_region;

	reg->gpu_alloc = kbase_alloc_create(*va_pages,
			KBASE_MEM_TYPE_IMPORTED_USER_BUF);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_GPU_NX; /* User-buffers are always No eXecute */
	reg->flags &= ~KBASE_REG_GROWABLE; /* Cannot be grown */

	user_buf = &reg->gpu_alloc->imported.user_buf;

	user_buf->size = size;
	user_buf->address = address;
	user_buf->nr_pages = *va_pages;
	user_buf->mm = current->mm;
	user_buf->pages = kmalloc_array(*va_pages, sizeof(struct page *),
			GFP_KERNEL);

	if (!user_buf->pages)
		goto no_page_array;

	/* If the region is coherent with the CPU then the memory is imported
	 * and mapped onto the GPU immediately.
	 * Otherwise get_user_pages is called as a sanity check, but with
	 * NULL as the pages argument which will fault the pages, but not
	 * pin them. The memory will then be pinned only around the jobs that
	 * specify the region as an external resource.
	 */
	if (reg->flags & KBASE_REG_SHARE_BOTH) {
		pages = user_buf->pages;
		*flags |= KBASE_MEM_IMPORT_HAVE_PAGES;
	}

	down_read(&current->mm->mmap_sem);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	faulted_pages = get_user_pages(current, current->mm, address, *va_pages,
			reg->flags & KBASE_REG_GPU_WR, 0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	faulted_pages = get_user_pages(address, *va_pages,
			reg->flags & KBASE_REG_GPU_WR, 0, pages, NULL);
#else
	faulted_pages = get_user_pages(address, *va_pages,
			reg->flags & KBASE_REG_GPU_WR ? FOLL_WRITE : 0,
			pages, NULL);
#endif

	up_read(&current->mm->mmap_sem);

	if (faulted_pages != *va_pages)
		goto fault_mismatch;

	atomic_inc(&current->mm->mm_count);

	reg->gpu_alloc->nents = 0;
	reg->extent = 0;

	if (pages) {
		struct device *dev = kctx->kbdev->dev;
		unsigned long local_size = user_buf->size;
		unsigned long offset = user_buf->address & ~PAGE_MASK;
		struct tagged_addr *pa = kbase_get_gpu_phy_pages(reg);

		/* Top bit signifies that this was pinned on import */
		user_buf->current_mapping_usage_count |= PINNED_ON_IMPORT;

		for (i = 0; i < faulted_pages; i++) {
			dma_addr_t dma_addr;
			unsigned long min;

			min = MIN(PAGE_SIZE - offset, local_size);
			dma_addr = dma_map_page(dev, pages[i],
					offset, min,
					DMA_BIDIRECTIONAL);
			if (dma_mapping_error(dev, dma_addr))
				goto unwind_dma_map;

			user_buf->dma_addrs[i] = dma_addr;
			pa[i] = as_tagged(page_to_phys(pages[i]));

			local_size -= min;
			offset = 0;
		}

		reg->gpu_alloc->nents = faulted_pages;
	}

	return reg;

unwind_dma_map:
	while (i--) {
		dma_unmap_page(kctx->kbdev->dev,
				user_buf->dma_addrs[i],
				PAGE_SIZE, DMA_BIDIRECTIONAL);
	}
fault_mismatch:
	if (pages) {
		for (i = 0; i < faulted_pages; i++)
			put_page(pages[i]);
	}
	kfree(user_buf->pages);
no_page_array:
invalid_flags:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc_obj:
	kfree(reg);
no_region:
bad_size:
	return NULL;

}


u64 kbase_mem_alias(struct kbase_context *kctx, u64 *flags, u64 stride,
		    u64 nents, struct base_mem_aliasing_info *ai,
		    u64 *num_pages)
{
	struct kbase_va_region *reg;
	u64 gpu_va;
	size_t i;
	bool coherent;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(flags);
	KBASE_DEBUG_ASSERT(ai);
	KBASE_DEBUG_ASSERT(num_pages);

	/* mask to only allowed flags */
	*flags &= (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
		   BASE_MEM_COHERENT_SYSTEM | BASE_MEM_COHERENT_LOCAL |
		   BASE_MEM_COHERENT_SYSTEM_REQUIRED);

	if (!(*flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR))) {
		dev_warn(kctx->kbdev->dev,
				"kbase_mem_alias called with bad flags (%llx)",
				(unsigned long long)*flags);
		goto bad_flags;
	}
	coherent = (*flags & BASE_MEM_COHERENT_SYSTEM) != 0 ||
			(*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0;

	if (!stride)
		goto bad_stride;

	if (!nents)
		goto bad_nents;

	if ((nents * stride) > (U64_MAX / PAGE_SIZE))
		/* 64-bit address range is the max */
		goto bad_size;

	/* calculate the number of pages this alias will cover */
	*num_pages = nents * stride;

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		/* 64-bit tasks must MMAP anyway, but not expose this address to
		 * clients */
		*flags |= BASE_MEM_NEED_MMAP;
		reg = kbase_alloc_free_region(kctx, 0, *num_pages,
					      KBASE_REG_ZONE_SAME_VA);
	} else {
#else
	if (1) {
#endif
		reg = kbase_alloc_free_region(kctx, 0, *num_pages,
					      KBASE_REG_ZONE_CUSTOM_VA);
	}

	if (!reg)
		goto no_reg;

	/* zero-sized page array, as we don't need one/can support one */
	reg->gpu_alloc = kbase_alloc_create(0, KBASE_MEM_TYPE_ALIAS);
	if (IS_ERR_OR_NULL(reg->gpu_alloc))
		goto no_alloc_obj;

	reg->cpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);

	if (kbase_update_region_flags(kctx, reg, *flags) != 0)
		goto invalid_flags;

	reg->gpu_alloc->imported.alias.nents = nents;
	reg->gpu_alloc->imported.alias.stride = stride;
	reg->gpu_alloc->imported.alias.aliased = vzalloc(sizeof(*reg->gpu_alloc->imported.alias.aliased) * nents);
	if (!reg->gpu_alloc->imported.alias.aliased)
		goto no_aliased_array;

	kbase_gpu_vm_lock(kctx);

	/* validate and add src handles */
	for (i = 0; i < nents; i++) {
		if (ai[i].handle.basep.handle < BASE_MEM_FIRST_FREE_ADDRESS) {
			if (ai[i].handle.basep.handle !=
			    BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE)
				goto bad_handle; /* unsupported magic handle */
			if (!ai[i].length)
				goto bad_handle; /* must be > 0 */
			if (ai[i].length > stride)
				goto bad_handle; /* can't be larger than the
						    stride */
			reg->gpu_alloc->imported.alias.aliased[i].length = ai[i].length;
		} else {
			struct kbase_va_region *aliasing_reg;
			struct kbase_mem_phy_alloc *alloc;

			aliasing_reg = kbase_region_tracker_find_region_base_address(
				kctx,
				(ai[i].handle.basep.handle >> PAGE_SHIFT) << PAGE_SHIFT);

			/* validate found region */
			if (!aliasing_reg)
				goto bad_handle; /* Not found */
			if (aliasing_reg->flags & KBASE_REG_FREE)
				goto bad_handle; /* Free region */
			if (aliasing_reg->flags & KBASE_REG_DONT_NEED)
				goto bad_handle; /* Ephemeral region */
			if (!aliasing_reg->gpu_alloc)
				goto bad_handle; /* No alloc */
			if (aliasing_reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
				goto bad_handle; /* Not a native alloc */
			if (coherent != ((aliasing_reg->flags & KBASE_REG_SHARE_BOTH) != 0))
				goto bad_handle;
				/* Non-coherent memory cannot alias
				   coherent memory, and vice versa.*/

			/* check size against stride */
			if (!ai[i].length)
				goto bad_handle; /* must be > 0 */
			if (ai[i].length > stride)
				goto bad_handle; /* can't be larger than the
						    stride */

			alloc = aliasing_reg->gpu_alloc;

			/* check against the alloc's size */
			if (ai[i].offset > alloc->nents)
				goto bad_handle; /* beyond end */
			if (ai[i].offset + ai[i].length > alloc->nents)
				goto bad_handle; /* beyond end */

			reg->gpu_alloc->imported.alias.aliased[i].alloc = kbase_mem_phy_alloc_get(alloc);
			reg->gpu_alloc->imported.alias.aliased[i].length = ai[i].length;
			reg->gpu_alloc->imported.alias.aliased[i].offset = ai[i].offset;
		}
	}

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		/* Bind to a cookie */
		if (!kctx->cookies) {
			dev_err(kctx->kbdev->dev, "No cookies available for allocation!");
			goto no_cookie;
		}
		/* return a cookie */
		gpu_va = __ffs(kctx->cookies);
		kctx->cookies &= ~(1UL << gpu_va);
		BUG_ON(kctx->pending_regions[gpu_va]);
		kctx->pending_regions[gpu_va] = reg;

		/* relocate to correct base */
		gpu_va += PFN_DOWN(BASE_MEM_COOKIE_BASE);
		gpu_va <<= PAGE_SHIFT;
	} else /* we control the VA */ {
#else
	if (1) {
#endif
		if (kbase_gpu_mmap(kctx, reg, 0, *num_pages, 1) != 0) {
			dev_warn(kctx->kbdev->dev, "Failed to map memory on GPU");
			goto no_mmap;
		}
		/* return real GPU VA */
		gpu_va = reg->start_pfn << PAGE_SHIFT;
	}

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags &= ~KBASE_REG_GROWABLE;

	kbase_gpu_vm_unlock(kctx);

	return gpu_va;

#ifdef CONFIG_64BIT
no_cookie:
#endif
no_mmap:
bad_handle:
	kbase_gpu_vm_unlock(kctx);
no_aliased_array:
invalid_flags:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc_obj:
	kfree(reg);
no_reg:
bad_size:
bad_nents:
bad_stride:
bad_flags:
	return 0;
}

int kbase_mem_import(struct kbase_context *kctx, enum base_mem_import_type type,
		void __user *phandle, u32 padding, u64 *gpu_va, u64 *va_pages,
		u64 *flags)
{
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(gpu_va);
	KBASE_DEBUG_ASSERT(va_pages);
	KBASE_DEBUG_ASSERT(flags);

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT))
		*flags |= BASE_MEM_SAME_VA;
#endif

	if (!kbase_check_import_flags(*flags)) {
		dev_warn(kctx->kbdev->dev,
				"kbase_mem_import called with bad flags (%llx)",
				(unsigned long long)*flags);
		goto bad_flags;
	}

	if ((*flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED) != 0 &&
			!kbase_device_is_cpu_coherent(kctx->kbdev)) {
		dev_warn(kctx->kbdev->dev,
				"kbase_mem_import call required coherent mem when unavailable");
		goto bad_flags;
	}
	if ((*flags & BASE_MEM_COHERENT_SYSTEM) != 0 &&
			!kbase_device_is_cpu_coherent(kctx->kbdev)) {
		/* Remove COHERENT_SYSTEM flag if coherent mem is unavailable */
		*flags &= ~BASE_MEM_COHERENT_SYSTEM;
	}

	if ((padding != 0) && (type != BASE_MEM_IMPORT_TYPE_UMM)) {
		dev_warn(kctx->kbdev->dev,
				"padding is only supported for UMM");
		goto bad_flags;
	}

	switch (type) {
#ifdef CONFIG_UMP
	case BASE_MEM_IMPORT_TYPE_UMP: {
		ump_secure_id id;

		if (get_user(id, (ump_secure_id __user *)phandle))
			reg = NULL;
		else
			reg = kbase_mem_from_ump(kctx, id, va_pages, flags);
	}
	break;
#endif /* CONFIG_UMP */
#ifdef CONFIG_DMA_SHARED_BUFFER
	case BASE_MEM_IMPORT_TYPE_UMM: {
		int fd;

		if (get_user(fd, (int __user *)phandle))
			reg = NULL;
		else
			reg = kbase_mem_from_umm(kctx, fd, va_pages, flags,
					padding);
	}
	break;
#endif /* CONFIG_DMA_SHARED_BUFFER */
	case BASE_MEM_IMPORT_TYPE_USER_BUFFER: {
		struct base_mem_import_user_buffer user_buffer;
		void __user *uptr;

		if (copy_from_user(&user_buffer, phandle,
				sizeof(user_buffer))) {
			reg = NULL;
		} else {
#ifdef CONFIG_COMPAT
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				uptr = compat_ptr(user_buffer.ptr);
			else
#endif
				uptr = u64_to_user_ptr(user_buffer.ptr);

			reg = kbase_mem_from_user_buffer(kctx,
					(unsigned long)uptr, user_buffer.length,
					va_pages, flags);
		}
		break;
	}
	default: {
		reg = NULL;
		break;
	}
	}

	if (!reg)
		goto no_reg;

	kbase_gpu_vm_lock(kctx);

	/* mmap needed to setup VA? */
	if (*flags & (BASE_MEM_SAME_VA | BASE_MEM_NEED_MMAP)) {
		/* Bind to a cookie */
		if (!kctx->cookies)
			goto no_cookie;
		/* return a cookie */
		*gpu_va = __ffs(kctx->cookies);
		kctx->cookies &= ~(1UL << *gpu_va);
		BUG_ON(kctx->pending_regions[*gpu_va]);
		kctx->pending_regions[*gpu_va] = reg;

		/* relocate to correct base */
		*gpu_va += PFN_DOWN(BASE_MEM_COOKIE_BASE);
		*gpu_va <<= PAGE_SHIFT;

	} else if (*flags & KBASE_MEM_IMPORT_HAVE_PAGES)  {
		/* we control the VA, mmap now to the GPU */
		if (kbase_gpu_mmap(kctx, reg, 0, *va_pages, 1) != 0)
			goto no_gpu_va;
		/* return real GPU VA */
		*gpu_va = reg->start_pfn << PAGE_SHIFT;
	} else {
		/* we control the VA, but nothing to mmap yet */
		if (kbase_add_va_region(kctx, reg, 0, *va_pages, 1) != 0)
			goto no_gpu_va;
		/* return real GPU VA */
		*gpu_va = reg->start_pfn << PAGE_SHIFT;
	}

	/* clear out private flags */
	*flags &= ((1UL << BASE_MEM_FLAGS_NR_BITS) - 1);

	kbase_gpu_vm_unlock(kctx);

	return 0;

no_gpu_va:
no_cookie:
	kbase_gpu_vm_unlock(kctx);
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
	kfree(reg);
no_reg:
bad_flags:
	*gpu_va = 0;
	*va_pages = 0;
	*flags = 0;
	return -ENOMEM;
}

int kbase_mem_grow_gpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages)
{
	struct tagged_addr *phy_pages;
	u64 delta = new_pages - old_pages;
	int ret = 0;

	lockdep_assert_held(&kctx->reg_lock);

	/* Map the new pages into the GPU */
	phy_pages = kbase_get_gpu_phy_pages(reg);
	ret = kbase_mmu_insert_pages(kctx, reg->start_pfn + old_pages,
			phy_pages + old_pages, delta, reg->flags);

	return ret;
}

static void kbase_mem_shrink_cpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages)
{
	u64 gpu_va_start = reg->start_pfn;

	if (new_pages == old_pages)
		/* Nothing to do */
		return;

	unmap_mapping_range(kctx->filp->f_inode->i_mapping,
			(gpu_va_start + new_pages)<<PAGE_SHIFT,
			(old_pages - new_pages)<<PAGE_SHIFT, 1);
}

static int kbase_mem_shrink_gpu_mapping(struct kbase_context *kctx,
		struct kbase_va_region *reg,
		u64 new_pages, u64 old_pages)
{
	u64 delta = old_pages - new_pages;
	int ret = 0;

	ret = kbase_mmu_teardown_pages(kctx,
			reg->start_pfn + new_pages, delta);

	return ret;
}

int kbase_mem_commit(struct kbase_context *kctx, u64 gpu_addr, u64 new_pages)
{
	u64 old_pages;
	u64 delta;
	int res = -EINVAL;
	struct kbase_va_region *reg;
	bool read_locked = false;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(gpu_addr != 0);

	if (gpu_addr & ~PAGE_MASK) {
		dev_warn(kctx->kbdev->dev, "kbase:mem_commit: gpu_addr: passed parameter is invalid");
		return -EINVAL;
	}

	down_write(&current->mm->mmap_sem);
	kbase_gpu_vm_lock(kctx);

	/* Validate the region */
	reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
	if (!reg || (reg->flags & KBASE_REG_FREE))
		goto out_unlock;

	KBASE_DEBUG_ASSERT(reg->cpu_alloc);
	KBASE_DEBUG_ASSERT(reg->gpu_alloc);

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE)
		goto out_unlock;

	if (0 == (reg->flags & KBASE_REG_GROWABLE))
		goto out_unlock;

	/* Would overflow the VA region */
	if (new_pages > reg->nr_pages)
		goto out_unlock;

	/* can't be mapped more than once on the GPU */
	if (atomic_read(&reg->gpu_alloc->gpu_mappings) > 1)
		goto out_unlock;
	/* can't grow regions which are ephemeral */
	if (reg->flags & KBASE_REG_DONT_NEED)
		goto out_unlock;

	if (new_pages == reg->gpu_alloc->nents) {
		/* no change */
		res = 0;
		goto out_unlock;
	}

	old_pages = kbase_reg_current_backed_size(reg);
	if (new_pages > old_pages) {
		delta = new_pages - old_pages;

		/*
		 * No update to the mm so downgrade the writer lock to a read
		 * lock so other readers aren't blocked after this point.
		 */
		downgrade_write(&current->mm->mmap_sem);
		read_locked = true;

		/* Allocate some more pages */
		if (kbase_alloc_phy_pages_helper(reg->cpu_alloc, delta) != 0) {
			res = -ENOMEM;
			goto out_unlock;
		}
		if (reg->cpu_alloc != reg->gpu_alloc) {
			if (kbase_alloc_phy_pages_helper(
					reg->gpu_alloc, delta) != 0) {
				res = -ENOMEM;
				kbase_free_phy_pages_helper(reg->cpu_alloc,
						delta);
				goto out_unlock;
			}
		}

		/* No update required for CPU mappings, that's done on fault. */

		/* Update GPU mapping. */
		res = kbase_mem_grow_gpu_mapping(kctx, reg,
				new_pages, old_pages);

		/* On error free the new pages */
		if (res) {
			kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
			if (reg->cpu_alloc != reg->gpu_alloc)
				kbase_free_phy_pages_helper(reg->gpu_alloc,
						delta);
			res = -ENOMEM;
			goto out_unlock;
		}
	} else {
		delta = old_pages - new_pages;

		/* Update all CPU mapping(s) */
		kbase_mem_shrink_cpu_mapping(kctx, reg,
				new_pages, old_pages);

		/* Update the GPU mapping */
		res = kbase_mem_shrink_gpu_mapping(kctx, reg,
				new_pages, old_pages);
		if (res) {
			res = -ENOMEM;
			goto out_unlock;
		}

		kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
		if (reg->cpu_alloc != reg->gpu_alloc)
			kbase_free_phy_pages_helper(reg->gpu_alloc, delta);
	}

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	if (read_locked)
		up_read(&current->mm->mmap_sem);
	else
		up_write(&current->mm->mmap_sem);

	return res;
}

static void kbase_cpu_vm_open(struct vm_area_struct *vma)
{
	struct kbase_cpu_mapping *map = vma->vm_private_data;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);
	/* non-atomic as we're under Linux' mm lock */
	map->count++;
}

static void kbase_cpu_vm_close(struct vm_area_struct *vma)
{
	struct kbase_cpu_mapping *map = vma->vm_private_data;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);

	/* non-atomic as we're under Linux' mm lock */
	if (--map->count)
		return;

	KBASE_DEBUG_ASSERT(map->kctx);
	KBASE_DEBUG_ASSERT(map->alloc);

	kbase_gpu_vm_lock(map->kctx);

	if (map->free_on_close) {
		KBASE_DEBUG_ASSERT((map->region->flags & KBASE_REG_ZONE_MASK) ==
				KBASE_REG_ZONE_SAME_VA);
		/* Avoid freeing memory on the process death which results in
		 * GPU Page Fault. Memory will be freed in kbase_destroy_context
		 */
		if (!(current->flags & PF_EXITING))
			kbase_mem_free_region(map->kctx, map->region);
	}

	list_del(&map->mappings_list);

	kbase_gpu_vm_unlock(map->kctx);

	kbase_mem_phy_alloc_put(map->alloc);
	kfree(map);
}

KBASE_EXPORT_TEST_API(kbase_cpu_vm_close);


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
static int kbase_cpu_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static int kbase_cpu_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct kbase_cpu_mapping *map = vma->vm_private_data;
	pgoff_t rel_pgoff;
	size_t i;
	pgoff_t addr;

	KBASE_DEBUG_ASSERT(map);
	KBASE_DEBUG_ASSERT(map->count > 0);
	KBASE_DEBUG_ASSERT(map->kctx);
	KBASE_DEBUG_ASSERT(map->alloc);

	rel_pgoff = vmf->pgoff - map->region->start_pfn;

	kbase_gpu_vm_lock(map->kctx);
	if (rel_pgoff >= map->alloc->nents)
		goto locked_bad_fault;

	/* Fault on access to DONT_NEED regions */
	if (map->alloc->reg && (map->alloc->reg->flags & KBASE_REG_DONT_NEED))
		goto locked_bad_fault;

	/* insert all valid pages from the fault location */
	i = rel_pgoff;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
	addr = (pgoff_t)((uintptr_t)vmf->virtual_address >> PAGE_SHIFT);
#else
	addr = (pgoff_t)(vmf->address >> PAGE_SHIFT);
#endif
	while (i < map->alloc->nents && (addr < vma->vm_end >> PAGE_SHIFT)) {
		int ret = vm_insert_pfn(vma, addr << PAGE_SHIFT,
		    PFN_DOWN(as_phys_addr_t(map->alloc->pages[i])));
		if (ret < 0 && ret != -EBUSY)
			goto locked_bad_fault;

		i++; addr++;
	}

	kbase_gpu_vm_unlock(map->kctx);
	/* we resolved it, nothing for VM to do */
	return VM_FAULT_NOPAGE;

locked_bad_fault:
	kbase_gpu_vm_unlock(map->kctx);
	return VM_FAULT_SIGBUS;
}

const struct vm_operations_struct kbase_vm_ops = {
	.open  = kbase_cpu_vm_open,
	.close = kbase_cpu_vm_close,
	.fault = kbase_cpu_vm_fault
};

static int kbase_cpu_mmap(struct kbase_va_region *reg, struct vm_area_struct *vma, void *kaddr, size_t nr_pages, unsigned long aligned_offset, int free_on_close)
{
	struct kbase_cpu_mapping *map;
	struct tagged_addr *page_array;
	int err = 0;
	int i;

	map = kzalloc(sizeof(*map), GFP_KERNEL);

	if (!map) {
		WARN_ON(1);
		err = -ENOMEM;
		goto out;
	}

	/*
	 * VM_DONTCOPY - don't make this mapping available in fork'ed processes
	 * VM_DONTEXPAND - disable mremap on this region
	 * VM_IO - disables paging
	 * VM_DONTDUMP - Don't include in core dumps (3.7 only)
	 * VM_MIXEDMAP - Support mixing struct page*s and raw pfns.
	 *               This is needed to support using the dedicated and
	 *               the OS based memory backends together.
	 */
	/*
	 * This will need updating to propagate coherency flags
	 * See MIDBASE-1057
	 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	vma->vm_flags |= VM_DONTCOPY | VM_DONTDUMP | VM_DONTEXPAND | VM_IO;
#else
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED | VM_IO;
#endif
	vma->vm_ops = &kbase_vm_ops;
	vma->vm_private_data = map;

	page_array = kbase_get_cpu_phy_pages(reg);

	if (!(reg->flags & KBASE_REG_CPU_CACHED) &&
	    (reg->flags & (KBASE_REG_CPU_WR|KBASE_REG_CPU_RD))) {
		/* We can't map vmalloc'd memory uncached.
		 * Other memory will have been returned from
		 * kbase_mem_pool which would be
		 * suitable for mapping uncached.
		 */
		BUG_ON(kaddr);
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	}

	if (!kaddr) {
		unsigned long addr = vma->vm_start + aligned_offset;
		u64 start_off = vma->vm_pgoff - reg->start_pfn +
			(aligned_offset>>PAGE_SHIFT);

		vma->vm_flags |= VM_PFNMAP;
		for (i = 0; i < nr_pages; i++) {
			phys_addr_t phys;

			phys = as_phys_addr_t(page_array[i + start_off]);
			err = vm_insert_pfn(vma, addr, PFN_DOWN(phys));
			if (WARN_ON(err))
				break;

			addr += PAGE_SIZE;
		}
	} else {
		WARN_ON(aligned_offset);
		/* MIXEDMAP so we can vfree the kaddr early and not track it after map time */
		vma->vm_flags |= VM_MIXEDMAP;
		/* vmalloc remaping is easy... */
		err = remap_vmalloc_range(vma, kaddr, 0);
		WARN_ON(err);
	}

	if (err) {
		kfree(map);
		goto out;
	}

	map->region = reg;
	map->free_on_close = free_on_close;
	map->kctx = reg->kctx;
	map->alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	map->count = 1; /* start with one ref */

	if (reg->flags & KBASE_REG_CPU_CACHED)
		map->alloc->properties |= KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED;

	list_add(&map->mappings_list, &map->alloc->mappings);

 out:
	return err;
}

static int kbase_trace_buffer_mmap(struct kbase_context *kctx, struct vm_area_struct *vma, struct kbase_va_region **const reg, void **const kaddr)
{
	struct kbase_va_region *new_reg;
	u32 nr_pages;
	size_t size;
	int err = 0;
	u32 *tb;
	int owns_tb = 1;

	dev_dbg(kctx->kbdev->dev, "in %s\n", __func__);
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size >> PAGE_SHIFT;

	if (!kctx->jctx.tb) {
		KBASE_DEBUG_ASSERT(0 != size);
		tb = vmalloc_user(size);

		if (NULL == tb) {
			err = -ENOMEM;
			goto out;
		}

		err = kbase_device_trace_buffer_install(kctx, tb, size);
		if (err) {
			vfree(tb);
			goto out;
		}
	} else {
		err = -EINVAL;
		goto out;
	}

	*kaddr = kctx->jctx.tb;

	new_reg = kbase_alloc_free_region(kctx, 0, nr_pages, KBASE_REG_ZONE_SAME_VA);
	if (!new_reg) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out_no_region;
	}

	new_reg->cpu_alloc = kbase_alloc_create(0, KBASE_MEM_TYPE_TB);
	if (IS_ERR_OR_NULL(new_reg->cpu_alloc)) {
		err = -ENOMEM;
		new_reg->cpu_alloc = NULL;
		WARN_ON(1);
		goto out_no_alloc;
	}

	new_reg->gpu_alloc = kbase_mem_phy_alloc_get(new_reg->cpu_alloc);

	new_reg->cpu_alloc->imported.kctx = kctx;
	new_reg->flags &= ~KBASE_REG_FREE;
	new_reg->flags |= KBASE_REG_CPU_CACHED;

	/* alloc now owns the tb */
	owns_tb = 0;

	if (kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1) != 0) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out_no_va_region;
	}

	*reg = new_reg;

	/* map read only, noexec */
	vma->vm_flags &= ~(VM_WRITE | VM_MAYWRITE | VM_EXEC | VM_MAYEXEC);
	/* the rest of the flags is added by the cpu_mmap handler */

	dev_dbg(kctx->kbdev->dev, "%s done\n", __func__);
	return 0;

out_no_va_region:
out_no_alloc:
	kbase_free_alloced_region(new_reg);
out_no_region:
	if (owns_tb) {
		kbase_device_trace_buffer_uninstall(kctx);
		vfree(tb);
	}
out:
	return err;
}

static int kbase_mmu_dump_mmap(struct kbase_context *kctx, struct vm_area_struct *vma, struct kbase_va_region **const reg, void **const kmap_addr)
{
	struct kbase_va_region *new_reg;
	void *kaddr;
	u32 nr_pages;
	size_t size;
	int err = 0;

	dev_dbg(kctx->kbdev->dev, "in kbase_mmu_dump_mmap\n");
	size = (vma->vm_end - vma->vm_start);
	nr_pages = size >> PAGE_SHIFT;

	kaddr = kbase_mmu_dump(kctx, nr_pages);

	if (!kaddr) {
		err = -ENOMEM;
		goto out;
	}

	new_reg = kbase_alloc_free_region(kctx, 0, nr_pages, KBASE_REG_ZONE_SAME_VA);
	if (!new_reg) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out;
	}

	new_reg->cpu_alloc = kbase_alloc_create(0, KBASE_MEM_TYPE_RAW);
	if (IS_ERR_OR_NULL(new_reg->cpu_alloc)) {
		err = -ENOMEM;
		new_reg->cpu_alloc = NULL;
		WARN_ON(1);
		goto out_no_alloc;
	}

	new_reg->gpu_alloc = kbase_mem_phy_alloc_get(new_reg->cpu_alloc);

	new_reg->flags &= ~KBASE_REG_FREE;
	new_reg->flags |= KBASE_REG_CPU_CACHED;
	if (kbase_add_va_region(kctx, new_reg, vma->vm_start, nr_pages, 1) != 0) {
		err = -ENOMEM;
		WARN_ON(1);
		goto out_va_region;
	}

	*kmap_addr = kaddr;
	*reg = new_reg;

	dev_dbg(kctx->kbdev->dev, "kbase_mmu_dump_mmap done\n");
	return 0;

out_no_alloc:
out_va_region:
	kbase_free_alloced_region(new_reg);
out:
	return err;
}


void kbase_os_mem_map_lock(struct kbase_context *kctx)
{
	struct mm_struct *mm = current->mm;
	(void)kctx;
	down_read(&mm->mmap_sem);
}

void kbase_os_mem_map_unlock(struct kbase_context *kctx)
{
	struct mm_struct *mm = current->mm;
	(void)kctx;
	up_read(&mm->mmap_sem);
}

static int kbasep_reg_mmap(struct kbase_context *kctx,
			   struct vm_area_struct *vma,
			   struct kbase_va_region **regm,
			   size_t *nr_pages, size_t *aligned_offset)

{
	int cookie = vma->vm_pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);
	struct kbase_va_region *reg;
	int err = 0;

	*aligned_offset = 0;

	dev_dbg(kctx->kbdev->dev, "in kbasep_reg_mmap\n");

	/* SAME_VA stuff, fetch the right region */
	reg = kctx->pending_regions[cookie];
	if (!reg) {
		err = -ENOMEM;
		goto out;
	}

	if ((reg->flags & KBASE_REG_GPU_NX) && (reg->nr_pages != *nr_pages)) {
		/* incorrect mmap size */
		/* leave the cookie for a potential later
		 * mapping, or to be reclaimed later when the
		 * context is freed */
		err = -ENOMEM;
		goto out;
	}

	if ((vma->vm_flags & VM_READ && !(reg->flags & KBASE_REG_CPU_RD)) ||
	    (vma->vm_flags & VM_WRITE && !(reg->flags & KBASE_REG_CPU_WR))) {
		/* VM flags inconsistent with region flags */
		err = -EPERM;
		dev_err(kctx->kbdev->dev, "%s:%d inconsistent VM flags\n",
							__FILE__, __LINE__);
		goto out;
	}

	/* adjust down nr_pages to what we have physically */
	*nr_pages = kbase_reg_current_backed_size(reg);

	if (kbase_gpu_mmap(kctx, reg, vma->vm_start + *aligned_offset,
						reg->nr_pages, 1) != 0) {
		dev_err(kctx->kbdev->dev, "%s:%d\n", __FILE__, __LINE__);
		/* Unable to map in GPU space. */
		WARN_ON(1);
		err = -ENOMEM;
		goto out;
	}
	/* no need for the cookie anymore */
	kctx->pending_regions[cookie] = NULL;
	kctx->cookies |= (1UL << cookie);

	/*
	 * Overwrite the offset with the region start_pfn, so we effectively
	 * map from offset 0 in the region. However subtract the aligned
	 * offset so that when user space trims the mapping the beginning of
	 * the trimmed VMA has the correct vm_pgoff;
	 */
	vma->vm_pgoff = reg->start_pfn - ((*aligned_offset)>>PAGE_SHIFT);
out:
	*regm = reg;
	dev_dbg(kctx->kbdev->dev, "kbasep_reg_mmap done\n");

	return err;
}

int kbase_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kbase_context *kctx = file->private_data;
	struct kbase_va_region *reg = NULL;
	void *kaddr = NULL;
	size_t nr_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	int err = 0;
	int free_on_close = 0;
	struct device *dev = kctx->kbdev->dev;
	size_t aligned_offset = 0;

	dev_dbg(dev, "kbase_mmap\n");

	/* strip away corresponding VM_MAY% flags to the VM_% flags requested */
	vma->vm_flags &= ~((vma->vm_flags & (VM_READ | VM_WRITE)) << 4);

	if (0 == nr_pages) {
		err = -EINVAL;
		goto out;
	}

	if (!(vma->vm_flags & VM_SHARED)) {
		err = -EINVAL;
		goto out;
	}

	kbase_gpu_vm_lock(kctx);

	if (vma->vm_pgoff == PFN_DOWN(BASE_MEM_MAP_TRACKING_HANDLE)) {
		/* The non-mapped tracking helper page */
		err = kbase_tracking_page_setup(kctx, vma);
		goto out_unlock;
	}

	/* if not the MTP, verify that the MTP has been mapped */
	rcu_read_lock();
	/* catches both when the special page isn't present or
	 * when we've forked */
	if (rcu_dereference(kctx->process_mm) != current->mm) {
		err = -EINVAL;
		rcu_read_unlock();
		goto out_unlock;
	}
	rcu_read_unlock();

	switch (vma->vm_pgoff) {
	case PFN_DOWN(BASEP_MEM_INVALID_HANDLE):
	case PFN_DOWN(BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE):
		/* Illegal handle for direct map */
		err = -EINVAL;
		goto out_unlock;
	case PFN_DOWN(BASE_MEM_TRACE_BUFFER_HANDLE):
		err = kbase_trace_buffer_mmap(kctx, vma, &reg, &kaddr);
		if (0 != err)
			goto out_unlock;
		dev_dbg(dev, "kbase_trace_buffer_mmap ok\n");
		/* free the region on munmap */
		free_on_close = 1;
		break;
	case PFN_DOWN(BASE_MEM_MMU_DUMP_HANDLE):
		/* MMU dump */
		err = kbase_mmu_dump_mmap(kctx, vma, &reg, &kaddr);
		if (0 != err)
			goto out_unlock;
		/* free the region on munmap */
		free_on_close = 1;
		break;
	case PFN_DOWN(BASE_MEM_COOKIE_BASE) ...
	     PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) - 1: {
		err = kbasep_reg_mmap(kctx, vma, &reg, &nr_pages,
							&aligned_offset);
		if (0 != err)
			goto out_unlock;
		/* free the region on munmap */
		free_on_close = 1;
		break;
	}
	default: {
		reg = kbase_region_tracker_find_region_enclosing_address(kctx,
					(u64)vma->vm_pgoff << PAGE_SHIFT);

		if (reg && !(reg->flags & KBASE_REG_FREE)) {
			/* will this mapping overflow the size of the region? */
			if (nr_pages > (reg->nr_pages -
					(vma->vm_pgoff - reg->start_pfn))) {
				err = -ENOMEM;
				goto out_unlock;
			}

			if ((vma->vm_flags & VM_READ &&
			     !(reg->flags & KBASE_REG_CPU_RD)) ||
			    (vma->vm_flags & VM_WRITE &&
			     !(reg->flags & KBASE_REG_CPU_WR))) {
				/* VM flags inconsistent with region flags */
				err = -EPERM;
				dev_err(dev, "%s:%d inconsistent VM flags\n",
					__FILE__, __LINE__);
				goto out_unlock;
			}

#ifdef CONFIG_DMA_SHARED_BUFFER
			if (KBASE_MEM_TYPE_IMPORTED_UMM ==
							reg->cpu_alloc->type) {
				err = dma_buf_mmap(
					reg->cpu_alloc->imported.umm.dma_buf,
					vma, vma->vm_pgoff - reg->start_pfn);
				goto out_unlock;
			}
#endif /* CONFIG_DMA_SHARED_BUFFER */

			/* limit what we map to the amount currently backed */
			if (reg->cpu_alloc->nents < (vma->vm_pgoff - reg->start_pfn + nr_pages)) {
				if ((vma->vm_pgoff - reg->start_pfn) >= reg->cpu_alloc->nents)
					nr_pages = 0;
				else
					nr_pages = reg->cpu_alloc->nents - (vma->vm_pgoff - reg->start_pfn);
			}
		} else {
			err = -ENOMEM;
			goto out_unlock;
		}
	} /* default */
	} /* switch */

	err = kbase_cpu_mmap(reg, vma, kaddr, nr_pages, aligned_offset, free_on_close);

	if (vma->vm_pgoff == PFN_DOWN(BASE_MEM_MMU_DUMP_HANDLE)) {
		/* MMU dump - userspace should now have a reference on
		 * the pages, so we can now free the kernel mapping */
		vfree(kaddr);
	}

out_unlock:
	kbase_gpu_vm_unlock(kctx);
out:
	if (err)
		dev_err(dev, "mmap failed %d\n", err);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_mmap);

static void kbasep_sync_mem_regions(struct kbase_context *kctx,
		struct kbase_vmap_struct *map, enum kbase_sync_type dest)
{
	size_t i;
	off_t const offset = (uintptr_t)map->gpu_addr & ~PAGE_MASK;
	size_t const page_count = PFN_UP(offset + map->size);

	/* Sync first page */
	size_t sz = MIN(((size_t) PAGE_SIZE - offset), map->size);
	struct tagged_addr cpu_pa = map->cpu_pages[0];
	struct tagged_addr gpu_pa = map->gpu_pages[0];

	kbase_sync_single(kctx, cpu_pa, gpu_pa, offset, sz, dest);

	/* Sync middle pages (if any) */
	for (i = 1; page_count > 2 && i < page_count - 1; i++) {
		cpu_pa = map->cpu_pages[i];
		gpu_pa = map->gpu_pages[i];
		kbase_sync_single(kctx, cpu_pa, gpu_pa, 0, PAGE_SIZE, dest);
	}

	/* Sync last page (if any) */
	if (page_count > 1) {
		cpu_pa = map->cpu_pages[page_count - 1];
		gpu_pa = map->gpu_pages[page_count - 1];
		sz = ((offset + map->size - 1) & ~PAGE_MASK) + 1;
		kbase_sync_single(kctx, cpu_pa, gpu_pa, 0, sz, dest);
	}
}

void *kbase_vmap_prot(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		      unsigned long prot_request, struct kbase_vmap_struct *map)
{
	struct kbase_va_region *reg;
	unsigned long page_index;
	unsigned int offset = gpu_addr & ~PAGE_MASK;
	size_t page_count = PFN_UP(offset + size);
	struct tagged_addr *page_array;
	struct page **pages;
	void *cpu_addr = NULL;
	pgprot_t prot;
	size_t i;

	if (!size || !map)
		return NULL;

	/* check if page_count calculation will wrap */
	if (size > ((size_t)-1 / PAGE_SIZE))
		return NULL;

	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);
	if (!reg || (reg->flags & KBASE_REG_FREE))
		goto out_unlock;

	page_index = (gpu_addr >> PAGE_SHIFT) - reg->start_pfn;

	/* check if page_index + page_count will wrap */
	if (-1UL - page_count < page_index)
		goto out_unlock;

	if (page_index + page_count > kbase_reg_current_backed_size(reg))
		goto out_unlock;

	if (reg->flags & KBASE_REG_DONT_NEED)
		goto out_unlock;

	/* check access permissions can be satisfied
	 * Intended only for checking KBASE_REG_{CPU,GPU}_{RD,WR} */
	if ((reg->flags & prot_request) != prot_request)
		goto out_unlock;

	page_array = kbase_get_cpu_phy_pages(reg);
	if (!page_array)
		goto out_unlock;

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto out_unlock;

	for (i = 0; i < page_count; i++)
		pages[i] = phys_to_page(as_phys_addr_t(page_array[page_index +
								  i]));

	prot = PAGE_KERNEL;
	if (!(reg->flags & KBASE_REG_CPU_CACHED)) {
		/* Map uncached */
		prot = pgprot_writecombine(prot);
	}
	/* Note: enforcing a RO prot_request onto prot is not done, since:
	 * - CPU-arch-specific integration required
	 * - kbase_vmap() requires no access checks to be made/enforced */

	cpu_addr = vmap(pages, page_count, VM_MAP, prot);

	kfree(pages);

	if (!cpu_addr)
		goto out_unlock;

	map->gpu_addr = gpu_addr;
	map->cpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);
	map->cpu_pages = &kbase_get_cpu_phy_pages(reg)[page_index];
	map->gpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);
	map->gpu_pages = &kbase_get_gpu_phy_pages(reg)[page_index];
	map->addr = (void *)((uintptr_t)cpu_addr + offset);
	map->size = size;
	map->sync_needed = ((reg->flags & KBASE_REG_CPU_CACHED) != 0) &&
		!kbase_mem_is_imported(map->gpu_alloc->type);

	if (map->sync_needed)
		kbasep_sync_mem_regions(kctx, map, KBASE_SYNC_TO_CPU);
	kbase_gpu_vm_unlock(kctx);

	return map->addr;

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return NULL;
}

void *kbase_vmap(struct kbase_context *kctx, u64 gpu_addr, size_t size,
		struct kbase_vmap_struct *map)
{
	/* 0 is specified for prot_request to indicate no access checks should
	 * be made.
	 *
	 * As mentioned in kbase_vmap_prot() this means that a kernel-side
	 * CPU-RO mapping is not enforced to allow this to work */
	return kbase_vmap_prot(kctx, gpu_addr, size, 0u, map);
}
KBASE_EXPORT_TEST_API(kbase_vmap);

void kbase_vunmap(struct kbase_context *kctx, struct kbase_vmap_struct *map)
{
	void *addr = (void *)((uintptr_t)map->addr & PAGE_MASK);
	vunmap(addr);

	if (map->sync_needed)
		kbasep_sync_mem_regions(kctx, map, KBASE_SYNC_TO_DEVICE);
	map->gpu_addr = 0;
	map->cpu_alloc = kbase_mem_phy_alloc_put(map->cpu_alloc);
	map->gpu_alloc = kbase_mem_phy_alloc_put(map->gpu_alloc);
	map->cpu_pages = NULL;
	map->gpu_pages = NULL;
	map->addr = NULL;
	map->size = 0;
	map->sync_needed = false;
}
KBASE_EXPORT_TEST_API(kbase_vunmap);

void kbasep_os_process_page_usage_update(struct kbase_context *kctx, int pages)
{
	struct mm_struct *mm;

	rcu_read_lock();
	mm = rcu_dereference(kctx->process_mm);
	if (mm) {
		atomic_add(pages, &kctx->nonmapped_pages);
#ifdef SPLIT_RSS_COUNTING
		add_mm_counter(mm, MM_FILEPAGES, pages);
#else
		spin_lock(&mm->page_table_lock);
		add_mm_counter(mm, MM_FILEPAGES, pages);
		spin_unlock(&mm->page_table_lock);
#endif
	}
	rcu_read_unlock();
}

static void kbasep_os_process_page_usage_drain(struct kbase_context *kctx)
{
	int pages;
	struct mm_struct *mm;

	spin_lock(&kctx->mm_update_lock);
	mm = rcu_dereference_protected(kctx->process_mm, lockdep_is_held(&kctx->mm_update_lock));
	if (!mm) {
		spin_unlock(&kctx->mm_update_lock);
		return;
	}

	rcu_assign_pointer(kctx->process_mm, NULL);
	spin_unlock(&kctx->mm_update_lock);
	synchronize_rcu();

	pages = atomic_xchg(&kctx->nonmapped_pages, 0);
#ifdef SPLIT_RSS_COUNTING
	add_mm_counter(mm, MM_FILEPAGES, -pages);
#else
	spin_lock(&mm->page_table_lock);
	add_mm_counter(mm, MM_FILEPAGES, -pages);
	spin_unlock(&mm->page_table_lock);
#endif
}

static void kbase_special_vm_close(struct vm_area_struct *vma)
{
	struct kbase_context *kctx;

	kctx = vma->vm_private_data;
	kbasep_os_process_page_usage_drain(kctx);
}

static const struct vm_operations_struct kbase_vm_special_ops = {
	.close = kbase_special_vm_close,
};

static int kbase_tracking_page_setup(struct kbase_context *kctx, struct vm_area_struct *vma)
{
	/* check that this is the only tracking page */
	spin_lock(&kctx->mm_update_lock);
	if (rcu_dereference_protected(kctx->process_mm, lockdep_is_held(&kctx->mm_update_lock))) {
		spin_unlock(&kctx->mm_update_lock);
		return -EFAULT;
	}

	rcu_assign_pointer(kctx->process_mm, current->mm);

	spin_unlock(&kctx->mm_update_lock);

	/* no real access */
	vma->vm_flags &= ~(VM_READ | VM_MAYREAD | VM_WRITE | VM_MAYWRITE | VM_EXEC | VM_MAYEXEC);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP | VM_IO;
#else
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED | VM_IO;
#endif
	vma->vm_ops = &kbase_vm_special_ops;
	vma->vm_private_data = kctx;

	return 0;
}
void *kbase_va_alloc(struct kbase_context *kctx, u32 size, struct kbase_hwc_dma_mapping *handle)
{
	int res;
	void *va;
	dma_addr_t  dma_pa;
	struct kbase_va_region *reg;
	struct tagged_addr *page_array;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	unsigned long attrs = DMA_ATTR_WRITE_COMBINE;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	DEFINE_DMA_ATTRS(attrs);
#endif

	u32 pages = ((size - 1) >> PAGE_SHIFT) + 1;
	u32 flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
		    BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
	u32 i;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(0 != size);
	KBASE_DEBUG_ASSERT(0 != pages);

	if (size == 0)
		goto err;

	/* All the alloc calls return zeroed memory */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	va = dma_alloc_attrs(kctx->kbdev->dev, size, &dma_pa, GFP_KERNEL,
			     attrs);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	va = dma_alloc_attrs(kctx->kbdev->dev, size, &dma_pa, GFP_KERNEL,
			     &attrs);
#else
	va = dma_alloc_writecombine(kctx->kbdev->dev, size, &dma_pa, GFP_KERNEL);
#endif
	if (!va)
		goto err;

	/* Store the state so we can free it later. */
	handle->cpu_va = va;
	handle->dma_pa = dma_pa;
	handle->size   = size;


	reg = kbase_alloc_free_region(kctx, 0, pages, KBASE_REG_ZONE_SAME_VA);
	if (!reg)
		goto no_reg;

	reg->flags &= ~KBASE_REG_FREE;
	if (kbase_update_region_flags(kctx, reg, flags) != 0)
		goto invalid_flags;

	reg->cpu_alloc = kbase_alloc_create(pages, KBASE_MEM_TYPE_RAW);
	if (IS_ERR_OR_NULL(reg->cpu_alloc))
		goto no_alloc;

	reg->gpu_alloc = kbase_mem_phy_alloc_get(reg->cpu_alloc);

	page_array = kbase_get_cpu_phy_pages(reg);

	for (i = 0; i < pages; i++)
		page_array[i] = as_tagged(dma_pa + (i << PAGE_SHIFT));

	reg->cpu_alloc->nents = pages;

	kbase_gpu_vm_lock(kctx);
	res = kbase_gpu_mmap(kctx, reg, (uintptr_t) va, pages, 1);
	kbase_gpu_vm_unlock(kctx);
	if (res)
		goto no_mmap;

	return va;

no_mmap:
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
no_alloc:
invalid_flags:
	kfree(reg);
no_reg:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	dma_free_attrs(kctx->kbdev->dev, size, va, dma_pa, attrs);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	dma_free_attrs(kctx->kbdev->dev, size, va, dma_pa, &attrs);
#else
	dma_free_writecombine(kctx->kbdev->dev, size, va, dma_pa);
#endif
err:
	return NULL;
}
KBASE_EXPORT_SYMBOL(kbase_va_alloc);

void kbase_va_free(struct kbase_context *kctx, struct kbase_hwc_dma_mapping *handle)
{
	struct kbase_va_region *reg;
	int err;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0))
	DEFINE_DMA_ATTRS(attrs);
#endif

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(handle->cpu_va != NULL);

	kbase_gpu_vm_lock(kctx);
	reg = kbase_region_tracker_find_region_base_address(kctx, (uintptr_t)handle->cpu_va);
	KBASE_DEBUG_ASSERT(reg);
	err = kbase_gpu_munmap(kctx, reg);
	kbase_gpu_vm_unlock(kctx);
	KBASE_DEBUG_ASSERT(!err);

	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
	kfree(reg);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	dma_free_attrs(kctx->kbdev->dev, handle->size,
		       handle->cpu_va, handle->dma_pa, DMA_ATTR_WRITE_COMBINE);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	dma_free_attrs(kctx->kbdev->dev, handle->size,
			handle->cpu_va, handle->dma_pa, &attrs);
#else
	dma_free_writecombine(kctx->kbdev->dev, handle->size,
				handle->cpu_va, handle->dma_pa);
#endif
}
KBASE_EXPORT_SYMBOL(kbase_va_free);

