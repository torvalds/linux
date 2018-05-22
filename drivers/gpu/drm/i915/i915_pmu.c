/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/perf_event.h>
#include <linux/pm_runtime.h>

#include "i915_drv.h"
#include "i915_pmu.h"
#include "intel_ringbuffer.h"

/* Frequency for the sampling timer for events which need it. */
#define FREQUENCY 200
#define PERIOD max_t(u64, 10000, NSEC_PER_SEC / FREQUENCY)

#define ENGINE_SAMPLE_MASK \
	(BIT(I915_SAMPLE_BUSY) | \
	 BIT(I915_SAMPLE_WAIT) | \
	 BIT(I915_SAMPLE_SEMA))

#define ENGINE_SAMPLE_BITS (1 << I915_PMU_SAMPLE_BITS)

static cpumask_t i915_pmu_cpumask;

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

static bool is_engine_config(u64 config)
{
	return config < __I915_PMU_OTHER(0);
}

static unsigned int config_enabled_bit(u64 config)
{
	if (is_engine_config(config))
		return engine_config_sample(config);
	else
		return ENGINE_SAMPLE_BITS + (config - __I915_PMU_OTHER(0));
}

static u64 config_enabled_mask(u64 config)
{
	return BIT_ULL(config_enabled_bit(config));
}

static bool is_engine_event(struct perf_event *event)
{
	return is_engine_config(event->attr.config);
}

static unsigned int event_enabled_bit(struct perf_event *event)
{
	return config_enabled_bit(event->attr.config);
}

static bool pmu_needs_timer(struct drm_i915_private *i915, bool gpu_active)
{
	u64 enable;

	/*
	 * Only some counters need the sampling timer.
	 *
	 * We start with a bitmask of all currently enabled events.
	 */
	enable = i915->pmu.enable;

	/*
	 * Mask out all the ones which do not need the timer, or in
	 * other words keep all the ones that could need the timer.
	 */
	enable &= config_enabled_mask(I915_PMU_ACTUAL_FREQUENCY) |
		  config_enabled_mask(I915_PMU_REQUESTED_FREQUENCY) |
		  ENGINE_SAMPLE_MASK;

	/*
	 * When the GPU is idle per-engine counters do not need to be
	 * running so clear those bits out.
	 */
	if (!gpu_active)
		enable &= ~ENGINE_SAMPLE_MASK;
	/*
	 * Also there is software busyness tracking available we do not
	 * need the timer for I915_SAMPLE_BUSY counter.
	 *
	 * Use RCS as proxy for all engines.
	 */
	else if (intel_engine_supports_stats(i915->engine[RCS]))
		enable &= ~BIT(I915_SAMPLE_BUSY);

	/*
	 * If some bits remain it means we need the sampling timer running.
	 */
	return enable;
}

void i915_pmu_gt_parked(struct drm_i915_private *i915)
{
	if (!i915->pmu.base.event_init)
		return;

	spin_lock_irq(&i915->pmu.lock);
	/*
	 * Signal sampling timer to stop if only engine events are enabled and
	 * GPU went idle.
	 */
	i915->pmu.timer_enabled = pmu_needs_timer(i915, false);
	spin_unlock_irq(&i915->pmu.lock);
}

static void __i915_pmu_maybe_start_timer(struct drm_i915_private *i915)
{
	if (!i915->pmu.timer_enabled && pmu_needs_timer(i915, true)) {
		i915->pmu.timer_enabled = true;
		hrtimer_start_range_ns(&i915->pmu.timer,
				       ns_to_ktime(PERIOD), 0,
				       HRTIMER_MODE_REL_PINNED);
	}
}

void i915_pmu_gt_unparked(struct drm_i915_private *i915)
{
	if (!i915->pmu.base.event_init)
		return;

	spin_lock_irq(&i915->pmu.lock);
	/*
	 * Re-enable sampling timer when GPU goes active.
	 */
	__i915_pmu_maybe_start_timer(i915);
	spin_unlock_irq(&i915->pmu.lock);
}

static bool grab_forcewake(struct drm_i915_private *i915, bool fw)
{
	if (!fw)
		intel_uncore_forcewake_get(i915, FORCEWAKE_ALL);

	return true;
}

static void
update_sample(struct i915_pmu_sample *sample, u32 unit, u32 val)
{
	sample->cur += mul_u32_u32(val, unit);
}

static void engines_sample(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	bool fw = false;

	if ((dev_priv->pmu.enable & ENGINE_SAMPLE_MASK) == 0)
		return;

	if (!dev_priv->gt.awake)
		return;

	if (!intel_runtime_pm_get_if_in_use(dev_priv))
		return;

	for_each_engine(engine, dev_priv, id) {
		u32 current_seqno = intel_engine_get_seqno(engine);
		u32 last_seqno = intel_engine_last_submit(engine);
		u32 val;

		val = !i915_seqno_passed(current_seqno, last_seqno);

		update_sample(&engine->pmu.sample[I915_SAMPLE_BUSY],
			      PERIOD, val);

		if (val && (engine->pmu.enable &
		    (BIT(I915_SAMPLE_WAIT) | BIT(I915_SAMPLE_SEMA)))) {
			fw = grab_forcewake(dev_priv, fw);

			val = I915_READ_FW(RING_CTL(engine->mmio_base));
		} else {
			val = 0;
		}

		update_sample(&engine->pmu.sample[I915_SAMPLE_WAIT],
			      PERIOD, !!(val & RING_WAIT));

		update_sample(&engine->pmu.sample[I915_SAMPLE_SEMA],
			      PERIOD, !!(val & RING_WAIT_SEMAPHORE));
	}

	if (fw)
		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	intel_runtime_pm_put(dev_priv);
}

static void frequency_sample(struct drm_i915_private *dev_priv)
{
	if (dev_priv->pmu.enable &
	    config_enabled_mask(I915_PMU_ACTUAL_FREQUENCY)) {
		u32 val;

		val = dev_priv->gt_pm.rps.cur_freq;
		if (dev_priv->gt.awake &&
		    intel_runtime_pm_get_if_in_use(dev_priv)) {
			val = intel_get_cagf(dev_priv,
					     I915_READ_NOTRACE(GEN6_RPSTAT1));
			intel_runtime_pm_put(dev_priv);
		}

		update_sample(&dev_priv->pmu.sample[__I915_SAMPLE_FREQ_ACT],
			      1, intel_gpu_freq(dev_priv, val));
	}

	if (dev_priv->pmu.enable &
	    config_enabled_mask(I915_PMU_REQUESTED_FREQUENCY)) {
		update_sample(&dev_priv->pmu.sample[__I915_SAMPLE_FREQ_REQ], 1,
			      intel_gpu_freq(dev_priv,
					     dev_priv->gt_pm.rps.cur_freq));
	}
}

static enum hrtimer_restart i915_sample(struct hrtimer *hrtimer)
{
	struct drm_i915_private *i915 =
		container_of(hrtimer, struct drm_i915_private, pmu.timer);

	if (!READ_ONCE(i915->pmu.timer_enabled))
		return HRTIMER_NORESTART;

	engines_sample(i915);
	frequency_sample(i915);

	hrtimer_forward_now(hrtimer, ns_to_ktime(PERIOD));
	return HRTIMER_RESTART;
}

static u64 count_interrupts(struct drm_i915_private *i915)
{
	/* open-coded kstat_irqs() */
	struct irq_desc *desc = irq_to_desc(i915->drm.pdev->irq);
	u64 sum = 0;
	int cpu;

	if (!desc || !desc->kstat_irqs)
		return 0;

	for_each_possible_cpu(cpu)
		sum += *per_cpu_ptr(desc->kstat_irqs, cpu);

	return sum;
}

static void engine_event_destroy(struct perf_event *event)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	struct intel_engine_cs *engine;

	engine = intel_engine_lookup_user(i915,
					  engine_event_class(event),
					  engine_event_instance(event));
	if (WARN_ON_ONCE(!engine))
		return;

	if (engine_event_sample(event) == I915_SAMPLE_BUSY &&
	    intel_engine_supports_stats(engine))
		intel_disable_engine_stats(engine);
}

static void i915_pmu_event_destroy(struct perf_event *event)
{
	WARN_ON(event->parent);

	if (is_engine_event(event))
		engine_event_destroy(event);
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
		if (INTEL_GEN(engine->i915) < 6)
			return -ENODEV;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int engine_event_init(struct perf_event *event)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	struct intel_engine_cs *engine;
	u8 sample;
	int ret;

	engine = intel_engine_lookup_user(i915, engine_event_class(event),
					  engine_event_instance(event));
	if (!engine)
		return -ENODEV;

	sample = engine_event_sample(event);
	ret = engine_event_status(engine, sample);
	if (ret)
		return ret;

	if (sample == I915_SAMPLE_BUSY && intel_engine_supports_stats(engine))
		ret = intel_enable_engine_stats(engine);

	return ret;
}

static int i915_pmu_event_init(struct perf_event *event)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	int ret;

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

	if (is_engine_event(event)) {
		ret = engine_event_init(event);
	} else {
		ret = 0;
		switch (event->attr.config) {
		case I915_PMU_ACTUAL_FREQUENCY:
			if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
				 /* Requires a mutex for sampling! */
				ret = -ENODEV;
		case I915_PMU_REQUESTED_FREQUENCY:
			if (INTEL_GEN(i915) < 6)
				ret = -ENODEV;
			break;
		case I915_PMU_INTERRUPTS:
			break;
		case I915_PMU_RC6_RESIDENCY:
			if (!HAS_RC6(i915))
				ret = -ENODEV;
			break;
		default:
			ret = -ENOENT;
			break;
		}
	}
	if (ret)
		return ret;

	if (!event->parent)
		event->destroy = i915_pmu_event_destroy;

	return 0;
}

static u64 __get_rc6(struct drm_i915_private *i915)
{
	u64 val;

	val = intel_rc6_residency_ns(i915,
				     IS_VALLEYVIEW(i915) ?
				     VLV_GT_RENDER_RC6 :
				     GEN6_GT_GFX_RC6);

	if (HAS_RC6p(i915))
		val += intel_rc6_residency_ns(i915, GEN6_GT_GFX_RC6p);

	if (HAS_RC6pp(i915))
		val += intel_rc6_residency_ns(i915, GEN6_GT_GFX_RC6pp);

	return val;
}

static u64 get_rc6(struct drm_i915_private *i915, bool locked)
{
#if IS_ENABLED(CONFIG_PM)
	unsigned long flags;
	u64 val;

	if (intel_runtime_pm_get_if_in_use(i915)) {
		val = __get_rc6(i915);
		intel_runtime_pm_put(i915);

		/*
		 * If we are coming back from being runtime suspended we must
		 * be careful not to report a larger value than returned
		 * previously.
		 */

		if (!locked)
			spin_lock_irqsave(&i915->pmu.lock, flags);

		if (val >= i915->pmu.sample[__I915_SAMPLE_RC6_ESTIMATED].cur) {
			i915->pmu.sample[__I915_SAMPLE_RC6_ESTIMATED].cur = 0;
			i915->pmu.sample[__I915_SAMPLE_RC6].cur = val;
		} else {
			val = i915->pmu.sample[__I915_SAMPLE_RC6_ESTIMATED].cur;
		}

		if (!locked)
			spin_unlock_irqrestore(&i915->pmu.lock, flags);
	} else {
		struct pci_dev *pdev = i915->drm.pdev;
		struct device *kdev = &pdev->dev;
		unsigned long flags2;

		/*
		 * We are runtime suspended.
		 *
		 * Report the delta from when the device was suspended to now,
		 * on top of the last known real value, as the approximated RC6
		 * counter value.
		 */
		if (!locked)
			spin_lock_irqsave(&i915->pmu.lock, flags);

		spin_lock_irqsave(&kdev->power.lock, flags2);

		if (!i915->pmu.sample[__I915_SAMPLE_RC6_ESTIMATED].cur)
			i915->pmu.suspended_jiffies_last =
						kdev->power.suspended_jiffies;

		val = kdev->power.suspended_jiffies -
		      i915->pmu.suspended_jiffies_last;
		val += jiffies - kdev->power.accounting_timestamp;

		spin_unlock_irqrestore(&kdev->power.lock, flags2);

		val = jiffies_to_nsecs(val);
		val += i915->pmu.sample[__I915_SAMPLE_RC6].cur;
		i915->pmu.sample[__I915_SAMPLE_RC6_ESTIMATED].cur = val;

		if (!locked)
			spin_unlock_irqrestore(&i915->pmu.lock, flags);
	}

	return val;
#else
	return __get_rc6(i915);
#endif
}

static u64 __i915_pmu_event_read(struct perf_event *event, bool locked)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	u64 val = 0;

	if (is_engine_event(event)) {
		u8 sample = engine_event_sample(event);
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(i915,
						  engine_event_class(event),
						  engine_event_instance(event));

		if (WARN_ON_ONCE(!engine)) {
			/* Do nothing */
		} else if (sample == I915_SAMPLE_BUSY &&
			   intel_engine_supports_stats(engine)) {
			val = ktime_to_ns(intel_engine_get_busy_time(engine));
		} else {
			val = engine->pmu.sample[sample].cur;
		}
	} else {
		switch (event->attr.config) {
		case I915_PMU_ACTUAL_FREQUENCY:
			val =
			   div_u64(i915->pmu.sample[__I915_SAMPLE_FREQ_ACT].cur,
				   FREQUENCY);
			break;
		case I915_PMU_REQUESTED_FREQUENCY:
			val =
			   div_u64(i915->pmu.sample[__I915_SAMPLE_FREQ_REQ].cur,
				   FREQUENCY);
			break;
		case I915_PMU_INTERRUPTS:
			val = count_interrupts(i915);
			break;
		case I915_PMU_RC6_RESIDENCY:
			val = get_rc6(i915, locked);
			break;
		}
	}

	return val;
}

static void i915_pmu_event_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, new;

again:
	prev = local64_read(&hwc->prev_count);
	new = __i915_pmu_event_read(event, false);

	if (local64_cmpxchg(&hwc->prev_count, prev, new) != prev)
		goto again;

	local64_add(new - prev, &event->count);
}

static void i915_pmu_enable(struct perf_event *event)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	unsigned int bit = event_enabled_bit(event);
	unsigned long flags;

	spin_lock_irqsave(&i915->pmu.lock, flags);

	/*
	 * Update the bitmask of enabled events and increment
	 * the event reference counter.
	 */
	GEM_BUG_ON(bit >= I915_PMU_MASK_BITS);
	GEM_BUG_ON(i915->pmu.enable_count[bit] == ~0);
	i915->pmu.enable |= BIT_ULL(bit);
	i915->pmu.enable_count[bit]++;

	/*
	 * Start the sampling timer if needed and not already enabled.
	 */
	__i915_pmu_maybe_start_timer(i915);

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
		GEM_BUG_ON(!engine);
		engine->pmu.enable |= BIT(sample);

		GEM_BUG_ON(sample >= I915_PMU_SAMPLE_BITS);
		GEM_BUG_ON(engine->pmu.enable_count[sample] == ~0);
		engine->pmu.enable_count[sample]++;
	}

	/*
	 * Store the current counter value so we can report the correct delta
	 * for all listeners. Even when the event was already enabled and has
	 * an existing non-zero value.
	 */
	local64_set(&event->hw.prev_count, __i915_pmu_event_read(event, true));

	spin_unlock_irqrestore(&i915->pmu.lock, flags);
}

static void i915_pmu_disable(struct perf_event *event)
{
	struct drm_i915_private *i915 =
		container_of(event->pmu, typeof(*i915), pmu.base);
	unsigned int bit = event_enabled_bit(event);
	unsigned long flags;

	spin_lock_irqsave(&i915->pmu.lock, flags);

	if (is_engine_event(event)) {
		u8 sample = engine_event_sample(event);
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(i915,
						  engine_event_class(event),
						  engine_event_instance(event));
		GEM_BUG_ON(!engine);
		GEM_BUG_ON(sample >= I915_PMU_SAMPLE_BITS);
		GEM_BUG_ON(engine->pmu.enable_count[sample] == 0);
		/*
		 * Decrement the reference count and clear the enabled
		 * bitmask when the last listener on an event goes away.
		 */
		if (--engine->pmu.enable_count[sample] == 0)
			engine->pmu.enable &= ~BIT(sample);
	}

	GEM_BUG_ON(bit >= I915_PMU_MASK_BITS);
	GEM_BUG_ON(i915->pmu.enable_count[bit] == 0);
	/*
	 * Decrement the reference count and clear the enabled
	 * bitmask when the last listener on an event goes away.
	 */
	if (--i915->pmu.enable_count[bit] == 0) {
		i915->pmu.enable &= ~BIT_ULL(bit);
		i915->pmu.timer_enabled &= pmu_needs_timer(i915, true);
	}

	spin_unlock_irqrestore(&i915->pmu.lock, flags);
}

static void i915_pmu_event_start(struct perf_event *event, int flags)
{
	i915_pmu_enable(event);
	event->hw.state = 0;
}

static void i915_pmu_event_stop(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_UPDATE)
		i915_pmu_event_read(event);
	i915_pmu_disable(event);
	event->hw.state = PERF_HES_STOPPED;
}

static int i915_pmu_event_add(struct perf_event *event, int flags)
{
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

#define I915_EVENT_ATTR(_name, _config) \
	(&((struct i915_ext_attribute[]) { \
		{ .attr = __ATTR(_name, 0444, i915_pmu_event_show, NULL), \
		  .val = _config, } \
	})[0].attr.attr)

#define I915_EVENT_STR(_name, _str) \
	(&((struct perf_pmu_events_attr[]) { \
		{ .attr	     = __ATTR(_name, 0444, perf_event_sysfs_show, NULL), \
		  .id	     = 0, \
		  .event_str = _str, } \
	})[0].attr.attr)

#define I915_EVENT(_name, _config, _unit) \
	I915_EVENT_ATTR(_name, _config), \
	I915_EVENT_STR(_name.unit, _unit)

#define I915_ENGINE_EVENT(_name, _class, _instance, _sample) \
	I915_EVENT_ATTR(_name, __I915_PMU_ENGINE(_class, _instance, _sample)), \
	I915_EVENT_STR(_name.unit, "ns")

#define I915_ENGINE_EVENTS(_name, _class, _instance) \
	I915_ENGINE_EVENT(_name##_instance-busy, _class, _instance, I915_SAMPLE_BUSY), \
	I915_ENGINE_EVENT(_name##_instance-sema, _class, _instance, I915_SAMPLE_SEMA), \
	I915_ENGINE_EVENT(_name##_instance-wait, _class, _instance, I915_SAMPLE_WAIT)

static struct attribute *i915_pmu_events_attrs[] = {
	I915_ENGINE_EVENTS(rcs, I915_ENGINE_CLASS_RENDER, 0),
	I915_ENGINE_EVENTS(bcs, I915_ENGINE_CLASS_COPY, 0),
	I915_ENGINE_EVENTS(vcs, I915_ENGINE_CLASS_VIDEO, 0),
	I915_ENGINE_EVENTS(vcs, I915_ENGINE_CLASS_VIDEO, 1),
	I915_ENGINE_EVENTS(vecs, I915_ENGINE_CLASS_VIDEO_ENHANCE, 0),

	I915_EVENT(actual-frequency,    I915_PMU_ACTUAL_FREQUENCY,    "MHz"),
	I915_EVENT(requested-frequency, I915_PMU_REQUESTED_FREQUENCY, "MHz"),

	I915_EVENT_ATTR(interrupts, I915_PMU_INTERRUPTS),

	I915_EVENT(rc6-residency,   I915_PMU_RC6_RESIDENCY,   "ns"),

	NULL,
};

static const struct attribute_group i915_pmu_events_attr_group = {
	.name = "events",
	.attrs = i915_pmu_events_attrs,
};

static ssize_t
i915_pmu_get_attr_cpumask(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &i915_pmu_cpumask);
}

static DEVICE_ATTR(cpumask, 0444, i915_pmu_get_attr_cpumask, NULL);

static struct attribute *i915_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group i915_pmu_cpumask_attr_group = {
	.attrs = i915_cpumask_attrs,
};

static const struct attribute_group *i915_pmu_attr_groups[] = {
	&i915_pmu_format_attr_group,
	&i915_pmu_events_attr_group,
	&i915_pmu_cpumask_attr_group,
	NULL
};

static int i915_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct i915_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), node);

	GEM_BUG_ON(!pmu->base.event_init);

	/* Select the first online CPU as a designated reader. */
	if (!cpumask_weight(&i915_pmu_cpumask))
		cpumask_set_cpu(cpu, &i915_pmu_cpumask);

	return 0;
}

static int i915_pmu_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct i915_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), node);
	unsigned int target;

	GEM_BUG_ON(!pmu->base.event_init);

	if (cpumask_test_and_clear_cpu(cpu, &i915_pmu_cpumask)) {
		target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);
		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &i915_pmu_cpumask);
			perf_pmu_migrate_context(&pmu->base, cpu, target);
		}
	}

	return 0;
}

static enum cpuhp_state cpuhp_slot = CPUHP_INVALID;

static int i915_pmu_register_cpuhp_state(struct drm_i915_private *i915)
{
	enum cpuhp_state slot;
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/x86/intel/i915:online",
				      i915_pmu_cpu_online,
				      i915_pmu_cpu_offline);
	if (ret < 0)
		return ret;

	slot = ret;
	ret = cpuhp_state_add_instance(slot, &i915->pmu.node);
	if (ret) {
		cpuhp_remove_multi_state(slot);
		return ret;
	}

	cpuhp_slot = slot;
	return 0;
}

static void i915_pmu_unregister_cpuhp_state(struct drm_i915_private *i915)
{
	WARN_ON(cpuhp_slot == CPUHP_INVALID);
	WARN_ON(cpuhp_state_remove_instance(cpuhp_slot, &i915->pmu.node));
	cpuhp_remove_multi_state(cpuhp_slot);
}

void i915_pmu_register(struct drm_i915_private *i915)
{
	int ret;

	if (INTEL_GEN(i915) <= 2) {
		DRM_INFO("PMU not supported for this GPU.");
		return;
	}

	i915->pmu.base.attr_groups	= i915_pmu_attr_groups;
	i915->pmu.base.task_ctx_nr	= perf_invalid_context;
	i915->pmu.base.event_init	= i915_pmu_event_init;
	i915->pmu.base.add		= i915_pmu_event_add;
	i915->pmu.base.del		= i915_pmu_event_del;
	i915->pmu.base.start		= i915_pmu_event_start;
	i915->pmu.base.stop		= i915_pmu_event_stop;
	i915->pmu.base.read		= i915_pmu_event_read;
	i915->pmu.base.event_idx	= i915_pmu_event_event_idx;

	spin_lock_init(&i915->pmu.lock);
	hrtimer_init(&i915->pmu.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	i915->pmu.timer.function = i915_sample;

	ret = perf_pmu_register(&i915->pmu.base, "i915", -1);
	if (ret)
		goto err;

	ret = i915_pmu_register_cpuhp_state(i915);
	if (ret)
		goto err_unreg;

	return;

err_unreg:
	perf_pmu_unregister(&i915->pmu.base);
err:
	i915->pmu.base.event_init = NULL;
	DRM_NOTE("Failed to register PMU! (err=%d)\n", ret);
}

void i915_pmu_unregister(struct drm_i915_private *i915)
{
	if (!i915->pmu.base.event_init)
		return;

	WARN_ON(i915->pmu.enable);

	hrtimer_cancel(&i915->pmu.timer);

	i915_pmu_unregister_cpuhp_state(i915);

	perf_pmu_unregister(&i915->pmu.base);
	i915->pmu.base.event_init = NULL;
}
