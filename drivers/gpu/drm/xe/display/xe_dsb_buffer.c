// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "i915_vma.h"
#include "intel_display_types.h"
#include "intel_dsb_buffer.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_device_types.h"

u32 intel_dsb_buffer_ggtt_offset(struct intel_dsb_buffer *dsb_buf)
{
	return xe_bo_ggtt_addr(dsb_buf->vma->bo);
}

void intel_dsb_buffer_write(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val)
{
	struct xe_device *xe = dsb_buf->vma->bo->tile->xe;

	iosys_map_wr(&dsb_buf->vma->bo->vmap, idx * 4, u32, val);
	xe_device_l2_flush(xe);
}

u32 intel_dsb_buffer_read(struct intel_dsb_buffer *dsb_buf, u32 idx)
{
	return iosys_map_rd(&dsb_buf->vma->bo->vmap, idx * 4, u32);
}

void intel_dsb_buffer_memset(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size)
{
	struct xe_device *xe = dsb_buf->vma->bo->tile->xe;

	WARN_ON(idx > (dsb_buf->buf_size - size) / sizeof(*dsb_buf->cmd_buf));

	iosys_map_memset(&dsb_buf->vma->bo->vmap, idx * 4, val, size);
	xe_device_l2_flush(xe);
}

bool intel_dsb_buffer_create(struct intel_crtc *crtc, struct intel_dsb_buffer *dsb_buf, size_t size)
{
	struct xe_device *xe = to_xe_device(crtc->base.dev);
	struct xe_bo *obj;
	struct i915_vma *vma;

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return false;

	/* Set scanout flag for WC mapping */
	obj = xe_bo_create_pin_map(xe, xe_device_get_root_tile(xe),
				   NULL, PAGE_ALIGN(size),
				   ttm_bo_type_kernel,
				   XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
				   XE_BO_FLAG_SCANOUT | XE_BO_FLAG_GGTT);
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
	/*
	 * The memory barrier here is to ensure coherency of DSB vs MMIO,
	 * both for weak ordering archs and discrete cards.
	 */
	xe_device_wmb(dsb_buf->vma->bo->tile->xe);
}
