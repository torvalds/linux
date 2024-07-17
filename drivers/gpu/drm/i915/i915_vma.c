/*
 * Copyright © 2016 Intel Corporation
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
#include <linux/dma-fence-array.h>
#include <drm/drm_gem.h>

#include "display/intel_display.h"
#include "display/intel_frontbuffer.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_object_frontbuffer.h"
#include "gem/i915_gem_tiling.h"
#include "gt/intel_engine.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_requests.h"
#include "gt/intel_tlb.h"

#include "i915_drv.h"
#include "i915_gem_evict.h"
#include "i915_sw_fence_work.h"
#include "i915_trace.h"
#include "i915_vma.h"
#include "i915_vma_resource.h"

static inline void assert_vma_held_evict(const struct i915_vma *vma)
{
	/*
	 * We may be forced to unbind when the vm is dead, to clean it up.
	 * This is the only exception to the requirement of the object lock
	 * being held.
	 */
	if (kref_read(&vma->vm->ref))
		assert_object_held_shared(vma->obj);
}

static struct kmem_cache *slab_vmas;

static struct i915_vma *i915_vma_alloc(void)
{
	return kmem_cache_zalloc(slab_vmas, GFP_KERNEL);
}

static void i915_vma_free(struct i915_vma *vma)
{
	return kmem_cache_free(slab_vmas, vma);
}

#if IS_ENABLED(CONFIG_DRM_I915_ERRLOG_GEM) && IS_ENABLED(CONFIG_DRM_DEBUG_MM)

#include <linux/stackdepot.h>

static void vma_print_allocator(struct i915_vma *vma, const char *reason)
{
	char buf[512];

	if (!vma->node.stack) {
		drm_dbg(vma->obj->base.dev,
			"vma.node [%08llx + %08llx] %s: unknown owner\n",
			vma->node.start, vma->node.size, reason);
		return;
	}

	stack_depot_snprint(vma->node.stack, buf, sizeof(buf), 0);
	drm_dbg(vma->obj->base.dev,
		"vma.node [%08llx + %08llx] %s: inserted at %s\n",
		vma->node.start, vma->node.size, reason, buf);
}

#else

static void vma_print_allocator(struct i915_vma *vma, const char *reason)
{
}

#endif

static inline struct i915_vma *active_to_vma(struct i915_active *ref)
{
	return container_of(ref, typeof(struct i915_vma), active);
}

static int __i915_vma_active(struct i915_active *ref)
{
	struct i915_vma *vma = active_to_vma(ref);

	if (!i915_vma_tryget(vma))
		return -ENOENT;

	/*
	 * Exclude global GTT VMA from holding a GT wakeref
	 * while active, otherwise GPU never goes idle.
	 */
	if (!i915_vma_is_ggtt(vma)) {
		/*
		 * Since we and our _retire() counterpart can be
		 * called asynchronously, storing a wakeref tracking
		 * handle inside struct i915_vma is not safe, and
		 * there is no other good place for that.  Hence,
		 * use untracked variants of intel_gt_pm_get/put().
		 */
		intel_gt_pm_get_untracked(vma->vm->gt);
	}

	return 0;
}

static void __i915_vma_retire(struct i915_active *ref)
{
	struct i915_vma *vma = active_to_vma(ref);

	if (!i915_vma_is_ggtt(vma)) {
		/*
		 * Since we can be called from atomic contexts,
		 * use an async variant of intel_gt_pm_put().
		 */
		intel_gt_pm_put_async_untracked(vma->vm->gt);
	}

	i915_vma_put(vma);
}

static struct i915_vma *
vma_create(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_gtt_view *view)
{
	struct i915_vma *pos = ERR_PTR(-E2BIG);
	struct i915_vma *vma;
	struct rb_node *rb, **p;
	int err;

	/* The aliasing_ppgtt should never be used directly! */
	GEM_BUG_ON(vm == &vm->gt->ggtt->alias->vm);

	vma = i915_vma_alloc();
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	vma->ops = &vm->vma_ops;
	vma->obj = obj;
	vma->size = obj->base.size;
	vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	i915_active_init(&vma->active, __i915_vma_active, __i915_vma_retire, 0);

	/* Declare ourselves safe for use inside shrinkers */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&vma->active.mutex);
		fs_reclaim_release(GFP_KERNEL);
	}

	INIT_LIST_HEAD(&vma->closed_link);
	INIT_LIST_HEAD(&vma->obj_link);
	RB_CLEAR_NODE(&vma->obj_node);

	if (view && view->type != I915_GTT_VIEW_NORMAL) {
		vma->gtt_view = *view;
		if (view->type == I915_GTT_VIEW_PARTIAL) {
			GEM_BUG_ON(range_overflows_t(u64,
						     view->partial.offset,
						     view->partial.size,
						     obj->base.size >> PAGE_SHIFT));
			vma->size = view->partial.size;
			vma->size <<= PAGE_SHIFT;
			GEM_BUG_ON(vma->size > obj->base.size);
		} else if (view->type == I915_GTT_VIEW_ROTATED) {
			vma->size = intel_rotation_info_size(&view->rotated);
			vma->size <<= PAGE_SHIFT;
		} else if (view->type == I915_GTT_VIEW_REMAPPED) {
			vma->size = intel_remapped_info_size(&view->remapped);
			vma->size <<= PAGE_SHIFT;
		}
	}

	if (unlikely(vma->size > vm->total))
		goto err_vma;

	GEM_BUG_ON(!IS_ALIGNED(vma->size, I915_GTT_PAGE_SIZE));

	err = mutex_lock_interruptible(&vm->mutex);
	if (err) {
		pos = ERR_PTR(err);
		goto err_vma;
	}

	vma->vm = vm;
	list_add_tail(&vma->vm_link, &vm->unbound_list);

	spin_lock(&obj->vma.lock);
	if (i915_is_ggtt(vm)) {
		if (unlikely(overflows_type(vma->size, u32)))
			goto err_unlock;

		vma->fence_size = i915_gem_fence_size(vm->i915, vma->size,
						      i915_gem_object_get_tiling(obj),
						      i915_gem_object_get_stride(obj));
		if (unlikely(vma->fence_size < vma->size || /* overflow */
			     vma->fence_size > vm->total))
			goto err_unlock;

		GEM_BUG_ON(!IS_ALIGNED(vma->fence_size, I915_GTT_MIN_ALIGNMENT));

		vma->fence_alignment = i915_gem_fence_alignment(vm->i915, vma->size,
								i915_gem_object_get_tiling(obj),
								i915_gem_object_get_stride(obj));
		GEM_BUG_ON(!is_power_of_2(vma->fence_alignment));

		__set_bit(I915_VMA_GGTT_BIT, __i915_vma_flags(vma));
	}

	rb = NULL;
	p = &obj->vma.tree.rb_node;
	while (*p) {
		long cmp;

		rb = *p;
		pos = rb_entry(rb, struct i915_vma, obj_node);

		/*
		 * If the view already exists in the tree, another thread
		 * already created a matching vma, so return the older instance
		 * and dispose of ours.
		 */
		cmp = i915_vma_compare(pos, vm, view);
		if (cmp < 0)
			p = &rb->rb_right;
		else if (cmp > 0)
			p = &rb->rb_left;
		else
			goto err_unlock;
	}
	rb_link_node(&vma->obj_node, rb, p);
	rb_insert_color(&vma->obj_node, &obj->vma.tree);

	if (i915_vma_is_ggtt(vma))
		/*
		 * We put the GGTT vma at the start of the vma-list, followed
		 * by the ppGGTT vma. This allows us to break early when
		 * iterating over only the GGTT vma for an object, see
		 * for_each_ggtt_vma()
		 */
		list_add(&vma->obj_link, &obj->vma.list);
	else
		list_add_tail(&vma->obj_link, &obj->vma.list);

	spin_unlock(&obj->vma.lock);
	mutex_unlock(&vm->mutex);

	return vma;

err_unlock:
	spin_unlock(&obj->vma.lock);
	list_del_init(&vma->vm_link);
	mutex_unlock(&vm->mutex);
err_vma:
	i915_vma_free(vma);
	return pos;
}

static struct i915_vma *
i915_vma_lookup(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_gtt_view *view)
{
	struct rb_node *rb;

	rb = obj->vma.tree.rb_node;
	while (rb) {
		struct i915_vma *vma = rb_entry(rb, struct i915_vma, obj_node);
		long cmp;

		cmp = i915_vma_compare(vma, vm, view);
		if (cmp == 0)
			return vma;

		if (cmp < 0)
			rb = rb->rb_right;
		else
			rb = rb->rb_left;
	}

	return NULL;
}

/**
 * i915_vma_instance - return the singleton instance of the VMA
 * @obj: parent &struct drm_i915_gem_object to be mapped
 * @vm: address space in which the mapping is located
 * @view: additional mapping requirements
 *
 * i915_vma_instance() looks up an existing VMA of the @obj in the @vm with
 * the same @view characteristics. If a match is not found, one is created.
 * Once created, the VMA is kept until either the object is freed, or the
 * address space is closed.
 *
 * Returns the vma, or an error pointer.
 */
struct i915_vma *
i915_vma_instance(struct drm_i915_gem_object *obj,
		  struct i915_address_space *vm,
		  const struct i915_gtt_view *view)
{
	struct i915_vma *vma;

	GEM_BUG_ON(view && !i915_is_ggtt_or_dpt(vm));
	GEM_BUG_ON(!kref_read(&vm->ref));

	spin_lock(&obj->vma.lock);
	vma = i915_vma_lookup(obj, vm, view);
	spin_unlock(&obj->vma.lock);

	/* vma_create() will resolve the race if another creates the vma */
	if (unlikely(!vma))
		vma = vma_create(obj, vm, view);

	GEM_BUG_ON(!IS_ERR(vma) && i915_vma_compare(vma, vm, view));
	return vma;
}

struct i915_vma_work {
	struct dma_fence_work base;
	struct i915_address_space *vm;
	struct i915_vm_pt_stash stash;
	struct i915_vma_resource *vma_res;
	struct drm_i915_gem_object *obj;
	struct i915_sw_dma_fence_cb cb;
	unsigned int pat_index;
	unsigned int flags;
};

static void __vma_bind(struct dma_fence_work *work)
{
	struct i915_vma_work *vw = container_of(work, typeof(*vw), base);
	struct i915_vma_resource *vma_res = vw->vma_res;

	/*
	 * We are about the bind the object, which must mean we have already
	 * signaled the work to potentially clear/move the pages underneath. If
	 * something went wrong at that stage then the object should have
	 * unknown_state set, in which case we need to skip the bind.
	 */
	if (i915_gem_object_has_unknown_state(vw->obj))
		return;

	vma_res->ops->bind_vma(vma_res->vm, &vw->stash,
			       vma_res, vw->pat_index, vw->flags);
}

static void __vma_release(struct dma_fence_work *work)
{
	struct i915_vma_work *vw = container_of(work, typeof(*vw), base);

	if (vw->obj)
		i915_gem_object_put(vw->obj);

	i915_vm_free_pt_stash(vw->vm, &vw->stash);
	if (vw->vma_res)
		i915_vma_resource_put(vw->vma_res);
}

static const struct dma_fence_work_ops bind_ops = {
	.name = "bind",
	.work = __vma_bind,
	.release = __vma_release,
};

struct i915_vma_work *i915_vma_work(void)
{
	struct i915_vma_work *vw;

	vw = kzalloc(sizeof(*vw), GFP_KERNEL);
	if (!vw)
		return NULL;

	dma_fence_work_init(&vw->base, &bind_ops);
	vw->base.dma.error = -EAGAIN; /* disable the worker by default */

	return vw;
}

int i915_vma_wait_for_bind(struct i915_vma *vma)
{
	int err = 0;

	if (rcu_access_pointer(vma->active.excl.fence)) {
		struct dma_fence *fence;

		rcu_read_lock();
		fence = dma_fence_get_rcu_safe(&vma->active.excl.fence);
		rcu_read_unlock();
		if (fence) {
			err = dma_fence_wait(fence, true);
			dma_fence_put(fence);
		}
	}

	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
static int i915_vma_verify_bind_complete(struct i915_vma *vma)
{
	struct dma_fence *fence = i915_active_fence_get(&vma->active.excl);
	int err;

	if (!fence)
		return 0;

	if (dma_fence_is_signaled(fence))
		err = fence->error;
	else
		err = -EBUSY;

	dma_fence_put(fence);

	return err;
}
#else
#define i915_vma_verify_bind_complete(_vma) 0
#endif

I915_SELFTEST_EXPORT void
i915_vma_resource_init_from_vma(struct i915_vma_resource *vma_res,
				struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;

	i915_vma_resource_init(vma_res, vma->vm, vma->pages, &vma->page_sizes,
			       obj->mm.rsgt, i915_gem_object_is_readonly(obj),
			       i915_gem_object_is_lmem(obj), obj->mm.region,
			       vma->ops, vma->private, __i915_vma_offset(vma),
			       __i915_vma_size(vma), vma->size, vma->guard);
}

/**
 * i915_vma_bind - Sets up PTEs for an VMA in it's corresponding address space.
 * @vma: VMA to map
 * @pat_index: PAT index to set in PTE
 * @flags: flags like global or local mapping
 * @work: preallocated worker for allocating and binding the PTE
 * @vma_res: pointer to a preallocated vma resource. The resource is either
 * consumed or freed.
 *
 * DMA addresses are taken from the scatter-gather table of this object (or of
 * this VMA in case of non-default GGTT views) and PTE entries set up.
 * Note that DMA addresses are also the only part of the SG table we care about.
 */
int i915_vma_bind(struct i915_vma *vma,
		  unsigned int pat_index,
		  u32 flags,
		  struct i915_vma_work *work,
		  struct i915_vma_resource *vma_res)
{
	u32 bind_flags;
	u32 vma_flags;
	int ret;

	lockdep_assert_held(&vma->vm->mutex);
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(vma->size > i915_vma_size(vma));

	if (GEM_DEBUG_WARN_ON(range_overflows(vma->node.start,
					      vma->node.size,
					      vma->vm->total))) {
		i915_vma_resource_free(vma_res);
		return -ENODEV;
	}

	if (GEM_DEBUG_WARN_ON(!flags)) {
		i915_vma_resource_free(vma_res);
		return -EINVAL;
	}

	bind_flags = flags;
	bind_flags &= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;

	vma_flags = atomic_read(&vma->flags);
	vma_flags &= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;

	bind_flags &= ~vma_flags;
	if (bind_flags == 0) {
		i915_vma_resource_free(vma_res);
		return 0;
	}

	GEM_BUG_ON(!atomic_read(&vma->pages_count));

	/* Wait for or await async unbinds touching our range */
	if (work && bind_flags & vma->vm->bind_async_flags)
		ret = i915_vma_resource_bind_dep_await(vma->vm,
						       &work->base.chain,
						       vma->node.start,
						       vma->node.size,
						       true,
						       GFP_NOWAIT |
						       __GFP_RETRY_MAYFAIL |
						       __GFP_NOWARN);
	else
		ret = i915_vma_resource_bind_dep_sync(vma->vm, vma->node.start,
						      vma->node.size, true);
	if (ret) {
		i915_vma_resource_free(vma_res);
		return ret;
	}

	if (vma->resource || !vma_res) {
		/* Rebinding with an additional I915_VMA_*_BIND */
		GEM_WARN_ON(!vma_flags);
		i915_vma_resource_free(vma_res);
	} else {
		i915_vma_resource_init_from_vma(vma_res, vma);
		vma->resource = vma_res;
	}
	trace_i915_vma_bind(vma, bind_flags);
	if (work && bind_flags & vma->vm->bind_async_flags) {
		struct dma_fence *prev;

		work->vma_res = i915_vma_resource_get(vma->resource);
		work->pat_index = pat_index;
		work->flags = bind_flags;

		/*
		 * Note we only want to chain up to the migration fence on
		 * the pages (not the object itself). As we don't track that,
		 * yet, we have to use the exclusive fence instead.
		 *
		 * Also note that we do not want to track the async vma as
		 * part of the obj->resv->excl_fence as it only affects
		 * execution and not content or object's backing store lifetime.
		 */
		prev = i915_active_set_exclusive(&vma->active, &work->base.dma);
		if (prev) {
			__i915_sw_fence_await_dma_fence(&work->base.chain,
							prev,
							&work->cb);
			dma_fence_put(prev);
		}

		work->base.dma.error = 0; /* enable the queue_work() */
		work->obj = i915_gem_object_get(vma->obj);
	} else {
		ret = i915_gem_object_wait_moving_fence(vma->obj, true);
		if (ret) {
			i915_vma_resource_free(vma->resource);
			vma->resource = NULL;

			return ret;
		}
		vma->ops->bind_vma(vma->vm, NULL, vma->resource, pat_index,
				   bind_flags);
	}

	atomic_or(bind_flags, &vma->flags);
	return 0;
}

void __iomem *i915_vma_pin_iomap(struct i915_vma *vma)
{
	void __iomem *ptr;
	int err;

	if (WARN_ON_ONCE(vma->obj->flags & I915_BO_ALLOC_GPU_ONLY))
		return IOMEM_ERR_PTR(-EINVAL);

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND));
	GEM_BUG_ON(i915_vma_verify_bind_complete(vma));

	ptr = READ_ONCE(vma->iomap);
	if (ptr == NULL) {
		/*
		 * TODO: consider just using i915_gem_object_pin_map() for lmem
		 * instead, which already supports mapping non-contiguous chunks
		 * of pages, that way we can also drop the
		 * I915_BO_ALLOC_CONTIGUOUS when allocating the object.
		 */
		if (i915_gem_object_is_lmem(vma->obj)) {
			ptr = i915_gem_object_lmem_io_map(vma->obj, 0,
							  vma->obj->base.size);
		} else if (i915_vma_is_map_and_fenceable(vma)) {
			ptr = io_mapping_map_wc(&i915_vm_to_ggtt(vma->vm)->iomap,
						i915_vma_offset(vma),
						i915_vma_size(vma));
		} else {
			ptr = (void __iomem *)
				i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
			if (IS_ERR(ptr)) {
				err = PTR_ERR(ptr);
				goto err;
			}
			ptr = page_pack_bits(ptr, 1);
		}

		if (ptr == NULL) {
			err = -ENOMEM;
			goto err;
		}

		if (unlikely(cmpxchg(&vma->iomap, NULL, ptr))) {
			if (page_unmask_bits(ptr))
				__i915_gem_object_release_map(vma->obj);
			else
				io_mapping_unmap(ptr);
			ptr = vma->iomap;
		}
	}

	__i915_vma_pin(vma);

	err = i915_vma_pin_fence(vma);
	if (err)
		goto err_unpin;

	i915_vma_set_ggtt_write(vma);

	/* NB Access through the GTT requires the device to be awake. */
	return page_mask_bits(ptr);

err_unpin:
	__i915_vma_unpin(vma);
err:
	return IOMEM_ERR_PTR(err);
}

void i915_vma_flush_writes(struct i915_vma *vma)
{
	if (i915_vma_unset_ggtt_write(vma))
		intel_gt_flush_ggtt_writes(vma->vm->gt);
}

void i915_vma_unpin_iomap(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->iomap == NULL);

	/* XXX We keep the mapping until __i915_vma_unbind()/evict() */

	i915_vma_flush_writes(vma);

	i915_vma_unpin_fence(vma);
	i915_vma_unpin(vma);
}

void i915_vma_unpin_and_release(struct i915_vma **p_vma, unsigned int flags)
{
	struct i915_vma *vma;
	struct drm_i915_gem_object *obj;

	vma = fetch_and_zero(p_vma);
	if (!vma)
		return;

	obj = vma->obj;
	GEM_BUG_ON(!obj);

	i915_vma_unpin(vma);

	if (flags & I915_VMA_RELEASE_MAP)
		i915_gem_object_unpin_map(obj);

	i915_gem_object_put(obj);
}

bool i915_vma_misplaced(const struct i915_vma *vma,
			u64 size, u64 alignment, u64 flags)
{
	if (!drm_mm_node_allocated(&vma->node))
		return false;

	if (test_bit(I915_VMA_ERROR_BIT, __i915_vma_flags(vma)))
		return true;

	if (i915_vma_size(vma) < size)
		return true;

	GEM_BUG_ON(alignment && !is_power_of_2(alignment));
	if (alignment && !IS_ALIGNED(i915_vma_offset(vma), alignment))
		return true;

	if (flags & PIN_MAPPABLE && !i915_vma_is_map_and_fenceable(vma))
		return true;

	if (flags & PIN_OFFSET_BIAS &&
	    i915_vma_offset(vma) < (flags & PIN_OFFSET_MASK))
		return true;

	if (flags & PIN_OFFSET_FIXED &&
	    i915_vma_offset(vma) != (flags & PIN_OFFSET_MASK))
		return true;

	if (flags & PIN_OFFSET_GUARD &&
	    vma->guard < (flags & PIN_OFFSET_MASK))
		return true;

	return false;
}

void __i915_vma_set_map_and_fenceable(struct i915_vma *vma)
{
	bool mappable, fenceable;

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!vma->fence_size);

	fenceable = (i915_vma_size(vma) >= vma->fence_size &&
		     IS_ALIGNED(i915_vma_offset(vma), vma->fence_alignment));

	mappable = i915_ggtt_offset(vma) + vma->fence_size <=
		   i915_vm_to_ggtt(vma->vm)->mappable_end;

	if (mappable && fenceable)
		set_bit(I915_VMA_CAN_FENCE_BIT, __i915_vma_flags(vma));
	else
		clear_bit(I915_VMA_CAN_FENCE_BIT, __i915_vma_flags(vma));
}

bool i915_gem_valid_gtt_space(struct i915_vma *vma, unsigned long color)
{
	struct drm_mm_node *node = &vma->node;
	struct drm_mm_node *other;

	/*
	 * On some machines we have to be careful when putting differing types
	 * of snoopable memory together to avoid the prefetcher crossing memory
	 * domains and dying. During vm initialisation, we decide whether or not
	 * these constraints apply and set the drm_mm.color_adjust
	 * appropriately.
	 */
	if (!i915_vm_has_cache_coloring(vma->vm))
		return true;

	/* Only valid to be called on an already inserted vma */
	GEM_BUG_ON(!drm_mm_node_allocated(node));
	GEM_BUG_ON(list_empty(&node->node_list));

	other = list_prev_entry(node, node_list);
	if (i915_node_color_differs(other, color) &&
	    !drm_mm_hole_follows(other))
		return false;

	other = list_next_entry(node, node_list);
	if (i915_node_color_differs(other, color) &&
	    !drm_mm_hole_follows(node))
		return false;

	return true;
}

/**
 * i915_vma_insert - finds a slot for the vma in its address space
 * @vma: the vma
 * @ww: An optional struct i915_gem_ww_ctx
 * @size: requested size in bytes (can be larger than the VMA)
 * @alignment: required alignment
 * @flags: mask of PIN_* flags to use
 *
 * First we try to allocate some free space that meets the requirements for
 * the VMA. Failiing that, if the flags permit, it will evict an old VMA,
 * preferrably the oldest idle entry to make room for the new VMA.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
static int
i915_vma_insert(struct i915_vma *vma, struct i915_gem_ww_ctx *ww,
		u64 size, u64 alignment, u64 flags)
{
	unsigned long color, guard;
	u64 start, end;
	int ret;

	GEM_BUG_ON(i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));
	GEM_BUG_ON(drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(hweight64(flags & (PIN_OFFSET_GUARD | PIN_OFFSET_FIXED | PIN_OFFSET_BIAS)) > 1);

	size = max(size, vma->size);
	alignment = max_t(typeof(alignment), alignment, vma->display_alignment);
	if (flags & PIN_MAPPABLE) {
		size = max_t(typeof(size), size, vma->fence_size);
		alignment = max_t(typeof(alignment),
				  alignment, vma->fence_alignment);
	}

	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(!IS_ALIGNED(alignment, I915_GTT_MIN_ALIGNMENT));
	GEM_BUG_ON(!is_power_of_2(alignment));

	guard = vma->guard; /* retain guard across rebinds */
	if (flags & PIN_OFFSET_GUARD) {
		GEM_BUG_ON(overflows_type(flags & PIN_OFFSET_MASK, u32));
		guard = max_t(u32, guard, flags & PIN_OFFSET_MASK);
	}
	/*
	 * As we align the node upon insertion, but the hardware gets
	 * node.start + guard, the easiest way to make that work is
	 * to make the guard a multiple of the alignment size.
	 */
	guard = ALIGN(guard, alignment);

	start = flags & PIN_OFFSET_BIAS ? flags & PIN_OFFSET_MASK : 0;
	GEM_BUG_ON(!IS_ALIGNED(start, I915_GTT_PAGE_SIZE));

	end = vma->vm->total;
	if (flags & PIN_MAPPABLE)
		end = min_t(u64, end, i915_vm_to_ggtt(vma->vm)->mappable_end);
	if (flags & PIN_ZONE_4G)
		end = min_t(u64, end, (1ULL << 32) - I915_GTT_PAGE_SIZE);
	GEM_BUG_ON(!IS_ALIGNED(end, I915_GTT_PAGE_SIZE));

	alignment = max(alignment, i915_vm_obj_min_alignment(vma->vm, vma->obj));

	/*
	 * If binding the object/GGTT view requires more space than the entire
	 * aperture has, reject it early before evicting everything in a vain
	 * attempt to find space.
	 */
	if (size > end - 2 * guard) {
		drm_dbg(vma->obj->base.dev,
			"Attempting to bind an object larger than the aperture: request=%llu > %s aperture=%llu\n",
			size, flags & PIN_MAPPABLE ? "mappable" : "total", end);
		return -ENOSPC;
	}

	color = 0;

	if (i915_vm_has_cache_coloring(vma->vm))
		color = vma->obj->pat_index;

	if (flags & PIN_OFFSET_FIXED) {
		u64 offset = flags & PIN_OFFSET_MASK;
		if (!IS_ALIGNED(offset, alignment) ||
		    range_overflows(offset, size, end))
			return -EINVAL;
		/*
		 * The caller knows not of the guard added by others and
		 * requests for the offset of the start of its buffer
		 * to be fixed, which may not be the same as the position
		 * of the vma->node due to the guard pages.
		 */
		if (offset < guard || offset + size > end - guard)
			return -ENOSPC;

		ret = i915_gem_gtt_reserve(vma->vm, ww, &vma->node,
					   size + 2 * guard,
					   offset - guard,
					   color, flags);
		if (ret)
			return ret;
	} else {
		size += 2 * guard;
		/*
		 * We only support huge gtt pages through the 48b PPGTT,
		 * however we also don't want to force any alignment for
		 * objects which need to be tightly packed into the low 32bits.
		 *
		 * Note that we assume that GGTT are limited to 4GiB for the
		 * forseeable future. See also i915_ggtt_offset().
		 */
		if (upper_32_bits(end - 1) &&
		    vma->page_sizes.sg > I915_GTT_PAGE_SIZE &&
		    !HAS_64K_PAGES(vma->vm->i915)) {
			/*
			 * We can't mix 64K and 4K PTEs in the same page-table
			 * (2M block), and so to avoid the ugliness and
			 * complexity of coloring we opt for just aligning 64K
			 * objects to 2M.
			 */
			u64 page_alignment =
				rounddown_pow_of_two(vma->page_sizes.sg |
						     I915_GTT_PAGE_SIZE_2M);

			/*
			 * Check we don't expand for the limited Global GTT
			 * (mappable aperture is even more precious!). This
			 * also checks that we exclude the aliasing-ppgtt.
			 */
			GEM_BUG_ON(i915_vma_is_ggtt(vma));

			alignment = max(alignment, page_alignment);

			if (vma->page_sizes.sg & I915_GTT_PAGE_SIZE_64K)
				size = round_up(size, I915_GTT_PAGE_SIZE_2M);
		}

		ret = i915_gem_gtt_insert(vma->vm, ww, &vma->node,
					  size, alignment, color,
					  start, end, flags);
		if (ret)
			return ret;

		GEM_BUG_ON(vma->node.start < start);
		GEM_BUG_ON(vma->node.start + vma->node.size > end);
	}
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(!i915_gem_valid_gtt_space(vma, color));

	list_move_tail(&vma->vm_link, &vma->vm->bound_list);
	vma->guard = guard;

	return 0;
}

static void
i915_vma_detach(struct i915_vma *vma)
{
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));

	/*
	 * And finally now the object is completely decoupled from this
	 * vma, we can drop its hold on the backing storage and allow
	 * it to be reaped by the shrinker.
	 */
	list_move_tail(&vma->vm_link, &vma->vm->unbound_list);
}

static bool try_qad_pin(struct i915_vma *vma, unsigned int flags)
{
	unsigned int bound;

	bound = atomic_read(&vma->flags);

	if (flags & PIN_VALIDATE) {
		flags &= I915_VMA_BIND_MASK;

		return (flags & bound) == flags;
	}

	/* with the lock mandatory for unbind, we don't race here */
	flags &= I915_VMA_BIND_MASK;
	do {
		if (unlikely(flags & ~bound))
			return false;

		if (unlikely(bound & (I915_VMA_OVERFLOW | I915_VMA_ERROR)))
			return false;

		GEM_BUG_ON(((bound + 1) & I915_VMA_PIN_MASK) == 0);
	} while (!atomic_try_cmpxchg(&vma->flags, &bound, bound + 1));

	return true;
}

static struct scatterlist *
rotate_pages(struct drm_i915_gem_object *obj, unsigned int offset,
	     unsigned int width, unsigned int height,
	     unsigned int src_stride, unsigned int dst_stride,
	     struct sg_table *st, struct scatterlist *sg)
{
	unsigned int column, row;
	pgoff_t src_idx;

	for (column = 0; column < width; column++) {
		unsigned int left;

		src_idx = src_stride * (height - 1) + column + offset;
		for (row = 0; row < height; row++) {
			st->nents++;
			/*
			 * We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */
			sg_set_page(sg, NULL, I915_GTT_PAGE_SIZE, 0);
			sg_dma_address(sg) =
				i915_gem_object_get_dma_address(obj, src_idx);
			sg_dma_len(sg) = I915_GTT_PAGE_SIZE;
			sg = sg_next(sg);
			src_idx -= src_stride;
		}

		left = (dst_stride - height) * I915_GTT_PAGE_SIZE;

		if (!left)
			continue;

		st->nents++;

		/*
		 * The DE ignores the PTEs for the padding tiles, the sg entry
		 * here is just a conenience to indicate how many padding PTEs
		 * to insert at this spot.
		 */
		sg_set_page(sg, NULL, left, 0);
		sg_dma_address(sg) = 0;
		sg_dma_len(sg) = left;
		sg = sg_next(sg);
	}

	return sg;
}

static noinline struct sg_table *
intel_rotate_pages(struct intel_rotation_info *rot_info,
		   struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_rotation_info_size(rot_info);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *st;
	struct scatterlist *sg;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rot_info->plane); i++)
		sg = rotate_pages(obj, rot_info->plane[i].offset,
				  rot_info->plane[i].width, rot_info->plane[i].height,
				  rot_info->plane[i].src_stride,
				  rot_info->plane[i].dst_stride,
				  st, sg);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	drm_dbg(&i915->drm, "Failed to create rotated mapping for object size %zu! (%ux%u tiles, %u pages)\n",
		obj->base.size, rot_info->plane[0].width,
		rot_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static struct scatterlist *
add_padding_pages(unsigned int count,
		  struct sg_table *st, struct scatterlist *sg)
{
	st->nents++;

	/*
	 * The DE ignores the PTEs for the padding tiles, the sg entry
	 * here is just a convenience to indicate how many padding PTEs
	 * to insert at this spot.
	 */
	sg_set_page(sg, NULL, count * I915_GTT_PAGE_SIZE, 0);
	sg_dma_address(sg) = 0;
	sg_dma_len(sg) = count * I915_GTT_PAGE_SIZE;
	sg = sg_next(sg);

	return sg;
}

static struct scatterlist *
remap_tiled_color_plane_pages(struct drm_i915_gem_object *obj,
			      unsigned long offset, unsigned int alignment_pad,
			      unsigned int width, unsigned int height,
			      unsigned int src_stride, unsigned int dst_stride,
			      struct sg_table *st, struct scatterlist *sg,
			      unsigned int *gtt_offset)
{
	unsigned int row;

	if (!width || !height)
		return sg;

	if (alignment_pad)
		sg = add_padding_pages(alignment_pad, st, sg);

	for (row = 0; row < height; row++) {
		unsigned int left = width * I915_GTT_PAGE_SIZE;

		while (left) {
			dma_addr_t addr;
			unsigned int length;

			/*
			 * We don't need the pages, but need to initialize
			 * the entries so the sg list can be happily traversed.
			 * The only thing we need are DMA addresses.
			 */

			addr = i915_gem_object_get_dma_address_len(obj, offset, &length);

			length = min(left, length);

			st->nents++;

			sg_set_page(sg, NULL, length, 0);
			sg_dma_address(sg) = addr;
			sg_dma_len(sg) = length;
			sg = sg_next(sg);

			offset += length / I915_GTT_PAGE_SIZE;
			left -= length;
		}

		offset += src_stride - width;

		left = (dst_stride - width) * I915_GTT_PAGE_SIZE;

		if (!left)
			continue;

		sg = add_padding_pages(left >> PAGE_SHIFT, st, sg);
	}

	*gtt_offset += alignment_pad + dst_stride * height;

	return sg;
}

static struct scatterlist *
remap_contiguous_pages(struct drm_i915_gem_object *obj,
		       pgoff_t obj_offset,
		       unsigned int count,
		       struct sg_table *st, struct scatterlist *sg)
{
	struct scatterlist *iter;
	unsigned int offset;

	iter = i915_gem_object_get_sg_dma(obj, obj_offset, &offset);
	GEM_BUG_ON(!iter);

	do {
		unsigned int len;

		len = min(sg_dma_len(iter) - (offset << PAGE_SHIFT),
			  count << PAGE_SHIFT);
		sg_set_page(sg, NULL, len, 0);
		sg_dma_address(sg) =
			sg_dma_address(iter) + (offset << PAGE_SHIFT);
		sg_dma_len(sg) = len;

		st->nents++;
		count -= len >> PAGE_SHIFT;
		if (count == 0)
			return sg;

		sg = __sg_next(sg);
		iter = __sg_next(iter);
		offset = 0;
	} while (1);
}

static struct scatterlist *
remap_linear_color_plane_pages(struct drm_i915_gem_object *obj,
			       pgoff_t obj_offset, unsigned int alignment_pad,
			       unsigned int size,
			       struct sg_table *st, struct scatterlist *sg,
			       unsigned int *gtt_offset)
{
	if (!size)
		return sg;

	if (alignment_pad)
		sg = add_padding_pages(alignment_pad, st, sg);

	sg = remap_contiguous_pages(obj, obj_offset, size, st, sg);
	sg = sg_next(sg);

	*gtt_offset += alignment_pad + size;

	return sg;
}

static struct scatterlist *
remap_color_plane_pages(const struct intel_remapped_info *rem_info,
			struct drm_i915_gem_object *obj,
			int color_plane,
			struct sg_table *st, struct scatterlist *sg,
			unsigned int *gtt_offset)
{
	unsigned int alignment_pad = 0;

	if (rem_info->plane_alignment)
		alignment_pad = ALIGN(*gtt_offset, rem_info->plane_alignment) - *gtt_offset;

	if (rem_info->plane[color_plane].linear)
		sg = remap_linear_color_plane_pages(obj,
						    rem_info->plane[color_plane].offset,
						    alignment_pad,
						    rem_info->plane[color_plane].size,
						    st, sg,
						    gtt_offset);

	else
		sg = remap_tiled_color_plane_pages(obj,
						   rem_info->plane[color_plane].offset,
						   alignment_pad,
						   rem_info->plane[color_plane].width,
						   rem_info->plane[color_plane].height,
						   rem_info->plane[color_plane].src_stride,
						   rem_info->plane[color_plane].dst_stride,
						   st, sg,
						   gtt_offset);

	return sg;
}

static noinline struct sg_table *
intel_remap_pages(struct intel_remapped_info *rem_info,
		  struct drm_i915_gem_object *obj)
{
	unsigned int size = intel_remapped_info_size(rem_info);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int gtt_offset = 0;
	int ret = -ENOMEM;
	int i;

	/* Allocate target SG list. */
	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, size, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;
	sg = st->sgl;

	for (i = 0 ; i < ARRAY_SIZE(rem_info->plane); i++)
		sg = remap_color_plane_pages(rem_info, obj, i, st, sg, &gtt_offset);

	i915_sg_trim(st);

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:

	drm_dbg(&i915->drm, "Failed to create remapped mapping for object size %zu! (%ux%u tiles, %u pages)\n",
		obj->base.size, rem_info->plane[0].width,
		rem_info->plane[0].height, size);

	return ERR_PTR(ret);
}

static noinline struct sg_table *
intel_partial_pages(const struct i915_gtt_view *view,
		    struct drm_i915_gem_object *obj)
{
	struct sg_table *st;
	struct scatterlist *sg;
	unsigned int count = view->partial.size;
	int ret = -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_st_alloc;

	ret = sg_alloc_table(st, count, GFP_KERNEL);
	if (ret)
		goto err_sg_alloc;

	st->nents = 0;

	sg = remap_contiguous_pages(obj, view->partial.offset, count, st, st->sgl);

	sg_mark_end(sg);
	i915_sg_trim(st); /* Drop any unused tail entries. */

	return st;

err_sg_alloc:
	kfree(st);
err_st_alloc:
	return ERR_PTR(ret);
}

static int
__i915_vma_get_pages(struct i915_vma *vma)
{
	struct sg_table *pages;

	/*
	 * The vma->pages are only valid within the lifespan of the borrowed
	 * obj->mm.pages. When the obj->mm.pages sg_table is regenerated, so
	 * must be the vma->pages. A simple rule is that vma->pages must only
	 * be accessed when the obj->mm.pages are pinned.
	 */
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(vma->obj));

	switch (vma->gtt_view.type) {
	default:
		GEM_BUG_ON(vma->gtt_view.type);
		fallthrough;
	case I915_GTT_VIEW_NORMAL:
		pages = vma->obj->mm.pages;
		break;

	case I915_GTT_VIEW_ROTATED:
		pages =
			intel_rotate_pages(&vma->gtt_view.rotated, vma->obj);
		break;

	case I915_GTT_VIEW_REMAPPED:
		pages =
			intel_remap_pages(&vma->gtt_view.remapped, vma->obj);
		break;

	case I915_GTT_VIEW_PARTIAL:
		pages = intel_partial_pages(&vma->gtt_view, vma->obj);
		break;
	}

	if (IS_ERR(pages)) {
		drm_err(&vma->vm->i915->drm,
			"Failed to get pages for VMA view type %u (%ld)!\n",
			vma->gtt_view.type, PTR_ERR(pages));
		return PTR_ERR(pages);
	}

	vma->pages = pages;

	return 0;
}

I915_SELFTEST_EXPORT int i915_vma_get_pages(struct i915_vma *vma)
{
	int err;

	if (atomic_add_unless(&vma->pages_count, 1, 0))
		return 0;

	err = i915_gem_object_pin_pages(vma->obj);
	if (err)
		return err;

	err = __i915_vma_get_pages(vma);
	if (err)
		goto err_unpin;

	vma->page_sizes = vma->obj->mm.page_sizes;
	atomic_inc(&vma->pages_count);

	return 0;

err_unpin:
	__i915_gem_object_unpin_pages(vma->obj);

	return err;
}

void vma_invalidate_tlb(struct i915_address_space *vm, u32 *tlb)
{
	struct intel_gt *gt;
	int id;

	if (!tlb)
		return;

	/*
	 * Before we release the pages that were bound by this vma, we
	 * must invalidate all the TLBs that may still have a reference
	 * back to our physical address. It only needs to be done once,
	 * so after updating the PTE to point away from the pages, record
	 * the most recent TLB invalidation seqno, and if we have not yet
	 * flushed the TLBs upon release, perform a full invalidation.
	 */
	for_each_gt(gt, vm->i915, id)
		WRITE_ONCE(tlb[id],
			   intel_gt_next_invalidate_tlb_full(gt));
}

static void __vma_put_pages(struct i915_vma *vma, unsigned int count)
{
	/* We allocate under vma_get_pages, so beware the shrinker */
	GEM_BUG_ON(atomic_read(&vma->pages_count) < count);

	if (atomic_sub_return(count, &vma->pages_count) == 0) {
		if (vma->pages != vma->obj->mm.pages) {
			sg_free_table(vma->pages);
			kfree(vma->pages);
		}
		vma->pages = NULL;

		i915_gem_object_unpin_pages(vma->obj);
	}
}

I915_SELFTEST_EXPORT void i915_vma_put_pages(struct i915_vma *vma)
{
	if (atomic_add_unless(&vma->pages_count, -1, 1))
		return;

	__vma_put_pages(vma, 1);
}

static void vma_unbind_pages(struct i915_vma *vma)
{
	unsigned int count;

	lockdep_assert_held(&vma->vm->mutex);

	/* The upper portion of pages_count is the number of bindings */
	count = atomic_read(&vma->pages_count);
	count >>= I915_VMA_PAGES_BIAS;
	GEM_BUG_ON(!count);

	__vma_put_pages(vma, count | count << I915_VMA_PAGES_BIAS);
}

int i915_vma_pin_ww(struct i915_vma *vma, struct i915_gem_ww_ctx *ww,
		    u64 size, u64 alignment, u64 flags)
{
	struct i915_vma_work *work = NULL;
	struct dma_fence *moving = NULL;
	struct i915_vma_resource *vma_res = NULL;
	intel_wakeref_t wakeref;
	unsigned int bound;
	int err;

	assert_vma_held(vma);
	GEM_BUG_ON(!ww);

	BUILD_BUG_ON(PIN_GLOBAL != I915_VMA_GLOBAL_BIND);
	BUILD_BUG_ON(PIN_USER != I915_VMA_LOCAL_BIND);

	GEM_BUG_ON(!(flags & (PIN_USER | PIN_GLOBAL)));

	/* First try and grab the pin without rebinding the vma */
	if (try_qad_pin(vma, flags))
		return 0;

	err = i915_vma_get_pages(vma);
	if (err)
		return err;

	/*
	 * In case of a global GTT, we must hold a runtime-pm wakeref
	 * while global PTEs are updated.  In other cases, we hold
	 * the rpm reference while the VMA is active.  Since runtime
	 * resume may require allocations, which are forbidden inside
	 * vm->mutex, get the first rpm wakeref outside of the mutex.
	 */
	wakeref = intel_runtime_pm_get(&vma->vm->i915->runtime_pm);

	if (flags & vma->vm->bind_async_flags) {
		/* lock VM */
		err = i915_vm_lock_objects(vma->vm, ww);
		if (err)
			goto err_rpm;

		work = i915_vma_work();
		if (!work) {
			err = -ENOMEM;
			goto err_rpm;
		}

		work->vm = vma->vm;

		err = i915_gem_object_get_moving_fence(vma->obj, &moving);
		if (err)
			goto err_rpm;

		dma_fence_work_chain(&work->base, moving);

		/* Allocate enough page directories to used PTE */
		if (vma->vm->allocate_va_range) {
			err = i915_vm_alloc_pt_stash(vma->vm,
						     &work->stash,
						     vma->size);
			if (err)
				goto err_fence;

			err = i915_vm_map_pt_stash(vma->vm, &work->stash);
			if (err)
				goto err_fence;
		}
	}

	vma_res = i915_vma_resource_alloc();
	if (IS_ERR(vma_res)) {
		err = PTR_ERR(vma_res);
		goto err_fence;
	}

	/*
	 * Differentiate between user/kernel vma inside the aliasing-ppgtt.
	 *
	 * We conflate the Global GTT with the user's vma when using the
	 * aliasing-ppgtt, but it is still vitally important to try and
	 * keep the use cases distinct. For example, userptr objects are
	 * not allowed inside the Global GTT as that will cause lock
	 * inversions when we have to evict them the mmu_notifier callbacks -
	 * but they are allowed to be part of the user ppGTT which can never
	 * be mapped. As such we try to give the distinct users of the same
	 * mutex, distinct lockclasses [equivalent to how we keep i915_ggtt
	 * and i915_ppgtt separate].
	 *
	 * NB this may cause us to mask real lock inversions -- while the
	 * code is safe today, lockdep may not be able to spot future
	 * transgressions.
	 */
	err = mutex_lock_interruptible_nested(&vma->vm->mutex,
					      !(flags & PIN_GLOBAL));
	if (err)
		goto err_vma_res;

	/* No more allocations allowed now we hold vm->mutex */

	if (unlikely(i915_vma_is_closed(vma))) {
		err = -ENOENT;
		goto err_unlock;
	}

	bound = atomic_read(&vma->flags);
	if (unlikely(bound & I915_VMA_ERROR)) {
		err = -ENOMEM;
		goto err_unlock;
	}

	if (unlikely(!((bound + 1) & I915_VMA_PIN_MASK))) {
		err = -EAGAIN; /* pins are meant to be fairly temporary */
		goto err_unlock;
	}

	if (unlikely(!(flags & ~bound & I915_VMA_BIND_MASK))) {
		if (!(flags & PIN_VALIDATE))
			__i915_vma_pin(vma);
		goto err_unlock;
	}

	err = i915_active_acquire(&vma->active);
	if (err)
		goto err_unlock;

	if (!(bound & I915_VMA_BIND_MASK)) {
		err = i915_vma_insert(vma, ww, size, alignment, flags);
		if (err)
			goto err_active;

		if (i915_is_ggtt(vma->vm))
			__i915_vma_set_map_and_fenceable(vma);
	}

	GEM_BUG_ON(!vma->pages);
	err = i915_vma_bind(vma,
			    vma->obj->pat_index,
			    flags, work, vma_res);
	vma_res = NULL;
	if (err)
		goto err_remove;

	/* There should only be at most 2 active bindings (user, global) */
	GEM_BUG_ON(bound + I915_VMA_PAGES_ACTIVE < bound);
	atomic_add(I915_VMA_PAGES_ACTIVE, &vma->pages_count);
	list_move_tail(&vma->vm_link, &vma->vm->bound_list);

	if (!(flags & PIN_VALIDATE)) {
		__i915_vma_pin(vma);
		GEM_BUG_ON(!i915_vma_is_pinned(vma));
	}
	GEM_BUG_ON(!i915_vma_is_bound(vma, flags));
	GEM_BUG_ON(i915_vma_misplaced(vma, size, alignment, flags));

err_remove:
	if (!i915_vma_is_bound(vma, I915_VMA_BIND_MASK)) {
		i915_vma_detach(vma);
		drm_mm_remove_node(&vma->node);
	}
err_active:
	i915_active_release(&vma->active);
err_unlock:
	mutex_unlock(&vma->vm->mutex);
err_vma_res:
	i915_vma_resource_free(vma_res);
err_fence:
	if (work)
		dma_fence_work_commit_imm(&work->base);
err_rpm:
	intel_runtime_pm_put(&vma->vm->i915->runtime_pm, wakeref);

	if (moving)
		dma_fence_put(moving);

	i915_vma_put_pages(vma);
	return err;
}

static void flush_idle_contexts(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		intel_engine_flush_barriers(engine);

	intel_gt_wait_for_idle(gt, MAX_SCHEDULE_TIMEOUT);
}

static int __i915_ggtt_pin(struct i915_vma *vma, struct i915_gem_ww_ctx *ww,
			   u32 align, unsigned int flags)
{
	struct i915_address_space *vm = vma->vm;
	struct intel_gt *gt;
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	int err;

	do {
		err = i915_vma_pin_ww(vma, ww, 0, align, flags | PIN_GLOBAL);

		if (err != -ENOSPC) {
			if (!err) {
				err = i915_vma_wait_for_bind(vma);
				if (err)
					i915_vma_unpin(vma);
			}
			return err;
		}

		/* Unlike i915_vma_pin, we don't take no for an answer! */
		list_for_each_entry(gt, &ggtt->gt_list, ggtt_link)
			flush_idle_contexts(gt);
		if (mutex_lock_interruptible(&vm->mutex) == 0) {
			/*
			 * We pass NULL ww here, as we don't want to unbind
			 * locked objects when called from execbuf when pinning
			 * is removed. This would probably regress badly.
			 */
			i915_gem_evict_vm(vm, NULL, NULL);
			mutex_unlock(&vm->mutex);
		}
	} while (1);
}

int i915_ggtt_pin(struct i915_vma *vma, struct i915_gem_ww_ctx *ww,
		  u32 align, unsigned int flags)
{
	struct i915_gem_ww_ctx _ww;
	int err;

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));

	if (ww)
		return __i915_ggtt_pin(vma, ww, align, flags);

	lockdep_assert_not_held(&vma->obj->base.resv->lock.base);

	for_i915_gem_ww(&_ww, err, true) {
		err = i915_gem_object_lock(vma->obj, &_ww);
		if (!err)
			err = __i915_ggtt_pin(vma, &_ww, align, flags);
	}

	return err;
}

/**
 * i915_ggtt_clear_scanout - Clear scanout flag for all objects ggtt vmas
 * @obj: i915 GEM object
 * This function clears scanout flags for objects ggtt vmas. These flags are set
 * when object is pinned for display use and this function to clear them all is
 * targeted to be called by frontbuffer tracking code when the frontbuffer is
 * about to be released.
 */
void i915_ggtt_clear_scanout(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;

	spin_lock(&obj->vma.lock);
	for_each_ggtt_vma(vma, obj) {
		i915_vma_clear_scanout(vma);
		vma->display_alignment = I915_GTT_MIN_ALIGNMENT;
	}
	spin_unlock(&obj->vma.lock);
}

static void __vma_close(struct i915_vma *vma, struct intel_gt *gt)
{
	/*
	 * We defer actually closing, unbinding and destroying the VMA until
	 * the next idle point, or if the object is freed in the meantime. By
	 * postponing the unbind, we allow for it to be resurrected by the
	 * client, avoiding the work required to rebind the VMA. This is
	 * advantageous for DRI, where the client/server pass objects
	 * between themselves, temporarily opening a local VMA to the
	 * object, and then closing it again. The same object is then reused
	 * on the next frame (or two, depending on the depth of the swap queue)
	 * causing us to rebind the VMA once more. This ends up being a lot
	 * of wasted work for the steady state.
	 */
	GEM_BUG_ON(i915_vma_is_closed(vma));
	list_add(&vma->closed_link, &gt->closed_vma);
}

void i915_vma_close(struct i915_vma *vma)
{
	struct intel_gt *gt = vma->vm->gt;
	unsigned long flags;

	if (i915_vma_is_ggtt(vma))
		return;

	GEM_BUG_ON(!atomic_read(&vma->open_count));
	if (atomic_dec_and_lock_irqsave(&vma->open_count,
					&gt->closed_lock,
					flags)) {
		__vma_close(vma, gt);
		spin_unlock_irqrestore(&gt->closed_lock, flags);
	}
}

static void __i915_vma_remove_closed(struct i915_vma *vma)
{
	list_del_init(&vma->closed_link);
}

void i915_vma_reopen(struct i915_vma *vma)
{
	struct intel_gt *gt = vma->vm->gt;

	spin_lock_irq(&gt->closed_lock);
	if (i915_vma_is_closed(vma))
		__i915_vma_remove_closed(vma);
	spin_unlock_irq(&gt->closed_lock);
}

static void force_unbind(struct i915_vma *vma)
{
	if (!drm_mm_node_allocated(&vma->node))
		return;

	atomic_and(~I915_VMA_PIN_MASK, &vma->flags);
	WARN_ON(__i915_vma_unbind(vma));
	GEM_BUG_ON(drm_mm_node_allocated(&vma->node));
}

static void release_references(struct i915_vma *vma, struct intel_gt *gt,
			       bool vm_ddestroy)
{
	struct drm_i915_gem_object *obj = vma->obj;

	GEM_BUG_ON(i915_vma_is_active(vma));

	spin_lock(&obj->vma.lock);
	list_del(&vma->obj_link);
	if (!RB_EMPTY_NODE(&vma->obj_node))
		rb_erase(&vma->obj_node, &obj->vma.tree);

	spin_unlock(&obj->vma.lock);

	spin_lock_irq(&gt->closed_lock);
	__i915_vma_remove_closed(vma);
	spin_unlock_irq(&gt->closed_lock);

	if (vm_ddestroy)
		i915_vm_resv_put(vma->vm);

	i915_active_fini(&vma->active);
	GEM_WARN_ON(vma->resource);
	i915_vma_free(vma);
}

/*
 * i915_vma_destroy_locked - Remove all weak reference to the vma and put
 * the initial reference.
 *
 * This function should be called when it's decided the vma isn't needed
 * anymore. The caller must assure that it doesn't race with another lookup
 * plus destroy, typically by taking an appropriate reference.
 *
 * Current callsites are
 * - __i915_gem_object_pages_fini()
 * - __i915_vm_close() - Blocks the above function by taking a reference on
 * the object.
 * - __i915_vma_parked() - Blocks the above functions by taking a reference
 * on the vm and a reference on the object. Also takes the object lock so
 * destruction from __i915_vma_parked() can be blocked by holding the
 * object lock. Since the object lock is only allowed from within i915 with
 * an object refcount, holding the object lock also implicitly blocks the
 * vma freeing from __i915_gem_object_pages_fini().
 *
 * Because of locks taken during destruction, a vma is also guaranteed to
 * stay alive while the following locks are held if it was looked up while
 * holding one of the locks:
 * - vm->mutex
 * - obj->vma.lock
 * - gt->closed_lock
 */
void i915_vma_destroy_locked(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->mutex);

	force_unbind(vma);
	list_del_init(&vma->vm_link);
	release_references(vma, vma->vm->gt, false);
}

void i915_vma_destroy(struct i915_vma *vma)
{
	struct intel_gt *gt;
	bool vm_ddestroy;

	mutex_lock(&vma->vm->mutex);
	force_unbind(vma);
	list_del_init(&vma->vm_link);
	vm_ddestroy = vma->vm_ddestroy;
	vma->vm_ddestroy = false;

	/* vma->vm may be freed when releasing vma->vm->mutex. */
	gt = vma->vm->gt;
	mutex_unlock(&vma->vm->mutex);
	release_references(vma, gt, vm_ddestroy);
}

void i915_vma_parked(struct intel_gt *gt)
{
	struct i915_vma *vma, *next;
	LIST_HEAD(closed);

	spin_lock_irq(&gt->closed_lock);
	list_for_each_entry_safe(vma, next, &gt->closed_vma, closed_link) {
		struct drm_i915_gem_object *obj = vma->obj;
		struct i915_address_space *vm = vma->vm;

		/* XXX All to avoid keeping a reference on i915_vma itself */

		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		if (!i915_vm_tryget(vm)) {
			i915_gem_object_put(obj);
			continue;
		}

		list_move(&vma->closed_link, &closed);
	}
	spin_unlock_irq(&gt->closed_lock);

	/* As the GT is held idle, no vma can be reopened as we destroy them */
	list_for_each_entry_safe(vma, next, &closed, closed_link) {
		struct drm_i915_gem_object *obj = vma->obj;
		struct i915_address_space *vm = vma->vm;

		if (i915_gem_object_trylock(obj, NULL)) {
			INIT_LIST_HEAD(&vma->closed_link);
			i915_vma_destroy(vma);
			i915_gem_object_unlock(obj);
		} else {
			/* back you go.. */
			spin_lock_irq(&gt->closed_lock);
			list_add(&vma->closed_link, &gt->closed_vma);
			spin_unlock_irq(&gt->closed_lock);
		}

		i915_gem_object_put(obj);
		i915_vm_put(vm);
	}
}

static void __i915_vma_iounmap(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_pinned(vma));

	if (vma->iomap == NULL)
		return;

	if (page_unmask_bits(vma->iomap))
		__i915_gem_object_release_map(vma->obj);
	else
		io_mapping_unmap(vma->iomap);
	vma->iomap = NULL;
}

void i915_vma_revoke_mmap(struct i915_vma *vma)
{
	struct drm_vma_offset_node *node;
	u64 vma_offset;

	if (!i915_vma_has_userfault(vma))
		return;

	GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
	GEM_BUG_ON(!vma->obj->userfault_count);

	node = &vma->mmo->vma_node;
	vma_offset = vma->gtt_view.partial.offset << PAGE_SHIFT;
	unmap_mapping_range(vma->vm->i915->drm.anon_inode->i_mapping,
			    drm_vma_node_offset_addr(node) + vma_offset,
			    vma->size,
			    1);

	i915_vma_unset_userfault(vma);
	if (!--vma->obj->userfault_count)
		list_del(&vma->obj->userfault_link);
}

static int
__i915_request_await_bind(struct i915_request *rq, struct i915_vma *vma)
{
	return __i915_request_await_exclusive(rq, &vma->active);
}

static int __i915_vma_move_to_active(struct i915_vma *vma, struct i915_request *rq)
{
	int err;

	/* Wait for the vma to be bound before we start! */
	err = __i915_request_await_bind(rq, vma);
	if (err)
		return err;

	return i915_active_add_request(&vma->active, rq);
}

int _i915_vma_move_to_active(struct i915_vma *vma,
			     struct i915_request *rq,
			     struct dma_fence *fence,
			     unsigned int flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	int err;

	assert_object_held(obj);

	GEM_BUG_ON(!vma->pages);

	if (!(flags & __EXEC_OBJECT_NO_REQUEST_AWAIT)) {
		err = i915_request_await_object(rq, vma->obj, flags & EXEC_OBJECT_WRITE);
		if (unlikely(err))
			return err;
	}
	err = __i915_vma_move_to_active(vma, rq);
	if (unlikely(err))
		return err;

	/*
	 * Reserve fences slot early to prevent an allocation after preparing
	 * the workload and associating fences with dma_resv.
	 */
	if (fence && !(flags & __EXEC_OBJECT_NO_RESERVE)) {
		struct dma_fence *curr;
		int idx;

		dma_fence_array_for_each(curr, idx, fence)
			;
		err = dma_resv_reserve_fences(vma->obj->base.resv, idx);
		if (unlikely(err))
			return err;
	}

	if (flags & EXEC_OBJECT_WRITE) {
		struct intel_frontbuffer *front;

		front = i915_gem_object_get_frontbuffer(obj);
		if (unlikely(front)) {
			if (intel_frontbuffer_invalidate(front, ORIGIN_CS))
				i915_active_add_request(&front->write, rq);
			intel_frontbuffer_put(front);
		}
	}

	if (fence) {
		struct dma_fence *curr;
		enum dma_resv_usage usage;
		int idx;

		if (flags & EXEC_OBJECT_WRITE) {
			usage = DMA_RESV_USAGE_WRITE;
			obj->write_domain = I915_GEM_DOMAIN_RENDER;
			obj->read_domains = 0;
		} else {
			usage = DMA_RESV_USAGE_READ;
			obj->write_domain = 0;
		}

		dma_fence_array_for_each(curr, idx, fence)
			dma_resv_add_fence(vma->obj->base.resv, curr, usage);
	}

	if (flags & EXEC_OBJECT_NEEDS_FENCE && vma->fence)
		i915_active_add_request(&vma->fence->active, rq);

	obj->read_domains |= I915_GEM_GPU_DOMAINS;
	obj->mm.dirty = true;

	GEM_BUG_ON(!i915_vma_is_active(vma));
	return 0;
}

struct dma_fence *__i915_vma_evict(struct i915_vma *vma, bool async)
{
	struct i915_vma_resource *vma_res = vma->resource;
	struct dma_fence *unbind_fence;

	GEM_BUG_ON(i915_vma_is_pinned(vma));
	assert_vma_held_evict(vma);

	if (i915_vma_is_map_and_fenceable(vma)) {
		/* Force a pagefault for domain tracking on next user access */
		i915_vma_revoke_mmap(vma);

		/*
		 * Check that we have flushed all writes through the GGTT
		 * before the unbind, other due to non-strict nature of those
		 * indirect writes they may end up referencing the GGTT PTE
		 * after the unbind.
		 *
		 * Note that we may be concurrently poking at the GGTT_WRITE
		 * bit from set-domain, as we mark all GGTT vma associated
		 * with an object. We know this is for another vma, as we
		 * are currently unbinding this one -- so if this vma will be
		 * reused, it will be refaulted and have its dirty bit set
		 * before the next write.
		 */
		i915_vma_flush_writes(vma);

		/* release the fence reg _after_ flushing */
		i915_vma_revoke_fence(vma);

		clear_bit(I915_VMA_CAN_FENCE_BIT, __i915_vma_flags(vma));
	}

	__i915_vma_iounmap(vma);

	GEM_BUG_ON(vma->fence);
	GEM_BUG_ON(i915_vma_has_userfault(vma));

	/* Object backend must be async capable. */
	GEM_WARN_ON(async && !vma->resource->bi.pages_rsgt);

	/* If vm is not open, unbind is a nop. */
	vma_res->needs_wakeref = i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND) &&
		kref_read(&vma->vm->ref);
	vma_res->skip_pte_rewrite = !kref_read(&vma->vm->ref) ||
		vma->vm->skip_pte_rewrite;
	trace_i915_vma_unbind(vma);

	if (async)
		unbind_fence = i915_vma_resource_unbind(vma_res,
							vma->obj->mm.tlb);
	else
		unbind_fence = i915_vma_resource_unbind(vma_res, NULL);

	vma->resource = NULL;

	atomic_and(~(I915_VMA_BIND_MASK | I915_VMA_ERROR | I915_VMA_GGTT_WRITE),
		   &vma->flags);

	i915_vma_detach(vma);

	if (!async) {
		if (unbind_fence) {
			dma_fence_wait(unbind_fence, false);
			dma_fence_put(unbind_fence);
			unbind_fence = NULL;
		}
		vma_invalidate_tlb(vma->vm, vma->obj->mm.tlb);
	}

	/*
	 * Binding itself may not have completed until the unbind fence signals,
	 * so don't drop the pages until that happens, unless the resource is
	 * async_capable.
	 */

	vma_unbind_pages(vma);
	return unbind_fence;
}

int __i915_vma_unbind(struct i915_vma *vma)
{
	int ret;

	lockdep_assert_held(&vma->vm->mutex);
	assert_vma_held_evict(vma);

	if (!drm_mm_node_allocated(&vma->node))
		return 0;

	if (i915_vma_is_pinned(vma)) {
		vma_print_allocator(vma, "is pinned");
		return -EAGAIN;
	}

	/*
	 * After confirming that no one else is pinning this vma, wait for
	 * any laggards who may have crept in during the wait (through
	 * a residual pin skipping the vm->mutex) to complete.
	 */
	ret = i915_vma_sync(vma);
	if (ret)
		return ret;

	GEM_BUG_ON(i915_vma_is_active(vma));
	__i915_vma_evict(vma, false);

	drm_mm_remove_node(&vma->node); /* pairs with i915_vma_release() */
	return 0;
}

static struct dma_fence *__i915_vma_unbind_async(struct i915_vma *vma)
{
	struct dma_fence *fence;

	lockdep_assert_held(&vma->vm->mutex);

	if (!drm_mm_node_allocated(&vma->node))
		return NULL;

	if (i915_vma_is_pinned(vma) ||
	    &vma->obj->mm.rsgt->table != vma->resource->bi.pages)
		return ERR_PTR(-EAGAIN);

	/*
	 * We probably need to replace this with awaiting the fences of the
	 * object's dma_resv when the vma active goes away. When doing that
	 * we need to be careful to not add the vma_resource unbind fence
	 * immediately to the object's dma_resv, because then unbinding
	 * the next vma from the object, in case there are many, will
	 * actually await the unbinding of the previous vmas, which is
	 * undesirable.
	 */
	if (i915_sw_fence_await_active(&vma->resource->chain, &vma->active,
				       I915_ACTIVE_AWAIT_EXCL |
				       I915_ACTIVE_AWAIT_ACTIVE) < 0) {
		return ERR_PTR(-EBUSY);
	}

	fence = __i915_vma_evict(vma, true);

	drm_mm_remove_node(&vma->node); /* pairs with i915_vma_release() */

	return fence;
}

int i915_vma_unbind(struct i915_vma *vma)
{
	struct i915_address_space *vm = vma->vm;
	intel_wakeref_t wakeref = 0;
	int err;

	assert_object_held_shared(vma->obj);

	/* Optimistic wait before taking the mutex */
	err = i915_vma_sync(vma);
	if (err)
		return err;

	if (!drm_mm_node_allocated(&vma->node))
		return 0;

	if (i915_vma_is_pinned(vma)) {
		vma_print_allocator(vma, "is pinned");
		return -EAGAIN;
	}

	if (i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
		/* XXX not always required: nop_clear_range */
		wakeref = intel_runtime_pm_get(&vm->i915->runtime_pm);

	err = mutex_lock_interruptible_nested(&vma->vm->mutex, !wakeref);
	if (err)
		goto out_rpm;

	err = __i915_vma_unbind(vma);
	mutex_unlock(&vm->mutex);

out_rpm:
	if (wakeref)
		intel_runtime_pm_put(&vm->i915->runtime_pm, wakeref);
	return err;
}

int i915_vma_unbind_async(struct i915_vma *vma, bool trylock_vm)
{
	struct drm_i915_gem_object *obj = vma->obj;
	struct i915_address_space *vm = vma->vm;
	intel_wakeref_t wakeref = 0;
	struct dma_fence *fence;
	int err;

	/*
	 * We need the dma-resv lock since we add the
	 * unbind fence to the dma-resv object.
	 */
	assert_object_held(obj);

	if (!drm_mm_node_allocated(&vma->node))
		return 0;

	if (i915_vma_is_pinned(vma)) {
		vma_print_allocator(vma, "is pinned");
		return -EAGAIN;
	}

	if (!obj->mm.rsgt)
		return -EBUSY;

	err = dma_resv_reserve_fences(obj->base.resv, 2);
	if (err)
		return -EBUSY;

	/*
	 * It would be great if we could grab this wakeref from the
	 * async unbind work if needed, but we can't because it uses
	 * kmalloc and it's in the dma-fence signalling critical path.
	 */
	if (i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
		wakeref = intel_runtime_pm_get(&vm->i915->runtime_pm);

	if (trylock_vm && !mutex_trylock(&vm->mutex)) {
		err = -EBUSY;
		goto out_rpm;
	} else if (!trylock_vm) {
		err = mutex_lock_interruptible_nested(&vm->mutex, !wakeref);
		if (err)
			goto out_rpm;
	}

	fence = __i915_vma_unbind_async(vma);
	mutex_unlock(&vm->mutex);
	if (IS_ERR_OR_NULL(fence)) {
		err = PTR_ERR_OR_ZERO(fence);
		goto out_rpm;
	}

	dma_resv_add_fence(obj->base.resv, fence, DMA_RESV_USAGE_READ);
	dma_fence_put(fence);

out_rpm:
	if (wakeref)
		intel_runtime_pm_put(&vm->i915->runtime_pm, wakeref);
	return err;
}

int i915_vma_unbind_unlocked(struct i915_vma *vma)
{
	int err;

	i915_gem_object_lock(vma->obj, NULL);
	err = i915_vma_unbind(vma);
	i915_gem_object_unlock(vma->obj);

	return err;
}

struct i915_vma *i915_vma_make_unshrinkable(struct i915_vma *vma)
{
	i915_gem_object_make_unshrinkable(vma->obj);
	return vma;
}

void i915_vma_make_shrinkable(struct i915_vma *vma)
{
	i915_gem_object_make_shrinkable(vma->obj);
}

void i915_vma_make_purgeable(struct i915_vma *vma)
{
	i915_gem_object_make_purgeable(vma->obj);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_vma.c"
#endif

void i915_vma_module_exit(void)
{
	kmem_cache_destroy(slab_vmas);
}

int __init i915_vma_module_init(void)
{
	slab_vmas = KMEM_CACHE(i915_vma, SLAB_HWCACHE_ALIGN);
	if (!slab_vmas)
		return -ENOMEM;

	return 0;
}
