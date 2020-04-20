// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/sort.h>

#include "intel_engine_pm.h"
#include "intel_gpu_commands.h"
#include "intel_gt_pm.h"
#include "intel_rc6.h"
#include "selftest_rps.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_spinner.h"
#include "selftests/librapl.h"

static void dummy_rps_work(struct work_struct *wrk)
{
}

static int cmp_u64(const void *A, const void *B)
{
	const u64 *a = A, *b = B;

	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static struct i915_vma *
create_spin_counter(struct intel_engine_cs *engine,
		    struct i915_address_space *vm,
		    u32 **cancel,
		    u32 **counter)
{
	enum {
		COUNT,
		INC,
		__NGPR__,
	};
#define CS_GPR(x) GEN8_RING_CS_GPR(engine->mmio_base, x)
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *base, *cs;
	int loop, i;
	int err;

	obj = i915_gem_object_create_internal(vm->i915, 4096);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err) {
		i915_vma_put(vma);
		return ERR_PTR(err);
	}

	base = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(base)) {
		i915_gem_object_put(obj);
		return ERR_CAST(base);
	}
	cs = base;

	*cs++ = MI_LOAD_REGISTER_IMM(__NGPR__ * 2);
	for (i = 0; i < __NGPR__; i++) {
		*cs++ = i915_mmio_reg_offset(CS_GPR(i));
		*cs++ = 0;
		*cs++ = i915_mmio_reg_offset(CS_GPR(i)) + 4;
		*cs++ = 0;
	}

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(CS_GPR(INC));
	*cs++ = 1;

	loop = cs - base;

	*cs++ = MI_MATH(4);
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(COUNT));
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(INC));
	*cs++ = MI_MATH_ADD;
	*cs++ = MI_MATH_STORE(MI_MATH_REG(COUNT), MI_MATH_REG_ACCU);

	*cs++ = MI_STORE_REGISTER_MEM_GEN8;
	*cs++ = i915_mmio_reg_offset(CS_GPR(COUNT));
	*cs++ = lower_32_bits(vma->node.start + 1000 * sizeof(*cs));
	*cs++ = upper_32_bits(vma->node.start + 1000 * sizeof(*cs));

	*cs++ = MI_BATCH_BUFFER_START_GEN8;
	*cs++ = lower_32_bits(vma->node.start + loop * sizeof(*cs));
	*cs++ = upper_32_bits(vma->node.start + loop * sizeof(*cs));

	i915_gem_object_flush_map(obj);

	*cancel = base + loop;
	*counter = memset32(base + 1000, 0, 1);
	return vma;
}

static u64 __measure_frequency(u32 *cntr, int duration_ms)
{
	u64 dc, dt;

	dt = ktime_get();
	dc = READ_ONCE(*cntr);
	usleep_range(1000 * duration_ms, 2000 * duration_ms);
	dc = READ_ONCE(*cntr) - dc;
	dt = ktime_get() - dt;

	return div64_u64(1000 * 1000 * dc, dt);
}

static u64 measure_frequency_at(struct intel_rps *rps, u32 *cntr, int *freq)
{
	u64 x[5];
	int i;

	mutex_lock(&rps->lock);
	GEM_BUG_ON(!rps->active);
	intel_rps_set(rps, *freq);
	mutex_unlock(&rps->lock);

	msleep(20); /* more than enough time to stabilise! */

	for (i = 0; i < 5; i++)
		x[i] = __measure_frequency(cntr, 2);
	*freq = read_cagf(rps);

	/* A simple triangle filter for better result stability */
	sort(x, 5, sizeof(*x), cmp_u64, NULL);
	return div_u64(x[1] + 2 * x[2] + x[3], 4);
}

static bool scaled_within(u64 x, u64 y, u32 f_n, u32 f_d)
{
	return f_d * x > f_n * y && f_n * x < f_d * y;
}

int live_rps_frequency(void *arg)
{
	void (*saved_work)(struct work_struct *wrk);
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * The premise is that the GPU does change freqency at our behest.
	 * Let's check there is a correspondence between the requested
	 * frequency, the actual frequency, and the observed clock rate.
	 */

	if (!rps->enabled || rps->max_freq <= rps->min_freq)
		return 0;

	if (INTEL_GEN(gt->i915) < 8) /* for CS simplicity */
		return 0;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		struct i915_vma *vma;
		u32 *cancel, *cntr;
		struct {
			u64 count;
			int freq;
		} min, max;

		vma = create_spin_counter(engine,
					  engine->kernel_context->vm,
					  &cancel, &cntr);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			break;
		}

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_vma;
		}

		i915_vma_lock(vma);
		err = i915_request_await_object(rq, vma->obj, false);
		if (!err)
			err = i915_vma_move_to_active(vma, rq, 0);
		if (!err)
			err = rq->engine->emit_bb_start(rq,
							vma->node.start,
							PAGE_SIZE, 0);
		i915_vma_unlock(vma);
		i915_request_add(rq);
		if (err)
			goto err_vma;

		if (wait_for(READ_ONCE(*cntr), 10)) {
			pr_err("%s: timed loop did not start\n",
			       engine->name);
			goto err_vma;
		}

		min.freq = rps->min_freq;
		min.count = measure_frequency_at(rps, cntr, &min.freq);

		max.freq = rps->max_freq;
		max.count = measure_frequency_at(rps, cntr, &max.freq);

		pr_info("%s: min:%lluKHz @ %uMHz, max:%lluKHz @ %uMHz [%d%%]\n",
			engine->name,
			min.count, intel_gpu_freq(rps, min.freq),
			max.count, intel_gpu_freq(rps, max.freq),
			(int)DIV64_U64_ROUND_CLOSEST(100 * min.freq * max.count,
						     max.freq * min.count));

		if (!scaled_within(max.freq * min.count,
				   min.freq * max.count,
				   1, 2)) {
			pr_err("%s: CS did not scale with frequency! scaled min:%llu, max:%llu\n",
			       engine->name,
			       max.freq * min.count,
			       min.freq * max.count);
			err = -EINVAL;
		}

err_vma:
		*cancel = MI_BATCH_BUFFER_END;
		i915_gem_object_unpin_map(vma->obj);
		i915_vma_unpin(vma);
		i915_vma_put(vma);

		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	return err;
}

static void sleep_for_ei(struct intel_rps *rps, int timeout_us)
{
	/* Flush any previous EI */
	usleep_range(timeout_us, 2 * timeout_us);

	/* Reset the interrupt status */
	rps_disable_interrupts(rps);
	GEM_BUG_ON(rps->pm_iir);
	rps_enable_interrupts(rps);

	/* And then wait for the timeout, for real this time */
	usleep_range(2 * timeout_us, 3 * timeout_us);
}

static int __rps_up_interrupt(struct intel_rps *rps,
			      struct intel_engine_cs *engine,
			      struct igt_spinner *spin)
{
	struct intel_uncore *uncore = engine->uncore;
	struct i915_request *rq;
	u32 timeout;

	if (!intel_engine_can_store_dword(engine))
		return 0;

	mutex_lock(&rps->lock);
	GEM_BUG_ON(!rps->active);
	intel_rps_set(rps, rps->min_freq);
	mutex_unlock(&rps->lock);

	rq = igt_spinner_create_request(spin, engine->kernel_context, MI_NOOP);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_request_get(rq);
	i915_request_add(rq);

	if (!igt_wait_for_spinner(spin, rq)) {
		pr_err("%s: RPS spinner did not start\n",
		       engine->name);
		i915_request_put(rq);
		intel_gt_set_wedged(engine->gt);
		return -EIO;
	}

	if (!rps->active) {
		pr_err("%s: RPS not enabled on starting spinner\n",
		       engine->name);
		igt_spinner_end(spin);
		i915_request_put(rq);
		return -EINVAL;
	}

	if (!(rps->pm_events & GEN6_PM_RP_UP_THRESHOLD)) {
		pr_err("%s: RPS did not register UP interrupt\n",
		       engine->name);
		i915_request_put(rq);
		return -EINVAL;
	}

	if (rps->last_freq != rps->min_freq) {
		pr_err("%s: RPS did not program min frequency\n",
		       engine->name);
		i915_request_put(rq);
		return -EINVAL;
	}

	timeout = intel_uncore_read(uncore, GEN6_RP_UP_EI);
	timeout = GT_PM_INTERVAL_TO_US(engine->i915, timeout);

	sleep_for_ei(rps, timeout);
	GEM_BUG_ON(i915_request_completed(rq));

	igt_spinner_end(spin);
	i915_request_put(rq);

	if (rps->cur_freq != rps->min_freq) {
		pr_err("%s: Frequency unexpectedly changed [up], now %d!\n",
		       engine->name, intel_rps_read_actual_frequency(rps));
		return -EINVAL;
	}

	if (!(rps->pm_iir & GEN6_PM_RP_UP_THRESHOLD)) {
		pr_err("%s: UP interrupt not recorded for spinner, pm_iir:%x, prev_up:%x, up_threshold:%x, up_ei:%x\n",
		       engine->name, rps->pm_iir,
		       intel_uncore_read(uncore, GEN6_RP_PREV_UP),
		       intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_UP_EI));
		return -EINVAL;
	}

	return 0;
}

static int __rps_down_interrupt(struct intel_rps *rps,
				struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	u32 timeout;

	mutex_lock(&rps->lock);
	GEM_BUG_ON(!rps->active);
	intel_rps_set(rps, rps->max_freq);
	mutex_unlock(&rps->lock);

	if (!(rps->pm_events & GEN6_PM_RP_DOWN_THRESHOLD)) {
		pr_err("%s: RPS did not register DOWN interrupt\n",
		       engine->name);
		return -EINVAL;
	}

	if (rps->last_freq != rps->max_freq) {
		pr_err("%s: RPS did not program max frequency\n",
		       engine->name);
		return -EINVAL;
	}

	timeout = intel_uncore_read(uncore, GEN6_RP_DOWN_EI);
	timeout = GT_PM_INTERVAL_TO_US(engine->i915, timeout);

	sleep_for_ei(rps, timeout);

	if (rps->cur_freq != rps->max_freq) {
		pr_err("%s: Frequency unexpectedly changed [down], now %d!\n",
		       engine->name,
		       intel_rps_read_actual_frequency(rps));
		return -EINVAL;
	}

	if (!(rps->pm_iir & (GEN6_PM_RP_DOWN_THRESHOLD | GEN6_PM_RP_DOWN_TIMEOUT))) {
		pr_err("%s: DOWN interrupt not recorded for idle, pm_iir:%x, prev_down:%x, down_threshold:%x, down_ei:%x [prev_up:%x, up_threshold:%x, up_ei:%x]\n",
		       engine->name, rps->pm_iir,
		       intel_uncore_read(uncore, GEN6_RP_PREV_DOWN),
		       intel_uncore_read(uncore, GEN6_RP_DOWN_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_DOWN_EI),
		       intel_uncore_read(uncore, GEN6_RP_PREV_UP),
		       intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_UP_EI));
		return -EINVAL;
	}

	return 0;
}

int live_rps_interrupt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	void (*saved_work)(struct work_struct *wrk);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	u32 pm_events;
	int err = 0;

	/*
	 * First, let's check whether or not we are receiving interrupts.
	 */

	if (!rps->enabled || rps->max_freq <= rps->min_freq)
		return 0;

	intel_gt_pm_get(gt);
	pm_events = rps->pm_events;
	intel_gt_pm_put(gt);
	if (!pm_events) {
		pr_err("No RPS PM events registered, but RPS is enabled?\n");
		return -ENODEV;
	}

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	for_each_engine(engine, gt, id) {
		/* Keep the engine busy with a spinner; expect an UP! */
		if (pm_events & GEN6_PM_RP_UP_THRESHOLD) {
			intel_gt_pm_wait_for_idle(engine->gt);
			GEM_BUG_ON(rps->active);

			intel_engine_pm_get(engine);
			err = __rps_up_interrupt(rps, engine, &spin);
			intel_engine_pm_put(engine);
			if (err)
				goto out;

			intel_gt_pm_wait_for_idle(engine->gt);
		}

		/* Keep the engine awake but idle and check for DOWN */
		if (pm_events & GEN6_PM_RP_DOWN_THRESHOLD) {
			intel_engine_pm_get(engine);
			intel_rc6_disable(&gt->rc6);

			err = __rps_down_interrupt(rps, engine);

			intel_rc6_enable(&gt->rc6);
			intel_engine_pm_put(engine);
			if (err)
				goto out;
		}
	}

out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	igt_spinner_fini(&spin);

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	return err;
}

static u64 __measure_power(int duration_ms)
{
	u64 dE, dt;

	dt = ktime_get();
	dE = librapl_energy_uJ();
	usleep_range(1000 * duration_ms, 2000 * duration_ms);
	dE = librapl_energy_uJ() - dE;
	dt = ktime_get() - dt;

	return div64_u64(1000 * 1000 * dE, dt);
}

static u64 measure_power_at(struct intel_rps *rps, int freq)
{
	u64 x[5];
	int i;

	mutex_lock(&rps->lock);
	GEM_BUG_ON(!rps->active);
	intel_rps_set(rps, freq);
	mutex_unlock(&rps->lock);

	msleep(20); /* more than enough time to stabilise! */

	i = read_cagf(rps);
	if (i != freq)
		pr_notice("Running at %x [%uMHz], not target %x [%uMHz]\n",
			  i, intel_gpu_freq(rps, i),
			  freq, intel_gpu_freq(rps, freq));

	for (i = 0; i < 5; i++)
		x[i] = __measure_power(5);

	/* A simple triangle filter for better result stability */
	sort(x, 5, sizeof(*x), cmp_u64, NULL);
	return div_u64(x[1] + 2 * x[2] + x[3], 4);
}

int live_rps_power(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	void (*saved_work)(struct work_struct *wrk);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * Our fundamental assumption is that running at lower frequency
	 * actually saves power. Let's see if our RAPL measurement support
	 * that theory.
	 */

	if (!rps->enabled || rps->max_freq <= rps->min_freq)
		return 0;

	if (!librapl_energy_uJ())
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		u64 min, max;

		if (!intel_engine_can_store_dword(engine))
			continue;

		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			pr_err("%s: RPS spinner did not start\n",
			       engine->name);
			intel_gt_set_wedged(engine->gt);
			err = -EIO;
			break;
		}

		max = measure_power_at(rps, rps->max_freq);
		min = measure_power_at(rps, rps->min_freq);

		igt_spinner_end(&spin);

		pr_info("%s: min:%llumW @ %uMHz, max:%llumW @ %uMHz\n",
			engine->name,
			min, intel_gpu_freq(rps, rps->min_freq),
			max, intel_gpu_freq(rps, rps->max_freq));
		if (11 * min > 10 * max) {
			pr_err("%s: did not conserve power when setting lower frequency!\n",
			       engine->name);
			err = -EINVAL;
			break;
		}

		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			break;
		}
	}

	igt_spinner_fini(&spin);

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	return err;
}
