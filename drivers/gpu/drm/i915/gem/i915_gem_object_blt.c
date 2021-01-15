// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_buffer_pool.h"
#include "gt/intel_ring.h"
#include "i915_gem_clflush.h"
#include "i915_gem_object_blt.h"

struct i915_vma *intel_emit_vma_fill_blt(struct intel_context *ce,
					 struct i915_vma *vma,
					 struct i915_gem_ww_ctx *ww,
					 u32 value)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	const u32 block_size = SZ_8M; /* ~1ms at 8GiB/s preemption delay */
	struct intel_gt_buffer_pool_node *pool;
	struct i915_vma *batch;
	u64 offset;
	u64 count;
	u64 rem;
	u32 size;
	u32 *cmd;
	int err;

	GEM_BUG_ON(intel_engine_is_virtual(ce->engine));
	intel_engine_pm_get(ce->engine);

	count = div_u64(round_up(vma->size, block_size), block_size);
	size = (1 + 8 * count) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	pool = intel_gt_get_buffer_pool(ce->engine->gt, size);
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);
		goto out_pm;
	}

	err = i915_gem_object_lock(pool->obj, ww);
	if (err)
		goto out_put;

	batch = i915_vma_instance(pool->obj, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_put;
	}

	err = i915_vma_pin_ww(batch, ww, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_put;

	cmd = i915_gem_object_pin_map(pool->obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_unpin;
	}

	rem = vma->size;
	offset = vma->node.start;

	do {
		u32 size = min_t(u64, rem, block_size);

		GEM_BUG_ON(size >> PAGE_SHIFT > S16_MAX);

		if (INTEL_GEN(i915) >= 8) {
			*cmd++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (7 - 2);
			*cmd++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = lower_32_bits(offset);
			*cmd++ = upper_32_bits(offset);
			*cmd++ = value;
		} else {
			*cmd++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
			*cmd++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = offset;
			*cmd++ = value;
		}

		/* Allow ourselves to be preempted in between blocks. */
		*cmd++ = MI_ARB_CHECK;

		offset += size;
		rem -= size;
	} while (rem);

	*cmd = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(pool->obj);
	i915_gem_object_unpin_map(pool->obj);

	intel_gt_chipset_flush(ce->vm->gt);

	batch->private = pool;
	return batch;

out_unpin:
	i915_vma_unpin(batch);
out_put:
	intel_gt_buffer_pool_put(pool);
out_pm:
	intel_engine_pm_put(ce->engine);
	return ERR_PTR(err);
}

int intel_emit_vma_mark_active(struct i915_vma *vma, struct i915_request *rq)
{
	int err;

	err = i915_request_await_object(rq, vma->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, 0);
	if (unlikely(err))
		return err;

	return intel_gt_buffer_pool_mark_active(vma->private, rq);
}

void intel_emit_vma_release(struct intel_context *ce, struct i915_vma *vma)
{
	i915_vma_unpin(vma);
	intel_gt_buffer_pool_put(vma->private);
	intel_engine_pm_put(ce->engine);
}

static int
move_obj_to_gpu(struct drm_i915_gem_object *obj,
		struct i915_request *rq,
		bool write)
{
	if (obj->cache_dirty & ~obj->cache_coherent)
		i915_gem_clflush_object(obj, 0);

	return i915_request_await_object(rq, obj, write);
}

int i915_gem_object_fill_blt(struct drm_i915_gem_object *obj,
			     struct intel_context *ce,
			     u32 value)
{
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	struct i915_vma *batch;
	struct i915_vma *vma;
	int err;

	vma = i915_vma_instance(obj, ce->vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	i915_gem_ww_ctx_init(&ww, true);
	intel_engine_pm_get(ce->engine);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (err)
		goto out;

	err = intel_context_pin_ww(ce, &ww);
	if (err)
		goto out;

	err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_USER);
	if (err)
		goto out_ctx;

	batch = intel_emit_vma_fill_blt(ce, vma, &ww, value);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_vma;
	}

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	err = move_obj_to_gpu(vma->obj, rq, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	if (unlikely(err))
		goto out_request;

	if (ce->engine->emit_init_breadcrumb)
		err = ce->engine->emit_init_breadcrumb(rq);

	if (likely(!err))
		err = ce->engine->emit_bb_start(rq,
						batch->node.start,
						batch->node.size,
						0);
out_request:
	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);
out_batch:
	intel_emit_vma_release(ce, batch);
out_vma:
	i915_vma_unpin(vma);
out_ctx:
	intel_context_unpin(ce);
out:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	intel_engine_pm_put(ce->engine);
	return err;
}

/* Wa_1209644611:icl,ehl */
static bool wa_1209644611_applies(struct drm_i915_private *i915, u32 size)
{
	u32 height = size >> PAGE_SHIFT;

	if (!IS_GEN(i915, 11))
		return false;

	return height % 4 == 3 && height <= 8;
}

struct i915_vma *intel_emit_vma_copy_blt(struct intel_context *ce,
					 struct i915_gem_ww_ctx *ww,
					 struct i915_vma *src,
					 struct i915_vma *dst)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	const u32 block_size = SZ_8M; /* ~1ms at 8GiB/s preemption delay */
	struct intel_gt_buffer_pool_node *pool;
	struct i915_vma *batch;
	u64 src_offset, dst_offset;
	u64 count, rem;
	u32 size, *cmd;
	int err;

	GEM_BUG_ON(src->size != dst->size);

	GEM_BUG_ON(intel_engine_is_virtual(ce->engine));
	intel_engine_pm_get(ce->engine);

	count = div_u64(round_up(dst->size, block_size), block_size);
	size = (1 + 11 * count) * sizeof(u32);
	size = round_up(size, PAGE_SIZE);
	pool = intel_gt_get_buffer_pool(ce->engine->gt, size);
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);
		goto out_pm;
	}

	err = i915_gem_object_lock(pool->obj, ww);
	if (err)
		goto out_put;

	batch = i915_vma_instance(pool->obj, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_put;
	}

	err = i915_vma_pin_ww(batch, ww, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_put;

	cmd = i915_gem_object_pin_map(pool->obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_unpin;
	}

	rem = src->size;
	src_offset = src->node.start;
	dst_offset = dst->node.start;

	do {
		size = min_t(u64, rem, block_size);
		GEM_BUG_ON(size >> PAGE_SHIFT > S16_MAX);

		if (INTEL_GEN(i915) >= 9 &&
		    !wa_1209644611_applies(i915, size)) {
			*cmd++ = GEN9_XY_FAST_COPY_BLT_CMD | (10 - 2);
			*cmd++ = BLT_DEPTH_32 | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = lower_32_bits(dst_offset);
			*cmd++ = upper_32_bits(dst_offset);
			*cmd++ = 0;
			*cmd++ = PAGE_SIZE;
			*cmd++ = lower_32_bits(src_offset);
			*cmd++ = upper_32_bits(src_offset);
		} else if (INTEL_GEN(i915) >= 8) {
			*cmd++ = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (10 - 2);
			*cmd++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
			*cmd++ = 0;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
			*cmd++ = lower_32_bits(dst_offset);
			*cmd++ = upper_32_bits(dst_offset);
			*cmd++ = 0;
			*cmd++ = PAGE_SIZE;
			*cmd++ = lower_32_bits(src_offset);
			*cmd++ = upper_32_bits(src_offset);
		} else {
			*cmd++ = SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
			*cmd++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
			*cmd++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE;
			*cmd++ = dst_offset;
			*cmd++ = PAGE_SIZE;
			*cmd++ = src_offset;
		}

		/* Allow ourselves to be preempted in between blocks. */
		*cmd++ = MI_ARB_CHECK;

		src_offset += size;
		dst_offset += size;
		rem -= size;
	} while (rem);

	*cmd = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(pool->obj);
	i915_gem_object_unpin_map(pool->obj);

	intel_gt_chipset_flush(ce->vm->gt);
	batch->private = pool;
	return batch;

out_unpin:
	i915_vma_unpin(batch);
out_put:
	intel_gt_buffer_pool_put(pool);
out_pm:
	intel_engine_pm_put(ce->engine);
	return ERR_PTR(err);
}

int i915_gem_object_copy_blt(struct drm_i915_gem_object *src,
			     struct drm_i915_gem_object *dst,
			     struct intel_context *ce)
{
	struct i915_address_space *vm = ce->vm;
	struct i915_vma *vma[2], *batch;
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	int err, i;

	vma[0] = i915_vma_instance(src, vm, NULL);
	if (IS_ERR(vma[0]))
		return PTR_ERR(vma[0]);

	vma[1] = i915_vma_instance(dst, vm, NULL);
	if (IS_ERR(vma[1]))
		return PTR_ERR(vma[1]);

	i915_gem_ww_ctx_init(&ww, true);
	intel_engine_pm_get(ce->engine);
retry:
	err = i915_gem_object_lock(src, &ww);
	if (!err)
		err = i915_gem_object_lock(dst, &ww);
	if (!err)
		err = intel_context_pin_ww(ce, &ww);
	if (err)
		goto out;

	err = i915_vma_pin_ww(vma[0], &ww, 0, 0, PIN_USER);
	if (err)
		goto out_ctx;

	err = i915_vma_pin_ww(vma[1], &ww, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_unpin_src;

	batch = intel_emit_vma_copy_blt(ce, &ww, vma[0], vma[1]);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unpin_dst;
	}

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	for (i = 0; i < ARRAY_SIZE(vma); i++) {
		err = move_obj_to_gpu(vma[i]->obj, rq, i);
		if (unlikely(err))
			goto out_request;
	}

	for (i = 0; i < ARRAY_SIZE(vma); i++) {
		unsigned int flags = i ? EXEC_OBJECT_WRITE : 0;

		err = i915_vma_move_to_active(vma[i], rq, flags);
		if (unlikely(err))
			goto out_request;
	}

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);

out_request:
	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);
out_batch:
	intel_emit_vma_release(ce, batch);
out_unpin_dst:
	i915_vma_unpin(vma[1]);
out_unpin_src:
	i915_vma_unpin(vma[0]);
out_ctx:
	intel_context_unpin(ce);
out:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	intel_engine_pm_put(ce->engine);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_object_blt.c"
#endif
