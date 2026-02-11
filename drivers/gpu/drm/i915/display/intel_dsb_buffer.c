// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"
#include "i915_drv.h"
#include "i915_vma.h"
#include "intel_dsb_buffer.h"

struct intel_dsb_buffer {
	u32 *cmd_buf;
	struct i915_vma *vma;
	size_t buf_size;
};

u32 intel_dsb_buffer_ggtt_offset(struct intel_dsb_buffer *dsb_buf)
{
	return i915_ggtt_offset(dsb_buf->vma);
}

void intel_dsb_buffer_write(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val)
{
	dsb_buf->cmd_buf[idx] = val;
}

u32 intel_dsb_buffer_read(struct intel_dsb_buffer *dsb_buf, u32 idx)
{
	return dsb_buf->cmd_buf[idx];
}

void intel_dsb_buffer_memset(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size)
{
	WARN_ON(idx > (dsb_buf->buf_size - size) / sizeof(*dsb_buf->cmd_buf));

	memset(&dsb_buf->cmd_buf[idx], val, size);
}

struct intel_dsb_buffer *intel_dsb_buffer_create(struct drm_device *drm, size_t size)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct intel_dsb_buffer *dsb_buf;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *buf;
	int ret;

	dsb_buf = kzalloc(sizeof(*dsb_buf), GFP_KERNEL);
	if (!dsb_buf)
		return ERR_PTR(-ENOMEM);

	if (HAS_LMEM(i915)) {
		obj = i915_gem_object_create_lmem(i915, PAGE_ALIGN(size),
						  I915_BO_ALLOC_CONTIGUOUS);
		if (IS_ERR(obj)) {
			ret = PTR_ERR(obj);
			goto err;
		}
	} else {
		obj = i915_gem_object_create_internal(i915, PAGE_ALIGN(size));
		if (IS_ERR(obj)) {
			ret = PTR_ERR(obj);
			goto err;
		}

		i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);
	}

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		i915_gem_object_put(obj);
		goto err;
	}

	buf = i915_gem_object_pin_map_unlocked(vma->obj, I915_MAP_WC);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
		goto err;
	}

	dsb_buf->vma = vma;
	dsb_buf->cmd_buf = buf;
	dsb_buf->buf_size = size;

	return dsb_buf;

err:
	kfree(dsb_buf);

	return ERR_PTR(ret);
}

void intel_dsb_buffer_cleanup(struct intel_dsb_buffer *dsb_buf)
{
	i915_vma_unpin_and_release(&dsb_buf->vma, I915_VMA_RELEASE_MAP);
	kfree(dsb_buf);
}

void intel_dsb_buffer_flush_map(struct intel_dsb_buffer *dsb_buf)
{
	i915_gem_object_flush_map(dsb_buf->vma->obj);
}
