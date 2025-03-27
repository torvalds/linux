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
struct intel_gt;

/*
 * Non-engine events that we need to track enabled-disabled transition and
 * current state.
 */
enum i915_pmu_tracked_events {
	__I915_PMU_ACTUAL_FREQUENCY_ENABLED = 0,
	__I915_PMU_REQUESTED_FREQUENCY_ENABLED,
	__I915_PMU_RC6_RESIDENCY_ENABLED,
	__I915_PMU_TRACKED_EVENT_COUNT, /* count marker */
};

/*
 * Slots used from the sampling timer (non-engine events) with some extras for
 * convenience.
 */
enum {
	__I915_SAMPLE_FREQ_ACT = 0,
	__I915_SAMPLE_FREQ_REQ,
	__I915_SAMPLE_RC6,
	__I915_SAMPLE_RC6_LAST_REPORTED,
	__I915_NUM_PMU_SAMPLERS
};

#define I915_PMU_MAX_GT 2

/*
 * How many different events we track in the global PMU mask.
 *
 * It is also used to know to needed number of event reference counters.
 */
#define I915_PMU_MASK_BITS \
	(I915_ENGINE_SAMPLE_COUNT + \
	 I915_PMU_MAX_GT * __I915_PMU_TRACKED_EVENT_COUNT)

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
	 * @registered: PMU is registered and not in the unregistering process.
	 */
	bool registered;
	/**
	 * @name: Name as registered with perf core.
	 */
	const char *name;
	/**
	 * @lock: Lock protecting enable mask and ref count handling.
	 */
	spinlock_t lock;
	/**
	 * @unparked: GT unparked mask.
	 */
	unsigned int unparked;
	/**
	 * @timer: Timer for internal i915 PMU sampling.
	 */
	struct hrtimer timer;
	/**
	 * @enable: Bitmask of specific enabled events.
	 *
	 * For some events we need to track their state and do some internal
	 * house keeping.
	 *
	 * Each engine event sampler type and event listed in enum
	 * i915_pmu_tracked_events gets a bit in this field.
	 *
	 * Low bits are engine samplers and other events continue from there.
	 */
	u32 enable;

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
	struct i915_pmu_sample sample[I915_PMU_MAX_GT][__I915_NUM_PMU_SAMPLERS];
	/**
	 * @sleep_last: Last time GT parked for RC6 estimation.
	 */
	ktime_t sleep_last[I915_PMU_MAX_GT];
	/**
	 * @irq_count: Number of interrupts
	 *
	 * Intentionally unsigned long to avoid atomics or heuristics on 32bit.
	 * 4e9 interrupts are a lot and postprocessing can really deal with an
	 * occasional wraparound easily. It's 32bit after all.
	 */
	unsigned long irq_count;
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
int i915_pmu_init(void);
void i915_pmu_exit(void);
void i915_pmu_register(struct drm_i915_private *i915);
void i915_pmu_unregister(struct drm_i915_private *i915);
void i915_pmu_gt_parked(struct intel_gt *gt);
void i915_pmu_gt_unparked(struct intel_gt *gt);
#else
static inline int i915_pmu_init(void) { return 0; }
static inline void i915_pmu_exit(void) {}
static inline void i915_pmu_register(struct drm_i915_private *i915) {}
static inline void i915_pmu_unregister(struct drm_i915_private *i915) {}
static inline void i915_pmu_gt_parked(struct intel_gt *gt) {}
static inline void i915_pmu_gt_unparked(struct intel_gt *gt) {}
#endif

#endif
