/*
 * Performance counter support for POWER8 processors.
 *
 * Copyright 2009 Paul Mackerras, IBM Corporation.
 * Copyright 2013 Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)	"power8-pmu: " fmt

#include "isa207-common.h"

/*
 * Some power8 event codes.
 */
#define EVENT(_name, _code)	_name = _code,

enum {
#include "power8-events-list.h"
};

#undef EVENT

/* MMCRA IFM bits - POWER8 */
#define	POWER8_MMCRA_IFM1		0x0000000040000000UL
#define	POWER8_MMCRA_IFM2		0x0000000080000000UL
#define	POWER8_MMCRA_IFM3		0x00000000C0000000UL

/* Table of alternatives, sorted by column 0 */
static const unsigned int event_alternatives[][MAX_ALT] = {
	{ PM_MRK_ST_CMPL,		PM_MRK_ST_CMPL_ALT },
	{ PM_BR_MRK_2PATH,		PM_BR_MRK_2PATH_ALT },
	{ PM_L3_CO_MEPF,		PM_L3_CO_MEPF_ALT },
	{ PM_MRK_DATA_FROM_L2MISS,	PM_MRK_DATA_FROM_L2MISS_ALT },
	{ PM_CMPLU_STALL_ALT,		PM_CMPLU_STALL },
	{ PM_BR_2PATH,			PM_BR_2PATH_ALT },
	{ PM_INST_DISP,			PM_INST_DISP_ALT },
	{ PM_RUN_CYC_ALT,		PM_RUN_CYC },
	{ PM_MRK_FILT_MATCH,		PM_MRK_FILT_MATCH_ALT },
	{ PM_LD_MISS_L1,		PM_LD_MISS_L1_ALT },
	{ PM_RUN_INST_CMPL_ALT,		PM_RUN_INST_CMPL },
};

/*
 * Scan the alternatives table for a match and return the
 * index into the alternatives table if found, else -1.
 */
static int find_alternative(u64 event)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(event_alternatives); ++i) {
		if (event < event_alternatives[i][0])
			break;

		for (j = 0; j < MAX_ALT && event_alternatives[i][j]; ++j)
			if (event == event_alternatives[i][j])
				return i;
	}

	return -1;
}

static int power8_get_alternatives(u64 event, unsigned int flags, u64 alt[])
{
	int i, j, num_alt = 0;
	u64 alt_event;

	alt[num_alt++] = event;

	i = find_alternative(event);
	if (i >= 0) {
		/* Filter out the original event, it's already in alt[0] */
		for (j = 0; j < MAX_ALT; ++j) {
			alt_event = event_alternatives[i][j];
			if (alt_event && alt_event != event)
				alt[num_alt++] = alt_event;
		}
	}

	if (flags & PPMU_ONLY_COUNT_RUN) {
		/*
		 * We're only counting in RUN state, so PM_CYC is equivalent to
		 * PM_RUN_CYC and PM_INST_CMPL === PM_RUN_INST_CMPL.
		 */
		j = num_alt;
		for (i = 0; i < num_alt; ++i) {
			switch (alt[i]) {
			case PM_CYC:
				alt[j++] = PM_RUN_CYC;
				break;
			case PM_RUN_CYC:
				alt[j++] = PM_CYC;
				break;
			case PM_INST_CMPL:
				alt[j++] = PM_RUN_INST_CMPL;
				break;
			case PM_RUN_INST_CMPL:
				alt[j++] = PM_INST_CMPL;
				break;
			}
		}
		num_alt = j;
	}

	return num_alt;
}

GENERIC_EVENT_ATTR(cpu-cycles,			PM_CYC);
GENERIC_EVENT_ATTR(stalled-cycles-frontend,	PM_GCT_NOSLOT_CYC);
GENERIC_EVENT_ATTR(stalled-cycles-backend,	PM_CMPLU_STALL);
GENERIC_EVENT_ATTR(instructions,		PM_INST_CMPL);
GENERIC_EVENT_ATTR(branch-instructions,		PM_BRU_FIN);
GENERIC_EVENT_ATTR(branch-misses,		PM_BR_MPRED_CMPL);
GENERIC_EVENT_ATTR(cache-references,		PM_LD_REF_L1);
GENERIC_EVENT_ATTR(cache-misses,		PM_LD_MISS_L1);

CACHE_EVENT_ATTR(L1-dcache-load-misses,		PM_LD_MISS_L1);
CACHE_EVENT_ATTR(L1-dcache-loads,		PM_LD_REF_L1);

CACHE_EVENT_ATTR(L1-dcache-prefetches,		PM_L1_PREF);
CACHE_EVENT_ATTR(L1-dcache-store-misses,	PM_ST_MISS_L1);
CACHE_EVENT_ATTR(L1-icache-load-misses,		PM_L1_ICACHE_MISS);
CACHE_EVENT_ATTR(L1-icache-loads,		PM_INST_FROM_L1);
CACHE_EVENT_ATTR(L1-icache-prefetches,		PM_IC_PREF_WRITE);

CACHE_EVENT_ATTR(LLC-load-misses,		PM_DATA_FROM_L3MISS);
CACHE_EVENT_ATTR(LLC-loads,			PM_DATA_FROM_L3);
CACHE_EVENT_ATTR(LLC-prefetches,		PM_L3_PREF_ALL);
CACHE_EVENT_ATTR(LLC-store-misses,		PM_L2_ST_MISS);
CACHE_EVENT_ATTR(LLC-stores,			PM_L2_ST);

CACHE_EVENT_ATTR(branch-load-misses,		PM_BR_MPRED_CMPL);
CACHE_EVENT_ATTR(branch-loads,			PM_BRU_FIN);
CACHE_EVENT_ATTR(dTLB-load-misses,		PM_DTLB_MISS);
CACHE_EVENT_ATTR(iTLB-load-misses,		PM_ITLB_MISS);

static struct attribute *power8_events_attr[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_GCT_NOSLOT_CYC),
	GENERIC_EVENT_PTR(PM_CMPLU_STALL),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	GENERIC_EVENT_PTR(PM_BRU_FIN),
	GENERIC_EVENT_PTR(PM_BR_MPRED_CMPL),
	GENERIC_EVENT_PTR(PM_LD_REF_L1),
	GENERIC_EVENT_PTR(PM_LD_MISS_L1),

	CACHE_EVENT_PTR(PM_LD_MISS_L1),
	CACHE_EVENT_PTR(PM_LD_REF_L1),
	CACHE_EVENT_PTR(PM_L1_PREF),
	CACHE_EVENT_PTR(PM_ST_MISS_L1),
	CACHE_EVENT_PTR(PM_L1_ICACHE_MISS),
	CACHE_EVENT_PTR(PM_INST_FROM_L1),
	CACHE_EVENT_PTR(PM_IC_PREF_WRITE),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3MISS),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3),
	CACHE_EVENT_PTR(PM_L3_PREF_ALL),
	CACHE_EVENT_PTR(PM_L2_ST_MISS),
	CACHE_EVENT_PTR(PM_L2_ST),

	CACHE_EVENT_PTR(PM_BR_MPRED_CMPL),
	CACHE_EVENT_PTR(PM_BRU_FIN),

	CACHE_EVENT_PTR(PM_DTLB_MISS),
	CACHE_EVENT_PTR(PM_ITLB_MISS),
	NULL
};

static struct attribute_group power8_pmu_events_group = {
	.name = "events",
	.attrs = power8_events_attr,
};

PMU_FORMAT_ATTR(event,		"config:0-49");
PMU_FORMAT_ATTR(pmcxsel,	"config:0-7");
PMU_FORMAT_ATTR(mark,		"config:8");
PMU_FORMAT_ATTR(combine,	"config:11");
PMU_FORMAT_ATTR(unit,		"config:12-15");
PMU_FORMAT_ATTR(pmc,		"config:16-19");
PMU_FORMAT_ATTR(cache_sel,	"config:20-23");
PMU_FORMAT_ATTR(sample_mode,	"config:24-28");
PMU_FORMAT_ATTR(thresh_sel,	"config:29-31");
PMU_FORMAT_ATTR(thresh_stop,	"config:32-35");
PMU_FORMAT_ATTR(thresh_start,	"config:36-39");
PMU_FORMAT_ATTR(thresh_cmp,	"config:40-49");

static struct attribute *power8_pmu_format_attr[] = {
	&format_attr_event.attr,
	&format_attr_pmcxsel.attr,
	&format_attr_mark.attr,
	&format_attr_combine.attr,
	&format_attr_unit.attr,
	&format_attr_pmc.attr,
	&format_attr_cache_sel.attr,
	&format_attr_sample_mode.attr,
	&format_attr_thresh_sel.attr,
	&format_attr_thresh_stop.attr,
	&format_attr_thresh_start.attr,
	&format_attr_thresh_cmp.attr,
	NULL,
};

struct attribute_group power8_pmu_format_group = {
	.name = "format",
	.attrs = power8_pmu_format_attr,
};

static const struct attribute_group *power8_pmu_attr_groups[] = {
	&power8_pmu_format_group,
	&power8_pmu_events_group,
	NULL,
};

static int power8_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] =	PM_GCT_NOSLOT_CYC,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] =	PM_CMPLU_STALL,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] =		PM_BRU_FIN,
	[PERF_COUNT_HW_BRANCH_MISSES] =			PM_BR_MPRED_CMPL,
	[PERF_COUNT_HW_CACHE_REFERENCES] =		PM_LD_REF_L1,
	[PERF_COUNT_HW_CACHE_MISSES] =			PM_LD_MISS_L1,
};

static u64 power8_bhrb_filter_map(u64 branch_sample_type)
{
	u64 pmu_bhrb_filter = 0;

	/* BHRB and regular PMU events share the same privilege state
	 * filter configuration. BHRB is always recorded along with a
	 * regular PMU event. As the privilege state filter is handled
	 * in the basic PMC configuration of the accompanying regular
	 * PMU event, we ignore any separate BHRB specific request.
	 */

	/* No branch filter requested */
	if (branch_sample_type & PERF_SAMPLE_BRANCH_ANY)
		return pmu_bhrb_filter;

	/* Invalid branch filter options - HW does not support */
	if (branch_sample_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		return -1;

	if (branch_sample_type & PERF_SAMPLE_BRANCH_IND_CALL)
		return -1;

	if (branch_sample_type & PERF_SAMPLE_BRANCH_CALL)
		return -1;

	if (branch_sample_type & PERF_SAMPLE_BRANCH_ANY_CALL) {
		pmu_bhrb_filter |= POWER8_MMCRA_IFM1;
		return pmu_bhrb_filter;
	}

	/* Every thing else is unsupported */
	return -1;
}

static void power8_config_bhrb(u64 pmu_bhrb_filter)
{
	/* Enable BHRB filter in PMU */
	mtspr(SPRN_MMCRA, (mfspr(SPRN_MMCRA) | pmu_bhrb_filter));
}

#define C(x)	PERF_COUNT_HW_CACHE_##x

/*
 * Table of generalized cache-related events.
 * 0 means not supported, -1 means nonsensical, other values
 * are event codes.
 */
static int power8_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[ C(L1D) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = PM_LD_REF_L1,
			[ C(RESULT_MISS)   ] = PM_LD_MISS_L1,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_ST_MISS_L1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = PM_L1_PREF,
			[ C(RESULT_MISS)   ] = 0,
		},
	},
	[ C(L1I) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = PM_INST_FROM_L1,
			[ C(RESULT_MISS)   ] = PM_L1_ICACHE_MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = PM_L1_DEMAND_WRITE,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = PM_IC_PREF_WRITE,
			[ C(RESULT_MISS)   ] = 0,
		},
	},
	[ C(LL) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = PM_DATA_FROM_L3,
			[ C(RESULT_MISS)   ] = PM_DATA_FROM_L3MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = PM_L2_ST,
			[ C(RESULT_MISS)   ] = PM_L2_ST_MISS,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = PM_L3_PREF_ALL,
			[ C(RESULT_MISS)   ] = 0,
		},
	},
	[ C(DTLB) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_DTLB_MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
	},
	[ C(ITLB) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_ITLB_MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
	},
	[ C(BPU) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = PM_BRU_FIN,
			[ C(RESULT_MISS)   ] = PM_BR_MPRED_CMPL,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
	},
	[ C(NODE) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
	},
};

#undef C

static struct power_pmu power8_pmu = {
	.name			= "POWER8",
	.n_counter		= MAX_PMU_COUNTERS,
	.max_alternatives	= MAX_ALT + 1,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.compute_mmcr		= isa207_compute_mmcr,
	.config_bhrb		= power8_config_bhrb,
	.bhrb_filter_map	= power8_bhrb_filter_map,
	.get_constraint		= isa207_get_constraint,
	.get_alternatives	= power8_get_alternatives,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_HAS_SIER | PPMU_ARCH_207S,
	.n_generic		= ARRAY_SIZE(power8_generic_events),
	.generic_events		= power8_generic_events,
	.cache_events		= &power8_cache_events,
	.attr_groups		= power8_pmu_attr_groups,
	.bhrb_nr		= 32,
};

static int __init init_power8_pmu(void)
{
	int rc;

	if (!cur_cpu_spec->oprofile_cpu_type ||
	    strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power8"))
		return -ENODEV;

	rc = register_power_pmu(&power8_pmu);
	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	if (cpu_has_feature(CPU_FTR_PMAO_BUG))
		pr_info("PMAO restore workaround active.\n");

	return 0;
}
early_initcall(init_power8_pmu);
