// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "i915_drv.h"
#include "i915_vma.h"
#include "intel_display_types.h"
#include "intel_dsb_buffer.h"
#include "xe_bo.h"
#include "xe_gt.h"

u32 intel_dsb_buffer_ggtt_offset(struct intel_dsb_buffer *dsb_buf)
{
	return xe_bo_ggtt_addr(dsb_buf->vma->bo);
}

void intel_dsb_buffer_write(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val)
{
	iosys_map_wr(&dsb_buf->vma->bo->vmap, idx * 4, u32, val);
}

u32 intel_dsb_buffer_read(struct intel_dsb_buffer *dsb_buf, u32 idx)
{
	return iosys_map_rd(&dsb_buf->vma->bo->vmap, idx * 4, u32);
}

void intel_dsb_buffer_memset(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size)
{
	WARN_ON(idx > (dsb_buf->buf_size - size) / sizeof(*dsb_buf->cmd_buf));

	iosys_map_memset(&dsb_buf->vma->bo->vmap, idx * 4, val, size);
}

bool intel_dsb_buffer_create(struct intel_crtc *crtc, struct intel_dsb_buffer *dsb_buf, size_t size)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return false;

	obj = xe_bo_create_pin_map(i915, xe_device_get_root_tile(i915),
				   NULL, PAGE_ALIGN(size),
				   ttm_bo_type_kernel,
				   XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(i915)) |
				   XE_BO_FLAG_GGTT);
	if (IS_ERR(obj)) {
		kfree(vma);
		return false;
	}

	vma->bo = obj;
	dsb_buf->vma = vma;
	dsb_buf->buf_size = size;

	return true;
}

void intel_dsb_buffer_cleanup(struct intel_dsb_buffer *dsb_buf)
{
	xe_bo_unpin_map_no_vm(dsb_buf->vma->bo);
	kfree(dsb_buf->vma);
}

void intel_dsb_buffer_flush_map(struct intel_dsb_buffer *dsb_buf)
{
	/* TODO: add xe specific flush_map() for dsb buffer object. */
}
