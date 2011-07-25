/*
 * Performance counter support for POWER6 processors.
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/string.h>
#include <asm/reg.h>
#include <asm/cputable.h>

/*
 * Bits in event code for POWER6
 */
#define PM_PMC_SH	20	/* PMC number (1-based) for direct events */
#define PM_PMC_MSK	0x7
#define PM_PMC_MSKS	(PM_PMC_MSK << PM_PMC_SH)
#define PM_UNIT_SH	16	/* Unit event comes (TTMxSEL encoding) */
#define PM_UNIT_MSK	0xf
#define PM_UNIT_MSKS	(PM_UNIT_MSK << PM_UNIT_SH)
#define PM_LLAV		0x8000	/* Load lookahead match value */
#define PM_LLA		0x4000	/* Load lookahead match enable */
#define PM_BYTE_SH	12	/* Byte of event bus to use */
#define PM_BYTE_MSK	3
#define PM_SUBUNIT_SH	8	/* Subunit event comes from (NEST_SEL enc.) */
#define PM_SUBUNIT_MSK	7
#define PM_SUBUNIT_MSKS	(PM_SUBUNIT_MSK << PM_SUBUNIT_SH)
#define PM_PMCSEL_MSK	0xff	/* PMCxSEL value */
#define PM_BUSEVENT_MSK	0xf3700

/*
 * Bits in MMCR1 for POWER6
 */
#define MMCR1_TTM0SEL_SH	60
#define MMCR1_TTMSEL_SH(n)	(MMCR1_TTM0SEL_SH - (n) * 4)
#define MMCR1_TTMSEL_MSK	0xf
#define MMCR1_TTMSEL(m, n)	(((m) >> MMCR1_TTMSEL_SH(n)) & MMCR1_TTMSEL_MSK)
#define MMCR1_NESTSEL_SH	45
#define MMCR1_NESTSEL_MSK	0x7
#define MMCR1_NESTSEL(m)	(((m) >> MMCR1_NESTSEL_SH) & MMCR1_NESTSEL_MSK)
#define MMCR1_PMC1_LLA		(1ul << 44)
#define MMCR1_PMC1_LLA_VALUE	(1ul << 39)
#define MMCR1_PMC1_ADDR_SEL	(1ul << 35)
#define MMCR1_PMC1SEL_SH	24
#define MMCR1_PMCSEL_SH(n)	(MMCR1_PMC1SEL_SH - (n) * 8)
#define MMCR1_PMCSEL_MSK	0xff

/*
 * Map of which direct events on which PMCs are marked instruction events.
 * Indexed by PMCSEL value >> 1.
 * Bottom 4 bits are a map of which PMCs are interesting,
 * top 4 bits say what sort of event:
 *   0 = direct marked event,
 *   1 = byte decode event,
 *   4 = add/and event (PMC1 -> bits 0 & 4),
 *   5 = add/and event (PMC1 -> bits 1 & 5),
 *   6 = add/and event (PMC1 -> bits 2 & 6),
 *   7 = add/and event (PMC1 -> bits 3 & 7).
 */
static unsigned char direct_event_is_marked[0x60 >> 1] = {
	0,	/* 00 */
	0,	/* 02 */
	0,	/* 04 */
	0x07,	/* 06 PM_MRK_ST_CMPL, PM_MRK_ST_GPS, PM_MRK_ST_CMPL_INT */
	0x04,	/* 08 PM_MRK_DFU_FIN */
	0x06,	/* 0a PM_MRK_IFU_FIN, PM_MRK_INST_FIN */
	0,	/* 0c */
	0,	/* 0e */
	0x02,	/* 10 PM_MRK_INST_DISP */
	0x08,	/* 12 PM_MRK_LSU_DERAT_MISS */
	0,	/* 14 */
	0,	/* 16 */
	0x0c,	/* 18 PM_THRESH_TIMEO, PM_MRK_INST_FIN */
	0x0f,	/* 1a PM_MRK_INST_DISP, PM_MRK_{FXU,FPU,LSU}_FIN */
	0x01,	/* 1c PM_MRK_INST_ISSUED */
	0,	/* 1e */
	0,	/* 20 */
	0,	/* 22 */
	0,	/* 24 */
	0,	/* 26 */
	0x15,	/* 28 PM_MRK_DATA_FROM_L2MISS, PM_MRK_DATA_FROM_L3MISS */
	0,	/* 2a */
	0,	/* 2c */
	0,	/* 2e */
	0x4f,	/* 30 */
	0x7f,	/* 32 */
	0x4f,	/* 34 */
	0x5f,	/* 36 */
	0x6f,	/* 38 */
	0x4f,	/* 3a */
	0,	/* 3c */
	0x08,	/* 3e PM_MRK_INST_TIMEO */
	0x1f,	/* 40 */
	0x1f,	/* 42 */
	0x1f,	/* 44 */
	0x1f,	/* 46 */
	0x1f,	/* 48 */
	0x1f,	/* 4a */
	0x1f,	/* 4c */
	0x1f,	/* 4e */
	0,	/* 50 */
	0x05,	/* 52 PM_MRK_BR_TAKEN, PM_MRK_BR_MPRED */
	0x1c,	/* 54 PM_MRK_PTEG_FROM_L3MISS, PM_MRK_PTEG_FROM_L2MISS */
	0x02,	/* 56 PM_MRK_LD_MISS_L1 */
	0,	/* 58 */
	0,	/* 5a */
	0,	/* 5c */
	0,	/* 5e */
};

/*
 * Masks showing for each unit which bits are marked events.
 * These masks are in LE order, i.e. 0x00000001 is byte 0, bit 0.
 */
static u32 marked_bus_events[16] = {
	0x01000000,	/* direct events set 1: byte 3 bit 0 */
	0x00010000,	/* direct events set 2: byte 2 bit 0 */
	0, 0, 0, 0,	/* IDU, IFU, nest: nothing */
	0x00000088,	/* VMX set 1: byte 0 bits 3, 7 */
	0x000000c0,	/* VMX set 2: byte 0 bits 4-7 */
	0x04010000,	/* LSU set 1: byte 2 bit 0, byte 3 bit 2 */
	0xff010000u,	/* LSU set 2: byte 2 bit 0, all of byte 3 */
	0,		/* LSU set 3 */
	0x00000010,	/* VMX set 3: byte 0 bit 4 */
	0,		/* BFP set 1 */
	0x00000022,	/* BFP set 2: byte 0 bits 1, 5 */
	0, 0
};
	
/*
 * Returns 1 if event counts things relating to marked instructions
 * and thus needs the MMCRA_SAMPLE_ENABLE bit set, or 0 if not.
 */
static int power6_marked_instr_event(u64 event)
{
	int pmc, psel, ptype;
	int bit, byte, unit;
	u32 mask;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	psel = (event & PM_PMCSEL_MSK) >> 1;	/* drop edge/level bit */
	if (pmc >= 5)
		return 0;

	bit = -1;
	if (psel < sizeof(direct_event_is_marked)) {
		ptype = direct_event_is_marked[psel];
		if (pmc == 0 || !(ptype & (1 << (pmc - 1))))
			return 0;
		ptype >>= 4;
		if (ptype == 0)
			return 1;
		if (ptype == 1)
			bit = 0;
		else
			bit = ptype ^ (pmc - 1);
	} else if ((psel & 0x48) == 0x40)
		bit = psel & 7;

	if (!(event & PM_BUSEVENT_MSK) || bit == -1)
		return 0;

	byte = (event >> PM_BYTE_SH) & PM_BYTE_MSK;
	unit = (event >> PM_UNIT_SH) & PM_UNIT_MSK;
	mask = marked_bus_events[unit];
	return (mask >> (byte * 8 + bit)) & 1;
}

/*
 * Assign PMC numbers and compute MMCR1 value for a set of events
 */
static int p6_compute_mmcr(u64 event[], int n_ev,
			   unsigned int hwc[], unsigned long mmcr[])
{
	unsigned long mmcr1 = 0;
	unsigned long mmcra = MMCRA_SDAR_DCACHE_MISS | MMCRA_SDAR_ERAT_MISS;
	int i;
	unsigned int pmc, ev, b, u, s, psel;
	unsigned int ttmset = 0;
	unsigned int pmc_inuse = 0;

	if (n_ev > 6)
		return -1;
	for (i = 0; i < n_ev; ++i) {
		pmc = (event[i] >> PM_PMC_SH) & PM_PMC_MSK;
		if (pmc) {
			if (pmc_inuse & (1 << (pmc - 1)))
				return -1;	/* collision! */
			pmc_inuse |= 1 << (pmc - 1);
		}
	}
	for (i = 0; i < n_ev; ++i) {
		ev = event[i];
		pmc = (ev >> PM_PMC_SH) & PM_PMC_MSK;
		if (pmc) {
			--pmc;
		} else {
			/* can go on any PMC; find a free one */
			for (pmc = 0; pmc < 4; ++pmc)
				if (!(pmc_inuse & (1 << pmc)))
					break;
			if (pmc >= 4)
				return -1;
			pmc_inuse |= 1 << pmc;
		}
		hwc[i] = pmc;
		psel = ev & PM_PMCSEL_MSK;
		if (ev & PM_BUSEVENT_MSK) {
			/* this event uses the event bus */
			b = (ev >> PM_BYTE_SH) & PM_BYTE_MSK;
			u = (ev >> PM_UNIT_SH) & PM_UNIT_MSK;
			/* check for conflict on this byte of event bus */
			if ((ttmset & (1 << b)) && MMCR1_TTMSEL(mmcr1, b) != u)
				return -1;
			mmcr1 |= (unsigned long)u << MMCR1_TTMSEL_SH(b);
			ttmset |= 1 << b;
			if (u == 5) {
				/* Nest events have a further mux */
				s = (ev >> PM_SUBUNIT_SH) & PM_SUBUNIT_MSK;
				if ((ttmset & 0x10) &&
				    MMCR1_NESTSEL(mmcr1) != s)
					return -1;
				ttmset |= 0x10;
				mmcr1 |= (unsigned long)s << MMCR1_NESTSEL_SH;
			}
			if (0x30 <= psel && psel <= 0x3d) {
				/* these need the PMCx_ADDR_SEL bits */
				if (b >= 2)
					mmcr1 |= MMCR1_PMC1_ADDR_SEL >> pmc;
			}
			/* bus select values are different for PMC3/4 */
			if (pmc >= 2 && (psel & 0x90) == 0x80)
				psel ^= 0x20;
		}
		if (ev & PM_LLA) {
			mmcr1 |= MMCR1_PMC1_LLA >> pmc;
			if (ev & PM_LLAV)
				mmcr1 |= MMCR1_PMC1_LLA_VALUE >> pmc;
		}
		if (power6_marked_instr_event(event[i]))
			mmcra |= MMCRA_SAMPLE_ENABLE;
		if (pmc < 4)
			mmcr1 |= (unsigned long)psel << MMCR1_PMCSEL_SH(pmc);
	}
	mmcr[0] = 0;
	if (pmc_inuse & 1)
		mmcr[0] = MMCR0_PMC1CE;
	if (pmc_inuse & 0xe)
		mmcr[0] |= MMCR0_PMCjCE;
	mmcr[1] = mmcr1;
	mmcr[2] = mmcra;
	return 0;
}

/*
 * Layout of constraint bits:
 *
 *	0-1	add field: number of uses of PMC1 (max 1)
 *	2-3, 4-5, 6-7, 8-9, 10-11: ditto for PMC2, 3, 4, 5, 6
 *	12-15	add field: number of uses of PMC1-4 (max 4)
 *	16-19	select field: unit on byte 0 of event bus
 *	20-23, 24-27, 28-31 ditto for bytes 1, 2, 3
 *	32-34	select field: nest (subunit) event selector
 */
static int p6_get_constraint(u64 event, unsigned long *maskp,
			     unsigned long *valp)
{
	int pmc, byte, sh, subunit;
	unsigned long mask = 0, value = 0;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	if (pmc) {
		if (pmc > 4 && !(event == 0x500009 || event == 0x600005))
			return -1;
		sh = (pmc - 1) * 2;
		mask |= 2 << sh;
		value |= 1 << sh;
	}
	if (event & PM_BUSEVENT_MSK) {
		byte = (event >> PM_BYTE_SH) & PM_BYTE_MSK;
		sh = byte * 4 + (16 - PM_UNIT_SH);
		mask |= PM_UNIT_MSKS << sh;
		value |= (unsigned long)(event & PM_UNIT_MSKS) << sh;
		if ((event & PM_UNIT_MSKS) == (5 << PM_UNIT_SH)) {
			subunit = (event >> PM_SUBUNIT_SH) & PM_SUBUNIT_MSK;
			mask  |= (unsigned long)PM_SUBUNIT_MSK << 32;
			value |= (unsigned long)subunit << 32;
		}
	}
	if (pmc <= 4) {
		mask  |= 0x8000;	/* add field for count of PMC1-4 uses */
		value |= 0x1000;
	}
	*maskp = mask;
	*valp = value;
	return 0;
}

static int p6_limited_pmc_event(u64 event)
{
	int pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;

	return pmc == 5 || pmc == 6;
}

#define MAX_ALT	4	/* at most 4 alternatives for any event */

static const unsigned int event_alternatives[][MAX_ALT] = {
	{ 0x0130e8, 0x2000f6, 0x3000fc },	/* PM_PTEG_RELOAD_VALID */
	{ 0x080080, 0x10000d, 0x30000c, 0x4000f0 }, /* PM_LD_MISS_L1 */
	{ 0x080088, 0x200054, 0x3000f0 },	/* PM_ST_MISS_L1 */
	{ 0x10000a, 0x2000f4, 0x600005 },	/* PM_RUN_CYC */
	{ 0x10000b, 0x2000f5 },			/* PM_RUN_COUNT */
	{ 0x10000e, 0x400010 },			/* PM_PURR */
	{ 0x100010, 0x4000f8 },			/* PM_FLUSH */
	{ 0x10001a, 0x200010 },			/* PM_MRK_INST_DISP */
	{ 0x100026, 0x3000f8 },			/* PM_TB_BIT_TRANS */
	{ 0x100054, 0x2000f0 },			/* PM_ST_FIN */
	{ 0x100056, 0x2000fc },			/* PM_L1_ICACHE_MISS */
	{ 0x1000f0, 0x40000a },			/* PM_INST_IMC_MATCH_CMPL */
	{ 0x1000f8, 0x200008 },			/* PM_GCT_EMPTY_CYC */
	{ 0x1000fc, 0x400006 },			/* PM_LSU_DERAT_MISS_CYC */
	{ 0x20000e, 0x400007 },			/* PM_LSU_DERAT_MISS */
	{ 0x200012, 0x300012 },			/* PM_INST_DISP */
	{ 0x2000f2, 0x3000f2 },			/* PM_INST_DISP */
	{ 0x2000f8, 0x300010 },			/* PM_EXT_INT */
	{ 0x2000fe, 0x300056 },			/* PM_DATA_FROM_L2MISS */
	{ 0x2d0030, 0x30001a },			/* PM_MRK_FPU_FIN */
	{ 0x30000a, 0x400018 },			/* PM_MRK_INST_FIN */
	{ 0x3000f6, 0x40000e },			/* PM_L1_DCACHE_RELOAD_VALID */
	{ 0x3000fe, 0x400056 },			/* PM_DATA_FROM_L3MISS */
};

/*
 * This could be made more efficient with a binary search on
 * a presorted list, if necessary
 */
static int find_alternatives_list(u64 event)
{
	int i, j;
	unsigned int alt;

	for (i = 0; i < ARRAY_SIZE(event_alternatives); ++i) {
		if (event < event_alternatives[i][0])
			return -1;
		for (j = 0; j < MAX_ALT; ++j) {
			alt = event_alternatives[i][j];
			if (!alt || event < alt)
				break;
			if (event == alt)
				return i;
		}
	}
	return -1;
}

static int p6_get_alternatives(u64 event, unsigned int flags, u64 alt[])
{
	int i, j, nlim;
	unsigned int psel, pmc;
	unsigned int nalt = 1;
	u64 aevent;

	alt[0] = event;
	nlim = p6_limited_pmc_event(event);

	/* check the alternatives table */
	i = find_alternatives_list(event);
	if (i >= 0) {
		/* copy out alternatives from list */
		for (j = 0; j < MAX_ALT; ++j) {
			aevent = event_alternatives[i][j];
			if (!aevent)
				break;
			if (aevent != event)
				alt[nalt++] = aevent;
			nlim += p6_limited_pmc_event(aevent);
		}

	} else {
		/* Check for alternative ways of computing sum events */
		/* PMCSEL 0x32 counter N == PMCSEL 0x34 counter 5-N */
		psel = event & (PM_PMCSEL_MSK & ~1);	/* ignore edge bit */
		pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
		if (pmc && (psel == 0x32 || psel == 0x34))
			alt[nalt++] = ((event ^ 0x6) & ~PM_PMC_MSKS) |
				((5 - pmc) << PM_PMC_SH);

		/* PMCSEL 0x38 counter N == PMCSEL 0x3a counter N+/-2 */
		if (pmc && (psel == 0x38 || psel == 0x3a))
			alt[nalt++] = ((event ^ 0x2) & ~PM_PMC_MSKS) |
				((pmc > 2? pmc - 2: pmc + 2) << PM_PMC_SH);
	}

	if (flags & PPMU_ONLY_COUNT_RUN) {
		/*
		 * We're only counting in RUN state,
		 * so PM_CYC is equivalent to PM_RUN_CYC,
		 * PM_INST_CMPL === PM_RUN_INST_CMPL, PM_PURR === PM_RUN_PURR.
		 * This doesn't include alternatives that don't provide
		 * any extra flexibility in assigning PMCs (e.g.
		 * 0x10000a for PM_RUN_CYC vs. 0x1e for PM_CYC).
		 * Note that even with these additional alternatives
		 * we never end up with more than 4 alternatives for any event.
		 */
		j = nalt;
		for (i = 0; i < nalt; ++i) {
			switch (alt[i]) {
			case 0x1e:	/* PM_CYC */
				alt[j++] = 0x600005;	/* PM_RUN_CYC */
				++nlim;
				break;
			case 0x10000a:	/* PM_RUN_CYC */
				alt[j++] = 0x1e;	/* PM_CYC */
				break;
			case 2:		/* PM_INST_CMPL */
				alt[j++] = 0x500009;	/* PM_RUN_INST_CMPL */
				++nlim;
				break;
			case 0x500009:	/* PM_RUN_INST_CMPL */
				alt[j++] = 2;		/* PM_INST_CMPL */
				break;
			case 0x10000e:	/* PM_PURR */
				alt[j++] = 0x4000f4;	/* PM_RUN_PURR */
				break;
			case 0x4000f4:	/* PM_RUN_PURR */
				alt[j++] = 0x10000e;	/* PM_PURR */
				break;
			}
		}
		nalt = j;
	}

	if (!(flags & PPMU_LIMITED_PMC_OK) && nlim) {
		/* remove the limited PMC events */
		j = 0;
		for (i = 0; i < nalt; ++i) {
			if (!p6_limited_pmc_event(alt[i])) {
				alt[j] = alt[i];
				++j;
			}
		}
		nalt = j;
	} else if ((flags & PPMU_LIMITED_PMC_REQD) && nlim < nalt) {
		/* remove all but the limited PMC events */
		j = 0;
		for (i = 0; i < nalt; ++i) {
			if (p6_limited_pmc_event(alt[i])) {
				alt[j] = alt[i];
				++j;
			}
		}
		nalt = j;
	}

	return nalt;
}

static void p6_disable_pmc(unsigned int pmc, unsigned long mmcr[])
{
	/* Set PMCxSEL to 0 to disable PMCx */
	if (pmc <= 3)
		mmcr[1] &= ~(0xffUL << MMCR1_PMCSEL_SH(pmc));
}

static int power6_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x1e,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 2,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0x280030, /* LD_REF_L1 */
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x30000c, /* LD_MISS_L1 */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x410a0,  /* BR_PRED */
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0x400052, /* BR_MPRED */
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

/*
 * Table of generalized cache-related events.
 * 0 means not supported, -1 means nonsensical, other values
 * are event codes.
 * The "DTLB" and "ITLB" events relate to the DERAT and IERAT.
 */
static int power6_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0x80082,	0x80080		},
		[C(OP_WRITE)] = {	0x80086,	0x80088		},
		[C(OP_PREFETCH)] = {	0x810a4,	0		},
	},
	[C(L1I)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0x100056 	},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	0x4008c,	0		},
	},
	[C(LL)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0x150730,	0x250532	},
		[C(OP_WRITE)] = {	0x250432,	0x150432	},
		[C(OP_PREFETCH)] = {	0x810a6,	0		},
	},
	[C(DTLB)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0x20000e	},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
	[C(ITLB)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0x420ce		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
	[C(BPU)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0x430e6,	0x400052	},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
	[C(NODE)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	-1,		-1		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
};

static struct power_pmu power6_pmu = {
	.name			= "POWER6",
	.n_counter		= 6,
	.max_alternatives	= MAX_ALT,
	.add_fields		= 0x1555,
	.test_adder		= 0x3000,
	.compute_mmcr		= p6_compute_mmcr,
	.get_constraint		= p6_get_constraint,
	.get_alternatives	= p6_get_alternatives,
	.disable_pmc		= p6_disable_pmc,
	.limited_pmc_event	= p6_limited_pmc_event,
	.flags			= PPMU_LIMITED_PMC5_6 | PPMU_ALT_SIPR,
	.n_generic		= ARRAY_SIZE(power6_generic_events),
	.generic_events		= power6_generic_events,
	.cache_events		= &power6_cache_events,
};

static int init_power6_pmu(void)
{
	if (!cur_cpu_spec->oprofile_cpu_type ||
	    strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power6"))
		return -ENODEV;

	return register_power_pmu(&power6_pmu);
}

early_initcall(init_power6_pmu);
