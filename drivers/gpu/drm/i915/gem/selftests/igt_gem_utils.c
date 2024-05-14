/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "igt_gem_utils.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_pm.h"
#include "gt/intel_context.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "i915_vma.h"
#include "i915_drv.h"

#include "i915_request.h"

struct i915_request *
igt_request_alloc(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	struct i915_request *rq;

	/*
	 * Pinning the contexts may generate requests in order to acquire
	 * GGTT space, so do this first before we reserve a seqno for
	 * ourselves.
	 */
	ce = i915_gem_context_get_engine(ctx, engine->legacy_idx);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	rq = intel_context_create_request(ce);
	intel_context_put(ce);

	return rq;
}

struct i915_vma *
igt_emit_store_dw(struct i915_vma *vma,
		  u64 offset,
		  unsigned long count,
		  u32 val)
{
	struct drm_i915_gem_object *obj;
	const int ver = GRAPHICS_VER(vma->vm->i915);
	unsigned long n, size;
	u32 *cmd;
	int err;

	size = (4 * count + 1) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	obj = i915_gem_object_create_internal(vma->vm->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	cmd = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	GEM_BUG_ON(offset + (count - 1) * PAGE_SIZE > vma->node.size);
	offset += vma->node.start;

	for (n = 0; n < count; n++) {
		if (ver >= 8) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4;
			*cmd++ = lower_32_bits(offset);
			*cmd++ = upper_32_bits(offset);
			*cmd++ = val;
		} else if (ver >= 4) {
			*cmd++ = MI_STORE_DWORD_IMM_GEN4 |
				(ver < 6 ? MI_USE_GGTT : 0);
			*cmd++ = 0;
			*cmd++ = offset;
			*cmd++ = val;
		} else {
			*cmd++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
			*cmd++ = offset;
			*cmd++ = val;
		}
		offset += PAGE_SIZE;
	}
	*cmd = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	intel_gt_chipset_flush(vma->vm->gt);

	vma = i915_vma_instance(obj, vma->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

int igt_gpu_fill_dw(struct intel_context *ce,
		    struct i915_vma *vma, u64 offset,
		    unsigned long count, u32 val)
{
	struct i915_request *rq;
	struct i915_vma *batch;
	unsigned int flags;
	int err;

	GEM_BUG_ON(!intel_engine_can_store_dword(ce->engine));
	GEM_BUG_ON(!i915_vma_is_pinned(vma));

	batch = igt_emit_store_dw(vma, offset, count, val);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	i915_vma_lock(batch);
	err = i915_request_await_object(rq, batch->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(batch, rq, 0);
	i915_vma_unlock(batch);
	if (err)
		goto skip_request;

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto skip_request;

	flags = 0;
	if (GRAPHICS_VER(ce->vm->i915) <= 5)
		flags |= I915_DISPATCH_SECURE;

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					flags);

skip_request:
	if (err)
		i915_request_set_error_once(rq, err);
	i915_request_add(rq);
err_batch:
	i915_vma_unpin_and_release(&batch, 0);
	return err;
}
