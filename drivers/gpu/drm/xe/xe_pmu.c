// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_drv.h>
#include <linux/device.h>

#include "xe_device.h"
#include "xe_pm.h"
#include "xe_pmu.h"

/**
 * DOC: Xe PMU (Performance Monitoring Unit)
 *
 * Expose events/counters like GT-C6 residency and GT frequency to user land via
 * the perf interface. Events are per device. The GT can be selected with an
 * extra config sub-field (bits 60-63).
 *
 * All events are listed in sysfs:
 *
 *     $ ls -ld /sys/bus/event_source/devices/xe_*
 *     $ ls /sys/bus/event_source/devices/xe_0000_00_02.0/events/
 *     $ ls /sys/bus/event_source/devices/xe_0000_00_02.0/format/
 *
 * The format directory has info regarding the configs that can be used.
 * The standard perf tool can be used to grep for a certain event as well.
 * Example:
 *
 *     $ perf list | grep gt-c6
 *
 * To sample a specific event for a GT at regular intervals:
 *
 *     $ perf stat -e <event_name,gt=> -I <interval>
 */

#define XE_PMU_EVENT_GT_MASK		GENMASK_ULL(63, 60)
#define XE_PMU_EVENT_ID_MASK		GENMASK_ULL(11, 0)

static unsigned int config_to_event_id(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_ID_MASK, config);
}

static unsigned int config_to_gt_id(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_GT_MASK, config);
}

static struct xe_gt *event_to_gt(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	u64 gt = config_to_gt_id(event->attr.config);

	return xe_device_get_gt(xe, gt);
}

static bool event_supported(struct xe_pmu *pmu, unsigned int gt,
			    unsigned int id)
{
	if (gt >= XE_MAX_GT_PER_TILE)
		return false;

	return false;
}

static void xe_pmu_event_destroy(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);

	drm_WARN_ON(&xe->drm, event->parent);
	xe_pm_runtime_put(xe);
	drm_dev_put(&xe->drm);
}

static int xe_pmu_event_init(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;
	unsigned int id, gt;

	if (!pmu->registered)
		return -ENODEV;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	gt = config_to_gt_id(event->attr.config);
	id = config_to_event_id(event->attr.config);
	if (!event_supported(pmu, gt, id))
		return -ENOENT;

	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	if (!event->parent) {
		drm_dev_get(&xe->drm);
		xe_pm_runtime_get(xe);
		event->destroy = xe_pmu_event_destroy;
	}

	return 0;
}

static u64 __xe_pmu_event_read(struct perf_event *event)
{
	struct xe_gt *gt = event_to_gt(event);
	u64 val = 0;

	if (!gt)
		return 0;

	return val;
}

static void xe_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, new;

	prev = local64_read(&hwc->prev_count);
	do {
		new = __xe_pmu_event_read(event);
	} while (!local64_try_cmpxchg(&hwc->prev_count, &prev, new));

	local64_add(new - prev, &event->count);
}

static void xe_pmu_event_read(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (!pmu->registered) {
		event->hw.state = PERF_HES_STOPPED;
		return;
	}

	xe_pmu_event_update(event);
}

static void xe_pmu_enable(struct perf_event *event)
{
	/*
	 * Store the current counter value so we can report the correct delta
	 * for all listeners. Even when the event was already enabled and has
	 * an existing non-zero value.
	 */
	local64_set(&event->hw.prev_count, __xe_pmu_event_read(event));
}

static void xe_pmu_event_start(struct perf_event *event, int flags)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (!pmu->registered)
		return;

	xe_pmu_enable(event);
	event->hw.state = 0;
}

static void xe_pmu_event_stop(struct perf_event *event, int flags)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (pmu->registered)
		if (flags & PERF_EF_UPDATE)
			xe_pmu_event_update(event);

	event->hw.state = PERF_HES_STOPPED;
}

static int xe_pmu_event_add(struct perf_event *event, int flags)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (!pmu->registered)
		return -ENODEV;

	if (flags & PERF_EF_START)
		xe_pmu_event_start(event, flags);

	return 0;
}

static void xe_pmu_event_del(struct perf_event *event, int flags)
{
	xe_pmu_event_stop(event, PERF_EF_UPDATE);
}

PMU_FORMAT_ATTR(gt,	"config:60-63");
PMU_FORMAT_ATTR(event,	"config:0-11");

static struct attribute *pmu_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_gt.attr,
	NULL,
};

static const struct attribute_group pmu_format_attr_group = {
	.name = "format",
	.attrs = pmu_format_attrs,
};

static struct attribute *pmu_event_attrs[] = {
	/* No events yet */
	NULL,
};

static const struct attribute_group pmu_events_attr_group = {
	.name = "events",
	.attrs = pmu_event_attrs,
};

/**
 * xe_pmu_unregister() - Remove/cleanup PMU registration
 * @arg: Ptr to pmu
 */
static void xe_pmu_unregister(void *arg)
{
	struct xe_pmu *pmu = arg;
	struct xe_device *xe = container_of(pmu, typeof(*xe), pmu);

	if (!pmu->registered)
		return;

	pmu->registered = false;

	perf_pmu_unregister(&pmu->base);
	kfree(pmu->name);
}

/**
 * xe_pmu_register() - Define basic PMU properties for Xe and add event callbacks.
 * @pmu: the PMU object
 *
 * Returns 0 on success and an appropriate error code otherwise
 */
int xe_pmu_register(struct xe_pmu *pmu)
{
	struct xe_device *xe = container_of(pmu, typeof(*xe), pmu);
	static const struct attribute_group *attr_groups[] = {
		&pmu_format_attr_group,
		&pmu_events_attr_group,
		NULL
	};
	int ret = -ENOMEM;
	char *name;

	BUILD_BUG_ON(XE_MAX_GT_PER_TILE != XE_PMU_MAX_GT);

	if (IS_SRIOV_VF(xe))
		return 0;

	name = kasprintf(GFP_KERNEL, "xe_%s",
			 dev_name(xe->drm.dev));
	if (!name)
		goto err;

	/* tools/perf reserves colons as special. */
	strreplace(name, ':', '_');

	pmu->name		= name;
	pmu->base.attr_groups	= attr_groups;
	pmu->base.scope		= PERF_PMU_SCOPE_SYS_WIDE;
	pmu->base.module	= THIS_MODULE;
	pmu->base.task_ctx_nr	= perf_invalid_context;
	pmu->base.event_init	= xe_pmu_event_init;
	pmu->base.add		= xe_pmu_event_add;
	pmu->base.del		= xe_pmu_event_del;
	pmu->base.start		= xe_pmu_event_start;
	pmu->base.stop		= xe_pmu_event_stop;
	pmu->base.read		= xe_pmu_event_read;

	ret = perf_pmu_register(&pmu->base, pmu->name, -1);
	if (ret)
		goto err_name;

	pmu->registered = true;

	return devm_add_action_or_reset(xe->drm.dev, xe_pmu_unregister, pmu);

err_name:
	kfree(name);
err:
	drm_err(&xe->drm, "Failed to register PMU (ret=%d)!\n", ret);

	return ret;
}
