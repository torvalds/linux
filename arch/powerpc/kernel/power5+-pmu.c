/*
 * Performance counter support for POWER5+/++ (not POWER5) processors.
 *
 * Copyright 2009 Paul Mackerras, IBM Corporation.
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
 * Bits in event code for POWER5+ (POWER5 GS) and POWER5++ (POWER5 GS DD3)
 */
#define PM_PMC_SH	20	/* PMC number (1-based) for direct events */
#define PM_PMC_MSK	0xf
#define PM_PMC_MSKS	(PM_PMC_MSK << PM_PMC_SH)
#define PM_UNIT_SH	16	/* TTMMUX number and setting - unit select */
#define PM_UNIT_MSK	0xf
#define PM_BYTE_SH	12	/* Byte number of event bus to use */
#define PM_BYTE_MSK	7
#define PM_GRS_SH	8	/* Storage subsystem mux select */
#define PM_GRS_MSK	7
#define PM_BUSEVENT_MSK	0x80	/* Set if event uses event bus */
#define PM_PMCSEL_MSK	0x7f

/* Values in PM_UNIT field */
#define PM_FPU		0
#define PM_ISU0		1
#define PM_IFU		2
#define PM_ISU1		3
#define PM_IDU		4
#define PM_ISU0_ALT	6
#define PM_GRS		7
#define PM_LSU0		8
#define PM_LSU1		0xc
#define PM_LASTUNIT	0xc

/*
 * Bits in MMCR1 for POWER5+
 */
#define MMCR1_TTM0SEL_SH	62
#define MMCR1_TTM1SEL_SH	60
#define MMCR1_TTM2SEL_SH	58
#define MMCR1_TTM3SEL_SH	56
#define MMCR1_TTMSEL_MSK	3
#define MMCR1_TD_CP_DBG0SEL_SH	54
#define MMCR1_TD_CP_DBG1SEL_SH	52
#define MMCR1_TD_CP_DBG2SEL_SH	50
#define MMCR1_TD_CP_DBG3SEL_SH	48
#define MMCR1_GRS_L2SEL_SH	46
#define MMCR1_GRS_L2SEL_MSK	3
#define MMCR1_GRS_L3SEL_SH	44
#define MMCR1_GRS_L3SEL_MSK	3
#define MMCR1_GRS_MCSEL_SH	41
#define MMCR1_GRS_MCSEL_MSK	7
#define MMCR1_GRS_FABSEL_SH	39
#define MMCR1_GRS_FABSEL_MSK	3
#define MMCR1_PMC1_ADDER_SEL_SH	35
#define MMCR1_PMC2_ADDER_SEL_SH	34
#define MMCR1_PMC3_ADDER_SEL_SH	33
#define MMCR1_PMC4_ADDER_SEL_SH	32
#define MMCR1_PMC1SEL_SH	25
#define MMCR1_PMC2SEL_SH	17
#define MMCR1_PMC3SEL_SH	9
#define MMCR1_PMC4SEL_SH	1
#define MMCR1_PMCSEL_SH(n)	(MMCR1_PMC1SEL_SH - (n) * 8)
#define MMCR1_PMCSEL_MSK	0x7f

/*
 * Layout of constraint bits:
 * 6666555555555544444444443333333333222222222211111111110000000000
 * 3210987654321098765432109876543210987654321098765432109876543210
 *             [  ><><>< ><> <><>[  >  <  ><  ><  ><  ><><><><><><>
 *             NC  G0G1G2 G3 T0T1 UC    B0  B1  B2  B3 P6P5P4P3P2P1
 *
 * NC - number of counters
 *     51: NC error 0x0008_0000_0000_0000
 *     48-50: number of events needing PMC1-4 0x0007_0000_0000_0000
 *
 * G0..G3 - GRS mux constraints
 *     46-47: GRS_L2SEL value
 *     44-45: GRS_L3SEL value
 *     41-44: GRS_MCSEL value
 *     39-40: GRS_FABSEL value
 *	Note that these match up with their bit positions in MMCR1
 *
 * T0 - TTM0 constraint
 *     36-37: TTM0SEL value (0=FPU, 2=IFU, 3=ISU1) 0x30_0000_0000
 *
 * T1 - TTM1 constraint
 *     34-35: TTM1SEL value (0=IDU, 3=GRS) 0x0c_0000_0000
 *
 * UC - unit constraint: can't have all three of FPU|IFU|ISU1, ISU0, IDU|GRS
 *     33: UC3 error 0x02_0000_0000
 *     32: FPU|IFU|ISU1 events needed 0x01_0000_0000
 *     31: ISU0 events needed 0x01_8000_0000
 *     30: IDU|GRS events needed 0x00_4000_0000
 *
 * B0
 *     24-27: Byte 0 event source 0x0f00_0000
 *	      Encoding as for the event code
 *
 * B1, B2, B3
 *     20-23, 16-19, 12-15: Byte 1, 2, 3 event sources
 *
 * P6
 *     11: P6 error 0x800
 *     10-11: Count of events needing PMC6
 *
 * P1..P5
 *     0-9: Count of events needing PMC1..PMC5
 */

static const int grsel_shift[8] = {
	MMCR1_GRS_L2SEL_SH, MMCR1_GRS_L2SEL_SH, MMCR1_GRS_L2SEL_SH,
	MMCR1_GRS_L3SEL_SH, MMCR1_GRS_L3SEL_SH, MMCR1_GRS_L3SEL_SH,
	MMCR1_GRS_MCSEL_SH, MMCR1_GRS_FABSEL_SH
};

/* Masks and values for using events from the various units */
static unsigned long unit_cons[PM_LASTUNIT+1][2] = {
	[PM_FPU] =   { 0x3200000000ul, 0x0100000000ul },
	[PM_ISU0] =  { 0x0200000000ul, 0x0080000000ul },
	[PM_ISU1] =  { 0x3200000000ul, 0x3100000000ul },
	[PM_IFU] =   { 0x3200000000ul, 0x2100000000ul },
	[PM_IDU] =   { 0x0e00000000ul, 0x0040000000ul },
	[PM_GRS] =   { 0x0e00000000ul, 0x0c40000000ul },
};

static int power5p_get_constraint(u64 event, unsigned long *maskp,
				  unsigned long *valp)
{
	int pmc, byte, unit, sh;
	int bit, fmask;
	unsigned long mask = 0, value = 0;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	if (pmc) {
		if (pmc > 6)
			return -1;
		sh = (pmc - 1) * 2;
		mask |= 2 << sh;
		value |= 1 << sh;
		if (pmc >= 5 && !(event == 0x500009 || event == 0x600005))
			return -1;
	}
	if (event & PM_BUSEVENT_MSK) {
		unit = (event >> PM_UNIT_SH) & PM_UNIT_MSK;
		if (unit > PM_LASTUNIT)
			return -1;
		if (unit == PM_ISU0_ALT)
			unit = PM_ISU0;
		mask |= unit_cons[unit][0];
		value |= unit_cons[unit][1];
		byte = (event >> PM_BYTE_SH) & PM_BYTE_MSK;
		if (byte >= 4) {
			if (unit != PM_LSU1)
				return -1;
			/* Map LSU1 low word (bytes 4-7) to unit LSU1+1 */
			++unit;
			byte &= 3;
		}
		if (unit == PM_GRS) {
			bit = event & 7;
			fmask = (bit == 6)? 7: 3;
			sh = grsel_shift[bit];
			mask |= (unsigned long)fmask << sh;
			value |= (unsigned long)((event >> PM_GRS_SH) & fmask)
				<< sh;
		}
		/* Set byte lane select field */
		mask  |= 0xfUL << (24 - 4 * byte);
		value |= (unsigned long)unit << (24 - 4 * byte);
	}
	if (pmc < 5) {
		/* need a counter from PMC1-4 set */
		mask  |= 0x8000000000000ul;
		value |= 0x1000000000000ul;
	}
	*maskp = mask;
	*valp = value;
	return 0;
}

static int power5p_limited_pmc_event(u64 event)
{
	int pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;

	return pmc == 5 || pmc == 6;
}

#define MAX_ALT	3	/* at most 3 alternatives for any event */

static const unsigned int event_alternatives[][MAX_ALT] = {
	{ 0x100c0,  0x40001f },			/* PM_GCT_FULL_CYC */
	{ 0x120e4,  0x400002 },			/* PM_GRP_DISP_REJECT */
	{ 0x230e2,  0x323087 },			/* PM_BR_PRED_CR */
	{ 0x230e3,  0x223087, 0x3230a0 },	/* PM_BR_PRED_TA */
	{ 0x410c7,  0x441084 },			/* PM_THRD_L2MISS_BOTH_CYC */
	{ 0x800c4,  0xc20e0 },			/* PM_DTLB_MISS */
	{ 0xc50c6,  0xc60e0 },			/* PM_MRK_DTLB_MISS */
	{ 0x100005, 0x600005 },			/* PM_RUN_CYC */
	{ 0x100009, 0x200009 },			/* PM_INST_CMPL */
	{ 0x200015, 0x300015 },			/* PM_LSU_LMQ_SRQ_EMPTY_CYC */
	{ 0x300009, 0x400009 },			/* PM_INST_DISP */
};

/*
 * Scan the alternatives table for a match and return the
 * index into the alternatives table if found, else -1.
 */
static int find_alternative(unsigned int event)
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

static const unsigned char bytedecode_alternatives[4][4] = {
	/* PMC 1 */	{ 0x21, 0x23, 0x25, 0x27 },
	/* PMC 2 */	{ 0x07, 0x17, 0x0e, 0x1e },
	/* PMC 3 */	{ 0x20, 0x22, 0x24, 0x26 },
	/* PMC 4 */	{ 0x07, 0x17, 0x0e, 0x1e }
};

/*
 * Some direct events for decodes of event bus byte 3 have alternative
 * PMCSEL values on other counters.  This returns the alternative
 * event code for those that do, or -1 otherwise.  This also handles
 * alternative PCMSEL values for add events.
 */
static s64 find_alternative_bdecode(u64 event)
{
	int pmc, altpmc, pp, j;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	if (pmc == 0 || pmc > 4)
		return -1;
	altpmc = 5 - pmc;	/* 1 <-> 4, 2 <-> 3 */
	pp = event & PM_PMCSEL_MSK;
	for (j = 0; j < 4; ++j) {
		if (bytedecode_alternatives[pmc - 1][j] == pp) {
			return (event & ~(PM_PMC_MSKS | PM_PMCSEL_MSK)) |
				(altpmc << PM_PMC_SH) |
				bytedecode_alternatives[altpmc - 1][j];
		}
	}

	/* new decode alternatives for power5+ */
	if (pmc == 1 && (pp == 0x0d || pp == 0x0e))
		return event + (2 << PM_PMC_SH) + (0x2e - 0x0d);
	if (pmc == 3 && (pp == 0x2e || pp == 0x2f))
		return event - (2 << PM_PMC_SH) - (0x2e - 0x0d);

	/* alternative add event encodings */
	if (pp == 0x10 || pp == 0x28)
		return ((event ^ (0x10 ^ 0x28)) & ~PM_PMC_MSKS) |
			(altpmc << PM_PMC_SH);

	return -1;
}

static int power5p_get_alternatives(u64 event, unsigned int flags, u64 alt[])
{
	int i, j, nalt = 1;
	int nlim;
	s64 ae;

	alt[0] = event;
	nalt = 1;
	nlim = power5p_limited_pmc_event(event);
	i = find_alternative(event);
	if (i >= 0) {
		for (j = 0; j < MAX_ALT; ++j) {
			ae = event_alternatives[i][j];
			if (ae && ae != event)
				alt[nalt++] = ae;
			nlim += power5p_limited_pmc_event(ae);
		}
	} else {
		ae = find_alternative_bdecode(event);
		if (ae > 0)
			alt[nalt++] = ae;
	}

	if (flags & PPMU_ONLY_COUNT_RUN) {
		/*
		 * We're only counting in RUN state,
		 * so PM_CYC is equivalent to PM_RUN_CYC
		 * and PM_INST_CMPL === PM_RUN_INST_CMPL.
		 * This doesn't include alternatives that don't provide
		 * any extra flexibility in assigning PMCs (e.g.
		 * 0x100005 for PM_RUN_CYC vs. 0xf for PM_CYC).
		 * Note that even with these additional alternatives
		 * we never end up with more than 3 alternatives for any event.
		 */
		j = nalt;
		for (i = 0; i < nalt; ++i) {
			switch (alt[i]) {
			case 0xf:	/* PM_CYC */
				alt[j++] = 0x600005;	/* PM_RUN_CYC */
				++nlim;
				break;
			case 0x600005:	/* PM_RUN_CYC */
				alt[j++] = 0xf;
				break;
			case 0x100009:	/* PM_INST_CMPL */
				alt[j++] = 0x500009;	/* PM_RUN_INST_CMPL */
				++nlim;
				break;
			case 0x500009:	/* PM_RUN_INST_CMPL */
				alt[j++] = 0x100009;	/* PM_INST_CMPL */
				alt[j++] = 0x200009;
				break;
			}
		}
		nalt = j;
	}

	if (!(flags & PPMU_LIMITED_PMC_OK) && nlim) {
		/* remove the limited PMC events */
		j = 0;
		for (i = 0; i < nalt; ++i) {
			if (!power5p_limited_pmc_event(alt[i])) {
				alt[j] = alt[i];
				++j;
			}
		}
		nalt = j;
	} else if ((flags & PPMU_LIMITED_PMC_REQD) && nlim < nalt) {
		/* remove all but the limited PMC events */
		j = 0;
		for (i = 0; i < nalt; ++i) {
			if (power5p_limited_pmc_event(alt[i])) {
				alt[j] = alt[i];
				++j;
			}
		}
		nalt = j;
	}

	return nalt;
}

/*
 * Map of which direct events on which PMCs are marked instruction events.
 * Indexed by PMCSEL value, bit i (LE) set if PMC i is a marked event.
 * Bit 0 is set if it is marked for all PMCs.
 * The 0x80 bit indicates a byte decode PMCSEL value.
 */
static unsigned char direct_event_is_marked[0x28] = {
	0,	/* 00 */
	0x1f,	/* 01 PM_IOPS_CMPL */
	0x2,	/* 02 PM_MRK_GRP_DISP */
	0xe,	/* 03 PM_MRK_ST_CMPL, PM_MRK_ST_GPS, PM_MRK_ST_CMPL_INT */
	0,	/* 04 */
	0x1c,	/* 05 PM_MRK_BRU_FIN, PM_MRK_INST_FIN, PM_MRK_CRU_FIN */
	0x80,	/* 06 */
	0x80,	/* 07 */
	0, 0, 0,/* 08 - 0a */
	0x18,	/* 0b PM_THRESH_TIMEO, PM_MRK_GRP_TIMEO */
	0,	/* 0c */
	0x80,	/* 0d */
	0x80,	/* 0e */
	0,	/* 0f */
	0,	/* 10 */
	0x14,	/* 11 PM_MRK_GRP_BR_REDIR, PM_MRK_GRP_IC_MISS */
	0,	/* 12 */
	0x10,	/* 13 PM_MRK_GRP_CMPL */
	0x1f,	/* 14 PM_GRP_MRK, PM_MRK_{FXU,FPU,LSU}_FIN */
	0x2,	/* 15 PM_MRK_GRP_ISSUED */
	0x80,	/* 16 */
	0x80,	/* 17 */
	0, 0, 0, 0, 0,
	0x80,	/* 1d */
	0x80,	/* 1e */
	0,	/* 1f */
	0x80,	/* 20 */
	0x80,	/* 21 */
	0x80,	/* 22 */
	0x80,	/* 23 */
	0x80,	/* 24 */
	0x80,	/* 25 */
	0x80,	/* 26 */
	0x80,	/* 27 */
};

/*
 * Returns 1 if event counts things relating to marked instructions
 * and thus needs the MMCRA_SAMPLE_ENABLE bit set, or 0 if not.
 */
static int power5p_marked_instr_event(u64 event)
{
	int pmc, psel;
	int bit, byte, unit;
	u32 mask;

	pmc = (event >> PM_PMC_SH) & PM_PMC_MSK;
	psel = event & PM_PMCSEL_MSK;
	if (pmc >= 5)
		return 0;

	bit = -1;
	if (psel < sizeof(direct_event_is_marked)) {
		if (direct_event_is_marked[psel] & (1 << pmc))
			return 1;
		if (direct_event_is_marked[psel] & 0x80)
			bit = 4;
		else if (psel == 0x08)
			bit = pmc - 1;
		else if (psel == 0x10)
			bit = 4 - pmc;
		else if (psel == 0x1b && (pmc == 1 || pmc == 3))
			bit = 4;
	} else if ((psel & 0x48) == 0x40) {
		bit = psel & 7;
	} else if (psel == 0x28) {
		bit = pmc - 1;
	} else if (pmc == 3 && (psel == 0x2e || psel == 0x2f)) {
		bit = 4;
	}

	if (!(event & PM_BUSEVENT_MSK) || bit == -1)
		return 0;

	byte = (event >> PM_BYTE_SH) & PM_BYTE_MSK;
	unit = (event >> PM_UNIT_SH) & PM_UNIT_MSK;
	if (unit == PM_LSU0) {
		/* byte 1 bits 0-7, byte 2 bits 0,2-4,6 */
		mask = 0x5dff00;
	} else if (unit == PM_LSU1 && byte >= 4) {
		byte -= 4;
		/* byte 5 bits 6-7, byte 6 bits 0,4, byte 7 bits 0-4,6 */
		mask = 0x5f11c000;
	} else
		return 0;

	return (mask >> (byte * 8 + bit)) & 1;
}

static int power5p_compute_mmcr(u64 event[], int n_ev,
				unsigned int hwc[], unsigned long mmcr[])
{
	unsigned long mmcr1 = 0;
	unsigned long mmcra = 0;
	unsigned int pmc, unit, byte, psel;
	unsigned int ttm;
	int i, isbus, bit, grsel;
	unsigned int pmc_inuse = 0;
	unsigned char busbyte[4];
	unsigned char unituse[16];
	int ttmuse;

	if (n_ev > 6)
		return -1;

	/* First pass to count resource use */
	memset(busbyte, 0, sizeof(busbyte));
	memset(unituse, 0, sizeof(unituse));
	for (i = 0; i < n_ev; ++i) {
		pmc = (event[i] >> PM_PMC_SH) & PM_PMC_MSK;
		if (pmc) {
			if (pmc > 6)
				return -1;
			if (pmc_inuse & (1 << (pmc - 1)))
				return -1;
			pmc_inuse |= 1 << (pmc - 1);
		}
		if (event[i] & PM_BUSEVENT_MSK) {
			unit = (event[i] >> PM_UNIT_SH) & PM_UNIT_MSK;
			byte = (event[i] >> PM_BYTE_SH) & PM_BYTE_MSK;
			if (unit > PM_LASTUNIT)
				return -1;
			if (unit == PM_ISU0_ALT)
				unit = PM_ISU0;
			if (byte >= 4) {
				if (unit != PM_LSU1)
					return -1;
				++unit;
				byte &= 3;
			}
			if (busbyte[byte] && busbyte[byte] != unit)
				return -1;
			busbyte[byte] = unit;
			unituse[unit] = 1;
		}
	}

	/*
	 * Assign resources and set multiplexer selects.
	 *
	 * PM_ISU0 can go either on TTM0 or TTM1, but that's the only
	 * choice we have to deal with.
	 */
	if (unituse[PM_ISU0] &
	    (unituse[PM_FPU] | unituse[PM_IFU] | unituse[PM_ISU1])) {
		unituse[PM_ISU0_ALT] = 1;	/* move ISU to TTM1 */
		unituse[PM_ISU0] = 0;
	}
	/* Set TTM[01]SEL fields. */
	ttmuse = 0;
	for (i = PM_FPU; i <= PM_ISU1; ++i) {
		if (!unituse[i])
			continue;
		if (ttmuse++)
			return -1;
		mmcr1 |= (unsigned long)i << MMCR1_TTM0SEL_SH;
	}
	ttmuse = 0;
	for (; i <= PM_GRS; ++i) {
		if (!unituse[i])
			continue;
		if (ttmuse++)
			return -1;
		mmcr1 |= (unsigned long)(i & 3) << MMCR1_TTM1SEL_SH;
	}
	if (ttmuse > 1)
		return -1;

	/* Set byte lane select fields, TTM[23]SEL and GRS_*SEL. */
	for (byte = 0; byte < 4; ++byte) {
		unit = busbyte[byte];
		if (!unit)
			continue;
		if (unit == PM_ISU0 && unituse[PM_ISU0_ALT]) {
			/* get ISU0 through TTM1 rather than TTM0 */
			unit = PM_ISU0_ALT;
		} else if (unit == PM_LSU1 + 1) {
			/* select lower word of LSU1 for this byte */
			mmcr1 |= 1ul << (MMCR1_TTM3SEL_SH + 3 - byte);
		}
		ttm = unit >> 2;
		mmcr1 |= (unsigned long)ttm
			<< (MMCR1_TD_CP_DBG0SEL_SH - 2 * byte);
	}

	/* Second pass: assign PMCs, set PMCxSEL and PMCx_ADDER_SEL fields */
	for (i = 0; i < n_ev; ++i) {
		pmc = (event[i] >> PM_PMC_SH) & PM_PMC_MSK;
		unit = (event[i] >> PM_UNIT_SH) & PM_UNIT_MSK;
		byte = (event[i] >> PM_BYTE_SH) & PM_BYTE_MSK;
		psel = event[i] & PM_PMCSEL_MSK;
		isbus = event[i] & PM_BUSEVENT_MSK;
		if (!pmc) {
			/* Bus event or any-PMC direct event */
			for (pmc = 0; pmc < 4; ++pmc) {
				if (!(pmc_inuse & (1 << pmc)))
					break;
			}
			if (pmc >= 4)
				return -1;
			pmc_inuse |= 1 << pmc;
		} else if (pmc <= 4) {
			/* Direct event */
			--pmc;
			if (isbus && (byte & 2) &&
			    (psel == 8 || psel == 0x10 || psel == 0x28))
				/* add events on higher-numbered bus */
				mmcr1 |= 1ul << (MMCR1_PMC1_ADDER_SEL_SH - pmc);
		} else {
			/* Instructions or run cycles on PMC5/6 */
			--pmc;
		}
		if (isbus && unit == PM_GRS) {
			bit = psel & 7;
			grsel = (event[i] >> PM_GRS_SH) & PM_GRS_MSK;
			mmcr1 |= (unsigned long)grsel << grsel_shift[bit];
		}
		if (power5p_marked_instr_event(event[i]))
			mmcra |= MMCRA_SAMPLE_ENABLE;
		if ((psel & 0x58) == 0x40 && (byte & 1) != ((pmc >> 1) & 1))
			/* select alternate byte lane */
			psel |= 0x10;
		if (pmc <= 3)
			mmcr1 |= psel << MMCR1_PMCSEL_SH(pmc);
		hwc[i] = pmc;
	}

	/* Return MMCRx values */
	mmcr[0] = 0;
	if (pmc_inuse & 1)
		mmcr[0] = MMCR0_PMC1CE;
	if (pmc_inuse & 0x3e)
		mmcr[0] |= MMCR0_PMCjCE;
	mmcr[1] = mmcr1;
	mmcr[2] = mmcra;
	return 0;
}

static void power5p_disable_pmc(unsigned int pmc, unsigned long mmcr[])
{
	if (pmc <= 3)
		mmcr[1] &= ~(0x7fUL << MMCR1_PMCSEL_SH(pmc));
}

static int power5p_generic_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0xf,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x100009,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0x1c10a8, /* LD_REF_L1 */
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x3c1088, /* LD_MISS_L1 */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x230e4,  /* BR_ISSUED */
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0x230e5,  /* BR_MPRED_CR */
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

/*
 * Table of generalized cache-related events.
 * 0 means not supported, -1 means nonsensical, other values
 * are event codes.
 */
static int power5p_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0x1c10a8,	0x3c1088	},
		[C(OP_WRITE)] = {	0x2c10a8,	0xc10c3		},
		[C(OP_PREFETCH)] = {	0xc70e7,	-1		},
	},
	[C(L1I)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	0,		0		},
	},
	[C(LL)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0		},
		[C(OP_WRITE)] = {	0,		0		},
		[C(OP_PREFETCH)] = {	0xc50c3,	0		},
	},
	[C(DTLB)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0xc20e4,	0x800c4		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
	[C(ITLB)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0,		0x800c0		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
	[C(BPU)] = {		/* 	RESULT_ACCESS	RESULT_MISS */
		[C(OP_READ)] = {	0x230e4,	0x230e5		},
		[C(OP_WRITE)] = {	-1,		-1		},
		[C(OP_PREFETCH)] = {	-1,		-1		},
	},
};

static struct power_pmu power5p_pmu = {
	.name			= "POWER5+/++",
	.n_counter		= 6,
	.max_alternatives	= MAX_ALT,
	.add_fields		= 0x7000000000055ul,
	.test_adder		= 0x3000040000000ul,
	.compute_mmcr		= power5p_compute_mmcr,
	.get_constraint		= power5p_get_constraint,
	.get_alternatives	= power5p_get_alternatives,
	.disable_pmc		= power5p_disable_pmc,
	.limited_pmc_event	= power5p_limited_pmc_event,
	.flags			= PPMU_LIMITED_PMC5_6,
	.n_generic		= ARRAY_SIZE(power5p_generic_events),
	.generic_events		= power5p_generic_events,
	.cache_events		= &power5p_cache_events,
};

static int init_power5p_pmu(void)
{
	if (!cur_cpu_spec->oprofile_cpu_type ||
	    (strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power5+")
	     && strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power5++")))
		return -ENODEV;

	return register_power_pmu(&power5p_pmu);
}

arch_initcall(init_power5p_pmu);
