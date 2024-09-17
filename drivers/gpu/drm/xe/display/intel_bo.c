// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_gem.h>

#include "xe_bo.h"
#include "intel_bo.h"

bool intel_bo_is_tiled(struct drm_gem_object *obj)
{
	/* legacy tiling is unused */
	return false;
}

bool intel_bo_is_userptr(struct drm_gem_object *obj)
{
	/* xe does not have userptr bos */
	return false;
}

bool intel_bo_is_shmem(struct drm_gem_object *obj)
{
	return false;
}

void intel_bo_flush_if_display(struct drm_gem_object *obj)
{
}

int intel_bo_fb_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	return drm_gem_prime_mmap(obj, vma);
}
