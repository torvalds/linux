// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2019 Madhavan Srinivasan, IBM Corporation.

#define pr_fmt(fmt)	"generic-compat-pmu: " fmt

#include "isa207-common.h"

/*
 * Raw event encoding:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *                                 [ pmc ]                       [    pmcxsel    ]
 */

/*
 * Event codes defined in ISA v3.0B
 */
#define EVENT(_name, _code)	_name = _code,

enum {
	/* Cycles, alternate code */
	EVENT(PM_CYC_ALT,			0x100f0)
	/* One or more instructions completed in a cycle */
	EVENT(PM_CYC_INST_CMPL,			0x100f2)
	/* Floating-point instruction completed */
	EVENT(PM_FLOP_CMPL,			0x100f4)
	/* Instruction ERAT/L1-TLB miss */
	EVENT(PM_L1_ITLB_MISS,			0x100f6)
	/* All instructions completed and none available */
	EVENT(PM_NO_INST_AVAIL,			0x100f8)
	/* A load-type instruction completed (ISA v3.0+) */
	EVENT(PM_LD_CMPL,			0x100fc)
	/* Instruction completed, alternate code (ISA v3.0+) */
	EVENT(PM_INST_CMPL_ALT,			0x100fe)
	/* A store-type instruction completed */
	EVENT(PM_ST_CMPL,			0x200f0)
	/* Instruction Dispatched */
	EVENT(PM_INST_DISP,			0x200f2)
	/* Run_cycles */
	EVENT(PM_RUN_CYC,			0x200f4)
	/* Data ERAT/L1-TLB miss/reload */
	EVENT(PM_L1_DTLB_RELOAD,		0x200f6)
	/* Taken branch completed */
	EVENT(PM_BR_TAKEN_CMPL,			0x200fa)
	/* Demand iCache Miss */
	EVENT(PM_L1_ICACHE_MISS,		0x200fc)
	/* L1 Dcache reload from memory */
	EVENT(PM_L1_RELOAD_FROM_MEM,		0x200fe)
	/* L1 Dcache store miss */
	EVENT(PM_ST_MISS_L1,			0x300f0)
	/* Alternate code for PM_INST_DISP */
	EVENT(PM_INST_DISP_ALT,			0x300f2)
	/* Branch direction or target mispredicted */
	EVENT(PM_BR_MISPREDICT,			0x300f6)
	/* Data TLB miss/reload */
	EVENT(PM_DTLB_MISS,			0x300fc)
	/* Demand LD - L3 Miss (not L2 hit and not L3 hit) */
	EVENT(PM_DATA_FROM_L3MISS,		0x300fe)
	/* L1 Dcache load miss */
	EVENT(PM_LD_MISS_L1,			0x400f0)
	/* Cycle when instruction(s) dispatched */
	EVENT(PM_CYC_INST_DISP,			0x400f2)
	/* Branch or branch target mispredicted */
	EVENT(PM_BR_MPRED_CMPL,			0x400f6)
	/* Instructions completed with run latch set */
	EVENT(PM_RUN_INST_CMPL,			0x400fa)
	/* Instruction TLB miss/reload */
	EVENT(PM_ITLB_MISS,			0x400fc)
	/* Load data not cached */
	EVENT(PM_LD_NOT_CACHED,			0x400fe)
	/* Instructions */
	EVENT(PM_INST_CMPL,			0x500fa)
	/* Cycles */
	EVENT(PM_CYC,				0x600f4)
};

#undef EVENT

/* Table of alternatives, sorted in increasing order of column 0 */
/* Note that in each row, column 0 must be the smallest */
static const unsigned int generic_event_alternatives[][MAX_ALT] = {
	{ PM_CYC_ALT,			PM_CYC },
	{ PM_INST_CMPL_ALT,		PM_INST_CMPL },
	{ PM_INST_DISP,			PM_INST_DISP_ALT },
};

static int generic_get_alternatives(u64 event, unsigned int flags, u64 alt[])
{
	int num_alt = 0;

	num_alt = isa207_get_alternatives(event, alt,
					  ARRAY_SIZE(generic_event_alternatives), flags,
					  generic_event_alternatives);

	return num_alt;
}

GENERIC_EVENT_ATTR(cpu-cycles,			PM_CYC);
GENERIC_EVENT_ATTR(instructions,		PM_INST_CMPL);
GENERIC_EVENT_ATTR(stalled-cycles-frontend,	PM_NO_INST_AVAIL);
GENERIC_EVENT_ATTR(branch-misses,		PM_BR_MPRED_CMPL);
GENERIC_EVENT_ATTR(cache-misses,		PM_LD_MISS_L1);

CACHE_EVENT_ATTR(L1-dcache-load-misses,		PM_LD_MISS_L1);
CACHE_EVENT_ATTR(L1-dcache-store-misses,	PM_ST_MISS_L1);
CACHE_EVENT_ATTR(L1-icache-load-misses,		PM_L1_ICACHE_MISS);
CACHE_EVENT_ATTR(LLC-load-misses,		PM_DATA_FROM_L3MISS);
CACHE_EVENT_ATTR(branch-load-misses,		PM_BR_MPRED_CMPL);
CACHE_EVENT_ATTR(dTLB-load-misses,		PM_DTLB_MISS);
CACHE_EVENT_ATTR(iTLB-load-misses,		PM_ITLB_MISS);

static struct attribute *generic_compat_events_attr[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	GENERIC_EVENT_PTR(PM_NO_INST_AVAIL),
	GENERIC_EVENT_PTR(PM_BR_MPRED_CMPL),
	GENERIC_EVENT_PTR(PM_LD_MISS_L1),
	CACHE_EVENT_PTR(PM_LD_MISS_L1),
	CACHE_EVENT_PTR(PM_ST_MISS_L1),
	CACHE_EVENT_PTR(PM_L1_ICACHE_MISS),
	CACHE_EVENT_PTR(PM_DATA_FROM_L3MISS),
	CACHE_EVENT_PTR(PM_BR_MPRED_CMPL),
	CACHE_EVENT_PTR(PM_DTLB_MISS),
	CACHE_EVENT_PTR(PM_ITLB_MISS),
	NULL
};

static const struct attribute_group generic_compat_pmu_events_group = {
	.name = "events",
	.attrs = generic_compat_events_attr,
};

PMU_FORMAT_ATTR(event,		"config:0-19");
PMU_FORMAT_ATTR(pmcxsel,	"config:0-7");
PMU_FORMAT_ATTR(pmc,		"config:16-19");

static struct attribute *generic_compat_pmu_format_attr[] = {
	&format_attr_event.attr,
	&format_attr_pmcxsel.attr,
	&format_attr_pmc.attr,
	NULL,
};

static const struct attribute_group generic_compat_pmu_format_group = {
	.name = "format",
	.attrs = generic_compat_pmu_format_attr,
};

static struct attribute *generic_compat_pmu_caps_attrs[] = {
	NULL
};

static struct attribute_group generic_compat_pmu_caps_group = {
	.name  = "caps",
	.attrs = generic_compat_pmu_caps_attrs,
};

static const struct attribute_group *generic_compat_pmu_attr_groups[] = {
	&generic_compat_pmu_format_group,
	&generic_compat_pmu_events_group,
	&generic_compat_pmu_caps_group,
	NULL,
};

static int compat_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] =	PM_NO_INST_AVAIL,
	[PERF_COUNT_HW_BRANCH_MISSES] =			PM_BR_MPRED_CMPL,
	[PERF_COUNT_HW_CACHE_MISSES] =			PM_LD_MISS_L1,
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

/*
 * Table of generalized cache-related events.
 * 0 means not supported, -1 means nonsensical, other values
 * are event codes.
 */
static u64 generic_compat_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[ C(L1D) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_LD_MISS_L1,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_ST_MISS_L1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},
	[ C(L1I) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_L1_ICACHE_MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},
	[ C(LL) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = PM_DATA_FROM_L3MISS,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
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
			[ C(RESULT_ACCESS) ] = 0,
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

/*
 * We set MMCR0[CC5-6RUN] so we can use counters 5 and 6 for
 * PM_INST_CMPL and PM_CYC.
 */
static int generic_compute_mmcr(u64 event[], int n_ev,
				unsigned int hwc[], struct mmcr_regs *mmcr,
				struct perf_event *pevents[], u32 flags)
{
	int ret;

	ret = isa207_compute_mmcr(event, n_ev, hwc, mmcr, pevents, flags);
	if (!ret)
		mmcr->mmcr0 |= MMCR0_C56RUN;
	return ret;
}

static struct power_pmu generic_compat_pmu = {
	.name			= "ISAv3",
	.n_counter		= MAX_PMU_COUNTERS,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.compute_mmcr		= generic_compute_mmcr,
	.get_constraint		= isa207_get_constraint,
	.get_alternatives	= generic_get_alternatives,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_HAS_SIER | PPMU_ARCH_207S,
	.n_generic		= ARRAY_SIZE(compat_generic_events),
	.generic_events		= compat_generic_events,
	.cache_events		= &generic_compat_cache_events,
	.attr_groups		= generic_compat_pmu_attr_groups,
};

int __init init_generic_compat_pmu(void)
{
	int rc = 0;

	/*
	 * From ISA v2.07 on, PMU features are architected;
	 * we require >= v3.0 because (a) that has PM_LD_CMPL and
	 * PM_INST_CMPL_ALT, which v2.07 doesn't have, and
	 * (b) we don't expect any non-IBM Power ISA
	 * implementations that conform to v2.07 but not v3.0.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		return -ENODEV;

	rc = register_power_pmu(&generic_compat_pmu);
	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	return 0;
}
