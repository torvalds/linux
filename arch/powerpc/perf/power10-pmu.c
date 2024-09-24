// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Performance counter support for POWER10 processors.
 *
 * Copyright 2020 Madhavan Srinivasan, IBM Corporation.
 * Copyright 2020 Athira Rajeev, IBM Corporation.
 */

#define pr_fmt(fmt)	"power10-pmu: " fmt

#include "isa207-common.h"

/*
 * Raw event encoding for Power10:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   | | [ ]   [ src_match ] [  src_mask ]   | [ ] [ l2l3_sel ]  [  thresh_ctl   ]
 *   | |  |                                  |  |                         |
 *   | |  *- IFM (Linux)                     |  |        thresh start/stop -*
 *   | *- BHRB (Linux)                       |  src_sel
 *   *- EBB (Linux)                          *invert_bit
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   [   ] [  sample ]   [ ] [ ]   [ pmc ]   [unit ]   [ ] |  m   [    pmcxsel    ]
 *     |        |        |    |                        |   |  |
 *     |        |        |    |                        |   |  *- mark
 *     |        |        |    *- L1/L2/L3 cache_sel    |   |*-radix_scope_qual
 *     |        |        sdar_mode                     |
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
 *	MMCR1[20:27] = thresh_ctl
 * else if pmc == 4 and unit == 0xf and pmcxsel[0:6] == 0b0101001
 *	MMCR1[20:27] = thresh_ctl
 * else
 *	MMCRA[48:55] = thresh_ctl   (THRESH START/END)
 *
 * if thresh_sel:
 *	MMCRA[45:47] = thresh_sel
 *
 * if l2l3_sel:
 * MMCR2[56:60] = l2l3_sel[0:4]
 *
 * MMCR1[16] = cache_sel[0]
 * MMCR1[17] = cache_sel[1]
 * MMCR1[18] = radix_scope_qual
 *
 * if mark:
 *	MMCRA[63]    = 1		(SAMPLE_ENABLE)
 *	MMCRA[57:59] = sample[0:2]	(RAND_SAMP_ELIG)
 *	MMCRA[61:62] = sample[3:4]	(RAND_SAMP_MODE)
 *
 * if EBB and BHRB:
 *	MMCRA[32:33] = IFM
 *
 * MMCRA[SDAR_MODE]  = sdar_mode[0:1]
 */

/*
 * Some power10 event codes.
 */
#define EVENT(_name, _code)     enum{_name = _code}

#include "power10-events-list.h"

#undef EVENT

/* MMCRA IFM bits - POWER10 */
#define POWER10_MMCRA_IFM1		0x0000000040000000UL
#define POWER10_MMCRA_IFM2		0x0000000080000000UL
#define POWER10_MMCRA_IFM3		0x00000000C0000000UL
#define POWER10_MMCRA_BHRB_MASK		0x00000000C0000000UL

extern u64 PERF_REG_EXTENDED_MASK;

/* Table of alternatives, sorted by column 0 */
static const unsigned int power10_event_alternatives[][MAX_ALT] = {
	{ PM_INST_CMPL_ALT,		PM_INST_CMPL },
	{ PM_CYC_ALT,			PM_CYC },
};

static int power10_get_alternatives(u64 event, unsigned int flags, u64 alt[])
{
	int num_alt = 0;

	num_alt = isa207_get_alternatives(event, alt,
					  ARRAY_SIZE(power10_event_alternatives), flags,
					  power10_event_alternatives);

	return num_alt;
}

static int power10_check_attr_config(struct perf_event *ev)
{
	u64 val;
	u64 event = ev->attr.config;

	val = (event >> EVENT_SAMPLE_SHIFT) & EVENT_SAMPLE_MASK;
	if (val == 0x10 || isa3XX_check_attr_config(ev))
		return -EINVAL;

	return 0;
}

GENERIC_EVENT_ATTR(cpu-cycles,			PM_CYC);
GENERIC_EVENT_ATTR(instructions,		PM_INST_CMPL);
GENERIC_EVENT_ATTR(branch-instructions,		PM_BR_CMPL);
GENERIC_EVENT_ATTR(branch-misses,		PM_BR_MPRED_CMPL);
GENERIC_EVENT_ATTR(cache-references,		PM_LD_REF_L1);
GENERIC_EVENT_ATTR(cache-misses,		PM_LD_MISS_L1);
GENERIC_EVENT_ATTR(mem-loads,			MEM_LOADS);
GENERIC_EVENT_ATTR(mem-stores,			MEM_STORES);
GENERIC_EVENT_ATTR(branch-instructions,		PM_BR_FIN);
GENERIC_EVENT_ATTR(branch-misses,		PM_MPRED_BR_FIN);
GENERIC_EVENT_ATTR(cache-misses,		PM_LD_DEMAND_MISS_L1_FIN);

CACHE_EVENT_ATTR(L1-dcache-load-misses,		PM_LD_MISS_L1);
CACHE_EVENT_ATTR(L1-dcache-loads,		PM_LD_REF_L1);
CACHE_EVENT_ATTR(L1-dcache-prefetches,		PM_LD_PREFETCH_CACHE_LINE_MISS);
CACHE_EVENT_ATTR(L1-dcache-store-misses,	PM_ST_MISS_L1);
CACHE_EVENT_ATTR(L1-icache-load-misses,		PM_L1_ICACHE_MISS);
CACHE_EVENT_ATTR(L1-icache-loads,		PM_INST_FROM_L1);
CACHE_EVENT_ATTR(L1-icache-prefetches,		PM_IC_PREF_REQ);
CACHE_EVENT_ATTR(LLC-load-misses,		PM_DATA_FROM_L3MISS);
CACHE_EVENT_ATTR(LLC-loads,			PM_DATA_FROM_L3);
CACHE_EVENT_ATTR(LLC-prefetches,		PM_L3_PF_MISS_L3);
CACHE_EVENT_ATTR(LLC-store-misses,		PM_L2_ST_MISS);
CACHE_EVENT_ATTR(LLC-stores,			PM_L2_ST);
CACHE_EVENT_ATTR(branch-load-misses,		PM_BR_MPRED_CMPL);
CACHE_EVENT_ATTR(branch-loads,			PM_BR_CMPL);
CACHE_EVENT_ATTR(dTLB-load-misses,		PM_DTLB_MISS);
CACHE_EVENT_ATTR(iTLB-load-misses,		PM_ITLB_MISS);

static struct attribute *power10_events_attr_dd1[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	GENERIC_EVENT_PTR(PM_BR_CMPL),
	GENERIC_EVENT_PTR(PM_BR_MPRED_CMPL),
	GENERIC_EVENT_PTR(PM_LD_REF_L1),
	GENERIC_EVENT_PTR(PM_LD_MISS_L1),
	GENERIC_EVENT_PTR(MEM_LOADS),
	GENERIC_EVENT_PTR(MEM_STORES),
	CACHE_EVENT_PTR(PM_LD_MISS_L1),
	CACHE_EVENT_PTR(PM_LD_REF_L1),
	CACHE_EVENT_PTR(PM_LD_PREFETCH_CACHE_LINE_MISS),
	CACHE_EVENT_PTR(PM_ST_MISS_L1),
	CACHE_EVENT_PTR(PM_L1_ICACHE_MISS),
	CACHE_EVENT_PTR(PM_INST_FROM_L1),
	CACHE_EVENT_PTR(PM_IC_PREF_REQ),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3MISS),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3),
	CACHE_EVENT_PTR(PM_BR_MPRED_CMPL),
	CACHE_EVENT_PTR(PM_BR_CMPL),
	CACHE_EVENT_PTR(PM_DTLB_MISS),
	CACHE_EVENT_PTR(PM_ITLB_MISS),
	NULL
};

static struct attribute *power10_events_attr[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	GENERIC_EVENT_PTR(PM_BR_FIN),
	GENERIC_EVENT_PTR(PM_MPRED_BR_FIN),
	GENERIC_EVENT_PTR(PM_LD_REF_L1),
	GENERIC_EVENT_PTR(PM_LD_DEMAND_MISS_L1_FIN),
	GENERIC_EVENT_PTR(MEM_LOADS),
	GENERIC_EVENT_PTR(MEM_STORES),
	CACHE_EVENT_PTR(PM_LD_MISS_L1),
	CACHE_EVENT_PTR(PM_LD_REF_L1),
	CACHE_EVENT_PTR(PM_LD_PREFETCH_CACHE_LINE_MISS),
	CACHE_EVENT_PTR(PM_ST_MISS_L1),
	CACHE_EVENT_PTR(PM_L1_ICACHE_MISS),
	CACHE_EVENT_PTR(PM_INST_FROM_L1),
	CACHE_EVENT_PTR(PM_IC_PREF_REQ),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3MISS),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3),
	CACHE_EVENT_PTR(PM_L3_PF_MISS_L3),
	CACHE_EVENT_PTR(PM_L2_ST_MISS),
	CACHE_EVENT_PTR(PM_L2_ST),
	CACHE_EVENT_PTR(PM_BR_MPRED_CMPL),
	CACHE_EVENT_PTR(PM_BR_CMPL),
	CACHE_EVENT_PTR(PM_DTLB_MISS),
	CACHE_EVENT_PTR(PM_ITLB_MISS),
	NULL
};

static const struct attribute_group power10_pmu_events_group_dd1 = {
	.name = "events",
	.attrs = power10_events_attr_dd1,
};

static const struct attribute_group power10_pmu_events_group = {
	.name = "events",
	.attrs = power10_events_attr,
};

PMU_FORMAT_ATTR(event,          "config:0-59");
PMU_FORMAT_ATTR(pmcxsel,        "config:0-7");
PMU_FORMAT_ATTR(mark,           "config:8");
PMU_FORMAT_ATTR(combine,        "config:10-11");
PMU_FORMAT_ATTR(unit,           "config:12-15");
PMU_FORMAT_ATTR(pmc,            "config:16-19");
PMU_FORMAT_ATTR(cache_sel,      "config:20-21");
PMU_FORMAT_ATTR(sdar_mode,      "config:22-23");
PMU_FORMAT_ATTR(sample_mode,    "config:24-28");
PMU_FORMAT_ATTR(thresh_sel,     "config:29-31");
PMU_FORMAT_ATTR(thresh_stop,    "config:32-35");
PMU_FORMAT_ATTR(thresh_start,   "config:36-39");
PMU_FORMAT_ATTR(l2l3_sel,       "config:40-44");
PMU_FORMAT_ATTR(src_sel,        "config:45-46");
PMU_FORMAT_ATTR(invert_bit,     "config:47");
PMU_FORMAT_ATTR(src_mask,       "config:48-53");
PMU_FORMAT_ATTR(src_match,      "config:54-59");
PMU_FORMAT_ATTR(radix_scope,	"config:9");
PMU_FORMAT_ATTR(thresh_cmp,     "config1:0-17");

static struct attribute *power10_pmu_format_attr[] = {
	&format_attr_event.attr,
	&format_attr_pmcxsel.attr,
	&format_attr_mark.attr,
	&format_attr_combine.attr,
	&format_attr_unit.attr,
	&format_attr_pmc.attr,
	&format_attr_cache_sel.attr,
	&format_attr_sdar_mode.attr,
	&format_attr_sample_mode.attr,
	&format_attr_thresh_sel.attr,
	&format_attr_thresh_stop.attr,
	&format_attr_thresh_start.attr,
	&format_attr_l2l3_sel.attr,
	&format_attr_src_sel.attr,
	&format_attr_invert_bit.attr,
	&format_attr_src_mask.attr,
	&format_attr_src_match.attr,
	&format_attr_radix_scope.attr,
	&format_attr_thresh_cmp.attr,
	NULL,
};

static const struct attribute_group power10_pmu_format_group = {
	.name = "format",
	.attrs = power10_pmu_format_attr,
};

static struct attribute *power10_pmu_caps_attrs[] = {
	NULL
};

static struct attribute_group power10_pmu_caps_group = {
	.name  = "caps",
	.attrs = power10_pmu_caps_attrs,
};

static const struct attribute_group *power10_pmu_attr_groups_dd1[] = {
	&power10_pmu_format_group,
	&power10_pmu_events_group_dd1,
	&power10_pmu_caps_group,
	NULL,
};

static const struct attribute_group *power10_pmu_attr_groups[] = {
	&power10_pmu_format_group,
	&power10_pmu_events_group,
	&power10_pmu_caps_group,
	NULL,
};

static int power10_generic_events_dd1[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] =		PM_BR_CMPL,
	[PERF_COUNT_HW_BRANCH_MISSES] =			PM_BR_MPRED_CMPL,
	[PERF_COUNT_HW_CACHE_REFERENCES] =		PM_LD_REF_L1,
	[PERF_COUNT_HW_CACHE_MISSES] =			PM_LD_MISS_L1,
};

static int power10_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] =		PM_BR_FIN,
	[PERF_COUNT_HW_BRANCH_MISSES] =			PM_MPRED_BR_FIN,
	[PERF_COUNT_HW_CACHE_REFERENCES] =		PM_LD_REF_L1,
	[PERF_COUNT_HW_CACHE_MISSES] =			PM_LD_DEMAND_MISS_L1_FIN,
};

static u64 power10_bhrb_filter_map(u64 branch_sample_type)
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

	if (branch_sample_type & PERF_SAMPLE_BRANCH_IND_CALL) {
		pmu_bhrb_filter |= POWER10_MMCRA_IFM2;
		return pmu_bhrb_filter;
	}

	if (branch_sample_type & PERF_SAMPLE_BRANCH_COND) {
		pmu_bhrb_filter |= POWER10_MMCRA_IFM3;
		return pmu_bhrb_filter;
	}

	if (branch_sample_type & PERF_SAMPLE_BRANCH_CALL)
		return -1;

	if (branch_sample_type & PERF_SAMPLE_BRANCH_ANY_CALL) {
		pmu_bhrb_filter |= POWER10_MMCRA_IFM1;
		return pmu_bhrb_filter;
	}

	/* Every thing else is unsupported */
	return -1;
}

static void power10_config_bhrb(u64 pmu_bhrb_filter)
{
	pmu_bhrb_filter &= POWER10_MMCRA_BHRB_MASK;

	/* Enable BHRB filter in PMU */
	mtspr(SPRN_MMCRA, (mfspr(SPRN_MMCRA) | pmu_bhrb_filter));
}

#define C(x)	PERF_COUNT_HW_CACHE_##x

/*
 * Table of generalized cache-related events.
 * 0 means not supported, -1 means nonsensical, other values
 * are event codes.
 */
static u64 power10_cache_events_dd1[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_LD_REF_L1,
			[C(RESULT_MISS)] = PM_LD_MISS_L1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_ST_MISS_L1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = PM_LD_PREFETCH_CACHE_LINE_MISS,
			[C(RESULT_MISS)] = 0,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_INST_FROM_L1,
			[C(RESULT_MISS)] = PM_L1_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = PM_INST_FROM_L1MISS,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = PM_IC_PREF_REQ,
			[C(RESULT_MISS)] = 0,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_DATA_FROM_L3,
			[C(RESULT_MISS)] = PM_DATA_FROM_L3MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = 0,
		},
	},
	 [C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_BR_CMPL,
			[C(RESULT_MISS)] = PM_BR_MPRED_CMPL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
};

static u64 power10_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_LD_REF_L1,
			[C(RESULT_MISS)] = PM_LD_MISS_L1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_ST_MISS_L1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = PM_LD_PREFETCH_CACHE_LINE_MISS,
			[C(RESULT_MISS)] = 0,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_INST_FROM_L1,
			[C(RESULT_MISS)] = PM_L1_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = PM_INST_FROM_L1MISS,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = PM_IC_PREF_REQ,
			[C(RESULT_MISS)] = 0,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_DATA_FROM_L3,
			[C(RESULT_MISS)] = PM_DATA_FROM_L3MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = PM_L2_ST,
			[C(RESULT_MISS)] = PM_L2_ST_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = PM_L3_PF_MISS_L3,
			[C(RESULT_MISS)] = 0,
		},
	},
	 [C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)] = PM_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = PM_BR_CMPL,
			[C(RESULT_MISS)] = PM_BR_MPRED_CMPL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)] = -1,
		},
	},
};

#undef C

/*
 * Set the MMCR0[CC56RUN] bit to enable counting for
 * PMC5 and PMC6 regardless of the state of CTRL[RUN],
 * so that we can use counters 5 and 6 as PM_INST_CMPL and
 * PM_CYC.
 */
static int power10_compute_mmcr(u64 event[], int n_ev,
				unsigned int hwc[], struct mmcr_regs *mmcr,
				struct perf_event *pevents[], u32 flags)
{
	int ret;

	ret = isa207_compute_mmcr(event, n_ev, hwc, mmcr, pevents, flags);
	if (!ret)
		mmcr->mmcr0 |= MMCR0_C56RUN;
	return ret;
}

static struct power_pmu power10_pmu = {
	.name			= "POWER10",
	.n_counter		= MAX_PMU_COUNTERS,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.group_constraint_mask	= CNST_CACHE_PMC4_MASK,
	.group_constraint_val	= CNST_CACHE_PMC4_VAL,
	.compute_mmcr		= power10_compute_mmcr,
	.config_bhrb		= power10_config_bhrb,
	.bhrb_filter_map	= power10_bhrb_filter_map,
	.get_constraint		= isa207_get_constraint,
	.get_alternatives	= power10_get_alternatives,
	.get_mem_data_src	= isa207_get_mem_data_src,
	.get_mem_weight		= isa207_get_mem_weight,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_HAS_SIER | PPMU_ARCH_207S |
				  PPMU_ARCH_31 | PPMU_HAS_ATTR_CONFIG1 |
				  PPMU_P10,
	.n_generic		= ARRAY_SIZE(power10_generic_events),
	.generic_events		= power10_generic_events,
	.cache_events		= &power10_cache_events,
	.attr_groups		= power10_pmu_attr_groups,
	.bhrb_nr		= 32,
	.capabilities           = PERF_PMU_CAP_EXTENDED_REGS,
	.check_attr_config	= power10_check_attr_config,
};

int __init init_power10_pmu(void)
{
	unsigned int pvr;
	int rc;

	pvr = mfspr(SPRN_PVR);
	if (PVR_VER(pvr) != PVR_POWER10)
		return -ENODEV;

	/* Add the ppmu flag for power10 DD1 */
	if ((PVR_CFG(pvr) == 1))
		power10_pmu.flags |= PPMU_P10_DD1;

	/* Set the PERF_REG_EXTENDED_MASK here */
	PERF_REG_EXTENDED_MASK = PERF_REG_PMU_MASK_31;

	if ((PVR_CFG(pvr) == 1)) {
		power10_pmu.generic_events = power10_generic_events_dd1;
		power10_pmu.attr_groups = power10_pmu_attr_groups_dd1;
		power10_pmu.cache_events = &power10_cache_events_dd1;
	}

	rc = register_power_pmu(&power10_pmu);
	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	return 0;
}

static struct power_pmu power11_pmu;

int __init init_power11_pmu(void)
{
	unsigned int pvr;
	int rc;

	pvr = mfspr(SPRN_PVR);
	if (PVR_VER(pvr) != PVR_POWER11)
		return -ENODEV;

	/* Set the PERF_REG_EXTENDED_MASK here */
	PERF_REG_EXTENDED_MASK = PERF_REG_PMU_MASK_31;

	power11_pmu = power10_pmu;
	power11_pmu.name = "Power11";

	rc = register_power_pmu(&power11_pmu);
	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	return 0;
}
