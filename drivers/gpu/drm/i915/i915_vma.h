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
#include <linux/rbtree.h>

#include <drm/drm_mm.h>

#include "i915_gem_gtt.h"
#include "i915_gem_fence_reg.h"
#include "i915_gem_object.h"

#include "i915_active.h"
#include "i915_request.h"

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
	const struct i915_vma_ops *ops;
	struct drm_i915_fence_reg *fence;
	struct reservation_object *resv; /** Alias of obj->resv */
	struct sg_table *pages;
	void __iomem *iomap;
	void *private; /* owned by creator */
	u64 size;
	u64 display_alignment;
	struct i915_page_sizes page_sizes;

	u32 fence_size;
	u32 fence_alignment;

	/**
	 * Count of the number of times this vma has been opened by different
	 * handles (but same file) for execbuf, i.e. the number of aliases
	 * that exist in the ctx->handle_vmas LUT for this vma.
	 */
	unsigned int open_count;
	unsigned long flags;
	/**
	 * How many users have pinned this object in GTT space.
	 *
	 * This is a tightly bound, fairly small number of users, so we
	 * stuff inside the flags field so that we can both check for overflow
	 * and detect a no-op i915_vma_pin() in a single check, while also
	 * pinning the vma.
	 *
	 * The worst case display setup would have the same vma pinned for
	 * use on each plane on each crtc, while also building the next atomic
	 * state and holding a pin for the length of the cleanup queue. In the
	 * future, the flip queue may be increased from 1.
	 * Estimated worst case: 3 [qlen] * 4 [max crtcs] * 7 [max planes] = 84
	 *
	 * For GEM, the number of concurrent users for pwrite/pread is
	 * unbounded. For execbuffer, it is currently one but will in future
	 * be extended to allow multiple clients to pin vma concurrently.
	 *
	 * We also use suballocated pages, with each suballocation claiming
	 * its own pin on the shared vma. At present, this is limited to
	 * exclusive cachelines of a single page, so a maximum of 64 possible
	 * users.
	 */
#define I915_VMA_PIN_MASK 0xff
#define I915_VMA_PIN_OVERFLOW	BIT(8)

	/** Flags and address space this VMA is bound to */
#define I915_VMA_GLOBAL_BIND	BIT(9)
#define I915_VMA_LOCAL_BIND	BIT(10)
#define I915_VMA_BIND_MASK (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND | I915_VMA_PIN_OVERFLOW)

#define I915_VMA_GGTT		BIT(11)
#define I915_VMA_CAN_FENCE	BIT(12)
#define I915_VMA_CLOSED		BIT(13)
#define I915_VMA_USERFAULT_BIT	14
#define I915_VMA_USERFAULT	BIT(I915_VMA_USERFAULT_BIT)
#define I915_VMA_GGTT_WRITE	BIT(15)

	struct i915_active active;
	struct i915_active_request last_fence;

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
	struct hlist_node obj_hash;

	/** This vma's place in the execbuf reservation list */
	struct list_head exec_link;
	struct list_head reloc_link;

	/** This vma's place in the eviction list */
	struct list_head evict_link;

	struct list_head closed_link;

	/**
	 * Used for performing relocations during execbuffer insertion.
	 */
	unsigned int *exec_flags;
	struct hlist_node exec_node;
	u32 exec_handle;
};

struct i915_vma *
i915_vma_instance(struct drm_i915_gem_object *obj,
		  struct i915_address_space *vm,
		  const struct i915_ggtt_view *view);

void i915_vma_unpin_and_release(struct i915_vma **p_vma, unsigned int flags);
#define I915_VMA_RELEASE_MAP BIT(0)

static inline bool i915_vma_is_active(const struct i915_vma *vma)
{
	return !i915_active_is_idle(&vma->active);
}

int __must_check i915_vma_move_to_active(struct i915_vma *vma,
					 struct i915_request *rq,
					 unsigned int flags);

static inline bool i915_vma_is_ggtt(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_GGTT;
}

static inline bool i915_vma_has_ggtt_write(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_GGTT_WRITE;
}

static inline void i915_vma_set_ggtt_write(struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	vma->flags |= I915_VMA_GGTT_WRITE;
}

static inline void i915_vma_unset_ggtt_write(struct i915_vma *vma)
{
	vma->flags &= ~I915_VMA_GGTT_WRITE;
}

void i915_vma_flush_writes(struct i915_vma *vma);

static inline bool i915_vma_is_map_and_fenceable(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_CAN_FENCE;
}

static inline bool i915_vma_is_closed(const struct i915_vma *vma)
{
	return vma->flags & I915_VMA_CLOSED;
}

static inline bool i915_vma_set_userfault(struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
	return __test_and_set_bit(I915_VMA_USERFAULT_BIT, &vma->flags);
}

static inline void i915_vma_unset_userfault(struct i915_vma *vma)
{
	return __clear_bit(I915_VMA_USERFAULT_BIT, &vma->flags);
}

static inline bool i915_vma_has_userfault(const struct i915_vma *vma)
{
	return test_bit(I915_VMA_USERFAULT_BIT, &vma->flags);
}

static inline u32 i915_ggtt_offset(const struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_ggtt(vma));
	GEM_BUG_ON(!vma->node.allocated);
	GEM_BUG_ON(upper_32_bits(vma->node.start));
	GEM_BUG_ON(upper_32_bits(vma->node.start + vma->node.size - 1));
	return lower_32_bits(vma->node.start);
}

static inline u32 i915_ggtt_pin_bias(struct i915_vma *vma)
{
	return i915_vm_to_ggtt(vma->vm)->pin_bias;
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

static __always_inline ptrdiff_t ptrdiff(const void *a, const void *b)
{
	return a - b;
}

static inline long
i915_vma_compare(struct i915_vma *vma,
		 struct i915_address_space *vm,
		 const struct i915_ggtt_view *view)
{
	ptrdiff_t cmp;

	GEM_BUG_ON(view && !i915_is_ggtt(vm));

	cmp = ptrdiff(vma->vm, vm);
	if (cmp)
		return cmp;

	BUILD_BUG_ON(I915_GGTT_VIEW_NORMAL != 0);
	cmp = vma->ggtt_view.type;
	if (!view)
		return cmp;

	cmp -= view->type;
	if (cmp)
		return cmp;

	assert_i915_gem_gtt_types();

	/* ggtt_view.type also encodes its size so that we both distinguish
	 * different views using it as a "type" and also use a compact (no
	 * accessing of uninitialised padding bytes) memcmp without storing
	 * an extra parameter or adding more code.
	 *
	 * To ensure that the memcmp is valid for all branches of the union,
	 * even though the code looks like it is just comparing one branch,
	 * we assert above that all branches have the same address, and that
	 * each branch has a unique type/size.
	 */
	BUILD_BUG_ON(I915_GGTT_VIEW_NORMAL >= I915_GGTT_VIEW_PARTIAL);
	BUILD_BUG_ON(I915_GGTT_VIEW_PARTIAL >= I915_GGTT_VIEW_ROTATED);
	BUILD_BUG_ON(offsetof(typeof(*view), rotated) !=
		     offsetof(typeof(*view), partial));
	return memcmp(&vma->ggtt_view.partial, &view->partial, view->type);
}

int i915_vma_bind(struct i915_vma *vma, enum i915_cache_level cache_level,
		  u32 flags);
bool i915_gem_valid_gtt_space(struct i915_vma *vma, unsigned long cache_level);
bool i915_vma_misplaced(const struct i915_vma *vma,
			u64 size, u64 alignment, u64 flags);
void __i915_vma_set_map_and_fenceable(struct i915_vma *vma);
void i915_vma_revoke_mmap(struct i915_vma *vma);
int __must_check i915_vma_unbind(struct i915_vma *vma);
void i915_vma_unlink_ctx(struct i915_vma *vma);
void i915_vma_close(struct i915_vma *vma);
void i915_vma_reopen(struct i915_vma *vma);
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
	if (likely(((++vma->flags ^ flags) & I915_VMA_BIND_MASK) == 0)) {
		GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
		GEM_BUG_ON(i915_vma_misplaced(vma, size, alignment, flags));
		return 0;
	}

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
	vma->flags--;
}

static inline void i915_vma_unpin(struct i915_vma *vma)
{
	GEM_BUG_ON(!i915_vma_is_pinned(vma));
	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));
	__i915_vma_unpin(vma);
}

static inline bool i915_vma_is_bound(const struct i915_vma *vma,
				     unsigned int where)
{
	return vma->flags & where;
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
void i915_vma_unpin_iomap(struct i915_vma *vma);

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
int i915_vma_pin_fence(struct i915_vma *vma);
int __must_check i915_vma_put_fence(struct i915_vma *vma);

static inline void __i915_vma_unpin_fence(struct i915_vma *vma)
{
	GEM_BUG_ON(vma->fence->pin_count <= 0);
	vma->fence->pin_count--;
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
	/* lockdep_assert_held(&vma->vm->i915->drm.struct_mutex); */
	if (vma->fence)
		__i915_vma_unpin_fence(vma);
}

void i915_vma_parked(struct drm_i915_private *i915);

#define for_each_until(cond) if (cond) break; else

/**
 * for_each_ggtt_vma - Iterate over the GGTT VMA belonging to an object.
 * @V: the #i915_vma iterator
 * @OBJ: the #drm_i915_gem_object
 *
 * GGTT VMA are placed at the being of the object's vma_list, see
 * vma_create(), so we can stop our walk as soon as we see a ppgtt VMA,
 * or the list is empty ofc.
 */
#define for_each_ggtt_vma(V, OBJ) \
	list_for_each_entry(V, &(OBJ)->vma.list, obj_link)		\
		for_each_until(!i915_vma_is_ggtt(V))

#endif
