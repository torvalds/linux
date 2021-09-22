/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _I915_GEM_TTM_PM_H_
#define _I915_GEM_TTM_PM_H_

#include <linux/types.h>

struct intel_memory_region;
struct drm_i915_gem_object;

#define I915_TTM_BACKUP_ALLOW_GPU BIT(0)
#define I915_TTM_BACKUP_PINNED    BIT(1)

int i915_ttm_backup_region(struct intel_memory_region *mr, u32 flags);

void i915_ttm_recover_region(struct intel_memory_region *mr);

int i915_ttm_restore_region(struct intel_memory_region *mr, u32 flags);

/* Internal I915 TTM functions below. */
void i915_ttm_backup_free(struct drm_i915_gem_object *obj);

#endif
