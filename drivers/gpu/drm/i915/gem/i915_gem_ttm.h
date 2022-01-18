/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _I915_GEM_TTM_H_
#define _I915_GEM_TTM_H_

#include "gem/i915_gem_object_types.h"

/**
 * i915_gem_to_ttm - Convert a struct drm_i915_gem_object to a
 * struct ttm_buffer_object.
 * @obj: Pointer to the gem object.
 *
 * Return: Pointer to the embedded struct ttm_buffer_object.
 */
static inline struct ttm_buffer_object *
i915_gem_to_ttm(struct drm_i915_gem_object *obj)
{
	return &obj->__do_not_access;
}

/*
 * i915 ttm gem object destructor. Internal use only.
 */
void i915_ttm_bo_destroy(struct ttm_buffer_object *bo);

/**
 * i915_ttm_to_gem - Convert a struct ttm_buffer_object to an embedding
 * struct drm_i915_gem_object.
 *
 * Return: Pointer to the embedding struct ttm_buffer_object, or NULL
 * if the object was not an i915 ttm object.
 */
static inline struct drm_i915_gem_object *
i915_ttm_to_gem(struct ttm_buffer_object *bo)
{
	if (GEM_WARN_ON(bo->destroy != i915_ttm_bo_destroy))
		return NULL;

	return container_of(bo, struct drm_i915_gem_object, __do_not_access);
}

int __i915_gem_ttm_object_init(struct intel_memory_region *mem,
			       struct drm_i915_gem_object *obj,
			       resource_size_t size,
			       resource_size_t page_size,
			       unsigned int flags);

int i915_gem_obj_copy_ttm(struct drm_i915_gem_object *dst,
			  struct drm_i915_gem_object *src,
			  bool allow_accel, bool intr);

/* Internal I915 TTM declarations and definitions below. */

#define I915_PL_LMEM0 TTM_PL_PRIV
#define I915_PL_SYSTEM TTM_PL_SYSTEM
#define I915_PL_STOLEN TTM_PL_VRAM
#define I915_PL_GGTT TTM_PL_TT

struct ttm_placement *i915_ttm_sys_placement(void);

#endif
