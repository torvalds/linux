/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009 Paul Mackerras, IBM Corporation.
 * Copyright 2013 Michael Ellerman, IBM Corporation.
 * Copyright 2016 Madhavan Srinivasan, IBM Corporation.
 */

#ifndef _LINUX_POWERPC_PERF_ISA207_COMMON_H_
#define _LINUX_POWERPC_PERF_ISA207_COMMON_H_

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <asm/firmware.h>
#include <asm/cputable.h>

#include "internal.h"

#define EVENT_EBB_MASK		1ull
#define EVENT_EBB_SHIFT		PERF_EVENT_CONFIG_EBB_SHIFT
#define EVENT_BHRB_MASK		1ull
#define EVENT_BHRB_SHIFT	62
#define EVENT_WANTS_BHRB	(EVENT_BHRB_MASK << EVENT_BHRB_SHIFT)
#define EVENT_IFM_MASK		3ull
#define EVENT_IFM_SHIFT		60
#define EVENT_THR_CMP_SHIFT	40	/* Threshold CMP value */
#define EVENT_THR_CMP_MASK	0x3ff
#define EVENT_THR_CTL_SHIFT	32	/* Threshold control value (start/stop) */
#define EVENT_THR_CTL_MASK	0xffull
#define EVENT_THR_SEL_SHIFT	29	/* Threshold select value */
#define EVENT_THR_SEL_MASK	0x7
#define EVENT_THRESH_SHIFT	29	/* All threshold bits */
#define EVENT_THRESH_MASK	0x1fffffull
#define EVENT_SAMPLE_SHIFT	24	/* Sampling mode & eligibility */
#define EVENT_SAMPLE_MASK	0x1f
#define EVENT_CACHE_SEL_SHIFT	20	/* L2/L3 cache select */
#define EVENT_CACHE_SEL_MASK	0xf
#define EVENT_IS_L1		(4 << EVENT_CACHE_SEL_SHIFT)
#define EVENT_PMC_SHIFT		16	/* PMC number (1-based) */
#define EVENT_PMC_MASK		0xf
#define EVENT_UNIT_SHIFT	12	/* Unit */
#define EVENT_UNIT_MASK		0xf
#define EVENT_COMBINE_SHIFT	11	/* Combine bit */
#define EVENT_COMBINE_MASK	0x1
#define EVENT_COMBINE(v)	(((v) >> EVENT_COMBINE_SHIFT) & EVENT_COMBINE_MASK)
#define EVENT_MARKED_SHIFT	8	/* Marked bit */
#define EVENT_MARKED_MASK	0x1
#define EVENT_IS_MARKED		(EVENT_MARKED_MASK << EVENT_MARKED_SHIFT)
#define EVENT_PSEL_MASK		0xff	/* PMCxSEL value */

/* Bits defined by Linux */
#define EVENT_LINUX_MASK	\
	((EVENT_EBB_MASK  << EVENT_EBB_SHIFT)			|	\
	 (EVENT_BHRB_MASK << EVENT_BHRB_SHIFT)			|	\
	 (EVENT_IFM_MASK  << EVENT_IFM_SHIFT))

#define EVENT_VALID_MASK	\
	((EVENT_THRESH_MASK    << EVENT_THRESH_SHIFT)		|	\
	 (EVENT_SAMPLE_MASK    << EVENT_SAMPLE_SHIFT)		|	\
	 (EVENT_CACHE_SEL_MASK << EVENT_CACHE_SEL_SHIFT)	|	\
	 (EVENT_PMC_MASK       << EVENT_PMC_SHIFT)		|	\
	 (EVENT_UNIT_MASK      << EVENT_UNIT_SHIFT)		|	\
	 (EVENT_COMBINE_MASK   << EVENT_COMBINE_SHIFT)		|	\
	 (EVENT_MARKED_MASK    << EVENT_MARKED_SHIFT)		|	\
	  EVENT_LINUX_MASK					|	\
	  EVENT_PSEL_MASK)

#define ONLY_PLM \
	(PERF_SAMPLE_BRANCH_USER        |\
	 PERF_SAMPLE_BRANCH_KERNEL      |\
	 PERF_SAMPLE_BRANCH_HV)

/* Contants to support power9 raw encoding format */
#define p9_EVENT_COMBINE_SHIFT	10	/* Combine bit */
#define p9_EVENT_COMBINE_MASK	0x3ull
#define p9_EVENT_COMBINE(v)	(((v) >> p9_EVENT_COMBINE_SHIFT) & p9_EVENT_COMBINE_MASK)
#define p9_SDAR_MODE_SHIFT	50
#define p9_SDAR_MODE_MASK	0x3ull
#define p9_SDAR_MODE(v)		(((v) >> p9_SDAR_MODE_SHIFT) & p9_SDAR_MODE_MASK)

#define p9_EVENT_VALID_MASK		\
	((p9_SDAR_MODE_MASK   << p9_SDAR_MODE_SHIFT		|	\
	(EVENT_THRESH_MASK    << EVENT_THRESH_SHIFT)		|	\
	(EVENT_SAMPLE_MASK    << EVENT_SAMPLE_SHIFT)		|	\
	(EVENT_CACHE_SEL_MASK << EVENT_CACHE_SEL_SHIFT)		|	\
	(EVENT_PMC_MASK       << EVENT_PMC_SHIFT)		|	\
	(EVENT_UNIT_MASK      << EVENT_UNIT_SHIFT)		|	\
	(p9_EVENT_COMBINE_MASK << p9_EVENT_COMBINE_SHIFT)	|	\
	(EVENT_MARKED_MASK    << EVENT_MARKED_SHIFT)		|	\
	 EVENT_LINUX_MASK					|	\
	 EVENT_PSEL_MASK))

/* Contants to support power10 raw encoding format */
#define p10_SDAR_MODE_SHIFT		22
#define p10_SDAR_MODE_MASK		0x3ull
#define p10_SDAR_MODE(v)		(((v) >> p10_SDAR_MODE_SHIFT) & \
					p10_SDAR_MODE_MASK)
#define p10_EVENT_L2L3_SEL_MASK		0x1f
#define p10_L2L3_SEL_SHIFT		3
#define p10_L2L3_EVENT_SHIFT		40
#define p10_EVENT_THRESH_MASK		0xffffull
#define p10_EVENT_CACHE_SEL_MASK	0x3ull
#define p10_EVENT_MMCR3_MASK		0x7fffull
#define p10_EVENT_MMCR3_SHIFT		45
#define p10_EVENT_RADIX_SCOPE_QUAL_SHIFT	9
#define p10_EVENT_RADIX_SCOPE_QUAL_MASK	0x1
#define p10_MMCR1_RADIX_SCOPE_QUAL_SHIFT	45

/* Event Threshold Compare bit constant for power10 in config1 attribute */
#define p10_EVENT_THR_CMP_SHIFT        0
#define p10_EVENT_THR_CMP_MASK 0x3FFFFull

#define p10_EVENT_VALID_MASK		\
	((p10_SDAR_MODE_MASK   << p10_SDAR_MODE_SHIFT		|	\
	(p10_EVENT_THRESH_MASK  << EVENT_THRESH_SHIFT)		|	\
	(EVENT_SAMPLE_MASK     << EVENT_SAMPLE_SHIFT)		|	\
	(p10_EVENT_CACHE_SEL_MASK  << EVENT_CACHE_SEL_SHIFT)	|	\
	(EVENT_PMC_MASK        << EVENT_PMC_SHIFT)		|	\
	(EVENT_UNIT_MASK       << EVENT_UNIT_SHIFT)		|	\
	(p9_EVENT_COMBINE_MASK << p9_EVENT_COMBINE_SHIFT)	|	\
	(p10_EVENT_MMCR3_MASK  << p10_EVENT_MMCR3_SHIFT)	|	\
	(EVENT_MARKED_MASK     << EVENT_MARKED_SHIFT)		|	\
	(p10_EVENT_RADIX_SCOPE_QUAL_MASK << p10_EVENT_RADIX_SCOPE_QUAL_SHIFT)	|	\
	 EVENT_LINUX_MASK					|	\
	EVENT_PSEL_MASK))
/*
 * Layout of constraint bits:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   [   fab_match   ]         [       thresh_cmp      ] [   thresh_ctl    ] [   ]
 *                                          |                                  |
 *                           [  thresh_cmp bits for p10]           thresh_sel -*
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *               [ ] |   [ ] |  [  sample ]   [     ]   [6] [5]   [4] [3]   [2] [1]
 *                |  |    |  |                  |
 *      BHRB IFM -*  |    |  |*radix_scope      |      Count of events for each PMC.
 *              EBB -*    |                     |        p1, p2, p3, p4, p5, p6.
 *      L1 I/D qualifier -*                     |
 *                     nc - number of counters -*
 *
 * The PMC fields P1..P6, and NC, are adder fields. As we accumulate constraints
 * we want the low bit of each field to be added to any existing value.
 *
 * Everything else is a value field.
 */

#define CNST_FAB_MATCH_VAL(v)	(((v) & EVENT_THR_CTL_MASK) << 56)
#define CNST_FAB_MATCH_MASK	CNST_FAB_MATCH_VAL(EVENT_THR_CTL_MASK)

/* We just throw all the threshold bits into the constraint */
#define CNST_THRESH_VAL(v)	(((v) & EVENT_THRESH_MASK) << 32)
#define CNST_THRESH_MASK	CNST_THRESH_VAL(EVENT_THRESH_MASK)

#define CNST_THRESH_CTL_SEL_VAL(v)	(((v) & 0x7ffull) << 32)
#define CNST_THRESH_CTL_SEL_MASK	CNST_THRESH_CTL_SEL_VAL(0x7ff)

#define p10_CNST_THRESH_CMP_VAL(v) (((v) & 0x7ffull) << 43)
#define p10_CNST_THRESH_CMP_MASK   p10_CNST_THRESH_CMP_VAL(0x7ff)

#define CNST_EBB_VAL(v)		(((v) & EVENT_EBB_MASK) << 24)
#define CNST_EBB_MASK		CNST_EBB_VAL(EVENT_EBB_MASK)

#define CNST_IFM_VAL(v)		(((v) & EVENT_IFM_MASK) << 25)
#define CNST_IFM_MASK		CNST_IFM_VAL(EVENT_IFM_MASK)

#define CNST_L1_QUAL_VAL(v)	(((v) & 3) << 22)
#define CNST_L1_QUAL_MASK	CNST_L1_QUAL_VAL(3)

#define CNST_SAMPLE_VAL(v)	(((v) & EVENT_SAMPLE_MASK) << 16)
#define CNST_SAMPLE_MASK	CNST_SAMPLE_VAL(EVENT_SAMPLE_MASK)

#define CNST_CACHE_GROUP_VAL(v)	(((v) & 0xffull) << 55)
#define CNST_CACHE_GROUP_MASK	CNST_CACHE_GROUP_VAL(0xff)
#define CNST_CACHE_PMC4_VAL	(1ull << 54)
#define CNST_CACHE_PMC4_MASK	CNST_CACHE_PMC4_VAL

#define CNST_L2L3_GROUP_VAL(v)	(((v) & 0x1full) << 55)
#define CNST_L2L3_GROUP_MASK	CNST_L2L3_GROUP_VAL(0x1f)

#define CNST_RADIX_SCOPE_GROUP_VAL(v)	(((v) & 0x1ull) << 21)
#define CNST_RADIX_SCOPE_GROUP_MASK	CNST_RADIX_SCOPE_GROUP_VAL(1)

/*
 * For NC we are counting up to 4 events. This requires three bits, and we need
 * the fifth event to overflow and set the 4th bit. To achieve that we bias the
 * fields by 3 in test_adder.
 */
#define CNST_NC_SHIFT		12
#define CNST_NC_VAL		(1 << CNST_NC_SHIFT)
#define CNST_NC_MASK		(8 << CNST_NC_SHIFT)
#define ISA207_TEST_ADDER	(3 << CNST_NC_SHIFT)

/*
 * For the per-PMC fields we have two bits. The low bit is added, so if two
 * events ask for the same PMC the sum will overflow, setting the high bit,
 * indicating an error. So our mask sets the high bit.
 */
#define CNST_PMC_SHIFT(pmc)	((pmc - 1) * 2)
#define CNST_PMC_VAL(pmc)	(1 << CNST_PMC_SHIFT(pmc))
#define CNST_PMC_MASK(pmc)	(2 << CNST_PMC_SHIFT(pmc))

/* Our add_fields is defined as: */
#define ISA207_ADD_FIELDS	\
	CNST_PMC_VAL(1) | CNST_PMC_VAL(2) | CNST_PMC_VAL(3) | \
	CNST_PMC_VAL(4) | CNST_PMC_VAL(5) | CNST_PMC_VAL(6) | CNST_NC_VAL

/* Bits in MMCR1 for PowerISA v2.07 */
#define MMCR1_UNIT_SHIFT(pmc)		(60 - (4 * ((pmc) - 1)))
#define MMCR1_COMBINE_SHIFT(pmc)	(35 - ((pmc) - 1))
#define MMCR1_PMCSEL_SHIFT(pmc)		(24 - (((pmc) - 1)) * 8)
#define MMCR1_FAB_SHIFT			36
#define MMCR1_DC_IC_QUAL_MASK		0x3
#define MMCR1_DC_IC_QUAL_SHIFT		46

/* MMCR1 Combine bits macro for power9 */
#define p9_MMCR1_COMBINE_SHIFT(pmc)	(38 - ((pmc - 1) * 2))

/* Bits in MMCRA for PowerISA v2.07 */
#define MMCRA_SAMP_MODE_SHIFT		1
#define MMCRA_SAMP_ELIG_SHIFT		4
#define MMCRA_SAMP_ELIG_MASK		7
#define MMCRA_THR_CTL_SHIFT		8
#define MMCRA_THR_SEL_SHIFT		16
#define MMCRA_THR_CMP_SHIFT		32
#define MMCRA_SDAR_MODE_SHIFT		42
#define MMCRA_SDAR_MODE_TLB		(1ull << MMCRA_SDAR_MODE_SHIFT)
#define MMCRA_SDAR_MODE_NO_UPDATES	~(0x3ull << MMCRA_SDAR_MODE_SHIFT)
#define MMCRA_SDAR_MODE_DCACHE		(2ull << MMCRA_SDAR_MODE_SHIFT)
#define MMCRA_IFM_SHIFT			30
#define MMCRA_THR_CTR_MANT_SHIFT	19
#define MMCRA_THR_CTR_MANT_MASK		0x7Ful
#define MMCRA_THR_CTR_MANT(v)		(((v) >> MMCRA_THR_CTR_MANT_SHIFT) &\
						MMCRA_THR_CTR_MANT_MASK)

#define MMCRA_THR_CTR_EXP_SHIFT		27
#define MMCRA_THR_CTR_EXP_MASK		0x7ul
#define MMCRA_THR_CTR_EXP(v)		(((v) >> MMCRA_THR_CTR_EXP_SHIFT) &\
						MMCRA_THR_CTR_EXP_MASK)

#define P10_MMCRA_THR_CTR_MANT_MASK	0xFFul
#define P10_MMCRA_THR_CTR_MANT(v)	(((v) >> MMCRA_THR_CTR_MANT_SHIFT) &\
						P10_MMCRA_THR_CTR_MANT_MASK)

/* MMCRA Threshold Compare bit constant for power9 */
#define p9_MMCRA_THR_CMP_SHIFT	45

/* Bits in MMCR2 for PowerISA v2.07 */
#define MMCR2_FCS(pmc)			(1ull << (63 - (((pmc) - 1) * 9)))
#define MMCR2_FCP(pmc)			(1ull << (62 - (((pmc) - 1) * 9)))
#define MMCR2_FCH(pmc)			(1ull << (57 - (((pmc) - 1) * 9)))

#define MAX_ALT				2
#define MAX_PMU_COUNTERS		6

/* Bits in MMCR3 for PowerISA v3.10 */
#define MMCR3_SHIFT(pmc)		(49 - (15 * ((pmc) - 1)))

#define ISA207_SIER_TYPE_SHIFT		15
#define ISA207_SIER_TYPE_MASK		(0x7ull << ISA207_SIER_TYPE_SHIFT)

#define ISA207_SIER_LDST_SHIFT		1
#define ISA207_SIER_LDST_MASK		(0x7ull << ISA207_SIER_LDST_SHIFT)

#define ISA207_SIER_DATA_SRC_SHIFT	53
#define ISA207_SIER_DATA_SRC_MASK	(0x7ull << ISA207_SIER_DATA_SRC_SHIFT)

/* Bits in SIER2/SIER3 for Power10 */
#define P10_SIER2_FINISH_CYC(sier2)	(((sier2) >> (63 - 37)) & 0x7fful)
#define P10_SIER2_DISPATCH_CYC(sier2)	(((sier2) >> (63 - 13)) & 0x7fful)

#define P(a, b)				PERF_MEM_S(a, b)
#define PH(a, b)			(P(LVL, HIT) | P(a, b))
#define PM(a, b)			(P(LVL, MISS) | P(a, b))
#define LEVEL(x)			P(LVLNUM, x)
#define REM				P(REMOTE, REMOTE)

int isa207_get_constraint(u64 event, unsigned long *maskp, unsigned long *valp, u64 event_config1);
int isa207_compute_mmcr(u64 event[], int n_ev,
				unsigned int hwc[], struct mmcr_regs *mmcr,
				struct perf_event *pevents[], u32 flags);
void isa207_disable_pmc(unsigned int pmc, struct mmcr_regs *mmcr);
int isa207_get_alternatives(u64 event, u64 alt[], int size, unsigned int flags,
					const unsigned int ev_alt[][MAX_ALT]);
void isa207_get_mem_data_src(union perf_mem_data_src *dsrc, u32 flags,
							struct pt_regs *regs);
void isa207_get_mem_weight(u64 *weight, u64 type);

int isa3XX_check_attr_config(struct perf_event *ev);

#endif
