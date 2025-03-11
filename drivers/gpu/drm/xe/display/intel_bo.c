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

bool intel_bo_is_protected(struct drm_gem_object *obj)
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

int intel_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);

	return xe_bo_read(bo, offset, dst, size);
}

struct intel_frontbuffer *intel_bo_get_frontbuffer(struct drm_gem_object *obj)
{
	return NULL;
}

struct intel_frontbuffer *intel_bo_set_frontbuffer(struct drm_gem_object *obj,
						   struct intel_frontbuffer *front)
{
	return front;
}

void intel_bo_describe(struct seq_file *m, struct drm_gem_object *obj)
{
	/* FIXME */
}
