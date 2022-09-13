/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/highmem.h>
#include <linux/sched/mm.h>

#include <drm/drm_cache.h>

#include "display/intel_frontbuffer.h"
#include "pxp/intel_pxp.h"

#include "i915_drv.h"
#include "i915_file_private.h"
#include "i915_gem_clflush.h"
#include "i915_gem_context.h"
#include "i915_gem_dmabuf.h"
#include "i915_gem_mman.h"
#include "i915_gem_object.h"
#include "i915_gem_ttm.h"
#include "i915_memcpy.h"
#include "i915_trace.h"

static struct kmem_cache *slab_objects;

static const struct drm_gem_object_funcs i915_gem_object_funcs;

struct drm_i915_gem_object *i915_gem_object_alloc(void)
{
	struct drm_i915_gem_object *obj;

	obj = kmem_cache_zalloc(slab_objects, GFP_KERNEL);
	if (!obj)
		return NULL;
	obj->base.funcs = &i915_gem_object_funcs;

	return obj;
}

void i915_gem_object_free(struct drm_i915_gem_object *obj)
{
	return kmem_cache_free(slab_objects, obj);
}

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops,
			  struct lock_class_key *key, unsigned flags)
{
	/*
	 * A gem object is embedded both in a struct ttm_buffer_object :/ and
	 * in a drm_i915_gem_object. Make sure they are aliased.
	 */
	BUILD_BUG_ON(offsetof(typeof(*obj), base) !=
		     offsetof(typeof(*obj), __do_not_access.base));

	spin_lock_init(&obj->vma.lock);
	INIT_LIST_HEAD(&obj->vma.list);

	INIT_LIST_HEAD(&obj->mm.link);

	INIT_LIST_HEAD(&obj->lut_list);
	spin_lock_init(&obj->lut_lock);

	spin_lock_init(&obj->mmo.lock);
	obj->mmo.offsets = RB_ROOT;

	init_rcu_head(&obj->rcu);

	obj->ops = ops;
	GEM_BUG_ON(flags & ~I915_BO_ALLOC_FLAGS);
	obj->flags = flags;

	obj->mm.madv = I915_MADV_WILLNEED;
	INIT_RADIX_TREE(&obj->mm.get_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->mm.get_page.lock);
	INIT_RADIX_TREE(&obj->mm.get_dma_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->mm.get_dma_page.lock);
}

/**
 * __i915_gem_object_fini - Clean up a GEM object initialization
 * @obj: The gem object to cleanup
 *
 * This function cleans up gem object fields that are set up by
 * drm_gem_private_object_init() and i915_gem_object_init().
 * It's primarily intended as a helper for backends that need to
 * clean up the gem object in separate steps.
 */
void __i915_gem_object_fini(struct drm_i915_gem_object *obj)
{
	mutex_destroy(&obj->mm.get_page.lock);
	mutex_destroy(&obj->mm.get_dma_page.lock);
	dma_resv_fini(&obj->base._resv);
}

/**
 * i915_gem_object_set_cache_coherency - Mark up the object's coherency levels
 * for a given cache_level
 * @obj: #drm_i915_gem_object
 * @cache_level: cache level
 */
void i915_gem_object_set_cache_coherency(struct drm_i915_gem_object *obj,
					 unsigned int cache_level)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	obj->cache_level = cache_level;

	if (cache_level != I915_CACHE_NONE)
		obj->cache_coherent = (I915_BO_CACHE_COHERENT_FOR_READ |
				       I915_BO_CACHE_COHERENT_FOR_WRITE);
	else if (HAS_LLC(i915))
		obj->cache_coherent = I915_BO_CACHE_COHERENT_FOR_READ;
	else
		obj->cache_coherent = 0;

	obj->cache_dirty =
		!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE) &&
		!IS_DGFX(i915);
}

bool i915_gem_object_can_bypass_llc(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	/*
	 * This is purely from a security perspective, so we simply don't care
	 * about non-userspace objects being able to bypass the LLC.
	 */
	if (!(obj->flags & I915_BO_ALLOC_USER))
		return false;

	/*
	 * EHL and JSL add the 'Bypass LLC' MOCS entry, which should make it
	 * possible for userspace to bypass the GTT caching bits set by the
	 * kernel, as per the given object cache_level. This is troublesome
	 * since the heavy flush we apply when first gathering the pages is
	 * skipped if the kernel thinks the object is coherent with the GPU. As
	 * a result it might be possible to bypass the cache and read the
	 * contents of the page directly, which could be stale data. If it's
	 * just a case of userspace shooting themselves in the foot then so be
	 * it, but since i915 takes the stance of always zeroing memory before
	 * handing it to userspace, we need to prevent this.
	 */
	return IS_JSL_EHL(i915);
}

static void i915_gem_close_object(struct drm_gem_object *gem, struct drm_file *file)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem);
	struct drm_i915_file_private *fpriv = file->driver_priv;
	struct i915_lut_handle bookmark = {};
	struct i915_mmap_offset *mmo, *mn;
	struct i915_lut_handle *lut, *ln;
	LIST_HEAD(close);

	spin_lock(&obj->lut_lock);
	list_for_each_entry_safe(lut, ln, &obj->lut_list, obj_link) {
		struct i915_gem_context *ctx = lut->ctx;

		if (ctx && ctx->file_priv == fpriv) {
			i915_gem_context_get(ctx);
			list_move(&lut->obj_link, &close);
		}

		/* Break long locks, and carefully continue on from this spot */
		if (&ln->obj_link != &obj->lut_list) {
			list_add_tail(&bookmark.obj_link, &ln->obj_link);
			if (cond_resched_lock(&obj->lut_lock))
				list_safe_reset_next(&bookmark, ln, obj_link);
			__list_del_entry(&bookmark.obj_link);
		}
	}
	spin_unlock(&obj->lut_lock);

	spin_lock(&obj->mmo.lock);
	rbtree_postorder_for_each_entry_safe(mmo, mn, &obj->mmo.offsets, offset)
		drm_vma_node_revoke(&mmo->vma_node, file);
	spin_unlock(&obj->mmo.lock);

	list_for_each_entry_safe(lut, ln, &close, obj_link) {
		struct i915_gem_context *ctx = lut->ctx;
		struct i915_vma *vma;

		/*
		 * We allow the process to have multiple handles to the same
		 * vma, in the same fd namespace, by virtue of flink/open.
		 */

		mutex_lock(&ctx->lut_mutex);
		vma = radix_tree_delete(&ctx->handles_vma, lut->handle);
		if (vma) {
			GEM_BUG_ON(vma->obj != obj);
			GEM_BUG_ON(!atomic_read(&vma->open_count));
			i915_vma_close(vma);
		}
		mutex_unlock(&ctx->lut_mutex);

		i915_gem_context_put(lut->ctx);
		i915_lut_handle_free(lut);
		i915_gem_object_put(obj);
	}
}

void __i915_gem_free_object_rcu(struct rcu_head *head)
{
	struct drm_i915_gem_object *obj =
		container_of(head, typeof(*obj), rcu);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	i915_gem_object_free(obj);

	GEM_BUG_ON(!atomic_read(&i915->mm.free_count));
	atomic_dec(&i915->mm.free_count);
}

static void __i915_gem_object_free_mmaps(struct drm_i915_gem_object *obj)
{
	/* Skip serialisation and waking the device if known to be not used. */

	if (obj->userfault_count && !IS_DGFX(to_i915(obj->base.dev)))
		i915_gem_object_release_mmap_gtt(obj);

	if (!RB_EMPTY_ROOT(&obj->mmo.offsets)) {
		struct i915_mmap_offset *mmo, *mn;

		i915_gem_object_release_mmap_offset(obj);

		rbtree_postorder_for_each_entry_safe(mmo, mn,
						     &obj->mmo.offsets,
						     offset) {
			drm_vma_offset_remove(obj->base.dev->vma_offset_manager,
					      &mmo->vma_node);
			kfree(mmo);
		}
		obj->mmo.offsets = RB_ROOT;
	}
}

/**
 * __i915_gem_object_pages_fini - Clean up pages use of a gem object
 * @obj: The gem object to clean up
 *
 * This function cleans up usage of the object mm.pages member. It
 * is intended for backends that need to clean up a gem object in
 * separate steps and needs to be called when the object is idle before
 * the object's backing memory is freed.
 */
void __i915_gem_object_pages_fini(struct drm_i915_gem_object *obj)
{
	assert_object_held_shared(obj);

	if (!list_empty(&obj->vma.list)) {
		struct i915_vma *vma;

		spin_lock(&obj->vma.lock);
		while ((vma = list_first_entry_or_null(&obj->vma.list,
						       struct i915_vma,
						       obj_link))) {
			GEM_BUG_ON(vma->obj != obj);
			spin_unlock(&obj->vma.lock);

			i915_vma_destroy(vma);

			spin_lock(&obj->vma.lock);
		}
		spin_unlock(&obj->vma.lock);
	}

	__i915_gem_object_free_mmaps(obj);

	atomic_set(&obj->mm.pages_pin_count, 0);
	__i915_gem_object_put_pages(obj);
	GEM_BUG_ON(i915_gem_object_has_pages(obj));
}

void __i915_gem_free_object(struct drm_i915_gem_object *obj)
{
	trace_i915_gem_object_destroy(obj);

	GEM_BUG_ON(!list_empty(&obj->lut_list));

	bitmap_free(obj->bit_17);

	if (obj->base.import_attach)
		drm_prime_gem_destroy(&obj->base, NULL);

	drm_gem_free_mmap_offset(&obj->base);

	if (obj->ops->release)
		obj->ops->release(obj);

	if (obj->mm.n_placements > 1)
		kfree(obj->mm.placements);

	if (obj->shares_resv_from)
		i915_vm_resv_put(obj->shares_resv_from);

	__i915_gem_object_fini(obj);
}

static void __i915_gem_free_objects(struct drm_i915_private *i915,
				    struct llist_node *freed)
{
	struct drm_i915_gem_object *obj, *on;

	llist_for_each_entry_safe(obj, on, freed, freed) {
		might_sleep();
		if (obj->ops->delayed_free) {
			obj->ops->delayed_free(obj);
			continue;
		}

		__i915_gem_object_pages_fini(obj);
		__i915_gem_free_object(obj);

		/* But keep the pointer alive for RCU-protected lookups */
		call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
		cond_resched();
	}
}

void i915_gem_flush_free_objects(struct drm_i915_private *i915)
{
	struct llist_node *freed = llist_del_all(&i915->mm.free_list);

	if (unlikely(freed))
		__i915_gem_free_objects(i915, freed);
}

static void __i915_gem_free_work(struct work_struct *work)
{
	struct drm_i915_private *i915 =
		container_of(work, struct drm_i915_private, mm.free_work);

	i915_gem_flush_free_objects(i915);
}

static void i915_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	GEM_BUG_ON(i915_gem_object_is_framebuffer(obj));

	/*
	 * Before we free the object, make sure any pure RCU-only
	 * read-side critical sections are complete, e.g.
	 * i915_gem_busy_ioctl(). For the corresponding synchronized
	 * lookup see i915_gem_object_lookup_rcu().
	 */
	atomic_inc(&i915->mm.free_count);

	/*
	 * Since we require blocking on struct_mutex to unbind the freed
	 * object from the GPU before releasing resources back to the
	 * system, we can not do that directly from the RCU callback (which may
	 * be a softirq context), but must instead then defer that work onto a
	 * kthread. We use the RCU callback rather than move the freed object
	 * directly onto the work queue so that we can mix between using the
	 * worker and performing frees directly from subsequent allocations for
	 * crude but effective memory throttling.
	 */

	if (llist_add(&obj->freed, &i915->mm.free_list))
		queue_work(i915->wq, &i915->mm.free_work);
}

void __i915_gem_object_flush_frontbuffer(struct drm_i915_gem_object *obj,
					 enum fb_op_origin origin)
{
	struct intel_frontbuffer *front;

	front = __intel_frontbuffer_get(obj);
	if (front) {
		intel_frontbuffer_flush(front, origin);
		intel_frontbuffer_put(front);
	}
}

void __i915_gem_object_invalidate_frontbuffer(struct drm_i915_gem_object *obj,
					      enum fb_op_origin origin)
{
	struct intel_frontbuffer *front;

	front = __intel_frontbuffer_get(obj);
	if (front) {
		intel_frontbuffer_invalidate(front, origin);
		intel_frontbuffer_put(front);
	}
}

static void
i915_gem_object_read_from_page_kmap(struct drm_i915_gem_object *obj, u64 offset, void *dst, int size)
{
	void *src_map;
	void *src_ptr;

	src_map = kmap_atomic(i915_gem_object_get_page(obj, offset >> PAGE_SHIFT));

	src_ptr = src_map + offset_in_page(offset);
	if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
		drm_clflush_virt_range(src_ptr, size);
	memcpy(dst, src_ptr, size);

	kunmap_atomic(src_map);
}

static void
i915_gem_object_read_from_page_iomap(struct drm_i915_gem_object *obj, u64 offset, void *dst, int size)
{
	void __iomem *src_map;
	void __iomem *src_ptr;
	dma_addr_t dma = i915_gem_object_get_dma_address(obj, offset >> PAGE_SHIFT);

	src_map = io_mapping_map_wc(&obj->mm.region->iomap,
				    dma - obj->mm.region->region.start,
				    PAGE_SIZE);

	src_ptr = src_map + offset_in_page(offset);
	if (!i915_memcpy_from_wc(dst, (void __force *)src_ptr, size))
		memcpy_fromio(dst, src_ptr, size);

	io_mapping_unmap(src_map);
}

/**
 * i915_gem_object_read_from_page - read data from the page of a GEM object
 * @obj: GEM object to read from
 * @offset: offset within the object
 * @dst: buffer to store the read data
 * @size: size to read
 *
 * Reads data from @obj at the specified offset. The requested region to read
 * from can't cross a page boundary. The caller must ensure that @obj pages
 * are pinned and that @obj is synced wrt. any related writes.
 *
 * Return: %0 on success or -ENODEV if the type of @obj's backing store is
 * unsupported.
 */
int i915_gem_object_read_from_page(struct drm_i915_gem_object *obj, u64 offset, void *dst, int size)
{
	GEM_BUG_ON(offset >= obj->base.size);
	GEM_BUG_ON(offset_in_page(offset) > PAGE_SIZE - size);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	if (i915_gem_object_has_struct_page(obj))
		i915_gem_object_read_from_page_kmap(obj, offset, dst, size);
	else if (i915_gem_object_has_iomem(obj))
		i915_gem_object_read_from_page_iomap(obj, offset, dst, size);
	else
		return -ENODEV;

	return 0;
}

/**
 * i915_gem_object_evictable - Whether object is likely evictable after unbind.
 * @obj: The object to check
 *
 * This function checks whether the object is likely unvictable after unbind.
 * If the object is not locked when checking, the result is only advisory.
 * If the object is locked when checking, and the function returns true,
 * then an eviction should indeed be possible. But since unlocked vma
 * unpinning and unbinding is currently possible, the object can actually
 * become evictable even if this function returns false.
 *
 * Return: true if the object may be evictable. False otherwise.
 */
bool i915_gem_object_evictable(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;
	int pin_count = atomic_read(&obj->mm.pages_pin_count);

	if (!pin_count)
		return true;

	spin_lock(&obj->vma.lock);
	list_for_each_entry(vma, &obj->vma.list, obj_link) {
		if (i915_vma_is_pinned(vma)) {
			spin_unlock(&obj->vma.lock);
			return false;
		}
		if (atomic_read(&vma->pages_count))
			pin_count--;
	}
	spin_unlock(&obj->vma.lock);
	GEM_WARN_ON(pin_count < 0);

	return pin_count == 0;
}

/**
 * i915_gem_object_migratable - Whether the object is migratable out of the
 * current region.
 * @obj: Pointer to the object.
 *
 * Return: Whether the object is allowed to be resident in other
 * regions than the current while pages are present.
 */
bool i915_gem_object_migratable(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mr = READ_ONCE(obj->mm.region);

	if (!mr)
		return false;

	return obj->mm.n_placements > 1;
}

/**
 * i915_gem_object_has_struct_page - Whether the object is page-backed
 * @obj: The object to query.
 *
 * This function should only be called while the object is locked or pinned,
 * otherwise the page backing may change under the caller.
 *
 * Return: True if page-backed, false otherwise.
 */
bool i915_gem_object_has_struct_page(const struct drm_i915_gem_object *obj)
{
#ifdef CONFIG_LOCKDEP
	if (IS_DGFX(to_i915(obj->base.dev)) &&
	    i915_gem_object_evictable((void __force *)obj))
		assert_object_held_shared(obj);
#endif
	return obj->mem_flags & I915_BO_FLAG_STRUCT_PAGE;
}

/**
 * i915_gem_object_has_iomem - Whether the object is iomem-backed
 * @obj: The object to query.
 *
 * This function should only be called while the object is locked or pinned,
 * otherwise the iomem backing may change under the caller.
 *
 * Return: True if iomem-backed, false otherwise.
 */
bool i915_gem_object_has_iomem(const struct drm_i915_gem_object *obj)
{
#ifdef CONFIG_LOCKDEP
	if (IS_DGFX(to_i915(obj->base.dev)) &&
	    i915_gem_object_evictable((void __force *)obj))
		assert_object_held_shared(obj);
#endif
	return obj->mem_flags & I915_BO_FLAG_IOMEM;
}

/**
 * i915_gem_object_can_migrate - Whether an object likely can be migrated
 *
 * @obj: The object to migrate
 * @id: The region intended to migrate to
 *
 * Check whether the object backend supports migration to the
 * given region. Note that pinning may affect the ability to migrate as
 * returned by this function.
 *
 * This function is primarily intended as a helper for checking the
 * possibility to migrate objects and might be slightly less permissive
 * than i915_gem_object_migrate() when it comes to objects with the
 * I915_BO_ALLOC_USER flag set.
 *
 * Return: true if migration is possible, false otherwise.
 */
bool i915_gem_object_can_migrate(struct drm_i915_gem_object *obj,
				 enum intel_region_id id)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned int num_allowed = obj->mm.n_placements;
	struct intel_memory_region *mr;
	unsigned int i;

	GEM_BUG_ON(id >= INTEL_REGION_UNKNOWN);
	GEM_BUG_ON(obj->mm.madv != I915_MADV_WILLNEED);

	mr = i915->mm.regions[id];
	if (!mr)
		return false;

	if (!IS_ALIGNED(obj->base.size, mr->min_page_size))
		return false;

	if (obj->mm.region == mr)
		return true;

	if (!i915_gem_object_evictable(obj))
		return false;

	if (!obj->ops->migrate)
		return false;

	if (!(obj->flags & I915_BO_ALLOC_USER))
		return true;

	if (num_allowed == 0)
		return false;

	for (i = 0; i < num_allowed; ++i) {
		if (mr == obj->mm.placements[i])
			return true;
	}

	return false;
}

/**
 * i915_gem_object_migrate - Migrate an object to the desired region id
 * @obj: The object to migrate.
 * @ww: An optional struct i915_gem_ww_ctx. If NULL, the backend may
 * not be successful in evicting other objects to make room for this object.
 * @id: The region id to migrate to.
 *
 * Attempt to migrate the object to the desired memory region. The
 * object backend must support migration and the object may not be
 * pinned, (explicitly pinned pages or pinned vmas). The object must
 * be locked.
 * On successful completion, the object will have pages pointing to
 * memory in the new region, but an async migration task may not have
 * completed yet, and to accomplish that, i915_gem_object_wait_migration()
 * must be called.
 *
 * Note: the @ww parameter is not used yet, but included to make sure
 * callers put some effort into obtaining a valid ww ctx if one is
 * available.
 *
 * Return: 0 on success. Negative error code on failure. In particular may
 * return -ENXIO on lack of region space, -EDEADLK for deadlock avoidance
 * if @ww is set, -EINTR or -ERESTARTSYS if signal pending, and
 * -EBUSY if the object is pinned.
 */
int i915_gem_object_migrate(struct drm_i915_gem_object *obj,
			    struct i915_gem_ww_ctx *ww,
			    enum intel_region_id id)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_memory_region *mr;

	GEM_BUG_ON(id >= INTEL_REGION_UNKNOWN);
	GEM_BUG_ON(obj->mm.madv != I915_MADV_WILLNEED);
	assert_object_held(obj);

	mr = i915->mm.regions[id];
	GEM_BUG_ON(!mr);

	if (!i915_gem_object_can_migrate(obj, id))
		return -EINVAL;

	if (!obj->ops->migrate) {
		if (GEM_WARN_ON(obj->mm.region != mr))
			return -EINVAL;
		return 0;
	}

	return obj->ops->migrate(obj, mr);
}

/**
 * i915_gem_object_placement_possible - Check whether the object can be
 * placed at certain memory type
 * @obj: Pointer to the object
 * @type: The memory type to check
 *
 * Return: True if the object can be placed in @type. False otherwise.
 */
bool i915_gem_object_placement_possible(struct drm_i915_gem_object *obj,
					enum intel_memory_type type)
{
	unsigned int i;

	if (!obj->mm.n_placements) {
		switch (type) {
		case INTEL_MEMORY_LOCAL:
			return i915_gem_object_has_iomem(obj);
		case INTEL_MEMORY_SYSTEM:
			return i915_gem_object_has_pages(obj);
		default:
			/* Ignore stolen for now */
			GEM_BUG_ON(1);
			return false;
		}
	}

	for (i = 0; i < obj->mm.n_placements; i++) {
		if (obj->mm.placements[i]->type == type)
			return true;
	}

	return false;
}

/**
 * i915_gem_object_needs_ccs_pages - Check whether the object requires extra
 * pages when placed in system-memory, in order to save and later restore the
 * flat-CCS aux state when the object is moved between local-memory and
 * system-memory
 * @obj: Pointer to the object
 *
 * Return: True if the object needs extra ccs pages. False otherwise.
 */
bool i915_gem_object_needs_ccs_pages(struct drm_i915_gem_object *obj)
{
	bool lmem_placement = false;
	int i;

	if (!HAS_FLAT_CCS(to_i915(obj->base.dev)))
		return false;

	for (i = 0; i < obj->mm.n_placements; i++) {
		/* Compression is not allowed for the objects with smem placement */
		if (obj->mm.placements[i]->type == INTEL_MEMORY_SYSTEM)
			return false;
		if (!lmem_placement &&
		    obj->mm.placements[i]->type == INTEL_MEMORY_LOCAL)
			lmem_placement = true;
	}

	return lmem_placement;
}

void i915_gem_init__objects(struct drm_i915_private *i915)
{
	INIT_WORK(&i915->mm.free_work, __i915_gem_free_work);
}

void i915_objects_module_exit(void)
{
	kmem_cache_destroy(slab_objects);
}

int __init i915_objects_module_init(void)
{
	slab_objects = KMEM_CACHE(drm_i915_gem_object, SLAB_HWCACHE_ALIGN);
	if (!slab_objects)
		return -ENOMEM;

	return 0;
}

static const struct drm_gem_object_funcs i915_gem_object_funcs = {
	.free = i915_gem_free_object,
	.close = i915_gem_close_object,
	.export = i915_gem_prime_export,
};

/**
 * i915_gem_object_get_moving_fence - Get the object's moving fence if any
 * @obj: The object whose moving fence to get.
 * @fence: The resulting fence
 *
 * A non-signaled moving fence means that there is an async operation
 * pending on the object that needs to be waited on before setting up
 * any GPU- or CPU PTEs to the object's pages.
 *
 * Return: Negative error code or 0 for success.
 */
int i915_gem_object_get_moving_fence(struct drm_i915_gem_object *obj,
				     struct dma_fence **fence)
{
	return dma_resv_get_singleton(obj->base.resv, DMA_RESV_USAGE_KERNEL,
				      fence);
}

/**
 * i915_gem_object_wait_moving_fence - Wait for the object's moving fence if any
 * @obj: The object whose moving fence to wait for.
 * @intr: Whether to wait interruptible.
 *
 * If the moving fence signaled without an error, it is detached from the
 * object and put.
 *
 * Return: 0 if successful, -ERESTARTSYS if the wait was interrupted,
 * negative error code if the async operation represented by the
 * moving fence failed.
 */
int i915_gem_object_wait_moving_fence(struct drm_i915_gem_object *obj,
				      bool intr)
{
	long ret;

	assert_object_held(obj);

	ret = dma_resv_wait_timeout(obj->base. resv, DMA_RESV_USAGE_KERNEL,
				    intr, MAX_SCHEDULE_TIMEOUT);
	if (!ret)
		ret = -ETIME;
	else if (ret > 0 && i915_gem_object_has_unknown_state(obj))
		ret = -EIO;

	return ret < 0 ? ret : 0;
}

/**
 * i915_gem_object_has_unknown_state - Return true if the object backing pages are
 * in an unknown_state. This means that userspace must NEVER be allowed to touch
 * the pages, with either the GPU or CPU.
 *
 * ONLY valid to be called after ensuring that all kernel fences have signalled
 * (in particular the fence for moving/clearing the object).
 */
bool i915_gem_object_has_unknown_state(struct drm_i915_gem_object *obj)
{
	/*
	 * The below barrier pairs with the dma_fence_signal() in
	 * __memcpy_work(). We should only sample the unknown_state after all
	 * the kernel fences have signalled.
	 */
	smp_rmb();
	return obj->mm.unknown_state;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/huge_gem_object.c"
#include "selftests/huge_pages.c"
#include "selftests/i915_gem_migrate.c"
#include "selftests/i915_gem_object.c"
#include "selftests/i915_gem_coherency.c"
#endif
