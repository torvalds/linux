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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <bcmdevs.h>
#include <sbchipc.h>
#include <bcmutils.h>
#include <bcmnvram.h>
#include "wlc_pmu.h"

/*
 * d11 slow to fast clock transition time in slow clock cycles
 */
#define D11SCC_SLOW2FAST_TRANSITION	2

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

/*
 * FVCO frequency
 */
#define FVCO_880	880000	/* 880MHz */
#define FVCO_1760	1760000	/* 1760MHz */
#define FVCO_1440	1440000	/* 1440MHz */
#define FVCO_960	960000	/* 960MHz */

/*
 * PMU crystal table indices for 1440MHz fvco
 */
#define PMU1_XTALTAB0_1440_12000K	0
#define PMU1_XTALTAB0_1440_13000K	1
#define PMU1_XTALTAB0_1440_14400K	2
#define PMU1_XTALTAB0_1440_15360K	3
#define PMU1_XTALTAB0_1440_16200K	4
#define PMU1_XTALTAB0_1440_16800K	5
#define PMU1_XTALTAB0_1440_19200K	6
#define PMU1_XTALTAB0_1440_19800K	7
#define PMU1_XTALTAB0_1440_20000K	8
#define PMU1_XTALTAB0_1440_25000K	9
#define PMU1_XTALTAB0_1440_26000K	10
#define PMU1_XTALTAB0_1440_30000K	11
#define PMU1_XTALTAB0_1440_37400K	12
#define PMU1_XTALTAB0_1440_38400K	13
#define PMU1_XTALTAB0_1440_40000K	14
#define PMU1_XTALTAB0_1440_48000K	15

/*
 * PMU crystal table indices for 960MHz fvco
 */
#define PMU1_XTALTAB0_960_12000K	0
#define PMU1_XTALTAB0_960_13000K	1
#define PMU1_XTALTAB0_960_14400K	2
#define PMU1_XTALTAB0_960_15360K	3
#define PMU1_XTALTAB0_960_16200K	4
#define PMU1_XTALTAB0_960_16800K	5
#define PMU1_XTALTAB0_960_19200K	6
#define PMU1_XTALTAB0_960_19800K	7
#define PMU1_XTALTAB0_960_20000K	8
#define PMU1_XTALTAB0_960_25000K	9
#define PMU1_XTALTAB0_960_26000K	10
#define PMU1_XTALTAB0_960_30000K	11
#define PMU1_XTALTAB0_960_37400K	12
#define PMU1_XTALTAB0_960_38400K	13
#define PMU1_XTALTAB0_960_40000K	14
#define PMU1_XTALTAB0_960_48000K	15

/*
 * PMU crystal table indices for 880MHz fvco
 */
#define PMU1_XTALTAB0_880_12000K	0
#define PMU1_XTALTAB0_880_13000K	1
#define PMU1_XTALTAB0_880_14400K	2
#define PMU1_XTALTAB0_880_15360K	3
#define PMU1_XTALTAB0_880_16200K	4
#define PMU1_XTALTAB0_880_16800K	5
#define PMU1_XTALTAB0_880_19200K	6
#define PMU1_XTALTAB0_880_19800K	7
#define PMU1_XTALTAB0_880_20000K	8
#define PMU1_XTALTAB0_880_24000K	9
#define PMU1_XTALTAB0_880_25000K	10
#define PMU1_XTALTAB0_880_26000K	11
#define PMU1_XTALTAB0_880_30000K	12
#define PMU1_XTALTAB0_880_37400K	13
#define PMU1_XTALTAB0_880_38400K	14
#define PMU1_XTALTAB0_880_40000K	15

/*
 * crystal frequency values
 */
#define XTAL_FREQ_24000MHZ		24000
#define XTAL_FREQ_30000MHZ		30000
#define XTAL_FREQ_37400MHZ		37400
#define XTAL_FREQ_48000MHZ		48000

/*
 * Resource dependancies mask change action
 *
 * @RES_DEPEND_SET: Override the dependancies mask
 * @RES_DEPEND_ADD: Add to the  dependancies mask
 * @RES_DEPEND_REMOVE: Remove from the dependancies mask
 */
#define RES_DEPEND_SET		0
#define RES_DEPEND_ADD		1
#define RES_DEPEND_REMOVE	-1

/* d11 slow to fast clock transition time in slow clock cycles */
#define D11SCC_SLOW2FAST_TRANSITION	2

/* Setup resource up/down timers */
typedef struct {
	u8 resnum;
	u16 updown;
} pmu_res_updown_t;

/* Change resource dependancies masks */
typedef struct {
	u32 res_mask;	/* resources (chip specific) */
	s8 action;		/* action */
	u32 depend_mask;	/* changes to the dependancies mask */
	 bool(*filter) (si_t *sih);	/* action is taken when filter is NULL or return true */
} pmu_res_depend_t;

/* setup pll and query clock speed */
typedef struct {
	u16 fref;
	u8 xf;
	u8 p1div;
	u8 p2div;
	u8 ndiv_int;
	u32 ndiv_frac;
} pmu1_xtaltab0_t;

/*
 * prototypes used in resource tables
 */
static bool si_pmu_res_depfltr_bb(si_t *sih);
static bool si_pmu_res_depfltr_ncb(si_t *sih);
static bool si_pmu_res_depfltr_paldo(si_t *sih);
static bool si_pmu_res_depfltr_npaldo(si_t *sih);

static const pmu_res_updown_t bcm4328a0_res_updown[] = {
	{
	RES4328_EXT_SWITCHER_PWM, 0x0101}, {
	RES4328_BB_SWITCHER_PWM, 0x1f01}, {
	RES4328_BB_SWITCHER_BURST, 0x010f}, {
	RES4328_BB_EXT_SWITCHER_BURST, 0x0101}, {
	RES4328_ILP_REQUEST, 0x0202}, {
	RES4328_RADIO_SWITCHER_PWM, 0x0f01}, {
	RES4328_RADIO_SWITCHER_BURST, 0x0f01}, {
	RES4328_ROM_SWITCH, 0x0101}, {
	RES4328_PA_REF_LDO, 0x0f01}, {
	RES4328_RADIO_LDO, 0x0f01}, {
	RES4328_AFE_LDO, 0x0f01}, {
	RES4328_PLL_LDO, 0x0f01}, {
	RES4328_BG_FILTBYP, 0x0101}, {
	RES4328_TX_FILTBYP, 0x0101}, {
	RES4328_RX_FILTBYP, 0x0101}, {
	RES4328_XTAL_PU, 0x0101}, {
	RES4328_XTAL_EN, 0xa001}, {
	RES4328_BB_PLL_FILTBYP, 0x0101}, {
	RES4328_RF_PLL_FILTBYP, 0x0101}, {
	RES4328_BB_PLL_PU, 0x0701}
};

static const pmu_res_depend_t bcm4328a0_res_depend[] = {
	/* Adjust ILP request resource not to force ext/BB switchers into burst mode */
	{
	PMURES_BIT(RES4328_ILP_REQUEST),
		    RES_DEPEND_SET,
		    PMURES_BIT(RES4328_EXT_SWITCHER_PWM) |
		    PMURES_BIT(RES4328_BB_SWITCHER_PWM), NULL}
};

static const pmu_res_updown_t bcm4325a0_res_updown_qt[] = {
	{
	RES4325_HT_AVAIL, 0x0300}, {
	RES4325_BBPLL_PWRSW_PU, 0x0101}, {
	RES4325_RFPLL_PWRSW_PU, 0x0101}, {
	RES4325_ALP_AVAIL, 0x0100}, {
	RES4325_XTAL_PU, 0x1000}, {
	RES4325_LNLDO1_PU, 0x0800}, {
	RES4325_CLDO_CBUCK_PWM, 0x0101}, {
	RES4325_CBUCK_PWM, 0x0803}
};

static const pmu_res_updown_t bcm4325a0_res_updown[] = {
	{
	RES4325_XTAL_PU, 0x1501}
};

static const pmu_res_depend_t bcm4325a0_res_depend[] = {
	/* Adjust OTP PU resource dependencies - remove BB BURST */
	{
	PMURES_BIT(RES4325_OTP_PU),
		    RES_DEPEND_REMOVE,
		    PMURES_BIT(RES4325_BUCK_BOOST_BURST), NULL},
	    /* Adjust ALP/HT Avail resource dependencies - bring up BB along if it is used. */
	{
	PMURES_BIT(RES4325_ALP_AVAIL) | PMURES_BIT(RES4325_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4325_BUCK_BOOST_BURST) |
		    PMURES_BIT(RES4325_BUCK_BOOST_PWM), si_pmu_res_depfltr_bb},
	    /* Adjust HT Avail resource dependencies - bring up RF switches along with HT. */
	{
	PMURES_BIT(RES4325_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4325_RX_PWRSW_PU) |
		    PMURES_BIT(RES4325_TX_PWRSW_PU) |
		    PMURES_BIT(RES4325_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4325_AFE_PWRSW_PU), NULL},
	    /* Adjust ALL resource dependencies - remove CBUCK dependancies if it is not used. */
	{
	PMURES_BIT(RES4325_ILP_REQUEST) |
		    PMURES_BIT(RES4325_ABUCK_BURST) |
		    PMURES_BIT(RES4325_ABUCK_PWM) |
		    PMURES_BIT(RES4325_LNLDO1_PU) |
		    PMURES_BIT(RES4325C1_LNLDO2_PU) |
		    PMURES_BIT(RES4325_XTAL_PU) |
		    PMURES_BIT(RES4325_ALP_AVAIL) |
		    PMURES_BIT(RES4325_RX_PWRSW_PU) |
		    PMURES_BIT(RES4325_TX_PWRSW_PU) |
		    PMURES_BIT(RES4325_RFPLL_PWRSW_PU) |
		    PMURES_BIT(RES4325_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4325_AFE_PWRSW_PU) |
		    PMURES_BIT(RES4325_BBPLL_PWRSW_PU) |
		    PMURES_BIT(RES4325_HT_AVAIL), RES_DEPEND_REMOVE,
		    PMURES_BIT(RES4325B0_CBUCK_LPOM) |
		    PMURES_BIT(RES4325B0_CBUCK_BURST) |
		    PMURES_BIT(RES4325B0_CBUCK_PWM), si_pmu_res_depfltr_ncb}
};

static const pmu_res_updown_t bcm4315a0_res_updown_qt[] = {
	{
	RES4315_HT_AVAIL, 0x0101}, {
	RES4315_XTAL_PU, 0x0100}, {
	RES4315_LNLDO1_PU, 0x0100}, {
	RES4315_PALDO_PU, 0x0100}, {
	RES4315_CLDO_PU, 0x0100}, {
	RES4315_CBUCK_PWM, 0x0100}, {
	RES4315_CBUCK_BURST, 0x0100}, {
	RES4315_CBUCK_LPOM, 0x0100}
};

static const pmu_res_updown_t bcm4315a0_res_updown[] = {
	{
	RES4315_XTAL_PU, 0x2501}
};

static const pmu_res_depend_t bcm4315a0_res_depend[] = {
	/* Adjust OTP PU resource dependencies - not need PALDO unless write */
	{
	PMURES_BIT(RES4315_OTP_PU),
		    RES_DEPEND_REMOVE,
		    PMURES_BIT(RES4315_PALDO_PU), si_pmu_res_depfltr_npaldo},
	    /* Adjust ALP/HT Avail resource dependencies - bring up PALDO along if it is used. */
	{
	PMURES_BIT(RES4315_ALP_AVAIL) | PMURES_BIT(RES4315_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4315_PALDO_PU), si_pmu_res_depfltr_paldo},
	    /* Adjust HT Avail resource dependencies - bring up RF switches along with HT. */
	{
	PMURES_BIT(RES4315_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4315_RX_PWRSW_PU) |
		    PMURES_BIT(RES4315_TX_PWRSW_PU) |
		    PMURES_BIT(RES4315_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4315_AFE_PWRSW_PU), NULL},
	    /* Adjust ALL resource dependencies - remove CBUCK dependancies if it is not used. */
	{
	PMURES_BIT(RES4315_CLDO_PU) | PMURES_BIT(RES4315_ILP_REQUEST) |
		    PMURES_BIT(RES4315_LNLDO1_PU) |
		    PMURES_BIT(RES4315_OTP_PU) |
		    PMURES_BIT(RES4315_LNLDO2_PU) |
		    PMURES_BIT(RES4315_XTAL_PU) |
		    PMURES_BIT(RES4315_ALP_AVAIL) |
		    PMURES_BIT(RES4315_RX_PWRSW_PU) |
		    PMURES_BIT(RES4315_TX_PWRSW_PU) |
		    PMURES_BIT(RES4315_RFPLL_PWRSW_PU) |
		    PMURES_BIT(RES4315_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4315_AFE_PWRSW_PU) |
		    PMURES_BIT(RES4315_BBPLL_PWRSW_PU) |
		    PMURES_BIT(RES4315_HT_AVAIL), RES_DEPEND_REMOVE,
		    PMURES_BIT(RES4315_CBUCK_LPOM) |
		    PMURES_BIT(RES4315_CBUCK_BURST) |
		    PMURES_BIT(RES4315_CBUCK_PWM), si_pmu_res_depfltr_ncb}
};

	/* 4329 specific. needs to come back this issue later */
static const pmu_res_updown_t bcm4329_res_updown[] = {
	{
	RES4329_XTAL_PU, 0x1501}
};

static const pmu_res_depend_t bcm4329_res_depend[] = {
	/* Adjust HT Avail resource dependencies */
	{
	PMURES_BIT(RES4329_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4329_CBUCK_LPOM) |
		    PMURES_BIT(RES4329_CBUCK_BURST) |
		    PMURES_BIT(RES4329_CBUCK_PWM) |
		    PMURES_BIT(RES4329_CLDO_PU) |
		    PMURES_BIT(RES4329_PALDO_PU) |
		    PMURES_BIT(RES4329_LNLDO1_PU) |
		    PMURES_BIT(RES4329_XTAL_PU) |
		    PMURES_BIT(RES4329_ALP_AVAIL) |
		    PMURES_BIT(RES4329_RX_PWRSW_PU) |
		    PMURES_BIT(RES4329_TX_PWRSW_PU) |
		    PMURES_BIT(RES4329_RFPLL_PWRSW_PU) |
		    PMURES_BIT(RES4329_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4329_AFE_PWRSW_PU) |
		    PMURES_BIT(RES4329_BBPLL_PWRSW_PU), NULL}
};

static const pmu_res_updown_t bcm4319a0_res_updown_qt[] = {
	{
	RES4319_HT_AVAIL, 0x0101}, {
	RES4319_XTAL_PU, 0x0100}, {
	RES4319_LNLDO1_PU, 0x0100}, {
	RES4319_PALDO_PU, 0x0100}, {
	RES4319_CLDO_PU, 0x0100}, {
	RES4319_CBUCK_PWM, 0x0100}, {
	RES4319_CBUCK_BURST, 0x0100}, {
	RES4319_CBUCK_LPOM, 0x0100}
};

static const pmu_res_updown_t bcm4319a0_res_updown[] = {
	{
	RES4319_XTAL_PU, 0x3f01}
};

static const pmu_res_depend_t bcm4319a0_res_depend[] = {
	/* Adjust OTP PU resource dependencies - not need PALDO unless write */
	{
	PMURES_BIT(RES4319_OTP_PU),
		    RES_DEPEND_REMOVE,
		    PMURES_BIT(RES4319_PALDO_PU), si_pmu_res_depfltr_npaldo},
	    /* Adjust HT Avail resource dependencies - bring up PALDO along if it is used. */
	{
	PMURES_BIT(RES4319_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4319_PALDO_PU), si_pmu_res_depfltr_paldo},
	    /* Adjust HT Avail resource dependencies - bring up RF switches along with HT. */
	{
	PMURES_BIT(RES4319_HT_AVAIL),
		    RES_DEPEND_ADD,
		    PMURES_BIT(RES4319_RX_PWRSW_PU) |
		    PMURES_BIT(RES4319_TX_PWRSW_PU) |
		    PMURES_BIT(RES4319_RFPLL_PWRSW_PU) |
		    PMURES_BIT(RES4319_LOGEN_PWRSW_PU) |
		    PMURES_BIT(RES4319_AFE_PWRSW_PU), NULL}
};

static const pmu_res_updown_t bcm4336a0_res_updown_qt[] = {
	{
	RES4336_HT_AVAIL, 0x0101}, {
	RES4336_XTAL_PU, 0x0100}, {
	RES4336_CLDO_PU, 0x0100}, {
	RES4336_CBUCK_PWM, 0x0100}, {
	RES4336_CBUCK_BURST, 0x0100}, {
	RES4336_CBUCK_LPOM, 0x0100}
};

static const pmu_res_updown_t bcm4336a0_res_updown[] = {
	{
	RES4336_HT_AVAIL, 0x0D01}
};

static const pmu_res_depend_t bcm4336a0_res_depend[] = {
	/* Just a dummy entry for now */
	{
	PMURES_BIT(RES4336_RSVD), RES_DEPEND_ADD, 0, NULL}
};

static const pmu_res_updown_t bcm4330a0_res_updown_qt[] = {
	{
	RES4330_HT_AVAIL, 0x0101}, {
	RES4330_XTAL_PU, 0x0100}, {
	RES4330_CLDO_PU, 0x0100}, {
	RES4330_CBUCK_PWM, 0x0100}, {
	RES4330_CBUCK_BURST, 0x0100}, {
	RES4330_CBUCK_LPOM, 0x0100}
};

static const pmu_res_updown_t bcm4330a0_res_updown[] = {
	{
	RES4330_HT_AVAIL, 0x0e02}
};

static const pmu_res_depend_t bcm4330a0_res_depend[] = {
	/* Just a dummy entry for now */
	{
	PMURES_BIT(RES4330_HT_AVAIL), RES_DEPEND_ADD, 0, NULL}
};

/* the following table is based on 1440Mhz fvco */
static const pmu1_xtaltab0_t pmu1_xtaltab0_1440[] = {
	{
	12000, 1, 1, 1, 0x78, 0x0}, {
	13000, 2, 1, 1, 0x6E, 0xC4EC4E}, {
	14400, 3, 1, 1, 0x64, 0x0}, {
	15360, 4, 1, 1, 0x5D, 0xC00000}, {
	16200, 5, 1, 1, 0x58, 0xE38E38}, {
	16800, 6, 1, 1, 0x55, 0xB6DB6D}, {
	19200, 7, 1, 1, 0x4B, 0}, {
	19800, 8, 1, 1, 0x48, 0xBA2E8B}, {
	20000, 9, 1, 1, 0x48, 0x0}, {
	25000, 10, 1, 1, 0x39, 0x999999}, {
	26000, 11, 1, 1, 0x37, 0x627627}, {
	30000, 12, 1, 1, 0x30, 0x0}, {
	37400, 13, 2, 1, 0x4D, 0x15E76}, {
	38400, 13, 2, 1, 0x4B, 0x0}, {
	40000, 14, 2, 1, 0x48, 0x0}, {
	48000, 15, 2, 1, 0x3c, 0x0}, {
	0, 0, 0, 0, 0, 0}
};

static const pmu1_xtaltab0_t pmu1_xtaltab0_960[] = {
	{
	12000, 1, 1, 1, 0x50, 0x0}, {
	13000, 2, 1, 1, 0x49, 0xD89D89}, {
	14400, 3, 1, 1, 0x42, 0xAAAAAA}, {
	15360, 4, 1, 1, 0x3E, 0x800000}, {
	16200, 5, 1, 1, 0x39, 0x425ED0}, {
	16800, 6, 1, 1, 0x39, 0x249249}, {
	19200, 7, 1, 1, 0x32, 0x0}, {
	19800, 8, 1, 1, 0x30, 0x7C1F07}, {
	20000, 9, 1, 1, 0x30, 0x0}, {
	25000, 10, 1, 1, 0x26, 0x666666}, {
	26000, 11, 1, 1, 0x24, 0xEC4EC4}, {
	30000, 12, 1, 1, 0x20, 0x0}, {
	37400, 13, 2, 1, 0x33, 0x563EF9}, {
	38400, 14, 2, 1, 0x32, 0x0}, {
	40000, 15, 2, 1, 0x30, 0x0}, {
	48000, 16, 2, 1, 0x28, 0x0}, {
	0, 0, 0, 0, 0, 0}
};

static const pmu1_xtaltab0_t pmu1_xtaltab0_880_4329[] = {
	{
	12000, 1, 3, 22, 0x9, 0xFFFFEF}, {
	13000, 2, 1, 6, 0xb, 0x483483}, {
	14400, 3, 1, 10, 0xa, 0x1C71C7}, {
	15360, 4, 1, 5, 0xb, 0x755555}, {
	16200, 5, 1, 10, 0x5, 0x6E9E06}, {
	16800, 6, 1, 10, 0x5, 0x3Cf3Cf}, {
	19200, 7, 1, 4, 0xb, 0x755555}, {
	19800, 8, 1, 11, 0x4, 0xA57EB}, {
	20000, 9, 1, 11, 0x4, 0x0}, {
	24000, 10, 3, 11, 0xa, 0x0}, {
	25000, 11, 5, 16, 0xb, 0x0}, {
	26000, 12, 1, 1, 0x21, 0xD89D89}, {
	30000, 13, 3, 8, 0xb, 0x0}, {
	37400, 14, 3, 1, 0x46, 0x969696}, {
	38400, 15, 1, 1, 0x16, 0xEAAAAA}, {
	40000, 16, 1, 2, 0xb, 0}, {
	0, 0, 0, 0, 0, 0}
};

/* the following table is based on 880Mhz fvco */
static const pmu1_xtaltab0_t pmu1_xtaltab0_880[] = {
	{
	12000, 1, 3, 22, 0x9, 0xFFFFEF}, {
	13000, 2, 1, 6, 0xb, 0x483483}, {
	14400, 3, 1, 10, 0xa, 0x1C71C7}, {
	15360, 4, 1, 5, 0xb, 0x755555}, {
	16200, 5, 1, 10, 0x5, 0x6E9E06}, {
	16800, 6, 1, 10, 0x5, 0x3Cf3Cf}, {
	19200, 7, 1, 4, 0xb, 0x755555}, {
	19800, 8, 1, 11, 0x4, 0xA57EB}, {
	20000, 9, 1, 11, 0x4, 0x0}, {
	24000, 10, 3, 11, 0xa, 0x0}, {
	25000, 11, 5, 16, 0xb, 0x0}, {
	26000, 12, 1, 2, 0x10, 0xEC4EC4}, {
	30000, 13, 3, 8, 0xb, 0x0}, {
	33600, 14, 1, 2, 0xd, 0x186186}, {
	38400, 15, 1, 2, 0xb, 0x755555}, {
	40000, 16, 1, 2, 0xb, 0}, {
	0, 0, 0, 0, 0, 0}
};

/* true if the power topology uses the buck boost to provide 3.3V to VDDIO_RF and WLAN PA */
static bool si_pmu_res_depfltr_bb(si_t *sih)
{
	return (sih->boardflags & BFL_BUCKBOOST) != 0;
}

/* true if the power topology doesn't use the cbuck. Key on chiprev also if the chip is BCM4325. */
static bool si_pmu_res_depfltr_ncb(si_t *sih)
{

	return (sih->boardflags & BFL_NOCBUCK) != 0;
}

/* true if the power topology uses the PALDO */
static bool si_pmu_res_depfltr_paldo(si_t *sih)
{
	return (sih->boardflags & BFL_PALDO) != 0;
}

/* true if the power topology doesn't use the PALDO */
static bool si_pmu_res_depfltr_npaldo(si_t *sih)
{
	return (sih->boardflags & BFL_PALDO) == 0;
}

/* Return dependancies (direct or all/indirect) for the given resources */
static u32
si_pmu_res_deps(si_t *sih, chipcregs_t *cc, u32 rsrcs,
		bool all)
{
	u32 deps = 0;
	u32 i;

	for (i = 0; i <= PMURES_MAX_RESNUM; i++) {
		if (!(rsrcs & PMURES_BIT(i)))
			continue;
		W_REG(&cc->res_table_sel, i);
		deps |= R_REG(&cc->res_dep_mask);
	}

	return !all ? deps : (deps
			      ? (deps |
				 si_pmu_res_deps(sih, cc, deps,
						 true)) : 0);
}

/* Determine min/max rsrc masks. Value 0 leaves hardware at default. */
static void si_pmu_res_masks(si_t *sih, u32 * pmin, u32 * pmax)
{
	u32 min_mask = 0, max_mask = 0;
	uint rsrcs;
	char *val;

	/* # resources */
	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

	/* determine min/max rsrc masks */
	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43238_CHIP_ID:
	case BCM4331_CHIP_ID:
	case BCM6362_CHIP_ID:
		/* ??? */
		break;

	case BCM4329_CHIP_ID:
		/* 4329 spedific issue. Needs to come back this issue later */
		/* Down to save the power. */
		min_mask =
		    PMURES_BIT(RES4329_CBUCK_LPOM) |
		    PMURES_BIT(RES4329_CLDO_PU);
		/* Allow (but don't require) PLL to turn on */
		max_mask = 0x3ff63e;
		break;
	case BCM4319_CHIP_ID:
		/* We only need a few resources to be kept on all the time */
		min_mask = PMURES_BIT(RES4319_CBUCK_LPOM) |
		    PMURES_BIT(RES4319_CLDO_PU);

		/* Allow everything else to be turned on upon requests */
		max_mask = ~(~0 << rsrcs);
		break;
	case BCM4336_CHIP_ID:
		/* Down to save the power. */
		min_mask =
		    PMURES_BIT(RES4336_CBUCK_LPOM) | PMURES_BIT(RES4336_CLDO_PU)
		    | PMURES_BIT(RES4336_LDO3P3_PU) | PMURES_BIT(RES4336_OTP_PU)
		    | PMURES_BIT(RES4336_DIS_INT_RESET_PD);
		/* Allow (but don't require) PLL to turn on */
		max_mask = 0x1ffffff;
		break;

	case BCM4330_CHIP_ID:
		/* Down to save the power. */
		min_mask =
		    PMURES_BIT(RES4330_CBUCK_LPOM) | PMURES_BIT(RES4330_CLDO_PU)
		    | PMURES_BIT(RES4330_DIS_INT_RESET_PD) |
		    PMURES_BIT(RES4330_LDO3P3_PU) | PMURES_BIT(RES4330_OTP_PU);
		/* Allow (but don't require) PLL to turn on */
		max_mask = 0xfffffff;
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

	/* Apply nvram override to min mask */
	val = getvar(NULL, "rmin");
	if (val != NULL) {
		min_mask = (u32) simple_strtoul(val, NULL, 0);
	}
	/* Apply nvram override to max mask */
	val = getvar(NULL, "rmax");
	if (val != NULL) {
		max_mask = (u32) simple_strtoul(val, NULL, 0);
	}

	*pmin = min_mask;
	*pmax = max_mask;
}

/* Return up time in ILP cycles for the given resource. */
static uint
si_pmu_res_uptime(si_t *sih, chipcregs_t *cc, u8 rsrc) {
	u32 deps;
	uint up, i, dup, dmax;
	u32 min_mask = 0, max_mask = 0;

	/* uptime of resource 'rsrc' */
	W_REG(&cc->res_table_sel, rsrc);
	up = (R_REG(&cc->res_updn_timer) >> 8) & 0xff;

	/* direct dependancies of resource 'rsrc' */
	deps = si_pmu_res_deps(sih, cc, PMURES_BIT(rsrc), false);
	for (i = 0; i <= PMURES_MAX_RESNUM; i++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		deps &= ~si_pmu_res_deps(sih, cc, PMURES_BIT(i), true);
	}
	si_pmu_res_masks(sih, &min_mask, &max_mask);
	deps &= ~min_mask;

	/* max uptime of direct dependancies */
	dmax = 0;
	for (i = 0; i <= PMURES_MAX_RESNUM; i++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		dup = si_pmu_res_uptime(sih, cc, (u8) i);
		if (dmax < dup)
			dmax = dup;
	}

	return up + dmax + PMURES_UP_TRANSITION;
}

static void
si_pmu_spuravoid_pllupdate(si_t *sih, chipcregs_t *cc, u8 spuravoid)
{
	u32 tmp = 0;
	u8 phypll_offset = 0;
	u8 bcm5357_bcm43236_p1div[] = { 0x1, 0x5, 0x5 };
	u8 bcm5357_bcm43236_ndiv[] = { 0x30, 0xf6, 0xfc };

	switch (sih->chip) {
	case BCM5357_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43238_CHIP_ID:

		/*
		 * BCM5357 needs to touch PLL1_PLLCTL[02],
		 * so offset PLL0_PLLCTL[02] by 6
		 */
		phypll_offset = (sih->chip == BCM5357_CHIP_ID) ? 6 : 0;

		/* RMW only the P1 divider */
		W_REG(&cc->pllcontrol_addr,
		      PMU1_PLL0_PLLCTL0 + phypll_offset);
		tmp = R_REG(&cc->pllcontrol_data);
		tmp &= (~(PMU1_PLL0_PC0_P1DIV_MASK));
		tmp |=
		    (bcm5357_bcm43236_p1div[spuravoid] <<
		     PMU1_PLL0_PC0_P1DIV_SHIFT);
		W_REG(&cc->pllcontrol_data, tmp);

		/* RMW only the int feedback divider */
		W_REG(&cc->pllcontrol_addr,
		      PMU1_PLL0_PLLCTL2 + phypll_offset);
		tmp = R_REG(&cc->pllcontrol_data);
		tmp &= ~(PMU1_PLL0_PC2_NDIV_INT_MASK);
		tmp |=
		    (bcm5357_bcm43236_ndiv[spuravoid]) <<
		    PMU1_PLL0_PC2_NDIV_INT_SHIFT;
		W_REG(&cc->pllcontrol_data, tmp);

		tmp = 1 << 10;
		break;

	case BCM4331_CHIP_ID:
		if (spuravoid == 2) {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11500014);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x0FC00a08);
		} else if (spuravoid == 1) {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11500014);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x0F600a08);
		} else {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11100014);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x03000a08);
		}
		tmp = 1 << 10;
		break;

	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM6362_CHIP_ID:
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

	case BCM4716_CHIP_ID:
	case BCM4748_CHIP_ID:
	case BCM47162_CHIP_ID:
		if (spuravoid == 1) {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11500060);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
			W_REG(&cc->pllcontrol_data, 0x080C0C06);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x0F600000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
			W_REG(&cc->pllcontrol_data, 0x00000000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
			W_REG(&cc->pllcontrol_data, 0x2001E924);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
			W_REG(&cc->pllcontrol_data, 0x88888815);
		} else {
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
			W_REG(&cc->pllcontrol_data, 0x11100060);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
			W_REG(&cc->pllcontrol_data, 0x080c0c06);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x03000000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
			W_REG(&cc->pllcontrol_data, 0x00000000);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
			W_REG(&cc->pllcontrol_data, 0x200005c0);
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
			W_REG(&cc->pllcontrol_data, 0x88888815);
		}

		tmp = 3 << 9;
		break;

	case BCM4319_CHIP_ID:
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
		W_REG(&cc->pllcontrol_data, 0x11100070);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
		W_REG(&cc->pllcontrol_data, 0x1014140a);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		W_REG(&cc->pllcontrol_data, 0x88888854);

		if (spuravoid == 1) {
			/* spur_avoid ON, so enable 41/82/164Mhz clock mode */
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x05201828);
		} else {
			/* enable 40/80/160Mhz clock mode */
			W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
			W_REG(&cc->pllcontrol_data, 0x05001828);
		}
		break;
	case BCM4336_CHIP_ID:
		/* Looks like these are only for default xtal freq 26MHz */
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
		W_REG(&cc->pllcontrol_data, 0x02100020);

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
		W_REG(&cc->pllcontrol_data, 0x0C0C0C0C);

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
		W_REG(&cc->pllcontrol_data, 0x01240C0C);

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
		W_REG(&cc->pllcontrol_data, 0x202C2820);

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		W_REG(&cc->pllcontrol_data, 0x88888825);

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
		if (spuravoid == 1)
			W_REG(&cc->pllcontrol_data, 0x00EC4EC4);
		else
			W_REG(&cc->pllcontrol_data, 0x00762762);

		tmp = PCTL_PLL_PLLCTL_UPD;
		break;

	default:
		/* bail out */
		return;
	}

	tmp |= R_REG(&cc->pmucontrol);
	W_REG(&cc->pmucontrol, tmp);
}

/* select default xtal frequency for each chip */
static const pmu1_xtaltab0_t *si_pmu1_xtaldef0(si_t *sih)
{
	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		/* Default to 38400Khz */
		return &pmu1_xtaltab0_880_4329[PMU1_XTALTAB0_880_38400K];
	case BCM4319_CHIP_ID:
		/* Default to 30000Khz */
		return &pmu1_xtaltab0_1440[PMU1_XTALTAB0_1440_30000K];
	case BCM4336_CHIP_ID:
		/* Default to 26000Khz */
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_26000K];
	case BCM4330_CHIP_ID:
		/* Default to 37400Khz */
		if (CST4330_CHIPMODE_SDIOD(sih->chipst))
			return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_37400K];
		else
			return &pmu1_xtaltab0_1440[PMU1_XTALTAB0_1440_37400K];
	default:
		break;
	}
	return NULL;
}

/* select xtal table for each chip */
static const pmu1_xtaltab0_t *si_pmu1_xtaltab0(si_t *sih)
{
	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		return pmu1_xtaltab0_880_4329;
	case BCM4319_CHIP_ID:
		return pmu1_xtaltab0_1440;
	case BCM4336_CHIP_ID:
		return pmu1_xtaltab0_960;
	case BCM4330_CHIP_ID:
		if (CST4330_CHIPMODE_SDIOD(sih->chipst))
			return pmu1_xtaltab0_960;
		else
			return pmu1_xtaltab0_1440;
	default:
		break;
	}
	return NULL;
}

/* query alp/xtal clock frequency */
static u32
si_pmu1_alpclk0(si_t *sih, chipcregs_t *cc)
{
	const pmu1_xtaltab0_t *xt;
	u32 xf;

	/* Find the frequency in the table */
	xf = (R_REG(&cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	    PCTL_XTALFREQ_SHIFT;
	for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt++)
		if (xt->xf == xf)
			break;
	/* Could not find it so assign a default value */
	if (xt == NULL || xt->fref == 0)
		xt = si_pmu1_xtaldef0(sih);
	return xt->fref * 1000;
}

/* select default pll fvco for each chip */
static u32 si_pmu1_pllfvco0(si_t *sih)
{
	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		return FVCO_880;
	case BCM4319_CHIP_ID:
		return FVCO_1440;
	case BCM4336_CHIP_ID:
		return FVCO_960;
	case BCM4330_CHIP_ID:
		if (CST4330_CHIPMODE_SDIOD(sih->chipst))
			return FVCO_960;
		else
			return FVCO_1440;
	default:
		break;
	}
	return 0;
}

static void si_pmu_set_4330_plldivs(si_t *sih)
{
	u32 FVCO = si_pmu1_pllfvco0(sih) / 1000;
	u32 m1div, m2div, m3div, m4div, m5div, m6div;
	u32 pllc1, pllc2;

	m2div = m3div = m4div = m6div = FVCO / 80;
	m5div = FVCO / 160;

	if (CST4330_CHIPMODE_SDIOD(sih->chipst))
		m1div = FVCO / 80;
	else
		m1div = FVCO / 90;
	pllc1 =
	    (m1div << PMU1_PLL0_PC1_M1DIV_SHIFT) | (m2div <<
						    PMU1_PLL0_PC1_M2DIV_SHIFT) |
	    (m3div << PMU1_PLL0_PC1_M3DIV_SHIFT) | (m4div <<
						    PMU1_PLL0_PC1_M4DIV_SHIFT);
	si_pmu_pllcontrol(sih, PMU1_PLL0_PLLCTL1, ~0, pllc1);

	pllc2 = si_pmu_pllcontrol(sih, PMU1_PLL0_PLLCTL1, 0, 0);
	pllc2 &= ~(PMU1_PLL0_PC2_M5DIV_MASK | PMU1_PLL0_PC2_M6DIV_MASK);
	pllc2 |=
	    ((m5div << PMU1_PLL0_PC2_M5DIV_SHIFT) |
	     (m6div << PMU1_PLL0_PC2_M6DIV_SHIFT));
	si_pmu_pllcontrol(sih, PMU1_PLL0_PLLCTL2, ~0, pllc2);
}

/* Set up PLL registers in the PMU as per the crystal speed.
 * XtalFreq field in pmucontrol register being 0 indicates the PLL
 * is not programmed and the h/w default is assumed to work, in which
 * case the xtal frequency is unknown to the s/w so we need to call
 * si_pmu1_xtaldef0() wherever it is needed to return a default value.
 */
static void si_pmu1_pllinit0(si_t *sih, chipcregs_t *cc, u32 xtal)
{
	const pmu1_xtaltab0_t *xt;
	u32 tmp;
	u32 buf_strength = 0;
	u8 ndiv_mode = 1;

	/* Use h/w default PLL config */
	if (xtal == 0) {
		return;
	}

	/* Find the frequency in the table */
	for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt++)
		if (xt->fref == xtal)
			break;

	/* Check current PLL state, bail out if it has been programmed or
	 * we don't know how to program it.
	 */
	if (xt == NULL || xt->fref == 0) {
		return;
	}
	/* for 4319 bootloader already programs the PLL but bootloader does not
	 * program the PLL4 and PLL5. So Skip this check for 4319
	 */
	if ((((R_REG(&cc->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	      PCTL_XTALFREQ_SHIFT) == xt->xf) &&
	    !((sih->chip == BCM4319_CHIP_ID)
	      || (sih->chip == BCM4330_CHIP_ID)))
		return;

	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		/* Change the BBPLL drive strength to 8 for all channels */
		buf_strength = 0x888888;
		AND_REG(&cc->min_res_mask,
			~(PMURES_BIT(RES4329_BBPLL_PWRSW_PU) |
			  PMURES_BIT(RES4329_HT_AVAIL)));
		AND_REG(&cc->max_res_mask,
			~(PMURES_BIT(RES4329_BBPLL_PWRSW_PU) |
			  PMURES_BIT(RES4329_HT_AVAIL)));
		SPINWAIT(R_REG(&cc->clk_ctl_st) & CCS_HTAVAIL,
			 PMU_MAX_TRANSITION_DLY);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
		if (xt->fref == 38400)
			tmp = 0x200024C0;
		else if (xt->fref == 37400)
			tmp = 0x20004500;
		else if (xt->fref == 26000)
			tmp = 0x200024C0;
		else
			tmp = 0x200005C0;	/* Chip Dflt Settings */
		W_REG(&cc->pllcontrol_data, tmp);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		tmp =
		    R_REG(&cc->pllcontrol_data) & PMU1_PLL0_PC5_CLK_DRV_MASK;
		if ((xt->fref == 38400) || (xt->fref == 37400)
		    || (xt->fref == 26000))
			tmp |= 0x15;
		else
			tmp |= 0x25;	/* Chip Dflt Settings */
		W_REG(&cc->pllcontrol_data, tmp);
		break;

	case BCM4319_CHIP_ID:
		/* Change the BBPLL drive strength to 2 for all channels */
		buf_strength = 0x222222;

		/* Make sure the PLL is off */
		/* WAR65104: Disable the HT_AVAIL resource first and then
		 * after a delay (more than downtime for HT_AVAIL) remove the
		 * BBPLL resource; backplane clock moves to ALP from HT.
		 */
		AND_REG(&cc->min_res_mask,
			~(PMURES_BIT(RES4319_HT_AVAIL)));
		AND_REG(&cc->max_res_mask,
			~(PMURES_BIT(RES4319_HT_AVAIL)));

		udelay(100);
		AND_REG(&cc->min_res_mask,
			~(PMURES_BIT(RES4319_BBPLL_PWRSW_PU)));
		AND_REG(&cc->max_res_mask,
			~(PMURES_BIT(RES4319_BBPLL_PWRSW_PU)));

		udelay(100);
		SPINWAIT(R_REG(&cc->clk_ctl_st) & CCS_HTAVAIL,
			 PMU_MAX_TRANSITION_DLY);
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL4);
		tmp = 0x200005c0;
		W_REG(&cc->pllcontrol_data, tmp);
		break;

	case BCM4336_CHIP_ID:
		AND_REG(&cc->min_res_mask,
			~(PMURES_BIT(RES4336_HT_AVAIL) |
			  PMURES_BIT(RES4336_MACPHY_CLKAVAIL)));
		AND_REG(&cc->max_res_mask,
			~(PMURES_BIT(RES4336_HT_AVAIL) |
			  PMURES_BIT(RES4336_MACPHY_CLKAVAIL)));
		udelay(100);
		SPINWAIT(R_REG(&cc->clk_ctl_st) & CCS_HTAVAIL,
			 PMU_MAX_TRANSITION_DLY);
		break;

	case BCM4330_CHIP_ID:
		AND_REG(&cc->min_res_mask,
			~(PMURES_BIT(RES4330_HT_AVAIL) |
			  PMURES_BIT(RES4330_MACPHY_CLKAVAIL)));
		AND_REG(&cc->max_res_mask,
			~(PMURES_BIT(RES4330_HT_AVAIL) |
			  PMURES_BIT(RES4330_MACPHY_CLKAVAIL)));
		udelay(100);
		SPINWAIT(R_REG(&cc->clk_ctl_st) & CCS_HTAVAIL,
			 PMU_MAX_TRANSITION_DLY);
		break;

	default:
		break;
	}

	/* Write p1div and p2div to pllcontrol[0] */
	W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL0);
	tmp = R_REG(&cc->pllcontrol_data) &
	    ~(PMU1_PLL0_PC0_P1DIV_MASK | PMU1_PLL0_PC0_P2DIV_MASK);
	tmp |=
	    ((xt->
	      p1div << PMU1_PLL0_PC0_P1DIV_SHIFT) & PMU1_PLL0_PC0_P1DIV_MASK) |
	    ((xt->
	      p2div << PMU1_PLL0_PC0_P2DIV_SHIFT) & PMU1_PLL0_PC0_P2DIV_MASK);
	W_REG(&cc->pllcontrol_data, tmp);

	if ((sih->chip == BCM4330_CHIP_ID))
		si_pmu_set_4330_plldivs(sih);

	if ((sih->chip == BCM4329_CHIP_ID)
	    && (sih->chiprev == 0)) {

		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL1);
		tmp = R_REG(&cc->pllcontrol_data);
		tmp = tmp & (~DOT11MAC_880MHZ_CLK_DIVISOR_MASK);
		tmp = tmp | DOT11MAC_880MHZ_CLK_DIVISOR_VAL;
		W_REG(&cc->pllcontrol_data, tmp);
	}
	if ((sih->chip == BCM4319_CHIP_ID) ||
	    (sih->chip == BCM4336_CHIP_ID) ||
	    (sih->chip == BCM4330_CHIP_ID))
		ndiv_mode = PMU1_PLL0_PC2_NDIV_MODE_MFB;
	else
		ndiv_mode = PMU1_PLL0_PC2_NDIV_MODE_MASH;

	/* Write ndiv_int and ndiv_mode to pllcontrol[2] */
	W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL2);
	tmp = R_REG(&cc->pllcontrol_data) &
	    ~(PMU1_PLL0_PC2_NDIV_INT_MASK | PMU1_PLL0_PC2_NDIV_MODE_MASK);
	tmp |=
	    ((xt->
	      ndiv_int << PMU1_PLL0_PC2_NDIV_INT_SHIFT) &
	     PMU1_PLL0_PC2_NDIV_INT_MASK) | ((ndiv_mode <<
					      PMU1_PLL0_PC2_NDIV_MODE_SHIFT) &
					     PMU1_PLL0_PC2_NDIV_MODE_MASK);
	W_REG(&cc->pllcontrol_data, tmp);

	/* Write ndiv_frac to pllcontrol[3] */
	W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL3);
	tmp = R_REG(&cc->pllcontrol_data) & ~PMU1_PLL0_PC3_NDIV_FRAC_MASK;
	tmp |= ((xt->ndiv_frac << PMU1_PLL0_PC3_NDIV_FRAC_SHIFT) &
		PMU1_PLL0_PC3_NDIV_FRAC_MASK);
	W_REG(&cc->pllcontrol_data, tmp);

	/* Write clock driving strength to pllcontrol[5] */
	if (buf_strength) {
		W_REG(&cc->pllcontrol_addr, PMU1_PLL0_PLLCTL5);
		tmp =
		    R_REG(&cc->pllcontrol_data) & ~PMU1_PLL0_PC5_CLK_DRV_MASK;
		tmp |= (buf_strength << PMU1_PLL0_PC5_CLK_DRV_SHIFT);
		W_REG(&cc->pllcontrol_data, tmp);
	}

	/* to operate the 4319 usb in 24MHz/48MHz; chipcontrol[2][84:83] needs
	 * to be updated.
	 */
	if ((sih->chip == BCM4319_CHIP_ID)
	    && (xt->fref != XTAL_FREQ_30000MHZ)) {
		W_REG(&cc->chipcontrol_addr, PMU1_PLL0_CHIPCTL2);
		tmp =
		    R_REG(&cc->chipcontrol_data) & ~CCTL_4319USB_XTAL_SEL_MASK;
		if (xt->fref == XTAL_FREQ_24000MHZ) {
			tmp |=
			    (CCTL_4319USB_24MHZ_PLL_SEL <<
			     CCTL_4319USB_XTAL_SEL_SHIFT);
		} else if (xt->fref == XTAL_FREQ_48000MHZ) {
			tmp |=
			    (CCTL_4319USB_48MHZ_PLL_SEL <<
			     CCTL_4319USB_XTAL_SEL_SHIFT);
		}
		W_REG(&cc->chipcontrol_data, tmp);
	}

	/* Flush deferred pll control registers writes */
	if (sih->pmurev >= 2)
		OR_REG(&cc->pmucontrol, PCTL_PLL_PLLCTL_UPD);

	/* Write XtalFreq. Set the divisor also. */
	tmp = R_REG(&cc->pmucontrol) &
	    ~(PCTL_ILP_DIV_MASK | PCTL_XTALFREQ_MASK);
	tmp |= (((((xt->fref + 127) / 128) - 1) << PCTL_ILP_DIV_SHIFT) &
		PCTL_ILP_DIV_MASK) |
	    ((xt->xf << PCTL_XTALFREQ_SHIFT) & PCTL_XTALFREQ_MASK);

	if ((sih->chip == BCM4329_CHIP_ID)
	    && sih->chiprev == 0) {
		/* clear the htstretch before clearing HTReqEn */
		AND_REG(&cc->clkstretch, ~CSTRETCH_HT);
		tmp &= ~PCTL_HT_REQ_EN;
	}

	W_REG(&cc->pmucontrol, tmp);
}

u32 si_pmu_ilp_clock(si_t *sih)
{
	static u32 ilpcycles_per_sec;

	if (ISSIM_ENAB(sih) || !PMUCTL_ENAB(sih))
		return ILP_CLOCK;

	if (ilpcycles_per_sec == 0) {
		u32 start, end, delta;
		u32 origidx = ai_coreidx(sih);
		chipcregs_t *cc = ai_setcoreidx(sih, SI_CC_IDX);
		start = R_REG(&cc->pmutimer);
		mdelay(ILP_CALC_DUR);
		end = R_REG(&cc->pmutimer);
		delta = end - start;
		ilpcycles_per_sec = delta * (1000 / ILP_CALC_DUR);
		ai_setcoreidx(sih, origidx);
	}

	return ilpcycles_per_sec;
}

void si_pmu_set_ldo_voltage(si_t *sih, u8 ldo, u8 voltage)
{
	u8 sr_cntl_shift = 0, rc_shift = 0, shift = 0, mask = 0;
	u8 addr = 0;

	switch (sih->chip) {
	case BCM4336_CHIP_ID:
		switch (ldo) {
		case SET_LDO_VOLTAGE_CLDO_PWM:
			addr = 4;
			rc_shift = 1;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_CLDO_BURST:
			addr = 4;
			rc_shift = 5;
			mask = 0xf;
			break;
		case SET_LDO_VOLTAGE_LNLDO1:
			addr = 4;
			rc_shift = 17;
			mask = 0xf;
			break;
		default:
			return;
		}
		break;
	case BCM4330_CHIP_ID:
		switch (ldo) {
		case SET_LDO_VOLTAGE_CBUCK_PWM:
			addr = 3;
			rc_shift = 0;
			mask = 0x1f;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	shift = sr_cntl_shift + rc_shift;

	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, regcontrol_addr),
		   ~0, addr);
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, regcontrol_data),
		   mask << shift, (voltage & mask) << shift);
}

u16 si_pmu_fast_pwrup_delay(si_t *sih)
{
	uint delay = PMU_MAX_TRANSITION_DLY;
	chipcregs_t *cc;
	uint origidx;
#ifdef BCMDBG
	char chn[8];
	chn[0] = 0;		/* to suppress compile error */
#endif

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43238_CHIP_ID:
	case BCM4331_CHIP_ID:
	case BCM6362_CHIP_ID:
	case BCM4313_CHIP_ID:
		delay = ISSIM_ENAB(sih) ? 70 : 3700;
		break;
	case BCM4329_CHIP_ID:
		if (ISSIM_ENAB(sih))
			delay = 70;
		else {
			u32 ilp = si_pmu_ilp_clock(sih);
			delay =
			    (si_pmu_res_uptime(sih, cc, RES4329_HT_AVAIL) +
			     D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp -
							      1) / ilp);
			delay = (11 * delay) / 10;
		}
		break;
	case BCM4319_CHIP_ID:
		delay = ISSIM_ENAB(sih) ? 70 : 3700;
		break;
	case BCM4336_CHIP_ID:
		if (ISSIM_ENAB(sih))
			delay = 70;
		else {
			u32 ilp = si_pmu_ilp_clock(sih);
			delay =
			    (si_pmu_res_uptime(sih, cc, RES4336_HT_AVAIL) +
			     D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp -
							      1) / ilp);
			delay = (11 * delay) / 10;
		}
		break;
	case BCM4330_CHIP_ID:
		if (ISSIM_ENAB(sih))
			delay = 70;
		else {
			u32 ilp = si_pmu_ilp_clock(sih);
			delay =
			    (si_pmu_res_uptime(sih, cc, RES4330_HT_AVAIL) +
			     D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp -
							      1) / ilp);
			delay = (11 * delay) / 10;
		}
		break;
	default:
		break;
	}
	/* Return to original core */
	ai_setcoreidx(sih, origidx);

	return (u16) delay;
}

void si_pmu_sprom_enable(si_t *sih, bool enable)
{
	chipcregs_t *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* Read/write a chipcontrol reg */
u32 si_pmu_chipcontrol(si_t *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, chipcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, chipcontrol_data), mask, val);
}

/* Read/write a regcontrol reg */
u32 si_pmu_regcontrol(si_t *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, regcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, regcontrol_data), mask, val);
}

/* Read/write a pllcontrol reg */
u32 si_pmu_pllcontrol(si_t *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, pllcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, pllcontrol_data), mask, val);
}

/* PMU PLL update */
void si_pmu_pllupd(si_t *sih)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, pmucontrol),
		   PCTL_PLL_PLLCTL_UPD, PCTL_PLL_PLLCTL_UPD);
}

/* query alp/xtal clock frequency */
u32 si_pmu_alp_clock(si_t *sih)
{
	chipcregs_t *cc;
	uint origidx;
	u32 clock = ALP_CLOCK;

	/* bail out with default */
	if (!PMUCTL_ENAB(sih))
		return clock;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43238_CHIP_ID:
	case BCM4331_CHIP_ID:
	case BCM6362_CHIP_ID:
	case BCM4716_CHIP_ID:
	case BCM4748_CHIP_ID:
	case BCM47162_CHIP_ID:
	case BCM4313_CHIP_ID:
	case BCM5357_CHIP_ID:
		/* always 20Mhz */
		clock = 20000 * 1000;
		break;
	case BCM4329_CHIP_ID:
	case BCM4319_CHIP_ID:
	case BCM4336_CHIP_ID:
	case BCM4330_CHIP_ID:

		clock = si_pmu1_alpclk0(sih, cc);
		break;
	case BCM5356_CHIP_ID:
		/* always 25Mhz */
		clock = 25000 * 1000;
		break;
	default:
		break;
	}

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
	return clock;
}

void si_pmu_spuravoid(si_t *sih, u8 spuravoid)
{
	chipcregs_t *cc;
	uint origidx, intr_val;
	u32 tmp = 0;

	/* Remember original core before switch to chipc */
	cc = (chipcregs_t *) ai_switch_core(sih, CC_CORE_ID, &origidx,
					    &intr_val);

	/* force the HT off  */
	if (sih->chip == BCM4336_CHIP_ID) {
		tmp = R_REG(&cc->max_res_mask);
		tmp &= ~RES4336_HT_AVAIL;
		W_REG(&cc->max_res_mask, tmp);
		/* wait for the ht to really go away */
		SPINWAIT(((R_REG(&cc->clk_ctl_st) & CCS_HTAVAIL) == 0),
			 10000);
	}

	/* update the pll changes */
	si_pmu_spuravoid_pllupdate(sih, cc, spuravoid);

	/* enable HT back on  */
	if (sih->chip == BCM4336_CHIP_ID) {
		tmp = R_REG(&cc->max_res_mask);
		tmp |= RES4336_HT_AVAIL;
		W_REG(&cc->max_res_mask, tmp);
	}

	/* Return to original core */
	ai_restore_core(sih, origidx, intr_val);
}

/* initialize PMU */
void si_pmu_init(si_t *sih)
{
	chipcregs_t *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	if (sih->pmurev == 1)
		AND_REG(&cc->pmucontrol, ~PCTL_NOILP_ON_WAIT);
	else if (sih->pmurev >= 2)
		OR_REG(&cc->pmucontrol, PCTL_NOILP_ON_WAIT);

	if ((sih->chip == BCM4329_CHIP_ID) && (sih->chiprev == 2)) {
		/* Fix for 4329b0 bad LPOM state. */
		W_REG(&cc->regcontrol_addr, 2);
		OR_REG(&cc->regcontrol_data, 0x100);

		W_REG(&cc->regcontrol_addr, 3);
		OR_REG(&cc->regcontrol_data, 0x4);
	}

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* initialize PMU chip controls and other chip level stuff */
void si_pmu_chip_init(si_t *sih)
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
void si_pmu_swreg_init(si_t *sih)
{
	switch (sih->chip) {
	case BCM4336_CHIP_ID:
		/* Reduce CLDO PWM output voltage to 1.2V */
		si_pmu_set_ldo_voltage(sih, SET_LDO_VOLTAGE_CLDO_PWM, 0xe);
		/* Reduce CLDO BURST output voltage to 1.2V */
		si_pmu_set_ldo_voltage(sih, SET_LDO_VOLTAGE_CLDO_BURST,
				       0xe);
		/* Reduce LNLDO1 output voltage to 1.2V */
		si_pmu_set_ldo_voltage(sih, SET_LDO_VOLTAGE_LNLDO1, 0xe);
		if (sih->chiprev == 0)
			si_pmu_regcontrol(sih, 2, 0x400000, 0x400000);
		break;

	case BCM4330_CHIP_ID:
		/* CBUCK Voltage is 1.8 by default and set that to 1.5 */
		si_pmu_set_ldo_voltage(sih, SET_LDO_VOLTAGE_CBUCK_PWM, 0);
		break;
	default:
		break;
	}
}

/* initialize PLL */
void si_pmu_pll_init(si_t *sih, uint xtalfreq)
{
	chipcregs_t *cc;
	uint origidx;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		if (xtalfreq == 0)
			xtalfreq = 38400;
		si_pmu1_pllinit0(sih, cc, xtalfreq);
		break;
	case BCM4313_CHIP_ID:
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43238_CHIP_ID:
	case BCM4331_CHIP_ID:
	case BCM6362_CHIP_ID:
		/* ??? */
		break;
	case BCM4319_CHIP_ID:
	case BCM4336_CHIP_ID:
	case BCM4330_CHIP_ID:
		si_pmu1_pllinit0(sih, cc, xtalfreq);
		break;
	default:
		break;
	}

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}

/* initialize PMU resources */
void si_pmu_res_init(si_t *sih)
{
	chipcregs_t *cc;
	uint origidx;
	const pmu_res_updown_t *pmu_res_updown_table = NULL;
	uint pmu_res_updown_table_sz = 0;
	const pmu_res_depend_t *pmu_res_depend_table = NULL;
	uint pmu_res_depend_table_sz = 0;
	u32 min_mask = 0, max_mask = 0;
	char name[8], *val;
	uint i, rsrcs;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		/* Optimize resources up/down timers */
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = NULL;
			pmu_res_updown_table_sz = 0;
		} else {
			pmu_res_updown_table = bcm4329_res_updown;
			pmu_res_updown_table_sz =
				ARRAY_SIZE(bcm4329_res_updown);
		}
		/* Optimize resources dependencies */
		pmu_res_depend_table = bcm4329_res_depend;
		pmu_res_depend_table_sz = ARRAY_SIZE(bcm4329_res_depend);
		break;

	case BCM4319_CHIP_ID:
		/* Optimize resources up/down timers */
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = bcm4319a0_res_updown_qt;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4319a0_res_updown_qt);
		} else {
			pmu_res_updown_table = bcm4319a0_res_updown;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4319a0_res_updown);
		}
		/* Optimize resources dependancies masks */
		pmu_res_depend_table = bcm4319a0_res_depend;
		pmu_res_depend_table_sz = ARRAY_SIZE(bcm4319a0_res_depend);
		break;

	case BCM4336_CHIP_ID:
		/* Optimize resources up/down timers */
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = bcm4336a0_res_updown_qt;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4336a0_res_updown_qt);
		} else {
			pmu_res_updown_table = bcm4336a0_res_updown;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4336a0_res_updown);
		}
		/* Optimize resources dependancies masks */
		pmu_res_depend_table = bcm4336a0_res_depend;
		pmu_res_depend_table_sz = ARRAY_SIZE(bcm4336a0_res_depend);
		break;

	case BCM4330_CHIP_ID:
		/* Optimize resources up/down timers */
		if (ISSIM_ENAB(sih)) {
			pmu_res_updown_table = bcm4330a0_res_updown_qt;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4330a0_res_updown_qt);
		} else {
			pmu_res_updown_table = bcm4330a0_res_updown;
			pmu_res_updown_table_sz =
			    ARRAY_SIZE(bcm4330a0_res_updown);
		}
		/* Optimize resources dependancies masks */
		pmu_res_depend_table = bcm4330a0_res_depend;
		pmu_res_depend_table_sz = ARRAY_SIZE(bcm4330a0_res_depend);
		break;

	default:
		break;
	}

	/* # resources */
	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

	/* Program up/down timers */
	while (pmu_res_updown_table_sz--) {
		W_REG(&cc->res_table_sel,
		      pmu_res_updown_table[pmu_res_updown_table_sz].resnum);
		W_REG(&cc->res_updn_timer,
		      pmu_res_updown_table[pmu_res_updown_table_sz].updown);
	}
	/* Apply nvram overrides to up/down timers */
	for (i = 0; i < rsrcs; i++) {
		snprintf(name, sizeof(name), "r%dt", i);
		val = getvar(NULL, name);
		if (val == NULL)
			continue;
		W_REG(&cc->res_table_sel, (u32) i);
		W_REG(&cc->res_updn_timer,
		      (u32) simple_strtoul(val, NULL, 0));
	}

	/* Program resource dependencies table */
	while (pmu_res_depend_table_sz--) {
		if (pmu_res_depend_table[pmu_res_depend_table_sz].filter != NULL
		    && !(pmu_res_depend_table[pmu_res_depend_table_sz].
			 filter) (sih))
			continue;
		for (i = 0; i < rsrcs; i++) {
			if ((pmu_res_depend_table[pmu_res_depend_table_sz].
			     res_mask & PMURES_BIT(i)) == 0)
				continue;
			W_REG(&cc->res_table_sel, i);
			switch (pmu_res_depend_table[pmu_res_depend_table_sz].
				action) {
			case RES_DEPEND_SET:
				W_REG(&cc->res_dep_mask,
				      pmu_res_depend_table
				      [pmu_res_depend_table_sz].depend_mask);
				break;
			case RES_DEPEND_ADD:
				OR_REG(&cc->res_dep_mask,
				       pmu_res_depend_table
				       [pmu_res_depend_table_sz].depend_mask);
				break;
			case RES_DEPEND_REMOVE:
				AND_REG(&cc->res_dep_mask,
					~pmu_res_depend_table
					[pmu_res_depend_table_sz].depend_mask);
				break;
			default:
				break;
			}
		}
	}
	/* Apply nvram overrides to dependancies masks */
	for (i = 0; i < rsrcs; i++) {
		snprintf(name, sizeof(name), "r%dd", i);
		val = getvar(NULL, name);
		if (val == NULL)
			continue;
		W_REG(&cc->res_table_sel, (u32) i);
		W_REG(&cc->res_dep_mask,
		      (u32) simple_strtoul(val, NULL, 0));
	}

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

u32 si_pmu_measure_alpclk(si_t *sih)
{
	chipcregs_t *cc;
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

bool si_pmu_is_otp_powered(si_t *sih)
{
	uint idx;
	chipcregs_t *cc;
	bool st;

	/* Remember original core before switch to chipc */
	idx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		st = (R_REG(&cc->res_state) & PMURES_BIT(RES4329_OTP_PU))
		    != 0;
		break;
	case BCM4319_CHIP_ID:
		st = (R_REG(&cc->res_state) & PMURES_BIT(RES4319_OTP_PU))
		    != 0;
		break;
	case BCM4336_CHIP_ID:
		st = (R_REG(&cc->res_state) & PMURES_BIT(RES4336_OTP_PU))
		    != 0;
		break;
	case BCM4330_CHIP_ID:
		st = (R_REG(&cc->res_state) & PMURES_BIT(RES4330_OTP_PU))
		    != 0;
		break;

		/* These chip doesn't use PMU bit to power up/down OTP. OTP always on.
		 * Use OTP_INIT command to reset/refresh state.
		 */
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM43421_CHIP_ID:
	case BCM43236_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43238_CHIP_ID:
		st = true;
		break;
	default:
		st = true;
		break;
	}

	/* Return to original core */
	ai_setcoreidx(sih, idx);
	return st;
}

/* power up/down OTP through PMU resources */
void si_pmu_otp_power(si_t *sih, bool on)
{
	chipcregs_t *cc;
	uint origidx;
	u32 rsrcs = 0;	/* rsrcs to turn on/off OTP power */

	/* Don't do anything if OTP is disabled */
	if (ai_is_otp_disabled(sih))
		return;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM4329_CHIP_ID:
		rsrcs = PMURES_BIT(RES4329_OTP_PU);
		break;
	case BCM4319_CHIP_ID:
		rsrcs = PMURES_BIT(RES4319_OTP_PU);
		break;
	case BCM4336_CHIP_ID:
		rsrcs = PMURES_BIT(RES4336_OTP_PU);
		break;
	case BCM4330_CHIP_ID:
		rsrcs = PMURES_BIT(RES4330_OTP_PU);
		break;
	default:
		break;
	}

	if (rsrcs != 0) {
		u32 otps;

		/* Figure out the dependancies (exclude min_res_mask) */
		u32 deps = si_pmu_res_deps(sih, cc, rsrcs, true);
		u32 min_mask = 0, max_mask = 0;
		si_pmu_res_masks(sih, &min_mask, &max_mask);
		deps &= ~min_mask;
		/* Turn on/off the power */
		if (on) {
			OR_REG(&cc->min_res_mask, (rsrcs | deps));
			SPINWAIT(!(R_REG(&cc->res_state) & rsrcs),
				 PMU_MAX_TRANSITION_DLY);
		} else {
			AND_REG(&cc->min_res_mask, ~(rsrcs | deps));
		}

		SPINWAIT((((otps = R_REG(&cc->otpstatus)) & OTPS_READY) !=
			  (on ? OTPS_READY : 0)), 100);
	}

	/* Return to original core */
	ai_setcoreidx(sih, origidx);
}
