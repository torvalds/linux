/*
 * Sonics Silicon Backplane
 * Broadcom ChipCommon Power Management Unit driver
 *
 * Copyright 2009, Michael Buesch <m@bues.ch>
 * Copyright 2007, Broadcom Corporation
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/ssb/ssb_driver_chipcommon.h>
#include <linux/delay.h>
#include <linux/export.h>
#ifdef CONFIG_BCM47XX
#include <bcm47xx_nvram.h>
#endif

#include "ssb_private.h"

static u32 ssb_chipco_pll_read(struct ssb_chipcommon *cc, u32 offset)
{
	chipco_write32(cc, SSB_CHIPCO_PLLCTL_ADDR, offset);
	return chipco_read32(cc, SSB_CHIPCO_PLLCTL_DATA);
}

static void ssb_chipco_pll_write(struct ssb_chipcommon *cc,
				 u32 offset, u32 value)
{
	chipco_write32(cc, SSB_CHIPCO_PLLCTL_ADDR, offset);
	chipco_write32(cc, SSB_CHIPCO_PLLCTL_DATA, value);
}

static void ssb_chipco_regctl_maskset(struct ssb_chipcommon *cc,
				   u32 offset, u32 mask, u32 set)
{
	u32 value;

	chipco_read32(cc, SSB_CHIPCO_REGCTL_ADDR);
	chipco_write32(cc, SSB_CHIPCO_REGCTL_ADDR, offset);
	chipco_read32(cc, SSB_CHIPCO_REGCTL_ADDR);
	value = chipco_read32(cc, SSB_CHIPCO_REGCTL_DATA);
	value &= mask;
	value |= set;
	chipco_write32(cc, SSB_CHIPCO_REGCTL_DATA, value);
	chipco_read32(cc, SSB_CHIPCO_REGCTL_DATA);
}

struct pmu0_plltab_entry {
	u16 freq;	/* Crystal frequency in kHz.*/
	u8 xf;		/* Crystal frequency value for PMU control */
	u8 wb_int;
	u32 wb_frac;
};

static const struct pmu0_plltab_entry pmu0_plltab[] = {
	{ .freq = 12000, .xf =  1, .wb_int = 73, .wb_frac = 349525, },
	{ .freq = 13000, .xf =  2, .wb_int = 67, .wb_frac = 725937, },
	{ .freq = 14400, .xf =  3, .wb_int = 61, .wb_frac = 116508, },
	{ .freq = 15360, .xf =  4, .wb_int = 57, .wb_frac = 305834, },
	{ .freq = 16200, .xf =  5, .wb_int = 54, .wb_frac = 336579, },
	{ .freq = 16800, .xf =  6, .wb_int = 52, .wb_frac = 399457, },
	{ .freq = 19200, .xf =  7, .wb_int = 45, .wb_frac = 873813, },
	{ .freq = 19800, .xf =  8, .wb_int = 44, .wb_frac = 466033, },
	{ .freq = 20000, .xf =  9, .wb_int = 44, .wb_frac = 0,      },
	{ .freq = 25000, .xf = 10, .wb_int = 70, .wb_frac = 419430, },
	{ .freq = 26000, .xf = 11, .wb_int = 67, .wb_frac = 725937, },
	{ .freq = 30000, .xf = 12, .wb_int = 58, .wb_frac = 699050, },
	{ .freq = 38400, .xf = 13, .wb_int = 45, .wb_frac = 873813, },
	{ .freq = 40000, .xf = 14, .wb_int = 45, .wb_frac = 0,      },
};
#define SSB_PMU0_DEFAULT_XTALFREQ	20000

static const struct pmu0_plltab_entry * pmu0_plltab_find_entry(u32 crystalfreq)
{
	const struct pmu0_plltab_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmu0_plltab); i++) {
		e = &pmu0_plltab[i];
		if (e->freq == crystalfreq)
			return e;
	}

	return NULL;
}

/* Tune the PLL to the crystal speed. crystalfreq is in kHz. */
static void ssb_pmu0_pllinit_r0(struct ssb_chipcommon *cc,
				u32 crystalfreq)
{
	struct ssb_bus *bus = cc->dev->bus;
	const struct pmu0_plltab_entry *e = NULL;
	u32 pmuctl, tmp, pllctl;
	unsigned int i;

	if (crystalfreq)
		e = pmu0_plltab_find_entry(crystalfreq);
	if (!e)
		e = pmu0_plltab_find_entry(SSB_PMU0_DEFAULT_XTALFREQ);
	BUG_ON(!e);
	crystalfreq = e->freq;
	cc->pmu.crystalfreq = e->freq;

	/* Check if the PLL already is programmed to this frequency. */
	pmuctl = chipco_read32(cc, SSB_CHIPCO_PMU_CTL);
	if (((pmuctl & SSB_CHIPCO_PMU_CTL_XTALFREQ) >> SSB_CHIPCO_PMU_CTL_XTALFREQ_SHIFT) == e->xf) {
		/* We're already there... */
		return;
	}

	ssb_info("Programming PLL to %u.%03u MHz\n",
		 crystalfreq / 1000, crystalfreq % 1000);

	/* First turn the PLL off. */
	switch (bus->chip_id) {
	case 0x4328:
		chipco_mask32(cc, SSB_CHIPCO_PMU_MINRES_MSK,
			      ~(1 << SSB_PMURES_4328_BB_PLL_PU));
		chipco_mask32(cc, SSB_CHIPCO_PMU_MAXRES_MSK,
			      ~(1 << SSB_PMURES_4328_BB_PLL_PU));
		break;
	case 0x5354:
		chipco_mask32(cc, SSB_CHIPCO_PMU_MINRES_MSK,
			      ~(1 << SSB_PMURES_5354_BB_PLL_PU));
		chipco_mask32(cc, SSB_CHIPCO_PMU_MAXRES_MSK,
			      ~(1 << SSB_PMURES_5354_BB_PLL_PU));
		break;
	default:
		SSB_WARN_ON(1);
	}
	for (i = 1500; i; i--) {
		tmp = chipco_read32(cc, SSB_CHIPCO_CLKCTLST);
		if (!(tmp & SSB_CHIPCO_CLKCTLST_HAVEHT))
			break;
		udelay(10);
	}
	tmp = chipco_read32(cc, SSB_CHIPCO_CLKCTLST);
	if (tmp & SSB_CHIPCO_CLKCTLST_HAVEHT)
		ssb_emerg("Failed to turn the PLL off!\n");

	/* Set PDIV in PLL control 0. */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU0_PLLCTL0);
	if (crystalfreq >= SSB_PMU0_PLLCTL0_PDIV_FREQ)
		pllctl |= SSB_PMU0_PLLCTL0_PDIV_MSK;
	else
		pllctl &= ~SSB_PMU0_PLLCTL0_PDIV_MSK;
	ssb_chipco_pll_write(cc, SSB_PMU0_PLLCTL0, pllctl);

	/* Set WILD in PLL control 1. */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU0_PLLCTL1);
	pllctl &= ~SSB_PMU0_PLLCTL1_STOPMOD;
	pllctl &= ~(SSB_PMU0_PLLCTL1_WILD_IMSK | SSB_PMU0_PLLCTL1_WILD_FMSK);
	pllctl |= ((u32)e->wb_int << SSB_PMU0_PLLCTL1_WILD_IMSK_SHIFT) & SSB_PMU0_PLLCTL1_WILD_IMSK;
	pllctl |= ((u32)e->wb_frac << SSB_PMU0_PLLCTL1_WILD_FMSK_SHIFT) & SSB_PMU0_PLLCTL1_WILD_FMSK;
	if (e->wb_frac == 0)
		pllctl |= SSB_PMU0_PLLCTL1_STOPMOD;
	ssb_chipco_pll_write(cc, SSB_PMU0_PLLCTL1, pllctl);

	/* Set WILD in PLL control 2. */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU0_PLLCTL2);
	pllctl &= ~SSB_PMU0_PLLCTL2_WILD_IMSKHI;
	pllctl |= (((u32)e->wb_int >> 4) << SSB_PMU0_PLLCTL2_WILD_IMSKHI_SHIFT) & SSB_PMU0_PLLCTL2_WILD_IMSKHI;
	ssb_chipco_pll_write(cc, SSB_PMU0_PLLCTL2, pllctl);

	/* Set the crystalfrequency and the divisor. */
	pmuctl = chipco_read32(cc, SSB_CHIPCO_PMU_CTL);
	pmuctl &= ~SSB_CHIPCO_PMU_CTL_ILP_DIV;
	pmuctl |= (((crystalfreq + 127) / 128 - 1) << SSB_CHIPCO_PMU_CTL_ILP_DIV_SHIFT)
			& SSB_CHIPCO_PMU_CTL_ILP_DIV;
	pmuctl &= ~SSB_CHIPCO_PMU_CTL_XTALFREQ;
	pmuctl |= ((u32)e->xf << SSB_CHIPCO_PMU_CTL_XTALFREQ_SHIFT) & SSB_CHIPCO_PMU_CTL_XTALFREQ;
	chipco_write32(cc, SSB_CHIPCO_PMU_CTL, pmuctl);
}

struct pmu1_plltab_entry {
	u16 freq;	/* Crystal frequency in kHz.*/
	u8 xf;		/* Crystal frequency value for PMU control */
	u8 ndiv_int;
	u32 ndiv_frac;
	u8 p1div;
	u8 p2div;
};

static const struct pmu1_plltab_entry pmu1_plltab[] = {
	{ .freq = 12000, .xf =  1, .p1div = 3, .p2div = 22, .ndiv_int =  0x9, .ndiv_frac = 0xFFFFEF, },
	{ .freq = 13000, .xf =  2, .p1div = 1, .p2div =  6, .ndiv_int =  0xb, .ndiv_frac = 0x483483, },
	{ .freq = 14400, .xf =  3, .p1div = 1, .p2div = 10, .ndiv_int =  0xa, .ndiv_frac = 0x1C71C7, },
	{ .freq = 15360, .xf =  4, .p1div = 1, .p2div =  5, .ndiv_int =  0xb, .ndiv_frac = 0x755555, },
	{ .freq = 16200, .xf =  5, .p1div = 1, .p2div = 10, .ndiv_int =  0x5, .ndiv_frac = 0x6E9E06, },
	{ .freq = 16800, .xf =  6, .p1div = 1, .p2div = 10, .ndiv_int =  0x5, .ndiv_frac = 0x3CF3CF, },
	{ .freq = 19200, .xf =  7, .p1div = 1, .p2div =  9, .ndiv_int =  0x5, .ndiv_frac = 0x17B425, },
	{ .freq = 19800, .xf =  8, .p1div = 1, .p2div = 11, .ndiv_int =  0x4, .ndiv_frac = 0xA57EB,  },
	{ .freq = 20000, .xf =  9, .p1div = 1, .p2div = 11, .ndiv_int =  0x4, .ndiv_frac = 0,        },
	{ .freq = 24000, .xf = 10, .p1div = 3, .p2div = 11, .ndiv_int =  0xa, .ndiv_frac = 0,        },
	{ .freq = 25000, .xf = 11, .p1div = 5, .p2div = 16, .ndiv_int =  0xb, .ndiv_frac = 0,        },
	{ .freq = 26000, .xf = 12, .p1div = 1, .p2div =  2, .ndiv_int = 0x10, .ndiv_frac = 0xEC4EC4, },
	{ .freq = 30000, .xf = 13, .p1div = 3, .p2div =  8, .ndiv_int =  0xb, .ndiv_frac = 0,        },
	{ .freq = 38400, .xf = 14, .p1div = 1, .p2div =  5, .ndiv_int =  0x4, .ndiv_frac = 0x955555, },
	{ .freq = 40000, .xf = 15, .p1div = 1, .p2div =  2, .ndiv_int =  0xb, .ndiv_frac = 0,        },
};

#define SSB_PMU1_DEFAULT_XTALFREQ	15360

static const struct pmu1_plltab_entry * pmu1_plltab_find_entry(u32 crystalfreq)
{
	const struct pmu1_plltab_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pmu1_plltab); i++) {
		e = &pmu1_plltab[i];
		if (e->freq == crystalfreq)
			return e;
	}

	return NULL;
}

/* Tune the PLL to the crystal speed. crystalfreq is in kHz. */
static void ssb_pmu1_pllinit_r0(struct ssb_chipcommon *cc,
				u32 crystalfreq)
{
	struct ssb_bus *bus = cc->dev->bus;
	const struct pmu1_plltab_entry *e = NULL;
	u32 buffer_strength = 0;
	u32 tmp, pllctl, pmuctl;
	unsigned int i;

	if (bus->chip_id == 0x4312) {
		/* We do not touch the BCM4312 PLL and assume
		 * the default crystal settings work out-of-the-box. */
		cc->pmu.crystalfreq = 20000;
		return;
	}

	if (crystalfreq)
		e = pmu1_plltab_find_entry(crystalfreq);
	if (!e)
		e = pmu1_plltab_find_entry(SSB_PMU1_DEFAULT_XTALFREQ);
	BUG_ON(!e);
	crystalfreq = e->freq;
	cc->pmu.crystalfreq = e->freq;

	/* Check if the PLL already is programmed to this frequency. */
	pmuctl = chipco_read32(cc, SSB_CHIPCO_PMU_CTL);
	if (((pmuctl & SSB_CHIPCO_PMU_CTL_XTALFREQ) >> SSB_CHIPCO_PMU_CTL_XTALFREQ_SHIFT) == e->xf) {
		/* We're already there... */
		return;
	}

	ssb_info("Programming PLL to %u.%03u MHz\n",
		 crystalfreq / 1000, crystalfreq % 1000);

	/* First turn the PLL off. */
	switch (bus->chip_id) {
	case 0x4325:
		chipco_mask32(cc, SSB_CHIPCO_PMU_MINRES_MSK,
			      ~((1 << SSB_PMURES_4325_BBPLL_PWRSW_PU) |
				(1 << SSB_PMURES_4325_HT_AVAIL)));
		chipco_mask32(cc, SSB_CHIPCO_PMU_MAXRES_MSK,
			      ~((1 << SSB_PMURES_4325_BBPLL_PWRSW_PU) |
				(1 << SSB_PMURES_4325_HT_AVAIL)));
		/* Adjust the BBPLL to 2 on all channels later. */
		buffer_strength = 0x222222;
		break;
	default:
		SSB_WARN_ON(1);
	}
	for (i = 1500; i; i--) {
		tmp = chipco_read32(cc, SSB_CHIPCO_CLKCTLST);
		if (!(tmp & SSB_CHIPCO_CLKCTLST_HAVEHT))
			break;
		udelay(10);
	}
	tmp = chipco_read32(cc, SSB_CHIPCO_CLKCTLST);
	if (tmp & SSB_CHIPCO_CLKCTLST_HAVEHT)
		ssb_emerg("Failed to turn the PLL off!\n");

	/* Set p1div and p2div. */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU1_PLLCTL0);
	pllctl &= ~(SSB_PMU1_PLLCTL0_P1DIV | SSB_PMU1_PLLCTL0_P2DIV);
	pllctl |= ((u32)e->p1div << SSB_PMU1_PLLCTL0_P1DIV_SHIFT) & SSB_PMU1_PLLCTL0_P1DIV;
	pllctl |= ((u32)e->p2div << SSB_PMU1_PLLCTL0_P2DIV_SHIFT) & SSB_PMU1_PLLCTL0_P2DIV;
	ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL0, pllctl);

	/* Set ndiv int and ndiv mode */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU1_PLLCTL2);
	pllctl &= ~(SSB_PMU1_PLLCTL2_NDIVINT | SSB_PMU1_PLLCTL2_NDIVMODE);
	pllctl |= ((u32)e->ndiv_int << SSB_PMU1_PLLCTL2_NDIVINT_SHIFT) & SSB_PMU1_PLLCTL2_NDIVINT;
	pllctl |= (1 << SSB_PMU1_PLLCTL2_NDIVMODE_SHIFT) & SSB_PMU1_PLLCTL2_NDIVMODE;
	ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL2, pllctl);

	/* Set ndiv frac */
	pllctl = ssb_chipco_pll_read(cc, SSB_PMU1_PLLCTL3);
	pllctl &= ~SSB_PMU1_PLLCTL3_NDIVFRAC;
	pllctl |= ((u32)e->ndiv_frac << SSB_PMU1_PLLCTL3_NDIVFRAC_SHIFT) & SSB_PMU1_PLLCTL3_NDIVFRAC;
	ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL3, pllctl);

	/* Change the drive strength, if required. */
	if (buffer_strength) {
		pllctl = ssb_chipco_pll_read(cc, SSB_PMU1_PLLCTL5);
		pllctl &= ~SSB_PMU1_PLLCTL5_CLKDRV;
		pllctl |= (buffer_strength << SSB_PMU1_PLLCTL5_CLKDRV_SHIFT) & SSB_PMU1_PLLCTL5_CLKDRV;
		ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL5, pllctl);
	}

	/* Tune the crystalfreq and the divisor. */
	pmuctl = chipco_read32(cc, SSB_CHIPCO_PMU_CTL);
	pmuctl &= ~(SSB_CHIPCO_PMU_CTL_ILP_DIV | SSB_CHIPCO_PMU_CTL_XTALFREQ);
	pmuctl |= ((((u32)e->freq + 127) / 128 - 1) << SSB_CHIPCO_PMU_CTL_ILP_DIV_SHIFT)
			& SSB_CHIPCO_PMU_CTL_ILP_DIV;
	pmuctl |= ((u32)e->xf << SSB_CHIPCO_PMU_CTL_XTALFREQ_SHIFT) & SSB_CHIPCO_PMU_CTL_XTALFREQ;
	chipco_write32(cc, SSB_CHIPCO_PMU_CTL, pmuctl);
}

static void ssb_pmu_pll_init(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;
	u32 crystalfreq = 0; /* in kHz. 0 = keep default freq. */

	if (bus->bustype == SSB_BUSTYPE_SSB) {
#ifdef CONFIG_BCM47XX
		char buf[20];
		if (bcm47xx_nvram_getenv("xtalfreq", buf, sizeof(buf)) >= 0)
			crystalfreq = simple_strtoul(buf, NULL, 0);
#endif
	}

	switch (bus->chip_id) {
	case 0x4312:
	case 0x4325:
		ssb_pmu1_pllinit_r0(cc, crystalfreq);
		break;
	case 0x4328:
		ssb_pmu0_pllinit_r0(cc, crystalfreq);
		break;
	case 0x5354:
		if (crystalfreq == 0)
			crystalfreq = 25000;
		ssb_pmu0_pllinit_r0(cc, crystalfreq);
		break;
	case 0x4322:
		if (cc->pmu.rev == 2) {
			chipco_write32(cc, SSB_CHIPCO_PLLCTL_ADDR, 0x0000000A);
			chipco_write32(cc, SSB_CHIPCO_PLLCTL_DATA, 0x380005C0);
		}
		break;
	case 43222:
		break;
	default:
		ssb_err("ERROR: PLL init unknown for device %04X\n",
			bus->chip_id);
	}
}

struct pmu_res_updown_tab_entry {
	u8 resource;	/* The resource number */
	u16 updown;	/* The updown value */
};

enum pmu_res_depend_tab_task {
	PMU_RES_DEP_SET = 1,
	PMU_RES_DEP_ADD,
	PMU_RES_DEP_REMOVE,
};

struct pmu_res_depend_tab_entry {
	u8 resource;	/* The resource number */
	u8 task;	/* SET | ADD | REMOVE */
	u32 depend;	/* The depend mask */
};

static const struct pmu_res_updown_tab_entry pmu_res_updown_tab_4328a0[] = {
	{ .resource = SSB_PMURES_4328_EXT_SWITCHER_PWM,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_BB_SWITCHER_PWM,		.updown = 0x1F01, },
	{ .resource = SSB_PMURES_4328_BB_SWITCHER_BURST,	.updown = 0x010F, },
	{ .resource = SSB_PMURES_4328_BB_EXT_SWITCHER_BURST,	.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_ILP_REQUEST,		.updown = 0x0202, },
	{ .resource = SSB_PMURES_4328_RADIO_SWITCHER_PWM,	.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_RADIO_SWITCHER_BURST,	.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_ROM_SWITCH,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_PA_REF_LDO,		.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_RADIO_LDO,		.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_AFE_LDO,			.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_PLL_LDO,			.updown = 0x0F01, },
	{ .resource = SSB_PMURES_4328_BG_FILTBYP,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_TX_FILTBYP,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_RX_FILTBYP,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_XTAL_PU,			.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_XTAL_EN,			.updown = 0xA001, },
	{ .resource = SSB_PMURES_4328_BB_PLL_FILTBYP,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_RF_PLL_FILTBYP,		.updown = 0x0101, },
	{ .resource = SSB_PMURES_4328_BB_PLL_PU,		.updown = 0x0701, },
};

static const struct pmu_res_depend_tab_entry pmu_res_depend_tab_4328a0[] = {
	{
		/* Adjust ILP Request to avoid forcing EXT/BB into burst mode. */
		.resource = SSB_PMURES_4328_ILP_REQUEST,
		.task = PMU_RES_DEP_SET,
		.depend = ((1 << SSB_PMURES_4328_EXT_SWITCHER_PWM) |
			   (1 << SSB_PMURES_4328_BB_SWITCHER_PWM)),
	},
};

static const struct pmu_res_updown_tab_entry pmu_res_updown_tab_4325a0[] = {
	{ .resource = SSB_PMURES_4325_XTAL_PU,			.updown = 0x1501, },
};

static const struct pmu_res_depend_tab_entry pmu_res_depend_tab_4325a0[] = {
	{
		/* Adjust HT-Available dependencies. */
		.resource = SSB_PMURES_4325_HT_AVAIL,
		.task = PMU_RES_DEP_ADD,
		.depend = ((1 << SSB_PMURES_4325_RX_PWRSW_PU) |
			   (1 << SSB_PMURES_4325_TX_PWRSW_PU) |
			   (1 << SSB_PMURES_4325_LOGEN_PWRSW_PU) |
			   (1 << SSB_PMURES_4325_AFE_PWRSW_PU)),
	},
};

static void ssb_pmu_resources_init(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;
	u32 min_msk = 0, max_msk = 0;
	unsigned int i;
	const struct pmu_res_updown_tab_entry *updown_tab = NULL;
	unsigned int updown_tab_size = 0;
	const struct pmu_res_depend_tab_entry *depend_tab = NULL;
	unsigned int depend_tab_size = 0;

	switch (bus->chip_id) {
	case 0x4312:
		 min_msk = 0xCBB;
		 break;
	case 0x4322:
	case 43222:
		/* We keep the default settings:
		 * min_msk = 0xCBB
		 * max_msk = 0x7FFFF
		 */
		break;
	case 0x4325:
		/* Power OTP down later. */
		min_msk = (1 << SSB_PMURES_4325_CBUCK_BURST) |
			  (1 << SSB_PMURES_4325_LNLDO2_PU);
		if (chipco_read32(cc, SSB_CHIPCO_CHIPSTAT) &
		    SSB_CHIPCO_CHST_4325_PMUTOP_2B)
			min_msk |= (1 << SSB_PMURES_4325_CLDO_CBUCK_BURST);
		/* The PLL may turn on, if it decides so. */
		max_msk = 0xFFFFF;
		updown_tab = pmu_res_updown_tab_4325a0;
		updown_tab_size = ARRAY_SIZE(pmu_res_updown_tab_4325a0);
		depend_tab = pmu_res_depend_tab_4325a0;
		depend_tab_size = ARRAY_SIZE(pmu_res_depend_tab_4325a0);
		break;
	case 0x4328:
		min_msk = (1 << SSB_PMURES_4328_EXT_SWITCHER_PWM) |
			  (1 << SSB_PMURES_4328_BB_SWITCHER_PWM) |
			  (1 << SSB_PMURES_4328_XTAL_EN);
		/* The PLL may turn on, if it decides so. */
		max_msk = 0xFFFFF;
		updown_tab = pmu_res_updown_tab_4328a0;
		updown_tab_size = ARRAY_SIZE(pmu_res_updown_tab_4328a0);
		depend_tab = pmu_res_depend_tab_4328a0;
		depend_tab_size = ARRAY_SIZE(pmu_res_depend_tab_4328a0);
		break;
	case 0x5354:
		/* The PLL may turn on, if it decides so. */
		max_msk = 0xFFFFF;
		break;
	default:
		ssb_err("ERROR: PMU resource config unknown for device %04X\n",
			bus->chip_id);
	}

	if (updown_tab) {
		for (i = 0; i < updown_tab_size; i++) {
			chipco_write32(cc, SSB_CHIPCO_PMU_RES_TABSEL,
				       updown_tab[i].resource);
			chipco_write32(cc, SSB_CHIPCO_PMU_RES_UPDNTM,
				       updown_tab[i].updown);
		}
	}
	if (depend_tab) {
		for (i = 0; i < depend_tab_size; i++) {
			chipco_write32(cc, SSB_CHIPCO_PMU_RES_TABSEL,
				       depend_tab[i].resource);
			switch (depend_tab[i].task) {
			case PMU_RES_DEP_SET:
				chipco_write32(cc, SSB_CHIPCO_PMU_RES_DEPMSK,
					       depend_tab[i].depend);
				break;
			case PMU_RES_DEP_ADD:
				chipco_set32(cc, SSB_CHIPCO_PMU_RES_DEPMSK,
					     depend_tab[i].depend);
				break;
			case PMU_RES_DEP_REMOVE:
				chipco_mask32(cc, SSB_CHIPCO_PMU_RES_DEPMSK,
					      ~(depend_tab[i].depend));
				break;
			default:
				SSB_WARN_ON(1);
			}
		}
	}

	/* Set the resource masks. */
	if (min_msk)
		chipco_write32(cc, SSB_CHIPCO_PMU_MINRES_MSK, min_msk);
	if (max_msk)
		chipco_write32(cc, SSB_CHIPCO_PMU_MAXRES_MSK, max_msk);
}

/* http://bcm-v4.sipsolutions.net/802.11/SSB/PmuInit */
void ssb_pmu_init(struct ssb_chipcommon *cc)
{
	u32 pmucap;

	if (!(cc->capabilities & SSB_CHIPCO_CAP_PMU))
		return;

	pmucap = chipco_read32(cc, SSB_CHIPCO_PMU_CAP);
	cc->pmu.rev = (pmucap & SSB_CHIPCO_PMU_CAP_REVISION);

	ssb_dbg("Found rev %u PMU (capabilities 0x%08X)\n",
		cc->pmu.rev, pmucap);

	if (cc->pmu.rev == 1)
		chipco_mask32(cc, SSB_CHIPCO_PMU_CTL,
			      ~SSB_CHIPCO_PMU_CTL_NOILPONW);
	else
		chipco_set32(cc, SSB_CHIPCO_PMU_CTL,
			     SSB_CHIPCO_PMU_CTL_NOILPONW);
	ssb_pmu_pll_init(cc);
	ssb_pmu_resources_init(cc);
}

void ssb_pmu_set_ldo_voltage(struct ssb_chipcommon *cc,
			     enum ssb_pmu_ldo_volt_id id, u32 voltage)
{
	struct ssb_bus *bus = cc->dev->bus;
	u32 addr, shift, mask;

	switch (bus->chip_id) {
	case 0x4328:
	case 0x5354:
		switch (id) {
		case LDO_VOLT1:
			addr = 2;
			shift = 25;
			mask = 0xF;
			break;
		case LDO_VOLT2:
			addr = 3;
			shift = 1;
			mask = 0xF;
			break;
		case LDO_VOLT3:
			addr = 3;
			shift = 9;
			mask = 0xF;
			break;
		case LDO_PAREF:
			addr = 3;
			shift = 17;
			mask = 0x3F;
			break;
		default:
			SSB_WARN_ON(1);
			return;
		}
		break;
	case 0x4312:
		if (SSB_WARN_ON(id != LDO_PAREF))
			return;
		addr = 0;
		shift = 21;
		mask = 0x3F;
		break;
	default:
		return;
	}

	ssb_chipco_regctl_maskset(cc, addr, ~(mask << shift),
				  (voltage & mask) << shift);
}

void ssb_pmu_set_ldo_paref(struct ssb_chipcommon *cc, bool on)
{
	struct ssb_bus *bus = cc->dev->bus;
	int ldo;

	switch (bus->chip_id) {
	case 0x4312:
		ldo = SSB_PMURES_4312_PA_REF_LDO;
		break;
	case 0x4328:
		ldo = SSB_PMURES_4328_PA_REF_LDO;
		break;
	case 0x5354:
		ldo = SSB_PMURES_5354_PA_REF_LDO;
		break;
	default:
		return;
	}

	if (on)
		chipco_set32(cc, SSB_CHIPCO_PMU_MINRES_MSK, 1 << ldo);
	else
		chipco_mask32(cc, SSB_CHIPCO_PMU_MINRES_MSK, ~(1 << ldo));
	chipco_read32(cc, SSB_CHIPCO_PMU_MINRES_MSK); //SPEC FIXME found via mmiotrace - dummy read?
}

EXPORT_SYMBOL(ssb_pmu_set_ldo_voltage);
EXPORT_SYMBOL(ssb_pmu_set_ldo_paref);

static u32 ssb_pmu_get_alp_clock_clk0(struct ssb_chipcommon *cc)
{
	u32 crystalfreq;
	const struct pmu0_plltab_entry *e = NULL;

	crystalfreq = chipco_read32(cc, SSB_CHIPCO_PMU_CTL) &
		      SSB_CHIPCO_PMU_CTL_XTALFREQ >> SSB_CHIPCO_PMU_CTL_XTALFREQ_SHIFT;
	e = pmu0_plltab_find_entry(crystalfreq);
	BUG_ON(!e);
	return e->freq * 1000;
}

u32 ssb_pmu_get_alp_clock(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;

	switch (bus->chip_id) {
	case 0x5354:
		ssb_pmu_get_alp_clock_clk0(cc);
	default:
		ssb_err("ERROR: PMU alp clock unknown for device %04X\n",
			bus->chip_id);
		return 0;
	}
}

u32 ssb_pmu_get_cpu_clock(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;

	switch (bus->chip_id) {
	case 0x5354:
		/* 5354 chip uses a non programmable PLL of frequency 240MHz */
		return 240000000;
	default:
		ssb_err("ERROR: PMU cpu clock unknown for device %04X\n",
			bus->chip_id);
		return 0;
	}
}

u32 ssb_pmu_get_controlclock(struct ssb_chipcommon *cc)
{
	struct ssb_bus *bus = cc->dev->bus;

	switch (bus->chip_id) {
	case 0x5354:
		return 120000000;
	default:
		ssb_err("ERROR: PMU controlclock unknown for device %04X\n",
			bus->chip_id);
		return 0;
	}
}

void ssb_pmu_spuravoid_pllupdate(struct ssb_chipcommon *cc, int spuravoid)
{
	u32 pmu_ctl = 0;

	switch (cc->dev->bus->chip_id) {
	case 0x4322:
		ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL0, 0x11100070);
		ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL1, 0x1014140a);
		ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL5, 0x88888854);
		if (spuravoid == 1)
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL2, 0x05201828);
		else
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL2, 0x05001828);
		pmu_ctl = SSB_CHIPCO_PMU_CTL_PLL_UPD;
		break;
	case 43222:
		if (spuravoid == 1) {
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL0, 0x11500008);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL1, 0x0C000C06);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL2, 0x0F600a08);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL3, 0x00000000);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL4, 0x2001E920);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL5, 0x88888815);
		} else {
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL0, 0x11100008);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL1, 0x0c000c06);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL2, 0x03000a08);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL3, 0x00000000);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL4, 0x200005c0);
			ssb_chipco_pll_write(cc, SSB_PMU1_PLLCTL5, 0x88888855);
		}
		pmu_ctl = SSB_CHIPCO_PMU_CTL_PLL_UPD;
		break;
	default:
		ssb_printk(KERN_ERR PFX
			   "Unknown spuravoidance settings for chip 0x%04X, not changing PLL\n",
			   cc->dev->bus->chip_id);
		return;
	}

	chipco_set32(cc, SSB_CHIPCO_PMU_CTL, pmu_ctl);
}
EXPORT_SYMBOL_GPL(ssb_pmu_spuravoid_pllupdate);
