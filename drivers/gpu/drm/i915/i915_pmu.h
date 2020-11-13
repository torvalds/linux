/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#ifndef __I915_PMU_H__
#define __I915_PMU_H__

#include <linux/hrtimer.h>
#include <linux/perf_event.h>
#include <linux/spinlock_types.h>
#include <uapi/drm/i915_drm.h>

struct drm_i915_private;

enum {
	__I915_SAMPLE_FREQ_ACT = 0,
	__I915_SAMPLE_FREQ_REQ,
	__I915_SAMPLE_RC6,
	__I915_SAMPLE_RC6_LAST_REPORTED,
	__I915_NUM_PMU_SAMPLERS
};

/**
 * How many different events we track in the global PMU mask.
 *
 * It is also used to know to needed number of event reference counters.
 */
#define I915_PMU_MASK_BITS \
	((1 << I915_PMU_SAMPLE_BITS) + \
	 (I915_PMU_LAST + 1 - __I915_PMU_OTHER(0)))

#define I915_ENGINE_SAMPLE_COUNT (I915_SAMPLE_SEMA + 1)

struct i915_pmu_sample {
	u64 cur;
};

struct i915_pmu {
	/**
	 * @cpuhp: Struct used for CPU hotplug handling.
	 */
	struct {
		struct hlist_node node;
		unsigned int cpu;
	} cpuhp;
	/**
	 * @base: PMU base.
	 */
	struct pmu base;
	/**
	 * @closed: i915 is unregistering.
	 */
	bool closed;
	/**
	 * @name: Name as registered with perf core.
	 */
	const char *name;
	/**
	 * @lock: Lock protecting enable mask and ref count handling.
	 */
	spinlock_t lock;
	/**
	 * @timer: Timer for internal i915 PMU sampling.
	 */
	struct hrtimer timer;
	/**
	 * @enable: Bitmask of all currently enabled events.
	 *
	 * Bits are derived from uAPI event numbers in a way that low 16 bits
	 * correspond to engine event _sample_ _type_ (I915_SAMPLE_QUEUED is
	 * bit 0), and higher bits correspond to other events (for instance
	 * I915_PMU_ACTUAL_FREQUENCY is bit 16 etc).
	 *
	 * In other words, low 16 bits are not per engine but per engine
	 * sampler type, while the upper bits are directly mapped to other
	 * event types.
	 */
	u64 enable;

	/**
	 * @timer_last:
	 *
	 * Timestmap of the previous timer invocation.
	 */
	ktime_t timer_last;

	/**
	 * @enable_count: Reference counts for the enabled events.
	 *
	 * Array indices are mapped in the same way as bits in the @enable field
	 * and they are used to control sampling on/off when multiple clients
	 * are using the PMU API.
	 */
	unsigned int enable_count[I915_PMU_MASK_BITS];
	/**
	 * @timer_enabled: Should the internal sampling timer be running.
	 */
	bool timer_enabled;
	/**
	 * @sample: Current and previous (raw) counters for sampling events.
	 *
	 * These counters are updated from the i915 PMU sampling timer.
	 *
	 * Only global counters are held here, while the per-engine ones are in
	 * struct intel_engine_cs.
	 */
	struct i915_pmu_sample sample[__I915_NUM_PMU_SAMPLERS];
	/**
	 * @sleep_last: Last time GT parked for RC6 estimation.
	 */
	ktime_t sleep_last;
	/**
	 * @events_attr_group: Device events attribute group.
	 */
	struct attribute_group events_attr_group;
	/**
	 * @i915_attr: Memory block holding device attributes.
	 */
	void *i915_attr;
	/**
	 * @pmu_attr: Memory block holding device attributes.
	 */
	void *pmu_attr;
};

#ifdef CONFIG_PERF_EVENTS
void i915_pmu_init(void);
void i915_pmu_exit(void);
void i915_pmu_register(struct drm_i915_private *i915);
void i915_pmu_unregister(struct drm_i915_private *i915);
void i915_pmu_gt_parked(struct drm_i915_private *i915);
void i915_pmu_gt_unparked(struct drm_i915_private *i915);
#else
static inline void i915_pmu_init(void) {}
static inline void i915_pmu_exit(void) {}
static inline void i915_pmu_register(struct drm_i915_private *i915) {}
static inline void i915_pmu_unregister(struct drm_i915_private *i915) {}
static inline void i915_pmu_gt_parked(struct drm_i915_private *i915) {}
static inline void i915_pmu_gt_unparked(struct drm_i915_private *i915) {}
#endif

#endif
