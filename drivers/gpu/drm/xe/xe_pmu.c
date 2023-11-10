// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "regs/xe_gt_regs.h"
#include "xe_device.h"
#include "xe_gt_clock.h"
#include "xe_mmio.h"

static cpumask_t xe_pmu_cpumask;
static unsigned int xe_pmu_target_cpu = -1;

static unsigned int config_gt_id(const u64 config)
{
	return config >> __XE_PMU_GT_SHIFT;
}

static u64 config_counter(const u64 config)
{
	return config & ~(~0ULL << __XE_PMU_GT_SHIFT);
}

static void xe_pmu_event_destroy(struct perf_event *event)
{
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);

	drm_WARN_ON(&xe->drm, event->parent);

	drm_dev_put(&xe->drm);
}

static u64 __engine_group_busyness_read(struct xe_gt *gt, int sample_type)
{
	u64 val;

	switch (sample_type) {
	case __XE_SAMPLE_RENDER_GROUP_BUSY:
		val = xe_mmio_read32(gt, XE_OAG_RENDER_BUSY_FREE);
		break;
	case __XE_SAMPLE_COPY_GROUP_BUSY:
		val = xe_mmio_read32(gt, XE_OAG_BLT_BUSY_FREE);
		break;
	case __XE_SAMPLE_MEDIA_GROUP_BUSY:
		val = xe_mmio_read32(gt, XE_OAG_ANY_MEDIA_FF_BUSY_FREE);
		break;
	case __XE_SAMPLE_ANY_ENGINE_GROUP_BUSY:
		val = xe_mmio_read32(gt, XE_OAG_RC0_ANY_ENGINE_BUSY_FREE);
		break;
	default:
		drm_warn(&gt->tile->xe->drm, "unknown pmu event\n");
	}

	return xe_gt_clock_cycles_to_ns(gt, val * 16);
}

static u64 engine_group_busyness_read(struct xe_gt *gt, u64 config)
{
	int sample_type = config_counter(config);
	const unsigned int gt_id = gt->info.id;
	struct xe_device *xe = gt->tile->xe;
	struct xe_pmu *pmu = &xe->pmu;
	unsigned long flags;
	bool device_awake;
	u64 val;

	device_awake = xe_device_mem_access_get_if_ongoing(xe);
	if (device_awake) {
		XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FW_GT));
		val = __engine_group_busyness_read(gt, sample_type);
		XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FW_GT));
		xe_device_mem_access_put(xe);
	}

	spin_lock_irqsave(&pmu->lock, flags);

	if (device_awake)
		pmu->sample[gt_id][sample_type] = val;
	else
		val = pmu->sample[gt_id][sample_type];

	spin_unlock_irqrestore(&pmu->lock, flags);

	return val;
}

static void engine_group_busyness_store(struct xe_gt *gt)
{
	struct xe_pmu *pmu = &gt->tile->xe->pmu;
	unsigned int gt_id = gt->info.id;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pmu->lock, flags);

	for (i = __XE_SAMPLE_RENDER_GROUP_BUSY; i <= __XE_SAMPLE_ANY_ENGINE_GROUP_BUSY; i++)
		pmu->sample[gt_id][i] = __engine_group_busyness_read(gt, i);

	spin_unlock_irqrestore(&pmu->lock, flags);
}

static int
config_status(struct xe_device *xe, u64 config)
{
	unsigned int gt_id = config_gt_id(config);
	struct xe_gt *gt = xe_device_get_gt(xe, gt_id);

	if (gt_id >= XE_PMU_MAX_GT)
		return -ENOENT;

	switch (config_counter(config)) {
	case XE_PMU_RENDER_GROUP_BUSY(0):
	case XE_PMU_COPY_GROUP_BUSY(0):
	case XE_PMU_ANY_ENGINE_GROUP_BUSY(0):
		if (gt->info.type == XE_GT_TYPE_MEDIA)
			return -ENOENT;
		break;
	case XE_PMU_MEDIA_GROUP_BUSY(0):
		if (!(gt->info.engine_mask & (BIT(XE_HW_ENGINE_VCS0) | BIT(XE_HW_ENGINE_VECS0))))
			return -ENOENT;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int xe_pmu_event_init(struct perf_event *event)
{
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;
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
	if (!cpumask_test_cpu(event->cpu, &xe_pmu_cpumask))
		return -EINVAL;

	ret = config_status(xe, event->attr.config);
	if (ret)
		return ret;

	if (!event->parent) {
		drm_dev_get(&xe->drm);
		event->destroy = xe_pmu_event_destroy;
	}

	return 0;
}

static u64 __xe_pmu_event_read(struct perf_event *event)
{
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);
	const unsigned int gt_id = config_gt_id(event->attr.config);
	const u64 config = event->attr.config;
	struct xe_gt *gt = xe_device_get_gt(xe, gt_id);
	u64 val;

	switch (config_counter(config)) {
	case XE_PMU_RENDER_GROUP_BUSY(0):
	case XE_PMU_COPY_GROUP_BUSY(0):
	case XE_PMU_ANY_ENGINE_GROUP_BUSY(0):
	case XE_PMU_MEDIA_GROUP_BUSY(0):
		val = engine_group_busyness_read(gt, config);
		break;
	default:
		drm_warn(&gt->tile->xe->drm, "unknown pmu event\n");
	}

	return val;
}

static void xe_pmu_event_read(struct perf_event *event)
{
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);
	struct hw_perf_event *hwc = &event->hw;
	struct xe_pmu *pmu = &xe->pmu;
	u64 prev, new;

	if (pmu->closed) {
		event->hw.state = PERF_HES_STOPPED;
		return;
	}
again:
	prev = local64_read(&hwc->prev_count);
	new = __xe_pmu_event_read(event);

	if (local64_cmpxchg(&hwc->prev_count, prev, new) != prev)
		goto again;

	local64_add(new - prev, &event->count);
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
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (pmu->closed)
		return;

	xe_pmu_enable(event);
	event->hw.state = 0;
}

static void xe_pmu_event_stop(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_UPDATE)
		xe_pmu_event_read(event);

	event->hw.state = PERF_HES_STOPPED;
}

static int xe_pmu_event_add(struct perf_event *event, int flags)
{
	struct xe_device *xe =
		container_of(event->pmu, typeof(*xe), pmu.base);
	struct xe_pmu *pmu = &xe->pmu;

	if (pmu->closed)
		return -ENODEV;

	if (flags & PERF_EF_START)
		xe_pmu_event_start(event, flags);

	return 0;
}

static void xe_pmu_event_del(struct perf_event *event, int flags)
{
	xe_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int xe_pmu_event_event_idx(struct perf_event *event)
{
	return 0;
}

struct xe_ext_attribute {
	struct device_attribute attr;
	unsigned long val;
};

static ssize_t xe_pmu_event_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct xe_ext_attribute *eattr;

	eattr = container_of(attr, struct xe_ext_attribute, attr);
	return sprintf(buf, "config=0x%lx\n", eattr->val);
}

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &xe_pmu_cpumask);
}

static DEVICE_ATTR_RO(cpumask);

static struct attribute *xe_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group xe_pmu_cpumask_attr_group = {
	.attrs = xe_cpumask_attrs,
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

static struct xe_ext_attribute *
add_xe_attr(struct xe_ext_attribute *attr, const char *name, u64 config)
{
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = xe_pmu_event_show;
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
create_event_attributes(struct xe_pmu *pmu)
{
	struct xe_device *xe = container_of(pmu, typeof(*xe), pmu);
	static const struct {
		unsigned int counter;
		const char *name;
		const char *unit;
		bool global;
	} events[] = {
		__event(0, "render-group-busy", "ns"),
		__event(1, "copy-group-busy", "ns"),
		__event(2, "media-group-busy", "ns"),
		__event(3, "any-engine-group-busy", "ns"),
	};

	struct perf_pmu_events_attr *pmu_attr = NULL, *pmu_iter;
	struct xe_ext_attribute *xe_attr = NULL, *xe_iter;
	struct attribute **attr = NULL, **attr_iter;
	unsigned int count = 0;
	unsigned int i, j;
	struct xe_gt *gt;

	/* Count how many counters we will be exposing. */
	for_each_gt(gt, xe, j) {
		for (i = 0; i < ARRAY_SIZE(events); i++) {
			u64 config = ___XE_PMU_OTHER(j, events[i].counter);

			if (!config_status(xe, config))
				count++;
		}
	}

	/* Allocate attribute objects and table. */
	xe_attr = kcalloc(count, sizeof(*xe_attr), GFP_KERNEL);
	if (!xe_attr)
		goto err_alloc;

	pmu_attr = kcalloc(count, sizeof(*pmu_attr), GFP_KERNEL);
	if (!pmu_attr)
		goto err_alloc;

	/* Max one pointer of each attribute type plus a termination entry. */
	attr = kcalloc(count * 2 + 1, sizeof(*attr), GFP_KERNEL);
	if (!attr)
		goto err_alloc;

	xe_iter = xe_attr;
	pmu_iter = pmu_attr;
	attr_iter = attr;

	for_each_gt(gt, xe, j) {
		for (i = 0; i < ARRAY_SIZE(events); i++) {
			u64 config = ___XE_PMU_OTHER(j, events[i].counter);
			char *str;

			if (config_status(xe, config))
				continue;

			if (events[i].global)
				str = kstrdup(events[i].name, GFP_KERNEL);
			else
				str = kasprintf(GFP_KERNEL, "%s-gt%u",
						events[i].name, j);
			if (!str)
				goto err;

			*attr_iter++ = &xe_iter->attr.attr;
			xe_iter = add_xe_attr(xe_iter, str, config);

			if (events[i].unit) {
				if (events[i].global)
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

	pmu->xe_attr = xe_attr;
	pmu->pmu_attr = pmu_attr;

	return attr;

err:
	for (attr_iter = attr; *attr_iter; attr_iter++)
		kfree((*attr_iter)->name);

err_alloc:
	kfree(attr);
	kfree(xe_attr);
	kfree(pmu_attr);

	return NULL;
}

static void free_event_attributes(struct xe_pmu *pmu)
{
	struct attribute **attr_iter = pmu->events_attr_group.attrs;

	for (; *attr_iter; attr_iter++)
		kfree((*attr_iter)->name);

	kfree(pmu->events_attr_group.attrs);
	kfree(pmu->xe_attr);
	kfree(pmu->pmu_attr);

	pmu->events_attr_group.attrs = NULL;
	pmu->xe_attr = NULL;
	pmu->pmu_attr = NULL;
}

static int xe_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct xe_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), cpuhp.node);

	/* Select the first online CPU as a designated reader. */
	if (cpumask_empty(&xe_pmu_cpumask))
		cpumask_set_cpu(cpu, &xe_pmu_cpumask);

	return 0;
}

static int xe_pmu_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct xe_pmu *pmu = hlist_entry_safe(node, typeof(*pmu), cpuhp.node);
	unsigned int target = xe_pmu_target_cpu;

	/*
	 * Unregistering an instance generates a CPU offline event which we must
	 * ignore to avoid incorrectly modifying the shared xe_pmu_cpumask.
	 */
	if (pmu->closed)
		return 0;

	if (cpumask_test_and_clear_cpu(cpu, &xe_pmu_cpumask)) {
		target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);

		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &xe_pmu_cpumask);
			xe_pmu_target_cpu = target;
		}
	}

	if (target < nr_cpu_ids && target != pmu->cpuhp.cpu) {
		perf_pmu_migrate_context(&pmu->base, cpu, target);
		pmu->cpuhp.cpu = target;
	}

	return 0;
}

static enum cpuhp_state cpuhp_slot = CPUHP_INVALID;

int xe_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/x86/intel/xe:online",
				      xe_pmu_cpu_online,
				      xe_pmu_cpu_offline);
	if (ret < 0)
		pr_notice("Failed to setup cpuhp state for xe PMU! (%d)\n",
			  ret);
	else
		cpuhp_slot = ret;

	return 0;
}

void xe_pmu_exit(void)
{
	if (cpuhp_slot != CPUHP_INVALID)
		cpuhp_remove_multi_state(cpuhp_slot);
}

static int xe_pmu_register_cpuhp_state(struct xe_pmu *pmu)
{
	if (cpuhp_slot == CPUHP_INVALID)
		return -EINVAL;

	return cpuhp_state_add_instance(cpuhp_slot, &pmu->cpuhp.node);
}

static void xe_pmu_unregister_cpuhp_state(struct xe_pmu *pmu)
{
	cpuhp_state_remove_instance(cpuhp_slot, &pmu->cpuhp.node);
}

void xe_pmu_suspend(struct xe_gt *gt)
{
	engine_group_busyness_store(gt);
}

static void xe_pmu_unregister(struct drm_device *device, void *arg)
{
	struct xe_pmu *pmu = arg;

	if (!pmu->base.event_init)
		return;

	/*
	 * "Disconnect" the PMU callbacks - since all are atomic synchronize_rcu
	 * ensures all currently executing ones will have exited before we
	 * proceed with unregistration.
	 */
	pmu->closed = true;
	synchronize_rcu();

	xe_pmu_unregister_cpuhp_state(pmu);

	perf_pmu_unregister(&pmu->base);
	pmu->base.event_init = NULL;
	kfree(pmu->base.attr_groups);
	kfree(pmu->name);
	free_event_attributes(pmu);
}

void xe_pmu_register(struct xe_pmu *pmu)
{
	struct xe_device *xe = container_of(pmu, typeof(*xe), pmu);
	const struct attribute_group *attr_groups[] = {
		&pmu->events_attr_group,
		&xe_pmu_cpumask_attr_group,
		NULL
	};

	int ret = -ENOMEM;

	spin_lock_init(&pmu->lock);
	pmu->cpuhp.cpu = -1;

	pmu->name = kasprintf(GFP_KERNEL,
			      "xe_%s",
			      dev_name(xe->drm.dev));
	if (pmu->name)
		/* tools/perf reserves colons as special. */
		strreplace((char *)pmu->name, ':', '_');

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
	pmu->base.event_init	= xe_pmu_event_init;
	pmu->base.add		= xe_pmu_event_add;
	pmu->base.del		= xe_pmu_event_del;
	pmu->base.start		= xe_pmu_event_start;
	pmu->base.stop		= xe_pmu_event_stop;
	pmu->base.read		= xe_pmu_event_read;
	pmu->base.event_idx	= xe_pmu_event_event_idx;

	ret = perf_pmu_register(&pmu->base, pmu->name, -1);
	if (ret)
		goto err_groups;

	ret = xe_pmu_register_cpuhp_state(pmu);
	if (ret)
		goto err_unreg;

	ret = drmm_add_action_or_reset(&xe->drm, xe_pmu_unregister, pmu);
	if (ret)
		goto err_cpuhp;

	return;

err_cpuhp:
	xe_pmu_unregister_cpuhp_state(pmu);
err_unreg:
	perf_pmu_unregister(&pmu->base);
err_groups:
	kfree(pmu->base.attr_groups);
err_attr:
	pmu->base.event_init = NULL;
	free_event_attributes(pmu);
err_name:
	kfree(pmu->name);
err:
	drm_notice(&xe->drm, "Failed to register PMU!\n");
}
