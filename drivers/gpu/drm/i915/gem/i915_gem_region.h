/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_REGION_H__
#define __I915_GEM_REGION_H__

#include <linux/types.h>

struct intel_memory_region;
struct drm_i915_gem_object;
struct sg_table;

struct i915_gem_apply_to_region;

#define I915_BO_INVALID_OFFSET ((resource_size_t)-1)

/**
 * struct i915_gem_apply_to_region_ops - ops to use when iterating over all
 * region objects.
 */
struct i915_gem_apply_to_region_ops {
	/**
	 * @process_obj: Process the current object
	 *
	 * Note that if this function is part of a ww transaction, and
	 * if returns -EDEADLK for one of the objects, it may be
	 * rerun for that same object in the same pass.
	 */
	int (*process_obj)(struct i915_gem_apply_to_region *apply,
			   struct drm_i915_gem_object *obj);
};

/**
 * struct i915_gem_apply_to_region - Argument to the struct
 * i915_gem_apply_to_region_ops functions.
 * @ops: The ops for the operation.
 * @ww: Locking context used for the transaction.
 * @interruptible: Whether to perform object locking interruptible.
 *
 * This structure is intended to be embedded in a private struct if needed
 */
struct i915_gem_apply_to_region {
	const struct i915_gem_apply_to_region_ops *ops;
	struct i915_gem_ww_ctx *ww;
	u32 interruptible:1;
};

void i915_gem_object_init_memory_region(struct drm_i915_gem_object *obj,
					struct intel_memory_region *mem);
void i915_gem_object_release_memory_region(struct drm_i915_gem_object *obj);

struct drm_i915_gem_object *
i915_gem_object_create_region(struct intel_memory_region *mem,
			      resource_size_t size,
			      resource_size_t page_size,
			      unsigned int flags);
struct drm_i915_gem_object *
i915_gem_object_create_region_at(struct intel_memory_region *mem,
				 resource_size_t offset,
				 resource_size_t size,
				 unsigned int flags);

int i915_gem_process_region(struct intel_memory_region *mr,
			    struct i915_gem_apply_to_region *apply);
#endif
