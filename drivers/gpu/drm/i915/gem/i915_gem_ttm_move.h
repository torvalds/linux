/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _I915_GEM_TTM_MOVE_H_
#define _I915_GEM_TTM_MOVE_H_

#include <linux/types.h>

#include "i915_selftest.h"

struct ttm_buffer_object;
struct ttm_operation_ctx;
struct ttm_place;
struct ttm_resource;
struct ttm_tt;

struct drm_i915_gem_object;
struct i915_refct_sgt;

int i915_ttm_move_notify(struct ttm_buffer_object *bo);

I915_SELFTEST_DECLARE(void i915_ttm_migrate_set_failure_modes(bool gpu_migration,
							      bool work_allocation));

int i915_gem_obj_copy_ttm(struct drm_i915_gem_object *dst,
			  struct drm_i915_gem_object *src,
			  bool allow_accel, bool intr);

/* Internal I915 TTM declarations and definitions below. */

int i915_ttm_move(struct ttm_buffer_object *bo, bool evict,
		  struct ttm_operation_ctx *ctx,
		  struct ttm_resource *dst_mem,
		  struct ttm_place *hop);

void i915_ttm_adjust_domains_after_move(struct drm_i915_gem_object *obj);

void i915_ttm_adjust_gem_after_move(struct drm_i915_gem_object *obj);

#endif
