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
 * Duration for ILP clock frequency measurement in milliseconds
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

u16 si_pmu_fast_pwrup_delay(struct si_pub *sih)
{
	uint delay = PMU_MAX_TRANSITION_DLY;

	switch (ai_get_chip_id(sih)) {
	case BCMA_CHIP_ID_BCM43224:
	case BCMA_CHIP_ID_BCM43225:
	case BCMA_CHIP_ID_BCM4313:
		delay = 3700;
		break;
	default:
		break;
	}

	return (u16) delay;
}

u32 si_pmu_measure_alpclk(struct si_pub *sih)
{
	struct si_info *sii = container_of(sih, struct si_info, pub);
	struct bcma_device *core;
	u32 alp_khz;

	if (ai_get_pmurev(sih) < 10)
		return 0;

	/* Remember original core before switch to chipc */
	core = sii->icbus->drv_cc.core;

	if (bcma_read32(core, CHIPCREGOFFS(pmustatus)) & PST_EXTLPOAVAIL) {
		u32 ilp_ctr, alp_hz;

		/*
		 * Enable the reg to measure the freq,
		 * in case it was disabled before
		 */
		bcma_write32(core, CHIPCREGOFFS(pmu_xtalfreq),
			    1U << PMU_XTALFREQ_REG_MEASURE_SHIFT);

		/* Delay for well over 4 ILP clocks */
		udelay(1000);

		/* Read the latched number of ALP ticks per 4 ILP ticks */
		ilp_ctr = bcma_read32(core, CHIPCREGOFFS(pmu_xtalfreq)) &
			  PMU_XTALFREQ_REG_ILPCTR_MASK;

		/*
		 * Turn off the PMU_XTALFREQ_REG_MEASURE_SHIFT
		 * bit to save power
		 */
		bcma_write32(core, CHIPCREGOFFS(pmu_xtalfreq), 0);

		/* Calculate ALP frequency */
		alp_hz = (ilp_ctr * EXT_ILP_HZ) / 4;

		/*
		 * Round to nearest 100KHz, and at
		 * the same time convert to KHz
		 */
		alp_khz = (alp_hz + 50000) / 100000 * 100;
	} else
		alp_khz = 0;

	return alp_khz;
}
