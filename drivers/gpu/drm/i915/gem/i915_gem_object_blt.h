/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_OBJECT_BLT_H__
#define __I915_GEM_OBJECT_BLT_H__

#include <linux/types.h>

#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "i915_vma.h"

struct drm_i915_gem_object;

struct i915_vma *intel_emit_vma_fill_blt(struct intel_context *ce,
					 struct i915_vma *vma,
					 u32 value);

struct i915_vma *intel_emit_vma_copy_blt(struct intel_context *ce,
					 struct i915_vma *src,
					 struct i915_vma *dst);

int intel_emit_vma_mark_active(struct i915_vma *vma, struct i915_request *rq);
void intel_emit_vma_release(struct intel_context *ce, struct i915_vma *vma);

int i915_gem_object_fill_blt(struct drm_i915_gem_object *obj,
			     struct intel_context *ce,
			     u32 value);

int i915_gem_object_copy_blt(struct drm_i915_gem_object *src,
			     struct drm_i915_gem_object *dst,
			     struct intel_context *ce);

#endif
