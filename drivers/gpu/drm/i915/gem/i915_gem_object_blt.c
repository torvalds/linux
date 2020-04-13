// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_pool.h"
#include "gt/intel_gt.h"
#include "gt/intel_ring.h"
#include "i915_gem_clflush.h"
#include "i915_gem_object_blt.h"

struct i915_vma *intel_emit_vma_fill_blt(struct intel_context *ce,
					 struct i915_vma *vma,
					 u32 value)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	const u32 block_size = SZ_8M; /* ~1ms at 8GiB/s preemption delay */
	struct intel_engine_pool_node *pool;
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
	pool = intel_engine_get_pool(ce->engine, size);
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);
		goto out_pm;
	}

	cmd = i915_gem_object_pin_map(pool->obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_put;
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
	intel_gt_chipset_flush(ce->vm->gt);

	i915_gem_object_unpin_map(pool->obj);

	batch = i915_vma_instance(pool->obj, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_put;
	}

	err = i915_vma_pin(batch, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_put;

	batch->private = pool;
	return batch;

out_put:
	intel_engine_pool_put(pool);
out_pm:
	intel_engine_pm_put(ce->engine);
	return ERR_PTR(err);
}

int intel_emit_vma_mark_active(struct i915_vma *vma, struct i915_request *rq)
{
	int err;

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, 0);
	i915_vma_unlock(vma);
	if (unlikely(err))
		return err;

	return intel_engine_pool_mark_active(vma->private, rq);
}

void intel_emit_vma_release(struct intel_context *ce, struct i915_vma *vma)
{
	i915_vma_unpin(vma);
	intel_engine_pool_put(vma->private);
	intel_engine_pm_put(ce->engine);
}

int i915_gem_object_fill_blt(struct drm_i915_gem_object *obj,
			     struct intel_context *ce,
			     u32 value)
{
	struct i915_request *rq;
	struct i915_vma *batch;
	struct i915_vma *vma;
	int err;

	vma = i915_vma_instance(obj, ce->vm, NULL);
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

	batch = intel_emit_vma_fill_blt(ce, vma, value);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unpin;
	}

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	err = i915_request_await_object(rq, obj, true);
	if (unlikely(err))
		goto out_request;

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_request;
	}

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (unlikely(err))
		goto out_request;

	err = ce->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);
out_request:
	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);
out_batch:
	intel_emit_vma_release(ce, batch);
out_unpin:
	i915_vma_unpin(vma);
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
					 struct i915_vma *src,
					 struct i915_vma *dst)
{
	struct drm_i915_private *i915 = ce->vm->i915;
	const u32 block_size = SZ_8M; /* ~1ms at 8GiB/s preemption delay */
	struct intel_engine_pool_node *pool;
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
	pool = intel_engine_get_pool(ce->engine, size);
	if (IS_ERR(pool)) {
		err = PTR_ERR(pool);
		goto out_pm;
	}

	cmd = i915_gem_object_pin_map(pool->obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto out_put;
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
	intel_gt_chipset_flush(ce->vm->gt);

	i915_gem_object_unpin_map(pool->obj);

	batch = i915_vma_instance(pool->obj, ce->vm, NULL);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_put;
	}

	err = i915_vma_pin(batch, 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_put;

	batch->private = pool;
	return batch;

out_put:
	intel_engine_pool_put(pool);
out_pm:
	intel_engine_pm_put(ce->engine);
	return ERR_PTR(err);
}

static int move_to_gpu(struct i915_vma *vma, struct i915_request *rq, bool write)
{
	struct drm_i915_gem_object *obj = vma->obj;

	if (obj->cache_dirty & ~obj->cache_coherent)
		i915_gem_clflush_object(obj, 0);

	return i915_request_await_object(rq, obj, write);
}

int i915_gem_object_copy_blt(struct drm_i915_gem_object *src,
			     struct drm_i915_gem_object *dst,
			     struct intel_context *ce)
{
	struct drm_gem_object *objs[] = { &src->base, &dst->base };
	struct i915_address_space *vm = ce->vm;
	struct i915_vma *vma[2], *batch;
	struct ww_acquire_ctx acquire;
	struct i915_request *rq;
	int err, i;

	vma[0] = i915_vma_instance(src, vm, NULL);
	if (IS_ERR(vma[0]))
		return PTR_ERR(vma[0]);

	err = i915_vma_pin(vma[0], 0, 0, PIN_USER);
	if (unlikely(err))
		return err;

	vma[1] = i915_vma_instance(dst, vm, NULL);
	if (IS_ERR(vma[1]))
		goto out_unpin_src;

	err = i915_vma_pin(vma[1], 0, 0, PIN_USER);
	if (unlikely(err))
		goto out_unpin_src;

	batch = intel_emit_vma_copy_blt(ce, vma[0], vma[1]);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unpin_dst;
	}

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	err = intel_emit_vma_mark_active(batch, rq);
	if (unlikely(err))
		goto out_request;

	err = drm_gem_lock_reservations(objs, ARRAY_SIZE(objs), &acquire);
	if (unlikely(err))
		goto out_request;

	for (i = 0; i < ARRAY_SIZE(vma); i++) {
		err = move_to_gpu(vma[i], rq, i);
		if (unlikely(err))
			goto out_unlock;
	}

	for (i = 0; i < ARRAY_SIZE(vma); i++) {
		unsigned int flags = i ? EXEC_OBJECT_WRITE : 0;

		err = i915_vma_move_to_active(vma[i], rq, flags);
		if (unlikely(err))
			goto out_unlock;
	}

	if (rq->engine->emit_init_breadcrumb) {
		err = rq->engine->emit_init_breadcrumb(rq);
		if (unlikely(err))
			goto out_unlock;
	}

	err = rq->engine->emit_bb_start(rq,
					batch->node.start, batch->node.size,
					0);
out_unlock:
	drm_gem_unlock_reservations(objs, ARRAY_SIZE(objs), &acquire);
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
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_object_blt.c"
#endif
