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
 
#include "i915_vma.h"

#include "i915_drv.h"
#include "intel_ringbuffer.h"
#include "intel_frontbuffer.h"

#include <drm/drm_gem.h>

static void
i915_vma_retire(struct i915_gem_active *active,
		struct drm_i915_gem_request *rq)
{
	const unsigned int idx = rq->engine->id;
	struct i915_vma *vma =
		container_of(active, struct i915_vma, last_read[idx]);
	struct drm_i915_gem_object *obj = vma->obj;

	GEM_BUG_ON(!i915_vma_has_active_engine(vma, idx));

	i915_vma_clear_active(vma, idx);
	if (i915_vma_is_active(vma))
		return;

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	if (unlikely(i915_vma_is_closed(vma) && !i915_vma_is_pinned(vma)))
		WARN_ON(i915_vma_unbind(vma));

	GEM_BUG_ON(!i915_gem_object_is_active(obj));
	if (--obj->active_count)
		return;

	/* Bump our place on the bound list to keep it roughly in LRU order
	 * so that we don't steal from recently used but inactive objects
	 * (unless we are forced to ofc!)
	 */
	if (obj->bind_count)
		list_move_tail(&obj->global_link, &rq->i915->mm.bound_list);

	obj->mm.dirty = true; /* be paranoid  */

	if (i915_gem_object_has_active_reference(obj)) {
		i915_gem_object_clear_active_reference(obj);
		i915_gem_object_put(obj);
	}
}

static struct i915_vma *
vma_create(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;
	struct rb_node *rb, **p;
	int i;

	vma = kmem_cache_zalloc(vm->i915->vmas, GFP_KERNEL);
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&vma->exec_list);
	for (i = 0; i < ARRAY_SIZE(vma->last_read); i++)
		init_request_active(&vma->last_read[i], i915_vma_retire);
	init_request_active(&vma->last_fence, NULL);
	vma->vm = vm;
	vma->obj = obj;
	vma->size = obj->base.size;
	vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	if (view && view->type != I915_GGTT_VIEW_NORMAL) {
		vma->ggtt_view = *view;
		if (view->type == I915_GGTT_VIEW_PARTIAL) {
			GEM_BUG_ON(range_overflows_t(u64,
						     view->partial.offset,
						     view->partial.size,
						     obj->base.size >> PAGE_SHIFT));
			vma->size = view->partial.size;
			vma->size <<= PAGE_SHIFT;
			GEM_BUG_ON(vma->size >= obj->base.size);
		} else if (view->type == I915_GGTT_VIEW_ROTATED) {
			vma->size = intel_rotation_info_size(&view->rotated);
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

		vma->flags |= I915_VMA_GGTT;
		list_add(&vma->obj_link, &obj->vma_list);
	} else {
		i915_ppgtt_get(i915_vm_to_ppgtt(vm));
		list_add_tail(&vma->obj_link, &obj->vma_list);
	}

	rb = NULL;
	p = &obj->vma_tree.rb_node;
	while (*p) {
		struct i915_vma *pos;

		rb = *p;
		pos = rb_entry(rb, struct i915_vma, obj_node);
		if (i915_vma_compare(pos, vm, view) < 0)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&vma->obj_node, rb, p);
	rb_insert_color(&vma->obj_node, &obj->vma_tree);
	list_add(&vma->vm_link, &vm->unbound_list);

	return vma;

err_vma:
	kmem_cache_free(vm->i915->vmas, vma);
	return ERR_PTR(-E2BIG);
}

static struct i915_vma *
vma_lookup(struct drm_i915_gem_object *obj,
	   struct i915_address_space *vm,
	   const struct i915_ggtt_view *view)
{
	struct rb_node *rb;

	rb = obj->vma_tree.rb_node;
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
 * Must be called with struct_mutex held.
 *
 * Returns the vma, or an error pointer.
 */
struct i915_vma *
i915_vma_instance(struct drm_i915_gem_object *obj,
		  struct i915_address_space *vm,
		  const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;

	lockdep_assert_held(&obj->base.dev->struct_mutex);
	GEM_BUG_ON(view && !i915_is_ggtt(vm));
	GEM_BUG_ON(vm->closed);

	vma = vma_lookup(obj, vm, view);
	if (!vma)
		vma = vma_create(obj, vm, view);

	GEM_BUG_ON(!IS_ERR(vma) && i915_vma_is_closed(vma));
	GEM_BUG_ON(!IS_ERR(vma) && i915_vma_compare(vma, vm, view));
	GEM_BUG_ON(!IS_ERR(vma) && vma_lookup(obj, vm, view) != vma);
	return vma;
}

/**
 * i915_vma_bind - Sets up PTEs for an VMA in it's corresponding address space.
 * @vma: VMA to map
 * @cache_level: mapping cache level
 * @flags: flags like global or local mapping
 *
 * DMA addresses are taken from the scatter-gather table of this object (or of
 * this VMA in case of non-default GGTT views) and PTE entries set up.
 * Note that DMA addresses are also the only part of the SG table we care about.
 */
int i915_vma_bind(struct i915_vma *vma, enum i915_cache_level cache_level,
		  u32 flags)
{
	u32 bind_flags;
	u32 vma_flags;
	int ret;

	if (WARN_ON(flags == 0))
		return -EINVAL;

	bind_flags = 0;
	if (flags & PIN_GLOBAL)
		bind_flags |= I915_VMA_GLOBAL_BIND;
	if (flags & PIN_USER)
		bind_flags |= I915_VMA_LOCAL_BIND;

	vma_flags = vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND);
	if (flags & PIN_UPDATE)
		bind_flags |= vma_flags;
	else
		bind_flags &= ~vma_flags;
	if (bind_flags == 0)
		return 0;

	if (GEM_WARN_ON(range_overflows(vma->node.start,
					vma->node.size,
					vma->vm->total)))
		return -ENODEV;

	if (vma_flags == 0 && vma->vm->allocate_va_range) {
		trace_i915_va_alloc(vma);
		ret = vma->vm->allocate_va_range(vma->vm,
						 vma->node.start,
						 vma->node.size);
		if (ret)
			return ret;
	}

	trace_i915_vma_bind(vma, bind_flags);
	ret = vma->vm->bind_vma(vma, cache_level, bind_flags);
	if (ret)
		return ret;

	vma->flags |= bind_flags;
	return 0;
}

void __iomem *i915_vma_pin_iomap(struct i915_vma *vma)
{
	void __iomem *ptr;

	/* Access through the GTT requires the device to be awake. */
	assert_rpm_wakelock_held(vma->vm->i915);

	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);
	if (WARN_ON(!i915_vma_is_map_and_fenceable(vma)))
		return IO_ERR_PTR(-ENODEV);

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON((vma->flags & I915_VMA_GLOBAL_BIND) == 0);

	ptr = vma->iomap;
	if (ptr == NULL) {
		ptr = io_mapping_map_wc(&i915_vm_to_ggtt(vma->vm)->mappable,
					vma->node.start,
					vma->node.size);
		if (ptr == NULL)
			return IO_ERR_PTR(-ENOMEM);

		vma->iomap = ptr;
	}

	__i915_vma_pin(vma);
	return ptr;
}

void i915_vma_unpin_and_release(struct i915_vma **p_vma)
{
	struct i915_vma *vma;
	struct drm_i915_gem_object *obj;

	vma = fetch_and_zero(p_vma);
	if (!vma)
		return;

	obj = vma->obj;

	i915_vma_unpin(vma);
	i915_vma_close(vma);

	__i915_gem_object_release_unless_active(obj);
}

bool
i915_vma_misplaced(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	if (!drm_mm_node_allocated(&vma->node))
		return false;

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

	/*
	 * Explicitly disable for rotated VMA since the display does not
	 * need the fence and the VMA is not accessible to other users.
	 */
	if (vma->ggtt_view.type == I915_GGTT_VIEW_ROTATED)
		return;

	fenceable = (vma->node.size >= vma->fence_size &&
		     IS_ALIGNED(vma->node.start, vma->fence_alignment));

	mappable = vma->node.start + vma->fence_size <= i915_vm_to_ggtt(vma->vm)->mappable_end;

	if (mappable && fenceable)
		vma->flags |= I915_VMA_CAN_FENCE;
	else
		vma->flags &= ~I915_VMA_CAN_FENCE;
}

static bool color_differs(struct drm_mm_node *node, unsigned long color)
{
	return node->allocated && node->color != color;
}

bool i915_gem_valid_gtt_space(struct i915_vma *vma, unsigned long cache_level)
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
	if (vma->vm->mm.color_adjust == NULL)
		return true;

	/* Only valid to be called on an already inserted vma */
	GEM_BUG_ON(!drm_mm_node_allocated(node));
	GEM_BUG_ON(list_empty(&node->node_list));

	other = list_prev_entry(node, node_list);
	if (color_differs(other, cache_level) && !drm_mm_hole_follows(other))
		return false;

	other = list_next_entry(node, node_list);
	if (color_differs(other, cache_level) && !drm_mm_hole_follows(node))
		return false;

	return true;
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
	struct drm_i915_private *dev_priv = vma->vm->i915;
	struct drm_i915_gem_object *obj = vma->obj;
	u64 start, end;
	int ret;

	GEM_BUG_ON(vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));
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
		end = min_t(u64, end, dev_priv->ggtt.mappable_end);
	if (flags & PIN_ZONE_4G)
		end = min_t(u64, end, (1ULL << 32) - I915_GTT_PAGE_SIZE);
	GEM_BUG_ON(!IS_ALIGNED(end, I915_GTT_PAGE_SIZE));

	/* If binding the object/GGTT view requires more space than the entire
	 * aperture has, reject it early before evicting everything in a vain
	 * attempt to find space.
	 */
	if (size > end) {
		DRM_DEBUG("Attempting to bind an object larger than the aperture: request=%llu [object=%zd] > %s aperture=%llu\n",
			  size, obj->base.size,
			  flags & PIN_MAPPABLE ? "mappable" : "total",
			  end);
		return -E2BIG;
	}

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ret;

	if (flags & PIN_OFFSET_FIXED) {
		u64 offset = flags & PIN_OFFSET_MASK;
		if (!IS_ALIGNED(offset, alignment) ||
		    range_overflows(offset, size, end)) {
			ret = -EINVAL;
			goto err_unpin;
		}

		ret = i915_gem_gtt_reserve(vma->vm, &vma->node,
					   size, offset, obj->cache_level,
					   flags);
		if (ret)
			goto err_unpin;
	} else {
		ret = i915_gem_gtt_insert(vma->vm, &vma->node,
					  size, alignment, obj->cache_level,
					  start, end, flags);
		if (ret)
			goto err_unpin;

		GEM_BUG_ON(vma->node.start < start);
		GEM_BUG_ON(vma->node.start + vma->node.size > end);
	}
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(!i915_gem_valid_gtt_space(vma, obj->cache_level));

	list_move_tail(&obj->global_link, &dev_priv->mm.bound_list);
	list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	obj->bind_count++;
	GEM_BUG_ON(atomic_read(&obj->mm.pages_pin_count) < obj->bind_count);

	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

static void
i915_vma_remove(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));

	drm_mm_remove_node(&vma->node);
	list_move_tail(&vma->vm_link, &vma->vm->unbound_list);

	/* Since the unbound list is global, only move to that list if
	 * no more VMAs exist.
	 */
	if (--obj->bind_count == 0)
		list_move_tail(&obj->global_link,
			       &to_i915(obj->base.dev)->mm.unbound_list);

	/* And finally now the object is completely decoupled from this vma,
	 * we can drop its hold on the backing storage and allow it to be
	 * reaped by the shrinker.
	 */
	i915_gem_object_unpin_pages(obj);
	GEM_BUG_ON(atomic_read(&obj->mm.pages_pin_count) < obj->bind_count);
}

int __i915_vma_do_pin(struct i915_vma *vma,
		      u64 size, u64 alignment, u64 flags)
{
	const unsigned int bound = vma->flags;
	int ret;

	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);
	GEM_BUG_ON((flags & (PIN_GLOBAL | PIN_USER)) == 0);
	GEM_BUG_ON((flags & PIN_GLOBAL) && !i915_vma_is_ggtt(vma));

	if (WARN_ON(bound & I915_VMA_PIN_OVERFLOW)) {
		ret = -EBUSY;
		goto err_unpin;
	}

	if ((bound & I915_VMA_BIND_MASK) == 0) {
		ret = i915_vma_insert(vma, size, alignment, flags);
		if (ret)
			goto err_unpin;
	}

	ret = i915_vma_bind(vma, vma->obj->cache_level, flags);
	if (ret)
		goto err_remove;

	if ((bound ^ vma->flags) & I915_VMA_GLOBAL_BIND)
		__i915_vma_set_map_and_fenceable(vma);

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(i915_vma_misplaced(vma, size, alignment, flags));
	return 0;

err_remove:
	if ((bound & I915_VMA_BIND_MASK) == 0) {
		GEM_BUG_ON(vma->pages);
		i915_vma_remove(vma);
	}
err_unpin:
	__i915_vma_unpin(vma);
	return ret;
}

void i915_vma_destroy(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->node.allocated);
	GEM_BUG_ON(i915_vma_is_active(vma));
	GEM_BUG_ON(!i915_vma_is_closed(vma));
	GEM_BUG_ON(vma->fence);

	list_del(&vma->vm_link);
	if (!i915_vma_is_ggtt(vma))
		i915_ppgtt_put(i915_vm_to_ppgtt(vma->vm));

	kmem_cache_free(to_i915(vma->obj->base.dev)->vmas, vma);
}

void i915_vma_close(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_closed(vma));
	vma->flags |= I915_VMA_CLOSED;

	list_del(&vma->obj_link);
	rb_erase(&vma->obj_node, &vma->obj->vma_tree);

	if (!i915_vma_is_active(vma) && !i915_vma_is_pinned(vma))
		WARN_ON(i915_vma_unbind(vma));
}

static void __i915_vma_iounmap(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_pinned(vma));

	if (vma->iomap == NULL)
		return;

	io_mapping_unmap(vma->iomap);
	vma->iomap = NULL;
}

int i915_vma_unbind(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;
	unsigned long active;
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	/* First wait upon any activity as retiring the request may
	 * have side-effects such as unpinning or even unbinding this vma.
	 */
	active = i915_vma_get_active(vma);
	if (active) {
		int idx;

		/* When a closed VMA is retired, it is unbound - eek.
		 * In order to prevent it from being recursively closed,
		 * take a pin on the vma so that the second unbind is
		 * aborted.
		 *
		 * Even more scary is that the retire callback may free
		 * the object (last active vma). To prevent the explosion
		 * we defer the actual object free to a worker that can
		 * only proceed once it acquires the struct_mutex (which
		 * we currently hold, therefore it cannot free this object
		 * before we are finished).
		 */
		__i915_vma_pin(vma);

		for_each_active(active, idx) {
			ret = i915_gem_active_retire(&vma->last_read[idx],
						     &vma->vm->i915->drm.struct_mutex);
			if (ret)
				break;
		}

		__i915_vma_unpin(vma);
		if (ret)
			return ret;

		GEM_BUG_ON(i915_vma_is_active(vma));
	}

	if (i915_vma_is_pinned(vma))
		return -EBUSY;

	if (!drm_mm_node_allocated(&vma->node))
		goto destroy;

	GEM_BUG_ON(obj->bind_count == 0);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	if (i915_vma_is_map_and_fenceable(vma)) {
		/* release the fence reg _after_ flushing */
		ret = i915_vma_put_fence(vma);
		if (ret)
			return ret;

		/* Force a pagefault for domain tracking on next user access */
		i915_gem_release_mmap(obj);

		__i915_vma_iounmap(vma);
		vma->flags &= ~I915_VMA_CAN_FENCE;
	}

	if (likely(!vma->vm->closed)) {
		trace_i915_vma_unbind(vma);
		vma->vm->unbind_vma(vma);
	}
	vma->flags &= ~(I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND);

	if (vma->pages != obj->mm.pages) {
		GEM_BUG_ON(!vma->pages);
		sg_free_table(vma->pages);
		kfree(vma->pages);
	}
	vma->pages = NULL;

	i915_vma_remove(vma);

destroy:
	if (unlikely(i915_vma_is_closed(vma)))
		i915_vma_destroy(vma);

	return 0;
}

