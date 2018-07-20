/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_selftest.h"

#include "igt_wedge_me.h"
#include "mock_context.h"

static struct drm_i915_gem_object *
read_nonprivs(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *result;
	struct i915_request *rq;
	struct i915_vma *vma;
	const u32 base = engine->mmio_base;
	u32 srm, *cs;
	int err;
	int i;

	result = i915_gem_object_create_internal(engine->i915, PAGE_SIZE);
	if (IS_ERR(result))
		return result;

	i915_gem_object_set_cache_level(result, I915_CACHE_LLC);

	cs = i915_gem_object_pin_map(result, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_obj;
	}
	memset(cs, 0xc5, PAGE_SIZE);
	i915_gem_object_unpin_map(result);

	vma = i915_vma_instance(result, &engine->i915->ggtt.vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_obj;

	rq = i915_request_alloc(engine, ctx);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_pin;
	}

	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	if (err)
		goto err_req;

	srm = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
	if (INTEL_GEN(ctx->i915) >= 8)
		srm++;

	cs = intel_ring_begin(rq, 4 * RING_MAX_NONPRIV_SLOTS);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_req;
	}

	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++) {
		*cs++ = srm;
		*cs++ = i915_mmio_reg_offset(RING_FORCE_TO_NONPRIV(base, i));
		*cs++ = i915_ggtt_offset(vma) + sizeof(u32) * i;
		*cs++ = 0;
	}
	intel_ring_advance(rq, cs);

	i915_gem_object_get(result);
	i915_gem_object_set_active_reference(result);

	i915_request_add(rq);
	i915_vma_unpin(vma);

	return result;

err_req:
	i915_request_add(rq);
err_pin:
	i915_vma_unpin(vma);
err_obj:
	i915_gem_object_put(result);
	return ERR_PTR(err);
}

static u32 get_whitelist_reg(const struct whitelist *w, unsigned int i)
{
	return i < w->count ? i915_mmio_reg_offset(w->reg[i]) : w->nopid;
}

static void print_results(const struct whitelist *w, const u32 *results)
{
	unsigned int i;

	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++) {
		u32 expected = get_whitelist_reg(w, i);
		u32 actual = results[i];

		pr_info("RING_NONPRIV[%d]: expected 0x%08x, found 0x%08x\n",
			i, expected, actual);
	}
}

static int check_whitelist(const struct whitelist *w,
			   struct i915_gem_context *ctx,
			   struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *results;
	struct igt_wedge_me wedge;
	u32 *vaddr;
	int err;
	int i;

	results = read_nonprivs(ctx, engine);
	if (IS_ERR(results))
		return PTR_ERR(results);

	err = 0;
	igt_wedge_on_timeout(&wedge, ctx->i915, HZ / 5) /* a safety net! */
		err = i915_gem_object_set_to_cpu_domain(results, false);
	if (i915_terminally_wedged(&ctx->i915->gpu_error))
		err = -EIO;
	if (err)
		goto out_put;

	vaddr = i915_gem_object_pin_map(results, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_put;
	}

	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++) {
		u32 expected = get_whitelist_reg(w, i);
		u32 actual = vaddr[i];

		if (expected != actual) {
			print_results(w, vaddr);
			pr_err("Invalid RING_NONPRIV[%d], expected 0x%08x, found 0x%08x\n",
			       i, expected, actual);

			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(results);
out_put:
	i915_gem_object_put(results);
	return err;
}

static int do_device_reset(struct intel_engine_cs *engine)
{
	i915_reset(engine->i915, ENGINE_MASK(engine->id), NULL);
	return 0;
}

static int do_engine_reset(struct intel_engine_cs *engine)
{
	return i915_reset_engine(engine, NULL);
}

static int switch_to_scratch_context(struct intel_engine_cs *engine)
{
	struct i915_gem_context *ctx;
	struct i915_request *rq;

	ctx = kernel_context(engine->i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	rq = i915_request_alloc(engine, ctx);
	kernel_context_close(ctx);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_request_add(rq);

	return 0;
}

static int check_whitelist_across_reset(struct intel_engine_cs *engine,
					int (*reset)(struct intel_engine_cs *),
					const struct whitelist *w,
					const char *name)
{
	struct i915_gem_context *ctx;
	int err;

	ctx = kernel_context(engine->i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	err = check_whitelist(w, ctx, engine);
	if (err) {
		pr_err("Invalid whitelist *before* %s reset!\n", name);
		goto out;
	}

	err = switch_to_scratch_context(engine);
	if (err)
		goto out;

	err = reset(engine);
	if (err) {
		pr_err("%s reset failed\n", name);
		goto out;
	}

	err = check_whitelist(w, ctx, engine);
	if (err) {
		pr_err("Whitelist not preserved in context across %s reset!\n",
		       name);
		goto out;
	}

	kernel_context_close(ctx);

	ctx = kernel_context(engine->i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	err = check_whitelist(w, ctx, engine);
	if (err) {
		pr_err("Invalid whitelist *after* %s reset in fresh context!\n",
		       name);
		goto out;
	}

out:
	kernel_context_close(ctx);
	return err;
}

static int live_reset_whitelist(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine = i915->engine[RCS];
	struct i915_gpu_error *error = &i915->gpu_error;
	struct whitelist w;
	int err = 0;

	/* If we reset the gpu, we should not lose the RING_NONPRIV */

	if (!engine)
		return 0;

	if (!whitelist_build(engine, &w))
		return 0;

	pr_info("Checking %d whitelisted registers (RING_NONPRIV)\n", w.count);

	set_bit(I915_RESET_BACKOFF, &error->flags);
	set_bit(I915_RESET_ENGINE + engine->id, &error->flags);

	if (intel_has_reset_engine(i915)) {
		err = check_whitelist_across_reset(engine,
						   do_engine_reset, &w,
						   "engine");
		if (err)
			goto out;
	}

	if (intel_has_gpu_reset(i915)) {
		err = check_whitelist_across_reset(engine,
						   do_device_reset, &w,
						   "device");
		if (err)
			goto out;
	}

out:
	clear_bit(I915_RESET_ENGINE + engine->id, &error->flags);
	clear_bit(I915_RESET_BACKOFF, &error->flags);
	return err;
}

int intel_workarounds_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_reset_whitelist),
	};
	int err;

	if (i915_terminally_wedged(&i915->gpu_error))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_subtests(tests, i915);
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}
