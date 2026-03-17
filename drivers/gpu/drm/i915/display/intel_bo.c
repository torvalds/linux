// SPDX-License-Identifier: MIT
/* Copyright © 2026 Intel Corporation */

#include <drm/drm_gem.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_bo.h"
#include "intel_display_core.h"
#include "intel_display_types.h"

bool intel_bo_is_tiled(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->is_tiled && display->parent->bo->is_tiled(obj);
}

bool intel_bo_is_userptr(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->is_userptr && display->parent->bo->is_userptr(obj);
}

bool intel_bo_is_shmem(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->is_shmem && display->parent->bo->is_shmem(obj);
}

bool intel_bo_is_protected(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->is_protected(obj);
}

int intel_bo_key_check(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->key_check(obj);
}

int intel_bo_fb_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->fb_mmap(obj, vma);
}

int intel_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->read_from_page(obj, offset, dst, size);
}

void intel_bo_describe(struct seq_file *m, struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	if (display->parent->bo->describe)
		display->parent->bo->describe(m, obj);
}

int intel_bo_framebuffer_init(struct drm_gem_object *obj, struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct intel_display *display = to_intel_display(obj->dev);

	return display->parent->bo->framebuffer_init(obj, mode_cmd);
}

void intel_bo_framebuffer_fini(struct drm_gem_object *obj)
{
	struct intel_display *display = to_intel_display(obj->dev);

	display->parent->bo->framebuffer_fini(obj);
}

struct drm_gem_object *intel_bo_framebuffer_lookup(struct intel_display *display,
						   struct drm_file *filp,
						   const struct drm_mode_fb_cmd2 *user_mode_cmd)
{
	return display->parent->bo->framebuffer_lookup(display->drm, filp, user_mode_cmd);
}
