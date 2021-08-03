// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/sort.h>

#include "i915_selftest.h"
#include "intel_gpu_commands.h"
#include "intel_gt_clock_utils.h"
#include "selftest_engine.h"
#include "selftest_engine_heartbeat.h"
#include "selftests/igt_atomic.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_spinner.h"

#define COUNT 5

static int cmp_u64(const void *A, const void *B)
{
	const u64 *a = A, *b = B;

	return *a - *b;
}

static u64 trifilter(u64 *a)
{
	sort(a, COUNT, sizeof(*a), cmp_u64, NULL);
	return (a[1] + 2 * a[2] + a[3]) >> 2;
}

static u32 *emit_wait(u32 *cs, u32 offset, int op, u32 value)
{
	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		op;
	*cs++ = value;
	*cs++ = offset;
	*cs++ = 0;

	return cs;
}

static u32 *emit_store(u32 *cs, u32 offset, u32 value)
{
	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = offset;
	*cs++ = 0;
	*cs++ = value;

	return cs;
}

static u32 *emit_srm(u32 *cs, i915_reg_t reg, u32 offset)
{
	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(reg);
	*cs++ = offset;
	*cs++ = 0;

	return cs;
}

static void write_semaphore(u32 *x, u32 value)
{
	WRITE_ONCE(*x, value);
	wmb();
}

static int __measure_timestamps(struct intel_context *ce,
				u64 *dt, u64 *d_ring, u64 *d_ctx)
{
	struct intel_engine_cs *engine = ce->engine;
	u32 *sema = memset32(engine->status_page.addr + 1000, 0, 5);
	u32 offset = i915_ggtt_offset(engine->status_page.vma);
	struct i915_request *rq;
	u32 *cs;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 28);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	/* Signal & wait for start */
	cs = emit_store(cs, offset + 4008, 1);
	cs = emit_wait(cs, offset + 4008, MI_SEMAPHORE_SAD_NEQ_SDD, 1);

	cs = emit_srm(cs, RING_TIMESTAMP(engine->mmio_base), offset + 4000);
	cs = emit_srm(cs, RING_CTX_TIMESTAMP(engine->mmio_base), offset + 4004);

	/* Busy wait */
	cs = emit_wait(cs, offset + 4008, MI_SEMAPHORE_SAD_EQ_SDD, 1);

	cs = emit_srm(cs, RING_TIMESTAMP(engine->mmio_base), offset + 4016);
	cs = emit_srm(cs, RING_CTX_TIMESTAMP(engine->mmio_base), offset + 4012);

	intel_ring_advance(rq, cs);
	i915_request_get(rq);
	i915_request_add(rq);
	intel_engine_flush_submission(engine);

	/* Wait for the request to start executing, that then waits for us */
	while (READ_ONCE(sema[2]) == 0)
		cpu_relax();

	/* Run the request for a 100us, sampling timestamps before/after */
	local_irq_disable();
	write_semaphore(&sema[2], 0);
	while (READ_ONCE(sema[1]) == 0) /* wait for the gpu to catch up */
		cpu_relax();
	*dt = local_clock();
	udelay(100);
	*dt = local_clock() - *dt;
	write_semaphore(&sema[2], 1);
	local_irq_enable();

	if (i915_request_wait(rq, 0, HZ / 2) < 0) {
		i915_request_put(rq);
		return -ETIME;
	}
	i915_request_put(rq);

	pr_debug("%s CTX_TIMESTAMP: [%x, %x], RING_TIMESTAMP: [%x, %x]\n",
		 engine->name, sema[1], sema[3], sema[0], sema[4]);

	*d_ctx = sema[3] - sema[1];
	*d_ring = sema[4] - sema[0];
	return 0;
}

static int __live_engine_timestamps(struct intel_engine_cs *engine)
{
	u64 s_ring[COUNT], s_ctx[COUNT], st[COUNT], d_ring, d_ctx, dt;
	struct intel_context *ce;
	int i, err = 0;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	for (i = 0; i < COUNT; i++) {
		err = __measure_timestamps(ce, &st[i], &s_ring[i], &s_ctx[i]);
		if (err)
			break;
	}
	intel_context_put(ce);
	if (err)
		return err;

	dt = trifilter(st);
	d_ring = trifilter(s_ring);
	d_ctx = trifilter(s_ctx);

	pr_info("%s elapsed:%lldns, CTX_TIMESTAMP:%lldns, RING_TIMESTAMP:%lldns\n",
		engine->name, dt,
		intel_gt_clock_interval_to_ns(engine->gt, d_ctx),
		intel_gt_clock_interval_to_ns(engine->gt, d_ring));

	d_ring = intel_gt_clock_interval_to_ns(engine->gt, d_ring);
	if (3 * dt > 4 * d_ring || 4 * dt < 3 * d_ring) {
		pr_err("%s Mismatch between ring timestamp and walltime!\n",
		       engine->name);
		return -EINVAL;
	}

	d_ring = trifilter(s_ring);
	d_ctx = trifilter(s_ctx);

	d_ctx *= engine->gt->clock_frequency;
	if (IS_ICELAKE(engine->i915))
		d_ring *= 12500000; /* Fixed 80ns for icl ctx timestamp? */
	else
		d_ring *= engine->gt->clock_frequency;

	if (3 * d_ctx > 4 * d_ring || 4 * d_ctx < 3 * d_ring) {
		pr_err("%s Mismatch between ring and context timestamps!\n",
		       engine->name);
		return -EINVAL;
	}

	return 0;
}

static int live_engine_timestamps(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Check that CS_TIMESTAMP / CTX_TIMESTAMP are in sync, i.e. share
	 * the same CS clock.
	 */

	if (GRAPHICS_VER(gt->i915) < 8)
		return 0;

	for_each_engine(engine, gt, id) {
		int err;

		st_engine_heartbeat_disable(engine);
		err = __live_engine_timestamps(engine);
		st_engine_heartbeat_enable(engine);
		if (err)
			return err;
	}

	return 0;
}

static int live_engine_busy_stats(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * Check that if an engine supports busy-stats, they tell the truth.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		ktime_t de, dt;
		ktime_t t[2];

		if (!intel_engine_supports_stats(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (intel_gt_pm_wait_for_idle(gt)) {
			err = -EBUSY;
			break;
		}

		st_engine_heartbeat_disable(engine);

		ENGINE_TRACE(engine, "measuring idle time\n");
		preempt_disable();
		de = intel_engine_get_busy_time(engine, &t[0]);
		udelay(100);
		de = ktime_sub(intel_engine_get_busy_time(engine, &t[1]), de);
		preempt_enable();
		dt = ktime_sub(t[1], t[0]);
		if (de < 0 || de > 10) {
			pr_err("%s: reported %lldns [%d%%] busyness while sleeping [for %lldns]\n",
			       engine->name,
			       de, (int)div64_u64(100 * de, dt), dt);
			GEM_TRACE_DUMP();
			err = -EINVAL;
			goto end;
		}

		/* 100% busy */
		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto end;
		}
		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			intel_gt_set_wedged(engine->gt);
			err = -ETIME;
			goto end;
		}

		ENGINE_TRACE(engine, "measuring busy time\n");
		preempt_disable();
		de = intel_engine_get_busy_time(engine, &t[0]);
		udelay(100);
		de = ktime_sub(intel_engine_get_busy_time(engine, &t[1]), de);
		preempt_enable();
		dt = ktime_sub(t[1], t[0]);
		if (100 * de < 95 * dt || 95 * de > 100 * dt) {
			pr_err("%s: reported %lldns [%d%%] busyness while spinning [for %lldns]\n",
			       engine->name,
			       de, (int)div64_u64(100 * de, dt), dt);
			GEM_TRACE_DUMP();
			err = -EINVAL;
			goto end;
		}

end:
		st_engine_heartbeat_enable(engine);
		igt_spinner_end(&spin);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	if (igt_flush_test(gt->i915))
		err = -EIO;
	return err;
}

static int live_engine_pm(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Check we can call intel_engine_pm_put from any context. No
	 * failures are reported directly, but if we mess up lockdep should
	 * tell us.
	 */
	if (intel_gt_pm_wait_for_idle(gt)) {
		pr_err("Unable to flush GT pm before test\n");
		return -EBUSY;
	}

	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
	for_each_engine(engine, gt, id) {
		const typeof(*igt_atomic_phases) *p;

		for (p = igt_atomic_phases; p->name; p++) {
			/*
			 * Acquisition is always synchronous, except if we
			 * know that the engine is already awake, in which
			 * case we should use intel_engine_pm_get_if_awake()
			 * to atomically grab the wakeref.
			 *
			 * In practice,
			 *    intel_engine_pm_get();
			 *    intel_engine_pm_put();
			 * occurs in one thread, while simultaneously
			 *    intel_engine_pm_get_if_awake();
			 *    intel_engine_pm_put();
			 * occurs from atomic context in another.
			 */
			GEM_BUG_ON(intel_engine_pm_is_awake(engine));
			intel_engine_pm_get(engine);

			p->critical_section_begin();
			if (!intel_engine_pm_get_if_awake(engine))
				pr_err("intel_engine_pm_get_if_awake(%s) failed under %s\n",
				       engine->name, p->name);
			else
				intel_engine_pm_put_async(engine);
			intel_engine_pm_put_async(engine);
			p->critical_section_end();

			intel_engine_pm_flush(engine);

			if (intel_engine_pm_is_awake(engine)) {
				pr_err("%s is still awake after flushing pm\n",
				       engine->name);
				return -EINVAL;
			}

			/* gt wakeref is async (deferred to workqueue) */
			if (intel_gt_pm_wait_for_idle(gt)) {
				pr_err("GT failed to idle\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

int live_engine_pm_selftests(struct intel_gt *gt)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_engine_timestamps),
		SUBTEST(live_engine_busy_stats),
		SUBTEST(live_engine_pm),
	};

	return intel_gt_live_subtests(tests, gt);
}
