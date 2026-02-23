// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_bo.h>

#include "i915_vma.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_dpt.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_fbdev.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_pm.h"
#include "xe_vram_types.h"

static void
write_dpt_rotated(struct xe_bo *bo, struct iosys_map *map, u32 *dpt_ofs, u32 bo_ofs,
		  u32 width, u32 height, u32 src_stride, u32 dst_stride)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ggtt *ggtt = xe_device_get_root_tile(xe)->mem.ggtt;
	u32 column, row;
	u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe->pat.idx[XE_CACHE_NONE]);

	/* TODO: Maybe rewrite so we can traverse the bo addresses sequentially,
	 * by writing dpt/ggtt in a different order?
	 */

	for (column = 0; column < width; column++) {
		u32 src_idx = src_stride * (height - 1) + column + bo_ofs;

		for (row = 0; row < height; row++) {
			u64 addr = xe_bo_addr(bo, src_idx * XE_PAGE_SIZE, XE_PAGE_SIZE);

			iosys_map_wr(map, *dpt_ofs, u64, pte | addr);
			*dpt_ofs += 8;
			src_idx -= src_stride;
		}

		/* The DE ignores the PTEs for the padding tiles */
		*dpt_ofs += (dst_stride - height) * 8;
	}

	/* Align to next page */
	*dpt_ofs = ALIGN(*dpt_ofs, 4096);
}

static void
write_dpt_remapped(struct xe_bo *bo, struct iosys_map *map, u32 *dpt_ofs,
		   u32 bo_ofs, u32 width, u32 height, u32 src_stride,
		   u32 dst_stride)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ggtt *ggtt = xe_device_get_root_tile(xe)->mem.ggtt;
	u32 column, row;
	u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe->pat.idx[XE_CACHE_NONE]);

	for (row = 0; row < height; row++) {
		u32 src_idx = src_stride * row + bo_ofs;

		for (column = 0; column < width; column++) {
			u64 addr = xe_bo_addr(bo, src_idx * XE_PAGE_SIZE, XE_PAGE_SIZE);
			iosys_map_wr(map, *dpt_ofs, u64, pte | addr);

			*dpt_ofs += 8;
			src_idx++;
		}

		/* The DE ignores the PTEs for the padding tiles */
		*dpt_ofs += (dst_stride - width) * 8;
	}

	/* Align to next page */
	*dpt_ofs = ALIGN(*dpt_ofs, 4096);
}

static int __xe_pin_fb_vma_dpt(const struct intel_framebuffer *fb,
			       const struct i915_gtt_view *view,
			       struct i915_vma *vma,
			       unsigned int alignment)
{
	struct xe_device *xe = to_xe_device(fb->base.dev);
	struct xe_tile *tile0 = xe_device_get_root_tile(xe);
	struct xe_ggtt *ggtt = tile0->mem.ggtt;
	struct drm_gem_object *obj = intel_fb_bo(&fb->base);
	struct xe_bo *bo = gem_to_xe_bo(obj), *dpt;
	u32 dpt_size, size = bo->ttm.base.size;

	if (view->type == I915_GTT_VIEW_NORMAL)
		dpt_size = ALIGN(size / XE_PAGE_SIZE * 8, XE_PAGE_SIZE);
	else if (view->type == I915_GTT_VIEW_REMAPPED)
		dpt_size = ALIGN(intel_remapped_info_size(&fb->remapped_view.gtt.remapped) * 8,
				 XE_PAGE_SIZE);
	else
		/* display uses 4K tiles instead of bytes here, convert to entries.. */
		dpt_size = ALIGN(intel_rotation_info_size(&view->rotated) * 8,
				 XE_PAGE_SIZE);

	if (IS_DGFX(xe))
		dpt = xe_bo_create_pin_map_at_novm(xe, tile0,
						   dpt_size, ~0ull,
						   ttm_bo_type_kernel,
						   XE_BO_FLAG_VRAM0 |
						   XE_BO_FLAG_GGTT |
						   XE_BO_FLAG_PAGETABLE,
						   alignment, false);
	else
		dpt = xe_bo_create_pin_map_at_novm(xe, tile0,
						   dpt_size,  ~0ull,
						   ttm_bo_type_kernel,
						   XE_BO_FLAG_STOLEN |
						   XE_BO_FLAG_GGTT |
						   XE_BO_FLAG_PAGETABLE,
						   alignment, false);
	if (IS_ERR(dpt))
		dpt = xe_bo_create_pin_map_at_novm(xe, tile0,
						   dpt_size,  ~0ull,
						   ttm_bo_type_kernel,
						   XE_BO_FLAG_SYSTEM |
						   XE_BO_FLAG_GGTT |
						   XE_BO_FLAG_PAGETABLE,
						   alignment, false);
	if (IS_ERR(dpt))
		return PTR_ERR(dpt);

	if (view->type == I915_GTT_VIEW_NORMAL) {
		u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe->pat.idx[XE_CACHE_NONE]);
		u32 x;

		for (x = 0; x < size / XE_PAGE_SIZE; x++) {
			u64 addr = xe_bo_addr(bo, x * XE_PAGE_SIZE, XE_PAGE_SIZE);

			iosys_map_wr(&dpt->vmap, x * 8, u64, pte | addr);
		}
	} else if (view->type == I915_GTT_VIEW_REMAPPED) {
		const struct intel_remapped_info *remap_info = &view->remapped;
		u32 i, dpt_ofs = 0;

		for (i = 0; i < ARRAY_SIZE(remap_info->plane); i++)
			write_dpt_remapped(bo, &dpt->vmap, &dpt_ofs,
					   remap_info->plane[i].offset,
					   remap_info->plane[i].width,
					   remap_info->plane[i].height,
					   remap_info->plane[i].src_stride,
					   remap_info->plane[i].dst_stride);

	} else {
		const struct intel_rotation_info *rot_info = &view->rotated;
		u32 i, dpt_ofs = 0;

		for (i = 0; i < ARRAY_SIZE(rot_info->plane); i++)
			write_dpt_rotated(bo, &dpt->vmap, &dpt_ofs,
					  rot_info->plane[i].offset,
					  rot_info->plane[i].width,
					  rot_info->plane[i].height,
					  rot_info->plane[i].src_stride,
					  rot_info->plane[i].dst_stride);
	}

	vma->dpt = dpt;
	vma->node = dpt->ggtt_node[tile0->id];

	/* Ensure DPT writes are flushed */
	xe_device_l2_flush(xe);
	return 0;
}

static void
write_ggtt_rotated(struct xe_ggtt *ggtt, u32 *ggtt_ofs,
		   u64 pte_flags,
		   xe_ggtt_set_pte_fn write_pte,
		   struct xe_bo *bo, u32 bo_ofs,
		   u32 width, u32 height, u32 src_stride, u32 dst_stride)
{
	u32 column, row;

	for (column = 0; column < width; column++) {
		u32 src_idx = src_stride * (height - 1) + column + bo_ofs;

		for (row = 0; row < height; row++) {
			u64 addr = xe_bo_addr(bo, src_idx * XE_PAGE_SIZE, XE_PAGE_SIZE);

			write_pte(ggtt, *ggtt_ofs, pte_flags | addr);
			*ggtt_ofs += XE_PAGE_SIZE;
			src_idx -= src_stride;
		}

		/* The DE ignores the PTEs for the padding tiles */
		*ggtt_ofs += (dst_stride - height) * XE_PAGE_SIZE;
	}
}

struct fb_rotate_args {
	const struct i915_gtt_view *view;
	struct xe_bo *bo;
};

static void write_ggtt_rotated_node(struct xe_ggtt *ggtt, struct xe_ggtt_node *node,
				    u64 pte_flags, xe_ggtt_set_pte_fn write_pte, void *data)
{
	struct fb_rotate_args *args = data;
	struct xe_bo *bo = args->bo;
	const struct intel_rotation_info *rot_info = &args->view->rotated;
	u32 ggtt_ofs = xe_ggtt_node_addr(node);

	for (u32 i = 0; i < ARRAY_SIZE(rot_info->plane); i++)
		write_ggtt_rotated(ggtt, &ggtt_ofs, pte_flags, write_pte,
				   bo, rot_info->plane[i].offset,
				   rot_info->plane[i].width,
				   rot_info->plane[i].height,
				   rot_info->plane[i].src_stride,
				   rot_info->plane[i].dst_stride);
}

static int __xe_pin_fb_vma_ggtt(const struct intel_framebuffer *fb,
				const struct i915_gtt_view *view,
				struct i915_vma *vma,
				unsigned int alignment)
{
	struct drm_gem_object *obj = intel_fb_bo(&fb->base);
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_device *xe = to_xe_device(fb->base.dev);
	struct xe_tile *tile0 = xe_device_get_root_tile(xe);
	struct xe_ggtt *ggtt = tile0->mem.ggtt;
	u64 pte, size;
	u32 align;
	int ret = 0;

	/* TODO: Consider sharing framebuffer mapping?
	 * embed i915_vma inside intel_framebuffer
	 */
	guard(xe_pm_runtime_noresume)(xe);

	align = XE_PAGE_SIZE;
	if (xe_bo_is_vram(bo) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		align = max(align, SZ_64K);

	/* Fast case, preallocated GGTT view? */
	if (bo->ggtt_node[tile0->id] && view->type == I915_GTT_VIEW_NORMAL) {
		vma->node = bo->ggtt_node[tile0->id];
		return 0;
	}

	/* TODO: Consider sharing framebuffer mapping?
	 * embed i915_vma inside intel_framebuffer
	 */
	if (view->type == I915_GTT_VIEW_NORMAL)
		size = xe_bo_size(bo);
	else
		/* display uses tiles instead of bytes here, so convert it back.. */
		size = intel_rotation_info_size(&view->rotated) * XE_PAGE_SIZE;

	pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe->pat.idx[XE_CACHE_NONE]);
	vma->node = xe_ggtt_node_insert_transform(ggtt, bo, pte,
						  ALIGN(size, align), align,
						  view->type == I915_GTT_VIEW_NORMAL ?
						  NULL : write_ggtt_rotated_node,
						  &(struct fb_rotate_args){view, bo});
	if (IS_ERR(vma->node))
		ret = PTR_ERR(vma->node);

	return ret;
}

static struct i915_vma *__xe_pin_fb_vma(const struct intel_framebuffer *fb,
					const struct i915_gtt_view *view,
					unsigned int alignment)
{
	struct drm_device *dev = fb->base.dev;
	struct xe_device *xe = to_xe_device(dev);
	struct i915_vma *vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	struct drm_gem_object *obj = intel_fb_bo(&fb->base);
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	int ret = 0;

	if (!vma)
		return ERR_PTR(-ENODEV);

	refcount_set(&vma->ref, 1);
	if (IS_DGFX(to_xe_device(bo->ttm.base.dev)) &&
	    intel_fb_rc_ccs_cc_plane(&fb->base) >= 0 &&
	    !(bo->flags & XE_BO_FLAG_NEEDS_CPU_ACCESS)) {
		struct xe_vram_region *vram = xe_device_get_root_tile(xe)->mem.vram;

		/*
		 * If we need to able to access the clear-color value stored in
		 * the buffer, then we require that such buffers are also CPU
		 * accessible.  This is important on small-bar systems where
		 * only some subset of VRAM is CPU accessible.
		 */
		if (xe_vram_region_io_size(vram) < xe_vram_region_usable_size(vram)) {
			ret = -EINVAL;
			goto err;
		}
	}

	/*
	 * Pin the framebuffer, we can't use xe_bo_(un)pin functions as the
	 * assumptions are incorrect for framebuffers
	 */
	xe_validation_guard(&ctx, &xe->val, &exec, (struct xe_val_flags) {.interruptible = true},
			    ret) {
		ret = drm_exec_lock_obj(&exec, &bo->ttm.base);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;

		if (IS_DGFX(xe))
			ret = xe_bo_migrate(bo, XE_PL_VRAM0, NULL, &exec);
		else
			ret = xe_bo_validate(bo, NULL, true, &exec);
		drm_exec_retry_on_contention(&exec);
		xe_validation_retry_on_oom(&ctx, &ret);
		if (!ret)
			ttm_bo_pin(&bo->ttm);
	}
	if (ret)
		goto err;

	vma->bo = bo;
	if (intel_fb_uses_dpt(&fb->base))
		ret = __xe_pin_fb_vma_dpt(fb, view, vma, alignment);
	else
		ret = __xe_pin_fb_vma_ggtt(fb, view, vma,  alignment);
	if (ret)
		goto err_unpin;

	return vma;

err_unpin:
	ttm_bo_reserve(&bo->ttm, false, false, NULL);
	ttm_bo_unpin(&bo->ttm);
	ttm_bo_unreserve(&bo->ttm);
err:
	kfree(vma);
	return ERR_PTR(ret);
}

static void __xe_unpin_fb_vma(struct i915_vma *vma)
{
	u8 tile_id = xe_device_get_root_tile(xe_bo_device(vma->bo))->id;

	if (!refcount_dec_and_test(&vma->ref))
		return;

	if (vma->dpt)
		xe_bo_unpin_map_no_vm(vma->dpt);
	else if (!xe_ggtt_node_allocated(vma->bo->ggtt_node[tile_id]) ||
		 vma->bo->ggtt_node[tile_id] != vma->node)
		xe_ggtt_node_remove(vma->node, false);

	ttm_bo_reserve(&vma->bo->ttm, false, false, NULL);
	ttm_bo_unpin(&vma->bo->ttm);
	ttm_bo_unreserve(&vma->bo->ttm);
	kfree(vma);
}

struct i915_vma *
intel_fb_pin_to_ggtt(const struct drm_framebuffer *fb,
		     const struct i915_gtt_view *view,
		     unsigned int alignment,
		     unsigned int phys_alignment,
		     unsigned int vtd_guard,
		     bool uses_fence,
		     unsigned long *out_flags)
{
	*out_flags = 0;

	return __xe_pin_fb_vma(to_intel_framebuffer(fb), view, alignment);
}

void intel_fb_unpin_vma(struct i915_vma *vma, unsigned long flags)
{
	__xe_unpin_fb_vma(vma);
}

static bool reuse_vma(struct intel_plane_state *new_plane_state,
		      const struct intel_plane_state *old_plane_state)
{
	struct intel_framebuffer *fb = to_intel_framebuffer(new_plane_state->hw.fb);
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	struct xe_device *xe = to_xe_device(fb->base.dev);
	struct intel_display *display = xe->display;
	struct i915_vma *vma;

	if (old_plane_state->hw.fb == new_plane_state->hw.fb &&
	    !memcmp(&old_plane_state->view.gtt,
		    &new_plane_state->view.gtt,
		    sizeof(new_plane_state->view.gtt))) {
		vma = old_plane_state->ggtt_vma;
		goto found;
	}

	if (fb == intel_fbdev_framebuffer(display->fbdev.fbdev)) {
		vma = intel_fbdev_vma_pointer(display->fbdev.fbdev);
		if (vma)
			goto found;
	}

	return false;

found:
	refcount_inc(&vma->ref);
	new_plane_state->ggtt_vma = vma;

	new_plane_state->surf = i915_ggtt_offset(new_plane_state->ggtt_vma) +
		plane->surf_offset(new_plane_state);

	return true;
}

int intel_plane_pin_fb(struct intel_plane_state *new_plane_state,
		       const struct intel_plane_state *old_plane_state)
{
	struct drm_framebuffer *fb = new_plane_state->hw.fb;
	struct drm_gem_object *obj = intel_fb_bo(fb);
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct i915_vma *vma;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	unsigned int alignment = plane->min_alignment(plane, fb, 0);

	if (reuse_vma(new_plane_state, old_plane_state))
		return 0;

	/* We reject creating !SCANOUT fb's, so this is weird.. */
	drm_WARN_ON(bo->ttm.base.dev, !(bo->flags & XE_BO_FLAG_SCANOUT));

	vma = __xe_pin_fb_vma(intel_fb, &new_plane_state->view.gtt, alignment);

	if (IS_ERR(vma))
		return PTR_ERR(vma);

	new_plane_state->ggtt_vma = vma;

	new_plane_state->surf = i915_ggtt_offset(new_plane_state->ggtt_vma) +
		plane->surf_offset(new_plane_state);

	return 0;
}

void intel_plane_unpin_fb(struct intel_plane_state *old_plane_state)
{
	__xe_unpin_fb_vma(old_plane_state->ggtt_vma);
	old_plane_state->ggtt_vma = NULL;
}

/*
 * For Xe introduce dummy intel_dpt_create which just return NULL,
 * intel_dpt_destroy which does nothing, and fake intel_dpt_ofsset returning 0;
 */
struct i915_address_space *intel_dpt_create(struct intel_framebuffer *fb)
{
	return NULL;
}

void intel_dpt_destroy(struct i915_address_space *vm)
{
	return;
}

u64 intel_dpt_offset(struct i915_vma *dpt_vma)
{
	return 0;
}

void intel_fb_get_map(struct i915_vma *vma, struct iosys_map *map)
{
	*map = vma->bo->vmap;
}
