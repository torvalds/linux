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

#include <drm/i915_drm.h>

#include "gem/i915_gem_object_types.h"

struct drm_i915_gem_object *i915_gem_object_alloc(void);
void i915_gem_object_free(struct drm_i915_gem_object *obj);

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
i915_gem_object_needs_async_cancel(const struct drm_i915_gem_object *obj)
{
	return obj->ops->flags & I915_GEM_OBJECT_ASYNC_CANCEL;
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

void __i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				     struct sg_table *pages,
				     bool needs_clflush);

#endif
