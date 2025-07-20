// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Performance monitoring support for Virtual Processor Area(VPA) based counters
 *
 * Copyright (C) 2024 IBM Corporation
 */
#define pr_fmt(fmt) "vpa_pmu: " fmt

#include <linux/module.h>
#include <linux/perf_event.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s_64.h>

#define MODULE_VERS "1.0"
#define MODULE_NAME "pseries_vpa_pmu"

#define EVENT(_name, _code)     enum{_name = _code}

#define VPA_PMU_EVENT_VAR(_id)  event_attr_##_id
#define VPA_PMU_EVENT_PTR(_id)  (&event_attr_##_id.attr.attr)

static ssize_t vpa_pmu_events_sysfs_show(struct device *dev,
					 struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);

	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define VPA_PMU_EVENT_ATTR(_name, _id)				\
	PMU_EVENT_ATTR(_name, VPA_PMU_EVENT_VAR(_id), _id,	\
			vpa_pmu_events_sysfs_show)

EVENT(L1_TO_L2_CS_LAT,	0x1);
EVENT(L2_TO_L1_CS_LAT,	0x2);
EVENT(L2_RUNTIME_AGG,	0x3);

VPA_PMU_EVENT_ATTR(l1_to_l2_lat,  L1_TO_L2_CS_LAT);
VPA_PMU_EVENT_ATTR(l2_to_l1_lat,  L2_TO_L1_CS_LAT);
VPA_PMU_EVENT_ATTR(l2_runtime_agg, L2_RUNTIME_AGG);

static struct attribute *vpa_pmu_events_attr[] = {
	VPA_PMU_EVENT_PTR(L1_TO_L2_CS_LAT),
	VPA_PMU_EVENT_PTR(L2_TO_L1_CS_LAT),
	VPA_PMU_EVENT_PTR(L2_RUNTIME_AGG),
	NULL
};

static const struct attribute_group vpa_pmu_events_group = {
	.name = "events",
	.attrs = vpa_pmu_events_attr,
};

PMU_FORMAT_ATTR(event, "config:0-31");
static struct attribute *vpa_pmu_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group vpa_pmu_format_group = {
	.name = "format",
	.attrs = vpa_pmu_format_attr,
};

static const struct attribute_group *vpa_pmu_attr_groups[] = {
	&vpa_pmu_events_group,
	&vpa_pmu_format_group,
	NULL
};

static int vpa_pmu_event_init(struct perf_event *event)
{
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* it does not support event sampling mode */
	if (is_sampling_event(event))
		return -EOPNOTSUPP;

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	/* Invalid event code */
	if ((event->attr.config <= 0) || (event->attr.config > 3))
		return -EINVAL;

	return 0;
}

static unsigned long get_counter_data(struct perf_event *event)
{
	unsigned int config = event->attr.config;
	u64 data;

	switch (config) {
	case L1_TO_L2_CS_LAT:
		if (event->attach_state & PERF_ATTACH_TASK)
			data = kvmhv_get_l1_to_l2_cs_time_vcpu();
		else
			data = kvmhv_get_l1_to_l2_cs_time();
		break;
	case L2_TO_L1_CS_LAT:
		if (event->attach_state & PERF_ATTACH_TASK)
			data = kvmhv_get_l2_to_l1_cs_time_vcpu();
		else
			data = kvmhv_get_l2_to_l1_cs_time();
		break;
	case L2_RUNTIME_AGG:
		if (event->attach_state & PERF_ATTACH_TASK)
			data = kvmhv_get_l2_runtime_agg_vcpu();
		else
			data = kvmhv_get_l2_runtime_agg();
		break;
	default:
		data = 0;
		break;
	}

	return data;
}

static int vpa_pmu_add(struct perf_event *event, int flags)
{
	u64 data;

	kvmhv_set_l2_counters_status(smp_processor_id(), true);

	data = get_counter_data(event);
	local64_set(&event->hw.prev_count, data);

	return 0;
}

static void vpa_pmu_read(struct perf_event *event)
{
	u64 prev_data, new_data, final_data;

	prev_data = local64_read(&event->hw.prev_count);
	new_data = get_counter_data(event);
	final_data = new_data - prev_data;

	local64_add(final_data, &event->count);
}

static void vpa_pmu_del(struct perf_event *event, int flags)
{
	vpa_pmu_read(event);

	/*
	 * Disable vpa counter accumulation
	 */
	kvmhv_set_l2_counters_status(smp_processor_id(), false);
}

static struct pmu vpa_pmu = {
	.module		= THIS_MODULE,
	.task_ctx_nr	= perf_sw_context,
	.name		= "vpa_pmu",
	.event_init	= vpa_pmu_event_init,
	.add		= vpa_pmu_add,
	.del		= vpa_pmu_del,
	.read		= vpa_pmu_read,
	.attr_groups	= vpa_pmu_attr_groups,
	.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
};

static int __init pseries_vpa_pmu_init(void)
{
	/*
	 * List of current Linux on Power platforms and
	 * this driver is supported only in PowerVM LPAR
	 * (L1) platform.
	 *
	 *	Enabled    Linux on Power Platforms
	 *      ----------------------------------------
	 *        [X]      PowerVM LPAR (L1)
	 *        [ ]      KVM Guest On PowerVM KoP(L2)
	 *        [ ]      Baremetal(PowerNV)
	 *        [ ]      KVM Guest On PowerNV
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR) || is_kvm_guest())
		return -ENODEV;

	perf_pmu_register(&vpa_pmu, vpa_pmu.name, -1);
	pr_info("Virtual Processor Area PMU registered.\n");

	return 0;
}

static void __exit pseries_vpa_pmu_cleanup(void)
{
	perf_pmu_unregister(&vpa_pmu);
	pr_info("Virtual Processor Area PMU unregistered.\n");
}

module_init(pseries_vpa_pmu_init);
module_exit(pseries_vpa_pmu_cleanup);
MODULE_DESCRIPTION("Perf Driver for pSeries VPA pmu counter");
MODULE_AUTHOR("Kajol Jain <kjain@linux.ibm.com>");
MODULE_AUTHOR("Madhavan Srinivasan <maddy@linux.ibm.com>");
MODULE_LICENSE("GPL");
