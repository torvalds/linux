/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_GEM_OBJECT_TYPES_H__
#define __I915_GEM_OBJECT_TYPES_H__

#include <drm/drm_gem.h>
#include <uapi/drm/i915_drm.h>

#include "i915_active.h"
#include "i915_selftest.h"

struct drm_i915_gem_object;
struct intel_fronbuffer;

/*
 * struct i915_lut_handle tracks the fast lookups from handle to vma used
 * for execbuf. Although we use a radixtree for that mapping, in order to
 * remove them as the object or context is closed, we need a secondary list
 * and a translation entry (i915_lut_handle).
 */
struct i915_lut_handle {
	struct list_head obj_link;
	struct i915_gem_context *ctx;
	u32 handle;
};

struct drm_i915_gem_object_ops {
	unsigned int flags;
#define I915_GEM_OBJECT_HAS_STRUCT_PAGE	BIT(0)
#define I915_GEM_OBJECT_HAS_IOMEM	BIT(1)
#define I915_GEM_OBJECT_IS_SHRINKABLE	BIT(2)
#define I915_GEM_OBJECT_IS_PROXY	BIT(3)
#define I915_GEM_OBJECT_NO_GGTT		BIT(4)
#define I915_GEM_OBJECT_ASYNC_CANCEL	BIT(5)

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
	int (*get_pages)(struct drm_i915_gem_object *obj);
	void (*put_pages)(struct drm_i915_gem_object *obj,
			  struct sg_table *pages);
	void (*truncate)(struct drm_i915_gem_object *obj);
	void (*writeback)(struct drm_i915_gem_object *obj);

	int (*pwrite)(struct drm_i915_gem_object *obj,
		      const struct drm_i915_gem_pwrite *arg);

	int (*dmabuf_export)(struct drm_i915_gem_object *obj);
	void (*release)(struct drm_i915_gem_object *obj);
};

struct drm_i915_gem_object {
	struct drm_gem_object base;

	const struct drm_i915_gem_object_ops *ops;

	struct {
		/**
		 * @vma.lock: protect the list/tree of vmas
		 */
		spinlock_t lock;

		/**
		 * @vma.list: List of VMAs backed by this object
		 *
		 * The VMA on this list are ordered by type, all GGTT vma are
		 * placed at the head and all ppGTT vma are placed at the tail.
		 * The different types of GGTT vma are unordered between
		 * themselves, use the @vma.tree (which has a defined order
		 * between all VMA) to quickly find an exact match.
		 */
		struct list_head list;

		/**
		 * @vma.tree: Ordered tree of VMAs backed by this object
		 *
		 * All VMA created for this object are placed in the @vma.tree
		 * for fast retrieval via a binary search in
		 * i915_vma_instance(). They are also added to @vma.list for
		 * easy iteration.
		 */
		struct rb_root tree;
	} vma;

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

	I915_SELFTEST_DECLARE(struct list_head st_link);

	unsigned long flags;
#define I915_BO_ALLOC_CONTIGUOUS BIT(0)
#define I915_BO_ALLOC_VOLATILE   BIT(1)
#define I915_BO_ALLOC_FLAGS (I915_BO_ALLOC_CONTIGUOUS | I915_BO_ALLOC_VOLATILE)

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

	struct intel_frontbuffer __rcu *frontbuffer;

	/** Current tiling stride for the object, if it's tiled. */
	unsigned int tiling_and_stride;
#define FENCE_MINIMUM_STRIDE 128 /* See i915_tiling_ok() */
#define TILING_MASK (FENCE_MINIMUM_STRIDE - 1)
#define STRIDE_MASK (~TILING_MASK)

	/** Count of VMA actually bound by this object */
	atomic_t bind_count;

	struct {
		struct mutex lock; /* protects the pages and their use */
		atomic_t pages_pin_count;
		atomic_t shrink_pin;

		/**
		 * Memory region for this object.
		 */
		struct intel_memory_region *region;
		/**
		 * List of memory region blocks allocated for this object.
		 */
		struct list_head blocks;
		/**
		 * Element within memory_region->objects or region->purgeable
		 * if the object is marked as DONTNEED. Access is protected by
		 * region->obj_lock.
		 */
		struct list_head region_link;

		struct sg_table *pages;
		void *mapping;

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
};

static inline struct drm_i915_gem_object *
to_intel_bo(struct drm_gem_object *gem)
{
	/* Assert that to_intel_bo(NULL) == NULL */
	BUILD_BUG_ON(offsetof(struct drm_i915_gem_object, base));

	return container_of(gem, struct drm_i915_gem_object, base);
}

#endif
