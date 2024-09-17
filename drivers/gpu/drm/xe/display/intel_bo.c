// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_gem.h>

#include "intel_bo.h"

bool intel_bo_is_tiled(struct drm_gem_object *obj)
{
	/* legacy tiling is unused */
	return false;
}
