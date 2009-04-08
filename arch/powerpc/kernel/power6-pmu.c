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
#include <linux/perf_counter.h>
#include <asm/reg.h>

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
#define MMCR1_PMC1_LLA		((u64)1 << 44)
#define MMCR1_PMC1_LLA_VALUE	((u64)1 << 39)
#define MMCR1_PMC1_ADDR_SEL	((u64)1 << 35)
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
static int power6_marked_instr_event(unsigned int event)
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
static int p6_compute_mmcr(unsigned int event[], int n_ev,
			   unsigned int hwc[], u64 mmcr[])
{
	u64 mmcr1 = 0;
	u64 mmcra = 0;
	int i;
	unsigned int pmc, ev, b, u, s, psel;
	unsigned int ttmset = 0;
	unsigned int pmc_inuse = 0;

	if (n_ev > 4)
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
			mmcr1 |= (u64)u << MMCR1_TTMSEL_SH(b);
			ttmset |= 1 << b;
			if (u == 5) {
				/* Nest events have a further mux */
				s = (ev >> PM_SUBUNIT_SH) & PM_SUBUNIT_MSK;
				if ((ttmset & 0x10) &&
				    MMCR1_NESTSEL(mmcr1) != s)
					return -1;
				ttmset |= 0x10;
				mmcr1 |= (u64)s << MMCR1_NESTSEL_SH;
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
		mmcr1 |= (u64)psel << MMCR1_PMCSEL_SH(pmc);
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
 *	2-3, 4-5, 6-7: ditto for PMC2, 3, 4
 *	8-10	select field: nest (subunit) event selector
 *	16-19	select field: unit on byte 0 of event bus
 *	20-23, 24-27, 28-31 ditto for bytes 1, 2, 3
 */
static int p6_get_constraint(unsigned int event, u64 *maskp, u64 *valp)
{
	int pmc, byte, sh;
	unsigned int mask = 0, value = 0;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	if (pmc) {
		if (pmc > 4)
			return -1;
		sh = (pmc - 1) * 2;
		mask |= 2 << sh;
		value |= 1 << sh;
	}
	if (event & PM_BUSEVENT_MSK) {
		byte = (event >> PM_BYTE_SH) & PM_BYTE_MSK;
		sh = byte * 4;
		mask |= PM_UNIT_MSKS << sh;
		value |= (event & PM_UNIT_MSKS) << sh;
		if ((event & PM_UNIT_MSKS) == (5 << PM_UNIT_SH)) {
			mask |= PM_SUBUNIT_MSKS;
			value |= event & PM_SUBUNIT_MSKS;
		}
	}
	*maskp = mask;
	*valp = value;
	return 0;
}

#define MAX_ALT	4	/* at most 4 alternatives for any event */

static const unsigned int event_alternatives[][MAX_ALT] = {
	{ 0x0130e8, 0x2000f6, 0x3000fc },	/* PM_PTEG_RELOAD_VALID */
	{ 0x080080, 0x10000d, 0x30000c, 0x4000f0 }, /* PM_LD_MISS_L1 */
	{ 0x080088, 0x200054, 0x3000f0 },	/* PM_ST_MISS_L1 */
	{ 0x10000a, 0x2000f4 },			/* PM_RUN_CYC */
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
static int find_alternatives_list(unsigned int event)
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

static int p6_get_alternatives(unsigned int event, unsigned int alt[])
{
	int i, j;
	unsigned int aevent, psel, pmc;
	unsigned int nalt = 1;

	alt[0] = event;

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

	return nalt;
}

static void p6_disable_pmc(unsigned int pmc, u64 mmcr[])
{
	/* Set PMCxSEL to 0 to disable PMCx */
	mmcr[1] &= ~(0xffUL << MMCR1_PMCSEL_SH(pmc));
}

static int power6_generic_events[] = {
	[PERF_COUNT_CPU_CYCLES] = 0x1e,
	[PERF_COUNT_INSTRUCTIONS] = 2,
	[PERF_COUNT_CACHE_REFERENCES] = 0x280030,	/* LD_REF_L1 */
	[PERF_COUNT_CACHE_MISSES] = 0x30000c,		/* LD_MISS_L1 */
	[PERF_COUNT_BRANCH_INSTRUCTIONS] = 0x410a0,	/* BR_PRED */ 
	[PERF_COUNT_BRANCH_MISSES] = 0x400052,		/* BR_MPRED */
};

struct power_pmu power6_pmu = {
	.n_counter = 4,
	.max_alternatives = MAX_ALT,
	.add_fields = 0x55,
	.test_adder = 0,
	.compute_mmcr = p6_compute_mmcr,
	.get_constraint = p6_get_constraint,
	.get_alternatives = p6_get_alternatives,
	.disable_pmc = p6_disable_pmc,
	.n_generic = ARRAY_SIZE(power6_generic_events),
	.generic_events = power6_generic_events,
};
