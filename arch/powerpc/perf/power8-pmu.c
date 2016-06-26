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

static inline bool event_is_fab_match(u64 event)
{
	/* Only check pmc, unit and pmcxsel, ignore the edge bit (0) */
	event &= 0xff0fe;

	/* PM_MRK_FAB_RSP_MATCH & PM_MRK_FAB_RSP_MATCH_CYC */
	return (event == 0x30056 || event == 0x4f052);
}

static int power8_get_constraint(u64 event, unsigned long *maskp, unsigned long *valp)
{
	unsigned int unit, pmc, cache, ebb;
	unsigned long mask, value;

	mask = value = 0;

	if (event & ~EVENT_VALID_MASK)
		return -1;

	pmc   = (event >> EVENT_PMC_SHIFT)        & EVENT_PMC_MASK;
	unit  = (event >> EVENT_UNIT_SHIFT)       & EVENT_UNIT_MASK;
	cache = (event >> EVENT_CACHE_SEL_SHIFT)  & EVENT_CACHE_SEL_MASK;
	ebb   = (event >> EVENT_EBB_SHIFT)        & EVENT_EBB_MASK;

	if (pmc) {
		u64 base_event;

		if (pmc > 6)
			return -1;

		/* Ignore Linux defined bits when checking event below */
		base_event = event & ~EVENT_LINUX_MASK;

		if (pmc >= 5 && base_event != PM_RUN_INST_CMPL &&
				base_event != PM_RUN_CYC)
			return -1;

		mask  |= CNST_PMC_MASK(pmc);
		value |= CNST_PMC_VAL(pmc);
	}

	if (pmc <= 4) {
		/*
		 * Add to number of counters in use. Note this includes events with
		 * a PMC of 0 - they still need a PMC, it's just assigned later.
		 * Don't count events on PMC 5 & 6, there is only one valid event
		 * on each of those counters, and they are handled above.
		 */
		mask  |= CNST_NC_MASK;
		value |= CNST_NC_VAL;
	}

	if (unit >= 6 && unit <= 9) {
		/*
		 * L2/L3 events contain a cache selector field, which is
		 * supposed to be programmed into MMCRC. However MMCRC is only
		 * HV writable, and there is no API for guest kernels to modify
		 * it. The solution is for the hypervisor to initialise the
		 * field to zeroes, and for us to only ever allow events that
		 * have a cache selector of zero. The bank selector (bit 3) is
		 * irrelevant, as long as the rest of the value is 0.
		 */
		if (cache & 0x7)
			return -1;

	} else if (event & EVENT_IS_L1) {
		mask  |= CNST_L1_QUAL_MASK;
		value |= CNST_L1_QUAL_VAL(cache);
	}

	if (event & EVENT_IS_MARKED) {
		mask  |= CNST_SAMPLE_MASK;
		value |= CNST_SAMPLE_VAL(event >> EVENT_SAMPLE_SHIFT);
	}

	/*
	 * Special case for PM_MRK_FAB_RSP_MATCH and PM_MRK_FAB_RSP_MATCH_CYC,
	 * the threshold control bits are used for the match value.
	 */
	if (event_is_fab_match(event)) {
		mask  |= CNST_FAB_MATCH_MASK;
		value |= CNST_FAB_MATCH_VAL(event >> EVENT_THR_CTL_SHIFT);
	} else {
		/*
		 * Check the mantissa upper two bits are not zero, unless the
		 * exponent is also zero. See the THRESH_CMP_MANTISSA doc.
		 */
		unsigned int cmp, exp;

		cmp = (event >> EVENT_THR_CMP_SHIFT) & EVENT_THR_CMP_MASK;
		exp = cmp >> 7;

		if (exp && (cmp & 0x60) == 0)
			return -1;

		mask  |= CNST_THRESH_MASK;
		value |= CNST_THRESH_VAL(event >> EVENT_THRESH_SHIFT);
	}

	if (!pmc && ebb)
		/* EBB events must specify the PMC */
		return -1;

	if (event & EVENT_WANTS_BHRB) {
		if (!ebb)
			/* Only EBB events can request BHRB */
			return -1;

		mask  |= CNST_IFM_MASK;
		value |= CNST_IFM_VAL(event >> EVENT_IFM_SHIFT);
	}

	/*
	 * All events must agree on EBB, either all request it or none.
	 * EBB events are pinned & exclusive, so this should never actually
	 * hit, but we leave it as a fallback in case.
	 */
	mask  |= CNST_EBB_VAL(ebb);
	value |= CNST_EBB_MASK;

	*maskp = mask;
	*valp = value;

	return 0;
}

static int power8_compute_mmcr(u64 event[], int n_ev,
			       unsigned int hwc[], unsigned long mmcr[],
			       struct perf_event *pevents[])
{
	unsigned long mmcra, mmcr1, mmcr2, unit, combine, psel, cache, val;
	unsigned int pmc, pmc_inuse;
	int i;

	pmc_inuse = 0;

	/* First pass to count resource use */
	for (i = 0; i < n_ev; ++i) {
		pmc = (event[i] >> EVENT_PMC_SHIFT) & EVENT_PMC_MASK;
		if (pmc)
			pmc_inuse |= 1 << pmc;
	}

	/* In continuous sampling mode, update SDAR on TLB miss */
	mmcra = MMCRA_SDAR_MODE_TLB;
	mmcr1 = mmcr2 = 0;

	/* Second pass: assign PMCs, set all MMCR1 fields */
	for (i = 0; i < n_ev; ++i) {
		pmc     = (event[i] >> EVENT_PMC_SHIFT) & EVENT_PMC_MASK;
		unit    = (event[i] >> EVENT_UNIT_SHIFT) & EVENT_UNIT_MASK;
		combine = (event[i] >> EVENT_COMBINE_SHIFT) & EVENT_COMBINE_MASK;
		psel    =  event[i] & EVENT_PSEL_MASK;

		if (!pmc) {
			for (pmc = 1; pmc <= 4; ++pmc) {
				if (!(pmc_inuse & (1 << pmc)))
					break;
			}

			pmc_inuse |= 1 << pmc;
		}

		if (pmc <= 4) {
			mmcr1 |= unit << MMCR1_UNIT_SHIFT(pmc);
			mmcr1 |= combine << MMCR1_COMBINE_SHIFT(pmc);
			mmcr1 |= psel << MMCR1_PMCSEL_SHIFT(pmc);
		}

		if (event[i] & EVENT_IS_L1) {
			cache = event[i] >> EVENT_CACHE_SEL_SHIFT;
			mmcr1 |= (cache & 1) << MMCR1_IC_QUAL_SHIFT;
			cache >>= 1;
			mmcr1 |= (cache & 1) << MMCR1_DC_QUAL_SHIFT;
		}

		if (event[i] & EVENT_IS_MARKED) {
			mmcra |= MMCRA_SAMPLE_ENABLE;

			val = (event[i] >> EVENT_SAMPLE_SHIFT) & EVENT_SAMPLE_MASK;
			if (val) {
				mmcra |= (val &  3) << MMCRA_SAMP_MODE_SHIFT;
				mmcra |= (val >> 2) << MMCRA_SAMP_ELIG_SHIFT;
			}
		}

		/*
		 * PM_MRK_FAB_RSP_MATCH and PM_MRK_FAB_RSP_MATCH_CYC,
		 * the threshold bits are used for the match value.
		 */
		if (event_is_fab_match(event[i])) {
			mmcr1 |= ((event[i] >> EVENT_THR_CTL_SHIFT) &
				  EVENT_THR_CTL_MASK) << MMCR1_FAB_SHIFT;
		} else {
			val = (event[i] >> EVENT_THR_CTL_SHIFT) & EVENT_THR_CTL_MASK;
			mmcra |= val << MMCRA_THR_CTL_SHIFT;
			val = (event[i] >> EVENT_THR_SEL_SHIFT) & EVENT_THR_SEL_MASK;
			mmcra |= val << MMCRA_THR_SEL_SHIFT;
			val = (event[i] >> EVENT_THR_CMP_SHIFT) & EVENT_THR_CMP_MASK;
			mmcra |= val << MMCRA_THR_CMP_SHIFT;
		}

		if (event[i] & EVENT_WANTS_BHRB) {
			val = (event[i] >> EVENT_IFM_SHIFT) & EVENT_IFM_MASK;
			mmcra |= val << MMCRA_IFM_SHIFT;
		}

		if (pevents[i]->attr.exclude_user)
			mmcr2 |= MMCR2_FCP(pmc);

		if (pevents[i]->attr.exclude_hv)
			mmcr2 |= MMCR2_FCH(pmc);

		if (pevents[i]->attr.exclude_kernel) {
			if (cpu_has_feature(CPU_FTR_HVMODE))
				mmcr2 |= MMCR2_FCH(pmc);
			else
				mmcr2 |= MMCR2_FCS(pmc);
		}

		hwc[i] = pmc - 1;
	}

	/* Return MMCRx values */
	mmcr[0] = 0;

	/* pmc_inuse is 1-based */
	if (pmc_inuse & 2)
		mmcr[0] = MMCR0_PMC1CE;

	if (pmc_inuse & 0x7c)
		mmcr[0] |= MMCR0_PMCjCE;

	/* If we're not using PMC 5 or 6, freeze them */
	if (!(pmc_inuse & 0x60))
		mmcr[0] |= MMCR0_FC56;

	mmcr[1] = mmcr1;
	mmcr[2] = mmcra;
	mmcr[3] = mmcr2;

	return 0;
}

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

static void power8_disable_pmc(unsigned int pmc, unsigned long mmcr[])
{
	if (pmc <= 3)
		mmcr[1] &= ~(0xffUL << MMCR1_PMCSEL_SHIFT(pmc + 1));
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
	.compute_mmcr		= power8_compute_mmcr,
	.config_bhrb		= power8_config_bhrb,
	.bhrb_filter_map	= power8_bhrb_filter_map,
	.get_constraint		= power8_get_constraint,
	.get_alternatives	= power8_get_alternatives,
	.disable_pmc		= power8_disable_pmc,
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
