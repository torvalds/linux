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

/* Fields in clkstretch */
#define CSTRETCH_HT		0xffff0000
#define CSTRETCH_ALP		0x0000ffff

/* d11 slow to fast clock transition time in slow clock cycles */
#define D11SCC_SLOW2FAST_TRANSITION	2

/* ILP clock */
#define	ILP_CLOCK		32000

/* ALP clock on pre-PMU chips */
#define	ALP_CLOCK		20000000

/* HT clock */
#define	HT_CLOCK		80000000

#define OTPS_READY		0x00001000

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

/* PMU Resource Request Timer registers */
/* This is based on PmuRev0 */
#define	PRRT_TIME_MASK	0x03ff
#define	PRRT_INTEN	0x0400
#define	PRRT_REQ_ACTIVE	0x0800
#define	PRRT_ALP_REQ	0x1000
#define	PRRT_HT_REQ	0x2000

/* PMU resource bit position */
#define PMURES_BIT(bit)	(1 << (bit))

/* PMU resource number limit */
#define PMURES_MAX_RESNUM	30

/* PMU chip control0 register */
#define	PMU_CHIPCTL0		0

/* PMU chip control1 register */
#define	PMU_CHIPCTL1			1
#define	PMU_CC1_RXC_DLL_BYPASS		0x00010000

#define PMU_CC1_IF_TYPE_MASK   		0x00000030
#define PMU_CC1_IF_TYPE_RMII    	0x00000000
#define PMU_CC1_IF_TYPE_MII     	0x00000010
#define PMU_CC1_IF_TYPE_RGMII   	0x00000020

#define PMU_CC1_SW_TYPE_MASK    	0x000000c0
#define PMU_CC1_SW_TYPE_EPHY    	0x00000000
#define PMU_CC1_SW_TYPE_EPHYMII 	0x00000040
#define PMU_CC1_SW_TYPE_EPHYRMII	0x00000080
#define PMU_CC1_SW_TYPE_RGMII   	0x000000c0

/* PMU corerev and chip specific PLL controls.
 * PMU<rev>_PLL<num>_XX where <rev> is PMU corerev and <num> is an arbitrary number
 * to differentiate different PLLs controlled by the same PMU rev.
 */
/* pllcontrol registers */
/* PDIV, div_phy, div_arm, div_adc, dith_sel, ioff, kpd_scale, lsb_sel, mash_sel, lf_c & lf_r */
#define	PMU0_PLL0_PLLCTL0		0
#define	PMU0_PLL0_PC0_PDIV_MASK		1
#define	PMU0_PLL0_PC0_PDIV_FREQ		25000
#define PMU0_PLL0_PC0_DIV_ARM_MASK	0x00000038
#define PMU0_PLL0_PC0_DIV_ARM_SHIFT	3
#define PMU0_PLL0_PC0_DIV_ARM_BASE	8

/* PC0_DIV_ARM for PLLOUT_ARM */
#define PMU0_PLL0_PC0_DIV_ARM_110MHZ	0
#define PMU0_PLL0_PC0_DIV_ARM_97_7MHZ	1
#define PMU0_PLL0_PC0_DIV_ARM_88MHZ	2
#define PMU0_PLL0_PC0_DIV_ARM_80MHZ	3	/* Default */
#define PMU0_PLL0_PC0_DIV_ARM_73_3MHZ	4
#define PMU0_PLL0_PC0_DIV_ARM_67_7MHZ	5
#define PMU0_PLL0_PC0_DIV_ARM_62_9MHZ	6
#define PMU0_PLL0_PC0_DIV_ARM_58_6MHZ	7

/* Wildcard base, stop_mod, en_lf_tp, en_cal & lf_r2 */
#define	PMU0_PLL0_PLLCTL1		1
#define	PMU0_PLL0_PC1_WILD_INT_MASK	0xf0000000
#define	PMU0_PLL0_PC1_WILD_INT_SHIFT	28
#define	PMU0_PLL0_PC1_WILD_FRAC_MASK	0x0fffff00
#define	PMU0_PLL0_PC1_WILD_FRAC_SHIFT	8
#define	PMU0_PLL0_PC1_STOP_MOD		0x00000040

/* Wildcard base, vco_calvar, vco_swc, vco_var_selref, vso_ical & vco_sel_avdd */
#define	PMU0_PLL0_PLLCTL2		2
#define	PMU0_PLL0_PC2_WILD_INT_MASK	0xf
#define	PMU0_PLL0_PC2_WILD_INT_SHIFT	4

/* pllcontrol registers */
/* ndiv_pwrdn, pwrdn_ch<x>, refcomp_pwrdn, dly_ch<x>, p1div, p2div, _bypass_sdmod */
#define PMU1_PLL0_PLLCTL0		0
#define PMU1_PLL0_PC0_P1DIV_MASK	0x00f00000
#define PMU1_PLL0_PC0_P1DIV_SHIFT	20
#define PMU1_PLL0_PC0_P2DIV_MASK	0x0f000000
#define PMU1_PLL0_PC0_P2DIV_SHIFT	24

/* m<x>div */
#define PMU1_PLL0_PLLCTL1		1
#define PMU1_PLL0_PC1_M1DIV_MASK	0x000000ff
#define PMU1_PLL0_PC1_M1DIV_SHIFT	0
#define PMU1_PLL0_PC1_M2DIV_MASK	0x0000ff00
#define PMU1_PLL0_PC1_M2DIV_SHIFT	8
#define PMU1_PLL0_PC1_M3DIV_MASK	0x00ff0000
#define PMU1_PLL0_PC1_M3DIV_SHIFT	16
#define PMU1_PLL0_PC1_M4DIV_MASK	0xff000000
#define PMU1_PLL0_PC1_M4DIV_SHIFT	24

#define PMU1_PLL0_CHIPCTL0		0
#define PMU1_PLL0_CHIPCTL1		1
#define PMU1_PLL0_CHIPCTL2		2

#define DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT 8
#define DOT11MAC_880MHZ_CLK_DIVISOR_MASK (0xFF << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)
#define DOT11MAC_880MHZ_CLK_DIVISOR_VAL  (0xE << DOT11MAC_880MHZ_CLK_DIVISOR_SHIFT)

/* m<x>div, ndiv_dither_mfb, ndiv_mode, ndiv_int */
#define PMU1_PLL0_PLLCTL2		2
#define PMU1_PLL0_PC2_M5DIV_MASK	0x000000ff
#define PMU1_PLL0_PC2_M5DIV_SHIFT	0
#define PMU1_PLL0_PC2_M6DIV_MASK	0x0000ff00
#define PMU1_PLL0_PC2_M6DIV_SHIFT	8
#define PMU1_PLL0_PC2_NDIV_MODE_MASK	0x000e0000
#define PMU1_PLL0_PC2_NDIV_MODE_SHIFT	17
#define PMU1_PLL0_PC2_NDIV_MODE_MASH	1
#define PMU1_PLL0_PC2_NDIV_MODE_MFB	2	/* recommended for 4319 */
#define PMU1_PLL0_PC2_NDIV_INT_MASK	0x1ff00000
#define PMU1_PLL0_PC2_NDIV_INT_SHIFT	20

/* ndiv_frac */
#define PMU1_PLL0_PLLCTL3		3
#define PMU1_PLL0_PC3_NDIV_FRAC_MASK	0x00ffffff
#define PMU1_PLL0_PC3_NDIV_FRAC_SHIFT	0

/* pll_ctrl */
#define PMU1_PLL0_PLLCTL4		4

/* pll_ctrl, vco_rng, clkdrive_ch<x> */
#define PMU1_PLL0_PLLCTL5		5
#define PMU1_PLL0_PC5_CLK_DRV_MASK 0xffffff00
#define PMU1_PLL0_PC5_CLK_DRV_SHIFT 8

/* PMU rev 2 control words */
#define PMU2_PHY_PLL_PLLCTL		4
#define PMU2_SI_PLL_PLLCTL		10

/* PMU rev 2 */
/* pllcontrol registers */
/* ndiv_pwrdn, pwrdn_ch<x>, refcomp_pwrdn, dly_ch<x>, p1div, p2div, _bypass_sdmod */
#define PMU2_PLL_PLLCTL0		0
#define PMU2_PLL_PC0_P1DIV_MASK 	0x00f00000
#define PMU2_PLL_PC0_P1DIV_SHIFT	20
#define PMU2_PLL_PC0_P2DIV_MASK 	0x0f000000
#define PMU2_PLL_PC0_P2DIV_SHIFT	24

/* m<x>div */
#define PMU2_PLL_PLLCTL1		1
#define PMU2_PLL_PC1_M1DIV_MASK 	0x000000ff
#define PMU2_PLL_PC1_M1DIV_SHIFT	0
#define PMU2_PLL_PC1_M2DIV_MASK 	0x0000ff00
#define PMU2_PLL_PC1_M2DIV_SHIFT	8
#define PMU2_PLL_PC1_M3DIV_MASK 	0x00ff0000
#define PMU2_PLL_PC1_M3DIV_SHIFT	16
#define PMU2_PLL_PC1_M4DIV_MASK 	0xff000000
#define PMU2_PLL_PC1_M4DIV_SHIFT	24

/* m<x>div, ndiv_dither_mfb, ndiv_mode, ndiv_int */
#define PMU2_PLL_PLLCTL2		2
#define PMU2_PLL_PC2_M5DIV_MASK 	0x000000ff
#define PMU2_PLL_PC2_M5DIV_SHIFT	0
#define PMU2_PLL_PC2_M6DIV_MASK 	0x0000ff00
#define PMU2_PLL_PC2_M6DIV_SHIFT	8
#define PMU2_PLL_PC2_NDIV_MODE_MASK	0x000e0000
#define PMU2_PLL_PC2_NDIV_MODE_SHIFT	17
#define PMU2_PLL_PC2_NDIV_INT_MASK	0x1ff00000
#define PMU2_PLL_PC2_NDIV_INT_SHIFT	20

/* ndiv_frac */
#define PMU2_PLL_PLLCTL3		3
#define PMU2_PLL_PC3_NDIV_FRAC_MASK	0x00ffffff
#define PMU2_PLL_PC3_NDIV_FRAC_SHIFT	0

/* pll_ctrl */
#define PMU2_PLL_PLLCTL4		4

/* pll_ctrl, vco_rng, clkdrive_ch<x> */
#define PMU2_PLL_PLLCTL5		5
#define PMU2_PLL_PC5_CLKDRIVE_CH1_MASK	0x00000f00
#define PMU2_PLL_PC5_CLKDRIVE_CH1_SHIFT	8
#define PMU2_PLL_PC5_CLKDRIVE_CH2_MASK	0x0000f000
#define PMU2_PLL_PC5_CLKDRIVE_CH2_SHIFT	12
#define PMU2_PLL_PC5_CLKDRIVE_CH3_MASK	0x000f0000
#define PMU2_PLL_PC5_CLKDRIVE_CH3_SHIFT	16
#define PMU2_PLL_PC5_CLKDRIVE_CH4_MASK	0x00f00000
#define PMU2_PLL_PC5_CLKDRIVE_CH4_SHIFT	20
#define PMU2_PLL_PC5_CLKDRIVE_CH5_MASK	0x0f000000
#define PMU2_PLL_PC5_CLKDRIVE_CH5_SHIFT	24
#define PMU2_PLL_PC5_CLKDRIVE_CH6_MASK	0xf0000000
#define PMU2_PLL_PC5_CLKDRIVE_CH6_SHIFT	28

/* PMU rev 5 (& 6) */
#define	PMU5_PLL_P1P2_OFF		0
#define	PMU5_PLL_P1_MASK		0x0f000000
#define	PMU5_PLL_P1_SHIFT		24
#define	PMU5_PLL_P2_MASK		0x00f00000
#define	PMU5_PLL_P2_SHIFT		20
#define	PMU5_PLL_M14_OFF		1
#define	PMU5_PLL_MDIV_MASK		0x000000ff
#define	PMU5_PLL_MDIV_WIDTH		8
#define	PMU5_PLL_NM5_OFF		2
#define	PMU5_PLL_NDIV_MASK		0xfff00000
#define	PMU5_PLL_NDIV_SHIFT		20
#define	PMU5_PLL_NDIV_MODE_MASK		0x000e0000
#define	PMU5_PLL_NDIV_MODE_SHIFT	17
#define	PMU5_PLL_FMAB_OFF		3
#define	PMU5_PLL_MRAT_MASK		0xf0000000
#define	PMU5_PLL_MRAT_SHIFT		28
#define	PMU5_PLL_ABRAT_MASK		0x08000000
#define	PMU5_PLL_ABRAT_SHIFT		27
#define	PMU5_PLL_FDIV_MASK		0x07ffffff
#define	PMU5_PLL_PLLCTL_OFF		4
#define	PMU5_PLL_PCHI_OFF		5
#define	PMU5_PLL_PCHI_MASK		0x0000003f

/* pmu XtalFreqRatio */
#define	PMU_XTALFREQ_REG_ILPCTR_MASK	0x00001FFF
#define	PMU_XTALFREQ_REG_MEASURE_MASK	0x80000000
#define	PMU_XTALFREQ_REG_MEASURE_SHIFT	31

/* Divider allocation in 4716/47162/5356/5357 */
#define	PMU5_MAINPLL_CPU		1
#define	PMU5_MAINPLL_MEM		2
#define	PMU5_MAINPLL_SI			3

#define PMU7_PLL_PLLCTL7                7
#define PMU7_PLL_PLLCTL8                8
#define PMU7_PLL_PLLCTL11		11

/* PLL usage in 4716/47162 */
#define	PMU4716_MAINPLL_PLL0		12

/* PLL usage in 5356/5357 */
#define	PMU5356_MAINPLL_PLL0		0
#define	PMU5357_MAINPLL_PLL0		0

/* 4328 resources */
#define RES4328_EXT_SWITCHER_PWM	0	/* 0x00001 */
#define RES4328_BB_SWITCHER_PWM		1	/* 0x00002 */
#define RES4328_BB_SWITCHER_BURST	2	/* 0x00004 */
#define RES4328_BB_EXT_SWITCHER_BURST	3	/* 0x00008 */
#define RES4328_ILP_REQUEST		4	/* 0x00010 */
#define RES4328_RADIO_SWITCHER_PWM	5	/* 0x00020 */
#define RES4328_RADIO_SWITCHER_BURST	6	/* 0x00040 */
#define RES4328_ROM_SWITCH		7	/* 0x00080 */
#define RES4328_PA_REF_LDO		8	/* 0x00100 */
#define RES4328_RADIO_LDO		9	/* 0x00200 */
#define RES4328_AFE_LDO			10	/* 0x00400 */
#define RES4328_PLL_LDO			11	/* 0x00800 */
#define RES4328_BG_FILTBYP		12	/* 0x01000 */
#define RES4328_TX_FILTBYP		13	/* 0x02000 */
#define RES4328_RX_FILTBYP		14	/* 0x04000 */
#define RES4328_XTAL_PU			15	/* 0x08000 */
#define RES4328_XTAL_EN			16	/* 0x10000 */
#define RES4328_BB_PLL_FILTBYP		17	/* 0x20000 */
#define RES4328_RF_PLL_FILTBYP		18	/* 0x40000 */
#define RES4328_BB_PLL_PU		19	/* 0x80000 */

/* 4325 A0/A1 resources */
#define RES4325_BUCK_BOOST_BURST	0	/* 0x00000001 */
#define RES4325_CBUCK_BURST		1	/* 0x00000002 */
#define RES4325_CBUCK_PWM		2	/* 0x00000004 */
#define RES4325_CLDO_CBUCK_BURST	3	/* 0x00000008 */
#define RES4325_CLDO_CBUCK_PWM		4	/* 0x00000010 */
#define RES4325_BUCK_BOOST_PWM		5	/* 0x00000020 */
#define RES4325_ILP_REQUEST		6	/* 0x00000040 */
#define RES4325_ABUCK_BURST		7	/* 0x00000080 */
#define RES4325_ABUCK_PWM		8	/* 0x00000100 */
#define RES4325_LNLDO1_PU		9	/* 0x00000200 */
#define RES4325_OTP_PU			10	/* 0x00000400 */
#define RES4325_LNLDO3_PU		11	/* 0x00000800 */
#define RES4325_LNLDO4_PU		12	/* 0x00001000 */
#define RES4325_XTAL_PU			13	/* 0x00002000 */
#define RES4325_ALP_AVAIL		14	/* 0x00004000 */
#define RES4325_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4325_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4325_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4325_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4325_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4325_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4325_HT_AVAIL		21	/* 0x00200000 */

/* 4325 B0/C0 resources */
#define RES4325B0_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4325B0_CBUCK_BURST		2	/* 0x00000004 */
#define RES4325B0_CBUCK_PWM		3	/* 0x00000008 */
#define RES4325B0_CLDO_PU		4	/* 0x00000010 */

/* 4325 C1 resources */
#define RES4325C1_LNLDO2_PU		12	/* 0x00001000 */

#define RES4329_RESERVED0		0	/* 0x00000001 */
#define RES4329_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4329_CBUCK_BURST		2	/* 0x00000004 */
#define RES4329_CBUCK_PWM		3	/* 0x00000008 */
#define RES4329_CLDO_PU			4	/* 0x00000010 */
#define RES4329_PALDO_PU		5	/* 0x00000020 */
#define RES4329_ILP_REQUEST		6	/* 0x00000040 */
#define RES4329_RESERVED7		7	/* 0x00000080 */
#define RES4329_RESERVED8		8	/* 0x00000100 */
#define RES4329_LNLDO1_PU		9	/* 0x00000200 */
#define RES4329_OTP_PU			10	/* 0x00000400 */
#define RES4329_RESERVED11		11	/* 0x00000800 */
#define RES4329_LNLDO2_PU		12	/* 0x00001000 */
#define RES4329_XTAL_PU			13	/* 0x00002000 */
#define RES4329_ALP_AVAIL		14	/* 0x00004000 */
#define RES4329_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4329_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4329_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4329_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4329_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4329_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4329_HT_AVAIL		21	/* 0x00200000 */

/* 4315 resources */
#define RES4315_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4315_CBUCK_BURST		2	/* 0x00000004 */
#define RES4315_CBUCK_PWM		3	/* 0x00000008 */
#define RES4315_CLDO_PU			4	/* 0x00000010 */
#define RES4315_PALDO_PU		5	/* 0x00000020 */
#define RES4315_ILP_REQUEST		6	/* 0x00000040 */
#define RES4315_LNLDO1_PU		9	/* 0x00000200 */
#define RES4315_OTP_PU			10	/* 0x00000400 */
#define RES4315_LNLDO2_PU		12	/* 0x00001000 */
#define RES4315_XTAL_PU			13	/* 0x00002000 */
#define RES4315_ALP_AVAIL		14	/* 0x00004000 */
#define RES4315_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4315_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4315_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4315_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4315_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4315_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4315_HT_AVAIL		21	/* 0x00200000 */

/* 4319 resources */
#define RES4319_CBUCK_LPOM		1	/* 0x00000002 */
#define RES4319_CBUCK_BURST		2	/* 0x00000004 */
#define RES4319_CBUCK_PWM		3	/* 0x00000008 */
#define RES4319_CLDO_PU			4	/* 0x00000010 */
#define RES4319_PALDO_PU		5	/* 0x00000020 */
#define RES4319_ILP_REQUEST		6	/* 0x00000040 */
#define RES4319_LNLDO1_PU		9	/* 0x00000200 */
#define RES4319_OTP_PU			10	/* 0x00000400 */
#define RES4319_LNLDO2_PU		12	/* 0x00001000 */
#define RES4319_XTAL_PU			13	/* 0x00002000 */
#define RES4319_ALP_AVAIL		14	/* 0x00004000 */
#define RES4319_RX_PWRSW_PU		15	/* 0x00008000 */
#define RES4319_TX_PWRSW_PU		16	/* 0x00010000 */
#define RES4319_RFPLL_PWRSW_PU		17	/* 0x00020000 */
#define RES4319_LOGEN_PWRSW_PU		18	/* 0x00040000 */
#define RES4319_AFE_PWRSW_PU		19	/* 0x00080000 */
#define RES4319_BBPLL_PWRSW_PU		20	/* 0x00100000 */
#define RES4319_HT_AVAIL		21	/* 0x00200000 */

#define CCTL_4319USB_XTAL_SEL_MASK	0x00180000
#define CCTL_4319USB_XTAL_SEL_SHIFT	19
#define CCTL_4319USB_48MHZ_PLL_SEL	1
#define CCTL_4319USB_24MHZ_PLL_SEL	2

/* PMU resources for 4336 */
#define	RES4336_CBUCK_LPOM		0
#define	RES4336_CBUCK_BURST		1
#define	RES4336_CBUCK_LP_PWM		2
#define	RES4336_CBUCK_PWM		3
#define	RES4336_CLDO_PU			4
#define	RES4336_DIS_INT_RESET_PD	5
#define	RES4336_ILP_REQUEST		6
#define	RES4336_LNLDO_PU		7
#define	RES4336_LDO3P3_PU		8
#define	RES4336_OTP_PU			9
#define	RES4336_XTAL_PU			10
#define	RES4336_ALP_AVAIL		11
#define	RES4336_RADIO_PU		12
#define	RES4336_BG_PU			13
#define	RES4336_VREG1p4_PU_PU		14
#define	RES4336_AFE_PWRSW_PU		15
#define	RES4336_RX_PWRSW_PU		16
#define	RES4336_TX_PWRSW_PU		17
#define	RES4336_BB_PWRSW_PU		18
#define	RES4336_SYNTH_PWRSW_PU		19
#define	RES4336_MISC_PWRSW_PU		20
#define	RES4336_LOGEN_PWRSW_PU		21
#define	RES4336_BBPLL_PWRSW_PU		22
#define	RES4336_MACPHY_CLKAVAIL		23
#define	RES4336_HT_AVAIL		24
#define	RES4336_RSVD			25

/* 4330 resources */
#define	RES4330_CBUCK_LPOM		0
#define	RES4330_CBUCK_BURST		1
#define	RES4330_CBUCK_LP_PWM		2
#define	RES4330_CBUCK_PWM		3
#define	RES4330_CLDO_PU			4
#define	RES4330_DIS_INT_RESET_PD	5
#define	RES4330_ILP_REQUEST		6
#define	RES4330_LNLDO_PU		7
#define	RES4330_LDO3P3_PU		8
#define	RES4330_OTP_PU			9
#define	RES4330_XTAL_PU			10
#define	RES4330_ALP_AVAIL		11
#define	RES4330_RADIO_PU		12
#define	RES4330_BG_PU			13
#define	RES4330_VREG1p4_PU_PU		14
#define	RES4330_AFE_PWRSW_PU		15
#define	RES4330_RX_PWRSW_PU		16
#define	RES4330_TX_PWRSW_PU		17
#define	RES4330_BB_PWRSW_PU		18
#define	RES4330_SYNTH_PWRSW_PU		19
#define	RES4330_MISC_PWRSW_PU		20
#define	RES4330_LOGEN_PWRSW_PU		21
#define	RES4330_BBPLL_PWRSW_PU		22
#define	RES4330_MACPHY_CLKAVAIL		23
#define	RES4330_HT_AVAIL		24
#define	RES4330_5gRX_PWRSW_PU		25
#define	RES4330_5gTX_PWRSW_PU		26
#define	RES4330_5g_LOGEN_PWRSW_PU	27

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

/* PMU resource up transition time in ILP cycles */
#define PMURES_UP_TRANSITION	2

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
si_pmu_spuravoid_pllupdate(struct si_pub *sih, chipcregs_t *cc, u8 spuravoid)
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

u32 si_pmu_ilp_clock(struct si_pub *sih)
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

u16 si_pmu_fast_pwrup_delay(struct si_pub *sih)
{
	uint delay = PMU_MAX_TRANSITION_DLY;

	/* Remember original core before switch to chipc */
	origidx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	case BCM4313_CHIP_ID:
		delay = ISSIM_ENAB(sih) ? 70 : 3700;
		break;
	default:
		break;
	}
	/* Return to original core */
	ai_setcoreidx(sih, origidx);

	return (u16) delay;
}

void si_pmu_sprom_enable(struct si_pub *sih, bool enable)
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
u32 si_pmu_chipcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, chipcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, chipcontrol_data), mask, val);
}

/* Read/write a regcontrol reg */
u32 si_pmu_regcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, regcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, regcontrol_data), mask, val);
}

/* Read/write a pllcontrol reg */
u32 si_pmu_pllcontrol(struct si_pub *sih, uint reg, u32 mask, u32 val)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, pllcontrol_addr), ~0,
		   reg);
	return ai_corereg(sih, SI_CC_IDX,
			  offsetof(chipcregs_t, pllcontrol_data), mask, val);
}

/* PMU PLL update */
void si_pmu_pllupd(struct si_pub *sih)
{
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, pmucontrol),
		   PCTL_PLL_PLLCTL_UPD, PCTL_PLL_PLLCTL_UPD);
}

/* query alp/xtal clock frequency */
u32 si_pmu_alp_clock(struct si_pub *sih)
{
	u32 clock = ALP_CLOCK;

	/* bail out with default */
	if (!PMUCTL_ENAB(sih))
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
	chipcregs_t *cc;
	uint origidx, intr_val;

	/* Remember original core before switch to chipc */
	cc = (chipcregs_t *) ai_switch_core(sih, CC_CORE_ID, &origidx,
					    &intr_val);

	/* update the pll changes */
	si_pmu_spuravoid_pllupdate(sih, cc, spuravoid);

	/* Return to original core */
	ai_restore_core(sih, origidx, intr_val);
}

/* initialize PMU */
void si_pmu_init(struct si_pub *sih)
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
	chipcregs_t *cc;
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
	chipcregs_t *cc;
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
