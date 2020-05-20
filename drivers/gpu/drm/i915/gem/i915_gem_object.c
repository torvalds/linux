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

#include <linux/sched/mm.h>

#include "display/intel_frontbuffer.h"
#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "i915_gem_context.h"
#include "i915_gem_mman.h"
#include "i915_gem_object.h"
#include "i915_globals.h"
#include "i915_trace.h"

static struct i915_global_object {
	struct i915_global base;
	struct kmem_cache *slab_objects;
} global;

struct drm_i915_gem_object *i915_gem_object_alloc(void)
{
	return kmem_cache_zalloc(global.slab_objects, GFP_KERNEL);
}

void i915_gem_object_free(struct drm_i915_gem_object *obj)
{
	return kmem_cache_free(global.slab_objects, obj);
}

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops,
			  struct lock_class_key *key)
{
	__mutex_init(&obj->mm.lock, "obj->mm.lock", key);

	spin_lock_init(&obj->vma.lock);
	INIT_LIST_HEAD(&obj->vma.list);

	INIT_LIST_HEAD(&obj->mm.link);

	INIT_LIST_HEAD(&obj->lut_list);

	spin_lock_init(&obj->mmo.lock);
	obj->mmo.offsets = RB_ROOT;

	init_rcu_head(&obj->rcu);

	obj->ops = ops;

	obj->mm.madv = I915_MADV_WILLNEED;
	INIT_RADIX_TREE(&obj->mm.get_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->mm.get_page.lock);
}

/**
 * Mark up the object's coherency levels for a given cache_level
 * @obj: #drm_i915_gem_object
 * @cache_level: cache level
 */
void i915_gem_object_set_cache_coherency(struct drm_i915_gem_object *obj,
					 unsigned int cache_level)
{
	obj->cache_level = cache_level;

	if (cache_level != I915_CACHE_NONE)
		obj->cache_coherent = (I915_BO_CACHE_COHERENT_FOR_READ |
				       I915_BO_CACHE_COHERENT_FOR_WRITE);
	else if (HAS_LLC(to_i915(obj->base.dev)))
		obj->cache_coherent = I915_BO_CACHE_COHERENT_FOR_READ;
	else
		obj->cache_coherent = 0;

	obj->cache_dirty =
		!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE);
}

void i915_gem_close_object(struct drm_gem_object *gem, struct drm_file *file)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem);
	struct drm_i915_file_private *fpriv = file->driver_priv;
	struct i915_mmap_offset *mmo, *mn;
	struct i915_lut_handle *lut, *ln;
	LIST_HEAD(close);

	i915_gem_object_lock(obj);
	list_for_each_entry_safe(lut, ln, &obj->lut_list, obj_link) {
		struct i915_gem_context *ctx = lut->ctx;

		if (ctx->file_priv != fpriv)
			continue;

		i915_gem_context_get(ctx);
		list_move(&lut->obj_link, &close);
	}
	i915_gem_object_unlock(obj);

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

		mutex_lock(&ctx->mutex);
		vma = radix_tree_delete(&ctx->handles_vma, lut->handle);
		if (vma) {
			GEM_BUG_ON(vma->obj != obj);
			GEM_BUG_ON(!atomic_read(&vma->open_count));
			i915_vma_close(vma);
		}
		mutex_unlock(&ctx->mutex);

		i915_gem_context_put(lut->ctx);
		i915_lut_handle_free(lut);
		i915_gem_object_put(obj);
	}
}

static void __i915_gem_free_object_rcu(struct rcu_head *head)
{
	struct drm_i915_gem_object *obj =
		container_of(head, typeof(*obj), rcu);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	dma_resv_fini(&obj->base._resv);
	i915_gem_object_free(obj);

	GEM_BUG_ON(!atomic_read(&i915->mm.free_count));
	atomic_dec(&i915->mm.free_count);
}

static void __i915_gem_free_objects(struct drm_i915_private *i915,
				    struct llist_node *freed)
{
	struct drm_i915_gem_object *obj, *on;

	llist_for_each_entry_safe(obj, on, freed, freed) {
		struct i915_mmap_offset *mmo, *mn;

		trace_i915_gem_object_destroy(obj);

		if (!list_empty(&obj->vma.list)) {
			struct i915_vma *vma;

			/*
			 * Note that the vma keeps an object reference while
			 * it is active, so it *should* not sleep while we
			 * destroy it. Our debug code errs insits it *might*.
			 * For the moment, play along.
			 */
			spin_lock(&obj->vma.lock);
			while ((vma = list_first_entry_or_null(&obj->vma.list,
							       struct i915_vma,
							       obj_link))) {
				GEM_BUG_ON(vma->obj != obj);
				spin_unlock(&obj->vma.lock);

				__i915_vma_put(vma);

				spin_lock(&obj->vma.lock);
			}
			spin_unlock(&obj->vma.lock);
		}

		i915_gem_object_release_mmap(obj);

		rbtree_postorder_for_each_entry_safe(mmo, mn,
						     &obj->mmo.offsets,
						     offset) {
			drm_vma_offset_remove(obj->base.dev->vma_offset_manager,
					      &mmo->vma_node);
			kfree(mmo);
		}
		obj->mmo.offsets = RB_ROOT;

		GEM_BUG_ON(obj->userfault_count);
		GEM_BUG_ON(!list_empty(&obj->lut_list));

		atomic_set(&obj->mm.pages_pin_count, 0);
		__i915_gem_object_put_pages(obj);
		GEM_BUG_ON(i915_gem_object_has_pages(obj));
		bitmap_free(obj->bit_17);

		if (obj->base.import_attach)
			drm_prime_gem_destroy(&obj->base, NULL);

		drm_gem_free_mmap_offset(&obj->base);

		if (obj->ops->release)
			obj->ops->release(obj);

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

void i915_gem_free_object(struct drm_gem_object *gem_obj)
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
	 * This serializes freeing with the shrinker. Since the free
	 * is delayed, first by RCU then by the workqueue, we want the
	 * shrinker to be able to free pages of unreferenced objects,
	 * or else we may oom whilst there are plenty of deferred
	 * freed objects.
	 */
	i915_gem_object_make_unshrinkable(obj);

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

static bool gpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	return !(obj->cache_level == I915_CACHE_NONE ||
		 obj->cache_level == I915_CACHE_WT);
}

void
i915_gem_object_flush_write_domain(struct drm_i915_gem_object *obj,
				   unsigned int flush_domains)
{
	struct i915_vma *vma;

	assert_object_held(obj);

	if (!(obj->write_domain & flush_domains))
		return;

	switch (obj->write_domain) {
	case I915_GEM_DOMAIN_GTT:
		spin_lock(&obj->vma.lock);
		for_each_ggtt_vma(vma, obj) {
			if (i915_vma_unset_ggtt_write(vma))
				intel_gt_flush_ggtt_writes(vma->vm->gt);
		}
		spin_unlock(&obj->vma.lock);

		i915_gem_object_flush_frontbuffer(obj, ORIGIN_CPU);
		break;

	case I915_GEM_DOMAIN_WC:
		wmb();
		break;

	case I915_GEM_DOMAIN_CPU:
		i915_gem_clflush_object(obj, I915_CLFLUSH_SYNC);
		break;

	case I915_GEM_DOMAIN_RENDER:
		if (gpu_write_needs_clflush(obj))
			obj->cache_dirty = true;
		break;
	}

	obj->write_domain = 0;
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

void i915_gem_init__objects(struct drm_i915_private *i915)
{
	INIT_WORK(&i915->mm.free_work, __i915_gem_free_work);
}

static void i915_global_objects_shrink(void)
{
	kmem_cache_shrink(global.slab_objects);
}

static void i915_global_objects_exit(void)
{
	kmem_cache_destroy(global.slab_objects);
}

static struct i915_global_object global = { {
	.shrink = i915_global_objects_shrink,
	.exit = i915_global_objects_exit,
} };

int __init i915_global_objects_init(void)
{
	global.slab_objects =
		KMEM_CACHE(drm_i915_gem_object, SLAB_HWCACHE_ALIGN);
	if (!global.slab_objects)
		return -ENOMEM;

	i915_global_register(&global.base);
	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/huge_gem_object.c"
#include "selftests/huge_pages.c"
#include "selftests/i915_gem_object.c"
#include "selftests/i915_gem_coherency.c"
#endif
