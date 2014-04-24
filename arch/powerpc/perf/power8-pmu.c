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

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <asm/firmware.h>


/*
 * Some power8 event codes.
 */
#define PM_CYC				0x0001e
#define PM_GCT_NOSLOT_CYC		0x100f8
#define PM_CMPLU_STALL			0x4000a
#define PM_INST_CMPL			0x00002
#define PM_BRU_FIN			0x10068
#define PM_BR_MPRED_CMPL		0x400f6

/* All L1 D cache load references counted at finish, gated by reject */
#define PM_LD_REF_L1			0x100ee
/* Load Missed L1 */
#define PM_LD_MISS_L1			0x3e054
/* Store Missed L1 */
#define PM_ST_MISS_L1			0x300f0
/* L1 cache data prefetches */
#define PM_L1_PREF			0x0d8b8
/* Instruction fetches from L1 */
#define PM_INST_FROM_L1			0x04080
/* Demand iCache Miss */
#define PM_L1_ICACHE_MISS		0x200fd
/* Instruction Demand sectors wriittent into IL1 */
#define PM_L1_DEMAND_WRITE		0x0408c
/* Instruction prefetch written into IL1 */
#define PM_IC_PREF_WRITE		0x0408e
/* The data cache was reloaded from local core's L3 due to a demand load */
#define PM_DATA_FROM_L3			0x4c042
/* Demand LD - L3 Miss (not L2 hit and not L3 hit) */
#define PM_DATA_FROM_L3MISS		0x300fe
/* All successful D-side store dispatches for this thread */
#define PM_L2_ST			0x17080
/* All successful D-side store dispatches for this thread that were L2 Miss */
#define PM_L2_ST_MISS			0x17082
/* Total HW L3 prefetches(Load+store) */
#define PM_L3_PREF_ALL			0x4e052
/* Data PTEG reload */
#define PM_DTLB_MISS			0x300fc
/* ITLB Reloaded */
#define PM_ITLB_MISS			0x400fc


/*
 * Raw event encoding for POWER8:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   | | [ ]                           [      thresh_cmp     ]   [  thresh_ctl   ]
 *   | |  |                                                              |
 *   | |  *- IFM (Linux)                 thresh start/stop OR FAB match -*
 *   | *- BHRB (Linux)
 *   *- EBB (Linux)
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   [   ] [  sample ]   [cache]   [ pmc ]   [unit ]   c     m   [    pmcxsel    ]
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
 * MMCR1[x]   = combine (PMCxCOMB)
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
 *	MMCRA[22:24] = thresh_cmp[0:2]
 *	MMCRA[25:31] = thresh_cmp[3:9]
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
 */

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

/* MMCRA IFM bits - POWER8 */
#define	POWER8_MMCRA_IFM1		0x0000000040000000UL
#define	POWER8_MMCRA_IFM2		0x0000000080000000UL
#define	POWER8_MMCRA_IFM3		0x00000000C0000000UL

#define ONLY_PLM \
	(PERF_SAMPLE_BRANCH_USER        |\
	 PERF_SAMPLE_BRANCH_KERNEL      |\
	 PERF_SAMPLE_BRANCH_HV)

/*
 * Layout of constraint bits:
 *
 *        60        56        52        48        44        40        36        32
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   [   fab_match   ]         [       thresh_cmp      ] [   thresh_ctl    ] [   ]
 *                                                                             |
 *                                                                 thresh_sel -*
 *
 *        28        24        20        16        12         8         4         0
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *               [ ] |   [ ]   [  sample ]   [     ]   [6] [5]   [4] [3]   [2] [1]
 *                |  |    |                     |
 *      BHRB IFM -*  |    |                     |      Count of events for each PMC.
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

#define CNST_EBB_VAL(v)		(((v) & EVENT_EBB_MASK) << 24)
#define CNST_EBB_MASK		CNST_EBB_VAL(EVENT_EBB_MASK)

#define CNST_IFM_VAL(v)		(((v) & EVENT_IFM_MASK) << 25)
#define CNST_IFM_MASK		CNST_IFM_VAL(EVENT_IFM_MASK)

#define CNST_L1_QUAL_VAL(v)	(((v) & 3) << 22)
#define CNST_L1_QUAL_MASK	CNST_L1_QUAL_VAL(3)

#define CNST_SAMPLE_VAL(v)	(((v) & EVENT_SAMPLE_MASK) << 16)
#define CNST_SAMPLE_MASK	CNST_SAMPLE_VAL(EVENT_SAMPLE_MASK)

/*
 * For NC we are counting up to 4 events. This requires three bits, and we need
 * the fifth event to overflow and set the 4th bit. To achieve that we bias the
 * fields by 3 in test_adder.
 */
#define CNST_NC_SHIFT		12
#define CNST_NC_VAL		(1 << CNST_NC_SHIFT)
#define CNST_NC_MASK		(8 << CNST_NC_SHIFT)
#define POWER8_TEST_ADDER	(3 << CNST_NC_SHIFT)

/*
 * For the per-PMC fields we have two bits. The low bit is added, so if two
 * events ask for the same PMC the sum will overflow, setting the high bit,
 * indicating an error. So our mask sets the high bit.
 */
#define CNST_PMC_SHIFT(pmc)	((pmc - 1) * 2)
#define CNST_PMC_VAL(pmc)	(1 << CNST_PMC_SHIFT(pmc))
#define CNST_PMC_MASK(pmc)	(2 << CNST_PMC_SHIFT(pmc))

/* Our add_fields is defined as: */
#define POWER8_ADD_FIELDS	\
	CNST_PMC_VAL(1) | CNST_PMC_VAL(2) | CNST_PMC_VAL(3) | \
	CNST_PMC_VAL(4) | CNST_PMC_VAL(5) | CNST_PMC_VAL(6) | CNST_NC_VAL


/* Bits in MMCR1 for POWER8 */
#define MMCR1_UNIT_SHIFT(pmc)		(60 - (4 * ((pmc) - 1)))
#define MMCR1_COMBINE_SHIFT(pmc)	(35 - ((pmc) - 1))
#define MMCR1_PMCSEL_SHIFT(pmc)		(24 - (((pmc) - 1)) * 8)
#define MMCR1_FAB_SHIFT			36
#define MMCR1_DC_QUAL_SHIFT		47
#define MMCR1_IC_QUAL_SHIFT		46

/* Bits in MMCRA for POWER8 */
#define MMCRA_SAMP_MODE_SHIFT		1
#define MMCRA_SAMP_ELIG_SHIFT		4
#define MMCRA_THR_CTL_SHIFT		8
#define MMCRA_THR_SEL_SHIFT		16
#define MMCRA_THR_CMP_SHIFT		32
#define MMCRA_SDAR_MODE_TLB		(1ull << 42)
#define MMCRA_IFM_SHIFT			30


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

		if (pmc >= 5 && base_event != 0x500fa && base_event != 0x600f4)
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
			       unsigned int hwc[], unsigned long mmcr[])
{
	unsigned long mmcra, mmcr1, unit, combine, psel, cache, val;
	unsigned int pmc, pmc_inuse;
	int i;

	pmc_inuse = 0;

	/* First pass to count resource use */
	for (i = 0; i < n_ev; ++i) {
		pmc = (event[i] >> EVENT_PMC_SHIFT) & EVENT_PMC_MASK;
		if (pmc)
			pmc_inuse |= 1 << pmc;
	}

	/* In continous sampling mode, update SDAR on TLB miss */
	mmcra = MMCRA_SDAR_MODE_TLB;
	mmcr1 = 0;

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

	return 0;
}

#define MAX_ALT	2

/* Table of alternatives, sorted by column 0 */
static const unsigned int event_alternatives[][MAX_ALT] = {
	{ 0x10134, 0x301e2 },		/* PM_MRK_ST_CMPL */
	{ 0x10138, 0x40138 },		/* PM_BR_MRK_2PATH */
	{ 0x18082, 0x3e05e },		/* PM_L3_CO_MEPF */
	{ 0x1d14e, 0x401e8 },		/* PM_MRK_DATA_FROM_L2MISS */
	{ 0x1e054, 0x4000a },		/* PM_CMPLU_STALL */
	{ 0x20036, 0x40036 },		/* PM_BR_2PATH */
	{ 0x200f2, 0x300f2 },		/* PM_INST_DISP */
	{ 0x200f4, 0x600f4 },		/* PM_RUN_CYC */
	{ 0x2013c, 0x3012e },		/* PM_MRK_FILT_MATCH */
	{ 0x3e054, 0x400f0 },		/* PM_LD_MISS_L1 */
	{ 0x400fa, 0x500fa },		/* PM_RUN_INST_CMPL */
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
			case 0x1e:	/* PM_CYC */
				alt[j++] = 0x600f4;	/* PM_RUN_CYC */
				break;
			case 0x600f4:	/* PM_RUN_CYC */
				alt[j++] = 0x1e;
				break;
			case 0x2:	/* PM_PPC_CMPL */
				alt[j++] = 0x500fa;	/* PM_RUN_INST_CMPL */
				break;
			case 0x500fa:	/* PM_RUN_INST_CMPL */
				alt[j++] = 0x2;	/* PM_PPC_CMPL */
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
	.n_counter		= 6,
	.max_alternatives	= MAX_ALT + 1,
	.add_fields		= POWER8_ADD_FIELDS,
	.test_adder		= POWER8_TEST_ADDER,
	.compute_mmcr		= power8_compute_mmcr,
	.config_bhrb		= power8_config_bhrb,
	.bhrb_filter_map	= power8_bhrb_filter_map,
	.get_constraint		= power8_get_constraint,
	.get_alternatives	= power8_get_alternatives,
	.disable_pmc		= power8_disable_pmc,
	.flags			= PPMU_HAS_SSLOT | PPMU_HAS_SIER | PPMU_BHRB | PPMU_EBB,
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
