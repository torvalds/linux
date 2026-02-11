// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "intel_dsb_buffer.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_device_types.h"

struct intel_dsb_buffer {
	u32 *cmd_buf;
	struct xe_bo *bo;
	size_t buf_size;
};

u32 intel_dsb_buffer_ggtt_offset(struct intel_dsb_buffer *dsb_buf)
{
	return xe_bo_ggtt_addr(dsb_buf->bo);
}

void intel_dsb_buffer_write(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val)
{
	iosys_map_wr(&dsb_buf->bo->vmap, idx * 4, u32, val);
}

u32 intel_dsb_buffer_read(struct intel_dsb_buffer *dsb_buf, u32 idx)
{
	return iosys_map_rd(&dsb_buf->bo->vmap, idx * 4, u32);
}

void intel_dsb_buffer_memset(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size)
{
	WARN_ON(idx > (dsb_buf->buf_size - size) / sizeof(*dsb_buf->cmd_buf));

	iosys_map_memset(&dsb_buf->bo->vmap, idx * 4, val, size);
}

struct intel_dsb_buffer *intel_dsb_buffer_create(struct drm_device *drm, size_t size)
{
	struct xe_device *xe = to_xe_device(drm);
	struct intel_dsb_buffer *dsb_buf;
	struct xe_bo *obj;
	int ret;

	dsb_buf = kzalloc(sizeof(*dsb_buf), GFP_KERNEL);
	if (!dsb_buf)
		return ERR_PTR(-ENOMEM);

	/* Set scanout flag for WC mapping */
	obj = xe_bo_create_pin_map_novm(xe, xe_device_get_root_tile(xe),
					PAGE_ALIGN(size),
					ttm_bo_type_kernel,
					XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
					XE_BO_FLAG_SCANOUT | XE_BO_FLAG_GGTT, false);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto err_pin_map;
	}

	dsb_buf->bo = obj;
	dsb_buf->buf_size = size;

	return dsb_buf;

err_pin_map:
	kfree(dsb_buf);

	return ERR_PTR(ret);
}

void intel_dsb_buffer_cleanup(struct intel_dsb_buffer *dsb_buf)
{
	xe_bo_unpin_map_no_vm(dsb_buf->bo);
	kfree(dsb_buf);
}

void intel_dsb_buffer_flush_map(struct intel_dsb_buffer *dsb_buf)
{
	struct xe_device *xe = dsb_buf->bo->tile->xe;

	/*
	 * The memory barrier here is to ensure coherency of DSB vs MMIO,
	 * both for weak ordering archs and discrete cards.
	 */
	xe_device_wmb(xe);
	xe_device_l2_flush(xe);
}
