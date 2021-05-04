/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "gem/i915_gem_pm.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"
#include "i915_selftest.h"
#include "intel_reset.h"

#include "selftests/igt_flush_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_spinner.h"
#include "selftests/mock_drm.h"

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

static const struct wo_register {
	enum intel_platform platform;
	u32 reg;
} wo_registers[] = {
	{ INTEL_GEMINILAKE, 0x731c }
};

struct wa_lists {
	struct i915_wa_list gt_wa_list;
	struct {
		struct i915_wa_list wa_list;
		struct i915_wa_list ctx_wa_list;
	} engine[I915_NUM_ENGINES];
};

static int request_add_sync(struct i915_request *rq, int err)
{
	i915_request_get(rq);
	i915_request_add(rq);
	if (i915_request_wait(rq, 0, HZ / 5) < 0)
		err = -EIO;
	i915_request_put(rq);

	return err;
}

static int request_add_spin(struct i915_request *rq, struct igt_spinner *spin)
{
	int err = 0;

	i915_request_get(rq);
	i915_request_add(rq);
	if (spin && !igt_wait_for_spinner(spin, rq))
		err = -ETIMEDOUT;
	i915_request_put(rq);

	return err;
}

static void
reference_lists_init(struct intel_gt *gt, struct wa_lists *lists)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	memset(lists, 0, sizeof(*lists));

	wa_init_start(&lists->gt_wa_list, "GT_REF", "global");
	gt_init_workarounds(gt->i915, &lists->gt_wa_list);
	wa_init_finish(&lists->gt_wa_list);

	for_each_engine(engine, gt, id) {
		struct i915_wa_list *wal = &lists->engine[id].wa_list;

		wa_init_start(wal, "REF", engine->name);
		engine_init_workarounds(engine, wal);
		wa_init_finish(wal);

		__intel_engine_init_ctx_wa(engine,
					   &lists->engine[id].ctx_wa_list,
					   "CTX_REF");
	}
}

static void
reference_lists_fini(struct intel_gt *gt, struct wa_lists *lists)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		intel_wa_list_free(&lists->engine[id].wa_list);

	intel_wa_list_free(&lists->gt_wa_list);
}

static struct drm_i915_gem_object *
read_nonprivs(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
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

	vma = i915_vma_instance(result, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_obj;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_pin;
	}

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	if (err)
		goto err_req;

	srm = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
	if (INTEL_GEN(engine->i915) >= 8)
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

static int check_whitelist(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	struct drm_i915_gem_object *results;
	struct intel_wedge_me wedge;
	u32 *vaddr;
	int err;
	int i;

	results = read_nonprivs(ce);
	if (IS_ERR(results))
		return PTR_ERR(results);

	err = 0;
	i915_gem_object_lock(results, NULL);
	intel_wedge_on_timeout(&wedge, engine->gt, HZ / 5) /* safety net! */
		err = i915_gem_object_set_to_cpu_domain(results, false);
	i915_gem_object_unlock(results);
	if (intel_gt_is_wedged(engine->gt))
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
	intel_gt_reset(engine->gt, engine->mask, "live_workarounds");
	return 0;
}

static int do_engine_reset(struct intel_engine_cs *engine)
{
	return intel_engine_reset(engine, "live_workarounds");
}

static int
switch_to_scratch_context(struct intel_engine_cs *engine,
			  struct igt_spinner *spin)
{
	struct intel_context *ce;
	struct i915_request *rq;
	int err = 0;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	rq = igt_spinner_create_request(spin, ce, MI_NOOP);
	intel_context_put(ce);

	if (IS_ERR(rq)) {
		spin = NULL;
		err = PTR_ERR(rq);
		goto err;
	}

	err = request_add_spin(rq, spin);
err:
	if (err && spin)
		igt_spinner_end(spin);

	return err;
}

static int check_whitelist_across_reset(struct intel_engine_cs *engine,
					int (*reset)(struct intel_engine_cs *),
					const char *name)
{
	struct intel_context *ce, *tmp;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err;

	pr_info("Checking %d whitelisted registers on %s (RING_NONPRIV) [%s]\n",
		engine->whitelist.count, engine->name, name);

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = igt_spinner_init(&spin, engine->gt);
	if (err)
		goto out_ctx;

	err = check_whitelist(ce);
	if (err) {
		pr_err("Invalid whitelist *before* %s reset!\n", name);
		goto out_spin;
	}

	err = switch_to_scratch_context(engine, &spin);
	if (err)
		goto out_spin;

	with_intel_runtime_pm(engine->uncore->rpm, wakeref)
		err = reset(engine);

	igt_spinner_end(&spin);

	if (err) {
		pr_err("%s reset failed\n", name);
		goto out_spin;
	}

	err = check_whitelist(ce);
	if (err) {
		pr_err("Whitelist not preserved in context across %s reset!\n",
		       name);
		goto out_spin;
	}

	tmp = intel_context_create(engine);
	if (IS_ERR(tmp)) {
		err = PTR_ERR(tmp);
		goto out_spin;
	}
	intel_context_put(ce);
	ce = tmp;

	err = check_whitelist(ce);
	if (err) {
		pr_err("Invalid whitelist *after* %s reset in fresh context!\n",
		       name);
		goto out_spin;
	}

out_spin:
	igt_spinner_fini(&spin);
out_ctx:
	intel_context_put(ce);
	return err;
}

static struct i915_vma *create_batch(struct i915_address_space *vm)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_internal(vm->i915, 16 * PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, vm, NULL);
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

	if ((reg & RING_FORCE_TO_NONPRIV_ACCESS_MASK) ==
	     RING_FORCE_TO_NONPRIV_ACCESS_WR)
		return true;

	for (i = 0; i < ARRAY_SIZE(wo_registers); i++) {
		if (wo_registers[i].platform == platform &&
		    wo_registers[i].reg == reg)
			return true;
	}

	return false;
}

static bool timestamp(const struct intel_engine_cs *engine, u32 reg)
{
	reg = (reg - engine->mmio_base) & ~RING_FORCE_TO_NONPRIV_ACCESS_MASK;
	switch (reg) {
	case 0x358:
	case 0x35c:
	case 0x3a8:
		return true;

	default:
		return false;
	}
}

static bool ro_register(u32 reg)
{
	if ((reg & RING_FORCE_TO_NONPRIV_ACCESS_MASK) ==
	     RING_FORCE_TO_NONPRIV_ACCESS_RD)
		return true;

	return false;
}

static int whitelist_writable_count(struct intel_engine_cs *engine)
{
	int count = engine->whitelist.count;
	int i;

	for (i = 0; i < engine->whitelist.count; i++) {
		u32 reg = i915_mmio_reg_offset(engine->whitelist.list[i].reg);

		if (ro_register(reg))
			count--;
	}

	return count;
}

static int check_dirty_whitelist(struct intel_context *ce)
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
	struct intel_engine_cs *engine = ce->engine;
	struct i915_vma *scratch;
	struct i915_vma *batch;
	int err = 0, i, v, sz;
	u32 *cs, *results;

	sz = (2 * ARRAY_SIZE(values) + 1) * sizeof(u32);
	scratch = __vm_create_scratch_for_read(ce->vm, sz);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	batch = create_batch(ce->vm);
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
		bool ro_reg;

		if (wo_register(engine, reg))
			continue;

		if (timestamp(engine, reg))
			continue; /* timestamps are expected to autoincrement */

		ro_reg = ro_register(reg);

		/* Clear non priv flags */
		reg &= RING_FORCE_TO_NONPRIV_ADDRESS_MASK;

		srm = MI_STORE_REGISTER_MEM;
		lrm = MI_LOAD_REGISTER_MEM;
		if (INTEL_GEN(engine->i915) >= 8)
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
		intel_gt_chipset_flush(engine->gt);

		rq = intel_context_create_request(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_batch;
		}

		if (engine->emit_init_breadcrumb) { /* Be nice if we hang */
			err = engine->emit_init_breadcrumb(rq);
			if (err)
				goto err_request;
		}

		i915_vma_lock(batch);
		err = i915_request_await_object(rq, batch->obj, false);
		if (err == 0)
			err = i915_vma_move_to_active(batch, rq, 0);
		i915_vma_unlock(batch);
		if (err)
			goto err_request;

		i915_vma_lock(scratch);
		err = i915_request_await_object(rq, scratch->obj, true);
		if (err == 0)
			err = i915_vma_move_to_active(scratch, rq,
						      EXEC_OBJECT_WRITE);
		i915_vma_unlock(scratch);
		if (err)
			goto err_request;

		err = engine->emit_bb_start(rq,
					    batch->node.start, PAGE_SIZE,
					    0);
		if (err)
			goto err_request;

err_request:
		err = request_add_sync(rq, err);
		if (err) {
			pr_err("%s: Futzing %x timedout; cancelling test\n",
			       engine->name, reg);
			intel_gt_set_wedged(engine->gt);
			goto out_batch;
		}

		results = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
		if (IS_ERR(results)) {
			err = PTR_ERR(results);
			goto out_batch;
		}

		GEM_BUG_ON(values[ARRAY_SIZE(values) - 1] != 0xffffffff);
		if (!ro_reg) {
			/* detect write masking */
			rsvd = results[ARRAY_SIZE(values)];
			if (!rsvd) {
				pr_err("%s: Unable to write to whitelisted register %x\n",
				       engine->name, reg);
				err = -EINVAL;
				goto out_unpin;
			}
		} else {
			rsvd = 0;
		}

		expect = results[0];
		idx = 1;
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			if (ro_reg)
				expect = results[0];
			else
				expect = reg_write(expect, values[v], rsvd);

			if (results[idx] != expect)
				err++;
			idx++;
		}
		for (v = 0; v < ARRAY_SIZE(values); v++) {
			if (ro_reg)
				expect = results[0];
			else
				expect = reg_write(expect, ~values[v], rsvd);

			if (results[idx] != expect)
				err++;
			idx++;
		}
		if (err) {
			pr_err("%s: %d mismatch between values written to whitelisted register [%x], and values read back!\n",
			       engine->name, err, reg);

			if (ro_reg)
				pr_info("%s: Whitelisted read-only register: %x, original value %08x\n",
					engine->name, reg, results[0]);
			else
				pr_info("%s: Whitelisted register: %x, original value %08x, rsvd %08x\n",
					engine->name, reg, results[0], rsvd);

			expect = results[0];
			idx = 1;
			for (v = 0; v < ARRAY_SIZE(values); v++) {
				u32 w = values[v];

				if (ro_reg)
					expect = results[0];
				else
					expect = reg_write(expect, w, rsvd);
				pr_info("Wrote %08x, read %08x, expect %08x\n",
					w, results[idx], expect);
				idx++;
			}
			for (v = 0; v < ARRAY_SIZE(values); v++) {
				u32 w = ~values[v];

				if (ro_reg)
					expect = results[0];
				else
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

	if (igt_flush_test(engine->i915))
		err = -EIO;
out_batch:
	i915_vma_unpin_and_release(&batch, 0);
out_scratch:
	i915_vma_unpin_and_release(&scratch, 0);
	return err;
}

static int live_dirty_whitelist(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Can the user write to the whitelisted registers? */

	if (INTEL_GEN(gt->i915) < 7) /* minimum requirement for LRI, SRM, LRM */
		return 0;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		int err;

		if (engine->whitelist.count == 0)
			continue;

		ce = intel_context_create(engine);
		if (IS_ERR(ce))
			return PTR_ERR(ce);

		err = check_dirty_whitelist(ce);
		intel_context_put(ce);
		if (err)
			return err;
	}

	return 0;
}

static int live_reset_whitelist(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* If we reset the gpu, we should not lose the RING_NONPRIV */
	igt_global_reset_lock(gt);

	for_each_engine(engine, gt, id) {
		if (engine->whitelist.count == 0)
			continue;

		if (intel_has_reset_engine(gt)) {
			err = check_whitelist_across_reset(engine,
							   do_engine_reset,
							   "engine");
			if (err)
				goto out;
		}

		if (intel_has_gpu_reset(gt)) {
			err = check_whitelist_across_reset(engine,
							   do_device_reset,
							   "device");
			if (err)
				goto out;
		}
	}

out:
	igt_global_reset_unlock(gt);
	return err;
}

static int read_whitelisted_registers(struct intel_context *ce,
				      struct i915_vma *results)
{
	struct intel_engine_cs *engine = ce->engine;
	struct i915_request *rq;
	int i, err = 0;
	u32 srm, *cs;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_vma_lock(results);
	err = i915_request_await_object(rq, results->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(results, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(results);
	if (err)
		goto err_req;

	srm = MI_STORE_REGISTER_MEM;
	if (INTEL_GEN(engine->i915) >= 8)
		srm++;

	cs = intel_ring_begin(rq, 4 * engine->whitelist.count);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_req;
	}

	for (i = 0; i < engine->whitelist.count; i++) {
		u64 offset = results->node.start + sizeof(u32) * i;
		u32 reg = i915_mmio_reg_offset(engine->whitelist.list[i].reg);

		/* Clear non priv flags */
		reg &= RING_FORCE_TO_NONPRIV_ADDRESS_MASK;

		*cs++ = srm;
		*cs++ = reg;
		*cs++ = lower_32_bits(offset);
		*cs++ = upper_32_bits(offset);
	}
	intel_ring_advance(rq, cs);

err_req:
	return request_add_sync(rq, err);
}

static int scrub_whitelisted_registers(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	struct i915_request *rq;
	struct i915_vma *batch;
	int i, err = 0;
	u32 *cs;

	batch = create_batch(ce->vm);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	cs = i915_gem_object_pin_map(batch->obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_batch;
	}

	*cs++ = MI_LOAD_REGISTER_IMM(whitelist_writable_count(engine));
	for (i = 0; i < engine->whitelist.count; i++) {
		u32 reg = i915_mmio_reg_offset(engine->whitelist.list[i].reg);

		if (ro_register(reg))
			continue;

		/* Clear non priv flags */
		reg &= RING_FORCE_TO_NONPRIV_ADDRESS_MASK;

		*cs++ = reg;
		*cs++ = 0xffffffff;
	}
	*cs++ = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(batch->obj);
	intel_gt_chipset_flush(engine->gt);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	if (engine->emit_init_breadcrumb) { /* Be nice if we hang */
		err = engine->emit_init_breadcrumb(rq);
		if (err)
			goto err_request;
	}

	i915_vma_lock(batch);
	err = i915_request_await_object(rq, batch->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(batch, rq, 0);
	i915_vma_unlock(batch);
	if (err)
		goto err_request;

	/* Perform the writes from an unprivileged "user" batch */
	err = engine->emit_bb_start(rq, batch->node.start, 0, 0);

err_request:
	err = request_add_sync(rq, err);

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
		const struct i915_wa *wa = &engine->whitelist.list[i];

		if (i915_mmio_reg_offset(wa->reg) &
		    RING_FORCE_TO_NONPRIV_ACCESS_RD)
			continue;

		if (!fn(engine, a[i], b[i], wa->reg))
			err = -EINVAL;
	}

	i915_gem_object_unpin_map(B->obj);
err_a:
	i915_gem_object_unpin_map(A->obj);
	return err;
}

static int live_isolated_whitelist(void *arg)
{
	struct intel_gt *gt = arg;
	struct {
		struct i915_vma *scratch[2];
	} client[2] = {};
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int i, err = 0;

	/*
	 * Check that a write into a whitelist register works, but
	 * invisible to a second context.
	 */

	if (!intel_engines_has_context_isolation(gt->i915))
		return 0;

	for (i = 0; i < ARRAY_SIZE(client); i++) {
		client[i].scratch[0] =
			__vm_create_scratch_for_read(gt->vm, 4096);
		if (IS_ERR(client[i].scratch[0])) {
			err = PTR_ERR(client[i].scratch[0]);
			goto err;
		}

		client[i].scratch[1] =
			__vm_create_scratch_for_read(gt->vm, 4096);
		if (IS_ERR(client[i].scratch[1])) {
			err = PTR_ERR(client[i].scratch[1]);
			i915_vma_unpin_and_release(&client[i].scratch[0], 0);
			goto err;
		}
	}

	for_each_engine(engine, gt, id) {
		struct intel_context *ce[2];

		if (!engine->kernel_context->vm)
			continue;

		if (!whitelist_writable_count(engine))
			continue;

		ce[0] = intel_context_create(engine);
		if (IS_ERR(ce[0])) {
			err = PTR_ERR(ce[0]);
			break;
		}
		ce[1] = intel_context_create(engine);
		if (IS_ERR(ce[1])) {
			err = PTR_ERR(ce[1]);
			intel_context_put(ce[0]);
			break;
		}

		/* Read default values */
		err = read_whitelisted_registers(ce[0], client[0].scratch[0]);
		if (err)
			goto err_ce;

		/* Try to overwrite registers (should only affect ctx0) */
		err = scrub_whitelisted_registers(ce[0]);
		if (err)
			goto err_ce;

		/* Read values from ctx1, we expect these to be defaults */
		err = read_whitelisted_registers(ce[1], client[1].scratch[0]);
		if (err)
			goto err_ce;

		/* Verify that both reads return the same default values */
		err = check_whitelisted_registers(engine,
						  client[0].scratch[0],
						  client[1].scratch[0],
						  result_eq);
		if (err)
			goto err_ce;

		/* Read back the updated values in ctx0 */
		err = read_whitelisted_registers(ce[0], client[0].scratch[1]);
		if (err)
			goto err_ce;

		/* User should be granted privilege to overwhite regs */
		err = check_whitelisted_registers(engine,
						  client[0].scratch[0],
						  client[0].scratch[1],
						  result_neq);
err_ce:
		intel_context_put(ce[1]);
		intel_context_put(ce[0]);
		if (err)
			break;
	}

err:
	for (i = 0; i < ARRAY_SIZE(client); i++) {
		i915_vma_unpin_and_release(&client[i].scratch[1], 0);
		i915_vma_unpin_and_release(&client[i].scratch[0], 0);
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	return err;
}

static bool
verify_wa_lists(struct intel_gt *gt, struct wa_lists *lists,
		const char *str)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	bool ok = true;

	ok &= wa_list_verify(gt->uncore, &lists->gt_wa_list, str);

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;

		ce = intel_context_create(engine);
		if (IS_ERR(ce))
			return false;

		ok &= engine_wa_list_verify(ce,
					    &lists->engine[id].wa_list,
					    str) == 0;

		ok &= engine_wa_list_verify(ce,
					    &lists->engine[id].ctx_wa_list,
					    str) == 0;

		intel_context_put(ce);
	}

	return ok;
}

static int
live_gpu_reset_workarounds(void *arg)
{
	struct intel_gt *gt = arg;
	intel_wakeref_t wakeref;
	struct wa_lists lists;
	bool ok;

	if (!intel_has_gpu_reset(gt))
		return 0;

	pr_info("Verifying after GPU reset...\n");

	igt_global_reset_lock(gt);
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	reference_lists_init(gt, &lists);

	ok = verify_wa_lists(gt, &lists, "before reset");
	if (!ok)
		goto out;

	intel_gt_reset(gt, ALL_ENGINES, "live_workarounds");

	ok = verify_wa_lists(gt, &lists, "after reset");

out:
	reference_lists_fini(gt, &lists);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	igt_global_reset_unlock(gt);

	return ok ? 0 : -ESRCH;
}

static int
live_engine_reset_workarounds(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct intel_context *ce;
	struct igt_spinner spin;
	struct i915_request *rq;
	intel_wakeref_t wakeref;
	struct wa_lists lists;
	int ret = 0;

	if (!intel_has_reset_engine(gt))
		return 0;

	igt_global_reset_lock(gt);
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	reference_lists_init(gt, &lists);

	for_each_engine(engine, gt, id) {
		bool ok;

		pr_info("Verifying after %s reset...\n", engine->name);
		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			ret = PTR_ERR(ce);
			break;
		}

		ok = verify_wa_lists(gt, &lists, "before reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}

		intel_engine_reset(engine, "live_workarounds:idle");

		ok = verify_wa_lists(gt, &lists, "after idle reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}

		ret = igt_spinner_init(&spin, engine->gt);
		if (ret)
			goto err;

		rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			igt_spinner_fini(&spin);
			goto err;
		}

		ret = request_add_spin(rq, &spin);
		if (ret) {
			pr_err("Spinner failed to start\n");
			igt_spinner_fini(&spin);
			goto err;
		}

		intel_engine_reset(engine, "live_workarounds:active");

		igt_spinner_end(&spin);
		igt_spinner_fini(&spin);

		ok = verify_wa_lists(gt, &lists, "after busy reset");
		if (!ok) {
			ret = -ESRCH;
			goto err;
		}

err:
		intel_context_put(ce);
		if (ret)
			break;
	}

	reference_lists_fini(gt, &lists);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	igt_global_reset_unlock(gt);

	igt_flush_test(gt->i915);

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

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
