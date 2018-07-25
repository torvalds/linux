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

#ifndef __I915_GEM_OBJECT_H__
#define __I915_GEM_OBJECT_H__

#include <linux/reservation.h>

#include <drm/drm_vma_manager.h>
#include <drm/drm_gem.h>
#include <drm/drmP.h>

#include <drm/i915_drm.h>

#include "i915_request.h"
#include "i915_selftest.h"

struct drm_i915_gem_object;

/*
 * struct i915_lut_handle tracks the fast lookups from handle to vma used
 * for execbuf. Although we use a radixtree for that mapping, in order to
 * remove them as the object or context is closed, we need a secondary list
 * and a translation entry (i915_lut_handle).
 */
struct i915_lut_handle {
	struct list_head obj_link;
	struct list_head ctx_link;
	struct i915_gem_context *ctx;
	u32 handle;
};

struct drm_i915_gem_object_ops {
	unsigned int flags;
#define I915_GEM_OBJECT_HAS_STRUCT_PAGE	BIT(0)
#define I915_GEM_OBJECT_IS_SHRINKABLE	BIT(1)
#define I915_GEM_OBJECT_IS_PROXY	BIT(2)

	/* Interface between the GEM object and its backing storage.
	 * get_pages() is called once prior to the use of the associated set
	 * of pages before to binding them into the GTT, and put_pages() is
	 * called after we no longer need them. As we expect there to be
	 * associated cost with migrating pages between the backing storage
	 * and making them available for the GPU (e.g. clflush), we may hold
	 * onto the pages after they are no longer referenced by the GPU
	 * in case they may be used again shortly (for example migrating the
	 * pages to a different memory domain within the GTT). put_pages()
	 * will therefore most likely be called when the object itself is
	 * being released or under memory pressure (where we attempt to
	 * reap pages for the shrinker).
	 */
	int (*get_pages)(struct drm_i915_gem_object *);
	void (*put_pages)(struct drm_i915_gem_object *, struct sg_table *);

	int (*pwrite)(struct drm_i915_gem_object *,
		      const struct drm_i915_gem_pwrite *);

	int (*dmabuf_export)(struct drm_i915_gem_object *);
	void (*release)(struct drm_i915_gem_object *);
};

struct drm_i915_gem_object {
	struct drm_gem_object base;

	const struct drm_i915_gem_object_ops *ops;

	/**
	 * @vma_list: List of VMAs backed by this object
	 *
	 * The VMA on this list are ordered by type, all GGTT vma are placed
	 * at the head and all ppGTT vma are placed at the tail. The different
	 * types of GGTT vma are unordered between themselves, use the
	 * @vma_tree (which has a defined order between all VMA) to find an
	 * exact match.
	 */
	struct list_head vma_list;
	/**
	 * @vma_tree: Ordered tree of VMAs backed by this object
	 *
	 * All VMA created for this object are placed in the @vma_tree for
	 * fast retrieval via a binary search in i915_vma_instance().
	 * They are also added to @vma_list for easy iteration.
	 */
	struct rb_root vma_tree;

	/**
	 * @lut_list: List of vma lookup entries in use for this object.
	 *
	 * If this object is closed, we need to remove all of its VMA from
	 * the fast lookup index in associated contexts; @lut_list provides
	 * this translation from object to context->handles_vma.
	 */
	struct list_head lut_list;

	/** Stolen memory for this object, instead of being backed by shmem. */
	struct drm_mm_node *stolen;
	union {
		struct rcu_head rcu;
		struct llist_node freed;
	};

	/**
	 * Whether the object is currently in the GGTT mmap.
	 */
	unsigned int userfault_count;
	struct list_head userfault_link;

	struct list_head batch_pool_link;
	I915_SELFTEST_DECLARE(struct list_head st_link);

	unsigned long flags;

	/**
	 * Have we taken a reference for the object for incomplete GPU
	 * activity?
	 */
#define I915_BO_ACTIVE_REF 0

	/*
	 * Is the object to be mapped as read-only to the GPU
	 * Only honoured if hardware has relevant pte bit
	 */
	unsigned int cache_level:3;
	unsigned int cache_coherent:2;
#define I915_BO_CACHE_COHERENT_FOR_READ BIT(0)
#define I915_BO_CACHE_COHERENT_FOR_WRITE BIT(1)
	unsigned int cache_dirty:1;

	/**
	 * @read_domains: Read memory domains.
	 *
	 * These monitor which caches contain read/write data related to the
	 * object. When transitioning from one set of domains to another,
	 * the driver is called to ensure that caches are suitably flushed and
	 * invalidated.
	 */
	u16 read_domains;

	/**
	 * @write_domain: Corresponding unique write memory domain.
	 */
	u16 write_domain;

	atomic_t frontbuffer_bits;
	unsigned int frontbuffer_ggtt_origin; /* write once */
	struct i915_gem_active frontbuffer_write;

	/** Current tiling stride for the object, if it's tiled. */
	unsigned int tiling_and_stride;
#define FENCE_MINIMUM_STRIDE 128 /* See i915_tiling_ok() */
#define TILING_MASK (FENCE_MINIMUM_STRIDE-1)
#define STRIDE_MASK (~TILING_MASK)

	/** Count of VMA actually bound by this object */
	unsigned int bind_count;
	unsigned int active_count;
	/** Count of how many global VMA are currently pinned for use by HW */
	unsigned int pin_global;

	struct {
		struct mutex lock; /* protects the pages and their use */
		atomic_t pages_pin_count;

		struct sg_table *pages;
		void *mapping;

		/* TODO: whack some of this into the error state */
		struct i915_page_sizes {
			/**
			 * The sg mask of the pages sg_table. i.e the mask of
			 * of the lengths for each sg entry.
			 */
			unsigned int phys;

			/**
			 * The gtt page sizes we are allowed to use given the
			 * sg mask and the supported page sizes. This will
			 * express the smallest unit we can use for the whole
			 * object, as well as the larger sizes we may be able
			 * to use opportunistically.
			 */
			unsigned int sg;

			/**
			 * The actual gtt page size usage. Since we can have
			 * multiple vma associated with this object we need to
			 * prevent any trampling of state, hence a copy of this
			 * struct also lives in each vma, therefore the gtt
			 * value here should only be read/write through the vma.
			 */
			unsigned int gtt;
		} page_sizes;

		I915_SELFTEST_DECLARE(unsigned int page_mask);

		struct i915_gem_object_page_iter {
			struct scatterlist *sg_pos;
			unsigned int sg_idx; /* in pages, but 32bit eek! */

			struct radix_tree_root radix;
			struct mutex lock; /* protects this cache */
		} get_page;

		/**
		 * Element within i915->mm.unbound_list or i915->mm.bound_list,
		 * locked by i915->mm.obj_lock.
		 */
		struct list_head link;

		/**
		 * Advice: are the backing pages purgeable?
		 */
		unsigned int madv:2;

		/**
		 * This is set if the object has been written to since the
		 * pages were last acquired.
		 */
		bool dirty:1;

		/**
		 * This is set if the object has been pinned due to unknown
		 * swizzling.
		 */
		bool quirked:1;
	} mm;

	/** Breadcrumb of last rendering to the buffer.
	 * There can only be one writer, but we allow for multiple readers.
	 * If there is a writer that necessarily implies that all other
	 * read requests are complete - but we may only be lazily clearing
	 * the read requests. A read request is naturally the most recent
	 * request on a ring, so we may have two different write and read
	 * requests on one ring where the write request is older than the
	 * read request. This allows for the CPU to read from an active
	 * buffer by only waiting for the write to complete.
	 */
	struct reservation_object *resv;

	/** References from framebuffers, locks out tiling changes. */
	unsigned int framebuffer_references;

	/** Record of address bit 17 of each page at last unbind. */
	unsigned long *bit_17;

	union {
		struct i915_gem_userptr {
			uintptr_t ptr;

			struct i915_mm_struct *mm;
			struct i915_mmu_object *mmu_object;
			struct work_struct *work;
		} userptr;

		unsigned long scratch;

		void *gvt_info;
	};

	/** for phys allocated objects */
	struct drm_dma_handle *phys_handle;

	struct reservation_object __builtin_resv;
};

static inline struct drm_i915_gem_object *
to_intel_bo(struct drm_gem_object *gem)
{
	/* Assert that to_intel_bo(NULL) == NULL */
	BUILD_BUG_ON(offsetof(struct drm_i915_gem_object, base));

	return container_of(gem, struct drm_i915_gem_object, base);
}

/**
 * i915_gem_object_lookup_rcu - look up a temporary GEM object from its handle
 * @filp: DRM file private date
 * @handle: userspace handle
 *
 * Returns:
 *
 * A pointer to the object named by the handle if such exists on @filp, NULL
 * otherwise. This object is only valid whilst under the RCU read lock, and
 * note carefully the object may be in the process of being destroyed.
 */
static inline struct drm_i915_gem_object *
i915_gem_object_lookup_rcu(struct drm_file *file, u32 handle)
{
#ifdef CONFIG_LOCKDEP
	WARN_ON(debug_locks && !lock_is_held(&rcu_lock_map));
#endif
	return idr_find(&file->object_idr, handle);
}

static inline struct drm_i915_gem_object *
i915_gem_object_lookup(struct drm_file *file, u32 handle)
{
	struct drm_i915_gem_object *obj;

	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, handle);
	if (obj && !kref_get_unless_zero(&obj->base.refcount))
		obj = NULL;
	rcu_read_unlock();

	return obj;
}

__deprecated
extern struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, u32 handle);

__attribute__((nonnull))
static inline struct drm_i915_gem_object *
i915_gem_object_get(struct drm_i915_gem_object *obj)
{
	drm_gem_object_get(&obj->base);
	return obj;
}

__attribute__((nonnull))
static inline void
i915_gem_object_put(struct drm_i915_gem_object *obj)
{
	__drm_gem_object_put(&obj->base);
}

static inline void i915_gem_object_lock(struct drm_i915_gem_object *obj)
{
	reservation_object_lock(obj->resv, NULL);
}

static inline void i915_gem_object_unlock(struct drm_i915_gem_object *obj)
{
	reservation_object_unlock(obj->resv);
}

static inline void
i915_gem_object_set_readonly(struct drm_i915_gem_object *obj)
{
	obj->base.vma_node.readonly = true;
}

static inline bool
i915_gem_object_is_readonly(const struct drm_i915_gem_object *obj)
{
	return obj->base.vma_node.readonly;
}

static inline bool
i915_gem_object_has_struct_page(const struct drm_i915_gem_object *obj)
{
	return obj->ops->flags & I915_GEM_OBJECT_HAS_STRUCT_PAGE;
}

static inline bool
i915_gem_object_is_shrinkable(const struct drm_i915_gem_object *obj)
{
	return obj->ops->flags & I915_GEM_OBJECT_IS_SHRINKABLE;
}

static inline bool
i915_gem_object_is_proxy(const struct drm_i915_gem_object *obj)
{
	return obj->ops->flags & I915_GEM_OBJECT_IS_PROXY;
}

static inline bool
i915_gem_object_is_active(const struct drm_i915_gem_object *obj)
{
	return obj->active_count;
}

static inline bool
i915_gem_object_has_active_reference(const struct drm_i915_gem_object *obj)
{
	return test_bit(I915_BO_ACTIVE_REF, &obj->flags);
}

static inline void
i915_gem_object_set_active_reference(struct drm_i915_gem_object *obj)
{
	lockdep_assert_held(&obj->base.dev->struct_mutex);
	__set_bit(I915_BO_ACTIVE_REF, &obj->flags);
}

static inline void
i915_gem_object_clear_active_reference(struct drm_i915_gem_object *obj)
{
	lockdep_assert_held(&obj->base.dev->struct_mutex);
	__clear_bit(I915_BO_ACTIVE_REF, &obj->flags);
}

void __i915_gem_object_release_unless_active(struct drm_i915_gem_object *obj);

static inline bool
i915_gem_object_is_framebuffer(const struct drm_i915_gem_object *obj)
{
	return READ_ONCE(obj->framebuffer_references);
}

static inline unsigned int
i915_gem_object_get_tiling(const struct drm_i915_gem_object *obj)
{
	return obj->tiling_and_stride & TILING_MASK;
}

static inline bool
i915_gem_object_is_tiled(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_get_tiling(obj) != I915_TILING_NONE;
}

static inline unsigned int
i915_gem_object_get_stride(const struct drm_i915_gem_object *obj)
{
	return obj->tiling_and_stride & STRIDE_MASK;
}

static inline unsigned int
i915_gem_tile_height(unsigned int tiling)
{
	GEM_BUG_ON(!tiling);
	return tiling == I915_TILING_Y ? 32 : 8;
}

static inline unsigned int
i915_gem_object_get_tile_height(const struct drm_i915_gem_object *obj)
{
	return i915_gem_tile_height(i915_gem_object_get_tiling(obj));
}

static inline unsigned int
i915_gem_object_get_tile_row_size(const struct drm_i915_gem_object *obj)
{
	return (i915_gem_object_get_stride(obj) *
		i915_gem_object_get_tile_height(obj));
}

int i915_gem_object_set_tiling(struct drm_i915_gem_object *obj,
			       unsigned int tiling, unsigned int stride);

static inline struct intel_engine_cs *
i915_gem_object_last_write_engine(struct drm_i915_gem_object *obj)
{
	struct intel_engine_cs *engine = NULL;
	struct dma_fence *fence;

	rcu_read_lock();
	fence = reservation_object_get_excl_rcu(obj->resv);
	rcu_read_unlock();

	if (fence && dma_fence_is_i915(fence) && !dma_fence_is_signaled(fence))
		engine = to_request(fence)->engine;
	dma_fence_put(fence);

	return engine;
}

void i915_gem_object_set_cache_coherency(struct drm_i915_gem_object *obj,
					 unsigned int cache_level);
void i915_gem_object_flush_if_display(struct drm_i915_gem_object *obj);

#endif

