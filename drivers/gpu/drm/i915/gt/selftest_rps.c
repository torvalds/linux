// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/pm_qos.h>
#include <linux/sort.h>

#include "gem/i915_gem_internal.h"

#include "i915_reg.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_engine_regs.h"
#include "intel_gpu_commands.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_pm.h"
#include "intel_rc6.h"
#include "selftest_engine_heartbeat.h"
#include "selftest_rps.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_spinner.h"
#include "selftests/librapl.h"

/* Try to isolate the impact of cstates from determing frequency response */
#define CPU_LATENCY 0 /* -1 to disable pm_qos, 0 to disable cstates */

static void dummy_rps_work(struct work_struct *wrk)
{
}

static int cmp_u64(const void *A, const void *B)
{
	const u64 *a = A, *b = B;

	if (*a < *b)
		return -1;
	else if (*a > *b)
		return 1;
	else
		return 0;
}

static int cmp_u32(const void *A, const void *B)
{
	const u32 *a = A, *b = B;

	if (*a < *b)
		return -1;
	else if (*a > *b)
		return 1;
	else
		return 0;
}

static struct i915_vma *
create_spin_counter(struct intel_engine_cs *engine,
		    struct i915_address_space *vm,
		    bool srm,
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
	unsigned long end;
	u32 *base, *cs;
	int loop, i;
	int err;

	obj = i915_gem_object_create_internal(vm->i915, 64 << 10);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	end = obj->base.size / sizeof(u32) - 1;

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_put;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err_unlock;

	i915_vma_lock(vma);

	base = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(base)) {
		err = PTR_ERR(base);
		goto err_unpin;
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

	/* Unroll the loop to avoid MI_BB_START stalls impacting measurements */
	for (i = 0; i < 1024; i++) {
		*cs++ = MI_MATH(4);
		*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(COUNT));
		*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(INC));
		*cs++ = MI_MATH_ADD;
		*cs++ = MI_MATH_STORE(MI_MATH_REG(COUNT), MI_MATH_REG_ACCU);

		if (srm) {
			*cs++ = MI_STORE_REGISTER_MEM_GEN8;
			*cs++ = i915_mmio_reg_offset(CS_GPR(COUNT));
			*cs++ = lower_32_bits(i915_vma_offset(vma) + end * sizeof(*cs));
			*cs++ = upper_32_bits(i915_vma_offset(vma) + end * sizeof(*cs));
		}
	}

	*cs++ = MI_BATCH_BUFFER_START_GEN8;
	*cs++ = lower_32_bits(i915_vma_offset(vma) + loop * sizeof(*cs));
	*cs++ = upper_32_bits(i915_vma_offset(vma) + loop * sizeof(*cs));
	GEM_BUG_ON(cs - base > end);

	i915_gem_object_flush_map(obj);

	*cancel = base + loop;
	*counter = srm ? memset32(base + end, 0, 1) : NULL;
	return vma;

err_unpin:
	i915_vma_unpin(vma);
err_unlock:
	i915_vma_unlock(vma);
err_put:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static u8 wait_for_freq(struct intel_rps *rps, u8 freq, int timeout_ms)
{
	u8 history[64], i;
	unsigned long end;
	int sleep;

	i = 0;
	memset(history, freq, sizeof(history));
	sleep = 20;

	/* The PCU does not change instantly, but drifts towards the goal? */
	end = jiffies + msecs_to_jiffies(timeout_ms);
	do {
		u8 act;

		act = read_cagf(rps);
		if (time_after(jiffies, end))
			return act;

		/* Target acquired */
		if (act == freq)
			return act;

		/* Any change within the last N samples? */
		if (!memchr_inv(history, act, sizeof(history)))
			return act;

		history[i] = act;
		i = (i + 1) % ARRAY_SIZE(history);

		usleep_range(sleep, 2 * sleep);
		sleep *= 2;
		if (sleep > timeout_ms * 20)
			sleep = timeout_ms * 20;
	} while (1);
}

static u8 rps_set_check(struct intel_rps *rps, u8 freq)
{
	mutex_lock(&rps->lock);
	GEM_BUG_ON(!intel_rps_is_active(rps));
	if (wait_for(!intel_rps_set(rps, freq), 50)) {
		mutex_unlock(&rps->lock);
		return 0;
	}
	GEM_BUG_ON(rps->last_freq != freq);
	mutex_unlock(&rps->lock);

	return wait_for_freq(rps, freq, 50);
}

static void show_pstate_limits(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (IS_BROXTON(i915)) {
		pr_info("P_STATE_CAP[%x]: 0x%08x\n",
			i915_mmio_reg_offset(BXT_RP_STATE_CAP),
			intel_uncore_read(rps_to_uncore(rps),
					  BXT_RP_STATE_CAP));
	} else if (GRAPHICS_VER(i915) == 9) {
		pr_info("P_STATE_LIMITS[%x]: 0x%08x\n",
			i915_mmio_reg_offset(GEN9_RP_STATE_LIMITS),
			intel_uncore_read(rps_to_uncore(rps),
					  GEN9_RP_STATE_LIMITS));
	}
}

int live_rps_clock_interval(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	void (*saved_work)(struct work_struct *wrk);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err = 0;

	if (!intel_rps_is_enabled(rps) || GRAPHICS_VER(gt->i915) < 6)
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	wakeref = intel_gt_pm_get(gt);
	intel_rps_disable(&gt->rps);

	intel_gt_check_clock_frequency(gt);

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		u32 cycles;
		u64 dt;

		if (!intel_engine_can_store_dword(engine))
			continue;

		st_engine_heartbeat_disable(engine);

		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			st_engine_heartbeat_enable(engine);
			err = PTR_ERR(rq);
			break;
		}

		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			pr_err("%s: RPS spinner did not start\n",
			       engine->name);
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			intel_gt_set_wedged(engine->gt);
			err = -EIO;
			break;
		}

		intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);

		intel_uncore_write_fw(gt->uncore, GEN6_RP_CUR_UP_EI, 0);

		/* Set the evaluation interval to infinity! */
		intel_uncore_write_fw(gt->uncore,
				      GEN6_RP_UP_EI, 0xffffffff);
		intel_uncore_write_fw(gt->uncore,
				      GEN6_RP_UP_THRESHOLD, 0xffffffff);

		intel_uncore_write_fw(gt->uncore, GEN6_RP_CONTROL,
				      GEN6_RP_ENABLE | GEN6_RP_UP_BUSY_AVG);

		if (wait_for(intel_uncore_read_fw(gt->uncore,
						  GEN6_RP_CUR_UP_EI),
			     10)) {
			/* Just skip the test; assume lack of HW support */
			pr_notice("%s: rps evaluation interval not ticking\n",
				  engine->name);
			err = -ENODEV;
		} else {
			ktime_t dt_[5];
			u32 cycles_[5];
			int i;

			for (i = 0; i < 5; i++) {
				preempt_disable();

				cycles_[i] = -intel_uncore_read_fw(gt->uncore, GEN6_RP_CUR_UP_EI);
				dt_[i] = ktime_get();

				udelay(1000);

				cycles_[i] += intel_uncore_read_fw(gt->uncore, GEN6_RP_CUR_UP_EI);
				dt_[i] = ktime_sub(ktime_get(), dt_[i]);

				preempt_enable();
			}

			/* Use the median of both cycle/dt; close enough */
			sort(cycles_, 5, sizeof(*cycles_), cmp_u32, NULL);
			cycles = (cycles_[1] + 2 * cycles_[2] + cycles_[3]) / 4;
			sort(dt_, 5, sizeof(*dt_), cmp_u64, NULL);
			dt = div_u64(dt_[1] + 2 * dt_[2] + dt_[3], 4);
		}

		intel_uncore_write_fw(gt->uncore, GEN6_RP_CONTROL, 0);
		intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);

		igt_spinner_end(&spin);
		st_engine_heartbeat_enable(engine);

		if (err == 0) {
			u64 time = intel_gt_pm_interval_to_ns(gt, cycles);
			u32 expected =
				intel_gt_ns_to_pm_interval(gt, dt);

			pr_info("%s: rps counted %d C0 cycles [%lldns] in %lldns [%d cycles], using GT clock frequency of %uKHz\n",
				engine->name, cycles, time, dt, expected,
				gt->clock_frequency / 1000);

			if (10 * time < 8 * dt ||
			    8 * time > 10 * dt) {
				pr_err("%s: rps clock time does not match walltime!\n",
				       engine->name);
				err = -EINVAL;
			}

			if (10 * expected < 8 * cycles ||
			    8 * expected > 10 * cycles) {
				pr_err("%s: walltime does not match rps clock ticks!\n",
				       engine->name);
				err = -EINVAL;
			}
		}

		if (igt_flush_test(gt->i915))
			err = -EIO;

		break; /* once is enough */
	}

	intel_rps_enable(&gt->rps);
	intel_gt_pm_put(gt, wakeref);

	igt_spinner_fini(&spin);

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	if (err == -ENODEV) /* skipped, don't report a fail */
		err = 0;

	return err;
}

int live_rps_control(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	void (*saved_work)(struct work_struct *wrk);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err = 0;

	/*
	 * Check that the actual frequency matches our requested frequency,
	 * to verify our control mechanism. We have to be careful that the
	 * PCU may throttle the GPU in which case the actual frequency used
	 * will be lowered than requested.
	 */

	if (!intel_rps_is_enabled(rps))
		return 0;

	if (IS_CHERRYVIEW(gt->i915)) /* XXX fragile PCU */
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	wakeref = intel_gt_pm_get(gt);
	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		ktime_t min_dt, max_dt;
		int f, limit;
		int min, max;

		if (!intel_engine_can_store_dword(engine))
			continue;

		st_engine_heartbeat_disable(engine);

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
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			intel_gt_set_wedged(engine->gt);
			err = -EIO;
			break;
		}

		if (rps_set_check(rps, rps->min_freq) != rps->min_freq) {
			pr_err("%s: could not set minimum frequency [%x], only %x!\n",
			       engine->name, rps->min_freq, read_cagf(rps));
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			show_pstate_limits(rps);
			err = -EINVAL;
			break;
		}

		for (f = rps->min_freq + 1; f < rps->max_freq; f++) {
			if (rps_set_check(rps, f) < f)
				break;
		}

		limit = rps_set_check(rps, f);

		if (rps_set_check(rps, rps->min_freq) != rps->min_freq) {
			pr_err("%s: could not restore minimum frequency [%x], only %x!\n",
			       engine->name, rps->min_freq, read_cagf(rps));
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			show_pstate_limits(rps);
			err = -EINVAL;
			break;
		}

		max_dt = ktime_get();
		max = rps_set_check(rps, limit);
		max_dt = ktime_sub(ktime_get(), max_dt);

		min_dt = ktime_get();
		min = rps_set_check(rps, rps->min_freq);
		min_dt = ktime_sub(ktime_get(), min_dt);

		igt_spinner_end(&spin);
		st_engine_heartbeat_enable(engine);

		pr_info("%s: range:[%x:%uMHz, %x:%uMHz] limit:[%x:%uMHz], %x:%x response %lluns:%lluns\n",
			engine->name,
			rps->min_freq, intel_gpu_freq(rps, rps->min_freq),
			rps->max_freq, intel_gpu_freq(rps, rps->max_freq),
			limit, intel_gpu_freq(rps, limit),
			min, max, ktime_to_ns(min_dt), ktime_to_ns(max_dt));

		if (limit == rps->min_freq) {
			pr_err("%s: GPU throttled to minimum!\n",
			       engine->name);
			show_pstate_limits(rps);
			err = -ENODEV;
			break;
		}

		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			break;
		}
	}
	intel_gt_pm_put(gt, wakeref);

	igt_spinner_fini(&spin);

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	return err;
}

static void show_pcu_config(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	unsigned int max_gpu_freq, min_gpu_freq;
	intel_wakeref_t wakeref;
	int gpu_freq;

	if (!HAS_LLC(i915))
		return;

	min_gpu_freq = rps->min_freq;
	max_gpu_freq = rps->max_freq;
	if (GRAPHICS_VER(i915) >= 9) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq /= GEN9_FREQ_SCALER;
		max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	wakeref = intel_runtime_pm_get(rps_to_uncore(rps)->rpm);

	pr_info("%5s  %5s  %5s\n", "GPU", "eCPU", "eRing");
	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		int ia_freq = gpu_freq;

		snb_pcode_read(rps_to_gt(rps)->uncore, GEN6_PCODE_READ_MIN_FREQ_TABLE,
			       &ia_freq, NULL);

		pr_info("%5d  %5d  %5d\n",
			gpu_freq * 50,
			((ia_freq >> 0) & 0xff) * 100,
			((ia_freq >> 8) & 0xff) * 100);
	}

	intel_runtime_pm_put(rps_to_uncore(rps)->rpm, wakeref);
}

static u64 __measure_frequency(u32 *cntr, int duration_ms)
{
	u64 dc, dt;

	dc = READ_ONCE(*cntr);
	dt = ktime_get();
	usleep_range(1000 * duration_ms, 2000 * duration_ms);
	dc = READ_ONCE(*cntr) - dc;
	dt = ktime_get() - dt;

	return div64_u64(1000 * 1000 * dc, dt);
}

static u64 measure_frequency_at(struct intel_rps *rps, u32 *cntr, int *freq)
{
	u64 x[5];
	int i;

	*freq = rps_set_check(rps, *freq);
	for (i = 0; i < 5; i++)
		x[i] = __measure_frequency(cntr, 2);
	*freq = (*freq + read_cagf(rps)) / 2;

	/* A simple triangle filter for better result stability */
	sort(x, 5, sizeof(*x), cmp_u64, NULL);
	return div_u64(x[1] + 2 * x[2] + x[3], 4);
}

static u64 __measure_cs_frequency(struct intel_engine_cs *engine,
				  int duration_ms)
{
	u64 dc, dt;

	dc = intel_uncore_read_fw(engine->uncore, CS_GPR(0));
	dt = ktime_get();
	usleep_range(1000 * duration_ms, 2000 * duration_ms);
	dc = intel_uncore_read_fw(engine->uncore, CS_GPR(0)) - dc;
	dt = ktime_get() - dt;

	return div64_u64(1000 * 1000 * dc, dt);
}

static u64 measure_cs_frequency_at(struct intel_rps *rps,
				   struct intel_engine_cs *engine,
				   int *freq)
{
	u64 x[5];
	int i;

	*freq = rps_set_check(rps, *freq);
	for (i = 0; i < 5; i++)
		x[i] = __measure_cs_frequency(engine, 2);
	*freq = (*freq + read_cagf(rps)) / 2;

	/* A simple triangle filter for better result stability */
	sort(x, 5, sizeof(*x), cmp_u64, NULL);
	return div_u64(x[1] + 2 * x[2] + x[3], 4);
}

static bool scaled_within(u64 x, u64 y, u32 f_n, u32 f_d)
{
	return f_d * x > f_n * y && f_n * x < f_d * y;
}

int live_rps_frequency_cs(void *arg)
{
	void (*saved_work)(struct work_struct *wrk);
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	struct intel_engine_cs *engine;
	struct pm_qos_request qos;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * The premise is that the GPU does change frequency at our behest.
	 * Let's check there is a correspondence between the requested
	 * frequency, the actual frequency, and the observed clock rate.
	 */

	if (!intel_rps_is_enabled(rps))
		return 0;

	if (GRAPHICS_VER(gt->i915) < 8) /* for CS simplicity */
		return 0;

	if (CPU_LATENCY >= 0)
		cpu_latency_qos_add_request(&qos, CPU_LATENCY);

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

		st_engine_heartbeat_disable(engine);

		vma = create_spin_counter(engine,
					  engine->kernel_context->vm, false,
					  &cancel, &cntr);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			st_engine_heartbeat_enable(engine);
			break;
		}

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_vma;
		}

		err = i915_vma_move_to_active(vma, rq, 0);
		if (!err)
			err = rq->engine->emit_bb_start(rq,
							i915_vma_offset(vma),
							PAGE_SIZE, 0);
		i915_request_add(rq);
		if (err)
			goto err_vma;

		if (wait_for(intel_uncore_read(engine->uncore, CS_GPR(0)),
			     10)) {
			pr_err("%s: timed loop did not start\n",
			       engine->name);
			goto err_vma;
		}

		min.freq = rps->min_freq;
		min.count = measure_cs_frequency_at(rps, engine, &min.freq);

		max.freq = rps->max_freq;
		max.count = measure_cs_frequency_at(rps, engine, &max.freq);

		pr_info("%s: min:%lluKHz @ %uMHz, max:%lluKHz @ %uMHz [%d%%]\n",
			engine->name,
			min.count, intel_gpu_freq(rps, min.freq),
			max.count, intel_gpu_freq(rps, max.freq),
			(int)DIV64_U64_ROUND_CLOSEST(100 * min.freq * max.count,
						     max.freq * min.count));

		if (!scaled_within(max.freq * min.count,
				   min.freq * max.count,
				   2, 3)) {
			int f;

			pr_err("%s: CS did not scale with frequency! scaled min:%llu, max:%llu\n",
			       engine->name,
			       max.freq * min.count,
			       min.freq * max.count);
			show_pcu_config(rps);

			for (f = min.freq + 1; f <= rps->max_freq; f++) {
				int act = f;
				u64 count;

				count = measure_cs_frequency_at(rps, engine, &act);
				if (act < f)
					break;

				pr_info("%s: %x:%uMHz: %lluKHz [%d%%]\n",
					engine->name,
					act, intel_gpu_freq(rps, act), count,
					(int)DIV64_U64_ROUND_CLOSEST(100 * min.freq * count,
								     act * min.count));

				f = act; /* may skip ahead [pcu granularity] */
			}

			err = -EINTR; /* ignore error, continue on with test */
		}

err_vma:
		*cancel = MI_BATCH_BUFFER_END;
		i915_gem_object_flush_map(vma->obj);
		i915_gem_object_unpin_map(vma->obj);
		i915_vma_unpin(vma);
		i915_vma_unlock(vma);
		i915_vma_put(vma);

		st_engine_heartbeat_enable(engine);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	if (CPU_LATENCY >= 0)
		cpu_latency_qos_remove_request(&qos);

	return err;
}

int live_rps_frequency_srm(void *arg)
{
	void (*saved_work)(struct work_struct *wrk);
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	struct intel_engine_cs *engine;
	struct pm_qos_request qos;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * The premise is that the GPU does change frequency at our behest.
	 * Let's check there is a correspondence between the requested
	 * frequency, the actual frequency, and the observed clock rate.
	 */

	if (!intel_rps_is_enabled(rps))
		return 0;

	if (GRAPHICS_VER(gt->i915) < 8) /* for CS simplicity */
		return 0;

	if (CPU_LATENCY >= 0)
		cpu_latency_qos_add_request(&qos, CPU_LATENCY);

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

		st_engine_heartbeat_disable(engine);

		vma = create_spin_counter(engine,
					  engine->kernel_context->vm, true,
					  &cancel, &cntr);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			st_engine_heartbeat_enable(engine);
			break;
		}

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_vma;
		}

		err = i915_vma_move_to_active(vma, rq, 0);
		if (!err)
			err = rq->engine->emit_bb_start(rq,
							i915_vma_offset(vma),
							PAGE_SIZE, 0);
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
			int f;

			pr_err("%s: CS did not scale with frequency! scaled min:%llu, max:%llu\n",
			       engine->name,
			       max.freq * min.count,
			       min.freq * max.count);
			show_pcu_config(rps);

			for (f = min.freq + 1; f <= rps->max_freq; f++) {
				int act = f;
				u64 count;

				count = measure_frequency_at(rps, cntr, &act);
				if (act < f)
					break;

				pr_info("%s: %x:%uMHz: %lluKHz [%d%%]\n",
					engine->name,
					act, intel_gpu_freq(rps, act), count,
					(int)DIV64_U64_ROUND_CLOSEST(100 * min.freq * count,
								     act * min.count));

				f = act; /* may skip ahead [pcu granularity] */
			}

			err = -EINTR; /* ignore error, continue on with test */
		}

err_vma:
		*cancel = MI_BATCH_BUFFER_END;
		i915_gem_object_flush_map(vma->obj);
		i915_gem_object_unpin_map(vma->obj);
		i915_vma_unpin(vma);
		i915_vma_unlock(vma);
		i915_vma_put(vma);

		st_engine_heartbeat_enable(engine);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	if (CPU_LATENCY >= 0)
		cpu_latency_qos_remove_request(&qos);

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

	rps_set_check(rps, rps->min_freq);

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

	if (!intel_rps_is_active(rps)) {
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
	timeout = intel_gt_pm_interval_to_ns(engine->gt, timeout);
	timeout = DIV_ROUND_UP(timeout, 1000);

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

	rps_set_check(rps, rps->max_freq);

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
	timeout = intel_gt_pm_interval_to_ns(engine->gt, timeout);
	timeout = DIV_ROUND_UP(timeout, 1000);

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
	intel_wakeref_t wakeref;
	u32 pm_events;
	int err = 0;

	/*
	 * First, let's check whether or not we are receiving interrupts.
	 */

	if (!intel_rps_has_interrupts(rps) || GRAPHICS_VER(gt->i915) < 6)
		return 0;

	pm_events = 0;
	with_intel_gt_pm(gt, wakeref)
		pm_events = rps->pm_events;
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
			GEM_BUG_ON(intel_rps_is_active(rps));

			st_engine_heartbeat_disable(engine);

			err = __rps_up_interrupt(rps, engine, &spin);

			st_engine_heartbeat_enable(engine);
			if (err)
				goto out;

			intel_gt_pm_wait_for_idle(engine->gt);
		}

		/* Keep the engine awake but idle and check for DOWN */
		if (pm_events & GEN6_PM_RP_DOWN_THRESHOLD) {
			st_engine_heartbeat_disable(engine);
			intel_rc6_disable(&gt->rc6);

			err = __rps_down_interrupt(rps, engine);

			intel_rc6_enable(&gt->rc6);
			st_engine_heartbeat_enable(engine);
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

	dE = librapl_energy_uJ();
	dt = ktime_get();
	usleep_range(1000 * duration_ms, 2000 * duration_ms);
	dE = librapl_energy_uJ() - dE;
	dt = ktime_get() - dt;

	return div64_u64(1000 * 1000 * dE, dt);
}

static u64 measure_power(struct intel_rps *rps, int *freq)
{
	u64 x[5];
	int i;

	for (i = 0; i < 5; i++)
		x[i] = __measure_power(5);

	*freq = (*freq + intel_rps_read_actual_frequency(rps)) / 2;

	/* A simple triangle filter for better result stability */
	sort(x, 5, sizeof(*x), cmp_u64, NULL);
	return div_u64(x[1] + 2 * x[2] + x[3], 4);
}

static u64 measure_power_at(struct intel_rps *rps, int *freq)
{
	*freq = rps_set_check(rps, *freq);
	msleep(100);
	return measure_power(rps, freq);
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

	if (!intel_rps_is_enabled(rps) || GRAPHICS_VER(gt->i915) < 6)
		return 0;

	if (!librapl_supported(gt->i915))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		struct {
			u64 power;
			int freq;
		} min, max;

		if (!intel_engine_can_store_dword(engine))
			continue;

		st_engine_heartbeat_disable(engine);

		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			st_engine_heartbeat_enable(engine);
			err = PTR_ERR(rq);
			break;
		}

		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			pr_err("%s: RPS spinner did not start\n",
			       engine->name);
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			intel_gt_set_wedged(engine->gt);
			err = -EIO;
			break;
		}

		max.freq = rps->max_freq;
		max.power = measure_power_at(rps, &max.freq);

		min.freq = rps->min_freq;
		min.power = measure_power_at(rps, &min.freq);

		igt_spinner_end(&spin);
		st_engine_heartbeat_enable(engine);

		pr_info("%s: min:%llumW @ %uMHz, max:%llumW @ %uMHz\n",
			engine->name,
			min.power, intel_gpu_freq(rps, min.freq),
			max.power, intel_gpu_freq(rps, max.freq));

		if (10 * min.freq >= 9 * max.freq) {
			pr_notice("Could not control frequency, ran at [%d:%uMHz, %d:%uMhz]\n",
				  min.freq, intel_gpu_freq(rps, min.freq),
				  max.freq, intel_gpu_freq(rps, max.freq));
			continue;
		}

		if (11 * min.power > 10 * max.power) {
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

int live_rps_dynamic(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * We've looked at the bascs, and have established that we
	 * can change the clock frequency and that the HW will generate
	 * interrupts based on load. Now we check how we integrate those
	 * moving parts into dynamic reclocking based on load.
	 */

	if (!intel_rps_is_enabled(rps) || GRAPHICS_VER(gt->i915) < 6)
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	if (intel_rps_has_interrupts(rps))
		pr_info("RPS has interrupt support\n");
	if (intel_rps_uses_timer(rps))
		pr_info("RPS has timer support\n");

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		struct {
			ktime_t dt;
			u8 freq;
		} min, max;

		if (!intel_engine_can_store_dword(engine))
			continue;

		intel_gt_pm_wait_for_idle(gt);
		GEM_BUG_ON(intel_rps_is_active(rps));
		rps->cur_freq = rps->min_freq;

		intel_engine_pm_get(engine);
		intel_rc6_disable(&gt->rc6);
		GEM_BUG_ON(rps->last_freq != rps->min_freq);

		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err;
		}

		i915_request_add(rq);

		max.dt = ktime_get();
		max.freq = wait_for_freq(rps, rps->max_freq, 500);
		max.dt = ktime_sub(ktime_get(), max.dt);

		igt_spinner_end(&spin);

		min.dt = ktime_get();
		min.freq = wait_for_freq(rps, rps->min_freq, 2000);
		min.dt = ktime_sub(ktime_get(), min.dt);

		pr_info("%s: dynamically reclocked to %u:%uMHz while busy in %lluns, and %u:%uMHz while idle in %lluns\n",
			engine->name,
			max.freq, intel_gpu_freq(rps, max.freq),
			ktime_to_ns(max.dt),
			min.freq, intel_gpu_freq(rps, min.freq),
			ktime_to_ns(min.dt));
		if (min.freq >= max.freq) {
			pr_err("%s: dynamic reclocking of spinner failed\n!",
			       engine->name);
			err = -EINVAL;
		}

err:
		intel_rc6_enable(&gt->rc6);
		intel_engine_pm_put(engine);

		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	igt_spinner_fini(&spin);

	return err;
}
