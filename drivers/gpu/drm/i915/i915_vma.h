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

#ifndef __I915_VMA_H__
#define __I915_VMA_H__

#include <linux/io-mapping.h>

#include <drm/drm_mm.h>

#include "i915_gem_gtt.h"
#include "i915_gem_fence_reg.h"
#include "i915_gem_object.h"
#include "i915_gem_request.h"


enum i915_cache_level;

/**
 * A VMA represents a GEM BO that is bound into an address space. Therefore, a
 * VMA's presence cannot be guaranteed before binding, or after unbinding the
 * object into/from the address space.
 *
 * To make things as simple as possible (ie. no refcounting), a VMA's lifetime
 * will always be <= an objects lifetime. So object refcounting should cover us.
 */
struct i915_vma {
	struct drm_mm_node node;
	struct drm_i915_gem_object *obj;
	struct i915_address_space *vm;
	struct drm_i915_fence_reg *fence;
	struct sg_table *pages;
	void __iomem *iomap;
	u64 size;
	u64 display_alignment;

	unsigned int flags;
	/**
	 * How many users have pinned this object in GTT space. The following
	 * users can each hold at most one reference: pwrite/pread, execbuffer
	 * (objects are not allowed multiple times for the same batchbuffer),
	 * and the framebuffer code. When switching/pageflipping, the
	 * framebuffer code has at most two buffers pinned per crtc.
	 *
	 * In the worst case this is 1 + 1 + 1 + 2*2 = 7. That would fit into 3
	 * bits with absolutely no headroom. So use 4 bits.
	 */
#define I915_VMA_PIN_MASK 0xf
#define I915_VMA_PIN_OVERFLOW	BIT(5)

	/** Flags and address space this VMA is bound to */
#define I915_VMA_GLOBAL_BIND	BIT(6)
#define I915_VMA_LOCAL_BIND	BIT(7)
#define I915_VMA_BIND_MASK (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND | I915_VMA_PIN_OVERFLOW)

#define I915_VMA_GGTT		BIT(8)
#define I915_VMA_CAN_FENCE	BIT(9)
#define I915_VMA_CLOSED		BIT(10)

	unsigned int active;
	struct i915_gem_active last_read[I915_NUM_ENGINES];
	struct i915_gem_active last_write;
	struct i915_gem_active last_fence;

	/**
	 * Support different GGTT views into the same object.
	 * This means there can be multiple VMA mappings per object and per VM.
	 * i915_ggtt_view_type is used to distinguish between those entries.
	 * The default one of zero (I915_GGTT_VIEW_NORMAL) is default and also
	 * assumed in GEM functions which take no ggtt view parameter.
	 */
	struct i915_ggtt_view ggtt_view;

	/** This object's place on the active/inactive lists */
	struct list_head vm_link;

	struct list_head obj_link; /* Link in the object's VMA list */
	struct rb_node obj_node;

	/** This vma's place in the batchbuffer or on the eviction list */
	struct list_head exec_list;

	/**
	 * Used for performing relocations during execbuffer insertion.
	 */
	struct hlist_node exec_node;
	unsigned long exec_handle;
	struct drm_i915_gem_exec_object2 *exec_entry;
};

struct i915_vma *
i915_vma_create(struct drm_i915_gem_object *obj,
		struct i915_address_space *vm,
		const struct i915_ggtt_view *view);

void i915_vma_unpin_and_release(struct i915_vma **p_vma);

static inline bool i915_vma_is_ggtt(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_GGTT;
}

static inline bool i915_vma_is_map_and_fenceable(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_CAN_FENCE;
}

static inline bool i915_vma_is_closed(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_CLOSED;
}

static inline unsigned int i915_vma_get_active(const struct i915_vma *vma)
{
	return vma->active;
}

static inline bool i915_vma_is_active(const struct i915_vma *vma)
{
	return i915_vma_get_active(vma);
}

static inline void i915_vma_set_active(struct i915_vma *vma,
				       unsigned int engine)
{
	vma->active |= BIT(engine);
}

static inline void i915_vma_clear_active(struct i915_vma *vma,
					 unsigned int engine)
{
	vma->active &= ~BIT(engine);
}

static inline bool i915_vma_has_active_engine(const struct i915_vma *vma,
					      unsigned int engine)
{
	return vma->active & BIT(engine);
}

static inline u32 i915_ggtt_offset(const struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!vma->node.allocated);
	GEM_BUG_ON(upper_32_bits(vma->node.start));
	GEM_BUG_ON(upper_32_bits(vma->node.start + vma->node.size - 1));
	return lower_32_bits(vma->node.start);
}

static inline struct i915_vma *i915_vma_get(struct i915_vma *vma)
{
	i915_gem_object_get(vma->obj);
	return vma;
}

static inline void i915_vma_put(struct i915_vma *vma)
{
	i915_gem_object_put(vma->obj);
}

static inline long
i915_vma_compare(struct i915_vma *vma,
		 struct i915_address_space *vm,
		 const struct i915_ggtt_view *view)
{
	GEM_BUG_ON(view && !i915_vma_is_ggtt(vma));

	if (vma->vm != vm)
		return vma->vm - vm;

	if (!view)
		return vma->ggtt_view.type;

	if (vma->ggtt_view.type != view->type)
		return vma->ggtt_view.type - view->type;

	return memcmp(&vma->ggtt_view.params,
		      &view->params,
		      sizeof(view->params));
}

int i915_vma_bind(struct i915_vma *vma, enum i915_cache_level cache_level,
		  u32 flags);
bool i915_gem_valid_gtt_space(struct i915_vma *vma, unsigned long cache_level);
bool
i915_vma_misplaced(struct i915_vma *vma, u64 size, u64 alignment, u64 flags);
void __i915_vma_set_map_and_fenceable(struct i915_vma *vma);
int __must_check i915_vma_unbind(struct i915_vma *vma);
void i915_vma_close(struct i915_vma *vma);
void i915_vma_destroy(struct i915_vma *vma);

int __i915_vma_do_pin(struct i915_vma *vma,
		      u64 size, u64 alignment, u64 flags);
static inline int __must_check
i915_vma_pin(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	BUILD_BUG_ON(PIN_MBZ != I915_VMA_PIN_OVERFLOW);
	BUILD_BUG_ON(PIN_GLOBAL != I915_VMA_GLOBAL_BIND);
	BUILD_BUG_ON(PIN_USER != I915_VMA_LOCAL_BIND);

	/* Pin early to prevent the shrinker/eviction logic from destroying
	 * our vma as we insert and bind.
	 */
	if (likely(((++vma->flags ^ flags) & I915_VMA_BIND_MASK) == 0))
		return 0;

	return __i915_vma_do_pin(vma, size, alignment, flags);
}

static inline int i915_vma_pin_count(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_PIN_MASK;
}

static inline bool i915_vma_is_pinned(const struct i915_vma *vma)
{
	return i915_vma_pin_count(vma);
}

static inline void __i915_vma_pin(struct i915_vma *vma)
{
	vma->flags++;
	GEM_BUG_ON(vma->flags & I915_VMA_PIN_OVERFLOW);
}

static inline void __i915_vma_unpin(struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_pinned(vma));
	vma->flags--;
}

static inline void i915_vma_unpin(struct i915_vma *vma)
{
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	__i915_vma_unpin(vma);
}

/**
 * i915_vma_pin_iomap - calls ioremap_wc to map the GGTT VMA via the aperture
 * @vma: VMA to iomap
 *
 * The passed in VMA has to be pinned in the global GTT mappable region.
 * An extra pinning of the VMA is acquired for the return iomapping,
 * the caller must call i915_vma_unpin_iomap to relinquish the pinning
 * after the iomapping is no longer required.
 *
 * Callers must hold the struct_mutex.
 *
 * Returns a valid iomapped pointer or ERR_PTR.
 */
void __iomem *i915_vma_pin_iomap(struct i915_vma *vma);
#define IO_ERR_PTR(x) ((void __iomem *)ERR_PTR(x))

/**
 * i915_vma_unpin_iomap - unpins the mapping returned from i915_vma_iomap
 * @vma: VMA to unpin
 *
 * Unpins the previously iomapped VMA from i915_vma_pin_iomap().
 *
 * Callers must hold the struct_mutex. This function is only valid to be
 * called on a VMA previously iomapped by the caller with i915_vma_pin_iomap().
 */
static inline void i915_vma_unpin_iomap(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->dev->struct_mutex);
	GEM_BUG_ON(vma->iomap == NULL);
	i915_vma_unpin(vma);
}

static inline struct page *i915_vma_first_page(struct i915_vma *vma)
{
	GEM_BUG_ON(!vma->pages);
	return sg_page(vma->pages->sgl);
}

/**
 * i915_vma_pin_fence - pin fencing state
 * @vma: vma to pin fencing for
 *
 * This pins the fencing state (whether tiled or untiled) to make sure the
 * vma (and its object) is ready to be used as a scanout target. Fencing
 * status must be synchronize first by calling i915_vma_get_fence():
 *
 * The resulting fence pin reference must be released again with
 * i915_vma_unpin_fence().
 *
 * Returns:
 *
 * True if the vma has a fence, false otherwise.
 */
static inline bool
i915_vma_pin_fence(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->dev->struct_mutex);
	if (vma->fence) {
		vma->fence->pin_count++;
		return true;
	} else
		return false;
}

/**
 * i915_vma_unpin_fence - unpin fencing state
 * @vma: vma to unpin fencing for
 *
 * This releases the fence pin reference acquired through
 * i915_vma_pin_fence. It will handle both objects with and without an
 * attached fence correctly, callers do not need to distinguish this.
 */
static inline void
i915_vma_unpin_fence(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->dev->struct_mutex);
	if (vma->fence) {
		GEM_BUG_ON(vma->fence->pin_count <= 0);
		vma->fence->pin_count--;
	}
}

#endif

