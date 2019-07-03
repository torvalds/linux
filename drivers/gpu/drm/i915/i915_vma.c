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
	i915_vma_get(active_to_vma(ref));
	return 0;
}

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
	GEM_BUG_ON(vm == &vm->i915->mm.aliasing_ppgtt->vm);

	vma = i915_vma_alloc();
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	vma->vm = vm;
	vma->ops = &vm->vma_ops;
	vma->obj = obj;
	vma->resv = obj->base.resv;
	vma->size = obj->base.size;
	vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	i915_active_init(vm->i915, &vma->active,
			 __i915_vma_active, __i915_vma_retire);
	INIT_ACTIVE_REQUEST(&vma->last_fence);

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

		vma->flags |= I915_VMA_GGTT;
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

	mutex_lock(&vm->mutex);
	list_add(&vma->vm_link, &vm->unbound_list);
	mutex_unlock(&vm->mutex);

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

	GEM_BUG_ON(view && !i915_is_ggtt(vm));
	GEM_BUG_ON(vm->closed);

	spin_lock(&obj->vma.lock);
	vma = vma_lookup(obj, vm, view);
	spin_unlock(&obj->vma.lock);

	/* vma_create() will resolve the race if another creates the vma */
	if (unlikely(!vma))
		vma = vma_create(obj, vm, view);

	GEM_BUG_ON(!IS_ERR(vma) && i915_vma_compare(vma, vm, view));
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

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(vma->size > vma->node.size);

	if (GEM_DEBUG_WARN_ON(range_overflows(vma->node.start,
					      vma->node.size,
					      vma->vm->total)))
		return -ENODEV;

	if (GEM_DEBUG_WARN_ON(!flags))
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

	GEM_BUG_ON(!vma->pages);

	trace_i915_vma_bind(vma, bind_flags);
	ret = vma->ops->bind_vma(vma, cache_level, bind_flags);
	if (ret)
		return ret;

	vma->flags |= bind_flags;
	return 0;
}

void __iomem *i915_vma_pin_iomap(struct i915_vma *vma)
{
	void __iomem *ptr;
	int err;

	/* Access through the GTT requires the device to be awake. */
	assert_rpm_wakelock_held(&vma->vm->i915->runtime_pm);

	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);
	if (WARN_ON(!i915_vma_is_map_and_fenceable(vma))) {
		err = -ENODEV;
		goto err;
	}

	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON((vma->flags & I915_VMA_GLOBAL_BIND) == 0);

	ptr = vma->iomap;
	if (ptr == NULL) {
		ptr = io_mapping_map_wc(&i915_vm_to_ggtt(vma->vm)->iomap,
					vma->node.start,
					vma->node.size);
		if (ptr == NULL) {
			err = -ENOMEM;
			goto err;
		}

		vma->iomap = ptr;
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
	if (!i915_vma_has_ggtt_write(vma))
		return;

	intel_gt_flush_ggtt_writes(vma->vm->gt);

	i915_vma_unset_ggtt_write(vma);
}

void i915_vma_unpin_iomap(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

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
	struct drm_i915_private *dev_priv = vma->vm->i915;
	unsigned int cache_level;
	u64 start, end;
	int ret;

	GEM_BUG_ON(i915_vma_is_closed(vma));
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
		DRM_DEBUG("Attempting to bind an object larger than the aperture: request=%llu > %s aperture=%llu\n",
			  size, flags & PIN_MAPPABLE ? "mappable" : "total",
			  end);
		return -ENOSPC;
	}

	if (vma->obj) {
		ret = i915_gem_object_pin_pages(vma->obj);
		if (ret)
			return ret;

		cache_level = vma->obj->cache_level;
	} else {
		cache_level = 0;
	}

	GEM_BUG_ON(vma->pages);

	ret = vma->ops->set_pages(vma);
	if (ret)
		goto err_unpin;

	if (flags & PIN_OFFSET_FIXED) {
		u64 offset = flags & PIN_OFFSET_MASK;
		if (!IS_ALIGNED(offset, alignment) ||
		    range_overflows(offset, size, end)) {
			ret = -EINVAL;
			goto err_clear;
		}

		ret = i915_gem_gtt_reserve(vma->vm, &vma->node,
					   size, offset, cache_level,
					   flags);
		if (ret)
			goto err_clear;
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
					  size, alignment, cache_level,
					  start, end, flags);
		if (ret)
			goto err_clear;

		GEM_BUG_ON(vma->node.start < start);
		GEM_BUG_ON(vma->node.start + vma->node.size > end);
	}
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(!i915_gem_valid_gtt_space(vma, cache_level));

	mutex_lock(&vma->vm->mutex);
	list_move_tail(&vma->vm_link, &vma->vm->bound_list);
	mutex_unlock(&vma->vm->mutex);

	if (vma->obj) {
		atomic_inc(&vma->obj->bind_count);
		assert_bind_count(vma->obj);
	}

	return 0;

err_clear:
	vma->ops->clear_pages(vma);
err_unpin:
	if (vma->obj)
		i915_gem_object_unpin_pages(vma->obj);
	return ret;
}

static void
i915_vma_remove(struct i915_vma *vma)
{
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	GEM_BUG_ON(vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));

	vma->ops->clear_pages(vma);

	mutex_lock(&vma->vm->mutex);
	drm_mm_remove_node(&vma->node);
	list_move_tail(&vma->vm_link, &vma->vm->unbound_list);
	mutex_unlock(&vma->vm->mutex);

	/*
	 * Since the unbound list is global, only move to that list if
	 * no more VMAs exist.
	 */
	if (vma->obj) {
		struct drm_i915_gem_object *obj = vma->obj;

		atomic_dec(&obj->bind_count);

		/*
		 * And finally now the object is completely decoupled from this
		 * vma, we can drop its hold on the backing storage and allow
		 * it to be reaped by the shrinker.
		 */
		i915_gem_object_unpin_pages(obj);
		assert_bind_count(obj);
	}
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
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	ret = i915_vma_bind(vma, vma->obj ? vma->obj->cache_level : 0, flags);
	if (ret)
		goto err_remove;

	GEM_BUG_ON((vma->flags & I915_VMA_BIND_MASK) == 0);

	if ((bound ^ vma->flags) & I915_VMA_GLOBAL_BIND)
		__i915_vma_set_map_and_fenceable(vma);

	GEM_BUG_ON(i915_vma_misplaced(vma, size, alignment, flags));
	return 0;

err_remove:
	if ((bound & I915_VMA_BIND_MASK) == 0) {
		i915_vma_remove(vma);
		GEM_BUG_ON(vma->pages);
		GEM_BUG_ON(vma->flags & I915_VMA_BIND_MASK);
	}
err_unpin:
	__i915_vma_unpin(vma);
	return ret;
}

void i915_vma_close(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;
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
	spin_lock_irqsave(&i915->gt.closed_lock, flags);
	list_add(&vma->closed_link, &i915->gt.closed_vma);
	spin_unlock_irqrestore(&i915->gt.closed_lock, flags);
}

static void __i915_vma_remove_closed(struct i915_vma *vma)
{
	struct drm_i915_private *i915 = vma->vm->i915;

	if (!i915_vma_is_closed(vma))
		return;

	spin_lock_irq(&i915->gt.closed_lock);
	list_del_init(&vma->closed_link);
	spin_unlock_irq(&i915->gt.closed_lock);
}

void i915_vma_reopen(struct i915_vma *vma)
{
	__i915_vma_remove_closed(vma);
}

static void __i915_vma_destroy(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->node.allocated);
	GEM_BUG_ON(vma->fence);

	GEM_BUG_ON(i915_active_request_isset(&vma->last_fence));

	mutex_lock(&vma->vm->mutex);
	list_del(&vma->vm_link);
	mutex_unlock(&vma->vm->mutex);

	if (vma->obj) {
		struct drm_i915_gem_object *obj = vma->obj;

		spin_lock(&obj->vma.lock);
		list_del(&vma->obj_link);
		rb_erase(&vma->obj_node, &vma->obj->vma.tree);
		spin_unlock(&obj->vma.lock);
	}

	i915_active_fini(&vma->active);

	i915_vma_free(vma);
}

void i915_vma_destroy(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

	GEM_BUG_ON(i915_vma_is_pinned(vma));

	__i915_vma_remove_closed(vma);

	WARN_ON(i915_vma_unbind(vma));
	GEM_BUG_ON(i915_vma_is_active(vma));

	__i915_vma_destroy(vma);
}

void i915_vma_parked(struct drm_i915_private *i915)
{
	struct i915_vma *vma, *next;

	spin_lock_irq(&i915->gt.closed_lock);
	list_for_each_entry_safe(vma, next, &i915->gt.closed_vma, closed_link) {
		list_del_init(&vma->closed_link);
		spin_unlock_irq(&i915->gt.closed_lock);

		i915_vma_destroy(vma);

		spin_lock_irq(&i915->gt.closed_lock);
	}
	spin_unlock_irq(&i915->gt.closed_lock);
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

	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

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

static void export_fence(struct i915_vma *vma,
			 struct i915_request *rq,
			 unsigned int flags)
{
	struct reservation_object *resv = vma->resv;

	/*
	 * Ignore errors from failing to allocate the new fence, we can't
	 * handle an error right now. Worst case should be missed
	 * synchronisation leading to rendering corruption.
	 */
	if (flags & EXEC_OBJECT_WRITE)
		reservation_object_add_excl_fence(resv, &rq->fence);
	else if (reservation_object_reserve_shared(resv, 1) == 0)
		reservation_object_add_shared_fence(resv, &rq->fence);
}

int i915_vma_move_to_active(struct i915_vma *vma,
			    struct i915_request *rq,
			    unsigned int flags)
{
	struct drm_i915_gem_object *obj = vma->obj;
	int err;

	assert_vma_held(vma);
	assert_object_held(obj);
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	/*
	 * Add a reference if we're newly entering the active list.
	 * The order in which we add operations to the retirement queue is
	 * vital here: mark_active adds to the start of the callback list,
	 * such that subsequent callbacks are called first. Therefore we
	 * add the active reference first and queue for it to be dropped
	 * *last*.
	 */
	err = i915_active_ref(&vma->active, rq->fence.context, rq);
	if (unlikely(err))
		return err;

	obj->write_domain = 0;
	if (flags & EXEC_OBJECT_WRITE) {
		obj->write_domain = I915_GEM_DOMAIN_RENDER;

		if (intel_fb_obj_invalidate(obj, ORIGIN_CS))
			__i915_active_request_set(&obj->frontbuffer_write, rq);

		obj->read_domains = 0;
	}
	obj->read_domains |= I915_GEM_GPU_DOMAINS;
	obj->mm.dirty = true;

	if (flags & EXEC_OBJECT_NEEDS_FENCE)
		__i915_active_request_set(&vma->last_fence, rq);

	export_fence(vma, rq, flags);

	GEM_BUG_ON(!i915_vma_is_active(vma));
	return 0;
}

int i915_vma_unbind(struct i915_vma *vma)
{
	int ret;

	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

	/*
	 * First wait upon any activity as retiring the request may
	 * have side-effects such as unpinning or even unbinding this vma.
	 */
	might_sleep();
	if (i915_vma_is_active(vma)) {
		/*
		 * When a closed VMA is retired, it is unbound - eek.
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

		ret = i915_active_wait(&vma->active);
		if (ret)
			goto unpin;

		ret = i915_active_request_retire(&vma->last_fence,
					      &vma->vm->i915->drm.struct_mutex);
unpin:
		__i915_vma_unpin(vma);
		if (ret)
			return ret;
	}
	GEM_BUG_ON(i915_vma_is_active(vma));

	if (i915_vma_is_pinned(vma)) {
		vma_print_allocator(vma, "is pinned");
		return -EBUSY;
	}

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
		ret = i915_vma_put_fence(vma);
		if (ret)
			return ret;

		/* Force a pagefault for domain tracking on next user access */
		i915_vma_revoke_mmap(vma);

		__i915_vma_iounmap(vma);
		vma->flags &= ~I915_VMA_CAN_FENCE;
	}
	GEM_BUG_ON(vma->fence);
	GEM_BUG_ON(i915_vma_has_userfault(vma));

	if (likely(!vma->vm->closed)) {
		trace_i915_vma_unbind(vma);
		vma->ops->unbind_vma(vma);
	}
	vma->flags &= ~(I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND);

	i915_vma_remove(vma);

	return 0;
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
