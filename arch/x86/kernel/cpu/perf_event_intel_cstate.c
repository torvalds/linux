/*
 * perf_event_intel_cstate.c: support cstate residency counters
 *
 * Copyright (C) 2015, Intel Corp.
 * Author: Kan Liang (kan.liang@intel.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 */

/*
 * This file export cstate related free running (read-only) counters
 * for perf. These counters may be use simultaneously by other tools,
 * such as turbostat. However, it still make sense to implement them
 * in perf. Because we can conveniently collect them together with
 * other events, and allow to use them from tools without special MSR
 * access code.
 *
 * The events only support system-wide mode counting. There is no
 * sampling support because it is not supported by the hardware.
 *
 * According to counters' scope and category, two PMUs are registered
 * with the perf_event core subsystem.
 *  - 'cstate_core': The counter is available for each physical core.
 *    The counters include CORE_C*_RESIDENCY.
 *  - 'cstate_pkg': The counter is available for each physical package.
 *    The counters include PKG_C*_RESIDENCY.
 *
 * All of these counters are specified in the Intel® 64 and IA-32
 * Architectures Software Developer.s Manual Vol3b.
 *
 * Model specific counters:
 *	MSR_CORE_C1_RES: CORE C1 Residency Counter
 *			 perf code: 0x00
 *			 Available model: SLM,AMT
 *			 Scope: Core (each processor core has a MSR)
 *	MSR_CORE_C3_RESIDENCY: CORE C3 Residency Counter
 *			       perf code: 0x01
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL
 *			       Scope: Core
 *	MSR_CORE_C6_RESIDENCY: CORE C6 Residency Counter
 *			       perf code: 0x02
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW,SKL
 *			       Scope: Core
 *	MSR_CORE_C7_RESIDENCY: CORE C7 Residency Counter
 *			       perf code: 0x03
 *			       Available model: SNB,IVB,HSW,BDW,SKL
 *			       Scope: Core
 *	MSR_PKG_C2_RESIDENCY:  Package C2 Residency Counter.
 *			       perf code: 0x00
 *			       Available model: SNB,IVB,HSW,BDW,SKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C3_RESIDENCY:  Package C3 Residency Counter.
 *			       perf code: 0x01
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C6_RESIDENCY:  Package C6 Residency Counter.
 *			       perf code: 0x02
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW,SKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C7_RESIDENCY:  Package C7 Residency Counter.
 *			       perf code: 0x03
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C8_RESIDENCY:  Package C8 Residency Counter.
 *			       perf code: 0x04
 *			       Available model: HSW ULT only
 *			       Scope: Package (physical package)
 *	MSR_PKG_C9_RESIDENCY:  Package C9 Residency Counter.
 *			       perf code: 0x05
 *			       Available model: HSW ULT only
 *			       Scope: Package (physical package)
 *	MSR_PKG_C10_RESIDENCY: Package C10 Residency Counter.
 *			       perf code: 0x06
 *			       Available model: HSW ULT only
 *			       Scope: Package (physical package)
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/nospec.h>
#include <asm/cpu_device_id.h>
#include "perf_event.h"

#define DEFINE_CSTATE_FORMAT_ATTR(_var, _name, _format)		\
static ssize_t __cstate_##_var##_show(struct kobject *kobj,	\
				struct kobj_attribute *attr,	\
				char *page)			\
{								\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);		\
	return sprintf(page, _format "\n");			\
}								\
static struct kobj_attribute format_attr_##_var =		\
	__ATTR(_name, 0444, __cstate_##_var##_show, NULL)

static ssize_t cstate_get_attr_cpumask(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);

struct perf_cstate_msr {
	u64	msr;
	struct	perf_pmu_events_attr *attr;
	bool	(*test)(int idx);
};


/* cstate_core PMU */

static struct pmu cstate_core_pmu;
static bool has_cstate_core;

enum perf_cstate_core_id {
	/*
	 * cstate_core events
	 */
	PERF_CSTATE_CORE_C1_RES = 0,
	PERF_CSTATE_CORE_C3_RES,
	PERF_CSTATE_CORE_C6_RES,
	PERF_CSTATE_CORE_C7_RES,

	PERF_CSTATE_CORE_EVENT_MAX,
};

bool test_core(int idx)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return false;

	switch (boot_cpu_data.x86_model) {
	case 30: /* 45nm Nehalem    */
	case 26: /* 45nm Nehalem-EP */
	case 46: /* 45nm Nehalem-EX */

	case 37: /* 32nm Westmere    */
	case 44: /* 32nm Westmere-EP */
	case 47: /* 32nm Westmere-EX */
		if (idx == PERF_CSTATE_CORE_C3_RES ||
		    idx == PERF_CSTATE_CORE_C6_RES)
			return true;
		break;
	case 42: /* 32nm SandyBridge         */
	case 45: /* 32nm SandyBridge-E/EN/EP */

	case 58: /* 22nm IvyBridge       */
	case 62: /* 22nm IvyBridge-EP/EX */

	case 60: /* 22nm Haswell Core */
	case 63: /* 22nm Haswell Server */
	case 69: /* 22nm Haswell ULT */
	case 70: /* 22nm Haswell + GT3e (Intel Iris Pro graphics) */

	case 61: /* 14nm Broadwell Core-M */
	case 86: /* 14nm Broadwell Xeon D */
	case 71: /* 14nm Broadwell + GT3e (Intel Iris Pro graphics) */
	case 79: /* 14nm Broadwell Server */

	case 78: /* 14nm Skylake Mobile */
	case 94: /* 14nm Skylake Desktop */
		if (idx == PERF_CSTATE_CORE_C3_RES ||
		    idx == PERF_CSTATE_CORE_C6_RES ||
		    idx == PERF_CSTATE_CORE_C7_RES)
			return true;
		break;
	case 55: /* 22nm Atom "Silvermont"                */
	case 77: /* 22nm Atom "Silvermont Avoton/Rangely" */
	case 76: /* 14nm Atom "Airmont"                   */
		if (idx == PERF_CSTATE_CORE_C1_RES ||
		    idx == PERF_CSTATE_CORE_C6_RES)
			return true;
		break;
	}

	return false;
}

PMU_EVENT_ATTR_STRING(c1-residency, evattr_cstate_core_c1, "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency, evattr_cstate_core_c3, "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency, evattr_cstate_core_c6, "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency, evattr_cstate_core_c7, "event=0x03");

static struct perf_cstate_msr core_msr[] = {
	[PERF_CSTATE_CORE_C1_RES] = { MSR_CORE_C1_RES,		&evattr_cstate_core_c1,	test_core, },
	[PERF_CSTATE_CORE_C3_RES] = { MSR_CORE_C3_RESIDENCY,	&evattr_cstate_core_c3, test_core, },
	[PERF_CSTATE_CORE_C6_RES] = { MSR_CORE_C6_RESIDENCY,	&evattr_cstate_core_c6, test_core, },
	[PERF_CSTATE_CORE_C7_RES] = { MSR_CORE_C7_RESIDENCY,	&evattr_cstate_core_c7,	test_core, },
};

static struct attribute *core_events_attrs[PERF_CSTATE_CORE_EVENT_MAX + 1] = {
	NULL,
};

static struct attribute_group core_events_attr_group = {
	.name = "events",
	.attrs = core_events_attrs,
};

DEFINE_CSTATE_FORMAT_ATTR(core_event, event, "config:0-63");
static struct attribute *core_format_attrs[] = {
	&format_attr_core_event.attr,
	NULL,
};

static struct attribute_group core_format_attr_group = {
	.name = "format",
	.attrs = core_format_attrs,
};

static cpumask_t cstate_core_cpu_mask;
static DEVICE_ATTR(cpumask, S_IRUGO, cstate_get_attr_cpumask, NULL);

static struct attribute *cstate_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group cpumask_attr_group = {
	.attrs = cstate_cpumask_attrs,
};

static const struct attribute_group *core_attr_groups[] = {
	&core_events_attr_group,
	&core_format_attr_group,
	&cpumask_attr_group,
	NULL,
};

/* cstate_core PMU end */


/* cstate_pkg PMU */

static struct pmu cstate_pkg_pmu;
static bool has_cstate_pkg;

enum perf_cstate_pkg_id {
	/*
	 * cstate_pkg events
	 */
	PERF_CSTATE_PKG_C2_RES = 0,
	PERF_CSTATE_PKG_C3_RES,
	PERF_CSTATE_PKG_C6_RES,
	PERF_CSTATE_PKG_C7_RES,
	PERF_CSTATE_PKG_C8_RES,
	PERF_CSTATE_PKG_C9_RES,
	PERF_CSTATE_PKG_C10_RES,

	PERF_CSTATE_PKG_EVENT_MAX,
};

bool test_pkg(int idx)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return false;

	switch (boot_cpu_data.x86_model) {
	case 30: /* 45nm Nehalem    */
	case 26: /* 45nm Nehalem-EP */
	case 46: /* 45nm Nehalem-EX */

	case 37: /* 32nm Westmere    */
	case 44: /* 32nm Westmere-EP */
	case 47: /* 32nm Westmere-EX */
		if (idx == PERF_CSTATE_CORE_C3_RES ||
		    idx == PERF_CSTATE_CORE_C6_RES ||
		    idx == PERF_CSTATE_CORE_C7_RES)
			return true;
		break;
	case 42: /* 32nm SandyBridge         */
	case 45: /* 32nm SandyBridge-E/EN/EP */

	case 58: /* 22nm IvyBridge       */
	case 62: /* 22nm IvyBridge-EP/EX */

	case 60: /* 22nm Haswell Core */
	case 63: /* 22nm Haswell Server */
	case 70: /* 22nm Haswell + GT3e (Intel Iris Pro graphics) */

	case 61: /* 14nm Broadwell Core-M */
	case 86: /* 14nm Broadwell Xeon D */
	case 71: /* 14nm Broadwell + GT3e (Intel Iris Pro graphics) */
	case 79: /* 14nm Broadwell Server */

	case 78: /* 14nm Skylake Mobile */
	case 94: /* 14nm Skylake Desktop */
		if (idx == PERF_CSTATE_PKG_C2_RES ||
		    idx == PERF_CSTATE_PKG_C3_RES ||
		    idx == PERF_CSTATE_PKG_C6_RES ||
		    idx == PERF_CSTATE_PKG_C7_RES)
			return true;
		break;
	case 55: /* 22nm Atom "Silvermont"                */
	case 77: /* 22nm Atom "Silvermont Avoton/Rangely" */
	case 76: /* 14nm Atom "Airmont"                   */
		if (idx == PERF_CSTATE_CORE_C6_RES)
			return true;
		break;
	case 69: /* 22nm Haswell ULT */
		if (idx == PERF_CSTATE_PKG_C2_RES ||
		    idx == PERF_CSTATE_PKG_C3_RES ||
		    idx == PERF_CSTATE_PKG_C6_RES ||
		    idx == PERF_CSTATE_PKG_C7_RES ||
		    idx == PERF_CSTATE_PKG_C8_RES ||
		    idx == PERF_CSTATE_PKG_C9_RES ||
		    idx == PERF_CSTATE_PKG_C10_RES)
			return true;
		break;
	}

	return false;
}

PMU_EVENT_ATTR_STRING(c2-residency, evattr_cstate_pkg_c2, "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency, evattr_cstate_pkg_c3, "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency, evattr_cstate_pkg_c6, "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency, evattr_cstate_pkg_c7, "event=0x03");
PMU_EVENT_ATTR_STRING(c8-residency, evattr_cstate_pkg_c8, "event=0x04");
PMU_EVENT_ATTR_STRING(c9-residency, evattr_cstate_pkg_c9, "event=0x05");
PMU_EVENT_ATTR_STRING(c10-residency, evattr_cstate_pkg_c10, "event=0x06");

static struct perf_cstate_msr pkg_msr[] = {
	[PERF_CSTATE_PKG_C2_RES] = { MSR_PKG_C2_RESIDENCY,	&evattr_cstate_pkg_c2,	test_pkg, },
	[PERF_CSTATE_PKG_C3_RES] = { MSR_PKG_C3_RESIDENCY,	&evattr_cstate_pkg_c3,	test_pkg, },
	[PERF_CSTATE_PKG_C6_RES] = { MSR_PKG_C6_RESIDENCY,	&evattr_cstate_pkg_c6,	test_pkg, },
	[PERF_CSTATE_PKG_C7_RES] = { MSR_PKG_C7_RESIDENCY,	&evattr_cstate_pkg_c7,	test_pkg, },
	[PERF_CSTATE_PKG_C8_RES] = { MSR_PKG_C8_RESIDENCY,	&evattr_cstate_pkg_c8,	test_pkg, },
	[PERF_CSTATE_PKG_C9_RES] = { MSR_PKG_C9_RESIDENCY,	&evattr_cstate_pkg_c9,	test_pkg, },
	[PERF_CSTATE_PKG_C10_RES] = { MSR_PKG_C10_RESIDENCY,	&evattr_cstate_pkg_c10,	test_pkg, },
};

static struct attribute *pkg_events_attrs[PERF_CSTATE_PKG_EVENT_MAX + 1] = {
	NULL,
};

static struct attribute_group pkg_events_attr_group = {
	.name = "events",
	.attrs = pkg_events_attrs,
};

DEFINE_CSTATE_FORMAT_ATTR(pkg_event, event, "config:0-63");
static struct attribute *pkg_format_attrs[] = {
	&format_attr_pkg_event.attr,
	NULL,
};
static struct attribute_group pkg_format_attr_group = {
	.name = "format",
	.attrs = pkg_format_attrs,
};

static cpumask_t cstate_pkg_cpu_mask;

static const struct attribute_group *pkg_attr_groups[] = {
	&pkg_events_attr_group,
	&pkg_format_attr_group,
	&cpumask_attr_group,
	NULL,
};

/* cstate_pkg PMU end*/

static ssize_t cstate_get_attr_cpumask(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);

	if (pmu == &cstate_core_pmu)
		return cpumap_print_to_pagebuf(true, buf, &cstate_core_cpu_mask);
	else if (pmu == &cstate_pkg_pmu)
		return cpumap_print_to_pagebuf(true, buf, &cstate_pkg_cpu_mask);
	else
		return 0;
}

static int cstate_pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config;
	int ret = 0;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest  ||
	    event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (event->pmu == &cstate_core_pmu) {
		if (cfg >= PERF_CSTATE_CORE_EVENT_MAX)
			return -EINVAL;
		if (!core_msr[cfg].attr)
			return -EINVAL;
		event->hw.event_base = core_msr[cfg].msr;
	} else if (event->pmu == &cstate_pkg_pmu) {
		if (cfg >= PERF_CSTATE_PKG_EVENT_MAX)
			return -EINVAL;
		cfg = array_index_nospec((unsigned long)cfg, PERF_CSTATE_PKG_EVENT_MAX);
		if (!pkg_msr[cfg].attr)
			return -EINVAL;
		event->hw.event_base = pkg_msr[cfg].msr;
	} else
		return -ENOENT;

	/* must be done before validate_group */
	event->hw.config = cfg;
	event->hw.idx = -1;

	return ret;
}

static inline u64 cstate_pmu_read_counter(struct perf_event *event)
{
	u64 val;

	rdmsrl(event->hw.event_base, val);
	return val;
}

static void cstate_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = cstate_pmu_read_counter(event);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			    new_raw_count) != prev_raw_count)
		goto again;

	local64_add(new_raw_count - prev_raw_count, &event->count);
}

static void cstate_pmu_event_start(struct perf_event *event, int mode)
{
	local64_set(&event->hw.prev_count, cstate_pmu_read_counter(event));
}

static void cstate_pmu_event_stop(struct perf_event *event, int mode)
{
	cstate_pmu_event_update(event);
}

static void cstate_pmu_event_del(struct perf_event *event, int mode)
{
	cstate_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int cstate_pmu_event_add(struct perf_event *event, int mode)
{
	if (mode & PERF_EF_START)
		cstate_pmu_event_start(event, mode);

	return 0;
}

static void cstate_cpu_exit(int cpu)
{
	int i, id, target;

	/* cpu exit for cstate core */
	if (has_cstate_core) {
		id = topology_core_id(cpu);
		target = -1;

		for_each_online_cpu(i) {
			if (i == cpu)
				continue;
			if (id == topology_core_id(i)) {
				target = i;
				break;
			}
		}
		if (cpumask_test_and_clear_cpu(cpu, &cstate_core_cpu_mask) && target >= 0)
			cpumask_set_cpu(target, &cstate_core_cpu_mask);
		WARN_ON(cpumask_empty(&cstate_core_cpu_mask));
		if (target >= 0)
			perf_pmu_migrate_context(&cstate_core_pmu, cpu, target);
	}

	/* cpu exit for cstate pkg */
	if (has_cstate_pkg) {
		id = topology_physical_package_id(cpu);
		target = -1;

		for_each_online_cpu(i) {
			if (i == cpu)
				continue;
			if (id == topology_physical_package_id(i)) {
				target = i;
				break;
			}
		}
		if (cpumask_test_and_clear_cpu(cpu, &cstate_pkg_cpu_mask) && target >= 0)
			cpumask_set_cpu(target, &cstate_pkg_cpu_mask);
		WARN_ON(cpumask_empty(&cstate_pkg_cpu_mask));
		if (target >= 0)
			perf_pmu_migrate_context(&cstate_pkg_pmu, cpu, target);
	}
}

static void cstate_cpu_init(int cpu)
{
	int i, id;

	/* cpu init for cstate core */
	if (has_cstate_core) {
		id = topology_core_id(cpu);
		for_each_cpu(i, &cstate_core_cpu_mask) {
			if (id == topology_core_id(i))
				break;
		}
		if (i >= nr_cpu_ids)
			cpumask_set_cpu(cpu, &cstate_core_cpu_mask);
	}

	/* cpu init for cstate pkg */
	if (has_cstate_pkg) {
		id = topology_physical_package_id(cpu);
		for_each_cpu(i, &cstate_pkg_cpu_mask) {
			if (id == topology_physical_package_id(i))
				break;
		}
		if (i >= nr_cpu_ids)
			cpumask_set_cpu(cpu, &cstate_pkg_cpu_mask);
	}
}

static int cstate_cpu_notifier(struct notifier_block *self,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		break;
	case CPU_STARTING:
		cstate_cpu_init(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_DYING:
		break;
	case CPU_ONLINE:
	case CPU_DEAD:
		break;
	case CPU_DOWN_PREPARE:
		cstate_cpu_exit(cpu);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

/*
 * Probe the cstate events and insert the available one into sysfs attrs
 * Return false if there is no available events.
 */
static bool cstate_probe_msr(struct perf_cstate_msr *msr,
			     struct attribute	**events_attrs,
			     int max_event_nr)
{
	int i, j = 0;
	u64 val;

	/* Probe the cstate events. */
	for (i = 0; i < max_event_nr; i++) {
		if (!msr[i].test(i) || rdmsrl_safe(msr[i].msr, &val))
			msr[i].attr = NULL;
	}

	/* List remaining events in the sysfs attrs. */
	for (i = 0; i < max_event_nr; i++) {
		if (msr[i].attr)
			events_attrs[j++] = &msr[i].attr->attr.attr;
	}
	events_attrs[j] = NULL;

	return (j > 0) ? true : false;
}

static int __init cstate_init(void)
{
	/* SLM has different MSR for PKG C6 */
	switch (boot_cpu_data.x86_model) {
	case 55:
	case 76:
	case 77:
		pkg_msr[PERF_CSTATE_PKG_C6_RES].msr = MSR_PKG_C7_RESIDENCY;
	}

	if (cstate_probe_msr(core_msr, core_events_attrs, PERF_CSTATE_CORE_EVENT_MAX))
		has_cstate_core = true;

	if (cstate_probe_msr(pkg_msr, pkg_events_attrs, PERF_CSTATE_PKG_EVENT_MAX))
		has_cstate_pkg = true;

	return (has_cstate_core || has_cstate_pkg) ? 0 : -ENODEV;
}

static void __init cstate_cpumask_init(void)
{
	int cpu;

	cpu_notifier_register_begin();

	for_each_online_cpu(cpu)
		cstate_cpu_init(cpu);

	__perf_cpu_notifier(cstate_cpu_notifier);

	cpu_notifier_register_done();
}

static struct pmu cstate_core_pmu = {
	.attr_groups	= core_attr_groups,
	.name		= "cstate_core",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add, /* must have */
	.del		= cstate_pmu_event_del, /* must have */
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
};

static struct pmu cstate_pkg_pmu = {
	.attr_groups	= pkg_attr_groups,
	.name		= "cstate_pkg",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add, /* must have */
	.del		= cstate_pmu_event_del, /* must have */
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
};

static void __init cstate_pmus_register(void)
{
	int err;

	if (has_cstate_core) {
		err = perf_pmu_register(&cstate_core_pmu, cstate_core_pmu.name, -1);
		if (WARN_ON(err))
			pr_info("Failed to register PMU %s error %d\n",
				cstate_core_pmu.name, err);
	}

	if (has_cstate_pkg) {
		err = perf_pmu_register(&cstate_pkg_pmu, cstate_pkg_pmu.name, -1);
		if (WARN_ON(err))
			pr_info("Failed to register PMU %s error %d\n",
				cstate_pkg_pmu.name, err);
	}
}

static int __init cstate_pmu_init(void)
{
	int err;

	if (cpu_has_hypervisor)
		return -ENODEV;

	err = cstate_init();
	if (err)
		return err;

	cstate_cpumask_init();

	cstate_pmus_register();

	return 0;
}

device_initcall(cstate_pmu_init);
