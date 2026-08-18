// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <linux/fb.h>

#include <drm/drm_panic.h>
#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "display/intel_fb.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_object.h"
#include "gem/i915_gem_object_frontbuffer.h"
#include "pxp/intel_pxp.h"

#include "i915_bo.h"
#include "i915_debugfs.h"
#include "i915_drv.h"

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

static int i915_bo_framebuffer_init(struct drm_gem_object *_obj,
				    struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned int tiling, stride;

	i915_gem_object_lock(obj, NULL);
	tiling = i915_gem_object_get_tiling(obj);
	stride = i915_gem_object_get_stride(obj);
	i915_gem_object_unlock(obj);

	if (mode_cmd->flags & DRM_MODE_FB_MODIFIERS) {
		/*
		 * If there's a fence, enforce that
		 * the fb modifier and tiling mode match.
		 */
		if (tiling != I915_TILING_NONE &&
		    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
			drm_dbg_kms(&i915->drm,
				    "tiling_mode doesn't match fb modifier\n");
			return -EINVAL;
		}
	} else {
		if (tiling == I915_TILING_X) {
			mode_cmd->modifier[0] = I915_FORMAT_MOD_X_TILED;
		} else if (tiling == I915_TILING_Y) {
			drm_dbg_kms(&i915->drm,
				    "No Y tiling for legacy addfb\n");
			return -EINVAL;
		}
	}

	/*
	 * gen2/3 display engine uses the fence if present,
	 * so the tiling mode must match the fb modifier exactly.
	 */
	if (GRAPHICS_VER(i915) < 4 &&
	    tiling != intel_fb_modifier_to_tiling(mode_cmd->modifier[0])) {
		drm_dbg_kms(&i915->drm,
			    "tiling_mode must match fb modifier exactly on gen2/3\n");
		return -EINVAL;
	}

	/*
	 * If there's a fence, enforce that
	 * the fb pitch and fence stride match.
	 */
	if (tiling != I915_TILING_NONE && mode_cmd->pitches[0] != stride) {
		drm_dbg_kms(&i915->drm,
			    "pitch (%d) must match tiling stride (%d)\n",
			    mode_cmd->pitches[0], stride);
		return -EINVAL;
	}

	return 0;
}

static void i915_bo_framebuffer_fini(struct drm_gem_object *obj)
{
	/* Nothing to do for i915 */
}

static struct drm_gem_object *
i915_bo_framebuffer_lookup(struct drm_device *drm,
			   struct drm_file *filp,
			   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);

	/* object is backed with LMEM for discrete */
	if (HAS_LMEM(i915) && !i915_gem_object_can_migrate(obj, INTEL_REGION_LMEM_0)) {
		/* object is "remote", not in local memory */
		i915_gem_object_put(obj);
		drm_dbg_kms(&i915->drm, "framebuffer must reside in local memory\n");
		return ERR_PTR(-EREMOTE);
	}

	return intel_bo_to_drm_bo(obj);
}

#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
static u32 i915_bo_fbdev_pitch_align(u32 stride)
{
	return ALIGN(stride, 64);
}

bool i915_bo_fbdev_prefer_stolen(struct drm_i915_private *i915, unsigned int size)
{
	/* Skip stolen on MTL as Wa_22018444074 mitigation. */
	if (IS_METEORLAKE(i915))
		return false;

	/*
	 * If the FB is too big, just don't use it since fbdev is not very
	 * important and we should probably use that space with FBC or other
	 * features.
	 */
	return i915->dsm.usable_size >= size * 2;
}

static struct drm_gem_object *i915_bo_fbdev_create(struct drm_device *drm, int size)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct drm_i915_gem_object *obj;

	obj = ERR_PTR(-ENODEV);
	if (HAS_LMEM(i915)) {
		obj = i915_gem_object_create_lmem(i915, size,
						  I915_BO_ALLOC_CONTIGUOUS |
						  I915_BO_ALLOC_USER);
	} else {
		if (i915_bo_fbdev_prefer_stolen(i915, size))
			obj = i915_gem_object_create_stolen(i915, size);
		else
			drm_info(drm, "Allocating fbdev: Stolen memory not preferred.\n");

		if (IS_ERR(obj))
			obj = i915_gem_object_create_shmem(i915, size);
	}

	if (IS_ERR(obj)) {
		drm_err(drm, "failed to allocate framebuffer (%pe)\n", obj);
		return ERR_PTR(-ENOMEM);
	}

	return &obj->base;
}

static void i915_bo_fbdev_destroy(struct drm_gem_object *obj)
{
	drm_gem_object_put(obj);
}

static int i915_bo_fbdev_fill_info(struct drm_gem_object *_obj, struct fb_info *info,
				   struct i915_vma *vma)
{
	struct drm_i915_private *i915 = to_i915(_obj->dev);
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	struct i915_gem_ww_ctx ww;
	void __iomem *vaddr;
	int ret;

	if (i915_gem_object_is_lmem(obj)) {
		struct intel_memory_region *mem = obj->mm.region;

		/* Use fbdev's framebuffer from lmem for discrete */
		info->fix.smem_start =
			(unsigned long)(mem->io.start +
					i915_gem_object_get_dma_address(obj, 0) -
					mem->region.start);
		info->fix.smem_len = obj->base.size;
	} else {
		struct i915_ggtt *ggtt = to_gt(i915)->ggtt;

		/* Our framebuffer is the entirety of fbdev's system memory */
		info->fix.smem_start =
			(unsigned long)(ggtt->gmadr.start + i915_ggtt_offset(vma));
		info->fix.smem_len = vma->size;
	}

	for_i915_gem_ww(&ww, ret, false) {
		ret = i915_gem_object_lock(vma->obj, &ww);

		if (ret)
			continue;

		vaddr = i915_vma_pin_iomap(vma);
		if (IS_ERR(vaddr)) {
			drm_err(&i915->drm,
				"Failed to remap framebuffer into virtual memory (%pe)\n", vaddr);
			ret = PTR_ERR(vaddr);
			continue;
		}
	}

	if (ret)
		return ret;

	info->screen_base = vaddr;
	info->screen_size = intel_bo_to_drm_bo(obj)->size;

	return 0;
}
#endif

const struct intel_display_bo_interface i915_display_bo_interface = {
	.is_tiled = i915_bo_is_tiled,
	.is_userptr = i915_bo_is_userptr,
	.is_shmem = i915_bo_is_shmem,
	.is_protected = i915_bo_is_protected,
	.key_check = i915_bo_key_check,
	.fb_mmap = i915_bo_fb_mmap,
	.read_from_page = i915_bo_read_from_page,
	.describe = i915_bo_describe,
	.framebuffer_init = i915_bo_framebuffer_init,
	.framebuffer_fini = i915_bo_framebuffer_fini,
	.framebuffer_lookup = i915_bo_framebuffer_lookup,
#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
	.fbdev_create = i915_bo_fbdev_create,
	.fbdev_destroy = i915_bo_fbdev_destroy,
	.fbdev_fill_info = i915_bo_fbdev_fill_info,
	.fbdev_pitch_align = i915_bo_fbdev_pitch_align,
#endif
};
