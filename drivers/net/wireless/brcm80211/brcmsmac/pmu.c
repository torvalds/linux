/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/io.h>

#include <brcm_hw_ids.h>
#include <chipcommon.h>
#include <brcmu_utils.h>
#include "pub.h"
#include "aiutils.h"
#include "pmu.h"
#include "soc.h"

/*
 * external LPO crystal frequency
 */
#define EXT_ILP_HZ 32768

/*
 * Duration for ILP clock frequency measurment in milliseconds
 *
 * remark: 1000 must be an integer multiple of this duration
 */
#define ILP_CALC_DUR	10

/* Fields in pmucontrol */
#define	PCTL_ILP_DIV_MASK	0xffff0000
#define	PCTL_ILP_DIV_SHIFT	16
#define PCTL_PLL_PLLCTL_UPD	0x00000400	/* rev 2 */
#define PCTL_NOILP_ON_WAIT	0x00000200	/* rev 1 */
#define	PCTL_HT_REQ_EN		0x00000100
#define	PCTL_ALP_REQ_EN		0x00000080
#define	PCTL_XTALFREQ_MASK	0x0000007c
#define	PCTL_XTALFREQ_SHIFT	2
#define	PCTL_ILP_DIV_EN		0x00000002
#define	PCTL_LPO_SEL		0x00000001

/* ILP clock */
#define	ILP_CLOCK		32000

/* ALP clock on pre-PMU chips */
#define	ALP_CLOCK		20000000

/* pmustatus */
#define PST_EXTLPOAVAIL	0x0100
#define PST_WDRESET	0x0080
#define	PST_INTPEND	0x0040
#define	PST_SBCLKST	0x0030
#define	PST_SBCLKST_ILP	0x0010
#define	PST_SBCLKST_ALP	0x0020
#define	PST_SBCLKST_HT	0x0030
#define	PST_ALPAVAIL	0x0008
#define	PST_HTAVAIL	0x0004
#define	PST_RESINIT	0x0003

/* PMU resource bit position */
#define PMURES_BIT(bit)	(1 << (bit))

/* PMU corerev and chip specific PLL controls.
 * PMU<rev>_PLL<num>_XX where <rev> is PMU corerev and <num> is an arbitrary
 * number to differentiate different PLLs controlled by the same PMU rev.
 */
/* pllcontrol registers:
 * ndiv_pwrdn, pwrdn_ch<x>, refcomp_pwrdn, dly_ch<x>,
 * p1div, p2div, _bypass_sdmod
 */
#define PMU1_PLL0_PLLCTL0		0
#define PMU1_PLL0_PLLCTL1		1
#define PMU1_PLL0_PLLCTL2		2
#define PMU1_PLL0_PLLCTL3		3
#define PMU1_PLL0_PLLCTL4		4
#define PMU1_PLL0_PLLCTL5		5

/* pmu XtalFreqRatio */
#define	PMU_XTALFREQ_REG_ILPCTR_MASK	0x00001FFF
#define	PMU_XTALFREQ_REG_MEASURE_MASK	0x80000000
#define	PMU_XTALFREQ_REG_MEASURE_SHIFT	31

/* 4313 resources */
#define	RES4313_BB_PU_RSRC		0
#define	RES4313_ILP_REQ_RSRC		1
#define	RES4313_XTAL_PU_RSRC		2
#define	RES4313_ALP_AVAIL_RSRC		3
#define	RES4313_RADIO_PU_RSRC		4
#define	RES4313_BG_PU_RSRC		5
#define	RES4313_VREG1P4_PU_RSRC		6
#define	RES4313_AFE_PWRSW_RSRC		7
#define	RES4313_RX_PWRSW_RSRC		8
#define	RES4313_TX_PWRSW_RSRC		9
#define	RES4313_BB_PWRSW_RSRC		10
#define	RES4313_SYNTH_PWRSW_RSRC	11
#define	RES4313_MISC_PWRSW_RSRC		12
#define	RES4313_BB_PLL_PWRSW_RSRC	13
#define	RES4313_HT_AVAIL_RSRC		14
#define	RES4313_MACPHY_CLK_AVAIL_RSRC	15

/* Determine min/max rsrc masks. Value 0 leaves hardware at default. */
static void si_pmu_res_masks(struct si_pub *sih, u32 * pmin, u32 * pmax)
{
	u32 min_mask = 0, max_mask = 0;
	uint rsrcs;

	/* # resources */
	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

	/* determine min/max rsrc masks */
	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
		/* ??? */
		break;

	case BCM4313_CHIP_ID:
		min_mask = PMURES_BIT(RES4313_BB_PU_RSRC) |
		    PMURES_BIT(RES4313_XTAL_PU_RSRC) |
		    PMURES_BIT(RES4313_ALP_AVAIL_RSRC) |
		    PMURES_BIT(RES4313_BB_PLL_PWRSW_RSRC);
		max_mask = 0xffff;
		break;
	default:
		break;
	}

	*pmin = min_mask;
	*pmax = max_mask;
}

static void
si_pmu_spuravoid_pllupdate(struct si_pub *sih, struct chipcregs __iomem *cc,
			   u8 spuravoid)
{
	u32 tmp = 0;

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
		if (spuravoid == 1) {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11500010);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
			W_REG(&cc->pllcontrol_data, 0x000C0C06);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x0F600a08);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
			W_REG(&cc->pllcontrol_data, 0x00000000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
			W_REG(&cc->pllcontrol_data, 0x2001E920);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
			W_REG(&cc->pllcontrol_data, 0x88888815);
		} else {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11100010);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
			W_REG(&cc->pllcontrol_data, 0x000c0c06);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x03000a08);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
			W_REG(&cc->pllcontrol_data, 0x00000000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
			W_REG(&cc->pllcontrol_data, 0x200005c0);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
			W_REG(&cc->pllcontrol_data, 0x88888815);
		}
		tmp = 1 << 10;
		break;

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
		W_REG(&cc->pllcontrol_data, 0x11100008);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
		W_REG(&cc->pllcontrol_data, 0x0c000c06);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
		W_REG(&cc->pllcontrol_data, 0x03000a08);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
		W_REG(&cc->pllcontrol_data, 0x00000000);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
		W_REG(&cc->pllcontrol_data, 0x200005c0);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		W_REG(&cc->pllcontrol_data, 0x88888855);

		tmp = 1 << 10;
		break;

	default:
		/* bail out */
		return;
	}

	tmp |= R_REG(&cc->pmucontrol);
	W_REG(&cc->pmucontrol, tmp);
}

u16 si_pmu_fast_pwrup_delay(struct si_pub *sih)
{
	uint delay = PMU_MAX_TRANSITION_DLY;

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM4313_CHIP_ID:
		delay = 3700;
		break;
	default:
		break;
	}

	return (u16) delay;
}

void si_pmu_sprom_enable(struct si_pub *sih, bool enable)
{
	struct chipcregs __iomem *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* Read/write a chipcontrol reg */
u32 si_pmu_chipcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(struct chipcregs, chipcontrol_addr),
		   ~0, reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(struct chipcregs, chipcontrol_data), mask,
			  val);
}

/* Read/write a regcontrol reg */
u32 si_pmu_regcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(struct chipcregs, regcontrol_addr),
		   ~0, reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(struct chipcregs, regcontrol_data), mask,
			  val);
}

/* Read/write a pllcontrol reg */
u32 si_pmu_pllcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(struct chipcregs, pllcontrol_addr),
		   ~0, reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(struct chipcregs, pllcontrol_data), mask,
			  val);
}

/* PMU PLL update */
void si_pmu_pllupd(struct si_pub *sih)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(struct chipcregs, pmucontrol),
		   PCTL_PLL_PLLCTL_UPD, PCTL_PLL_PLLCTL_UPD);
}

/* query alp/xtal clock frequency */
u32 si_pmu_alp_clock(struct si_pub *sih)
{
	u32 clock = ALP_CLOCK;

	/* bail out with default */
	if (!(sih->cccaps & CC_CAP_PMU))
		return clock;

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM4313_CHIP_ID:
		/* always 20Mhz */
		clock = 20000 * 1000;
		break;
	default:
		break;
	}

	return clock;
}

void si_pmu_spuravoid(struct si_pub *sih, u8 spuravoid)
{
	struct chipcregs __iomem *cc;
	uint origidx, intr_val;

	/* Remember original core before switch to chipc */
	cc = (struct chipcregs __iomem *)
			ai_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);

	/* update the pll changes */
	si_pmu_spuravoid_pllupdate(sih, cc, spuravoid);

	/* Return to original core */
	ai_restore_core(sih, origidx, intr_val);
}

/* initialize PMU */
void si_pmu_init(struct si_pub *sih)
{
	struct chipcregs __iomem *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	if (sih->pmurev == 1)
		AND_REG(&cc->pmucontrol, ~PCTL_NOILP_ON_WAIT);
	else if (sih->pmurev >= 2)
		OR_REG(&cc->pmucontrol, PCTL_NOILP_ON_WAIT);

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* initialize PMU chip controls and other chip level stuff */
void si_pmu_chip_init(struct si_pub *sih)
{
	uint origidx;

	/* Gate off SPROM clock and chip select signals */
	si_pmu_sprom_enable(sih, false);

	/* Remember original core */
	origidx = ai_coreidx(sih);

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* initialize PMU switch/regulators */
void si_pmu_swreg_init(struct si_pub *sih)
{
}

/* initialize PLL */
void si_pmu_pll_init(struct si_pub *sih, uint xtalfreq)
{
	struct chipcregs __iomem *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM4313_CHIP_ID:
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
		/* ??? */
		break;
	default:
		break;
	}

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* initialize PMU resources */
void si_pmu_res_init(struct si_pub *sih)
{
	struct chipcregs __iomem *cc;
	uint origidx;
	u32 min_mask = 0, max_mask = 0;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	/* Determine min/max rsrc masks */
	si_pmu_res_masks(sih, &min_mask, &max_mask);

	/* It is required to program max_mask first and then min_mask */

	/* Program max resource mask */

	if (max_mask)
		W_REG(&cc->max_res_mask, max_mask);

	/* Program min resource mask */

	if (min_mask)
		W_REG(&cc->min_res_mask, min_mask);

	/* Add some delay; allow resources to come up and settle. */
	mdelay(2);

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

u32 si_pmu_measure_alpclk(struct si_pub *sih)
{
	struct chipcregs __iomem *cc;
	uint origidx;
	u32 alp_khz;

	if (sih->pmurev < 10)
		return 0;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	if (R_REG(&cc->pmustatus) & PST_EXTLPOAVAIL) {
		u32 ilp_ctr, alp_hz;

		/*
		 * Enable the reg to measure the freq,
		 * in case it was disabled before
		 */
		W_REG(&cc->pmu_xtalfreq,
		      1U << PMU_XTALFREQ_REG_MEASURE_SHIFT);

		/* Delay for well over 4 ILP clocks */
		udelay(1000);

		/* Read the latched number of ALP ticks per 4 ILP ticks */
		ilp_ctr =
		    R_REG(&cc->pmu_xtalfreq) & PMU_XTALFREQ_REG_ILPCTR_MASK;

		/*
		 * Turn off the PMU_XTALFREQ_REG_MEASURE_SHIFT
		 * bit to save power
		 */
		W_REG(&cc->pmu_xtalfreq, 0);

		/* Calculate ALP frequency */
		alp_hz = (ilp_ctr * EXT_ILP_HZ) / 4;

		/*
		 * Round to nearest 100KHz, and at
		 * the same time convert to KHz
		 */
		alp_khz = (alp_hz + 50000) / 100000 * 100;
	} else
		alp_khz = 0;

	/* Return to original core */
	ai_setcoreidx(sih, origidx);

	return alp_khz;
}
