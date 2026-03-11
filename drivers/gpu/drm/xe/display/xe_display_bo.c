// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_gem.h>
#include <drm/intel/display_parent_interface.h>

#include "xe_bo.h"
#include "xe_display_bo.h"
#include "xe_pxp.h"

static bool xe_display_bo_is_protected(struct drm_gem_object *obj)
{
	return xe_bo_is_protected(gem_to_xe_bo(obj));
}

static int xe_display_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);

	return xe_bo_read(bo, offset, dst, size);
}

const struct intel_display_bo_interface xe_display_bo_interface = {
	.is_protected = xe_display_bo_is_protected,
	.key_check = xe_pxp_obj_key_check,
	.fb_mmap = drm_gem_prime_mmap,
	.read_from_page = xe_display_bo_read_from_page,
};
