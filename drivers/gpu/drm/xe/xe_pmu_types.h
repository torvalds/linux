/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PMU_TYPES_H_
#define _XE_PMU_TYPES_H_

#include <linux/perf_event.h>
#include <linux/spinlock_types.h>
#include <uapi/drm/xe_drm.h>

enum {
	__XE_SAMPLE_RENDER_GROUP_BUSY,
	__XE_SAMPLE_COPY_GROUP_BUSY,
	__XE_SAMPLE_MEDIA_GROUP_BUSY,
	__XE_SAMPLE_ANY_ENGINE_GROUP_BUSY,
	__XE_NUM_PMU_SAMPLERS
};

#define XE_PMU_MAX_GT 2

struct xe_pmu {
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
	 * @closed: xe is unregistering.
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
	 * @sample: Current and previous (raw) counters.
	 *
	 * These counters are updated when the device is awake.
	 *
	 */
	u64 sample[XE_PMU_MAX_GT][__XE_NUM_PMU_SAMPLERS];
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
	 * @xe_attr: Memory block holding device attributes.
	 */
	void *xe_attr;
	/**
	 * @pmu_attr: Memory block holding device attributes.
	 */
	void *pmu_attr;
};

#endif
