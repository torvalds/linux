// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_engine_pm.h"
#include "selftests/igt_flush_test.h"

static struct i915_vma *create_wally(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *cs;
	int err;

	obj = i915_gem_object_create_internal(engine->i915, 4096);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, engine->gt->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_HIGH);
	if (err) {
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	err = i915_vma_sync(vma);
	if (err) {
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	cs = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		i915_gem_object_put(obj);
		return ERR_CAST(cs);
	}

	if (GRAPHICS_VER(engine->i915) >= 6) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4;
		*cs++ = 0;
	} else if (GRAPHICS_VER(engine->i915) >= 4) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = 0;
	} else {
		*cs++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
	}
	*cs++ = vma->node.start + 4000;
	*cs++ = STACK_MAGIC;

	*cs++ = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	vma->private = intel_context_create(engine); /* dummy residuals */
	if (IS_ERR(vma->private)) {
		vma = ERR_CAST(vma->private);
		i915_gem_object_put(obj);
	}

	return vma;
}

static int context_sync(struct intel_context *ce)
{
	struct i915_request *rq;
	int err = 0;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_request_get(rq);
	i915_request_add(rq);

	if (i915_request_wait(rq, 0, HZ / 5) < 0)
		err = -ETIME;
	i915_request_put(rq);

	return err;
}

static int new_context_sync(struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = context_sync(ce);
	intel_context_put(ce);

	return err;
}

static int mixed_contexts_sync(struct intel_engine_cs *engine, u32 *result)
{
	int pass;
	int err;

	for (pass = 0; pass < 2; pass++) {
		WRITE_ONCE(*result, 0);
		err = context_sync(engine->kernel_context);
		if (err || READ_ONCE(*result)) {
			if (!err) {
				pr_err("pass[%d] wa_bb emitted for the kernel context\n",
				       pass);
				err = -EINVAL;
			}
			return err;
		}

		WRITE_ONCE(*result, 0);
		err = new_context_sync(engine);
		if (READ_ONCE(*result) != STACK_MAGIC) {
			if (!err) {
				pr_err("pass[%d] wa_bb *NOT* emitted after the kernel context\n",
				       pass);
				err = -EINVAL;
			}
			return err;
		}

		WRITE_ONCE(*result, 0);
		err = new_context_sync(engine);
		if (READ_ONCE(*result) != STACK_MAGIC) {
			if (!err) {
				pr_err("pass[%d] wa_bb *NOT* emitted for the user context switch\n",
				       pass);
				err = -EINVAL;
			}
			return err;
		}
	}

	return 0;
}

static int double_context_sync_00(struct intel_engine_cs *engine, u32 *result)
{
	struct intel_context *ce;
	int err, i;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	for (i = 0; i < 2; i++) {
		WRITE_ONCE(*result, 0);
		err = context_sync(ce);
		if (err)
			break;
	}
	intel_context_put(ce);
	if (err)
		return err;

	if (READ_ONCE(*result)) {
		pr_err("wa_bb emitted between the same user context\n");
		return -EINVAL;
	}

	return 0;
}

static int kernel_context_sync_00(struct intel_engine_cs *engine, u32 *result)
{
	struct intel_context *ce;
	int err, i;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	for (i = 0; i < 2; i++) {
		WRITE_ONCE(*result, 0);
		err = context_sync(ce);
		if (err)
			break;

		err = context_sync(engine->kernel_context);
		if (err)
			break;
	}
	intel_context_put(ce);
	if (err)
		return err;

	if (READ_ONCE(*result)) {
		pr_err("wa_bb emitted between the same user context [with intervening kernel]\n");
		return -EINVAL;
	}

	return 0;
}

static int __live_ctx_switch_wa(struct intel_engine_cs *engine)
{
	struct i915_vma *bb;
	u32 *result;
	int err;

	bb = create_wally(engine);
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	result = i915_gem_object_pin_map_unlocked(bb->obj, I915_MAP_WC);
	if (IS_ERR(result)) {
		intel_context_put(bb->private);
		i915_vma_unpin_and_release(&bb, 0);
		return PTR_ERR(result);
	}
	result += 1000;

	engine->wa_ctx.vma = bb;

	err = mixed_contexts_sync(engine, result);
	if (err)
		goto out;

	err = double_context_sync_00(engine, result);
	if (err)
		goto out;

	err = kernel_context_sync_00(engine, result);
	if (err)
		goto out;

out:
	intel_context_put(engine->wa_ctx.vma->private);
	i915_vma_unpin_and_release(&engine->wa_ctx.vma, I915_VMA_RELEASE_MAP);
	return err;
}

static int live_ctx_switch_wa(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Exercise the inter-context wa batch.
	 *
	 * Between each user context we run a wa batch, and since it may
	 * have implications for user visible state, we have to check that
	 * we do actually execute it.
	 *
	 * The trick we use is to replace the normal wa batch with a custom
	 * one that writes to a marker within it, and we can then look for
	 * that marker to confirm if the batch was run when we expect it,
	 * and equally important it was wasn't run when we don't!
	 */

	for_each_engine(engine, gt, id) {
		struct i915_vma *saved_wa;
		int err;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (IS_GRAPHICS_VER(gt->i915, 4, 5))
			continue; /* MI_STORE_DWORD is privileged! */

		saved_wa = fetch_and_zero(&engine->wa_ctx.vma);

		intel_engine_pm_get(engine);
		err = __live_ctx_switch_wa(engine);
		intel_engine_pm_put(engine);
		if (igt_flush_test(gt->i915))
			err = -EIO;

		engine->wa_ctx.vma = saved_wa;
		if (err)
			return err;
	}

	return 0;
}

int intel_ring_submission_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_ctx_switch_wa),
	};

	if (i915->gt.submission_method > INTEL_SUBMISSION_RING)
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
