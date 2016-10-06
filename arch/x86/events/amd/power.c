/*
 * Performance events - AMD Processor Power Reporting Mechanism
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Huang Rui <ray.huang@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <asm/cpu_device_id.h>
#include "../perf_event.h"

#define MSR_F15H_CU_PWR_ACCUMULATOR     0xc001007a
#define MSR_F15H_CU_MAX_PWR_ACCUMULATOR 0xc001007b
#define MSR_F15H_PTSC			0xc0010280

/* Event code: LSB 8 bits, passed in attr->config any other bit is reserved. */
#define AMD_POWER_EVENT_MASK		0xFFULL

/*
 * Accumulated power status counters.
 */
#define AMD_POWER_EVENTSEL_PKG		1

/*
 * The ratio of compute unit power accumulator sample period to the
 * PTSC period.
 */
static unsigned int cpu_pwr_sample_ratio;

/* Maximum accumulated power of a compute unit. */
static u64 max_cu_acc_power;

static struct pmu pmu_class;

/*
 * Accumulated power represents the sum of each compute unit's (CU) power
 * consumption. On any core of each CU we read the total accumulated power from
 * MSR_F15H_CU_PWR_ACCUMULATOR. cpu_mask represents CPU bit map of all cores
 * which are picked to measure the power for the CUs they belong to.
 */
static cpumask_t cpu_mask;

static void event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_pwr_acc, new_pwr_acc, prev_ptsc, new_ptsc;
	u64 delta, tdelta;

	prev_pwr_acc = hwc->pwr_acc;
	prev_ptsc = hwc->ptsc;
	rdmsrl(MSR_F15H_CU_PWR_ACCUMULATOR, new_pwr_acc);
	rdmsrl(MSR_F15H_PTSC, new_ptsc);

	/*
	 * Calculate the CU power consumption over a time period, the unit of
	 * final value (delta) is micro-Watts. Then add it to the event count.
	 */
	if (new_pwr_acc < prev_pwr_acc) {
		delta = max_cu_acc_power + new_pwr_acc;
		delta -= prev_pwr_acc;
	} else
		delta = new_pwr_acc - prev_pwr_acc;

	delta *= cpu_pwr_sample_ratio * 1000;
	tdelta = new_ptsc - prev_ptsc;

	do_div(delta, tdelta);
	local64_add(delta, &event->count);
}

static void __pmu_event_start(struct perf_event *event)
{
	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	event->hw.state = 0;

	rdmsrl(MSR_F15H_PTSC, event->hw.ptsc);
	rdmsrl(MSR_F15H_CU_PWR_ACCUMULATOR, event->hw.pwr_acc);
}

static void pmu_event_start(struct perf_event *event, int mode)
{
	__pmu_event_start(event);
}

static void pmu_event_stop(struct perf_event *event, int mode)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Mark event as deactivated and stopped. */
	if (!(hwc->state & PERF_HES_STOPPED))
		hwc->state |= PERF_HES_STOPPED;

	/* Check if software counter update is necessary. */
	if ((mode & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of an event
		 * that we are disabling:
		 */
		event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int pmu_event_add(struct perf_event *event, int mode)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (mode & PERF_EF_START)
		__pmu_event_start(event);

	return 0;
}

static void pmu_event_del(struct perf_event *event, int flags)
{
	pmu_event_stop(event, PERF_EF_UPDATE);
}

static int pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config & AMD_POWER_EVENT_MASK;

	/* Only look at AMD power events. */
	if (event->attr.type != pmu_class.type)
		return -ENOENT;

	/* Unsupported modes and filters. */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest  ||
	    /* no sampling */
	    event->attr.sample_period)
		return -EINVAL;

	if (cfg != AMD_POWER_EVENTSEL_PKG)
		return -EINVAL;

	return 0;
}

static void pmu_event_read(struct perf_event *event)
{
	event_update(event);
}

static ssize_t
get_attr_cpumask(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &cpu_mask);
}

static DEVICE_ATTR(cpumask, S_IRUGO, get_attr_cpumask, NULL);

static struct attribute *pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group pmu_attr_group = {
	.attrs = pmu_attrs,
};

/*
 * Currently it only supports to report the power of each
 * processor/package.
 */
EVENT_ATTR_STR(power-pkg, power_pkg, "event=0x01");

EVENT_ATTR_STR(power-pkg.unit, power_pkg_unit, "mWatts");

/* Convert the count from micro-Watts to milli-Watts. */
EVENT_ATTR_STR(power-pkg.scale, power_pkg_scale, "1.000000e-3");

static struct attribute *events_attr[] = {
	EVENT_PTR(power_pkg),
	EVENT_PTR(power_pkg_unit),
	EVENT_PTR(power_pkg_scale),
	NULL,
};

static struct attribute_group pmu_events_group = {
	.name	= "events",
	.attrs	= events_attr,
};

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *formats_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group pmu_format_group = {
	.name	= "format",
	.attrs	= formats_attr,
};

static const struct attribute_group *attr_groups[] = {
	&pmu_attr_group,
	&pmu_format_group,
	&pmu_events_group,
	NULL,
};

static struct pmu pmu_class = {
	.attr_groups	= attr_groups,
	/* system-wide only */
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= pmu_event_init,
	.add		= pmu_event_add,
	.del		= pmu_event_del,
	.start		= pmu_event_start,
	.stop		= pmu_event_stop,
	.read		= pmu_event_read,
};

static int power_cpu_exit(unsigned int cpu)
{
	int target;

	if (!cpumask_test_and_clear_cpu(cpu, &cpu_mask))
		return 0;

	/*
	 * Find a new CPU on the same compute unit, if was set in cpumask
	 * and still some CPUs on compute unit. Then migrate event and
	 * context to new CPU.
	 */
	target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);
	if (target < nr_cpumask_bits) {
		cpumask_set_cpu(target, &cpu_mask);
		perf_pmu_migrate_context(&pmu_class, cpu, target);
	}
	return 0;
}

static int power_cpu_init(unsigned int cpu)
{
	int target;

	/*
	 * 1) If any CPU is set at cpu_mask in the same compute unit, do
	 * nothing.
	 * 2) If no CPU is set at cpu_mask in the same compute unit,
	 * set current ONLINE CPU.
	 *
	 * Note: if there is a CPU aside of the new one already in the
	 * sibling mask, then it is also in cpu_mask.
	 */
	target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);
	if (target >= nr_cpumask_bits)
		cpumask_set_cpu(cpu, &cpu_mask);
	return 0;
}

static const struct x86_cpu_id cpu_match[] = {
	{ .vendor = X86_VENDOR_AMD, .family = 0x15 },
	{},
};

static int __init amd_power_pmu_init(void)
{
	int ret;

	if (!x86_match_cpu(cpu_match))
		return 0;

	if (!boot_cpu_has(X86_FEATURE_ACC_POWER))
		return -ENODEV;

	cpu_pwr_sample_ratio = cpuid_ecx(0x80000007);

	if (rdmsrl_safe(MSR_F15H_CU_MAX_PWR_ACCUMULATOR, &max_cu_acc_power)) {
		pr_err("Failed to read max compute unit power accumulator MSR\n");
		return -ENODEV;
	}


	cpuhp_setup_state(CPUHP_AP_PERF_X86_AMD_POWER_ONLINE,
			  "AP_PERF_X86_AMD_POWER_ONLINE",
			  power_cpu_init, power_cpu_exit);

	ret = perf_pmu_register(&pmu_class, "power", -1);
	if (WARN_ON(ret)) {
		pr_warn("AMD Power PMU registration failed\n");
		return ret;
	}

	pr_info("AMD Power PMU detected\n");
	return ret;
}
module_init(amd_power_pmu_init);

static void __exit amd_power_pmu_exit(void)
{
	cpuhp_remove_state_nocalls(CPUHP_AP_PERF_X86_AMD_POWER_ONLINE);
	perf_pmu_unregister(&pmu_class);
}
module_exit(amd_power_pmu_exit);

MODULE_AUTHOR("Huang Rui <ray.huang@amd.com>");
MODULE_DESCRIPTION("AMD Processor Power Reporting Mechanism");
MODULE_LICENSE("GPL v2");
