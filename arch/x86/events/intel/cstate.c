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
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW
 *						SKL,KNL
 *			       Scope: Core
 *	MSR_CORE_C7_RESIDENCY: CORE C7 Residency Counter
 *			       perf code: 0x03
 *			       Available model: SNB,IVB,HSW,BDW,SKL
 *			       Scope: Core
 *	MSR_PKG_C2_RESIDENCY:  Package C2 Residency Counter.
 *			       perf code: 0x00
 *			       Available model: SNB,IVB,HSW,BDW,SKL,KNL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C3_RESIDENCY:  Package C3 Residency Counter.
 *			       perf code: 0x01
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL,KNL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C6_RESIDENCY:  Package C6 Residency Counter.
 *			       perf code: 0x02
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW
 *						SKL,KNL
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
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include "../perf_event.h"

MODULE_LICENSE("GPL");

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

/* Model -> events mapping */
struct cstate_model {
	unsigned long		core_events;
	unsigned long		pkg_events;
	unsigned long		quirks;
};

/* Quirk flags */
#define SLM_PKG_C6_USE_C7_MSR	(1UL << 0)
#define KNL_CORE_C6_MSR		(1UL << 1)

struct perf_cstate_msr {
	u64	msr;
	struct	perf_pmu_events_attr *attr;
};


/* cstate_core PMU */
static struct pmu cstate_core_pmu;
static bool has_cstate_core;

enum perf_cstate_core_events {
	PERF_CSTATE_CORE_C1_RES = 0,
	PERF_CSTATE_CORE_C3_RES,
	PERF_CSTATE_CORE_C6_RES,
	PERF_CSTATE_CORE_C7_RES,

	PERF_CSTATE_CORE_EVENT_MAX,
};

PMU_EVENT_ATTR_STRING(c1-residency, evattr_cstate_core_c1, "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency, evattr_cstate_core_c3, "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency, evattr_cstate_core_c6, "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency, evattr_cstate_core_c7, "event=0x03");

static struct perf_cstate_msr core_msr[] = {
	[PERF_CSTATE_CORE_C1_RES] = { MSR_CORE_C1_RES,		&evattr_cstate_core_c1 },
	[PERF_CSTATE_CORE_C3_RES] = { MSR_CORE_C3_RESIDENCY,	&evattr_cstate_core_c3 },
	[PERF_CSTATE_CORE_C6_RES] = { MSR_CORE_C6_RESIDENCY,	&evattr_cstate_core_c6 },
	[PERF_CSTATE_CORE_C7_RES] = { MSR_CORE_C7_RESIDENCY,	&evattr_cstate_core_c7 },
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

/* cstate_pkg PMU */
static struct pmu cstate_pkg_pmu;
static bool has_cstate_pkg;

enum perf_cstate_pkg_events {
	PERF_CSTATE_PKG_C2_RES = 0,
	PERF_CSTATE_PKG_C3_RES,
	PERF_CSTATE_PKG_C6_RES,
	PERF_CSTATE_PKG_C7_RES,
	PERF_CSTATE_PKG_C8_RES,
	PERF_CSTATE_PKG_C9_RES,
	PERF_CSTATE_PKG_C10_RES,

	PERF_CSTATE_PKG_EVENT_MAX,
};

PMU_EVENT_ATTR_STRING(c2-residency, evattr_cstate_pkg_c2, "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency, evattr_cstate_pkg_c3, "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency, evattr_cstate_pkg_c6, "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency, evattr_cstate_pkg_c7, "event=0x03");
PMU_EVENT_ATTR_STRING(c8-residency, evattr_cstate_pkg_c8, "event=0x04");
PMU_EVENT_ATTR_STRING(c9-residency, evattr_cstate_pkg_c9, "event=0x05");
PMU_EVENT_ATTR_STRING(c10-residency, evattr_cstate_pkg_c10, "event=0x06");

static struct perf_cstate_msr pkg_msr[] = {
	[PERF_CSTATE_PKG_C2_RES] = { MSR_PKG_C2_RESIDENCY,	&evattr_cstate_pkg_c2 },
	[PERF_CSTATE_PKG_C3_RES] = { MSR_PKG_C3_RESIDENCY,	&evattr_cstate_pkg_c3 },
	[PERF_CSTATE_PKG_C6_RES] = { MSR_PKG_C6_RESIDENCY,	&evattr_cstate_pkg_c6 },
	[PERF_CSTATE_PKG_C7_RES] = { MSR_PKG_C7_RESIDENCY,	&evattr_cstate_pkg_c7 },
	[PERF_CSTATE_PKG_C8_RES] = { MSR_PKG_C8_RESIDENCY,	&evattr_cstate_pkg_c8 },
	[PERF_CSTATE_PKG_C9_RES] = { MSR_PKG_C9_RESIDENCY,	&evattr_cstate_pkg_c9 },
	[PERF_CSTATE_PKG_C10_RES] = { MSR_PKG_C10_RESIDENCY,	&evattr_cstate_pkg_c10 },
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
	int cpu;

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

	if (event->cpu < 0)
		return -EINVAL;

	if (event->pmu == &cstate_core_pmu) {
		if (cfg >= PERF_CSTATE_CORE_EVENT_MAX)
			return -EINVAL;
		if (!core_msr[cfg].attr)
			return -EINVAL;
		event->hw.event_base = core_msr[cfg].msr;
		cpu = cpumask_any_and(&cstate_core_cpu_mask,
				      topology_sibling_cpumask(event->cpu));
	} else if (event->pmu == &cstate_pkg_pmu) {
		if (cfg >= PERF_CSTATE_PKG_EVENT_MAX)
			return -EINVAL;
		if (!pkg_msr[cfg].attr)
			return -EINVAL;
		event->hw.event_base = pkg_msr[cfg].msr;
		cpu = cpumask_any_and(&cstate_pkg_cpu_mask,
				      topology_core_cpumask(event->cpu));
	} else {
		return -ENOENT;
	}

	if (cpu >= nr_cpu_ids)
		return -ENODEV;

	event->cpu = cpu;
	event->hw.config = cfg;
	event->hw.idx = -1;
	return 0;
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

/*
 * Check if exiting cpu is the designated reader. If so migrate the
 * events when there is a valid target available
 */
static int cstate_cpu_exit(unsigned int cpu)
{
	unsigned int target;

	if (has_cstate_core &&
	    cpumask_test_and_clear_cpu(cpu, &cstate_core_cpu_mask)) {

		target = cpumask_any_but(topology_sibling_cpumask(cpu), cpu);
		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &cstate_core_cpu_mask);
			perf_pmu_migrate_context(&cstate_core_pmu, cpu, target);
		}
	}

	if (has_cstate_pkg &&
	    cpumask_test_and_clear_cpu(cpu, &cstate_pkg_cpu_mask)) {

		target = cpumask_any_but(topology_core_cpumask(cpu), cpu);
		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &cstate_pkg_cpu_mask);
			perf_pmu_migrate_context(&cstate_pkg_pmu, cpu, target);
		}
	}
	return 0;
}

static int cstate_cpu_init(unsigned int cpu)
{
	unsigned int target;

	/*
	 * If this is the first online thread of that core, set it in
	 * the core cpu mask as the designated reader.
	 */
	target = cpumask_any_and(&cstate_core_cpu_mask,
				 topology_sibling_cpumask(cpu));

	if (has_cstate_core && target >= nr_cpu_ids)
		cpumask_set_cpu(cpu, &cstate_core_cpu_mask);

	/*
	 * If this is the first online thread of that package, set it
	 * in the package cpu mask as the designated reader.
	 */
	target = cpumask_any_and(&cstate_pkg_cpu_mask,
				 topology_core_cpumask(cpu));
	if (has_cstate_pkg && target >= nr_cpu_ids)
		cpumask_set_cpu(cpu, &cstate_pkg_cpu_mask);

	return 0;
}

static struct pmu cstate_core_pmu = {
	.attr_groups	= core_attr_groups,
	.name		= "cstate_core",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add,
	.del		= cstate_pmu_event_del,
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
	.module		= THIS_MODULE,
};

static struct pmu cstate_pkg_pmu = {
	.attr_groups	= pkg_attr_groups,
	.name		= "cstate_pkg",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add,
	.del		= cstate_pmu_event_del,
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
	.module		= THIS_MODULE,
};

static const struct cstate_model nhm_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C3_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C7_RES),
};

static const struct cstate_model snb_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C3_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES) |
				  BIT(PERF_CSTATE_CORE_C7_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C7_RES),
};

static const struct cstate_model hswult_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C3_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES) |
				  BIT(PERF_CSTATE_CORE_C7_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C7_RES) |
				  BIT(PERF_CSTATE_PKG_C8_RES) |
				  BIT(PERF_CSTATE_PKG_C9_RES) |
				  BIT(PERF_CSTATE_PKG_C10_RES),
};

static const struct cstate_model slm_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C6_RES),
	.quirks			= SLM_PKG_C6_USE_C7_MSR,
};


static const struct cstate_model knl_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES),
	.quirks			= KNL_CORE_C6_MSR,
};



#define X86_CSTATES_MODEL(model, states)				\
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, (unsigned long) &(states) }

static const struct x86_cpu_id intel_cstates_match[] __initconst = {
	X86_CSTATES_MODEL(INTEL_FAM6_NEHALEM,    nhm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_NEHALEM_EP, nhm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_NEHALEM_EX, nhm_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_WESTMERE,    nhm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_WESTMERE_EP, nhm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_WESTMERE_EX, nhm_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_SANDYBRIDGE,   snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_SANDYBRIDGE_X, snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_IVYBRIDGE,   snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_IVYBRIDGE_X, snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_HASWELL_CORE, snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_HASWELL_X,	   snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_HASWELL_GT3E, snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_HASWELL_ULT, hswult_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_ATOM_SILVERMONT1, slm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_ATOM_SILVERMONT2, slm_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_ATOM_AIRMONT,     slm_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_BROADWELL_CORE,   snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_BROADWELL_XEON_D, snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_BROADWELL_GT3E,   snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_BROADWELL_X,      snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_SKYLAKE_MOBILE,  snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_SKYLAKE_DESKTOP, snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_KABYLAKE_MOBILE,  snb_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_KABYLAKE_DESKTOP, snb_cstates),

	X86_CSTATES_MODEL(INTEL_FAM6_XEON_PHI_KNL, knl_cstates),
	X86_CSTATES_MODEL(INTEL_FAM6_XEON_PHI_KNM, knl_cstates),
	{ },
};
MODULE_DEVICE_TABLE(x86cpu, intel_cstates_match);

/*
 * Probe the cstate events and insert the available one into sysfs attrs
 * Return false if there are no available events.
 */
static bool __init cstate_probe_msr(const unsigned long evmsk, int max,
                                   struct perf_cstate_msr *msr,
                                   struct attribute **attrs)
{
	bool found = false;
	unsigned int bit;
	u64 val;

	for (bit = 0; bit < max; bit++) {
		if (test_bit(bit, &evmsk) && !rdmsrl_safe(msr[bit].msr, &val)) {
			*attrs++ = &msr[bit].attr->attr.attr;
			found = true;
		} else {
			msr[bit].attr = NULL;
		}
	}
	*attrs = NULL;

	return found;
}

static int __init cstate_probe(const struct cstate_model *cm)
{
	/* SLM has different MSR for PKG C6 */
	if (cm->quirks & SLM_PKG_C6_USE_C7_MSR)
		pkg_msr[PERF_CSTATE_PKG_C6_RES].msr = MSR_PKG_C7_RESIDENCY;

	/* KNL has different MSR for CORE C6 */
	if (cm->quirks & KNL_CORE_C6_MSR)
		pkg_msr[PERF_CSTATE_CORE_C6_RES].msr = MSR_KNL_CORE_C6_RESIDENCY;


	has_cstate_core = cstate_probe_msr(cm->core_events,
					   PERF_CSTATE_CORE_EVENT_MAX,
					   core_msr, core_events_attrs);

	has_cstate_pkg = cstate_probe_msr(cm->pkg_events,
					  PERF_CSTATE_PKG_EVENT_MAX,
					  pkg_msr, pkg_events_attrs);

	return (has_cstate_core || has_cstate_pkg) ? 0 : -ENODEV;
}

static inline void cstate_cleanup(void)
{
	cpuhp_remove_state_nocalls(CPUHP_AP_PERF_X86_CSTATE_ONLINE);
	cpuhp_remove_state_nocalls(CPUHP_AP_PERF_X86_CSTATE_STARTING);

	if (has_cstate_core)
		perf_pmu_unregister(&cstate_core_pmu);

	if (has_cstate_pkg)
		perf_pmu_unregister(&cstate_pkg_pmu);
}

static int __init cstate_init(void)
{
	int err;

	cpuhp_setup_state(CPUHP_AP_PERF_X86_CSTATE_STARTING,
			  "perf/x86/cstate:starting", cstate_cpu_init, NULL);
	cpuhp_setup_state(CPUHP_AP_PERF_X86_CSTATE_ONLINE,
			  "perf/x86/cstate:online", NULL, cstate_cpu_exit);

	if (has_cstate_core) {
		err = perf_pmu_register(&cstate_core_pmu, cstate_core_pmu.name, -1);
		if (err) {
			has_cstate_core = false;
			pr_info("Failed to register cstate core pmu\n");
			cstate_cleanup();
			return err;
		}
	}

	if (has_cstate_pkg) {
		err = perf_pmu_register(&cstate_pkg_pmu, cstate_pkg_pmu.name, -1);
		if (err) {
			has_cstate_pkg = false;
			pr_info("Failed to register cstate pkg pmu\n");
			cstate_cleanup();
			return err;
		}
	}
	return 0;
}

static int __init cstate_pmu_init(void)
{
	const struct x86_cpu_id *id;
	int err;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	id = x86_match_cpu(intel_cstates_match);
	if (!id)
		return -ENODEV;

	err = cstate_probe((const struct cstate_model *) id->driver_data);
	if (err)
		return err;

	return cstate_init();
}
module_init(cstate_pmu_init);

static void __exit cstate_pmu_exit(void)
{
	cstate_cleanup();
}
module_exit(cstate_pmu_exit);
