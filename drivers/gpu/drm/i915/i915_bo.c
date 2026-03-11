// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/drm_panic.h>
#include <drm/intel/display_parent_interface.h>

#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_object.h"
#include "gem/i915_gem_object_frontbuffer.h"
#include "pxp/intel_pxp.h"

#include "i915_bo.h"
#include "i915_debugfs.h"

static bool i915_bo_is_tiled(struct drm_gem_object *obj)
{
	return i915_gem_object_is_tiled(to_intel_bo(obj));
}

static bool i915_bo_is_userptr(struct drm_gem_object *obj)
{
	return i915_gem_object_is_userptr(to_intel_bo(obj));
}

static bool i915_bo_is_shmem(struct drm_gem_object *obj)
{
	return i915_gem_object_is_shmem(to_intel_bo(obj));
}

static bool i915_bo_is_protected(struct drm_gem_object *obj)
{
	return i915_gem_object_is_protected(to_intel_bo(obj));
}

static int i915_bo_key_check(struct drm_gem_object *obj)
{
	return intel_pxp_key_check(obj, false);
}

static int i915_bo_fb_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	return i915_gem_fb_mmap(to_intel_bo(obj), vma);
}

static int i915_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size)
{
	return i915_gem_object_read_from_page(to_intel_bo(obj), offset, dst, size);
}

static void i915_bo_describe(struct seq_file *m, struct drm_gem_object *obj)
{
	i915_debugfs_describe_obj(m, to_intel_bo(obj));
}

const struct intel_display_bo_interface i915_display_bo_interface = {
	.is_tiled = i915_bo_is_tiled,
	.is_userptr = i915_bo_is_userptr,
	.is_shmem = i915_bo_is_shmem,
	.is_protected = i915_bo_is_protected,
	.key_check = i915_bo_key_check,
	.fb_mmap = i915_bo_fb_mmap,
	.read_from_page = i915_bo_read_from_page,
	.describe = i915_bo_describe,
};
