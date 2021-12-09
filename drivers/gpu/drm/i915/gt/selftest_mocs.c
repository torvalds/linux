// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "i915_selftest.h"

#include "gem/selftests/mock_context.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_spinner.h"
#include "selftests/intel_scheduler_helpers.h"

struct live_mocs {
	struct drm_i915_mocs_table table;
	struct drm_i915_mocs_table *mocs;
	struct drm_i915_mocs_table *l3cc;
	struct i915_vma *scratch;
	void *vaddr;
};

static struct intel_context *mocs_context_create(struct intel_engine_cs *engine)
{
	struct intel_context *ce;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return ce;

	/* We build large requests to read the registers from the ring */
	ce->ring_size = SZ_16K;

	return ce;
}

static int request_add_sync(struct i915_request *rq, int err)
{
	i915_request_get(rq);
	i915_request_add(rq);
	if (i915_request_wait(rq, 0, HZ / 5) < 0)
		err = -ETIME;
	i915_request_put(rq);

	return err;
}

static int request_add_spin(struct i915_request *rq, struct igt_spinner *spin)
{
	int err = 0;

	i915_request_get(rq);
	i915_request_add(rq);
	if (spin && !igt_wait_for_spinner(spin, rq))
		err = -ETIME;
	i915_request_put(rq);

	return err;
}

static int live_mocs_init(struct live_mocs *arg, struct intel_gt *gt)
{
	unsigned int flags;
	int err;

	memset(arg, 0, sizeof(*arg));

	flags = get_mocs_settings(gt->i915, &arg->table);
	if (!flags)
		return -EINVAL;

	if (flags & HAS_RENDER_L3CC)
		arg->l3cc = &arg->table;

	if (flags & (HAS_GLOBAL_MOCS | HAS_ENGINE_MOCS))
		arg->mocs = &arg->table;

	arg->scratch =
		__vm_create_scratch_for_read_pinned(&gt->ggtt->vm, PAGE_SIZE);
	if (IS_ERR(arg->scratch))
		return PTR_ERR(arg->scratch);

	arg->vaddr = i915_gem_object_pin_map_unlocked(arg->scratch->obj, I915_MAP_WB);
	if (IS_ERR(arg->vaddr)) {
		err = PTR_ERR(arg->vaddr);
		goto err_scratch;
	}

	return 0;

err_scratch:
	i915_vma_unpin_and_release(&arg->scratch, 0);
	return err;
}

static void live_mocs_fini(struct live_mocs *arg)
{
	i915_vma_unpin_and_release(&arg->scratch, I915_VMA_RELEASE_MAP);
}

static int read_regs(struct i915_request *rq,
		     u32 addr, unsigned int count,
		     u32 *offset)
{
	unsigned int i;
	u32 *cs;

	GEM_BUG_ON(!IS_ALIGNED(*offset, sizeof(u32)));

	cs = intel_ring_begin(rq, 4 * count);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	for (i = 0; i < count; i++) {
		*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
		*cs++ = addr;
		*cs++ = *offset;
		*cs++ = 0;

		addr += sizeof(u32);
		*offset += sizeof(u32);
	}

	intel_ring_advance(rq, cs);

	return 0;
}

static int read_mocs_table(struct i915_request *rq,
			   const struct drm_i915_mocs_table *table,
			   u32 *offset)
{
	u32 addr;

	if (!table)
		return 0;

	if (HAS_GLOBAL_MOCS_REGISTERS(rq->engine->i915))
		addr = global_mocs_offset();
	else
		addr = mocs_offset(rq->engine);

	return read_regs(rq, addr, table->n_entries, offset);
}

static int read_l3cc_table(struct i915_request *rq,
			   const struct drm_i915_mocs_table *table,
			   u32 *offset)
{
	u32 addr = i915_mmio_reg_offset(GEN9_LNCFCMOCS(0));

	if (!table)
		return 0;

	return read_regs(rq, addr, (table->n_entries + 1) / 2, offset);
}

static int check_mocs_table(struct intel_engine_cs *engine,
			    const struct drm_i915_mocs_table *table,
			    u32 **vaddr)
{
	unsigned int i;
	u32 expect;

	if (!table)
		return 0;

	for_each_mocs(expect, table, i) {
		if (**vaddr != expect) {
			pr_err("%s: Invalid MOCS[%d] entry, found %08x, expected %08x\n",
			       engine->name, i, **vaddr, expect);
			return -EINVAL;
		}
		++*vaddr;
	}

	return 0;
}

static bool mcr_range(struct drm_i915_private *i915, u32 offset)
{
	/*
	 * Registers in this range are affected by the MCR selector
	 * which only controls CPU initiated MMIO. Routing does not
	 * work for CS access so we cannot verify them on this path.
	 */
	return GRAPHICS_VER(i915) >= 8 && offset >= 0xb000 && offset <= 0xb4ff;
}

static int check_l3cc_table(struct intel_engine_cs *engine,
			    const struct drm_i915_mocs_table *table,
			    u32 **vaddr)
{
	/* Can we read the MCR range 0xb00 directly? See intel_workarounds! */
	u32 reg = i915_mmio_reg_offset(GEN9_LNCFCMOCS(0));
	unsigned int i;
	u32 expect;

	if (!table)
		return 0;

	for_each_l3cc(expect, table, i) {
		if (!mcr_range(engine->i915, reg) && **vaddr != expect) {
			pr_err("%s: Invalid L3CC[%d] entry, found %08x, expected %08x\n",
			       engine->name, i, **vaddr, expect);
			return -EINVAL;
		}
		++*vaddr;
		reg += 4;
	}

	return 0;
}

static int check_mocs_engine(struct live_mocs *arg,
			     struct intel_context *ce)
{
	struct i915_vma *vma = arg->scratch;
	struct i915_request *rq;
	u32 offset;
	u32 *vaddr;
	int err;

	memset32(arg->vaddr, STACK_MAGIC, PAGE_SIZE / sizeof(u32));

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, true);
	if (!err)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);

	/* Read the mocs tables back using SRM */
	offset = i915_ggtt_offset(vma);
	if (!err)
		err = read_mocs_table(rq, arg->mocs, &offset);
	if (!err && ce->engine->class == RENDER_CLASS)
		err = read_l3cc_table(rq, arg->l3cc, &offset);
	offset -= i915_ggtt_offset(vma);
	GEM_BUG_ON(offset > PAGE_SIZE);

	err = request_add_sync(rq, err);
	if (err)
		return err;

	/* Compare the results against the expected tables */
	vaddr = arg->vaddr;
	if (!err)
		err = check_mocs_table(ce->engine, arg->mocs, &vaddr);
	if (!err && ce->engine->class == RENDER_CLASS)
		err = check_l3cc_table(ce->engine, arg->l3cc, &vaddr);
	if (err)
		return err;

	GEM_BUG_ON(arg->vaddr + offset != vaddr);
	return 0;
}

static int live_mocs_kernel(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct live_mocs mocs;
	int err;

	/* Basic check the system is configured with the expected mocs table */

	err = live_mocs_init(&mocs, gt);
	if (err)
		return err;

	for_each_engine(engine, gt, id) {
		intel_engine_pm_get(engine);
		err = check_mocs_engine(&mocs, engine->kernel_context);
		intel_engine_pm_put(engine);
		if (err)
			break;
	}

	live_mocs_fini(&mocs);
	return err;
}

static int live_mocs_clean(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct live_mocs mocs;
	int err;

	/* Every new context should see the same mocs table */

	err = live_mocs_init(&mocs, gt);
	if (err)
		return err;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;

		ce = mocs_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		err = check_mocs_engine(&mocs, ce);
		intel_context_put(ce);
		if (err)
			break;
	}

	live_mocs_fini(&mocs);
	return err;
}

static int active_engine_reset(struct intel_context *ce,
			       const char *reason,
			       bool using_guc)
{
	struct igt_spinner spin;
	struct i915_request *rq;
	int err;

	err = igt_spinner_init(&spin, ce->engine->gt);
	if (err)
		return err;

	rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
	if (IS_ERR(rq)) {
		igt_spinner_fini(&spin);
		return PTR_ERR(rq);
	}

	err = request_add_spin(rq, &spin);
	if (err == 0 && !using_guc)
		err = intel_engine_reset(ce->engine, reason);

	/* Ensure the reset happens and kills the engine */
	if (err == 0)
		err = intel_selftest_wait_for_rq(rq);

	igt_spinner_end(&spin);
	igt_spinner_fini(&spin);

	return err;
}

static int __live_mocs_reset(struct live_mocs *mocs,
			     struct intel_context *ce, bool using_guc)
{
	struct intel_gt *gt = ce->engine->gt;
	int err;

	if (intel_has_reset_engine(gt)) {
		if (!using_guc) {
			err = intel_engine_reset(ce->engine, "mocs");
			if (err)
				return err;

			err = check_mocs_engine(mocs, ce);
			if (err)
				return err;
		}

		err = active_engine_reset(ce, "mocs", using_guc);
		if (err)
			return err;

		err = check_mocs_engine(mocs, ce);
		if (err)
			return err;
	}

	if (intel_has_gpu_reset(gt)) {
		intel_gt_reset(gt, ce->engine->mask, "mocs");

		err = check_mocs_engine(mocs, ce);
		if (err)
			return err;
	}

	return 0;
}

static int live_mocs_reset(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct live_mocs mocs;
	int err = 0;

	/* Check the mocs setup is retained over per-engine and global resets */

	err = live_mocs_init(&mocs, gt);
	if (err)
		return err;

	igt_global_reset_lock(gt);
	for_each_engine(engine, gt, id) {
		bool using_guc = intel_engine_uses_guc(engine);
		struct intel_selftest_saved_policy saved;
		struct intel_context *ce;
		int err2;

		err = intel_selftest_modify_policy(engine, &saved,
						   SELFTEST_SCHEDULER_MODIFY_FAST_RESET);
		if (err)
			break;

		ce = mocs_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto restore;
		}

		intel_engine_pm_get(engine);

		err = __live_mocs_reset(&mocs, ce, using_guc);

		intel_engine_pm_put(engine);
		intel_context_put(ce);

restore:
		err2 = intel_selftest_restore_policy(engine, &saved);
		if (err == 0)
			err = err2;
		if (err)
			break;
	}
	igt_global_reset_unlock(gt);

	live_mocs_fini(&mocs);
	return err;
}

int intel_mocs_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_mocs_kernel),
		SUBTEST(live_mocs_clean),
		SUBTEST(live_mocs_reset),
	};
	struct drm_i915_mocs_table table;

	if (!get_mocs_settings(i915, &table))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
