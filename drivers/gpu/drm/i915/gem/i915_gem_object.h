/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_GEM_OBJECT_H__
#define __I915_GEM_OBJECT_H__

#include <drm/drm_gem.h>
#include <drm/drm_file.h>
#include <drm/drm_device.h>

#include "display/intel_frontbuffer.h"
#include "intel_memory_region.h"
#include "i915_gem_object_types.h"
#include "i915_gem_gtt.h"
#include "i915_gem_ww.h"
#include "i915_vma_types.h"

enum intel_region_id;

#define obj_to_i915(obj__) to_i915((obj__)->base.dev)

static inline bool i915_gem_object_size_2big(u64 size)
{
	struct drm_i915_gem_object *obj;

	if (overflows_type(size, obj->base.size))
		return true;

	return false;
}

unsigned int i915_gem_get_pat_index(struct drm_i915_private *i915,
				    enum i915_cache_level level);
bool i915_gem_object_has_cache_level(const struct drm_i915_gem_object *obj,
				     enum i915_cache_level lvl);
void i915_gem_init__objects(struct drm_i915_private *i915);

void i915_objects_module_exit(void);
int i915_objects_module_init(void);

struct drm_i915_gem_object *i915_gem_object_alloc(void);
void i915_gem_object_free(struct drm_i915_gem_object *obj);

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops,
			  struct lock_class_key *key,
			  unsigned alloc_flags);

void __i915_gem_object_fini(struct drm_i915_gem_object *obj);

struct drm_i915_gem_object *
i915_gem_object_create_shmem(struct drm_i915_private *i915,
			     resource_size_t size);
struct drm_i915_gem_object *
i915_gem_object_create_shmem_from_data(struct drm_i915_private *i915,
				       const void *data, resource_size_t size);
struct drm_i915_gem_object *
__i915_gem_object_create_user(struct drm_i915_private *i915, u64 size,
			      struct intel_memory_region **placements,
			      unsigned int n_placements);

extern const struct drm_i915_gem_object_ops i915_gem_shmem_ops;

void __i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				     struct sg_table *pages,
				     bool needs_clflush);

int i915_gem_object_pwrite_phys(struct drm_i915_gem_object *obj,
				const struct drm_i915_gem_pwrite *args);
int i915_gem_object_pread_phys(struct drm_i915_gem_object *obj,
			       const struct drm_i915_gem_pread *args);

int i915_gem_object_attach_phys(struct drm_i915_gem_object *obj, int align);
void i915_gem_object_put_pages_shmem(struct drm_i915_gem_object *obj,
				     struct sg_table *pages);
void i915_gem_object_put_pages_phys(struct drm_i915_gem_object *obj,
				    struct sg_table *pages);

void i915_gem_flush_free_objects(struct drm_i915_private *i915);

struct sg_table *
__i915_gem_object_unset_pages(struct drm_i915_gem_object *obj);

/**
 * i915_gem_object_lookup_rcu - look up a temporary GEM object from its handle
 * @file: DRM file private date
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
i915_gem_object_get_rcu(struct drm_i915_gem_object *obj)
{
	if (obj && !kref_get_unless_zero(&obj->base.refcount))
		obj = NULL;

	return obj;
}

static inline struct drm_i915_gem_object *
i915_gem_object_lookup(struct drm_file *file, u32 handle)
{
	struct drm_i915_gem_object *obj;

	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, handle);
	obj = i915_gem_object_get_rcu(obj);
	rcu_read_unlock();

	return obj;
}

__deprecated
struct drm_gem_object *
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

#define assert_object_held(obj) dma_resv_assert_held((obj)->base.resv)

/*
 * If more than one potential simultaneous locker, assert held.
 */
static inline void assert_object_held_shared(const struct drm_i915_gem_object *obj)
{
	/*
	 * Note mm list lookup is protected by
	 * kref_get_unless_zero().
	 */
	if (IS_ENABLED(CONFIG_LOCKDEP) &&
	    kref_read(&obj->base.refcount) > 0)
		assert_object_held(obj);
}

static inline int __i915_gem_object_lock(struct drm_i915_gem_object *obj,
					 struct i915_gem_ww_ctx *ww,
					 bool intr)
{
	int ret;

	if (intr)
		ret = dma_resv_lock_interruptible(obj->base.resv, ww ? &ww->ctx : NULL);
	else
		ret = dma_resv_lock(obj->base.resv, ww ? &ww->ctx : NULL);

	if (!ret && ww) {
		i915_gem_object_get(obj);
		list_add_tail(&obj->obj_link, &ww->obj_list);
	}
	if (ret == -EALREADY)
		ret = 0;

	if (ret == -EDEADLK) {
		i915_gem_object_get(obj);
		ww->contended = obj;
	}

	return ret;
}

static inline int i915_gem_object_lock(struct drm_i915_gem_object *obj,
				       struct i915_gem_ww_ctx *ww)
{
	return __i915_gem_object_lock(obj, ww, ww && ww->intr);
}

static inline int i915_gem_object_lock_interruptible(struct drm_i915_gem_object *obj,
						     struct i915_gem_ww_ctx *ww)
{
	WARN_ON(ww && !ww->intr);
	return __i915_gem_object_lock(obj, ww, true);
}

static inline bool i915_gem_object_trylock(struct drm_i915_gem_object *obj,
					   struct i915_gem_ww_ctx *ww)
{
	if (!ww)
		return dma_resv_trylock(obj->base.resv);
	else
		return ww_mutex_trylock(&obj->base.resv->lock, &ww->ctx);
}

static inline void i915_gem_object_unlock(struct drm_i915_gem_object *obj)
{
	if (obj->ops->adjust_lru)
		obj->ops->adjust_lru(obj);

	dma_resv_unlock(obj->base.resv);
}

static inline void
i915_gem_object_set_readonly(struct drm_i915_gem_object *obj)
{
	obj->flags |= I915_BO_READONLY;
}

static inline bool
i915_gem_object_is_readonly(const struct drm_i915_gem_object *obj)
{
	return obj->flags & I915_BO_READONLY;
}

static inline bool
i915_gem_object_is_contiguous(const struct drm_i915_gem_object *obj)
{
	return obj->flags & I915_BO_ALLOC_CONTIGUOUS;
}

static inline bool
i915_gem_object_is_volatile(const struct drm_i915_gem_object *obj)
{
	return obj->flags & I915_BO_ALLOC_VOLATILE;
}

static inline void
i915_gem_object_set_volatile(struct drm_i915_gem_object *obj)
{
	obj->flags |= I915_BO_ALLOC_VOLATILE;
}

static inline bool
i915_gem_object_has_tiling_quirk(struct drm_i915_gem_object *obj)
{
	return test_bit(I915_TILING_QUIRK_BIT, &obj->flags);
}

static inline void
i915_gem_object_set_tiling_quirk(struct drm_i915_gem_object *obj)
{
	set_bit(I915_TILING_QUIRK_BIT, &obj->flags);
}

static inline void
i915_gem_object_clear_tiling_quirk(struct drm_i915_gem_object *obj)
{
	clear_bit(I915_TILING_QUIRK_BIT, &obj->flags);
}

static inline bool
i915_gem_object_is_protected(const struct drm_i915_gem_object *obj)
{
	return obj->flags & I915_BO_PROTECTED;
}

static inline bool
i915_gem_object_type_has(const struct drm_i915_gem_object *obj,
			 unsigned long flags)
{
	return obj->ops->flags & flags;
}

bool i915_gem_object_has_struct_page(const struct drm_i915_gem_object *obj);

bool i915_gem_object_has_iomem(const struct drm_i915_gem_object *obj);

static inline bool
i915_gem_object_is_shrinkable(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_type_has(obj, I915_GEM_OBJECT_IS_SHRINKABLE);
}

static inline bool
i915_gem_object_has_self_managed_shrink_list(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_type_has(obj, I915_GEM_OBJECT_SELF_MANAGED_SHRINK_LIST);
}

static inline bool
i915_gem_object_is_proxy(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_type_has(obj, I915_GEM_OBJECT_IS_PROXY);
}

static inline bool
i915_gem_object_never_mmap(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_type_has(obj, I915_GEM_OBJECT_NO_MMAP);
}

static inline bool
i915_gem_object_is_framebuffer(const struct drm_i915_gem_object *obj)
{
	return READ_ONCE(obj->frontbuffer) || obj->is_dpt;
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

/**
 * __i915_gem_object_page_iter_get_sg - helper to find the target scatterlist
 * pointer and the target page position using pgoff_t n input argument and
 * i915_gem_object_page_iter
 * @obj: i915 GEM buffer object
 * @iter: i915 GEM buffer object page iterator
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * Context: Takes and releases the mutex lock of the i915_gem_object_page_iter.
 *          Takes and releases the RCU lock to search the radix_tree of
 *          i915_gem_object_page_iter.
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * Recommended to use wrapper macro: i915_gem_object_page_iter_get_sg()
 */
struct scatterlist *
__i915_gem_object_page_iter_get_sg(struct drm_i915_gem_object *obj,
				   struct i915_gem_object_page_iter *iter,
				   pgoff_t  n,
				   unsigned int *offset);

/**
 * i915_gem_object_page_iter_get_sg - wrapper macro for
 * __i915_gem_object_page_iter_get_sg()
 * @obj: i915 GEM buffer object
 * @it: i915 GEM buffer object page iterator
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * Context: Takes and releases the mutex lock of the i915_gem_object_page_iter.
 *          Takes and releases the RCU lock to search the radix_tree of
 *          i915_gem_object_page_iter.
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_page_iter_get_sg().
 */
#define i915_gem_object_page_iter_get_sg(obj, it, n, offset) ({	\
	static_assert(castable_to_type(n, pgoff_t));		\
	__i915_gem_object_page_iter_get_sg(obj, it, n, offset);	\
})

/**
 * __i915_gem_object_get_sg - helper to find the target scatterlist
 * pointer and the target page position using pgoff_t n input argument and
 * drm_i915_gem_object. It uses an internal shmem scatterlist lookup function.
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * It uses drm_i915_gem_object's internal shmem scatterlist lookup function as
 * i915_gem_object_page_iter and calls __i915_gem_object_page_iter_get_sg().
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * Recommended to use wrapper macro: i915_gem_object_get_sg()
 * See also __i915_gem_object_page_iter_get_sg()
 */
static inline struct scatterlist *
__i915_gem_object_get_sg(struct drm_i915_gem_object *obj, pgoff_t n,
			 unsigned int *offset)
{
	return __i915_gem_object_page_iter_get_sg(obj, &obj->mm.get_page, n, offset);
}

/**
 * i915_gem_object_get_sg - wrapper macro for __i915_gem_object_get_sg()
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_sg().
 * See also __i915_gem_object_page_iter_get_sg()
 */
#define i915_gem_object_get_sg(obj, n, offset) ({	\
	static_assert(castable_to_type(n, pgoff_t));	\
	__i915_gem_object_get_sg(obj, n, offset);	\
})

/**
 * __i915_gem_object_get_sg_dma - helper to find the target scatterlist
 * pointer and the target page position using pgoff_t n input argument and
 * drm_i915_gem_object. It uses an internal DMA mapped scatterlist lookup function
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * It uses drm_i915_gem_object's internal DMA mapped scatterlist lookup function
 * as i915_gem_object_page_iter and calls __i915_gem_object_page_iter_get_sg().
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * Recommended to use wrapper macro: i915_gem_object_get_sg_dma()
 * See also __i915_gem_object_page_iter_get_sg()
 */
static inline struct scatterlist *
__i915_gem_object_get_sg_dma(struct drm_i915_gem_object *obj, pgoff_t n,
			     unsigned int *offset)
{
	return __i915_gem_object_page_iter_get_sg(obj, &obj->mm.get_dma_page, n, offset);
}

/**
 * i915_gem_object_get_sg_dma - wrapper macro for __i915_gem_object_get_sg_dma()
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @offset: searched physical offset,
 *          it will be used for returning physical page offset value
 *
 * Returns:
 * The target scatterlist pointer and the target page position.
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_sg_dma().
 * See also __i915_gem_object_page_iter_get_sg()
 */
#define i915_gem_object_get_sg_dma(obj, n, offset) ({	\
	static_assert(castable_to_type(n, pgoff_t));	\
	__i915_gem_object_get_sg_dma(obj, n, offset);	\
})

/**
 * __i915_gem_object_get_page - helper to find the target page with a page offset
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * It uses drm_i915_gem_object's internal shmem scatterlist lookup function as
 * i915_gem_object_page_iter and calls __i915_gem_object_page_iter_get_sg()
 * internally.
 *
 * Returns:
 * The target page pointer.
 *
 * Recommended to use wrapper macro: i915_gem_object_get_page()
 * See also __i915_gem_object_page_iter_get_sg()
 */
struct page *
__i915_gem_object_get_page(struct drm_i915_gem_object *obj, pgoff_t n);

/**
 * i915_gem_object_get_page - wrapper macro for __i915_gem_object_get_page
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * Returns:
 * The target page pointer.
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_page().
 * See also __i915_gem_object_page_iter_get_sg()
 */
#define i915_gem_object_get_page(obj, n) ({		\
	static_assert(castable_to_type(n, pgoff_t));	\
	__i915_gem_object_get_page(obj, n);		\
})

/**
 * __i915_gem_object_get_dirty_page - helper to find the target page with a page
 * offset
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * It works like i915_gem_object_get_page(), but it marks the returned page dirty.
 *
 * Returns:
 * The target page pointer.
 *
 * Recommended to use wrapper macro: i915_gem_object_get_dirty_page()
 * See also __i915_gem_object_page_iter_get_sg() and __i915_gem_object_get_page()
 */
struct page *
__i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj, pgoff_t n);

/**
 * i915_gem_object_get_dirty_page - wrapper macro for __i915_gem_object_get_dirty_page
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * Returns:
 * The target page pointer.
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_dirty_page().
 * See also __i915_gem_object_page_iter_get_sg() and __i915_gem_object_get_page()
 */
#define i915_gem_object_get_dirty_page(obj, n) ({	\
	static_assert(castable_to_type(n, pgoff_t));	\
	__i915_gem_object_get_dirty_page(obj, n);	\
})

/**
 * __i915_gem_object_get_dma_address_len - helper to get bus addresses of
 * targeted DMA mapped scatterlist from i915 GEM buffer object and it's length
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @len: DMA mapped scatterlist's DMA bus addresses length to return
 *
 * Returns:
 * Bus addresses of targeted DMA mapped scatterlist
 *
 * Recommended to use wrapper macro: i915_gem_object_get_dma_address_len()
 * See also __i915_gem_object_page_iter_get_sg() and __i915_gem_object_get_sg_dma()
 */
dma_addr_t
__i915_gem_object_get_dma_address_len(struct drm_i915_gem_object *obj, pgoff_t n,
				      unsigned int *len);

/**
 * i915_gem_object_get_dma_address_len - wrapper macro for
 * __i915_gem_object_get_dma_address_len
 * @obj: i915 GEM buffer object
 * @n: page offset
 * @len: DMA mapped scatterlist's DMA bus addresses length to return
 *
 * Returns:
 * Bus addresses of targeted DMA mapped scatterlist
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_dma_address_len().
 * See also __i915_gem_object_page_iter_get_sg() and
 * __i915_gem_object_get_dma_address_len()
 */
#define i915_gem_object_get_dma_address_len(obj, n, len) ({	\
	static_assert(castable_to_type(n, pgoff_t));		\
	__i915_gem_object_get_dma_address_len(obj, n, len);	\
})

/**
 * __i915_gem_object_get_dma_address - helper to get bus addresses of
 * targeted DMA mapped scatterlist from i915 GEM buffer object
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * Returns:
 * Bus addresses of targeted DMA mapped scatterlis
 *
 * Recommended to use wrapper macro: i915_gem_object_get_dma_address()
 * See also __i915_gem_object_page_iter_get_sg() and __i915_gem_object_get_sg_dma()
 */
dma_addr_t
__i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj, pgoff_t n);

/**
 * i915_gem_object_get_dma_address - wrapper macro for
 * __i915_gem_object_get_dma_address
 * @obj: i915 GEM buffer object
 * @n: page offset
 *
 * Returns:
 * Bus addresses of targeted DMA mapped scatterlist
 *
 * In order to avoid the truncation of the input parameter, it checks the page
 * offset n's type from the input parameter before calling
 * __i915_gem_object_get_dma_address().
 * See also __i915_gem_object_page_iter_get_sg() and
 * __i915_gem_object_get_dma_address()
 */
#define i915_gem_object_get_dma_address(obj, n) ({	\
	static_assert(castable_to_type(n, pgoff_t));	\
	__i915_gem_object_get_dma_address(obj, n);	\
})

void __i915_gem_object_set_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages);

int ____i915_gem_object_get_pages(struct drm_i915_gem_object *obj);
int __i915_gem_object_get_pages(struct drm_i915_gem_object *obj);

static inline int __must_check
i915_gem_object_pin_pages(struct drm_i915_gem_object *obj)
{
	assert_object_held(obj);

	if (atomic_inc_not_zero(&obj->mm.pages_pin_count))
		return 0;

	return __i915_gem_object_get_pages(obj);
}

int i915_gem_object_pin_pages_unlocked(struct drm_i915_gem_object *obj);

static inline bool
i915_gem_object_has_pages(struct drm_i915_gem_object *obj)
{
	return !IS_ERR_OR_NULL(READ_ONCE(obj->mm.pages));
}

static inline void
__i915_gem_object_pin_pages(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));

	atomic_inc(&obj->mm.pages_pin_count);
}

static inline bool
i915_gem_object_has_pinned_pages(struct drm_i915_gem_object *obj)
{
	return atomic_read(&obj->mm.pages_pin_count);
}

static inline void
__i915_gem_object_unpin_pages(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	atomic_dec(&obj->mm.pages_pin_count);
}

static inline void
i915_gem_object_unpin_pages(struct drm_i915_gem_object *obj)
{
	__i915_gem_object_unpin_pages(obj);
}

int __i915_gem_object_put_pages(struct drm_i915_gem_object *obj);
int i915_gem_object_truncate(struct drm_i915_gem_object *obj);

/**
 * i915_gem_object_pin_map - return a contiguous mapping of the entire object
 * @obj: the object to map into kernel address space
 * @type: the type of mapping, used to select pgprot_t
 *
 * Calls i915_gem_object_pin_pages() to prevent reaping of the object's
 * pages and then returns a contiguous mapping of the backing storage into
 * the kernel address space. Based on the @type of mapping, the PTE will be
 * set to either WriteBack or WriteCombine (via pgprot_t).
 *
 * The caller is responsible for calling i915_gem_object_unpin_map() when the
 * mapping is no longer required.
 *
 * Returns the pointer through which to access the mapped object, or an
 * ERR_PTR() on error.
 */
void *__must_check i915_gem_object_pin_map(struct drm_i915_gem_object *obj,
					   enum i915_map_type type);

void *__must_check i915_gem_object_pin_map_unlocked(struct drm_i915_gem_object *obj,
						    enum i915_map_type type);

void __i915_gem_object_flush_map(struct drm_i915_gem_object *obj,
				 unsigned long offset,
				 unsigned long size);
static inline void i915_gem_object_flush_map(struct drm_i915_gem_object *obj)
{
	__i915_gem_object_flush_map(obj, 0, obj->base.size);
}

/**
 * i915_gem_object_unpin_map - releases an earlier mapping
 * @obj: the object to unmap
 *
 * After pinning the object and mapping its pages, once you are finished
 * with your access, call i915_gem_object_unpin_map() to release the pin
 * upon the mapping. Once the pin count reaches zero, that mapping may be
 * removed.
 */
static inline void i915_gem_object_unpin_map(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin_pages(obj);
}

void __i915_gem_object_release_map(struct drm_i915_gem_object *obj);

int i915_gem_object_prepare_read(struct drm_i915_gem_object *obj,
				 unsigned int *needs_clflush);
int i915_gem_object_prepare_write(struct drm_i915_gem_object *obj,
				  unsigned int *needs_clflush);
#define CLFLUSH_BEFORE	BIT(0)
#define CLFLUSH_AFTER	BIT(1)
#define CLFLUSH_FLAGS	(CLFLUSH_BEFORE | CLFLUSH_AFTER)

static inline void
i915_gem_object_finish_access(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin_pages(obj);
}

int i915_gem_object_get_moving_fence(struct drm_i915_gem_object *obj,
				     struct dma_fence **fence);
int i915_gem_object_wait_moving_fence(struct drm_i915_gem_object *obj,
				      bool intr);
bool i915_gem_object_has_unknown_state(struct drm_i915_gem_object *obj);

void i915_gem_object_set_cache_coherency(struct drm_i915_gem_object *obj,
					 unsigned int cache_level);
void i915_gem_object_set_pat_index(struct drm_i915_gem_object *obj,
				   unsigned int pat_index);
bool i915_gem_object_can_bypass_llc(struct drm_i915_gem_object *obj);
void i915_gem_object_flush_if_display(struct drm_i915_gem_object *obj);
void i915_gem_object_flush_if_display_locked(struct drm_i915_gem_object *obj);
bool i915_gem_cpu_write_needs_clflush(struct drm_i915_gem_object *obj);

int __must_check
i915_gem_object_set_to_wc_domain(struct drm_i915_gem_object *obj, bool write);
int __must_check
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj, bool write);
int __must_check
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write);
struct i915_vma * __must_check
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     struct i915_gem_ww_ctx *ww,
				     u32 alignment,
				     const struct i915_gtt_view *view,
				     unsigned int flags);

void i915_gem_object_make_unshrinkable(struct drm_i915_gem_object *obj);
void i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj);
void __i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj);
void __i915_gem_object_make_purgeable(struct drm_i915_gem_object *obj);
void i915_gem_object_make_purgeable(struct drm_i915_gem_object *obj);

static inline void __start_cpu_write(struct drm_i915_gem_object *obj)
{
	obj->read_domains = I915_GEM_DOMAIN_CPU;
	obj->write_domain = I915_GEM_DOMAIN_CPU;
	if (i915_gem_cpu_write_needs_clflush(obj))
		obj->cache_dirty = true;
}

void i915_gem_fence_wait_priority(struct dma_fence *fence,
				  const struct i915_sched_attr *attr);

int i915_gem_object_wait(struct drm_i915_gem_object *obj,
			 unsigned int flags,
			 long timeout);
int i915_gem_object_wait_priority(struct drm_i915_gem_object *obj,
				  unsigned int flags,
				  const struct i915_sched_attr *attr);

void __i915_gem_object_flush_frontbuffer(struct drm_i915_gem_object *obj,
					 enum fb_op_origin origin);
void __i915_gem_object_invalidate_frontbuffer(struct drm_i915_gem_object *obj,
					      enum fb_op_origin origin);

static inline void
i915_gem_object_flush_frontbuffer(struct drm_i915_gem_object *obj,
				  enum fb_op_origin origin)
{
	if (unlikely(rcu_access_pointer(obj->frontbuffer)))
		__i915_gem_object_flush_frontbuffer(obj, origin);
}

static inline void
i915_gem_object_invalidate_frontbuffer(struct drm_i915_gem_object *obj,
				       enum fb_op_origin origin)
{
	if (unlikely(rcu_access_pointer(obj->frontbuffer)))
		__i915_gem_object_invalidate_frontbuffer(obj, origin);
}

int i915_gem_object_read_from_page(struct drm_i915_gem_object *obj, u64 offset, void *dst, int size);

bool i915_gem_object_is_shmem(const struct drm_i915_gem_object *obj);

void __i915_gem_free_object_rcu(struct rcu_head *head);

void __i915_gem_object_pages_fini(struct drm_i915_gem_object *obj);

void __i915_gem_free_object(struct drm_i915_gem_object *obj);

bool i915_gem_object_evictable(struct drm_i915_gem_object *obj);

bool i915_gem_object_migratable(struct drm_i915_gem_object *obj);

int i915_gem_object_migrate(struct drm_i915_gem_object *obj,
			    struct i915_gem_ww_ctx *ww,
			    enum intel_region_id id);
int __i915_gem_object_migrate(struct drm_i915_gem_object *obj,
			      struct i915_gem_ww_ctx *ww,
			      enum intel_region_id id,
			      unsigned int flags);

bool i915_gem_object_can_migrate(struct drm_i915_gem_object *obj,
				 enum intel_region_id id);

int i915_gem_object_wait_migration(struct drm_i915_gem_object *obj,
				   unsigned int flags);

bool i915_gem_object_placement_possible(struct drm_i915_gem_object *obj,
					enum intel_memory_type type);

bool i915_gem_object_needs_ccs_pages(struct drm_i915_gem_object *obj);

int shmem_sg_alloc_table(struct drm_i915_private *i915, struct sg_table *st,
			 size_t size, struct intel_memory_region *mr,
			 struct address_space *mapping,
			 unsigned int max_segment);
void shmem_sg_free_table(struct sg_table *st, struct address_space *mapping,
			 bool dirty, bool backup);
void __shmem_writeback(size_t size, struct address_space *mapping);

#ifdef CONFIG_MMU_NOTIFIER
static inline bool
i915_gem_object_is_userptr(struct drm_i915_gem_object *obj)
{
	return obj->userptr.notifier.mm;
}

int i915_gem_object_userptr_submit_init(struct drm_i915_gem_object *obj);
int i915_gem_object_userptr_submit_done(struct drm_i915_gem_object *obj);
int i915_gem_object_userptr_validate(struct drm_i915_gem_object *obj);
#else
static inline bool i915_gem_object_is_userptr(struct drm_i915_gem_object *obj) { return false; }

static inline int i915_gem_object_userptr_submit_init(struct drm_i915_gem_object *obj) { GEM_BUG_ON(1); return -ENODEV; }
static inline int i915_gem_object_userptr_submit_done(struct drm_i915_gem_object *obj) { GEM_BUG_ON(1); return -ENODEV; }
static inline int i915_gem_object_userptr_validate(struct drm_i915_gem_object *obj) { GEM_BUG_ON(1); return -ENODEV; }

#endif

/**
 * i915_gem_object_get_frontbuffer - Get the object's frontbuffer
 * @obj: The object whose frontbuffer to get.
 *
 * Get pointer to object's frontbuffer if such exists. Please note that RCU
 * mechanism is used to handle e.g. ongoing removal of frontbuffer pointer.
 *
 * Return: pointer to object's frontbuffer is such exists or NULL
 */
static inline struct intel_frontbuffer *
i915_gem_object_get_frontbuffer(const struct drm_i915_gem_object *obj)
{
	struct intel_frontbuffer *front;

	if (likely(!rcu_access_pointer(obj->frontbuffer)))
		return NULL;

	rcu_read_lock();
	do {
		front = rcu_dereference(obj->frontbuffer);
		if (!front)
			break;

		if (unlikely(!kref_get_unless_zero(&front->ref)))
			continue;

		if (likely(front == rcu_access_pointer(obj->frontbuffer)))
			break;

		intel_frontbuffer_put(front);
	} while (1);
	rcu_read_unlock();

	return front;
}

/**
 * i915_gem_object_set_frontbuffer - Set the object's frontbuffer
 * @obj: The object whose frontbuffer to set.
 * @front: The frontbuffer to set
 *
 * Set object's frontbuffer pointer. If frontbuffer is already set for the
 * object keep it and return it's pointer to the caller. Please note that RCU
 * mechanism is used to handle e.g. ongoing removal of frontbuffer pointer. This
 * function is protected by i915->display.fb_tracking.lock
 *
 * Return: pointer to frontbuffer which was set.
 */
static inline struct intel_frontbuffer *
i915_gem_object_set_frontbuffer(struct drm_i915_gem_object *obj,
				struct intel_frontbuffer *front)
{
	struct intel_frontbuffer *cur = front;

	if (!front) {
		RCU_INIT_POINTER(obj->frontbuffer, NULL);
	} else if (rcu_access_pointer(obj->frontbuffer)) {
		cur = rcu_dereference_protected(obj->frontbuffer, true);
		kref_get(&cur->ref);
	} else {
		drm_gem_object_get(intel_bo_to_drm_bo(obj));
		rcu_assign_pointer(obj->frontbuffer, front);
	}

	return cur;
}

#endif
