/*
 * Support cstate residency counters
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
 *			 Available model: SLM,AMT,GLM,CNL,ICX,TNT,ADL,RPL
 *					  MTL,SRF,GRR,ARL,LNL
 *			 Scope: Core (each processor core has a MSR)
 *	MSR_CORE_C3_RESIDENCY: CORE C3 Residency Counter
 *			       perf code: 0x01
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL,GLM,
 *						CNL,KBL,CML,TNT
 *			       Scope: Core
 *	MSR_CORE_C6_RESIDENCY: CORE C6 Residency Counter
 *			       perf code: 0x02
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW,
 *						SKL,KNL,GLM,CNL,KBL,CML,ICL,ICX,
 *						TGL,TNT,RKL,ADL,RPL,SPR,MTL,SRF,
 *						GRR,ARL,LNL
 *			       Scope: Core
 *	MSR_CORE_C7_RESIDENCY: CORE C7 Residency Counter
 *			       perf code: 0x03
 *			       Available model: SNB,IVB,HSW,BDW,SKL,CNL,KBL,CML,
 *						ICL,TGL,RKL,ADL,RPL,MTL,ARL,LNL
 *			       Scope: Core
 *	MSR_PKG_C2_RESIDENCY:  Package C2 Residency Counter.
 *			       perf code: 0x00
 *			       Available model: SNB,IVB,HSW,BDW,SKL,KNL,GLM,CNL,
 *						KBL,CML,ICL,ICX,TGL,TNT,RKL,ADL,
 *						RPL,SPR,MTL,ARL,LNL,SRF
 *			       Scope: Package (physical package)
 *	MSR_PKG_C3_RESIDENCY:  Package C3 Residency Counter.
 *			       perf code: 0x01
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL,KNL,
 *						GLM,CNL,KBL,CML,ICL,TGL,TNT,RKL,
 *						ADL,RPL,MTL,ARL,LNL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C6_RESIDENCY:  Package C6 Residency Counter.
 *			       perf code: 0x02
 *			       Available model: SLM,AMT,NHM,WSM,SNB,IVB,HSW,BDW,
 *						SKL,KNL,GLM,CNL,KBL,CML,ICL,ICX,
 *						TGL,TNT,RKL,ADL,RPL,SPR,MTL,SRF,
 *						ARL,LNL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C7_RESIDENCY:  Package C7 Residency Counter.
 *			       perf code: 0x03
 *			       Available model: NHM,WSM,SNB,IVB,HSW,BDW,SKL,CNL,
 *						KBL,CML,ICL,TGL,RKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C8_RESIDENCY:  Package C8 Residency Counter.
 *			       perf code: 0x04
 *			       Available model: HSW ULT,KBL,CNL,CML,ICL,TGL,RKL,
 *						ADL,RPL,MTL,ARL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C9_RESIDENCY:  Package C9 Residency Counter.
 *			       perf code: 0x05
 *			       Available model: HSW ULT,KBL,CNL,CML,ICL,TGL,RKL
 *			       Scope: Package (physical package)
 *	MSR_PKG_C10_RESIDENCY: Package C10 Residency Counter.
 *			       perf code: 0x06
 *			       Available model: HSW ULT,KBL,GLM,CNL,CML,ICL,TGL,
 *						TNT,RKL,ADL,RPL,MTL,ARL,LNL
 *			       Scope: Package (physical package)
 *	MSR_MODULE_C6_RES_MS:  Module C6 Residency Counter.
 *			       perf code: 0x00
 *			       Available model: SRF,GRR
 *			       Scope: A cluster of cores shared L2 cache
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/nospec.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include "../perf_event.h"
#include "../probe.h"

MODULE_DESCRIPTION("Support for Intel cstate performance events");
MODULE_LICENSE("GPL");

#define DEFINE_CSTATE_FORMAT_ATTR(_var, _name, _format)		\
static ssize_t __cstate_##_var##_show(struct device *dev,	\
				struct device_attribute *attr,	\
				char *page)			\
{								\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);		\
	return sprintf(page, _format "\n");			\
}								\
static struct device_attribute format_attr_##_var =		\
	__ATTR(_name, 0444, __cstate_##_var##_show, NULL)

/* Model -> events mapping */
struct cstate_model {
	unsigned long		core_events;
	unsigned long		pkg_events;
	unsigned long		module_events;
	unsigned long		quirks;
};

/* Quirk flags */
#define SLM_PKG_C6_USE_C7_MSR	(1UL << 0)
#define KNL_CORE_C6_MSR		(1UL << 1)

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

PMU_EVENT_ATTR_STRING(c1-residency, attr_cstate_core_c1, "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency, attr_cstate_core_c3, "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency, attr_cstate_core_c6, "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency, attr_cstate_core_c7, "event=0x03");

static unsigned long core_msr_mask;

PMU_EVENT_GROUP(events, cstate_core_c1);
PMU_EVENT_GROUP(events, cstate_core_c3);
PMU_EVENT_GROUP(events, cstate_core_c6);
PMU_EVENT_GROUP(events, cstate_core_c7);

static bool test_msr(int idx, void *data)
{
	return test_bit(idx, (unsigned long *) data);
}

static struct perf_msr core_msr[] = {
	[PERF_CSTATE_CORE_C1_RES] = { MSR_CORE_C1_RES,		&group_cstate_core_c1,	test_msr },
	[PERF_CSTATE_CORE_C3_RES] = { MSR_CORE_C3_RESIDENCY,	&group_cstate_core_c3,	test_msr },
	[PERF_CSTATE_CORE_C6_RES] = { MSR_CORE_C6_RESIDENCY,	&group_cstate_core_c6,	test_msr },
	[PERF_CSTATE_CORE_C7_RES] = { MSR_CORE_C7_RESIDENCY,	&group_cstate_core_c7,	test_msr },
};

static struct attribute *attrs_empty[] = {
	NULL,
};

/*
 * There are no default events, but we need to create
 * "events" group (with empty attrs) before updating
 * it with detected events.
 */
static struct attribute_group cstate_events_attr_group = {
	.name = "events",
	.attrs = attrs_empty,
};

DEFINE_CSTATE_FORMAT_ATTR(cstate_event, event, "config:0-63");
static struct attribute *cstate_format_attrs[] = {
	&format_attr_cstate_event.attr,
	NULL,
};

static struct attribute_group cstate_format_attr_group = {
	.name = "format",
	.attrs = cstate_format_attrs,
};

static const struct attribute_group *cstate_attr_groups[] = {
	&cstate_events_attr_group,
	&cstate_format_attr_group,
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

PMU_EVENT_ATTR_STRING(c2-residency,  attr_cstate_pkg_c2,  "event=0x00");
PMU_EVENT_ATTR_STRING(c3-residency,  attr_cstate_pkg_c3,  "event=0x01");
PMU_EVENT_ATTR_STRING(c6-residency,  attr_cstate_pkg_c6,  "event=0x02");
PMU_EVENT_ATTR_STRING(c7-residency,  attr_cstate_pkg_c7,  "event=0x03");
PMU_EVENT_ATTR_STRING(c8-residency,  attr_cstate_pkg_c8,  "event=0x04");
PMU_EVENT_ATTR_STRING(c9-residency,  attr_cstate_pkg_c9,  "event=0x05");
PMU_EVENT_ATTR_STRING(c10-residency, attr_cstate_pkg_c10, "event=0x06");

static unsigned long pkg_msr_mask;

PMU_EVENT_GROUP(events, cstate_pkg_c2);
PMU_EVENT_GROUP(events, cstate_pkg_c3);
PMU_EVENT_GROUP(events, cstate_pkg_c6);
PMU_EVENT_GROUP(events, cstate_pkg_c7);
PMU_EVENT_GROUP(events, cstate_pkg_c8);
PMU_EVENT_GROUP(events, cstate_pkg_c9);
PMU_EVENT_GROUP(events, cstate_pkg_c10);

static struct perf_msr pkg_msr[] = {
	[PERF_CSTATE_PKG_C2_RES]  = { MSR_PKG_C2_RESIDENCY,	&group_cstate_pkg_c2,	test_msr },
	[PERF_CSTATE_PKG_C3_RES]  = { MSR_PKG_C3_RESIDENCY,	&group_cstate_pkg_c3,	test_msr },
	[PERF_CSTATE_PKG_C6_RES]  = { MSR_PKG_C6_RESIDENCY,	&group_cstate_pkg_c6,	test_msr },
	[PERF_CSTATE_PKG_C7_RES]  = { MSR_PKG_C7_RESIDENCY,	&group_cstate_pkg_c7,	test_msr },
	[PERF_CSTATE_PKG_C8_RES]  = { MSR_PKG_C8_RESIDENCY,	&group_cstate_pkg_c8,	test_msr },
	[PERF_CSTATE_PKG_C9_RES]  = { MSR_PKG_C9_RESIDENCY,	&group_cstate_pkg_c9,	test_msr },
	[PERF_CSTATE_PKG_C10_RES] = { MSR_PKG_C10_RESIDENCY,	&group_cstate_pkg_c10,	test_msr },
};

/* cstate_module PMU */
static struct pmu cstate_module_pmu;
static bool has_cstate_module;

enum perf_cstate_module_events {
	PERF_CSTATE_MODULE_C6_RES = 0,

	PERF_CSTATE_MODULE_EVENT_MAX,
};

PMU_EVENT_ATTR_STRING(c6-residency, attr_cstate_module_c6, "event=0x00");

static unsigned long module_msr_mask;

PMU_EVENT_GROUP(events, cstate_module_c6);

static struct perf_msr module_msr[] = {
	[PERF_CSTATE_MODULE_C6_RES]  = { MSR_MODULE_C6_RES_MS,	&group_cstate_module_c6,	test_msr },
};

static int cstate_pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	if (event->pmu == &cstate_core_pmu) {
		if (cfg >= PERF_CSTATE_CORE_EVENT_MAX)
			return -EINVAL;
		cfg = array_index_nospec((unsigned long)cfg, PERF_CSTATE_CORE_EVENT_MAX);
		if (!(core_msr_mask & (1 << cfg)))
			return -EINVAL;
		event->hw.event_base = core_msr[cfg].msr;
	} else if (event->pmu == &cstate_pkg_pmu) {
		if (cfg >= PERF_CSTATE_PKG_EVENT_MAX)
			return -EINVAL;
		cfg = array_index_nospec((unsigned long)cfg, PERF_CSTATE_PKG_EVENT_MAX);
		if (!(pkg_msr_mask & (1 << cfg)))
			return -EINVAL;
		event->hw.event_base = pkg_msr[cfg].msr;
	} else if (event->pmu == &cstate_module_pmu) {
		if (cfg >= PERF_CSTATE_MODULE_EVENT_MAX)
			return -EINVAL;
		cfg = array_index_nospec((unsigned long)cfg, PERF_CSTATE_MODULE_EVENT_MAX);
		if (!(module_msr_mask & (1 << cfg)))
			return -EINVAL;
		event->hw.event_base = module_msr[cfg].msr;
	} else {
		return -ENOENT;
	}

	event->hw.config = cfg;
	event->hw.idx = -1;
	return 0;
}

static inline u64 cstate_pmu_read_counter(struct perf_event *event)
{
	u64 val;

	rdmsrq(event->hw.event_base, val);
	return val;
}

static void cstate_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;

	prev_raw_count = local64_read(&hwc->prev_count);
	do {
		new_raw_count = cstate_pmu_read_counter(event);
	} while (!local64_try_cmpxchg(&hwc->prev_count,
				      &prev_raw_count, new_raw_count));

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

static const struct attribute_group *core_attr_update[] = {
	&group_cstate_core_c1,
	&group_cstate_core_c3,
	&group_cstate_core_c6,
	&group_cstate_core_c7,
	NULL,
};

static const struct attribute_group *pkg_attr_update[] = {
	&group_cstate_pkg_c2,
	&group_cstate_pkg_c3,
	&group_cstate_pkg_c6,
	&group_cstate_pkg_c7,
	&group_cstate_pkg_c8,
	&group_cstate_pkg_c9,
	&group_cstate_pkg_c10,
	NULL,
};

static const struct attribute_group *module_attr_update[] = {
	&group_cstate_module_c6,
	NULL
};

static struct pmu cstate_core_pmu = {
	.attr_groups	= cstate_attr_groups,
	.attr_update	= core_attr_update,
	.name		= "cstate_core",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add,
	.del		= cstate_pmu_event_del,
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
	.scope		= PERF_PMU_SCOPE_CORE,
	.module		= THIS_MODULE,
};

static struct pmu cstate_pkg_pmu = {
	.attr_groups	= cstate_attr_groups,
	.attr_update	= pkg_attr_update,
	.name		= "cstate_pkg",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add,
	.del		= cstate_pmu_event_del,
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
	.scope		= PERF_PMU_SCOPE_PKG,
	.module		= THIS_MODULE,
};

static struct pmu cstate_module_pmu = {
	.attr_groups	= cstate_attr_groups,
	.attr_update	= module_attr_update,
	.name		= "cstate_module",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= cstate_pmu_event_init,
	.add		= cstate_pmu_event_add,
	.del		= cstate_pmu_event_del,
	.start		= cstate_pmu_event_start,
	.stop		= cstate_pmu_event_stop,
	.read		= cstate_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
	.scope		= PERF_PMU_SCOPE_CLUSTER,
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

static const struct cstate_model cnl_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C3_RES) |
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

static const struct cstate_model icl_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C6_RES) |
				  BIT(PERF_CSTATE_CORE_C7_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C7_RES) |
				  BIT(PERF_CSTATE_PKG_C8_RES) |
				  BIT(PERF_CSTATE_PKG_C9_RES) |
				  BIT(PERF_CSTATE_PKG_C10_RES),
};

static const struct cstate_model icx_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES),
};

static const struct cstate_model adl_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES) |
				  BIT(PERF_CSTATE_CORE_C7_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C8_RES) |
				  BIT(PERF_CSTATE_PKG_C10_RES),
};

static const struct cstate_model lnl_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES) |
				  BIT(PERF_CSTATE_CORE_C7_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
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


static const struct cstate_model glm_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C3_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C3_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES) |
				  BIT(PERF_CSTATE_PKG_C10_RES),
};

static const struct cstate_model grr_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.module_events		= BIT(PERF_CSTATE_MODULE_C6_RES),
};

static const struct cstate_model srf_cstates __initconst = {
	.core_events		= BIT(PERF_CSTATE_CORE_C1_RES) |
				  BIT(PERF_CSTATE_CORE_C6_RES),

	.pkg_events		= BIT(PERF_CSTATE_PKG_C2_RES) |
				  BIT(PERF_CSTATE_PKG_C6_RES),

	.module_events		= BIT(PERF_CSTATE_MODULE_C6_RES),
};


static const struct x86_cpu_id intel_cstates_match[] __initconst = {
	X86_MATCH_VFM(INTEL_NEHALEM,		&nhm_cstates),
	X86_MATCH_VFM(INTEL_NEHALEM_EP,		&nhm_cstates),
	X86_MATCH_VFM(INTEL_NEHALEM_EX,		&nhm_cstates),

	X86_MATCH_VFM(INTEL_WESTMERE,		&nhm_cstates),
	X86_MATCH_VFM(INTEL_WESTMERE_EP,	&nhm_cstates),
	X86_MATCH_VFM(INTEL_WESTMERE_EX,	&nhm_cstates),

	X86_MATCH_VFM(INTEL_SANDYBRIDGE,	&snb_cstates),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE_X,	&snb_cstates),

	X86_MATCH_VFM(INTEL_IVYBRIDGE,		&snb_cstates),
	X86_MATCH_VFM(INTEL_IVYBRIDGE_X,	&snb_cstates),

	X86_MATCH_VFM(INTEL_HASWELL,		&snb_cstates),
	X86_MATCH_VFM(INTEL_HASWELL_X,		&snb_cstates),
	X86_MATCH_VFM(INTEL_HASWELL_G,		&snb_cstates),

	X86_MATCH_VFM(INTEL_HASWELL_L,		&hswult_cstates),

	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT,	&slm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT_D,	&slm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_AIRMONT,	&slm_cstates),

	X86_MATCH_VFM(INTEL_BROADWELL,		&snb_cstates),
	X86_MATCH_VFM(INTEL_BROADWELL_D,	&snb_cstates),
	X86_MATCH_VFM(INTEL_BROADWELL_G,	&snb_cstates),
	X86_MATCH_VFM(INTEL_BROADWELL_X,	&snb_cstates),

	X86_MATCH_VFM(INTEL_SKYLAKE_L,		&snb_cstates),
	X86_MATCH_VFM(INTEL_SKYLAKE,		&snb_cstates),
	X86_MATCH_VFM(INTEL_SKYLAKE_X,		&snb_cstates),

	X86_MATCH_VFM(INTEL_KABYLAKE_L,		&hswult_cstates),
	X86_MATCH_VFM(INTEL_KABYLAKE,		&hswult_cstates),
	X86_MATCH_VFM(INTEL_COMETLAKE_L,	&hswult_cstates),
	X86_MATCH_VFM(INTEL_COMETLAKE,		&hswult_cstates),

	X86_MATCH_VFM(INTEL_CANNONLAKE_L,	&cnl_cstates),

	X86_MATCH_VFM(INTEL_XEON_PHI_KNL,	&knl_cstates),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNM,	&knl_cstates),

	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_D,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_PLUS,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_D,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_L,	&glm_cstates),
	X86_MATCH_VFM(INTEL_ATOM_GRACEMONT,	&adl_cstates),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT_X,	&srf_cstates),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT,	&grr_cstates),

	X86_MATCH_VFM(INTEL_ICELAKE_L,		&icl_cstates),
	X86_MATCH_VFM(INTEL_ICELAKE,		&icl_cstates),
	X86_MATCH_VFM(INTEL_ICELAKE_X,		&icx_cstates),
	X86_MATCH_VFM(INTEL_ICELAKE_D,		&icx_cstates),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X,	&icx_cstates),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X,	&icx_cstates),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_X,	&icx_cstates),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_D,	&icx_cstates),

	X86_MATCH_VFM(INTEL_TIGERLAKE_L,	&icl_cstates),
	X86_MATCH_VFM(INTEL_TIGERLAKE,		&icl_cstates),
	X86_MATCH_VFM(INTEL_ROCKETLAKE,		&icl_cstates),
	X86_MATCH_VFM(INTEL_ALDERLAKE,		&adl_cstates),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,	&adl_cstates),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,		&adl_cstates),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,	&adl_cstates),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,	&adl_cstates),
	X86_MATCH_VFM(INTEL_METEORLAKE,		&adl_cstates),
	X86_MATCH_VFM(INTEL_METEORLAKE_L,	&adl_cstates),
	X86_MATCH_VFM(INTEL_ARROWLAKE,		&adl_cstates),
	X86_MATCH_VFM(INTEL_ARROWLAKE_H,	&adl_cstates),
	X86_MATCH_VFM(INTEL_ARROWLAKE_U,	&adl_cstates),
	X86_MATCH_VFM(INTEL_LUNARLAKE_M,	&lnl_cstates),
	{ },
};
MODULE_DEVICE_TABLE(x86cpu, intel_cstates_match);

static int __init cstate_probe(const struct cstate_model *cm)
{
	/* SLM has different MSR for PKG C6 */
	if (cm->quirks & SLM_PKG_C6_USE_C7_MSR)
		pkg_msr[PERF_CSTATE_PKG_C6_RES].msr = MSR_PKG_C7_RESIDENCY;

	/* KNL has different MSR for CORE C6 */
	if (cm->quirks & KNL_CORE_C6_MSR)
		pkg_msr[PERF_CSTATE_CORE_C6_RES].msr = MSR_KNL_CORE_C6_RESIDENCY;


	core_msr_mask = perf_msr_probe(core_msr, PERF_CSTATE_CORE_EVENT_MAX,
				       true, (void *) &cm->core_events);

	pkg_msr_mask = perf_msr_probe(pkg_msr, PERF_CSTATE_PKG_EVENT_MAX,
				      true, (void *) &cm->pkg_events);

	module_msr_mask = perf_msr_probe(module_msr, PERF_CSTATE_MODULE_EVENT_MAX,
				      true, (void *) &cm->module_events);

	has_cstate_core = !!core_msr_mask;
	has_cstate_pkg  = !!pkg_msr_mask;
	has_cstate_module  = !!module_msr_mask;

	return (has_cstate_core || has_cstate_pkg || has_cstate_module) ? 0 : -ENODEV;
}

static inline void cstate_cleanup(void)
{
	if (has_cstate_core)
		perf_pmu_unregister(&cstate_core_pmu);

	if (has_cstate_pkg)
		perf_pmu_unregister(&cstate_pkg_pmu);

	if (has_cstate_module)
		perf_pmu_unregister(&cstate_module_pmu);
}

static int __init cstate_init(void)
{
	int err;

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
		if (topology_max_dies_per_package() > 1) {
			/* CLX-AP is multi-die and the cstate is die-scope */
			cstate_pkg_pmu.scope = PERF_PMU_SCOPE_DIE;
			err = perf_pmu_register(&cstate_pkg_pmu,
						"cstate_die", -1);
		} else {
			err = perf_pmu_register(&cstate_pkg_pmu,
						cstate_pkg_pmu.name, -1);
		}
		if (err) {
			has_cstate_pkg = false;
			pr_info("Failed to register cstate pkg pmu\n");
			cstate_cleanup();
			return err;
		}
	}

	if (has_cstate_module) {
		err = perf_pmu_register(&cstate_module_pmu, cstate_module_pmu.name, -1);
		if (err) {
			has_cstate_module = false;
			pr_info("Failed to register cstate cluster pmu\n");
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
