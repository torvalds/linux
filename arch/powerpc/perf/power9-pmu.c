/*
 * Performance counter support for POWER9 processors.
 *
 * Copyright 2009 Paul Mackerras, IBM Corporation.
 * Copyright 2013 Michael Ellerman, IBM Corporation.
 * Copyright 2016 Madhavan Srinivasan, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or later version.
 */

#define pr_fmt(fmt)	"power9-pmu: " fmt

#include "isa207-common.h"

/*
 * Raw event encoding for Power9:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   | | [ ]                       [ ] [      thresh_cmp     ]   [  thresh_ctl   ]
 *   | |  |                         |                                     |
 *   | |  *- IFM (Linux)            |    thresh start/stop OR FAB match -*
 *   | *- BHRB (Linux)              *sm
 *   *- EBB (Linux)
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   [   ] [  sample ]   [cache]   [ pmc ]   [unit ]   []    m   [    pmcxsel    ]
 *     |        |           |                          |     |
 *     |        |           |                          |     *- mark
 *     |        |           *- L1/L2/L3 cache_sel      |
 *     |        |                                      |
 *     |        *- sampling mode for marked events     *- combine
 *     |
 *     *- thresh_sel
 *
 * Below uses IBM bit numbering.
 *
 * MMCR1[x:y] = unit    (PMCxUNIT)
 * MMCR1[24]   = pmc1combine[0]
 * MMCR1[25]   = pmc1combine[1]
 * MMCR1[26]   = pmc2combine[0]
 * MMCR1[27]   = pmc2combine[1]
 * MMCR1[28]   = pmc3combine[0]
 * MMCR1[29]   = pmc3combine[1]
 * MMCR1[30]   = pmc4combine[0]
 * MMCR1[31]   = pmc4combine[1]
 *
 * if pmc == 3 and unit == 0 and pmcxsel[0:6] == 0b0101011
 *	# PM_MRK_FAB_RSP_MATCH
 *	MMCR1[20:27] = thresh_ctl   (FAB_CRESP_MATCH / FAB_TYPE_MATCH)
 * else if pmc == 4 and unit == 0xf and pmcxsel[0:6] == 0b0101001
 *	# PM_MRK_FAB_RSP_MATCH_CYC
 *	MMCR1[20:27] = thresh_ctl   (FAB_CRESP_MATCH / FAB_TYPE_MATCH)
 * else
 *	MMCRA[48:55] = thresh_ctl   (THRESH START/END)
 *
 * if thresh_sel:
 *	MMCRA[45:47] = thresh_sel
 *
 * if thresh_cmp:
 *	MMCRA[9:11] = thresh_cmp[0:2]
 *	MMCRA[12:18] = thresh_cmp[3:9]
 *
 * if unit == 6 or unit == 7
 *	MMCRC[53:55] = cache_sel[1:3]      (L2EVENT_SEL)
 * else if unit == 8 or unit == 9:
 *	if cache_sel[0] == 0: # L3 bank
 *		MMCRC[47:49] = cache_sel[1:3]  (L3EVENT_SEL0)
 *	else if cache_sel[0] == 1:
 *		MMCRC[50:51] = cache_sel[2:3]  (L3EVENT_SEL1)
 * else if cache_sel[1]: # L1 event
 *	MMCR1[16] = cache_sel[2]
 *	MMCR1[17] = cache_sel[3]
 *
 * if mark:
 *	MMCRA[63]    = 1		(SAMPLE_ENABLE)
 *	MMCRA[57:59] = sample[0:2]	(RAND_SAMP_ELIG)
 *	MMCRA[61:62] = sample[3:4]	(RAND_SAMP_MODE)
 *
 * if EBB and BHRB:
 *	MMCRA[32:33] = IFM
 *
 * MMCRA[SDAR_MODE]  = sm
 */

/*
 * Some power9 event codes.
 */
#define EVENT(_name, _code)	_name = _code,

enum {
#include "power9-events-list.h"
};

#undef EVENT

/* MMCRA IFM bits - POWER9 */
#define POWER9_MMCRA_IFM1		0x0000000040000000UL
#define POWER9_MMCRA_IFM2		0x0000000080000000UL
#define POWER9_MMCRA_IFM3		0x00000000C0000000UL

/* PowerISA v2.07 format attribute structure*/
extern struct attribute_group isa207_pmu_format_group;

GENERIC_EVENT_ATTR(cpu-cycles,			PM_CYC);
GENERIC_EVENT_ATTR(stalled-cycles-frontend,	PM_ICT_NOSLOT_CYC);
GENERIC_EVENT_ATTR(stalled-cycles-backend,	PM_CMPLU_STALL);
GENERIC_EVENT_ATTR(instructions,		PM_INST_CMPL);
GENERIC_EVENT_ATTR(branch-instructions,		PM_BRU_CMPL);
GENERIC_EVENT_ATTR(branch-misses,		PM_BR_MPRED_CMPL);
GENERIC_EVENT_ATTR(cache-references,		PM_LD_REF_L1);
GENERIC_EVENT_ATTR(cache-misses,		PM_LD_MISS_L1_FIN);

CACHE_EVENT_ATTR(L1-dcache-load-misses,		PM_LD_MISS_L1_FIN);
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
CACHE_EVENT_ATTR(branch-loads,			PM_BRU_CMPL);
CACHE_EVENT_ATTR(dTLB-load-misses,		PM_DTLB_MISS);
CACHE_EVENT_ATTR(iTLB-load-misses,		PM_ITLB_MISS);

static struct attribute *power9_events_attr[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_ICT_NOSLOT_CYC),
	GENERIC_EVENT_PTR(PM_CMPLU_STALL),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	GENERIC_EVENT_PTR(PM_BRU_CMPL),
	GENERIC_EVENT_PTR(PM_BR_MPRED_CMPL),
	GENERIC_EVENT_PTR(PM_LD_REF_L1),
	GENERIC_EVENT_PTR(PM_LD_MISS_L1_FIN),
	CACHE_EVENT_PTR(PM_LD_MISS_L1_FIN),
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
	CACHE_EVENT_PTR(PM_BRU_CMPL),
	CACHE_EVENT_PTR(PM_DTLB_MISS),
	CACHE_EVENT_PTR(PM_ITLB_MISS),
	NULL
};

static struct attribute_group power9_pmu_events_group = {
	.name = "events",
	.attrs = power9_events_attr,
};

static const struct attribute_group *power9_isa207_pmu_attr_groups[] = {
	&isa207_pmu_format_group,
	&power9_pmu_events_group,
	NULL,
};

PMU_FORMAT_ATTR(event,		"config:0-51");
PMU_FORMAT_ATTR(pmcxsel,	"config:0-7");
PMU_FORMAT_ATTR(mark,		"config:8");
PMU_FORMAT_ATTR(combine,	"config:10-11");
PMU_FORMAT_ATTR(unit,		"config:12-15");
PMU_FORMAT_ATTR(pmc,		"config:16-19");
PMU_FORMAT_ATTR(cache_sel,	"config:20-23");
PMU_FORMAT_ATTR(sample_mode,	"config:24-28");
PMU_FORMAT_ATTR(thresh_sel,	"config:29-31");
PMU_FORMAT_ATTR(thresh_stop,	"config:32-35");
PMU_FORMAT_ATTR(thresh_start,	"config:36-39");
PMU_FORMAT_ATTR(thresh_cmp,	"config:40-49");
PMU_FORMAT_ATTR(sdar_mode,	"config:50-51");

static struct attribute *power9_pmu_format_attr[] = {
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
	&format_attr_sdar_mode.attr,
	NULL,
};

static struct attribute_group power9_pmu_format_group = {
	.name = "format",
	.attrs = power9_pmu_format_attr,
};

static const struct attribute_group *power9_pmu_attr_groups[] = {
	&power9_pmu_format_group,
	&power9_pmu_events_group,
	NULL,
};

static int power9_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] =	PM_ICT_NOSLOT_CYC,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] =	PM_CMPLU_STALL,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] =		PM_BRU_CMPL,
	[PERF_COUNT_HW_BRANCH_MISSES] =			PM_BR_MPRED_CMPL,
	[PERF_COUNT_HW_CACHE_REFERENCES] =		PM_LD_REF_L1,
	[PERF_COUNT_HW_CACHE_MISSES] =			PM_LD_MISS_L1_FIN,
};

static u64 power9_bhrb_filter_map(u64 branch_sample_type)
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
		pmu_bhrb_filter |= POWER9_MMCRA_IFM1;
		return pmu_bhrb_filter;
	}

	/* Every thing else is unsupported */
	return -1;
}

static void power9_config_bhrb(u64 pmu_bhrb_filter)
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
static int power9_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[ C(L1D) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = PM_LD_REF_L1,
			[ C(RESULT_MISS)   ] = PM_LD_MISS_L1_FIN,
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
			[ C(RESULT_ACCESS) ] = PM_BRU_CMPL,
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

static struct power_pmu power9_isa207_pmu = {
	.name			= "POWER9",
	.n_counter		= MAX_PMU_COUNTERS,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.compute_mmcr		= isa207_compute_mmcr,
	.config_bhrb		= power9_config_bhrb,
	.bhrb_filter_map	= power9_bhrb_filter_map,
	.get_constraint		= isa207_get_constraint,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_NO_SIAR | PPMU_ARCH_207S,
	.n_generic		= ARRAY_SIZE(power9_generic_events),
	.generic_events		= power9_generic_events,
	.cache_events		= &power9_cache_events,
	.attr_groups		= power9_isa207_pmu_attr_groups,
	.bhrb_nr		= 32,
};

static struct power_pmu power9_pmu = {
	.name			= "POWER9",
	.n_counter		= MAX_PMU_COUNTERS,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.compute_mmcr		= isa207_compute_mmcr,
	.config_bhrb		= power9_config_bhrb,
	.bhrb_filter_map	= power9_bhrb_filter_map,
	.get_constraint		= isa207_get_constraint,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_HAS_SIER | PPMU_ARCH_207S,
	.n_generic		= ARRAY_SIZE(power9_generic_events),
	.generic_events		= power9_generic_events,
	.cache_events		= &power9_cache_events,
	.attr_groups		= power9_pmu_attr_groups,
	.bhrb_nr		= 32,
};

static int __init init_power9_pmu(void)
{
	int rc = 0;

	/* Comes from cpu_specs[] */
	if (!cur_cpu_spec->oprofile_cpu_type ||
	    strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power9"))
		return -ENODEV;

	if (cpu_has_feature(CPU_FTR_POWER9_DD1)) {
		rc = register_power_pmu(&power9_isa207_pmu);
	} else {
		rc = register_power_pmu(&power9_pmu);
	}

	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	return 0;
}
early_initcall(init_power9_pmu);
