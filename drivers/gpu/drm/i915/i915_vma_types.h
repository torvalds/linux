/* SPDX-License-Identifier: MIT */
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

#ifndef __I915_VMA_TYPES_H__
#define __I915_VMA_TYPES_H__

#include <linux/rbtree.h>

#include <drm/drm_mm.h>

#include "gem/i915_gem_object_types.h"

enum i915_cache_level;

/**
 * DOC: Global GTT views
 *
 * Background and previous state
 *
 * Historically objects could exists (be bound) in global GTT space only as
 * singular instances with a view representing all of the object's backing pages
 * in a linear fashion. This view will be called a normal view.
 *
 * To support multiple views of the same object, where the number of mapped
 * pages is not equal to the backing store, or where the layout of the pages
 * is not linear, concept of a GGTT view was added.
 *
 * One example of an alternative view is a stereo display driven by a single
 * image. In this case we would have a framebuffer looking like this
 * (2x2 pages):
 *
 *    12
 *    34
 *
 * Above would represent a normal GGTT view as normally mapped for GPU or CPU
 * rendering. In contrast, fed to the display engine would be an alternative
 * view which could look something like this:
 *
 *   1212
 *   3434
 *
 * In this example both the size and layout of pages in the alternative view is
 * different from the normal view.
 *
 * Implementation and usage
 *
 * GGTT views are implemented using VMAs and are distinguished via enum
 * i915_ggtt_view_type and struct i915_ggtt_view.
 *
 * A new flavour of core GEM functions which work with GGTT bound objects were
 * added with the _ggtt_ infix, and sometimes with _view postfix to avoid
 * renaming  in large amounts of code. They take the struct i915_ggtt_view
 * parameter encapsulating all metadata required to implement a view.
 *
 * As a helper for callers which are only interested in the normal view,
 * globally const i915_ggtt_view_normal singleton instance exists. All old core
 * GEM API functions, the ones not taking the view parameter, are operating on,
 * or with the normal GGTT view.
 *
 * Code wanting to add or use a new GGTT view needs to:
 *
 * 1. Add a new enum with a suitable name.
 * 2. Extend the metadata in the i915_ggtt_view structure if required.
 * 3. Add support to i915_get_vma_pages().
 *
 * New views are required to build a scatter-gather table from within the
 * i915_get_vma_pages function. This table is stored in the vma.ggtt_view and
 * exists for the lifetime of an VMA.
 *
 * Core API is designed to have copy semantics which means that passed in
 * struct i915_ggtt_view does not need to be persistent (left around after
 * calling the core API functions).
 *
 */

struct intel_remapped_plane_info {
	/* in gtt pages */
	unsigned int width, height, stride, offset;
} __packed;

struct intel_remapped_info {
	struct intel_remapped_plane_info plane[2];
	unsigned int unused_mbz;
} __packed;

struct intel_rotation_info {
	struct intel_remapped_plane_info plane[2];
} __packed;

struct intel_partial_info {
	u64 offset;
	unsigned int size;
} __packed;

enum i915_ggtt_view_type {
	I915_GGTT_VIEW_NORMAL = 0,
	I915_GGTT_VIEW_ROTATED = sizeof(struct intel_rotation_info),
	I915_GGTT_VIEW_PARTIAL = sizeof(struct intel_partial_info),
	I915_GGTT_VIEW_REMAPPED = sizeof(struct intel_remapped_info),
};

static inline void assert_i915_gem_gtt_types(void)
{
	BUILD_BUG_ON(sizeof(struct intel_rotation_info) != 8*sizeof(unsigned int));
	BUILD_BUG_ON(sizeof(struct intel_partial_info) != sizeof(u64) + sizeof(unsigned int));
	BUILD_BUG_ON(sizeof(struct intel_remapped_info) != 9*sizeof(unsigned int));

	/* Check that rotation/remapped shares offsets for simplicity */
	BUILD_BUG_ON(offsetof(struct intel_remapped_info, plane[0]) !=
		     offsetof(struct intel_rotation_info, plane[0]));
	BUILD_BUG_ON(offsetofend(struct intel_remapped_info, plane[1]) !=
		     offsetofend(struct intel_rotation_info, plane[1]));

	/* As we encode the size of each branch inside the union into its type,
	 * we have to be careful that each branch has a unique size.
	 */
	switch ((enum i915_ggtt_view_type)0) {
	case I915_GGTT_VIEW_NORMAL:
	case I915_GGTT_VIEW_PARTIAL:
	case I915_GGTT_VIEW_ROTATED:
	case I915_GGTT_VIEW_REMAPPED:
		/* gcc complains if these are identical cases */
		break;
	}
}

struct i915_ggtt_view {
	enum i915_ggtt_view_type type;
	union {
		/* Members need to contain no holes/padding */
		struct intel_partial_info partial;
		struct intel_rotation_info rotated;
		struct intel_remapped_info remapped;
	};
};

/**
 * DOC: Virtual Memory Address
 *
 * A VMA represents a GEM BO that is bound into an address space. Therefore, a
 * VMA's presence cannot be guaranteed before binding, or after unbinding the
 * object into/from the address space.
 *
 * To make things as simple as possible (ie. no refcounting), a VMA's lifetime
 * will always be <= an objects lifetime. So object refcounting should cover us.
 */
struct i915_vma {
	struct drm_mm_node node;

	struct i915_address_space *vm;
	const struct i915_vma_ops *ops;

	struct drm_i915_gem_object *obj;
	struct dma_resv *resv; /** Alias of obj->resv */

	struct sg_table *pages;
	void __iomem *iomap;
	void *private; /* owned by creator */

	struct i915_fence_reg *fence;

	u64 size;
	u64 display_alignment;
	struct i915_page_sizes page_sizes;

	/* mmap-offset associated with fencing for this vma */
	struct i915_mmap_offset	*mmo;

	u32 fence_size;
	u32 fence_alignment;

	/**
	 * Count of the number of times this vma has been opened by different
	 * handles (but same file) for execbuf, i.e. the number of aliases
	 * that exist in the ctx->handle_vmas LUT for this vma.
	 */
	struct kref ref;
	atomic_t open_count;
	atomic_t flags;
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
#define I915_VMA_PIN_MASK 0x3ff
#define I915_VMA_OVERFLOW 0x200

	/** Flags and address space this VMA is bound to */
#define I915_VMA_GLOBAL_BIND_BIT 10
#define I915_VMA_LOCAL_BIND_BIT  11

#define I915_VMA_GLOBAL_BIND	((int)BIT(I915_VMA_GLOBAL_BIND_BIT))
#define I915_VMA_LOCAL_BIND	((int)BIT(I915_VMA_LOCAL_BIND_BIT))

#define I915_VMA_BIND_MASK (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND)

#define I915_VMA_ALLOC_BIT	12
#define I915_VMA_ALLOC		((int)BIT(I915_VMA_ALLOC_BIT))

#define I915_VMA_ERROR_BIT	13
#define I915_VMA_ERROR		((int)BIT(I915_VMA_ERROR_BIT))

#define I915_VMA_GGTT_BIT	14
#define I915_VMA_CAN_FENCE_BIT	15
#define I915_VMA_USERFAULT_BIT	16
#define I915_VMA_GGTT_WRITE_BIT	17

#define I915_VMA_GGTT		((int)BIT(I915_VMA_GGTT_BIT))
#define I915_VMA_CAN_FENCE	((int)BIT(I915_VMA_CAN_FENCE_BIT))
#define I915_VMA_USERFAULT	((int)BIT(I915_VMA_USERFAULT_BIT))
#define I915_VMA_GGTT_WRITE	((int)BIT(I915_VMA_GGTT_WRITE_BIT))

	struct i915_active active;

#define I915_VMA_PAGES_BIAS 24
#define I915_VMA_PAGES_ACTIVE (BIT(24) | 1)
	atomic_t pages_count; /* number of active binds to the pages */
	struct mutex pages_mutex; /* protect acquire/release of backing pages */

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

#endif

