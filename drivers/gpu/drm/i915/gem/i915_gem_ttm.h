/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _I915_GEM_TTM_H_
#define _I915_GEM_TTM_H_

#include <drm/ttm/ttm_placement.h>

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
	if (bo->destroy != i915_ttm_bo_destroy)
		return NULL;

	return container_of(bo, struct drm_i915_gem_object, __do_not_access);
}

int __i915_gem_ttm_object_init(struct intel_memory_region *mem,
			       struct drm_i915_gem_object *obj,
			       resource_size_t size,
			       resource_size_t page_size,
			       unsigned int flags);

/* Internal I915 TTM declarations and definitions below. */

#define I915_PL_LMEM0 TTM_PL_PRIV
#define I915_PL_SYSTEM TTM_PL_SYSTEM
#define I915_PL_STOLEN TTM_PL_VRAM
#define I915_PL_GGTT TTM_PL_TT

struct ttm_placement *i915_ttm_sys_placement(void);

void i915_ttm_free_cached_io_rsgt(struct drm_i915_gem_object *obj);

struct i915_refct_sgt *
i915_ttm_resource_get_st(struct drm_i915_gem_object *obj,
			 struct ttm_resource *res);

void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj);

int i915_ttm_purge(struct drm_i915_gem_object *obj);

/**
 * i915_ttm_gtt_binds_lmem - Should the memory be viewed as LMEM by the GTT?
 * @mem: struct ttm_resource representing the memory.
 *
 * Return: true if memory should be viewed as LMEM for GTT binding purposes,
 * false otherwise.
 */
static inline bool i915_ttm_gtt_binds_lmem(struct ttm_resource *mem)
{
	return mem->mem_type != I915_PL_SYSTEM;
}

/**
 * i915_ttm_cpu_maps_iomem - Should the memory be viewed as IOMEM by the CPU?
 * @mem: struct ttm_resource representing the memory.
 *
 * Return: true if memory should be viewed as IOMEM for CPU mapping purposes.
 */
static inline bool i915_ttm_cpu_maps_iomem(struct ttm_resource *mem)
{
	/* Once / if we support GGTT, this is also false for cached ttm_tts */
	return mem->mem_type != I915_PL_SYSTEM;
}
#endif
