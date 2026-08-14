// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/intel/display_parent_interface.h>
#include <drm/ttm/ttm_bo.h>

/* FIXME move the types to parent interface? */
#include "i915_gtt_view_types.h"

/* FIXME move intel_remapped_info_size() & co. to parent interface? */
#include "intel_fb.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_display_vma.h"
#include "xe_fb_pin.h"
#include "xe_ggtt.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_vram_types.h"

static void
write_dpt_rotated(struct xe_bo *bo, struct iosys_map *map, u32 *dpt_ofs, u32 bo_ofs,
		  u32 width, u32 height, u32 src_stride, u32 dst_stride)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ggtt *ggtt = xe_device_get_root_tile(xe)->mem.ggtt;
	u32 column, row;
	u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe_cache_pat_idx(xe, XE_CACHE_NONE));

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

static unsigned int
write_dpt_padding(struct iosys_map *map, unsigned int dest, unsigned int pad)
{
	/* The DE ignores the PTEs for the padding tiles */
	return dest + pad * sizeof(u64);
}

static unsigned int
write_dpt_remapped_linear(struct xe_bo *bo, struct iosys_map *map,
			  unsigned int dest,
			  const struct intel_remapped_plane_info *plane)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ggtt *ggtt = xe_device_get_root_tile(xe)->mem.ggtt;
	const u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo,
						 xe_cache_pat_idx(xe, XE_CACHE_NONE));
	unsigned int offset = plane->offset * XE_PAGE_SIZE;
	unsigned int size = plane->size;

	while (size--) {
		u64 addr = xe_bo_addr(bo, offset, XE_PAGE_SIZE);

		iosys_map_wr(map, dest, u64, addr | pte);
		dest += sizeof(u64);
		offset += XE_PAGE_SIZE;
	}

	return dest;
}

static unsigned int
write_dpt_remapped_tiled(struct xe_bo *bo, struct iosys_map *map,
			 unsigned int dest,
			 const struct intel_remapped_plane_info *plane)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ggtt *ggtt = xe_device_get_root_tile(xe)->mem.ggtt;
	const u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo,
						 xe_cache_pat_idx(xe, XE_CACHE_NONE));
	unsigned int offset, column, row;

	for (row = 0; row < plane->height; row++) {
		offset = (plane->offset + plane->src_stride * row) *
			 XE_PAGE_SIZE;

		for (column = 0; column < plane->width; column++) {
			u64 addr = xe_bo_addr(bo, offset, XE_PAGE_SIZE);

			iosys_map_wr(map, dest, u64, addr | pte);
			dest += sizeof(u64);
			offset += XE_PAGE_SIZE;
		}

		dest = write_dpt_padding(map, dest,
					 plane->dst_stride - plane->width);
	}

	return dest;
}

static void
write_dpt_remapped(struct xe_bo *bo,
		   const struct intel_remapped_info *remap_info,
		   struct iosys_map *map)
{
	unsigned int i, dest = 0;

	for (i = 0; i < ARRAY_SIZE(remap_info->plane); i++) {
		const struct intel_remapped_plane_info *plane =
				&remap_info->plane[i];

		if (!plane->linear && !plane->width && !plane->height)
			continue;

		if (dest && remap_info->plane_alignment) {
			const unsigned int index = dest / sizeof(u64);
			const unsigned int pad =
				ALIGN(index, remap_info->plane_alignment) -
				index;

			dest = write_dpt_padding(map, dest, pad);
		}

		if (plane->linear)
			dest = write_dpt_remapped_linear(bo, map, dest, plane);
		else
			dest = write_dpt_remapped_tiled(bo, map, dest, plane);
	}
}

static int __xe_pin_fb_vma_dpt(struct drm_gem_object *obj,
			       const struct intel_fb_pin_params *pin_params,
			       struct i915_vma *vma)
{
	struct xe_device *xe = to_xe_device(obj->dev);
	struct xe_tile *tile0 = xe_device_get_root_tile(xe);
	struct xe_ggtt *ggtt = tile0->mem.ggtt;
	const struct i915_gtt_view *view = pin_params->view;
	struct xe_bo *bo = gem_to_xe_bo(obj), *dpt;
	u32 dpt_size, size = bo->ttm.base.size;

	if (view->type == I915_GTT_VIEW_NORMAL)
		dpt_size = ALIGN(size / XE_PAGE_SIZE * 8, XE_PAGE_SIZE);
	else if (view->type == I915_GTT_VIEW_REMAPPED)
		dpt_size = ALIGN(intel_remapped_info_size(&view->remapped) * 8,
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
						   pin_params->alignment, false);
	else
		dpt = xe_bo_create_pin_map_at_novm(xe, tile0,
						   dpt_size,  ~0ull,
						   ttm_bo_type_kernel,
						   XE_BO_FLAG_STOLEN |
						   XE_BO_FLAG_GGTT |
						   XE_BO_FLAG_PAGETABLE,
						   pin_params->alignment, false);
	if (IS_ERR(dpt))
		dpt = xe_bo_create_pin_map_at_novm(xe, tile0,
						   dpt_size,  ~0ull,
						   ttm_bo_type_kernel,
						   XE_BO_FLAG_SYSTEM |
						   XE_BO_FLAG_GGTT |
						   XE_BO_FLAG_PAGETABLE |
						   XE_BO_FLAG_FORCE_WC,
						   pin_params->alignment, false);
	if (IS_ERR(dpt))
		return PTR_ERR(dpt);

	if (view->type == I915_GTT_VIEW_NORMAL) {
		u64 pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe_cache_pat_idx(xe, XE_CACHE_NONE));
		u32 x;

		for (x = 0; x < size / XE_PAGE_SIZE; x++) {
			u64 addr = xe_bo_addr(bo, x * XE_PAGE_SIZE, XE_PAGE_SIZE);

			iosys_map_wr(&dpt->vmap, x * 8, u64, pte | addr);
		}
	} else if (view->type == I915_GTT_VIEW_REMAPPED) {
		write_dpt_remapped(bo, &view->remapped, &dpt->vmap);
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

static int __xe_pin_fb_vma_ggtt(struct drm_gem_object *obj,
				const struct intel_fb_pin_params *pin_params,
				struct i915_vma *vma)
{
	const struct i915_gtt_view *view = pin_params->view;
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_device *xe = to_xe_device(obj->dev);
	struct xe_tile *tile0 = xe_device_get_root_tile(xe);
	struct xe_ggtt *ggtt = tile0->mem.ggtt;
	u64 pte, size;
	u32 align;
	int ret = 0;

	/* TODO: Consider sharing framebuffer mapping?
	 * embed i915_vma inside intel_framebuffer
	 */
	guard(xe_pm_runtime_noresume)(xe);

	align = max(XE_PAGE_SIZE, pin_params->alignment);
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

	pte = xe_ggtt_encode_pte_flags(ggtt, bo, xe_cache_pat_idx(xe, XE_CACHE_NONE));
	vma->node = xe_ggtt_insert_node_transform(ggtt, bo, pte,
						  ALIGN(size, align), align,
						  view->type == I915_GTT_VIEW_NORMAL ?
						  NULL : write_ggtt_rotated_node,
						  &(struct fb_rotate_args){view, bo});
	if (IS_ERR(vma->node))
		ret = PTR_ERR(vma->node);

	return ret;
}

static struct i915_vma *__xe_pin_fb_vma(struct drm_gem_object *obj, bool is_dpt,
					const struct intel_fb_pin_params *pin_params)
{
	struct xe_device *xe = to_xe_device(obj->dev);
	struct i915_vma *vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	struct xe_bo *bo = gem_to_xe_bo(obj);
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	int ret = 0;

	/* We reject creating !SCANOUT fb's, so this is weird.. */
	drm_WARN_ON(bo->ttm.base.dev, !(bo->flags & XE_BO_FLAG_FORCE_WC) &&
		    bo->ttm.type != ttm_bo_type_sg);

	if (!vma)
		return ERR_PTR(-ENODEV);

	refcount_set(&vma->ref, 1);
	if (IS_DGFX(to_xe_device(bo->ttm.base.dev)) &&
	    pin_params->needs_cpu_lmem_access &&
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
	if (is_dpt)
		ret = __xe_pin_fb_vma_dpt(obj, pin_params, vma);
	else
		ret = __xe_pin_fb_vma_ggtt(obj, pin_params, vma);
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
	else if (vma->bo->ggtt_node[tile_id] != vma->node)
		xe_ggtt_node_remove(vma->node, false);

	ttm_bo_reserve(&vma->bo->ttm, false, false, NULL);
	ttm_bo_unpin(&vma->bo->ttm);
	ttm_bo_unreserve(&vma->bo->ttm);
	kfree(vma);
}

int xe_fb_pin_ggtt_pin(struct drm_gem_object *obj,
		       const struct intel_fb_pin_params *pin_params,
		       struct i915_vma **out_ggtt_vma,
		       u32 *out_offset,
		       int *out_fence_id)
{
	struct i915_vma *ggtt_vma;

	ggtt_vma = __xe_pin_fb_vma(obj, false, pin_params);
	if (IS_ERR(ggtt_vma))
		return PTR_ERR(ggtt_vma);

	*out_ggtt_vma = ggtt_vma;
	*out_offset = xe_ggtt_node_addr(ggtt_vma->node);
	if (out_fence_id)
		*out_fence_id = -1;

	return 0;
}

static void xe_fb_pin_ggtt_unpin(struct i915_vma *ggtt_vma,
				 int fence_id)
{
	WARN_ON(fence_id >= 0);

	__xe_unpin_fb_vma(ggtt_vma);
}

static int xe_fb_pin_dpt_pin(struct drm_gem_object *obj, struct intel_dpt *dpt,
			     const struct intel_fb_pin_params *pin_params,
			     struct i915_vma **out_dpt_vma,
			     struct i915_vma **out_ggtt_vma,
			     u32 *out_offset)
{
	struct i915_vma *ggtt_vma;

	WARN_ON(dpt);

	ggtt_vma = __xe_pin_fb_vma(obj, true, pin_params);
	if (IS_ERR(ggtt_vma))
		return PTR_ERR(ggtt_vma);

	*out_dpt_vma = NULL; /* not used on xe */
	*out_ggtt_vma = ggtt_vma;
	*out_offset = xe_ggtt_node_addr(ggtt_vma->node);

	return 0;
}

static void xe_fb_pin_dpt_unpin(struct intel_dpt *dpt,
				struct i915_vma *dpt_vma,
				struct i915_vma *ggtt_vma)
{
	WARN_ON(dpt || dpt_vma);

	__xe_unpin_fb_vma(ggtt_vma);
}

static struct i915_vma *
xe_fb_pin_reuse_vma(struct i915_vma *old_ggtt_vma,
		    struct drm_gem_object *old_obj,
		    const struct i915_gtt_view *old_view,
		    struct drm_gem_object *new_obj,
		    const struct i915_gtt_view *new_view,
		    u32 *out_offset)
{
	if (old_ggtt_vma && old_obj == new_obj &&
	    !memcmp(old_view, new_view, sizeof(*new_view))) {
		refcount_inc(&old_ggtt_vma->ref);

		*out_offset = xe_ggtt_node_addr(old_ggtt_vma->node);

		return old_ggtt_vma;
	}

	return NULL;
}

static void xe_fb_pin_get_map(struct i915_vma *vma, struct iosys_map *map)
{
	*map = vma->bo->vmap;
}

const struct intel_display_fb_pin_interface xe_display_fb_pin_interface = {
	.ggtt_pin = xe_fb_pin_ggtt_pin,
	.ggtt_unpin = xe_fb_pin_ggtt_unpin,
	.dpt_pin = xe_fb_pin_dpt_pin,
	.dpt_unpin = xe_fb_pin_dpt_unpin,
	.reuse_vma = xe_fb_pin_reuse_vma,
	.get_map = xe_fb_pin_get_map,
};
