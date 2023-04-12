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

/*
 * XXX: There is a prevalence of the assumption that we fit the
 * object's page count inside a 32bit _signed_ variable. Let's document
 * this and catch if we ever need to fix it. In the meantime, if you do
 * spot such a local variable, please consider fixing!
 *
 * Aside from our own locals (for which we have no excuse!):
 * - sg_table embeds unsigned int for num_pages
 * - get_user_pages*() mixed ints with longs
 */
#define GEM_CHECK_SIZE_OVERFLOW(sz) \
	GEM_WARN_ON((sz) >> PAGE_SHIFT > INT_MAX)

static inline bool i915_gem_object_size_2big(u64 size)
{
	struct drm_i915_gem_object *obj;

	if (GEM_CHECK_SIZE_OVERFLOW(size))
		return true;

	if (overflows_type(size, obj->base.size))
		return true;

	return false;
}

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

struct scatterlist *
__i915_gem_object_get_sg(struct drm_i915_gem_object *obj,
			 struct i915_gem_object_page_iter *iter,
			 unsigned int n,
			 unsigned int *offset, bool dma);

static inline struct scatterlist *
i915_gem_object_get_sg(struct drm_i915_gem_object *obj,
		       unsigned int n,
		       unsigned int *offset)
{
	return __i915_gem_object_get_sg(obj, &obj->mm.get_page, n, offset, false);
}

static inline struct scatterlist *
i915_gem_object_get_sg_dma(struct drm_i915_gem_object *obj,
			   unsigned int n,
			   unsigned int *offset)
{
	return __i915_gem_object_get_sg(obj, &obj->mm.get_dma_page, n, offset, true);
}

struct page *
i915_gem_object_get_page(struct drm_i915_gem_object *obj,
			 unsigned int n);

struct page *
i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj,
			       unsigned int n);

dma_addr_t
i915_gem_object_get_dma_address_len(struct drm_i915_gem_object *obj,
				    unsigned long n,
				    unsigned int *len);

dma_addr_t
i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj,
				unsigned long n);

void __i915_gem_object_set_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages,
				 unsigned int sg_page_sizes);

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

#endif
