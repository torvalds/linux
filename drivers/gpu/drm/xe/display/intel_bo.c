// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_gem.h>

#include "intel_bo.h"
#include "intel_frontbuffer.h"
#include "xe_bo.h"
#include "xe_pxp.h"

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
	return xe_bo_is_protected(gem_to_xe_bo(obj));
}

int intel_bo_key_check(struct drm_gem_object *obj)
{
	return xe_pxp_obj_key_check(obj);
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

struct xe_frontbuffer {
	struct intel_frontbuffer base;
	struct drm_gem_object *obj;
	struct kref ref;
};

struct intel_frontbuffer *intel_bo_frontbuffer_get(struct drm_gem_object *obj)
{
	struct xe_frontbuffer *front;

	front = kmalloc(sizeof(*front), GFP_KERNEL);
	if (!front)
		return NULL;

	intel_frontbuffer_init(&front->base, obj->dev);

	kref_init(&front->ref);

	drm_gem_object_get(obj);
	front->obj = obj;

	return &front->base;
}

void intel_bo_frontbuffer_ref(struct intel_frontbuffer *_front)
{
	struct xe_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	kref_get(&front->ref);
}

static void frontbuffer_release(struct kref *ref)
{
	struct xe_frontbuffer *front =
		container_of(ref, typeof(*front), ref);

	intel_frontbuffer_fini(&front->base);

	drm_gem_object_put(front->obj);

	kfree(front);
}

void intel_bo_frontbuffer_put(struct intel_frontbuffer *_front)
{
	struct xe_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	kref_put(&front->ref, frontbuffer_release);
}

void intel_bo_frontbuffer_flush_for_display(struct intel_frontbuffer *front)
{
}

void intel_bo_describe(struct seq_file *m, struct drm_gem_object *obj)
{
	/* FIXME */
}
