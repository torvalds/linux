// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include "gem/i915_gem_object.h"
#include "intel_bo.h"

bool intel_bo_is_tiled(struct drm_gem_object *obj)
{
	return i915_gem_object_is_tiled(to_intel_bo(obj));
}
