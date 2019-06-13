/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "gem/i915_gem_pm.h"
#include "i915_selftest.h"
#include "intel_reset.h"

#include "selftests/igt_flush_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_spinner.h"
#include "selftests/igt_wedge_me.h"
#include "selftests/mock_drm.h"

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

static const struct wo_register {
	enum intel_platform platform;
	u32 reg;
} wo_registers[] = {
	{ INTEL_GEMINILAKE, 0x731c }
};

#define REF_NAME_MAX (INTEL_ENGINE_CS_MAX_NAME + 8)
struct wa_lists {
	struct i915_wa_list gt_wa_list;
	struct {
		char name[REF_NAME_MAX];
		struct i915_wa_list wa_list;
		struct i915_wa_list ctx_wa_list;
	} engine[I915_NUM_ENGINES];
};

static void
reference_lists_init(struct drm_i915_private *i915, struct wa_lists *lists)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	memset(lists, 0, sizeof(*lists));

	wa_init_start(&lists->gt_wa_list, "GT_REF");
	gt_init_workarounds(i915, &lists->gt_wa_list);
	wa_init_finish(&lists->gt_wa_list);

	for_each_engine(engine, i915, id) {
		struct i915_wa_list *wal = &lists->engine[id].wa_list;
		char *name = lists->engine[id].name;

		snprintf(name, REF_NAME_MAX, "%s_REF", engine->name);

		wa_init_start(wal, name);
		engine_init_workarounds(engine, wal);
		wa_init_finish(wal);

		snprintf(name, REF_NAME_MAX, "%s_CTX_REF", engine->name);

		__intel_engine_init_ctx_wa(engine,
					   &lists->engine[id].ctx_wa_list,
					   name);
	}
}

static void
reference_lists_fini(struct drm_i915_private *i915, struct wa_lists *lists)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id)
		intel_wa_list_free(&lists->engine[id].wa_list);

	intel_wa_list_free(&lists->gt_wa_list);
}

static struct drm_i915_gem_object *
read_nonprivs(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	const u32 base = engine->mmio_base;
	struct drm_i915_gem_object *result;
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 srm, *cs;
	int err;
	int i;

	result = i915_gem_object_create_internal(engine->i915, PAGE_SIZE);
	if (IS_ERR(result))
		return result;

	i915_gem_object_set_cache_coherency(result, I915_CACHE_LLC);

	cs = i915_gem_object_pin_map(result, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_obj;
	}
	memset(cs, 0xc5, PAGE_SIZE);
	i915_gem_object_flush_map(result);
	i915_gem_object_unpin_map(result);

	vma = i915_vma_instance(result, &engine->i915->ggtt.vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_obj;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_pin;
	}

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
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

static u32
get_whitelist_reg(const struct intel_engine_cs *engine, unsigned int i)
{
	i915_reg_t reg = i < engine->whitelist.count ?
			 engine->whitelist.list[i].reg :
			 RING_NOPID(engine->mmio_base);

	return i915_mmio_reg_offset(reg);
}

static void
print_results(const struct intel_engine_cs *engine, const u32 *results)
{
	unsigned int i;

	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++) {
		u32 expected = get_whitelist_reg(engine, i);
		u32 actual = results[i];

		pr_info("RING_NONPRIV[%d]: expected 0x%08x, found 0x%08x\n",
			i, expected, actual);
	}
}

static int check_whitelist(struct i915_gem_context *ctx,
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
	i915_gem_object_lock(results);
	igt_wedge_on_timeout(&wedge, ctx->i915, HZ / 5) /* a safety net! */
		err = i915_gem_object_set_to_cpu_domain(results, false);
	i915_gem_object_unlock(results);
	if (i915_terminally_wedged(ctx->i915))
		err = -EIO;
	if (err)
		goto out_put;

	vaddr = i915_gem_object_pin_map(results, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_put;
	}

	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++) {
		u32 expected = get_whitelist_reg(engine, i);
		u32 actual = vaddr[i];

		if (expected != actual) {
			print_results(engine, vaddr);
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
	i915_reset(engine->i915, engine->mask, "live_workarounds");
	return 0;
}

static int do_engine_reset(struct intel_engine_cs *engine)
{
	return i915_reset_engine(engine, "live_workarounds");
}

static int
switch_to_scratch_context(struct intel_engine_cs *engine,
			  struct igt_spinner *spin)
{
	struct i915_gem_context *ctx;
	struct i915_request *rq;
	intel_wakeref_t wakeref;
	int err = 0;

	ctx = kernel_context(engine->i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	GEM_BUG_ON(i915_gem_context_is_bannable(ctx));

	rq = ERR_PTR(-ENODEV);
	with_intel_runtime_pm(&engine->i915->runtime_pm, wakeref)
		rq = igt_spinner_create_request(spin, ctx, engine, MI_NOOP);

	kernel_context_close(ctx);

	if (IS_ERR(rq)) {
		spin = NULL;
		err = PTR_ERR(rq);
		goto err;
	}

	i915_request_add(rq);

	if (spin && !igt_wait_for_spinner(spin, rq)) {
		pr_err("Spinner failed to start\n");
		err = -ETIMEDOUT;
	}

err:
	if (err && spin)
		igt_spinner_end(spin);

	return err;
}

static int check_whitelist_across_reset(struct intel_engine_cs *engine,
					int (*reset)(struct intel_engine_cs *),
					const char *name)
{
	struct drm_i915_private *i915 = engine->i915;
	struct i915_gem_context *ctx;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err;

	pr_info("Checking %d whitelisted registers (RING_NONPRIV) [%s]\n",
		engine->whitelist.count, name);

	err = igt_spinner_init(&spin, i915);
	if (err)
		return err;

	ctx = kernel_context(i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	err = check_whitelist(ctx, engine);
	if (err) {
		pr_err("Invalid whitelist *before* %s reset!\n", name);
		goto out;
	}

	err = switch_to_scratch_context(engine, &spin);
	if (err)
		goto out;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		err = reset(engine);

	igt_spinner_end(&spin);
	igt_spinner_fini(&spin);

	if (err) {
		pr_err("%s reset failed\n", name);
		goto out;
	}

	err = check_whitelist(ctx, engine);
	if (err) {
		pr_err("Whitelist not preserved in context across %s reset!\n",
		       name);
		goto out;
	}

	kernel_context_close(ctx);

	ctx = kernel_context(i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	err = check_whitelist(ctx, engine);
	if (err) {
		pr_err("Invalid whitelist *after* %s reset in fresh context!\n",
		       name);
		goto out;
	}

out:
	kernel_context_close(ctx);
	return err;
}

static struct i915_vma *create_batch(struct i915_gem_context *ctx)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_internal(ctx->i915, 16 * PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, ctx->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err_obj;

	return vma;

err_obj:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static u32 reg_write(u32 old, u32 new, u32 rsvd)
{
	if (rsvd == 0x0000ffff) {
		old &= ~(new >> 16);
		old |= new & (new >> 16);
	} else {
		old &= ~rsvd;
		old |= new & rsvd;
	}

	return old;
}

static bool wo_register(struct intel_engine_cs *engine, u32 reg)
{
	enum intel_platform platform = INTEL_INFO(engine->i915)->platform;
	int i;

	for (i = 0; i < ARRAY_SIZE(wo_registers); i++) {
		if (wo_registers[i].platform == platform &&
		    wo_registers[i].reg == reg)
			return true;
	}

	return false;
}

static int check_dirty_whitelist(struct i915_gem_context *ctx,
				 struct intel_engine_cs *engine)
{
	const u32 values[] = {
		0x00000000,
		0x01010101,
		0x10100101,
		0x03030303,
		0x30300303,
		0x05050505,
		0x50500505,
		0x0f0f0f0f,
		0xf00ff00f,
		0x10101010,
		0xf0f01010,
		0x30303030,
		0xa0a03030,
		0x50505050,
		0xc0c05050,
		0xf0f0f0f0,
		0x11111111,
		0x33333333,
		0x55555555,
		0x0000ffff,
		0x00ff00ff,
		0xff0000ff,
		0xffff00ff,
		0xffffffff,
	};
	struct i915_vma *scratch;
	struct i915_vma *batch;
	int err = 0, i, v;
	u32 *cs, *results;

	scratch = create_scratch(ctx->vm, 2 * ARRAY_SIZE(values) + 1);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	batch = create_batch(ctx);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_scratch;
	}

	for (i = 0; i < engine->whitelist.count; i++) {
		u32 reg = i915_mmio_reg_offset(engine->whitelist.list[i].reg);
		u64 addr = scratch->node.start;
		struct i915_request *rq;
		u32 srm, lrm, rsvd;
		u32 expect;
		int idx;

		if (wo_register(engine, reg))
			continue;

		srm = MI_STORE_REGISTER_MEM;
		lrm = MI_LOAD_REGISTER_MEM;
		if (INTEL_GEN(ctx->i915) >= 8)
			lrm++, srm++;

		pr_debug("%s: Writing garbage to %x\n",
			 engine->name, reg);

		cs = i915_gem_object_pin_map(batch->obj, I915_MAP_WC);
		if (IS_ERR(cs)) {
			err = PTR_ERR(cs);
			goto out_batch;
		}

		/* SRM original */
		*cs++ = srm;
		*cs++ = reg;
		*cs++ = lower_32_bits(addr);
		*cs++ = upper_32_bits(addr);

		idx = 1;
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			/* LRI garbage */
			*cs++ = MI_LOAD_REGISTER_IMM(1);
			*cs++ = reg;
			*cs++ = values[v];

			/* SRM result */
			*cs++ = srm;
			*cs++ = reg;
			*cs++ = lower_32_bits(addr + sizeof(u32) * idx);
			*cs++ = upper_32_bits(addr + sizeof(u32) * idx);
			idx++;
		}
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			/* LRI garbage */
			*cs++ = MI_LOAD_REGISTER_IMM(1);
			*cs++ = reg;
			*cs++ = ~values[v];

			/* SRM result */
			*cs++ = srm;
			*cs++ = reg;
			*cs++ = lower_32_bits(addr + sizeof(u32) * idx);
			*cs++ = upper_32_bits(addr + sizeof(u32) * idx);
			idx++;
		}
		GEM_BUG_ON(idx * sizeof(u32) > scratch->size);

		/* LRM original -- don't leave garbage in the context! */
		*cs++ = lrm;
		*cs++ = reg;
		*cs++ = lower_32_bits(addr);
		*cs++ = upper_32_bits(addr);

		*cs++ = MI_BATCH_BUFFER_END;

		i915_gem_object_flush_map(batch->obj);
		i915_gem_object_unpin_map(batch->obj);
		i915_gem_chipset_flush(ctx->i915);

		rq = igt_request_alloc(ctx, engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_batch;
		}

		if (engine->emit_init_breadcrumb) { /* Be nice if we hang */
			err = engine->emit_init_breadcrumb(rq);
			if (err)
				goto err_request;
		}

		err = engine->emit_bb_start(rq,
					    batch->node.start, PAGE_SIZE,
					    0);
		if (err)
			goto err_request;

err_request:
		i915_request_add(rq);
		if (err)
			goto out_batch;

		if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 5) < 0) {
			pr_err("%s: Futzing %x timedout; cancelling test\n",
			       engine->name, reg);
			i915_gem_set_wedged(ctx->i915);
			err = -EIO;
			goto out_batch;
		}

		results = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
		if (IS_ERR(results)) {
			err = PTR_ERR(results);
			goto out_batch;
		}

		GEM_BUG_ON(values[ARRAY_SIZE(values) - 1] != 0xffffffff);
		rsvd = results[ARRAY_SIZE(values)]; /* detect write masking */
		if (!rsvd) {
			pr_err("%s: Unable to write to whitelisted register %x\n",
			       engine->name, reg);
			err = -EINVAL;
			goto out_unpin;
		}

		expect = results[0];
		idx = 1;
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			expect = reg_write(expect, values[v], rsvd);
			if (results[idx] != expect)
				err++;
			idx++;
		}
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			expect = reg_write(expect, ~values[v], rsvd);
			if (results[idx] != expect)
				err++;
			idx++;
		}
		if (err) {
			pr_err("%s: %d mismatch between values written to whitelisted register [%x], and values read back!\n",
			       engine->name, err, reg);

			pr_info("%s: Whitelisted register: %x, original value %08x, rsvd %08x\n",
				engine->name, reg, results[0], rsvd);

			expect = results[0];
			idx = 1;
			for (v = 0; v < ARRAY_SIZE(values); v++) {
				u32 w = values[v];

				expect = reg_write(expect, w, rsvd);
				pr_info("Wrote %08x, read %08x, expect %08x\n",
					w, results[idx], expect);
				idx++;
			}
			for (v = 0; v < ARRAY_SIZE(values); v++) {
				u32 w = ~values[v];

				expect = reg_write(expect, w, rsvd);
				pr_info("Wrote %08x, read %08x, expect %08x\n",
					w, results[idx], expect);
				idx++;
			}

			err = -EINVAL;
		}
out_unpin:
		i915_gem_object_unpin_map(scratch->obj);
		if (err)
			break;
	}

	if (igt_flush_test(ctx->i915, I915_WAIT_LOCKED))
		err = -EIO;
out_batch:
	i915_vma_unpin_and_release(&batch, 0);
out_scratch:
	i915_vma_unpin_and_release(&scratch, 0);
	return err;
}

static int live_dirty_whitelist(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	struct drm_file *file;
	int err = 0;

	/* Can the user write to the whitelisted registers? */

	if (INTEL_GEN(i915) < 7) /* minimum requirement for LRI, SRM, LRM */
		return 0;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	mutex_unlock(&i915->drm.struct_mutex);
	file = mock_file(i915);
	mutex_lock(&i915->drm.struct_mutex);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto out_rpm;
	}

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_file;
	}

	for_each_engine(engine, i915, id) {
		if (engine->whitelist.count == 0)
			continue;

		err = check_dirty_whitelist(ctx, engine);
		if (err)
			goto out_file;
	}

out_file:
	mutex_unlock(&i915->drm.struct_mutex);
	mock_file_free(i915, file);
	mutex_lock(&i915->drm.struct_mutex);
out_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return err;
}

static int live_reset_whitelist(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine = i915->engine[RCS0];
	int err = 0;

	/* If we reset the gpu, we should not lose the RING_NONPRIV */

	if (!engine || engine->whitelist.count == 0)
		return 0;

	igt_global_reset_lock(i915);

	if (intel_has_reset_engine(i915)) {
		err = check_whitelist_across_reset(engine,
						   do_engine_reset,
						   "engine");
		if (err)
			goto out;
	}

	if (intel_has_gpu_reset(i915)) {
		err = check_whitelist_across_reset(engine,
						   do_device_reset,
						   "device");
		if (err)
			goto out;
	}

out:
	igt_global_reset_unlock(i915);
	return err;
}

static int read_whitelisted_registers(struct i915_gem_context *ctx,
				      struct intel_engine_cs *engine,
				      struct i915_vma *results)
{
	struct i915_request *rq;
	int i, err = 0;
	u32 srm, *cs;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	srm = MI_STORE_REGISTER_MEM;
	if (INTEL_GEN(ctx->i915) >= 8)
		srm++;

	cs = intel_ring_begin(rq, 4 * engine->whitelist.count);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_req;
	}

	for (i = 0; i < engine->whitelist.count; i++) {
		u64 offset = results->node.start + sizeof(u32) * i;

		*cs++ = srm;
		*cs++ = i915_mmio_reg_offset(engine->whitelist.list[i].reg);
		*cs++ = lower_32_bits(offset);
		*cs++ = upper_32_bits(offset);
	}
	intel_ring_advance(rq, cs);

err_req:
	i915_request_add(rq);

	if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 5) < 0)
		err = -EIO;

	return err;
}

static int scrub_whitelisted_registers(struct i915_gem_context *ctx,
				       struct intel_engine_cs *engine)
{
	struct i915_request *rq;
	struct i915_vma *batch;
	int i, err = 0;
	u32 *cs;

	batch = create_batch(ctx);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	cs = i915_gem_object_pin_map(batch->obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_batch;
	}

	*cs++ = MI_LOAD_REGISTER_IMM(engine->whitelist.count);
	for (i = 0; i < engine->whitelist.count; i++) {
		*cs++ = i915_mmio_reg_offset(engine->whitelist.list[i].reg);
		*cs++ = 0xffffffff;
	}
	*cs++ = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(batch->obj);
	i915_gem_chipset_flush(ctx->i915);

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	if (engine->emit_init_breadcrumb) { /* Be nice if we hang */
		err = engine->emit_init_breadcrumb(rq);
		if (err)
			goto err_request;
	}

	/* Perform the writes from an unprivileged "user" batch */
	err = engine->emit_bb_start(rq, batch->node.start, 0, 0);

err_request:
	i915_request_add(rq);
	if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 5) < 0)
		err = -EIO;

err_unpin:
	i915_gem_object_unpin_map(batch->obj);
err_batch:
	i915_vma_unpin_and_release(&batch, 0);
	return err;
}

struct regmask {
	i915_reg_t reg;
	unsigned long gen_mask;
};

static bool find_reg(struct drm_i915_private *i915,
		     i915_reg_t reg,
		     const struct regmask *tbl,
		     unsigned long count)
{
	u32 offset = i915_mmio_reg_offset(reg);

	while (count--) {
		if (INTEL_INFO(i915)->gen_mask & tbl->gen_mask &&
		    i915_mmio_reg_offset(tbl->reg) == offset)
			return true;
		tbl++;
	}

	return false;
}

static bool pardon_reg(struct drm_i915_private *i915, i915_reg_t reg)
{
	/* Alas, we must pardon some whitelists. Mistakes already made */
	static const struct regmask pardon[] = {
		{ GEN9_CTX_PREEMPT_REG, INTEL_GEN_MASK(9, 9) },
		{ GEN8_L3SQCREG4, INTEL_GEN_MASK(9, 9) },
	};

	return find_reg(i915, reg, pardon, ARRAY_SIZE(pardon));
}

static bool result_eq(struct intel_engine_cs *engine,
		      u32 a, u32 b, i915_reg_t reg)
{
	if (a != b && !pardon_reg(engine->i915, reg)) {
		pr_err("Whitelisted register 0x%4x not context saved: A=%08x, B=%08x\n",
		       i915_mmio_reg_offset(reg), a, b);
		return false;
	}

	return true;
}

static bool writeonly_reg(struct drm_i915_private *i915, i915_reg_t reg)
{
	/* Some registers do not seem to behave and our writes unreadable */
	static const struct regmask wo[] = {
		{ GEN9_SLICE_COMMON_ECO_CHICKEN1, INTEL_GEN_MASK(9, 9) },
	};

	return find_reg(i915, reg, wo, ARRAY_SIZE(wo));
}

static bool result_neq(struct intel_engine_cs *engine,
		       u32 a, u32 b, i915_reg_t reg)
{
	if (a == b && !writeonly_reg(engine->i915, reg)) {
		pr_err("Whitelist register 0x%4x:%08x was unwritable\n",
		       i915_mmio_reg_offset(reg), a);
		return false;
	}

	return true;
}

static int
check_whitelisted_registers(struct intel_engine_cs *engine,
			    struct i915_vma *A,
			    struct i915_vma *B,
			    bool (*fn)(struct intel_engine_cs *engine,
				       u32 a, u32 b,
				       i915_reg_t reg))
{
	u32 *a, *b;
	int i, err;

	a = i915_gem_object_pin_map(A->obj, I915_MAP_WB);
	if (IS_ERR(a))
		return PTR_ERR(a);

	b = i915_gem_object_pin_map(B->obj, I915_MAP_WB);
	if (IS_ERR(b)) {
		err = PTR_ERR(b);
		goto err_a;
	}

	err = 0;
	for (i = 0; i < engine->whitelist.count; i++) {
		if (!fn(engine, a[i], b[i], engine->whitelist.list[i].reg))
			err = -EINVAL;
	}

	i915_gem_object_unpin_map(B->obj);
err_a:
	i915_gem_object_unpin_map(A->obj);
	return err;
}

static int live_isolated_whitelist(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct {
		struct i915_gem_context *ctx;
		struct i915_vma *scratch[2];
	} client[2] = {};
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int i, err = 0;

	/*
	 * Check that a write into a whitelist register works, but
	 * invisible to a second context.
	 */

	if (!intel_engines_has_context_isolation(i915))
		return 0;

	if (!i915->kernel_context->vm)
		return 0;

	for (i = 0; i < ARRAY_SIZE(client); i++) {
		struct i915_gem_context *c;

		c = kernel_context(i915);
		if (IS_ERR(c)) {
			err = PTR_ERR(c);
			goto err;
		}

		client[i].scratch[0] = create_scratch(c->vm, 1024);
		if (IS_ERR(client[i].scratch[0])) {
			err = PTR_ERR(client[i].scratch[0]);
			kernel_context_close(c);
			goto err;
		}

		client[i].scratch[1] = create_scratch(c->vm, 1024);
		if (IS_ERR(client[i].scratch[1])) {
			err = PTR_ERR(client[i].scratch[1]);
			i915_vma_unpin_and_release(&client[i].scratch[0], 0);
			kernel_context_close(c);
			goto err;
		}

		client[i].ctx = c;
	}

	for_each_engine(engine, i915, id) {
		if (!engine->whitelist.count)
			continue;

		/* Read default values */
		err = read_whitelisted_registers(client[0].ctx, engine,
						 client[0].scratch[0]);
		if (err)
			goto err;

		/* Try to overwrite registers (should only affect ctx0) */
		err = scrub_whitelisted_registers(client[0].ctx, engine);
		if (err)
			goto err;

		/* Read values from ctx1, we expect these to be defaults */
		err = read_whitelisted_registers(client[1].ctx, engine,
						 client[1].scratch[0]);
		if (err)
			goto err;

		/* Verify that both reads return the same default values */
		err = check_whitelisted_registers(engine,
						  client[0].scratch[0],
						  client[1].scratch[0],
						  result_eq);
		if (err)
			goto err;

		/* Read back the updated values in ctx0 */
		err = read_whitelisted_registers(client[0].ctx, engine,
						 client[0].scratch[1]);
		if (err)
			goto err;

		/* User should be granted privilege to overwhite regs */
		err = check_whitelisted_registers(engine,
						  client[0].scratch[0],
						  client[0].scratch[1],
						  result_neq);
		if (err)
			goto err;
	}

err:
	for (i = 0; i < ARRAY_SIZE(client); i++) {
		if (!client[i].ctx)
			break;

		i915_vma_unpin_and_release(&client[i].scratch[1], 0);
		i915_vma_unpin_and_release(&client[i].scratch[0], 0);
		kernel_context_close(client[i].ctx);
	}

	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;

	return err;
}

static bool
verify_wa_lists(struct i915_gem_context *ctx, struct wa_lists *lists,
		const char *str)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	bool ok = true;

	ok &= wa_list_verify(&i915->uncore, &lists->gt_wa_list, str);

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		enum intel_engine_id id = ce->engine->id;

		ok &= engine_wa_list_verify(ce,
					    &lists->engine[id].wa_list,
					    str) == 0;

		ok &= engine_wa_list_verify(ce,
					    &lists->engine[id].ctx_wa_list,
					    str) == 0;
	}
	i915_gem_context_unlock_engines(ctx);

	return ok;
}

static int
live_gpu_reset_workarounds(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx;
	intel_wakeref_t wakeref;
	struct wa_lists lists;
	bool ok;

	if (!intel_has_gpu_reset(i915))
		return 0;

	ctx = kernel_context(i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	pr_info("Verifying after GPU reset...\n");

	igt_global_reset_lock(i915);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	reference_lists_init(i915, &lists);

	ok = verify_wa_lists(ctx, &lists, "before reset");
	if (!ok)
		goto out;

	i915_reset(i915, ALL_ENGINES, "live_workarounds");

	ok = verify_wa_lists(ctx, &lists, "after reset");

out:
	kernel_context_close(ctx);
	reference_lists_fini(i915, &lists);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	igt_global_reset_unlock(i915);

	return ok ? 0 : -ESRCH;
}

static int
live_engine_reset_workarounds(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	struct igt_spinner spin;
	enum intel_engine_id id;
	struct i915_request *rq;
	intel_wakeref_t wakeref;
	struct wa_lists lists;
	int ret = 0;

	if (!intel_has_reset_engine(i915))
		return 0;

	ctx = kernel_context(i915);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	igt_global_reset_lock(i915);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	reference_lists_init(i915, &lists);

	for_each_engine(engine, i915, id) {
		bool ok;

		pr_info("Verifying after %s reset...\n", engine->name);

		ok = verify_wa_lists(ctx, &lists, "before reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}

		i915_reset_engine(engine, "live_workarounds");

		ok = verify_wa_lists(ctx, &lists, "after idle reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}

		ret = igt_spinner_init(&spin, i915);
		if (ret)
			goto err;

		rq = igt_spinner_create_request(&spin, ctx, engine, MI_NOOP);
		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			igt_spinner_fini(&spin);
			goto err;
		}

		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			pr_err("Spinner failed to start\n");
			igt_spinner_fini(&spin);
			ret = -ETIMEDOUT;
			goto err;
		}

		i915_reset_engine(engine, "live_workarounds");

		igt_spinner_end(&spin);
		igt_spinner_fini(&spin);

		ok = verify_wa_lists(ctx, &lists, "after busy reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}
	}

err:
	reference_lists_fini(i915, &lists);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	igt_global_reset_unlock(i915);
	kernel_context_close(ctx);

	igt_flush_test(i915, I915_WAIT_LOCKED);

	return ret;
}

int intel_workarounds_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_dirty_whitelist),
		SUBTEST(live_reset_whitelist),
		SUBTEST(live_isolated_whitelist),
		SUBTEST(live_gpu_reset_workarounds),
		SUBTEST(live_engine_reset_workarounds),
	};
	int err;

	if (i915_terminally_wedged(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_subtests(tests, i915);
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}
