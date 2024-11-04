/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#include <linux/pm_runtime.h>

#include "gt/intel_engine.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_rc6.h"
#include "gt/intel_rps.h"

#include "i915_drv.h"
#include "i915_pmu.h"

/* Frequency for the sampling timer for events which need it. */
#define FREQUENCY 200
#define PERIOD max_t(u64, 10000, NSEC_PER_SEC / FREQUENCY)

#define ENGINE_SAMPLE_MASK \
	(BIT(I915_SAMPLE_BUSY) | \
	 BIT(I915_SAMPLE_WAIT) | \
	 BIT(I915_SAMPLE_SEMA))

static cpumask_t i915_pmu_cpumask;
static unsigned int i915_pmu_target_cpu = -1;

static struct i915_pmu *event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct i915_pmu, base);
}

static struct drm_i915_private *pmu_to_i915(struct i915_pmu *pmu)
{
	return container_of(pmu, struct drm_i915_private, pmu);
}

static u8 engine_config_sample(u64 config)
{
	return config & I915_PMU_SAMPLE_MASK;
}

static u8 engine_event_sample(struct perf_event *event)
{
	return engine_config_sample(event->attr.config);
}

static u8 engine_event_class(struct perf_event *event)
{
	return (event->attr.config >> I915_PMU_CLASS_SHIFT) & 0xff;
}

static u8 engine_event_instance(struct perf_event *event)
{
	return (event->attr.config >> I915_PMU_SAMPLE_BITS) & 0xff;
}

static bool is_engine_config(const u64 config)
{
	return config < __I915_PMU_OTHER(0);
}

static unsigned int config_gt_id(const u64 config)
{
	return config >> __I915_PMU_GT_SHIFT;
}

static u64 config_counter(const u64 config)
{
	return config & ~(~0ULL << __I915_PMU_GT_SHIFT);
}

static unsigned int other_bit(const u64 config)
{
	unsigned int val;

	switch (config_counter(config)) {
	case I915_PMU_ACTUAL_FREQUENCY:
		val =  __I915_PMU_ACTUAL_FREQUENCY_ENABLED;
		break;
	case I915_PMU_REQUESTED_FREQUENCY:
		val = __I915_PMU_REQUESTED_FREQUENCY_ENABLED;
		break;
	case I915_PMU_RC6_RESIDENCY:
		val = __I915_PMU_RC6_RESIDENCY_ENABLED;
		break;
	default:
		/*
		 * Events that do not require sampling, or tracking state
		 * transitions between enabled and disabled can be ignored.
		 */
		return -1;
	}

	return I915_ENGINE_SAMPLE_COUNT +
	       config_gt_id(config) * __I915_PMU_TRACKED_EVENT_COUNT +
	       val;
}

static unsigned int config_bit(const u64 config)
{
	if (is_engine_config(config))
		return engine_config_sample(config);
	else
		return other_bit(config);
}

static u32 config_mask(const u64 config)
{
	unsigned int bit = config_bit(config);

	if (__builtin_constant_p(config))
		BUILD_BUG_ON(bit >
			     BITS_PER_TYPE(typeof_member(struct i915_pmu,
							 enable)) - 1);
	else
		WARN_ON_ONCE(bit >
			     BITS_PER_TYPE(typeof_member(struct i915_pmu,
							 enable)) - 1);

	return BIT(config_bit(config));
}

static bool is_engine_event(struct perf_event *event)
{
	return is_engine_config(event->attr.config);
}

static unsigned int event_bit(struct perf_event *event)
{
	return config_bit(event->attr.config);
}

static u32 frequency_enabled_mask(void)
{
	unsigned int i;
	u32 mask = 0;

	for (i = 0; i < I915_PMU_MAX_GT; i++)
		mask |= config_mask(__I915_PMU_ACTUAL_FREQUENCY(i)) |
			config_mask(__I915_PMU_REQUESTED_FREQUENCY(i));

	return mask;
}

static bool pmu_needs_timer(struct i915_pmu *pmu)
{
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	u32 enable;

	/*
	 * Only some counters need the sampling timer.
	 *
	 * We start with a bitmask of all currently enabled events.
	 */
	enable = pmu->enable;

	/*
	 * Mask out all the ones which do not need the timer, or in
	 * other words keep all the ones that could need the timer.
	 */
	enable &= frequency_enabled_mask() | ENGINE_SAMPLE_MASK;

	/*
	 * Also there is software busyness tracking available we do not
	 * need the timer for I915_SAMPLE_BUSY counter.
	 */
	if (i915->caps.scheduler & I915_SCHEDULER_CAP_ENGINE_BUSY_STATS)
		enable &= ~BIT(I915_SAMPLE_BUSY);

	/*
	 * If some bits remain it means we need the sampling timer running.
	 */
	return enable;
}

static u64 __get_rc6(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	u64 val;

	val = intel_rc6_residency_ns(&gt->rc6, INTEL_RC6_RES_RC6);

	if (HAS_RC6p(i915))
		val += intel_rc6_residency_ns(&gt->rc6, INTEL_RC6_RES_RC6p);

	if (HAS_RC6pp(i915))
		val += intel_rc6_residency_ns(&gt->rc6, INTEL_RC6_RES_RC6pp);

	return val;
}

static inline s64 ktime_since_raw(const ktime_t kt)
{
	return ktime_to_ns(ktime_sub(ktime_get_raw(), kt));
}

static u64 read_sample(struct i915_pmu *pmu, unsigned int gt_id, int sample)
{
	return pmu->sample[gt_id][sample].cur;
}

static void
store_sample(struct i915_pmu *pmu, unsigned int gt_id, int sample, u64 val)
{
	pmu->sample[gt_id][sample].cur = val;
}

static void
add_sample_mult(struct i915_pmu *pmu, unsigned int gt_id, int sample, u32 val, u32 mul)
{
	pmu->sample[gt_id][sample].cur += mul_u32_u32(val, mul);
}

static u64 get_rc6(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	const unsigned int gt_id = gt->info.id;
	struct i915_pmu *pmu = &i915->pmu;
	intel_wakeref_t wakeref;
	unsigned long flags;
	u64 val;

	wakeref = intel_gt_pm_get_if_awake(gt);
	if (wakeref) {
		val = __get_rc6(gt);
		intel_gt_pm_put_async(gt, wakeref);
	}

	spin_lock_irqsave(&pmu->lock, flags);

	if (wakeref) {
		store_sample(pmu, gt_id, __I915_SAMPLE_RC6, val);
	} else {
		/*
		 * We think we are runtime suspended.
		 *
		 * Report the delta from when the device was suspended to now,
		 * on top of the last known real value, as the approximated RC6
		 * counter value.
		 */
		val = ktime_since_raw(pmu->sleep_last[gt_id]);
		val += read_sample(pmu, gt_id, __I915_SAMPLE_RC6);
	}

	if (val < read_sample(pmu, gt_id, __I915_SAMPLE_RC6_LAST_REPORTED))
		val = read_sample(pmu, gt_id, __I915_SAMPLE_RC6_LAST_REPORTED);
	else
		store_sample(pmu, gt_id, __I915_SAMPLE_RC6_LAST_REPORTED, val);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return val;
}

static void init_rc6(struct i915_pmu *pmu)
{
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, i915, i) {
		intel_wakeref_t wakeref;

		with_intel_runtime_pm(gt->uncore->rpm, wakeref) {
			u64 val = __get_rc6(gt);

			store_sample(pmu, i, __I915_SAMPLE_RC6, val);
			store_sample(pmu, i, __I915_SAMPLE_RC6_LAST_REPORTED,
				     val);
			pmu->sleep_last[i] = ktime_get_raw();
		}
	}
}

static void park_rc6(struct intel_gt *gt)
{
	struct i915_pmu *pmu = &gt->i915->pmu;

	store_sample(pmu, gt->info.id, __I915_SAMPLE_RC6, __get_rc6(gt));
	pmu->sleep_last[gt->info.id] = ktime_get_raw();
}

static void __i915_pmu_maybe_start_timer(struct i915_pmu *pmu)
{
	if (!pmu->timer_enabled && pmu_needs_timer(pmu)) {
		pmu->timer_enabled = true;
		pmu->timer_last = ktime_get();
		hrtimer_start_range_ns(&pmu->timer,
				       ns_to_ktime(PERIOD), 0,
				       HRTIMER_MODE_REL_PINNED);
	}
}

void i915_pmu_gt_parked(struct intel_gt *gt)
{
	struct i915_pmu *pmu = &gt->i915->pmu;

	if (!pmu->base.event_init)
		return;

	spin_lock_irq(&pmu->lock);

	park_rc6(gt);

	/*
	 * Signal sampling timer to stop if only engine events are enabled and
	 * GPU went idle.
	 */
	pmu->unparked &= ~BIT(gt->info.id);
	if (pmu->unparked == 0)
		pmu->timer_enabled = false;

	spin_unlock_irq(&pmu->lock);
}

void i915_pmu_gt_unparked(struct intel_gt *gt)
{
	struct i915_pmu *pmu = &gt->i915->pmu;

	if (!pmu->base.event_init)
		return;

	spin_lock_irq(&pmu->lock);

	/*
	 * Re-enable sampling timer when GPU goes active.
	 */
	if (pmu->unparked == 0)
		__i915_pmu_maybe_start_timer(pmu);

	pmu->unparked |= BIT(gt->info.id);

	spin_unlock_irq(&pmu->lock);
}

static void
add_sample(struct i915_pmu_sample *sample, u32 val)
{
	sample->cur += val;
}

static bool exclusive_mmio_access(const struct drm_i915_private *i915)
{
	/*
	 * We have to avoid concurrent mmio cache line access on gen7 or
	 * risk a machine hang. For a fun history lesson dig out the old
	 * userspace intel_gpu_top and run it on Ivybridge or Haswell!
	 */
	return GRAPHICS_VER(i915) == 7;
}

static void gen3_engine_sample(struct intel_engine_cs *engine, unsigned int period_ns)
{
	struct intel_engine_pmu *pmu = &engine->pmu;
	bool busy;
	u32 val;

	val = ENGINE_READ_FW(engine, RING_CTL);
	if (val == 0) /* powerwell off => engine idle */
		return;

	if (val & RING_WAIT)
		add_sample(&pmu->sample[I915_SAMPLE_WAIT], period_ns);
	if (val & RING_WAIT_SEMAPHORE)
		add_sample(&pmu->sample[I915_SAMPLE_SEMA], period_ns);

	/* No need to sample when busy stats are supported. */
	if (intel_engine_supports_stats(engine))
		return;

	/*
	 * While waiting on a semaphore or event, MI_MODE reports the
	 * ring as idle. However, previously using the seqno, and with
	 * execlists sampling, we account for the ring waiting as the
	 * engine being busy. Therefore, we record the sample as being
	 * busy if either waiting or !idle.
	 */
	busy = val & (RING_WAIT_SEMAPHORE | RING_WAIT);
	if (!busy) {
		val = ENGINE_READ_FW(engine, RING_MI_MODE);
		busy = !(val & MODE_IDLE);
	}
	if (busy)
		add_sample(&pmu->sample[I915_SAMPLE_BUSY], period_ns);
}

static void gen2_engine_sample(struct intel_engine_cs *engine, unsigned int period_ns)
{
	struct intel_engine_pmu *pmu = &engine->pmu;
	u32 tail, head, acthd;

	tail = ENGINE_READ_FW(engine, RING_TAIL);
	head = ENGINE_READ_FW(engine, RING_HEAD);
	acthd = ENGINE_READ_FW(engine, ACTHD);

	if (head & HEAD_WAIT_I8XX)
		add_sample(&pmu->sample[I915_SAMPLE_WAIT], period_ns);

	if (head & HEAD_WAIT_I8XX || head != acthd ||
	    (head & HEAD_ADDR) != (tail & TAIL_ADDR))
		add_sample(&pmu->sample[I915_SAMPLE_BUSY], period_ns);
}

static void engine_sample(struct intel_engine_cs *engine, unsigned int period_ns)
{
	if (GRAPHICS_VER(engine->i915) >= 3)
		gen3_engine_sample(engine, period_ns);
	else
		gen2_engine_sample(engine, period_ns);
}

static void
engines_sample(struct intel_gt *gt, unsigned int period_ns)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long flags;

	if ((i915->pmu.enable & ENGINE_SAMPLE_MASK) == 0)
		return;

	if (!intel_gt_pm_is_awake(gt))
		return;

	for_each_engine(engine, gt, id) {
		if (!engine->pmu.enable)
			continue;

		if (!intel_engine_pm_get_if_awake(engine))
			continue;

		if (exclusive_mmio_access(i915)) {
			spin_lock_irqsave(&engine->uncore->lock, flags);
			engine_sample(engine, period_ns);
			spin_unlock_irqrestore(&engine->uncore->lock, flags);
		} else {
			engine_sample(engine, period_ns);
		}

		intel_engine_pm_put_async(engine);
	}
}

static bool
frequency_sampling_enabled(struct i915_pmu *pmu, unsigned int gt)
{
	return pmu->enable &
	       (config_mask(__I915_PMU_ACTUAL_FREQUENCY(gt)) |
		config_mask(__I915_PMU_REQUESTED_FREQUENCY(gt)));
}

static void
frequency_sample(struct intel_gt *gt, unsigned int period_ns)
{
	struct drm_i915_private *i915 = gt->i915;
	const unsigned int gt_id = gt->info.id;
	struct i915_pmu *pmu = &i915->pmu;
	struct intel_rps *rps = &gt->rps;
	intel_wakeref_t wakeref;

	if (!frequency_sampling_enabled(pmu, gt_id))
		return;

	/* Report 0/0 (actual/requested) frequency while parked. */
	wakeref = intel_gt_pm_get_if_awake(gt);
	if (!wakeref)
		return;

	if (pmu->enable & config_mask(__I915_PMU_ACTUAL_FREQUENCY(gt_id))) {
		u32 val;

		/*
		 * We take a quick peek here without using forcewake
		 * so that we don't perturb the system under observation
		 * (forcewake => !rc6 => increased power use). We expect
		 * that if the read fails because it is outside of the
		 * mmio power well, then it will return 0 -- in which
		 * case we assume the system is running at the intended
		 * frequency. Fortunately, the read should rarely fail!
		 */
		val = intel_rps_read_actual_frequency_fw(rps);
		if (!val)
			val = intel_gpu_freq(rps, rps->cur_freq);

		add_sample_mult(pmu, gt_id, __I915_SAMPLE_FREQ_ACT,
				val, period_ns / 1000);
	}

	if (pmu->enable & config_mask(__I915_PMU_REQUESTED_FREQUENCY(gt_id))) {
		add_sample_mult(pmu, gt_id, __I915_SAMPLE_FREQ_REQ,
				intel_rps_get_requested_frequency(rps),
				period_ns / 1000);
	}

	intel_gt_pm_put_async(gt, wakeref);
}

static enum hrtimer_restart i915_sample(struct hrtimer *hrtimer)
{
	struct i915_pmu *pmu = container_of(hrtimer, struct i915_pmu, timer);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	unsigned int period_ns;
	struct intel_gt *gt;
	unsigned int i;
	ktime_t now;

	if (!READ_ONCE(pmu->timer_enabled))
		return HRTIMER_NORESTART;

	now = ktime_get();
	period_ns = ktime_to_ns(ktime_sub(now, pmu->timer_last));
	pmu->timer_last = now;

	/*
	 * Strictly speaking the passed in period may not be 100% accurate for
	 * all internal calculation, since some amount of time can be spent on
	 * grabbing the forcewake. However the potential error from timer call-
	 * back delay greatly dominates this so we keep it simple.
	 */

	for_each_gt(gt, i915, i) {
		if (!(pmu->unparked & BIT(i)))
			continue;

		engines_sample(gt, period_ns);
		frequency_sample(gt, period_ns);
	}

	hrtimer_forward(hrtimer, now, ns_to_ktime(PERIOD));

	return HRTIMER_RESTART;
}

static void i915_pmu_event_destroy(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);

	drm_WARN_ON(&i915->drm, event->parent);

	drm_dev_put(&i915->drm);
}

static int
engine_event_status(struct intel_engine_cs *engine,
		    enum drm_i915_pmu_engine_sample sample)
{
	switch (sample) {
	case I915_SAMPLE_BUSY:
	case I915_SAMPLE_WAIT:
		break;
	case I915_SAMPLE_SEMA:
		if (GRAPHICS_VER(engine->i915) < 6)
			return -ENODEV;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int
config_status(struct drm_i915_private *i915, u64 config)
{
	struct intel_gt *gt = to_gt(i915);

	unsigned int gt_id = config_gt_id(config);
	unsigned int max_gt_id = HAS_EXTRA_GT_LIST(i915) ? 1 : 0;

	if (gt_id > max_gt_id)
		return -ENOENT;

	switch (config_counter(config)) {
	case I915_PMU_ACTUAL_FREQUENCY:
		if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
			/* Requires a mutex for sampling! */
			return -ENODEV;
		fallthrough;
	case I915_PMU_REQUESTED_FREQUENCY:
		if (GRAPHICS_VER(i915) < 6)
			return -ENODEV;
		break;
	case I915_PMU_INTERRUPTS:
		if (gt_id)
			return -ENOENT;
		break;
	case I915_PMU_RC6_RESIDENCY:
		if (!gt->rc6.supported)
			return -ENODEV;
		break;
	case I915_PMU_SOFTWARE_GT_AWAKE_TIME:
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int engine_event_init(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	struct intel_engine_cs *engine;

	engine = intel_engine_lookup_user(i915, engine_event_class(event),
					  engine_event_instance(event));
	if (!engine)
		return -ENODEV;

	return engine_event_status(engine, engine_event_sample(event));
}

static int i915_pmu_event_init(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	int ret;

	if (pmu->closed)
		return -ENODEV;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	if (event->cpu < 0)
		return -EINVAL;

	/* only allow running on one cpu at a time */
	if (!cpumask_test_cpu(event->cpu, &i915_pmu_cpumask))
		return -EINVAL;

	if (is_engine_event(event))
		ret = engine_event_init(event);
	else
		ret = config_status(i915, event->attr.config);
	if (ret)
		return ret;

	if (!event->parent) {
		drm_dev_get(&i915->drm);
		event->destroy = i915_pmu_event_destroy;
	}

	return 0;
}

static u64 __i915_pmu_event_read(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	u64 val = 0;

	if (is_engine_event(event)) {
		u8 sample = engine_event_sample(event);
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(i915,
						  engine_event_class(event),
						  engine_event_instance(event));

		if (drm_WARN_ON_ONCE(&i915->drm, !engine)) {
			/* Do nothing */
		} else if (sample == I915_SAMPLE_BUSY &&
			   intel_engine_supports_stats(engine)) {
			ktime_t unused;

			val = ktime_to_ns(intel_engine_get_busy_time(engine,
								     &unused));
		} else {
			val = engine->pmu.sample[sample].cur;
		}
	} else {
		const unsigned int gt_id = config_gt_id(event->attr.config);
		const u64 config = config_counter(event->attr.config);

		switch (config) {
		case I915_PMU_ACTUAL_FREQUENCY:
			val =
			   div_u64(read_sample(pmu, gt_id,
					       __I915_SAMPLE_FREQ_ACT),
				   USEC_PER_SEC /* to MHz */);
			break;
		case I915_PMU_REQUESTED_FREQUENCY:
			val =
			   div_u64(read_sample(pmu, gt_id,
					       __I915_SAMPLE_FREQ_REQ),
				   USEC_PER_SEC /* to MHz */);
			break;
		case I915_PMU_INTERRUPTS:
			val = READ_ONCE(pmu->irq_count);
			break;
		case I915_PMU_RC6_RESIDENCY:
			val = get_rc6(i915->gt[gt_id]);
			break;
		case I915_PMU_SOFTWARE_GT_AWAKE_TIME:
			val = ktime_to_ns(intel_gt_get_awake_time(to_gt(i915)));
			break;
		}
	}

	return val;
}

static void i915_pmu_event_read(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, new;

	if (pmu->closed) {
		event->hw.state = PERF_HES_STOPPED;
		return;
	}

	prev = local64_read(&hwc->prev_count);
	do {
		new = __i915_pmu_event_read(event);
	} while (!local64_try_cmpxchg(&hwc->prev_count, &prev, new));

	local64_add(new - prev, &event->count);
}

static void i915_pmu_enable(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	const unsigned int bit = event_bit(event);
	unsigned long flags;

	if (bit == -1)
		goto update;

	spin_lock_irqsave(&pmu->lock, flags);

	/*
	 * Update the bitmask of enabled events and increment
	 * the event reference counter.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(pmu->enable_count) != I915_PMU_MASK_BITS);
	GEM_BUG_ON(bit >= ARRAY_SIZE(pmu->enable_count));
	GEM_BUG_ON(pmu->enable_count[bit] == ~0);

	pmu->enable |= BIT(bit);
	pmu->enable_count[bit]++;

	/*
	 * Start the sampling timer if needed and not already enabled.
	 */
	__i915_pmu_maybe_start_timer(pmu);

	/*
	 * For per-engine events the bitmask and reference counting
	 * is stored per engine.
	 */
	if (is_engine_event(event)) {
		u8 sample = engine_event_sample(event);
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(i915,
						  engine_event_class(event),
						  engine_event_instance(event));

		BUILD_BUG_ON(ARRAY_SIZE(engine->pmu.enable_count) !=
			     I915_ENGINE_SAMPLE_COUNT);
		BUILD_BUG_ON(ARRAY_SIZE(engine->pmu.sample) !=
			     I915_ENGINE_SAMPLE_COUNT);
		GEM_BUG_ON(sample >= ARRAY_SIZE(engine->pmu.enable_count));
		GEM_BUG_ON(sample >= ARRAY_SIZE(engine->pmu.sample));
		GEM_BUG_ON(engine->pmu.enable_count[sample] == ~0);

		engine->pmu.enable |= BIT(sample);
		engine->pmu.enable_count[sample]++;
	}

	spin_unlock_irqrestore(&pmu->lock, flags);

update:
	/*
	 * Store the current counter value so we can report the correct delta
	 * for all listeners. Even when the event was already enabled and has
	 * an existing non-zero value.
	 */
	local64_set(&event->hw.prev_count, __i915_pmu_event_read(event));
}

static void i915_pmu_disable(struct perf_event *event)
{
	struct i915_pmu *pmu = event_to_pmu(event);
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	const unsigned int bit = event_bit(event);
	unsigned long flags;

	if (bit == -1)
		return;

	spin_lock_irqsave(&pmu->lock, flags);

	if (is_engine_event(event)) {
		u8 sample = engine_event_sample(event);
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(i915,
						  engine_event_class(event),
						  engine_event_instance(event));

		GEM_BUG_ON(sample >= ARRAY_SIZE(engine->pmu.enable_count));
		GEM_BUG_ON(sample >= ARRAY_SIZE(engine->pmu.sample));
		GEM_BUG_ON(engine->pmu.enable_count[sample] == 0);

		/*
		 * Decrement the reference count and clear the enabled
		 * bitmask when the last listener on an event goes away.
		 */
		if (--engine->pmu.enable_count[sample] == 0)
			engine->pmu.enable &= ~BIT(sample);
	}

	GEM_BUG_ON(bit >= ARRAY_SIZE(pmu->enable_count));
	GEM_BUG_ON(pmu->enable_count[bit] == 0);
	/*
	 * Decrement the reference count and clear the enabled
	 * bitmask when the last listener on an event goes away.
	 */
	if (--pmu->enable_count[bit] == 0) {
		pmu->enable &= ~BIT(bit);
		pmu->timer_enabled &= pmu_needs_timer(pmu);
	}

	spin_unlock_irqrestore(&pmu->lock, flags);
}

static void i915_pmu_event_start(struct perf_event *event, int flags)
{
	struct i915_pmu *pmu = event_to_pmu(event);

	if (pmu->closed)
		return;

	i915_pmu_enable(event);
	event->hw.state = 0;
}

static void i915_pmu_event_stop(struct perf_event *event, int flags)
{
	struct i915_pmu *pmu = event_to_pmu(event);

	if (pmu->closed)
		goto out;

	if (flags & PERF_EF_UPDATE)
		i915_pmu_event_read(event);

	i915_pmu_disable(event);

out:
	event->hw.state = PERF_HES_STOPPED;
}

static int i915_pmu_event_add(struct perf_event *event, int flags)
{
	struct i915_pmu *pmu = event_to_pmu(event);

	if (pmu->closed)
		return -ENODEV;

	if (flags & PERF_EF_START)
		i915_pmu_event_start(event, flags);

	return 0;
}

static void i915_pmu_event_del(struct perf_event *event, int flags)
{
	i915_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int i915_pmu_event_event_idx(struct perf_event *event)
{
	return 0;
}

struct i915_str_attribute {
	struct device_attribute attr;
	const char *str;
};

static ssize_t i915_pmu_format_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct i915_str_attribute *eattr;

	eattr = container_of(attr, struct i915_str_attribute, attr);
	return sprintf(buf, "%s\n", eattr->str);
}

#define I915_PMU_FORMAT_ATTR(_name, _config) \
	(&((struct i915_str_attribute[]) { \
		{ .attr = __ATTR(_name, 0444, i915_pmu_format_show, NULL), \
		  .str = _config, } \
	})[0].attr.attr)

static struct attribute *i915_pmu_format_attrs[] = {
	I915_PMU_FORMAT_ATTR(i915_eventid, "config:0-20"),
	NULL,
};

static const struct attribute_group i915_pmu_format_attr_group = {
	.name = "format",
	.attrs = i915_pmu_format_attrs,
};

struct i915_ext_attribute {
	struct device_attribute attr;
	unsigned long val;
};

static ssize_t i915_pmu_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i915_ext_attribute *eattr;

	eattr = container_of(attr, struct i915_ext_attribute, attr);
	return sprintf(buf, "config=0x%lx\n", eattr->val);
}

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &i915_pmu_cpumask);
}

static DEVICE_ATTR_RO(cpumask);

static struct attribute *i915_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group i915_pmu_cpumask_attr_group = {
	.attrs = i915_cpumask_attrs,
};

#define __event(__counter, __name, __unit) \
{ \
	.counter = (__counter), \
	.name = (__name), \
	.unit = (__unit), \
	.global = false, \
}

#define __global_event(__counter, __name, __unit) \
{ \
	.counter = (__counter), \
	.name = (__name), \
	.unit = (__unit), \
	.global = true, \
}

#define __engine_event(__sample, __name) \
{ \
	.sample = (__sample), \
	.name = (__name), \
}

static struct i915_ext_attribute *
add_i915_attr(struct i915_ext_attribute *attr, const char *name, u64 config)
{
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = i915_pmu_event_show;
	attr->val = config;

	return ++attr;
}

static struct perf_pmu_events_attr *
add_pmu_attr(struct perf_pmu_events_attr *attr, const char *name,
	     const char *str)
{
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = perf_event_sysfs_show;
	attr->event_str = str;

	return ++attr;
}

static struct attribute **
create_event_attributes(struct i915_pmu *pmu)
{
	struct drm_i915_private *i915 = pmu_to_i915(pmu);
	static const struct {
		unsigned int counter;
		const char *name;
		const char *unit;
		bool global;
	} events[] = {
		__event(0, "actual-frequency", "M"),
		__event(1, "requested-frequency", "M"),
		__global_event(2, "interrupts", NULL),
		__event(3, "rc6-residency", "ns"),
		__event(4, "software-gt-awake-time", "ns"),
	};
	static const struct {
		enum drm_i915_pmu_engine_sample sample;
		char *name;
	} engine_events[] = {
		__engine_event(I915_SAMPLE_BUSY, "busy"),
		__engine_event(I915_SAMPLE_SEMA, "sema"),
		__engine_event(I915_SAMPLE_WAIT, "wait"),
	};
	unsigned int count = 0;
	struct perf_pmu_events_attr *pmu_attr = NULL, *pmu_iter;
	struct i915_ext_attribute *i915_attr = NULL, *i915_iter;
	struct attribute **attr = NULL, **attr_iter;
	struct intel_engine_cs *engine;
	struct intel_gt *gt;
	unsigned int i, j;

	/* Count how many counters we will be exposing. */
	for_each_gt(gt, i915, j) {
		for (i = 0; i < ARRAY_SIZE(events); i++) {
			u64 config = ___I915_PMU_OTHER(j, events[i].counter);

			if (!config_status(i915, config))
				count++;
		}
	}

	for_each_uabi_engine(engine, i915) {
		for (i = 0; i < ARRAY_SIZE(engine_events); i++) {
			if (!engine_event_status(engine,
						 engine_events[i].sample))
				count++;
		}
	}

	/* Allocate attribute objects and table. */
	i915_attr = kcalloc(count, sizeof(*i915_attr), GFP_KERNEL);
	if (!i915_attr)
		goto err_alloc;

	pmu_attr = kcalloc(count, sizeof(*pmu_attr), GFP_KERNEL);
	if (!pmu_attr)
		goto err_alloc;

	/* Max one pointer of each attribute type plus a termination entry. */
	attr = kcalloc(count * 2 + 1, sizeof(*attr), GFP_KERNEL);
	if (!attr)
		goto err_alloc;

	i915_iter = i915_attr;
	pmu_iter = pmu_attr;
	attr_iter = attr;

	/* Initialize supported non-engine counters. */
	for_each_gt(gt, i915, j) {
		for (i = 0; i < ARRAY_SIZE(events); i++) {
			u64 config = ___I915_PMU_OTHER(j, events[i].counter);
			char *str;

			if (config_status(i915, config))
				continue;

			if (events[i].global || !HAS_EXTRA_GT_LIST(i915))
				str = kstrdup(events[i].name, GFP_KERNEL);
			else
				str = kasprintf(GFP_KERNEL, "%s-gt%u",
						events[i].name, j);
			if (!str)
				goto err;

			*attr_iter++ = &i915_iter->attr.attr;
			i915_iter = add_i915_attr(i915_iter, str, config);

			if (events[i].unit) {
				if (events[i].global || !HAS_EXTRA_GT_LIST(i915))
					str = kasprintf(GFP_KERNEL, "%s.unit",
							events[i].name);
				else
					str = kasprintf(GFP_KERNEL, "%s-gt%u.unit",
							events[i].name, j);
				if (!str)
					goto err;

				*attr_iter++ = &pmu_iter->attr.attr;
				pmu_iter = add_pmu_attr(pmu_iter, str,
							events[i].unit);
			}
		}
	}

	/* Initialize supported engine counters. */
	for_each_uabi_engine(engine, i915) {
		for (i = 0; i < ARRAY_SIZE(engine_events); i++) {
			char *str;

			if (engine_event_status(engine,
						engine_events[i].sample))
				continue;

			str = kasprintf(GFP_KERNEL, "%s-%s",
					engine->name, engine_events[i].name);
			if (!str)
				goto err;

			*attr_iter++ = &i915_iter->attr.attr;
			i915_iter =
				add_i915_attr(i915_iter, str,
					      __I915_PMU_ENGINE(engine->uabi_class,
								engine->uabi_instance,
								engine_events[i].sample));

			str = kasprintf(GFP_KERNEL, "%s-%s.unit",
					engine->name, engine_events[i].name);
			if (!str)
				goto err;

			*attr_iter++ = &pmu_iter->attr.attr;
			pmu_iter = add_pmu_attr(pmu_iter, str, "ns");
		}
	}

	pmu->i915_attr = i915_attr;
	pmu->pmu_attr = pmu_attr;

	return attr;

err:;
	for (attr_iter = attr; *attr_iter; attr_iter++)
		kfree((*attr_iter)->name);

err_alloc:
	kfree(attr);
	kfree(i915_attr);
	kfree(pmu_attr);

	return NULL;
}

static void free_event_attributes(struct i915_pmu *pmu)
{
	struct attribute **attr_iter = pmu->events_attr_group.attrs;

	for (; *attr_iter; attr_iter++)
		kfree((*attr_iter)->name);

	kfree(pmu->events_attr_group.attrs);
	kfree(pmu->i915_attr);
	kfree(pmu->pmu_attr);

	pmu->events_attr_group.attrs = NULL;
	pmu->i915_attr = NULL;
	pmu->pmu_attr = NULL;
}

static int i915_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct i915_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), cpuhp.node);

	GEM_BUG_ON(!pmu->base.event_init);

	/* Select the first online CPU as a designated reader. */
	if (cpumask_empty(&i915_pmu_cpumask))
		cpumask_set_cpu(cpu, &i915_pmu_cpumask);

	return 0;
}

static int i915_pmu_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct i915_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), cpuhp.node);
	unsigned int target = i915_pmu_target_cpu;

	GEM_BUG_ON(!pmu->base.event_init);

	/*
	 * Unregistering an instance generates a CPU offline event which we must
	 * ignore to avoid incorrectly modifying the shared i915_pmu_cpumask.
	 */
	if (pmu->closed)
		return 0;

	if (cpumask_test_and_clear_cpu(cpu, &i915_pmu_cpumask)) {
		target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);

		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &i915_pmu_cpumask);
			i915_pmu_target_cpu = target;
		}
	}

	if (target < nr_cpu_ids && target != pmu->cpuhp.cpu) {
		perf_pmu_migrate_context(&pmu->base, cpu, target);
		pmu->cpuhp.cpu = target;
	}

	return 0;
}

static enum cpuhp_state cpuhp_slot = CPUHP_INVALID;

int i915_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/x86/intel/i915:online",
				      i915_pmu_cpu_online,
				      i915_pmu_cpu_offline);
	if (ret < 0)
		pr_notice("Failed to setup cpuhp state for i915 PMU! (%d)\n",
			  ret);
	else
		cpuhp_slot = ret;

	return 0;
}

void i915_pmu_exit(void)
{
	if (cpuhp_slot != CPUHP_INVALID)
		cpuhp_remove_multi_state(cpuhp_slot);
}

static int i915_pmu_register_cpuhp_state(struct i915_pmu *pmu)
{
	if (cpuhp_slot == CPUHP_INVALID)
		return -EINVAL;

	return cpuhp_state_add_instance(cpuhp_slot, &pmu->cpuhp.node);
}

static void i915_pmu_unregister_cpuhp_state(struct i915_pmu *pmu)
{
	cpuhp_state_remove_instance(cpuhp_slot, &pmu->cpuhp.node);
}

void i915_pmu_register(struct drm_i915_private *i915)
{
	struct i915_pmu *pmu = &i915->pmu;
	const struct attribute_group *attr_groups[] = {
		&i915_pmu_format_attr_group,
		&pmu->events_attr_group,
		&i915_pmu_cpumask_attr_group,
		NULL
	};

	int ret = -ENOMEM;

	spin_lock_init(&pmu->lock);
	hrtimer_init(&pmu->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pmu->timer.function = i915_sample;
	pmu->cpuhp.cpu = -1;
	init_rc6(pmu);

	if (IS_DGFX(i915)) {
		pmu->name = kasprintf(GFP_KERNEL,
				      "i915_%s",
				      dev_name(i915->drm.dev));
		if (pmu->name) {
			/* tools/perf reserves colons as special. */
			strreplace((char *)pmu->name, ':', '_');
		}
	} else {
		pmu->name = "i915";
	}
	if (!pmu->name)
		goto err;

	pmu->events_attr_group.name = "events";
	pmu->events_attr_group.attrs = create_event_attributes(pmu);
	if (!pmu->events_attr_group.attrs)
		goto err_name;

	pmu->base.attr_groups = kmemdup(attr_groups, sizeof(attr_groups),
					GFP_KERNEL);
	if (!pmu->base.attr_groups)
		goto err_attr;

	pmu->base.module	= THIS_MODULE;
	pmu->base.task_ctx_nr	= perf_invalid_context;
	pmu->base.event_init	= i915_pmu_event_init;
	pmu->base.add		= i915_pmu_event_add;
	pmu->base.del		= i915_pmu_event_del;
	pmu->base.start		= i915_pmu_event_start;
	pmu->base.stop		= i915_pmu_event_stop;
	pmu->base.read		= i915_pmu_event_read;
	pmu->base.event_idx	= i915_pmu_event_event_idx;

	ret = perf_pmu_register(&pmu->base, pmu->name, -1);
	if (ret)
		goto err_groups;

	ret = i915_pmu_register_cpuhp_state(pmu);
	if (ret)
		goto err_unreg;

	return;

err_unreg:
	perf_pmu_unregister(&pmu->base);
err_groups:
	kfree(pmu->base.attr_groups);
err_attr:
	pmu->base.event_init = NULL;
	free_event_attributes(pmu);
err_name:
	if (IS_DGFX(i915))
		kfree(pmu->name);
err:
	drm_notice(&i915->drm, "Failed to register PMU!\n");
}

void i915_pmu_unregister(struct drm_i915_private *i915)
{
	struct i915_pmu *pmu = &i915->pmu;

	if (!pmu->base.event_init)
		return;

	/*
	 * "Disconnect" the PMU callbacks - since all are atomic synchronize_rcu
	 * ensures all currently executing ones will have exited before we
	 * proceed with unregistration.
	 */
	pmu->closed = true;
	synchronize_rcu();

	hrtimer_cancel(&pmu->timer);

	i915_pmu_unregister_cpuhp_state(pmu);

	perf_pmu_unregister(&pmu->base);
	pmu->base.event_init = NULL;
	kfree(pmu->base.attr_groups);
	if (IS_DGFX(i915))
		kfree(pmu->name);
	free_event_attributes(pmu);
}
