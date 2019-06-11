// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_gem_object_blt.h"

#include "i915_gem_clflush.h"
#include "intel_drv.h"

int intel_emit_vma_fill_blt(struct i915_request *rq,
			    struct i915_vma *vma,
			    u32 value)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 8);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (INTEL_GEN(rq->i915) >= 8) {
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (7 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = vma->size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = lower_32_bits(vma->node.start);
		*cs++ = upper_32_bits(vma->node.start);
		*cs++ = value;
		*cs++ = MI_NOOP;
	} else {
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = vma->size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = vma->node.start;
		*cs++ = value;
		*cs++ = MI_NOOP;
		*cs++ = MI_NOOP;
	}

	intel_ring_advance(rq, cs);

	return 0;
}

int i915_gem_object_fill_blt(struct drm_i915_gem_object *obj,
			     struct intel_context *ce,
			     u32 value)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_gem_context *ctx = ce->gem_context;
	struct i915_address_space *vm = ctx->vm ?: &i915->ggtt.vm;
	struct i915_request *rq;
	struct i915_vma *vma;
	int err;

	/* XXX: ce->vm please */
	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (unlikely(err))
		return err;

	if (obj->cache_dirty & ~obj->cache_coherent) {
		i915_gem_object_lock(obj);
		i915_gem_clflush_object(obj, 0);
		i915_gem_object_unlock(obj);
	}

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_unpin;
	}

	err = i915_request_await_object(rq, obj, true);
	if (unlikely(err))
		goto out_request;

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (unlikely(err))
		goto out_request;

	err = intel_emit_vma_fill_blt(rq, vma, value);
out_request:
	if (unlikely(err))
		i915_request_skip(rq, err);

	i915_request_add(rq);
out_unpin:
	i915_vma_unpin(vma);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_object_blt.c"
#endif
