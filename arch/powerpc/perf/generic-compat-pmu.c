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
 *                                 [ pmc ]   [unit ]   [ ]   m   [    pmcxsel    ]
 *                                                     |     |
 *                                                     |     *- mark
 *                                                     |
 *                                                     |
 *                                                     *- combine
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
 */

/*
 * Some power9 event codes.
 */
#define EVENT(_name, _code)	_name = _code,

enum {
EVENT(PM_CYC,					0x0001e)
EVENT(PM_INST_CMPL,				0x00002)
};

#undef EVENT

GENERIC_EVENT_ATTR(cpu-cycles,			PM_CYC);
GENERIC_EVENT_ATTR(instructions,		PM_INST_CMPL);

static struct attribute *generic_compat_events_attr[] = {
	GENERIC_EVENT_PTR(PM_CYC),
	GENERIC_EVENT_PTR(PM_INST_CMPL),
	NULL
};

static struct attribute_group generic_compat_pmu_events_group = {
	.name = "events",
	.attrs = generic_compat_events_attr,
};

PMU_FORMAT_ATTR(event,		"config:0-19");
PMU_FORMAT_ATTR(pmcxsel,	"config:0-7");
PMU_FORMAT_ATTR(mark,		"config:8");
PMU_FORMAT_ATTR(combine,	"config:10-11");
PMU_FORMAT_ATTR(unit,		"config:12-15");
PMU_FORMAT_ATTR(pmc,		"config:16-19");

static struct attribute *generic_compat_pmu_format_attr[] = {
	&format_attr_event.attr,
	&format_attr_pmcxsel.attr,
	&format_attr_mark.attr,
	&format_attr_combine.attr,
	&format_attr_unit.attr,
	&format_attr_pmc.attr,
	NULL,
};

static struct attribute_group generic_compat_pmu_format_group = {
	.name = "format",
	.attrs = generic_compat_pmu_format_attr,
};

static const struct attribute_group *generic_compat_pmu_attr_groups[] = {
	&generic_compat_pmu_format_group,
	&generic_compat_pmu_events_group,
	NULL,
};

static int compat_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] =			PM_CYC,
	[PERF_COUNT_HW_INSTRUCTIONS] =			PM_INST_CMPL,
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
			[ C(RESULT_MISS)   ] = 0,
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
	[ C(L1I) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
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
			[ C(RESULT_MISS)   ] = 0,
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
			[ C(RESULT_MISS)   ] = 0,
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
			[ C(RESULT_MISS)   ] = 0,
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
			[ C(RESULT_MISS)   ] = 0,
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

static struct power_pmu generic_compat_pmu = {
	.name			= "GENERIC_COMPAT",
	.n_counter		= MAX_PMU_COUNTERS,
	.add_fields		= ISA207_ADD_FIELDS,
	.test_adder		= ISA207_TEST_ADDER,
	.compute_mmcr		= isa207_compute_mmcr,
	.get_constraint		= isa207_get_constraint,
	.disable_pmc		= isa207_disable_pmc,
	.flags			= PPMU_HAS_SIER | PPMU_ARCH_207S,
	.n_generic		= ARRAY_SIZE(compat_generic_events),
	.generic_events		= compat_generic_events,
	.cache_events		= &generic_compat_cache_events,
	.attr_groups		= generic_compat_pmu_attr_groups,
};

int init_generic_compat_pmu(void)
{
	int rc = 0;

	rc = register_power_pmu(&generic_compat_pmu);
	if (rc)
		return rc;

	/* Tell userspace that EBB is supported */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_EBB;

	return 0;
}
