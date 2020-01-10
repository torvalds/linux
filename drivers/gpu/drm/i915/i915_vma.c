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
#include <drm/drm_gem.h>

#include "display/intel_frontbuffer.h"

#include "gt/intel_engine.h"
#include "gt/intel_gt.h"

#include "i915_drv.h"
#include "i915_globals.h"
#include "i915_sw_fence_work.h"
#include "i915_trace.h"
#include "i915_vma.h"

static struct i915_global_vma {
	struct i915_global base;
	struct kmem_cache *slab_vmas;
} global;

struct i915_vma *i915_vma_alloc(void)
{
	return kmem_cache_zalloc(global.slab_vmas, GFP_KERNEL);
}

void i915_vma_free(struct i915_vma *vma)
{
	return kmem_cache_free(global.slab_vmas, vma);
}

#if IS_ENABLED(CONFIG_DRM_I915_ERRLOG_GEM) && IS_ENABLED(CONFIG_DRM_DEBUG_MM)

#include <linux/stackdepot.h>

static void vma_print_allocator(struct i915_vma *vma, const char *reason)
{
	unsigned long *entries;
	unsigned int nr_entries;
	char buf[512];

	if (!vma->node.stack) {
		DRM_DEBUG_DRIVER("vma.node [%08llx + %08llx] %s: unknown owner\n",
				 vma->node.start, vma->node.size, reason);
		return;
	}

	nr_entries = stack_depot_fetch(vma->node.stack, &entries);
	stack_trace_snprint(buf, sizeof(buf), entries, nr_entries, 0);
	DRM_DEBUG_DRIVER("vma.node [%08llx + %08llx] %s: inserted at %s\n",
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
	return i915_vma_tryget(active_to_vma(ref)) ? 0 : -ENOENT;
}

__i915_active_call
static void __i915_vma_retire(struct i915_active *ref)
{
	i915_vma_put(active_to_vma(ref));
}

static struct i915_vma *
vma_create(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;
	struct rb_node *rb, **p;

	/* The aliasing_ppgtt should never be used directly! */
	GEM_BUG_ON(vm == &vm->gt->ggtt->alias->vm);

	vma = i915_vma_alloc();
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&vma->pages_mutex);
	vma->vm = i915_vm_get(vm);
	vma->ops = &vm->vma_ops;
	vma->obj = obj;
	vma->resv = obj->base.resv;
	vma->size = obj->base.size;
	vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	i915_active_init(&vma->active, __i915_vma_active, __i915_vma_retire);

	/* Declare ourselves safe for use inside shrinkers */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&vma->active.mutex);
		fs_reclaim_release(GFP_KERNEL);
	}

	INIT_LIST_HEAD(&vma->closed_link);

	if (view && view->type != I915_GGTT_VIEW_NORMAL) {
		vma->ggtt_view = *view;
		if (view->type == I915_GGTT_VIEW_PARTIAL) {
			GEM_BUG_ON(range_overflows_t(u64,
						     view->partial.offset,
						     view->partial.size,
						     obj->base.size >> PAGE_SHIFT));
			vma->size = view->partial.size;
			vma->size <<= PAGE_SHIFT;
			GEM_BUG_ON(vma->size > obj->base.size);
		} else if (view->type == I915_GGTT_VIEW_ROTATED) {
			vma->size = intel_rotation_info_size(&view->rotated);
			vma->size <<= PAGE_SHIFT;
		} else if (view->type == I915_GGTT_VIEW_REMAPPED) {
			vma->size = intel_remapped_info_size(&view->remapped);
			vma->size <<= PAGE_SHIFT;
		}
	}

	if (unlikely(vma->size > vm->total))
		goto err_vma;

	GEM_BUG_ON(!IS_ALIGNED(vma->size, I915_GTT_PAGE_SIZE));

	if (i915_is_ggtt(vm)) {
		if (unlikely(overflows_type(vma->size, u32)))
			goto err_vma;

		vma->fence_size = i915_gem_fence_size(vm->i915, vma->size,
						      i915_gem_object_get_tiling(obj),
						      i915_gem_object_get_stride(obj));
		if (unlikely(vma->fence_size < vma->size || /* overflow */
			     vma->fence_size > vm->total))
			goto err_vma;

		GEM_BUG_ON(!IS_ALIGNED(vma->fence_size, I915_GTT_MIN_ALIGNMENT));

		vma->fence_alignment = i915_gem_fence_alignment(vm->i915, vma->size,
								i915_gem_object_get_tiling(obj),
								i915_gem_object_get_stride(obj));
		GEM_BUG_ON(!is_power_of_2(vma->fence_alignment));

		__set_bit(I915_VMA_GGTT_BIT, __i915_vma_flags(vma));
	}

	spin_lock(&obj->vma.lock);

	rb = NULL;
	p = &obj->vma.tree.rb_node;
	while (*p) {
		struct i915_vma *pos;
		long cmp;

		rb = *p;
		pos = rb_entry(rb, struct i915_vma, obj_node);

		/*
		 * If the view already exists in the tree, another thread
		 * already created a matching vma, so return the older instance
		 * and dispose of ours.
		 */
		cmp = i915_vma_compare(pos, vm, view);
		if (cmp == 0) {
			spin_unlock(&obj->vma.lock);
			i915_vma_free(vma);
			return pos;
		}

		if (cmp < 0)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
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

	return vma;

err_vma:
	i915_vma_free(vma);
	return ERR_PTR(-E2BIG);
}

static struct i915_vma *
vma_lookup(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_ggtt_view *view)
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
		  const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;

	GEM_BUG_ON(view && !i915_is_ggtt(vm));
	GEM_BUG_ON(!atomic_read(&vm->open));

	spin_lock(&obj->vma.lock);
	vma = vma_lookup(obj, vm, view);
	spin_unlock(&obj->vma.lock);

	/* vma_create() will resolve the race if another creates the vma */
	if (unlikely(!vma))
		vma = vma_create(obj, vm, view);

	GEM_BUG_ON(!IS_ERR(vma) && i915_vma_compare(vma, vm, view));
	return vma;
}

struct i915_vma_work {
	struct dma_fence_work base;
	struct i915_vma *vma;
	enum i915_cache_level cache_level;
	unsigned int flags;
};

static int __vma_bind(struct dma_fence_work *work)
{
	struct i915_vma_work *vw = container_of(work, typeof(*vw), base);
	struct i915_vma *vma = vw->vma;
	int err;

	err = vma->ops->bind_vma(vma, vw->cache_level, vw->flags);
	if (err)
		atomic_or(I915_VMA_ERROR, &vma->flags);

	if (vma->obj)
		__i915_gem_object_unpin_pages(vma->obj);

	return err;
}

static const struct dma_fence_work_ops bind_ops = {
	.name = "bind",
	.work = __vma_bind,
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

/**
 * i915_vma_bind - Sets up PTEs for an VMA in it's corresponding address space.
 * @vma: VMA to map
 * @cache_level: mapping cache level
 * @flags: flags like global or local mapping
 * @work: preallocated worker for allocating and binding the PTE
 *
 * DMA addresses are taken from the scatter-gather table of this object (or of
 * this VMA in case of non-default GGTT views) and PTE entries set up.
 * Note that DMA addresses are also the only part of the SG table we care about.
 */
int i915_vma_bind(struct i915_vma *vma,
		  enum i915_cache_level cache_level,
		  u32 flags,
		  struct i915_vma_work *work)
{
	u32 bind_flags;
	u32 vma_flags;
	int ret;

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(vma->size > vma->node.size);

	if (GEM_DEBUG_WARN_ON(range_overflows(vma->node.start,
					      vma->node.size,
					      vma->vm->total)))
		return -ENODEV;

	if (GEM_DEBUG_WARN_ON(!flags))
		return -EINVAL;

	bind_flags = flags;
	bind_flags &= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;

	vma_flags = atomic_read(&vma->flags);
	vma_flags &= I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
	if (flags & PIN_UPDATE)
		bind_flags |= vma_flags;
	else
		bind_flags &= ~vma_flags;
	if (bind_flags == 0)
		return 0;

	GEM_BUG_ON(!vma->pages);

	trace_i915_vma_bind(vma, bind_flags);
	if (work && (bind_flags & ~vma_flags) & vma->vm->bind_async_flags) {
		work->vma = vma;
		work->cache_level = cache_level;
		work->flags = bind_flags | I915_VMA_ALLOC;

		/*
		 * Note we only want to chain up to the migration fence on
		 * the pages (not the object itself). As we don't track that,
		 * yet, we have to use the exclusive fence instead.
		 *
		 * Also note that we do not want to track the async vma as
		 * part of the obj->resv->excl_fence as it only affects
		 * execution and not content or object's backing store lifetime.
		 */
		GEM_BUG_ON(i915_active_has_exclusive(&vma->active));
		i915_active_set_exclusive(&vma->active, &work->base.dma);
		work->base.dma.error = 0; /* enable the queue_work() */

		if (vma->obj)
			__i915_gem_object_pin_pages(vma->obj);
	} else {
		GEM_BUG_ON((bind_flags & ~vma_flags) & vma->vm->bind_async_flags);
		ret = vma->ops->bind_vma(vma, cache_level, bind_flags);
		if (ret)
			return ret;
	}

	atomic_or(bind_flags, &vma->flags);
	return 0;
}

void __iomem *i915_vma_pin_iomap(struct i915_vma *vma)
{
	void __iomem *ptr;
	int err;

	/* Access through the GTT requires the device to be awake. */
	assert_rpm_wakelock_held(vma->vm->gt->uncore->rpm);
	if (GEM_WARN_ON(!i915_vma_is_map_and_fenceable(vma))) {
		err = -ENODEV;
		goto err;
	}

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND));

	ptr = READ_ONCE(vma->iomap);
	if (ptr == NULL) {
		ptr = io_mapping_map_wc(&i915_vm_to_ggtt(vma->vm)->iomap,
					vma->node.start,
					vma->node.size);
		if (ptr == NULL) {
			err = -ENOMEM;
			goto err;
		}

		if (unlikely(cmpxchg(&vma->iomap, NULL, ptr))) {
			io_mapping_unmap(ptr);
			ptr = vma->iomap;
		}
	}

	__i915_vma_pin(vma);

	err = i915_vma_pin_fence(vma);
	if (err)
		goto err_unpin;

	i915_vma_set_ggtt_write(vma);
	return ptr;

err_unpin:
	__i915_vma_unpin(vma);
err:
	return IO_ERR_PTR(err);
}

void i915_vma_flush_writes(struct i915_vma *vma)
{
	if (i915_vma_unset_ggtt_write(vma))
		intel_gt_flush_ggtt_writes(vma->vm->gt);
}

void i915_vma_unpin_iomap(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->iomap == NULL);

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
	i915_vma_close(vma);

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

	if (vma->node.size < size)
		return true;

	GEM_BUG_ON(alignment && !is_power_of_2(alignment));
	if (alignment && !IS_ALIGNED(vma->node.start, alignment))
		return true;

	if (flags & PIN_MAPPABLE && !i915_vma_is_map_and_fenceable(vma))
		return true;

	if (flags & PIN_OFFSET_BIAS &&
	    vma->node.start < (flags & PIN_OFFSET_MASK))
		return true;

	if (flags & PIN_OFFSET_FIXED &&
	    vma->node.start != (flags & PIN_OFFSET_MASK))
		return true;

	return false;
}

void __i915_vma_set_map_and_fenceable(struct i915_vma *vma)
{
	bool mappable, fenceable;

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!vma->fence_size);

	fenceable = (vma->node.size >= vma->fence_size &&
		     IS_ALIGNED(vma->node.start, vma->fence_alignment));

	mappable = vma->node.start + vma->fence_size <= i915_vm_to_ggtt(vma->vm)->mappable_end;

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

static void assert_bind_count(const struct drm_i915_gem_object *obj)
{
	/*
	 * Combine the assertion that the object is bound and that we have
	 * pinned its pages. But we should never have bound the object
	 * more than we have pinned its pages. (For complete accuracy, we
	 * assume that no else is pinning the pages, but as a rough assertion
	 * that we will not run into problems later, this will do!)
	 */
	GEM_BUG_ON(atomic_read(&obj->mm.pages_pin_count) < atomic_read(&obj->bind_count));
}

/**
 * i915_vma_insert - finds a slot for the vma in its address space
 * @vma: the vma
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
i915_vma_insert(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	unsigned long color;
	u64 start, end;
	int ret;

	GEM_BUG_ON(i915_vma_is_closed(vma));
	GEM_BUG_ON(i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));
	GEM_BUG_ON(drm_mm_node_allocated(&vma->node));

	size = max(size, vma->size);
	alignment = max(alignment, vma->display_alignment);
	if (flags & PIN_MAPPABLE) {
		size = max_t(typeof(size), size, vma->fence_size);
		alignment = max_t(typeof(alignment),
				  alignment, vma->fence_alignment);
	}

	GEM_BUG_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(!IS_ALIGNED(alignment, I915_GTT_MIN_ALIGNMENT));
	GEM_BUG_ON(!is_power_of_2(alignment));

	start = flags & PIN_OFFSET_BIAS ? flags & PIN_OFFSET_MASK : 0;
	GEM_BUG_ON(!IS_ALIGNED(start, I915_GTT_PAGE_SIZE));

	end = vma->vm->total;
	if (flags & PIN_MAPPABLE)
		end = min_t(u64, end, i915_vm_to_ggtt(vma->vm)->mappable_end);
	if (flags & PIN_ZONE_4G)
		end = min_t(u64, end, (1ULL << 32) - I915_GTT_PAGE_SIZE);
	GEM_BUG_ON(!IS_ALIGNED(end, I915_GTT_PAGE_SIZE));

	/* If binding the object/GGTT view requires more space than the entire
	 * aperture has, reject it early before evicting everything in a vain
	 * attempt to find space.
	 */
	if (size > end) {
		DRM_DEBUG("Attempting to bind an object larger than the aperture: request=%llu > %s aperture=%llu\n",
			  size, flags & PIN_MAPPABLE ? "mappable" : "total",
			  end);
		return -ENOSPC;
	}

	color = 0;
	if (vma->obj && i915_vm_has_cache_coloring(vma->vm))
		color = vma->obj->cache_level;

	if (flags & PIN_OFFSET_FIXED) {
		u64 offset = flags & PIN_OFFSET_MASK;
		if (!IS_ALIGNED(offset, alignment) ||
		    range_overflows(offset, size, end))
			return -EINVAL;

		ret = i915_gem_gtt_reserve(vma->vm, &vma->node,
					   size, offset, color,
					   flags);
		if (ret)
			return ret;
	} else {
		/*
		 * We only support huge gtt pages through the 48b PPGTT,
		 * however we also don't want to force any alignment for
		 * objects which need to be tightly packed into the low 32bits.
		 *
		 * Note that we assume that GGTT are limited to 4GiB for the
		 * forseeable future. See also i915_ggtt_offset().
		 */
		if (upper_32_bits(end - 1) &&
		    vma->page_sizes.sg > I915_GTT_PAGE_SIZE) {
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

		ret = i915_gem_gtt_insert(vma->vm, &vma->node,
					  size, alignment, color,
					  start, end, flags);
		if (ret)
			return ret;

		GEM_BUG_ON(vma->node.start < start);
		GEM_BUG_ON(vma->node.start + vma->node.size > end);
	}
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(!i915_gem_valid_gtt_space(vma, color));

	if (vma->obj) {
		struct drm_i915_gem_object *obj = vma->obj;

		atomic_inc(&obj->bind_count);
		assert_bind_count(obj);
	}
	list_add_tail(&vma->vm_link, &vma->vm->bound_list);

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
	list_del(&vma->vm_link);
	if (vma->obj) {
		struct drm_i915_gem_object *obj = vma->obj;

		assert_bind_count(obj);
		atomic_dec(&obj->bind_count);
	}
}

static bool try_qad_pin(struct i915_vma *vma, unsigned int flags)
{
	unsigned int bound;
	bool pinned = true;

	bound = atomic_read(&vma->flags);
	do {
		if (unlikely(flags & ~bound))
			return false;

		if (unlikely(bound & (I915_VMA_OVERFLOW | I915_VMA_ERROR)))
			return false;

		if (!(bound & I915_VMA_PIN_MASK))
			goto unpinned;

		GEM_BUG_ON(((bound + 1) & I915_VMA_PIN_MASK) == 0);
	} while (!atomic_try_cmpxchg(&vma->flags, &bound, bound + 1));

	return true;

unpinned:
	/*
	 * If pin_count==0, but we are bound, check under the lock to avoid
	 * racing with a concurrent i915_vma_unbind().
	 */
	mutex_lock(&vma->vm->mutex);
	do {
		if (unlikely(bound & (I915_VMA_OVERFLOW | I915_VMA_ERROR))) {
			pinned = false;
			break;
		}

		if (unlikely(flags & ~bound)) {
			pinned = false;
			break;
		}
	} while (!atomic_try_cmpxchg(&vma->flags, &bound, bound + 1));
	mutex_unlock(&vma->vm->mutex);

	return pinned;
}

static int vma_get_pages(struct i915_vma *vma)
{
	int err = 0;

	if (atomic_add_unless(&vma->pages_count, 1, 0))
		return 0;

	/* Allocations ahoy! */
	if (mutex_lock_interruptible(&vma->pages_mutex))
		return -EINTR;

	if (!atomic_read(&vma->pages_count)) {
		if (vma->obj) {
			err = i915_gem_object_pin_pages(vma->obj);
			if (err)
				goto unlock;
		}

		err = vma->ops->set_pages(vma);
		if (err) {
			if (vma->obj)
				i915_gem_object_unpin_pages(vma->obj);
			goto unlock;
		}
	}
	atomic_inc(&vma->pages_count);

unlock:
	mutex_unlock(&vma->pages_mutex);

	return err;
}

static void __vma_put_pages(struct i915_vma *vma, unsigned int count)
{
	/* We allocate under vma_get_pages, so beware the shrinker */
	mutex_lock_nested(&vma->pages_mutex, SINGLE_DEPTH_NESTING);
	GEM_BUG_ON(atomic_read(&vma->pages_count) < count);
	if (atomic_sub_return(count, &vma->pages_count) == 0) {
		vma->ops->clear_pages(vma);
		GEM_BUG_ON(vma->pages);
		if (vma->obj)
			i915_gem_object_unpin_pages(vma->obj);
	}
	mutex_unlock(&vma->pages_mutex);
}

static void vma_put_pages(struct i915_vma *vma)
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

int i915_vma_pin(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	struct i915_vma_work *work = NULL;
	unsigned int bound;
	int err;

	BUILD_BUG_ON(PIN_GLOBAL != I915_VMA_GLOBAL_BIND);
	BUILD_BUG_ON(PIN_USER != I915_VMA_LOCAL_BIND);

	GEM_BUG_ON(flags & PIN_UPDATE);
	GEM_BUG_ON(!(flags & (PIN_USER | PIN_GLOBAL)));

	/* First try and grab the pin without rebinding the vma */
	if (try_qad_pin(vma, flags & I915_VMA_BIND_MASK))
		return 0;

	err = vma_get_pages(vma);
	if (err)
		return err;

	if (flags & vma->vm->bind_async_flags) {
		work = i915_vma_work();
		if (!work) {
			err = -ENOMEM;
			goto err_pages;
		}
	}

	/* No more allocations allowed once we hold vm->mutex */
	err = mutex_lock_interruptible(&vma->vm->mutex);
	if (err)
		goto err_fence;

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
		__i915_vma_pin(vma);
		goto err_unlock;
	}

	err = i915_active_acquire(&vma->active);
	if (err)
		goto err_unlock;

	if (!(bound & I915_VMA_BIND_MASK)) {
		err = i915_vma_insert(vma, size, alignment, flags);
		if (err)
			goto err_active;

		if (i915_is_ggtt(vma->vm))
			__i915_vma_set_map_and_fenceable(vma);
	}

	GEM_BUG_ON(!vma->pages);
	err = i915_vma_bind(vma,
			    vma->obj ? vma->obj->cache_level : 0,
			    flags, work);
	if (err)
		goto err_remove;

	/* There should only be at most 2 active bindings (user, global) */
	GEM_BUG_ON(bound + I915_VMA_PAGES_ACTIVE < bound);
	atomic_add(I915_VMA_PAGES_ACTIVE, &vma->pages_count);
	list_move_tail(&vma->vm_link, &vma->vm->bound_list);

	__i915_vma_pin(vma);
	GEM_BUG_ON(!i915_vma_is_pinned(vma));
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
err_fence:
	if (work)
		dma_fence_work_commit(&work->base);
err_pages:
	vma_put_pages(vma);
	return err;
}

void i915_vma_close(struct i915_vma *vma)
{
	struct intel_gt *gt = vma->vm->gt;
	unsigned long flags;

	GEM_BUG_ON(i915_vma_is_closed(vma));

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
	spin_lock_irqsave(&gt->closed_lock, flags);
	list_add(&vma->closed_link, &gt->closed_vma);
	spin_unlock_irqrestore(&gt->closed_lock, flags);
}

static void __i915_vma_remove_closed(struct i915_vma *vma)
{
	struct intel_gt *gt = vma->vm->gt;

	spin_lock_irq(&gt->closed_lock);
	list_del_init(&vma->closed_link);
	spin_unlock_irq(&gt->closed_lock);
}

void i915_vma_reopen(struct i915_vma *vma)
{
	if (i915_vma_is_closed(vma))
		__i915_vma_remove_closed(vma);
}

void i915_vma_destroy(struct i915_vma *vma)
{
	if (drm_mm_node_allocated(&vma->node)) {
		mutex_lock(&vma->vm->mutex);
		atomic_and(~I915_VMA_PIN_MASK, &vma->flags);
		WARN_ON(__i915_vma_unbind(vma));
		mutex_unlock(&vma->vm->mutex);
		GEM_BUG_ON(drm_mm_node_allocated(&vma->node));
	}
	GEM_BUG_ON(i915_vma_is_active(vma));

	if (vma->obj) {
		struct drm_i915_gem_object *obj = vma->obj;

		spin_lock(&obj->vma.lock);
		list_del(&vma->obj_link);
		rb_erase(&vma->obj_node, &obj->vma.tree);
		spin_unlock(&obj->vma.lock);
	}

	__i915_vma_remove_closed(vma);
	i915_vm_put(vma->vm);

	i915_active_fini(&vma->active);
	i915_vma_free(vma);
}

void i915_vma_parked(struct intel_gt *gt)
{
	struct i915_vma *vma, *next;

	spin_lock_irq(&gt->closed_lock);
	list_for_each_entry_safe(vma, next, &gt->closed_vma, closed_link) {
		struct drm_i915_gem_object *obj = vma->obj;
		struct i915_address_space *vm = vma->vm;

		/* XXX All to avoid keeping a reference on i915_vma itself */

		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		if (!i915_vm_tryopen(vm)) {
			i915_gem_object_put(obj);
			obj = NULL;
		}

		spin_unlock_irq(&gt->closed_lock);

		if (obj) {
			i915_vma_destroy(vma);
			i915_gem_object_put(obj);
		}

		i915_vm_close(vm);

		/* Restart after dropping lock */
		spin_lock_irq(&gt->closed_lock);
		next = list_first_entry(&gt->closed_vma,
					typeof(*next), closed_link);
	}
	spin_unlock_irq(&gt->closed_lock);
}

static void __i915_vma_iounmap(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_pinned(vma));

	if (vma->iomap == NULL)
		return;

	io_mapping_unmap(vma->iomap);
	vma->iomap = NULL;
}

void i915_vma_revoke_mmap(struct i915_vma *vma)
{
	struct drm_vma_offset_node *node = &vma->obj->base.vma_node;
	u64 vma_offset;

	lockdep_assert_held(&vma->vm->mutex);

	if (!i915_vma_has_userfault(vma))
		return;

	GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
	GEM_BUG_ON(!vma->obj->userfault_count);

	vma_offset = vma->ggtt_view.partial.offset << PAGE_SHIFT;
	unmap_mapping_range(vma->vm->i915->drm.anon_inode->i_mapping,
			    drm_vma_node_offset_addr(node) + vma_offset,
			    vma->size,
			    1);

	i915_vma_unset_userfault(vma);
	if (!--vma->obj->userfault_count)
		list_del(&vma->obj->userfault_link);
}

int __i915_vma_move_to_active(struct i915_vma *vma, struct i915_request *rq)
{
	int err;

	GEM_BUG_ON(!i915_vma_is_pinned(vma));

	/* Wait for the vma to be bound before we start! */
	err = i915_request_await_active(rq, &vma->active);
	if (err)
		return err;

	return i915_active_add_request(&vma->active, rq);
}

int i915_vma_move_to_active(struct i915_vma *vma,
			    struct i915_request *rq,
			    unsigned int flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	int err;

	assert_object_held(obj);

	err = __i915_vma_move_to_active(vma, rq);
	if (unlikely(err))
		return err;

	if (flags & EXEC_OBJECT_WRITE) {
		struct intel_frontbuffer *front;

		front = __intel_frontbuffer_get(obj);
		if (unlikely(front)) {
			if (intel_frontbuffer_invalidate(front, ORIGIN_CS))
				i915_active_add_request(&front->write, rq);
			intel_frontbuffer_put(front);
		}

		dma_resv_add_excl_fence(vma->resv, &rq->fence);
		obj->write_domain = I915_GEM_DOMAIN_RENDER;
		obj->read_domains = 0;
	} else {
		err = dma_resv_reserve_shared(vma->resv, 1);
		if (unlikely(err))
			return err;

		dma_resv_add_shared_fence(vma->resv, &rq->fence);
		obj->write_domain = 0;
	}
	obj->read_domains |= I915_GEM_GPU_DOMAINS;
	obj->mm.dirty = true;

	GEM_BUG_ON(!i915_vma_is_active(vma));
	return 0;
}

int __i915_vma_unbind(struct i915_vma *vma)
{
	int ret;

	lockdep_assert_held(&vma->vm->mutex);

	/*
	 * First wait upon any activity as retiring the request may
	 * have side-effects such as unpinning or even unbinding this vma.
	 *
	 * XXX Actually waiting under the vm->mutex is a hinderance and
	 * should be pipelined wherever possible. In cases where that is
	 * unavoidable, we should lift the wait to before the mutex.
	 */
	ret = i915_vma_sync(vma);
	if (ret)
		return ret;

	GEM_BUG_ON(i915_vma_is_active(vma));
	if (i915_vma_is_pinned(vma)) {
		vma_print_allocator(vma, "is pinned");
		return -EBUSY;
	}

	GEM_BUG_ON(i915_vma_is_active(vma));
	if (!drm_mm_node_allocated(&vma->node))
		return 0;

	if (i915_vma_is_map_and_fenceable(vma)) {
		/*
		 * Check that we have flushed all writes through the GGTT
		 * before the unbind, other due to non-strict nature of those
		 * indirect writes they may end up referencing the GGTT PTE
		 * after the unbind.
		 */
		i915_vma_flush_writes(vma);
		GEM_BUG_ON(i915_vma_has_ggtt_write(vma));

		/* release the fence reg _after_ flushing */
		ret = i915_vma_revoke_fence(vma);
		if (ret)
			return ret;

		/* Force a pagefault for domain tracking on next user access */
		i915_vma_revoke_mmap(vma);

		__i915_vma_iounmap(vma);
		clear_bit(I915_VMA_CAN_FENCE_BIT, __i915_vma_flags(vma));
	}
	GEM_BUG_ON(vma->fence);
	GEM_BUG_ON(i915_vma_has_userfault(vma));

	if (likely(atomic_read(&vma->vm->open))) {
		trace_i915_vma_unbind(vma);
		vma->ops->unbind_vma(vma);
	}
	atomic_and(~(I915_VMA_BIND_MASK | I915_VMA_ERROR), &vma->flags);

	i915_vma_detach(vma);
	vma_unbind_pages(vma);

	drm_mm_remove_node(&vma->node); /* pairs with i915_vma_destroy() */
	return 0;
}

int i915_vma_unbind(struct i915_vma *vma)
{
	struct i915_address_space *vm = vma->vm;
	int err;

	err = mutex_lock_interruptible(&vm->mutex);
	if (err)
		return err;

	err = __i915_vma_unbind(vma);
	mutex_unlock(&vm->mutex);

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

static void i915_global_vma_shrink(void)
{
	kmem_cache_shrink(global.slab_vmas);
}

static void i915_global_vma_exit(void)
{
	kmem_cache_destroy(global.slab_vmas);
}

static struct i915_global_vma global = { {
	.shrink = i915_global_vma_shrink,
	.exit = i915_global_vma_exit,
} };

int __init i915_global_vma_init(void)
{
	global.slab_vmas = KMEM_CACHE(i915_vma, SLAB_HWCACHE_ALIGN);
	if (!global.slab_vmas)
		return -ENOMEM;

	i915_global_register(&global.base);
	return 0;
}
