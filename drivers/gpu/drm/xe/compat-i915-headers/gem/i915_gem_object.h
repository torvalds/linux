/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _I915_GEM_OBJECT_H_
#define _I915_GEM_OBJECT_H_

#include <linux/types.h>

#include "xe_bo.h"

#define i915_gem_object_is_shmem(obj) (0) /* We don't use shmem */

static inline dma_addr_t i915_gem_object_get_dma_address(const struct xe_bo *bo, pgoff_t n)
{
	/* Should never be called */
	WARN_ON(1);
	return n;
}

static inline bool i915_gem_object_is_tiled(const struct xe_bo *bo)
{
	/* legacy tiling is unused */
	return false;
}

static inline bool i915_gem_object_is_userptr(const struct xe_bo *bo)
{
	/* legacy tiling is unused */
	return false;
}

#endif
