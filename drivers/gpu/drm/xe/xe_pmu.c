// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_drv.h>
#include <linux/device.h>

#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt_idle.h"
#include "xe_guc_engine_activity.h"
#include "xe_hw_engine.h"
#include "xe_pm.h"
#include "xe_pmu.h"

/**
 * DOC: Xe PMU (Performance Monitoring Unit)
 *
 * Expose events/counters like GT-C6 residency, GT frequency and per-class-engine
 * activity to user land via the perf interface. Events are per device.
 *
 * All events are listed in sysfs:
 *
 *     $ ls -ld /sys/bus/event_source/devices/xe_*
 *     $ ls /sys/bus/event_source/devices/xe_0000_00_02.0/events/
 *     $ ls /sys/bus/event_source/devices/xe_0000_00_02.0/format/
 *
 * The following format parameters are available to read events,
 * but only few are valid with each event:
 *
 *	gt[60:63]		Selects gt for the event
 *	engine_class[20:27]	Selects engine-class for event
 *	engine_instance[12:19]	Selects the engine-instance for the event
 *
 * For engine specific events (engine-*), gt, engine_class and engine_instance parameters must be
 * set as populated by DRM_XE_DEVICE_QUERY_ENGINES.
 *
 * For gt specific events (gt-*) gt parameter must be passed. All other parameters will be 0.
 *
 * The standard perf tool can be used to grep for a certain event as well.
 * Example:
 *
 *     $ perf list | grep gt-c6
 *
 * To sample a specific event for a GT at regular intervals:
 *
 *     $ perf stat -e <event_name,gt=> -I <interval>
 */

#define XE_PMU_EVENT_GT_MASK			GENMASK_ULL(63, 60)
#define XE_PMU_EVENT_ENGINE_CLASS_MASK		GENMASK_ULL(27, 20)
#define XE_PMU_EVENT_ENGINE_INSTANCE_MASK	GENMASK_ULL(19, 12)
#define XE_PMU_EVENT_ID_MASK			GENMASK_ULL(11, 0)

static unsigned int config_to_event_id(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_ID_MASK, config);
}

static unsigned int config_to_engine_class(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_ENGINE_CLASS_MASK, config);
}

static unsigned int config_to_engine_instance(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_ENGINE_INSTANCE_MASK, config);
}

static unsigned int config_to_gt_id(u64 config)
{
	return FIELD_GET(XE_PMU_EVENT_GT_MASK, config);
}

#define XE_PMU_EVENT_GT_C6_RESIDENCY		0x01
#define XE_PMU_EVENT_ENGINE_ACTIVE_TICKS	0x02
#define XE_PMU_EVENT_ENGINE_TOTAL_TICKS		0x03

static struct xe_gt *event_to_gt(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	u64 gt = config_to_gt_id(event->attr.config);

	return xe_device_get_gt(xe, gt);
}

static struct xe_hw_engine *event_to_hwe(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct drm_xe_engine_class_instance eci;
	u64 config = event->attr.config;
	struct xe_hw_engine *hwe;

	eci.engine_class = config_to_engine_class(config);
	eci.engine_instance = config_to_engine_instance(config);
	eci.gt_id = config_to_gt_id(config);

	hwe = xe_hw_engine_lookup(xe, eci);
	if (!hwe || xe_hw_engine_is_reserved(hwe))
		return NULL;

	return hwe;
}

static bool is_engine_event(u64 config)
{
	unsigned int event_id = config_to_event_id(config);

	return (event_id == XE_PMU_EVENT_ENGINE_TOTAL_TICKS ||
		event_id == XE_PMU_EVENT_ENGINE_ACTIVE_TICKS);
}

static bool event_gt_forcewake(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	u64 config = event->attr.config;
	struct xe_gt *gt;
	unsigned int *fw_ref;

	if (!is_engine_event(config))
		return true;

	gt = xe_device_get_gt(xe, config_to_gt_id(config));

	fw_ref = kzalloc(sizeof(*fw_ref), GFP_KERNEL);
	if (!fw_ref)
		return false;

	*fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!*fw_ref) {
		kfree(fw_ref);
		return false;
	}

	event->pmu_private = fw_ref;

	return true;
}

static bool event_supported(struct xe_pmu *pmu, unsigned int gt,
			    unsigned int id)
{
	if (gt >= XE_MAX_GT_PER_TILE)
		return false;

	return id < sizeof(pmu->supported_events) * BITS_PER_BYTE &&
		pmu->supported_events & BIT_ULL(id);
}

static bool event_param_valid(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	unsigned int engine_class, engine_instance;
	u64 config = event->attr.config;
	struct xe_gt *gt;

	gt = xe_device_get_gt(xe, config_to_gt_id(config));
	if (!gt)
		return false;

	engine_class = config_to_engine_class(config);
	engine_instance = config_to_engine_instance(config);

	switch (config_to_event_id(config)) {
	case XE_PMU_EVENT_GT_C6_RESIDENCY:
		if (engine_class || engine_instance)
			return false;
		break;
	case XE_PMU_EVENT_ENGINE_ACTIVE_TICKS:
	case XE_PMU_EVENT_ENGINE_TOTAL_TICKS:
		if (!event_to_hwe(event))
			return false;
		break;
	}

	return true;
}

static void xe_pmu_event_destroy(struct perf_event *event)
{
	struct xe_device *xe = container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_gt *gt;
	unsigned int *fw_ref = event->pmu_private;

	if (fw_ref) {
		gt = xe_device_get_gt(xe, config_to_gt_id(event->attr.config));
		xe_force_wake_put(gt_to_fw(gt), *fw_ref);
		kfree(fw_ref);
		event->pmu_private = NULL;
	}

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

	if (!event_param_valid(event))
		return -ENOENT;

	if (!event->parent) {
		drm_dev_get(&xe->drm);
		xe_pm_runtime_get(xe);
		if (!event_gt_forcewake(event)) {
			xe_pm_runtime_put(xe);
			drm_dev_put(&xe->drm);
			return -EINVAL;
		}
		event->destroy = xe_pmu_event_destroy;
	}

	return 0;
}

static u64 read_engine_events(struct xe_gt *gt, struct perf_event *event)
{
	struct xe_hw_engine *hwe;
	u64 val = 0;

	hwe = event_to_hwe(event);
	if (config_to_event_id(event->attr.config) == XE_PMU_EVENT_ENGINE_ACTIVE_TICKS)
		val = xe_guc_engine_activity_active_ticks(&gt->uc.guc, hwe);
	else
		val = xe_guc_engine_activity_total_ticks(&gt->uc.guc, hwe);

	return val;
}

static u64 __xe_pmu_event_read(struct perf_event *event)
{
	struct xe_gt *gt = event_to_gt(event);

	if (!gt)
		return 0;

	switch (config_to_event_id(event->attr.config)) {
	case XE_PMU_EVENT_GT_C6_RESIDENCY:
		return xe_gt_idle_residency_msec(&gt->gtidle);
	case XE_PMU_EVENT_ENGINE_ACTIVE_TICKS:
	case XE_PMU_EVENT_ENGINE_TOTAL_TICKS:
		return read_engine_events(gt, event);
	}

	return 0;
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

PMU_FORMAT_ATTR(gt,			"config:60-63");
PMU_FORMAT_ATTR(engine_class,		"config:20-27");
PMU_FORMAT_ATTR(engine_instance,	"config:12-19");
PMU_FORMAT_ATTR(event,			"config:0-11");

static struct attribute *pmu_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_engine_class.attr,
	&format_attr_engine_instance.attr,
	&format_attr_gt.attr,
	NULL,
};

static const struct attribute_group pmu_format_attr_group = {
	.name = "format",
	.attrs = pmu_format_attrs,
};

static ssize_t event_attr_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct perf_pmu_events_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_attr, attr);

	return sprintf(buf, "event=%#04llx\n", pmu_attr->id);
}

#define XE_EVENT_ATTR(name_, v_, id_)					\
	PMU_EVENT_ATTR(name_, pmu_event_ ## v_, id_, event_attr_show)

#define XE_EVENT_ATTR_UNIT(name_, v_, unit_)				\
	PMU_EVENT_ATTR_STRING(name_.unit, pmu_event_unit_ ## v_, unit_)

#define XE_EVENT_ATTR_GROUP(v_, id_, ...)				\
	static struct attribute *pmu_attr_ ##v_[] = {			\
		__VA_ARGS__,						\
		NULL							\
	};								\
	static umode_t is_visible_##v_(struct kobject *kobj,		\
				       struct attribute *attr, int idx) \
	{								\
		struct perf_pmu_events_attr *pmu_attr;			\
		struct xe_pmu *pmu;					\
									\
		pmu_attr = container_of(attr, typeof(*pmu_attr), attr.attr); \
		pmu = container_of(dev_get_drvdata(kobj_to_dev(kobj)),	\
				   typeof(*pmu), base);			\
									\
		return event_supported(pmu, 0, id_) ? attr->mode : 0;	\
	}								\
	static const struct attribute_group pmu_group_ ##v_ = {		\
		.name = "events",					\
		.attrs = pmu_attr_ ## v_,				\
		.is_visible = is_visible_ ## v_,			\
	}

#define XE_EVENT_ATTR_SIMPLE(name_, v_, id_, unit_)			\
	XE_EVENT_ATTR(name_, v_, id_)					\
	XE_EVENT_ATTR_UNIT(name_, v_, unit_)				\
	XE_EVENT_ATTR_GROUP(v_, id_, &pmu_event_ ##v_.attr.attr,	\
			    &pmu_event_unit_ ##v_.attr.attr)

#define XE_EVENT_ATTR_NOUNIT(name_, v_, id_)				\
	XE_EVENT_ATTR(name_, v_, id_)					\
	XE_EVENT_ATTR_GROUP(v_, id_, &pmu_event_ ##v_.attr.attr)

XE_EVENT_ATTR_SIMPLE(gt-c6-residency, gt_c6_residency, XE_PMU_EVENT_GT_C6_RESIDENCY, "ms");
XE_EVENT_ATTR_NOUNIT(engine-active-ticks, engine_active_ticks, XE_PMU_EVENT_ENGINE_ACTIVE_TICKS);
XE_EVENT_ATTR_NOUNIT(engine-total-ticks, engine_total_ticks, XE_PMU_EVENT_ENGINE_TOTAL_TICKS);

static struct attribute *pmu_empty_event_attrs[] = {
	/* Empty - all events are added as groups with .attr_update() */
	NULL,
};

static const struct attribute_group pmu_events_attr_group = {
	.name = "events",
	.attrs = pmu_empty_event_attrs,
};

static const struct attribute_group *pmu_events_attr_update[] = {
	&pmu_group_gt_c6_residency,
	&pmu_group_engine_active_ticks,
	&pmu_group_engine_total_ticks,
	NULL,
};

static void set_supported_events(struct xe_pmu *pmu)
{
	struct xe_device *xe = container_of(pmu, typeof(*xe), pmu);
	struct xe_gt *gt = xe_device_get_gt(xe, 0);

	if (!xe->info.skip_guc_pc)
		pmu->supported_events |= BIT_ULL(XE_PMU_EVENT_GT_C6_RESIDENCY);

	if (xe_guc_engine_activity_supported(&gt->uc.guc)) {
		pmu->supported_events |= BIT_ULL(XE_PMU_EVENT_ENGINE_ACTIVE_TICKS);
		pmu->supported_events |= BIT_ULL(XE_PMU_EVENT_ENGINE_TOTAL_TICKS);
	}
}

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
	pmu->base.attr_update	= pmu_events_attr_update;
	pmu->base.scope		= PERF_PMU_SCOPE_SYS_WIDE;
	pmu->base.module	= THIS_MODULE;
	pmu->base.task_ctx_nr	= perf_invalid_context;
	pmu->base.event_init	= xe_pmu_event_init;
	pmu->base.add		= xe_pmu_event_add;
	pmu->base.del		= xe_pmu_event_del;
	pmu->base.start		= xe_pmu_event_start;
	pmu->base.stop		= xe_pmu_event_stop;
	pmu->base.read		= xe_pmu_event_read;

	set_supported_events(pmu);

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
