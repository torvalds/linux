/*
 *  linux/arch/arm/mach-omap2/clock24xx.h
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK24XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK24XX_H

#include "clock.h"

#include "prm.h"
#include "cm.h"
#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"

static void omap2_table_mpu_recalc(struct clk *clk);
static int omap2_select_table_rate(struct clk *clk, unsigned long rate);
static long omap2_round_to_table_rate(struct clk *clk, unsigned long rate);
static void omap2_sys_clk_recalc(struct clk *clk);
static void omap2_osc_clk_recalc(struct clk *clk);
static void omap2_sys_clk_recalc(struct clk *clk);
static void omap2_dpllcore_recalc(struct clk *clk);
static int omap2_clk_fixed_enable(struct clk *clk);
static void omap2_clk_fixed_disable(struct clk *clk);
static int omap2_enable_osc_ck(struct clk *clk);
static void omap2_disable_osc_ck(struct clk *clk);
static int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate);

/* Key dividers which make up a PRCM set. Ratio's for a PRCM are mandated.
 * xtal_speed, dpll_speed, mpu_speed, CM_CLKSEL_MPU,CM_CLKSEL_DSP
 * CM_CLKSEL_GFX, CM_CLKSEL1_CORE, CM_CLKSEL1_PLL CM_CLKSEL2_PLL, CM_CLKSEL_MDM
 */
struct prcm_config {
	unsigned long xtal_speed;	/* crystal rate */
	unsigned long dpll_speed;	/* dpll: out*xtal*M/(N-1)table_recalc */
	unsigned long mpu_speed;	/* speed of MPU */
	unsigned long cm_clksel_mpu;	/* mpu divider */
	unsigned long cm_clksel_dsp;	/* dsp+iva1 div(2420), iva2.1(2430) */
	unsigned long cm_clksel_gfx;	/* gfx dividers */
	unsigned long cm_clksel1_core;	/* major subsystem dividers */
	unsigned long cm_clksel1_pll;	/* m,n */
	unsigned long cm_clksel2_pll;	/* dpllx1 or x2 out */
	unsigned long cm_clksel_mdm;	/* modem dividers 2430 only */
	unsigned long base_sdrc_rfr;	/* base refresh timing for a set */
	unsigned char flags;
};

/*
 * The OMAP2 processor can be run at several discrete 'PRCM configurations'.
 * These configurations are characterized by voltage and speed for clocks.
 * The device is only validated for certain combinations. One way to express
 * these combinations is via the 'ratio's' which the clocks operate with
 * respect to each other. These ratio sets are for a given voltage/DPLL
 * setting. All configurations can be described by a DPLL setting and a ratio
 * There are 3 ratio sets for the 2430 and X ratio sets for 2420.
 *
 * 2430 differs from 2420 in that there are no more phase synchronizers used.
 * They both have a slightly different clock domain setup. 2420(iva1,dsp) vs
 * 2430 (iva2.1, NOdsp, mdm)
 */

/* Core fields for cm_clksel, not ratio governed */
#define RX_CLKSEL_DSS1			(0x10 << 8)
#define RX_CLKSEL_DSS2			(0x0 << 13)
#define RX_CLKSEL_SSI			(0x5 << 20)

/*-------------------------------------------------------------------------
 * Voltage/DPLL ratios
 *-------------------------------------------------------------------------*/

/* 2430 Ratio's, 2430-Ratio Config 1 */
#define R1_CLKSEL_L3			(4 << 0)
#define R1_CLKSEL_L4			(2 << 5)
#define R1_CLKSEL_USB			(4 << 25)
#define R1_CM_CLKSEL1_CORE_VAL		R1_CLKSEL_USB | RX_CLKSEL_SSI | \
					RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					R1_CLKSEL_L4 | R1_CLKSEL_L3
#define R1_CLKSEL_MPU			(2 << 0)
#define R1_CM_CLKSEL_MPU_VAL		R1_CLKSEL_MPU
#define R1_CLKSEL_DSP			(2 << 0)
#define R1_CLKSEL_DSP_IF		(2 << 5)
#define R1_CM_CLKSEL_DSP_VAL		R1_CLKSEL_DSP | R1_CLKSEL_DSP_IF
#define R1_CLKSEL_GFX			(2 << 0)
#define R1_CM_CLKSEL_GFX_VAL		R1_CLKSEL_GFX
#define R1_CLKSEL_MDM			(4 << 0)
#define R1_CM_CLKSEL_MDM_VAL		R1_CLKSEL_MDM

/* 2430-Ratio Config 2 */
#define R2_CLKSEL_L3			(6 << 0)
#define R2_CLKSEL_L4			(2 << 5)
#define R2_CLKSEL_USB			(2 << 25)
#define R2_CM_CLKSEL1_CORE_VAL		R2_CLKSEL_USB | RX_CLKSEL_SSI | \
					RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					R2_CLKSEL_L4 | R2_CLKSEL_L3
#define R2_CLKSEL_MPU			(2 << 0)
#define R2_CM_CLKSEL_MPU_VAL		R2_CLKSEL_MPU
#define R2_CLKSEL_DSP			(2 << 0)
#define R2_CLKSEL_DSP_IF		(3 << 5)
#define R2_CM_CLKSEL_DSP_VAL		R2_CLKSEL_DSP | R2_CLKSEL_DSP_IF
#define R2_CLKSEL_GFX			(2 << 0)
#define R2_CM_CLKSEL_GFX_VAL		R2_CLKSEL_GFX
#define R2_CLKSEL_MDM			(6 << 0)
#define R2_CM_CLKSEL_MDM_VAL		R2_CLKSEL_MDM

/* 2430-Ratio Bootm (BYPASS) */
#define RB_CLKSEL_L3			(1 << 0)
#define RB_CLKSEL_L4			(1 << 5)
#define RB_CLKSEL_USB			(1 << 25)
#define RB_CM_CLKSEL1_CORE_VAL		RB_CLKSEL_USB | RX_CLKSEL_SSI | \
					RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					RB_CLKSEL_L4 | RB_CLKSEL_L3
#define RB_CLKSEL_MPU			(1 << 0)
#define RB_CM_CLKSEL_MPU_VAL		RB_CLKSEL_MPU
#define RB_CLKSEL_DSP			(1 << 0)
#define RB_CLKSEL_DSP_IF		(1 << 5)
#define RB_CM_CLKSEL_DSP_VAL		RB_CLKSEL_DSP | RB_CLKSEL_DSP_IF
#define RB_CLKSEL_GFX			(1 << 0)
#define RB_CM_CLKSEL_GFX_VAL		RB_CLKSEL_GFX
#define RB_CLKSEL_MDM			(1 << 0)
#define RB_CM_CLKSEL_MDM_VAL		RB_CLKSEL_MDM

/* 2420 Ratio Equivalents */
#define RXX_CLKSEL_VLYNQ		(0x12 << 15)
#define RXX_CLKSEL_SSI			(0x8 << 20)

/* 2420-PRCM III 532MHz core */
#define RIII_CLKSEL_L3			(4 << 0)	/* 133MHz */
#define RIII_CLKSEL_L4			(2 << 5)	/* 66.5MHz */
#define RIII_CLKSEL_USB			(4 << 25)	/* 33.25MHz */
#define RIII_CM_CLKSEL1_CORE_VAL	RIII_CLKSEL_USB | RXX_CLKSEL_SSI | \
					RXX_CLKSEL_VLYNQ | RX_CLKSEL_DSS2 | \
					RX_CLKSEL_DSS1 | RIII_CLKSEL_L4 | \
					RIII_CLKSEL_L3
#define RIII_CLKSEL_MPU			(2 << 0)	/* 266MHz */
#define RIII_CM_CLKSEL_MPU_VAL		RIII_CLKSEL_MPU
#define RIII_CLKSEL_DSP			(3 << 0)	/* c5x - 177.3MHz */
#define RIII_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 88.67MHz */
#define RIII_SYNC_DSP			(1 << 7)	/* Enable sync */
#define RIII_CLKSEL_IVA			(6 << 8)	/* iva1 - 88.67MHz */
#define RIII_SYNC_IVA			(1 << 13)	/* Enable sync */
#define RIII_CM_CLKSEL_DSP_VAL		RIII_SYNC_IVA | RIII_CLKSEL_IVA | \
					RIII_SYNC_DSP | RIII_CLKSEL_DSP_IF | \
					RIII_CLKSEL_DSP
#define RIII_CLKSEL_GFX			(2 << 0)	/* 66.5MHz */
#define RIII_CM_CLKSEL_GFX_VAL		RIII_CLKSEL_GFX

/* 2420-PRCM II 600MHz core */
#define RII_CLKSEL_L3			(6 << 0)	/* 100MHz */
#define RII_CLKSEL_L4			(2 << 5)	/* 50MHz */
#define RII_CLKSEL_USB			(2 << 25)	/* 50MHz */
#define RII_CM_CLKSEL1_CORE_VAL		RII_CLKSEL_USB | \
					RXX_CLKSEL_SSI | RXX_CLKSEL_VLYNQ | \
					RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					RII_CLKSEL_L4 | RII_CLKSEL_L3
#define RII_CLKSEL_MPU			(2 << 0)	/* 300MHz */
#define RII_CM_CLKSEL_MPU_VAL		RII_CLKSEL_MPU
#define RII_CLKSEL_DSP			(3 << 0)	/* c5x - 200MHz */
#define RII_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 100MHz */
#define RII_SYNC_DSP			(0 << 7)	/* Bypass sync */
#define RII_CLKSEL_IVA			(3 << 8)	/* iva1 - 200MHz */
#define RII_SYNC_IVA			(0 << 13)	/* Bypass sync */
#define RII_CM_CLKSEL_DSP_VAL		RII_SYNC_IVA | RII_CLKSEL_IVA | \
					RII_SYNC_DSP | RII_CLKSEL_DSP_IF | \
					RII_CLKSEL_DSP
#define RII_CLKSEL_GFX			(2 << 0)	/* 50MHz */
#define RII_CM_CLKSEL_GFX_VAL		RII_CLKSEL_GFX

/* 2420-PRCM I 660MHz core */
#define RI_CLKSEL_L3			(4 << 0)	/* 165MHz */
#define RI_CLKSEL_L4			(2 << 5)	/* 82.5MHz */
#define RI_CLKSEL_USB			(4 << 25)	/* 41.25MHz */
#define RI_CM_CLKSEL1_CORE_VAL		RI_CLKSEL_USB | \
					RXX_CLKSEL_SSI | RXX_CLKSEL_VLYNQ | \
					RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					RI_CLKSEL_L4 | RI_CLKSEL_L3
#define RI_CLKSEL_MPU			(2 << 0)	/* 330MHz */
#define RI_CM_CLKSEL_MPU_VAL		RI_CLKSEL_MPU
#define RI_CLKSEL_DSP			(3 << 0)	/* c5x - 220MHz */
#define RI_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 110MHz */
#define RI_SYNC_DSP			(1 << 7)	/* Activate sync */
#define RI_CLKSEL_IVA			(4 << 8)	/* iva1 - 165MHz */
#define RI_SYNC_IVA			(0 << 13)	/* Bypass sync */
#define RI_CM_CLKSEL_DSP_VAL		RI_SYNC_IVA | RI_CLKSEL_IVA | \
					RI_SYNC_DSP | RI_CLKSEL_DSP_IF | \
					RI_CLKSEL_DSP
#define RI_CLKSEL_GFX			(1 << 0)	/* 165MHz */
#define RI_CM_CLKSEL_GFX_VAL		RI_CLKSEL_GFX

/* 2420-PRCM VII (boot) */
#define RVII_CLKSEL_L3			(1 << 0)
#define RVII_CLKSEL_L4			(1 << 5)
#define RVII_CLKSEL_DSS1		(1 << 8)
#define RVII_CLKSEL_DSS2		(0 << 13)
#define RVII_CLKSEL_VLYNQ		(1 << 15)
#define RVII_CLKSEL_SSI			(1 << 20)
#define RVII_CLKSEL_USB			(1 << 25)

#define RVII_CM_CLKSEL1_CORE_VAL	RVII_CLKSEL_USB | RVII_CLKSEL_SSI | \
					RVII_CLKSEL_VLYNQ | RVII_CLKSEL_DSS2 | \
					RVII_CLKSEL_DSS1 | RVII_CLKSEL_L4 | RVII_CLKSEL_L3

#define RVII_CLKSEL_MPU			(1 << 0) /* all divide by 1 */
#define RVII_CM_CLKSEL_MPU_VAL		RVII_CLKSEL_MPU

#define RVII_CLKSEL_DSP			(1 << 0)
#define RVII_CLKSEL_DSP_IF		(1 << 5)
#define RVII_SYNC_DSP			(0 << 7)
#define RVII_CLKSEL_IVA			(1 << 8)
#define RVII_SYNC_IVA			(0 << 13)
#define RVII_CM_CLKSEL_DSP_VAL		RVII_SYNC_IVA | RVII_CLKSEL_IVA | RVII_SYNC_DSP | \
					RVII_CLKSEL_DSP_IF | RVII_CLKSEL_DSP

#define RVII_CLKSEL_GFX			(1 << 0)
#define RVII_CM_CLKSEL_GFX_VAL		RVII_CLKSEL_GFX

/*-------------------------------------------------------------------------
 * 2430 Target modes: Along with each configuration the CPU has several
 * modes which goes along with them. Modes mainly are the addition of
 * describe DPLL combinations to go along with a ratio.
 *-------------------------------------------------------------------------*/

/* Hardware governed */
#define MX_48M_SRC			(0 << 3)
#define MX_54M_SRC			(0 << 5)
#define MX_APLLS_CLIKIN_12		(3 << 23)
#define MX_APLLS_CLIKIN_13		(2 << 23)
#define MX_APLLS_CLIKIN_19_2		(0 << 23)

/*
 * 2430 - standalone, 2*ref*M/(n+1), M/N is for exactness not relock speed
 * #5a	(ratio1) baseport-target, target DPLL = 266*2 = 532MHz
 */
#define M5A_DPLL_MULT_12		(133 << 12)
#define M5A_DPLL_DIV_12			(5 << 8)
#define M5A_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5A_DPLL_DIV_12 | M5A_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12
#define M5A_DPLL_MULT_13		(61 << 12)
#define M5A_DPLL_DIV_13			(2 << 8)
#define M5A_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5A_DPLL_DIV_13 | M5A_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13
#define M5A_DPLL_MULT_19		(55 << 12)
#define M5A_DPLL_DIV_19			(3 << 8)
#define M5A_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5A_DPLL_DIV_19 | M5A_DPLL_MULT_19 | \
					MX_APLLS_CLIKIN_19_2
/* #5b	(ratio1) target DPLL = 200*2 = 400MHz */
#define M5B_DPLL_MULT_12		(50 << 12)
#define M5B_DPLL_DIV_12			(2 << 8)
#define M5B_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5B_DPLL_DIV_12 | M5B_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12
#define M5B_DPLL_MULT_13		(200 << 12)
#define M5B_DPLL_DIV_13			(12 << 8)

#define M5B_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5B_DPLL_DIV_13 | M5B_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13
#define M5B_DPLL_MULT_19		(125 << 12)
#define M5B_DPLL_DIV_19			(31 << 8)
#define M5B_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | \
					M5B_DPLL_DIV_19 | M5B_DPLL_MULT_19 | \
					MX_APLLS_CLIKIN_19_2
/*
 * #4	(ratio2), DPLL = 399*2 = 798MHz, L3=133MHz
 */
#define M4_DPLL_MULT_12			(133 << 12)
#define M4_DPLL_DIV_12			(3 << 8)
#define M4_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					M4_DPLL_DIV_12 | M4_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12

#define M4_DPLL_MULT_13			(399 << 12)
#define M4_DPLL_DIV_13			(12 << 8)
#define M4_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					M4_DPLL_DIV_13 | M4_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13

#define M4_DPLL_MULT_19			(145 << 12)
#define M4_DPLL_DIV_19			(6 << 8)
#define M4_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | \
					M4_DPLL_DIV_19 | M4_DPLL_MULT_19 | \
					MX_APLLS_CLIKIN_19_2

/*
 * #3	(ratio2) baseport-target, target DPLL = 330*2 = 660MHz
 */
#define M3_DPLL_MULT_12			(55 << 12)
#define M3_DPLL_DIV_12			(1 << 8)
#define M3_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					M3_DPLL_DIV_12 | M3_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12
#define M3_DPLL_MULT_13			(76 << 12)
#define M3_DPLL_DIV_13			(2 << 8)
#define M3_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					M3_DPLL_DIV_13 | M3_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13
#define M3_DPLL_MULT_19			(17 << 12)
#define M3_DPLL_DIV_19			(0 << 8)
#define M3_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | \
					M3_DPLL_DIV_19 | M3_DPLL_MULT_19 | \
					MX_APLLS_CLIKIN_19_2

/*
 * #2   (ratio1) DPLL = 330*2 = 660MHz, L3=165MHz
 */
#define M2_DPLL_MULT_12		        (55 << 12)
#define M2_DPLL_DIV_12		        (1 << 8)
#define M2_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					M2_DPLL_DIV_12 | M2_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12

/* Speed changes - Used 658.7MHz instead of 660MHz for LP-Refresh M=76 N=2,
 * relock time issue */
/* Core frequency changed from 330/165 to 329/164 MHz*/
#define M2_DPLL_MULT_13		        (76 << 12)
#define M2_DPLL_DIV_13		        (2 << 8)
#define M2_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					M2_DPLL_DIV_13 | M2_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13

#define M2_DPLL_MULT_19		        (17 << 12)
#define M2_DPLL_DIV_19		        (0 << 8)
#define M2_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | \
					M2_DPLL_DIV_19 | M2_DPLL_MULT_19 | \
					MX_APLLS_CLIKIN_19_2

/* boot (boot) */
#define MB_DPLL_MULT			(1 << 12)
#define MB_DPLL_DIV			(0 << 8)
#define MB_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | MB_DPLL_DIV |\
					MB_DPLL_MULT | MX_APLLS_CLIKIN_12

#define MB_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | MB_DPLL_DIV |\
					MB_DPLL_MULT | MX_APLLS_CLIKIN_13

#define MB_CM_CLKSEL1_PLL_19_VAL	MX_48M_SRC | MX_54M_SRC | MB_DPLL_DIV |\
					MB_DPLL_MULT | MX_APLLS_CLIKIN_19

/*
 * 2430 - chassis (sedna)
 * 165 (ratio1) same as above #2
 * 150 (ratio1)
 * 133 (ratio2) same as above #4
 * 110 (ratio2) same as above #3
 * 104 (ratio2)
 * boot (boot)
 */

/* PRCM I target DPLL = 2*330MHz = 660MHz */
#define MI_DPLL_MULT_12			(55 << 12)
#define MI_DPLL_DIV_12			(1 << 8)
#define MI_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					MI_DPLL_DIV_12 | MI_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12

/*
 * 2420 Equivalent - mode registers
 * PRCM II , target DPLL = 2*300MHz = 600MHz
 */
#define MII_DPLL_MULT_12		(50 << 12)
#define MII_DPLL_DIV_12			(1 << 8)
#define MII_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					MII_DPLL_DIV_12 | MII_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12
#define MII_DPLL_MULT_13		(300 << 12)
#define MII_DPLL_DIV_13			(12 << 8)
#define MII_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					MII_DPLL_DIV_13 | MII_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13

/* PRCM III target DPLL = 2*266 = 532MHz*/
#define MIII_DPLL_MULT_12		(133 << 12)
#define MIII_DPLL_DIV_12		(5 << 8)
#define MIII_CM_CLKSEL1_PLL_12_VAL	MX_48M_SRC | MX_54M_SRC | \
					MIII_DPLL_DIV_12 | MIII_DPLL_MULT_12 | \
					MX_APLLS_CLIKIN_12
#define MIII_DPLL_MULT_13		(266 << 12)
#define MIII_DPLL_DIV_13		(12 << 8)
#define MIII_CM_CLKSEL1_PLL_13_VAL	MX_48M_SRC | MX_54M_SRC | \
					MIII_DPLL_DIV_13 | MIII_DPLL_MULT_13 | \
					MX_APLLS_CLIKIN_13

/* PRCM VII (boot bypass) */
#define MVII_CM_CLKSEL1_PLL_12_VAL	MB_CM_CLKSEL1_PLL_12_VAL
#define MVII_CM_CLKSEL1_PLL_13_VAL	MB_CM_CLKSEL1_PLL_13_VAL

/* High and low operation value */
#define MX_CLKSEL2_PLL_2x_VAL		(2 << 0)
#define MX_CLKSEL2_PLL_1x_VAL		(1 << 0)

/* MPU speed defines */
#define S12M	12000000
#define S13M	13000000
#define S19M	19200000
#define S26M	26000000
#define S100M	100000000
#define S133M	133000000
#define S150M	150000000
#define S164M	164000000
#define S165M	165000000
#define S199M	199000000
#define S200M	200000000
#define S266M	266000000
#define S300M	300000000
#define S329M	329000000
#define S330M	330000000
#define S399M	399000000
#define S400M	400000000
#define S532M	532000000
#define S600M	600000000
#define S658M	658000000
#define S660M	660000000
#define S798M	798000000

/*-------------------------------------------------------------------------
 * Key dividers which make up a PRCM set. Ratio's for a PRCM are mandated.
 * xtal_speed, dpll_speed, mpu_speed, CM_CLKSEL_MPU,
 * CM_CLKSEL_DSP, CM_CLKSEL_GFX, CM_CLKSEL1_CORE, CM_CLKSEL1_PLL,
 * CM_CLKSEL2_PLL, CM_CLKSEL_MDM
 *
 * Filling in table based on H4 boards and 2430-SDPs variants available.
 * There are quite a few more rates combinations which could be defined.
 *
 * When multiple values are defined the start up will try and choose the
 * fastest one. If a 'fast' value is defined, then automatically, the /2
 * one should be included as it can be used.	Generally having more that
 * one fast set does not make sense, as static timings need to be changed
 * to change the set.	 The exception is the bypass setting which is
 * availble for low power bypass.
 *
 * Note: This table needs to be sorted, fastest to slowest.
 *-------------------------------------------------------------------------*/
static struct prcm_config rate_table[] = {
	/* PRCM I - FAST */
	{S12M, S660M, S330M, RI_CM_CLKSEL_MPU_VAL,		/* 330MHz ARM */
		RI_CM_CLKSEL_DSP_VAL, RI_CM_CLKSEL_GFX_VAL,
		RI_CM_CLKSEL1_CORE_VAL, MI_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_165MHz,
		RATE_IN_242X},

	/* PRCM II - FAST */
	{S12M, S600M, S300M, RII_CM_CLKSEL_MPU_VAL,		/* 300MHz ARM */
		RII_CM_CLKSEL_DSP_VAL, RII_CM_CLKSEL_GFX_VAL,
		RII_CM_CLKSEL1_CORE_VAL, MII_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_100MHz,
		RATE_IN_242X},

	{S13M, S600M, S300M, RII_CM_CLKSEL_MPU_VAL,		/* 300MHz ARM */
		RII_CM_CLKSEL_DSP_VAL, RII_CM_CLKSEL_GFX_VAL,
		RII_CM_CLKSEL1_CORE_VAL, MII_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_100MHz,
		RATE_IN_242X},

	/* PRCM III - FAST */
	{S12M, S532M, S266M, RIII_CM_CLKSEL_MPU_VAL,		/* 266MHz ARM */
		RIII_CM_CLKSEL_DSP_VAL, RIII_CM_CLKSEL_GFX_VAL,
		RIII_CM_CLKSEL1_CORE_VAL, MIII_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_133MHz,
		RATE_IN_242X},

	{S13M, S532M, S266M, RIII_CM_CLKSEL_MPU_VAL,		/* 266MHz ARM */
		RIII_CM_CLKSEL_DSP_VAL, RIII_CM_CLKSEL_GFX_VAL,
		RIII_CM_CLKSEL1_CORE_VAL, MIII_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_133MHz,
		RATE_IN_242X},

	/* PRCM II - SLOW */
	{S12M, S300M, S150M, RII_CM_CLKSEL_MPU_VAL,		/* 150MHz ARM */
		RII_CM_CLKSEL_DSP_VAL, RII_CM_CLKSEL_GFX_VAL,
		RII_CM_CLKSEL1_CORE_VAL, MII_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_100MHz,
		RATE_IN_242X},

	{S13M, S300M, S150M, RII_CM_CLKSEL_MPU_VAL,		/* 150MHz ARM */
		RII_CM_CLKSEL_DSP_VAL, RII_CM_CLKSEL_GFX_VAL,
		RII_CM_CLKSEL1_CORE_VAL, MII_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_100MHz,
		RATE_IN_242X},

	/* PRCM III - SLOW */
	{S12M, S266M, S133M, RIII_CM_CLKSEL_MPU_VAL,		/* 133MHz ARM */
		RIII_CM_CLKSEL_DSP_VAL, RIII_CM_CLKSEL_GFX_VAL,
		RIII_CM_CLKSEL1_CORE_VAL, MIII_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_133MHz,
		RATE_IN_242X},

	{S13M, S266M, S133M, RIII_CM_CLKSEL_MPU_VAL,		/* 133MHz ARM */
		RIII_CM_CLKSEL_DSP_VAL, RIII_CM_CLKSEL_GFX_VAL,
		RIII_CM_CLKSEL1_CORE_VAL, MIII_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_133MHz,
		RATE_IN_242X},

	/* PRCM-VII (boot-bypass) */
	{S12M, S12M, S12M, RVII_CM_CLKSEL_MPU_VAL,		/* 12MHz ARM*/
		RVII_CM_CLKSEL_DSP_VAL, RVII_CM_CLKSEL_GFX_VAL,
		RVII_CM_CLKSEL1_CORE_VAL, MVII_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_BYPASS,
		RATE_IN_242X},

	/* PRCM-VII (boot-bypass) */
	{S13M, S13M, S13M, RVII_CM_CLKSEL_MPU_VAL,		/* 13MHz ARM */
		RVII_CM_CLKSEL_DSP_VAL, RVII_CM_CLKSEL_GFX_VAL,
		RVII_CM_CLKSEL1_CORE_VAL, MVII_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, 0, SDRC_RFR_CTRL_BYPASS,
		RATE_IN_242X},

	/* PRCM #4 - ratio2 (ES2.1) - FAST */
	{S13M, S798M, S399M, R2_CM_CLKSEL_MPU_VAL,		/* 399MHz ARM */
		R2_CM_CLKSEL_DSP_VAL, R2_CM_CLKSEL_GFX_VAL,
		R2_CM_CLKSEL1_CORE_VAL, M4_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, R2_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_133MHz,
		RATE_IN_243X},

	/* PRCM #2 - ratio1 (ES2) - FAST */
	{S13M, S658M, S329M, R1_CM_CLKSEL_MPU_VAL,		/* 330MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M2_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_165MHz,
		RATE_IN_243X},

	/* PRCM #5a - ratio1 - FAST */
	{S13M, S532M, S266M, R1_CM_CLKSEL_MPU_VAL,		/* 266MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M5A_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_133MHz,
		RATE_IN_243X},

	/* PRCM #5b - ratio1 - FAST */
	{S13M, S400M, S200M, R1_CM_CLKSEL_MPU_VAL,		/* 200MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M5B_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_100MHz,
		RATE_IN_243X},

	/* PRCM #4 - ratio1 (ES2.1) - SLOW */
	{S13M, S399M, S199M, R2_CM_CLKSEL_MPU_VAL,		/* 200MHz ARM */
		R2_CM_CLKSEL_DSP_VAL, R2_CM_CLKSEL_GFX_VAL,
		R2_CM_CLKSEL1_CORE_VAL, M4_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_1x_VAL, R2_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_133MHz,
		RATE_IN_243X},

	/* PRCM #2 - ratio1 (ES2) - SLOW */
	{S13M, S329M, S164M, R1_CM_CLKSEL_MPU_VAL,		/* 165MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M2_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_1x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_165MHz,
		RATE_IN_243X},

	/* PRCM #5a - ratio1 - SLOW */
	{S13M, S266M, S133M, R1_CM_CLKSEL_MPU_VAL,		/* 133MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M5A_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_1x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_133MHz,
		RATE_IN_243X},

	/* PRCM #5b - ratio1 - SLOW*/
	{S13M, S200M, S100M, R1_CM_CLKSEL_MPU_VAL,		/* 100MHz ARM */
		R1_CM_CLKSEL_DSP_VAL, R1_CM_CLKSEL_GFX_VAL,
		R1_CM_CLKSEL1_CORE_VAL, M5B_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_1x_VAL, R1_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_100MHz,
		RATE_IN_243X},

	/* PRCM-boot/bypass */
	{S13M, S13M, S13M, RB_CM_CLKSEL_MPU_VAL,		/* 13Mhz */
		RB_CM_CLKSEL_DSP_VAL, RB_CM_CLKSEL_GFX_VAL,
		RB_CM_CLKSEL1_CORE_VAL, MB_CM_CLKSEL1_PLL_13_VAL,
		MX_CLKSEL2_PLL_2x_VAL, RB_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_BYPASS,
		RATE_IN_243X},

	/* PRCM-boot/bypass */
	{S12M, S12M, S12M, RB_CM_CLKSEL_MPU_VAL,		/* 12Mhz */
		RB_CM_CLKSEL_DSP_VAL, RB_CM_CLKSEL_GFX_VAL,
		RB_CM_CLKSEL1_CORE_VAL, MB_CM_CLKSEL1_PLL_12_VAL,
		MX_CLKSEL2_PLL_2x_VAL, RB_CM_CLKSEL_MDM_VAL,
		SDRC_RFR_CTRL_BYPASS,
		RATE_IN_243X},

	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/*-------------------------------------------------------------------------
 * 24xx clock tree.
 *
 * NOTE:In many cases here we are assigning a 'default' parent.	In many
 *	cases the parent is selectable.	The get/set parent calls will also
 *	switch sources.
 *
 *	Many some clocks say always_enabled, but they can be auto idled for
 *	power savings. They will always be available upon clock request.
 *
 *	Several sources are given initial rates which may be wrong, this will
 *	be fixed up in the init func.
 *
 *	Things are broadly separated below by clock domains. It is
 *	noteworthy that most periferals have dependencies on multiple clock
 *	domains. Many get their interface clocks from the L4 domain, but get
 *	functional clocks from fixed sources or other core domain derived
 *	clocks.
 *-------------------------------------------------------------------------*/

/* Base external input clocks */
static struct clk func_32k_ck = {
	.name		= "func_32k_ck",
	.rate		= 32000,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_FIXED | ALWAYS_ENABLED | RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &propagate_rate,
};

/* Typical 12/13MHz in standalone mode, will be 26Mhz in chassis mode */
static struct clk osc_ck = {		/* (*12, *13, 19.2, *26, 38.4)MHz */
	.name		= "osc_ck",
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.enable		= &omap2_enable_osc_ck,
	.disable	= &omap2_disable_osc_ck,
	.recalc		= &omap2_osc_clk_recalc,
};

/* Without modem likely 12MHz, with modem likely 13MHz */
static struct clk sys_ck = {		/* (*12, *13, 19.2, 26, 38.4)MHz */
	.name		= "sys_ck",		/* ~ ref_clk also */
	.parent		= &osc_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ALWAYS_ENABLED | RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_sys_clk_recalc,
};

static struct clk alt_ck = {		/* Typical 54M or 48M, may not exist */
	.name		= "alt_ck",
	.rate		= 54000000,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_FIXED | ALWAYS_ENABLED | RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &propagate_rate,
};

/*
 * Analog domain root source clocks
 */

/* dpll_ck, is broken out in to special cases through clksel */
/* REVISIT: Rate changes on dpll_ck trigger a full set change.	...
 * deal with this
 */

static struct dpll_data dpll_dd = {
	.mult_div1_reg		= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.mult_mask		= OMAP24XX_DPLL_MULT_MASK,
	.div1_mask		= OMAP24XX_DPLL_DIV_MASK,
	.max_multiplier		= 1024,
	.max_divider		= 16,
	.rate_tolerance		= DEFAULT_DPLL_RATE_TOLERANCE
};

/*
 * XXX Cannot add round_rate here yet, as this is still a composite clock,
 * not just a DPLL
 */
static struct clk dpll_ck = {
	.name		= "dpll_ck",
	.parent		= &sys_ck,		/* Can be func_32k also */
	.dpll_data	= &dpll_dd,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES | ALWAYS_ENABLED,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_dpllcore_recalc,
	.set_rate	= &omap2_reprogram_dpllcore,
};

static struct clk apll96_ck = {
	.name		= "apll96_ck",
	.parent		= &sys_ck,
	.rate		= 96000000,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_FIXED | RATE_PROPAGATES | ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_96M_PLL_SHIFT,
	.enable		= &omap2_clk_fixed_enable,
	.disable	= &omap2_clk_fixed_disable,
	.recalc		= &propagate_rate,
};

static struct clk apll54_ck = {
	.name		= "apll54_ck",
	.parent		= &sys_ck,
	.rate		= 54000000,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_FIXED | RATE_PROPAGATES | ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_54M_PLL_SHIFT,
	.enable		= &omap2_clk_fixed_enable,
	.disable	= &omap2_clk_fixed_disable,
	.recalc		= &propagate_rate,
};

/*
 * PRCM digital base sources
 */

/* func_54m_ck */

static const struct clksel_rate func_54m_apll54_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel_rate func_54m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel func_54m_clksel[] = {
	{ .parent = &apll54_ck, .rates = func_54m_apll54_rates, },
	{ .parent = &alt_ck,	.rates = func_54m_alt_rates, },
	{ .parent = NULL },
};

static struct clk func_54m_ck = {
	.name		= "func_54m_ck",
	.parent		= &apll54_ck,	/* can also be alt_clk */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES | PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_54M_SOURCE,
	.clksel		= func_54m_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk core_ck = {
	.name		= "core_ck",
	.parent		= &dpll_ck,		/* can also be 32k */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ALWAYS_ENABLED | RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* func_96m_ck */
static const struct clksel_rate func_96m_apll96_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel_rate func_96m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_243X | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel func_96m_clksel[] = {
	{ .parent = &apll96_ck,	.rates = func_96m_apll96_rates },
	{ .parent = &alt_ck,	.rates = func_96m_alt_rates },
	{ .parent = NULL }
};

/* The parent of this clock is not selectable on 2420. */
static struct clk func_96m_ck = {
	.name		= "func_96m_ck",
	.parent		= &apll96_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES | PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP2430_96M_SOURCE,
	.clksel		= func_96m_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* func_48m_ck */

static const struct clksel_rate func_48m_apll96_rates[] = {
	{ .div = 2, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel_rate func_48m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 },
};

static const struct clksel func_48m_clksel[] = {
	{ .parent = &apll96_ck,	.rates = func_48m_apll96_rates },
	{ .parent = &alt_ck, .rates = func_48m_alt_rates },
	{ .parent = NULL }
};

static struct clk func_48m_ck = {
	.name		= "func_48m_ck",
	.parent		= &apll96_ck,	 /* 96M or Alt */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES | PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_48M_SOURCE,
	.clksel		= func_48m_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk func_12m_ck = {
	.name		= "func_12m_ck",
	.parent		= &func_48m_ck,
	.fixed_div	= 4,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES | PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_fixed_divisor_recalc,
};

/* Secure timer, only available in secure mode */
static struct clk wdt1_osc_ck = {
	.name		= "ck_wdt1_osc",
	.parent		= &osc_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.recalc		= &followparent_recalc,
};

/*
 * The common_clkout* clksel_rate structs are common to
 * sys_clkout, sys_clkout_src, sys_clkout2, and sys_clkout2_src.
 * sys_clkout2_* are 2420-only, so the
 * clksel_rate flags fields are inaccurate for those clocks. This is
 * harmless since access to those clocks are gated by the struct clk
 * flags fields, which mark them as 2420-only.
 */
static const struct clksel_rate common_clkout_src_core_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_sys_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_96m_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_54m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel common_clkout_src_clksel[] = {
	{ .parent = &core_ck,	  .rates = common_clkout_src_core_rates },
	{ .parent = &sys_ck,	  .rates = common_clkout_src_sys_rates },
	{ .parent = &func_96m_ck, .rates = common_clkout_src_96m_rates },
	{ .parent = &func_54m_ck, .rates = common_clkout_src_54m_rates },
	{ .parent = NULL }
};

static struct clk sys_clkout_src = {
	.name		= "sys_clkout_src",
	.parent		= &func_54m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.enable_bit	= OMAP24XX_CLKOUT_EN_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP24XX_CLKOUT_SOURCE_MASK,
	.clksel		= common_clkout_src_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static const struct clksel_rate common_clkout_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 2, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 8, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 16, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel sys_clkout_clksel[] = {
	{ .parent = &sys_clkout_src, .rates = common_clkout_rates },
	{ .parent = NULL }
};

static struct clk sys_clkout = {
	.name		= "sys_clkout",
	.parent		= &sys_clkout_src,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.clksel_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP24XX_CLKOUT_DIV_MASK,
	.clksel		= sys_clkout_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* In 2430, new in 2420 ES2 */
static struct clk sys_clkout2_src = {
	.name		= "sys_clkout2_src",
	.parent		= &func_54m_ck,
	.flags		= CLOCK_IN_OMAP242X | RATE_PROPAGATES,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.enable_bit	= OMAP2420_CLKOUT2_EN_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP2420_CLKOUT2_SOURCE_MASK,
	.clksel		= common_clkout_src_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static const struct clksel sys_clkout2_clksel[] = {
	{ .parent = &sys_clkout2_src, .rates = common_clkout_rates },
	{ .parent = NULL }
};

/* In 2430, new in 2420 ES2 */
static struct clk sys_clkout2 = {
	.name		= "sys_clkout2",
	.parent		= &sys_clkout2_src,
	.flags		= CLOCK_IN_OMAP242X | PARENT_CONTROLS_CLOCK,
	.clkdm_name	= "wkup_clkdm",
	.clksel_reg	= OMAP24XX_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP2420_CLKOUT2_DIV_MASK,
	.clksel		= sys_clkout2_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk emul_ck = {
	.name		= "emul_ck",
	.parent		= &func_54m_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP24XX_PRCM_CLKEMUL_CTRL,
	.enable_bit	= OMAP24XX_EMULATION_EN_SHIFT,
	.recalc		= &followparent_recalc,

};

/*
 * MPU clock domain
 *	Clocks:
 *		MPU_FCLK, MPU_ICLK
 *		INT_M_FCLK, INT_M_I_CLK
 *
 * - Individual clocks are hardware managed.
 * - Base divider comes from: CM_CLKSEL_MPU
 *
 */
static const struct clksel_rate mpu_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_242X },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 0 },
};

static const struct clksel mpu_clksel[] = {
	{ .parent = &core_ck, .rates = mpu_core_rates },
	{ .parent = NULL }
};

static struct clk mpu_ck = {	/* Control cpu */
	.name		= "mpu_ck",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ALWAYS_ENABLED | DELAYED_APP |
				CONFIG_PARTICIPANT | RATE_PROPAGATES,
	.clkdm_name	= "mpu_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(MPU_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_MPU_MASK,
	.clksel		= mpu_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/*
 * DSP (2430-IVA2.1) (2420-UMA+IVA1) clock domain
 * Clocks:
 *	2430: IVA2.1_FCLK (really just DSP_FCLK), IVA2.1_ICLK
 *	2420: UMA_FCLK, UMA_ICLK, IVA_MPU, IVA_COP
 *
 * Won't be too specific here. The core clock comes into this block
 * it is divided then tee'ed. One branch goes directly to xyz enable
 * controls. The other branch gets further divided by 2 then possibly
 * routed into a synchronizer and out of clocks abc.
 */
static const struct clksel_rate dsp_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 12, .val = 12, .flags = RATE_IN_242X },
	{ .div = 0 },
};

static const struct clksel dsp_fck_clksel[] = {
	{ .parent = &core_ck, .rates = dsp_fck_core_rates },
	{ .parent = NULL }
};

static struct clk dsp_fck = {
	.name		= "dsp_fck",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | DELAYED_APP |
				CONFIG_PARTICIPANT | RATE_PROPAGATES,
	.clkdm_name	= "dsp_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_DSP_MASK,
	.clksel		= dsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* DSP interface clock */
static const struct clksel_rate dsp_irate_ick_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_243X },
	{ .div = 0 },
};

static const struct clksel dsp_irate_ick_clksel[] = {
	{ .parent = &dsp_fck, .rates = dsp_irate_ick_rates },
	{ .parent = NULL }
};

/* This clock does not exist as such in the TRM. */
static struct clk dsp_irate_ick = {
	.name		= "dsp_irate_ick",
	.parent		= &dsp_fck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | DELAYED_APP |
				CONFIG_PARTICIPANT | PARENT_CONTROLS_CLOCK,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_DSP_IF_MASK,
	.clksel		= dsp_irate_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	      = &omap2_clksel_set_rate
};

/* 2420 only */
static struct clk dsp_ick = {
	.name		= "dsp_ick",	 /* apparently ipi and isp */
	.parent		= &dsp_irate_ick,
	.flags		= CLOCK_IN_OMAP242X | DELAYED_APP | CONFIG_PARTICIPANT,
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP2420_EN_DSP_IPI_SHIFT,	      /* for ipi */
};

/* 2430 only - EN_DSP controls both dsp fclk and iclk on 2430 */
static struct clk iva2_1_ick = {
	.name		= "iva2_1_ick",
	.parent		= &dsp_irate_ick,
	.flags		= CLOCK_IN_OMAP243X | DELAYED_APP | CONFIG_PARTICIPANT,
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT,
};

/*
 * The IVA1 is an ARM7 core on the 2420 that has nothing to do with
 * the C54x, but which is contained in the DSP powerdomain.  Does not
 * exist on later OMAPs.
 */
static struct clk iva1_ifck = {
	.name		= "iva1_ifck",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP242X | CONFIG_PARTICIPANT |
				RATE_PROPAGATES | DELAYED_APP,
	.clkdm_name	= "iva1_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP2420_EN_IVA_COP_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP2420_CLKSEL_IVA_MASK,
	.clksel		= dsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* IVA1 mpu/int/i/f clocks are /2 of parent */
static struct clk iva1_mpu_int_ifck = {
	.name		= "iva1_mpu_int_ifck",
	.parent		= &iva1_ifck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "iva1_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP2420_EN_IVA_MPU_SHIFT,
	.fixed_div	= 2,
	.recalc		= &omap2_fixed_divisor_recalc,
};

/*
 * L3 clock domain
 * L3 clocks are used for both interface and functional clocks to
 * multiple entities. Some of these clocks are completely managed
 * by hardware, and some others allow software control. Hardware
 * managed ones general are based on directly CLK_REQ signals and
 * various auto idle settings. The functional spec sets many of these
 * as 'tie-high' for their enables.
 *
 * I-CLOCKS:
 *	L3-Interconnect, SMS, GPMC, SDRC, OCM_RAM, OCM_ROM, SDMA
 *	CAM, HS-USB.
 * F-CLOCK
 *	SSI.
 *
 * GPMC memories and SDRC have timing and clock sensitive registers which
 * may very well need notification when the clock changes. Currently for low
 * operating points, these are taken care of in sleep.S.
 */
static const struct clksel_rate core_l3_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_242X },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 6, .val = 6, .flags = RATE_IN_24XX },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 12, .val = 12, .flags = RATE_IN_242X },
	{ .div = 16, .val = 16, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel core_l3_clksel[] = {
	{ .parent = &core_ck, .rates = core_l3_core_rates },
	{ .parent = NULL }
};

static struct clk core_l3_ck = {	/* Used for ick and fck, interconnect */
	.name		= "core_l3_ck",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ALWAYS_ENABLED | DELAYED_APP |
				CONFIG_PARTICIPANT | RATE_PROPAGATES,
	.clkdm_name	= "core_l3_clkdm",
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_L3_MASK,
	.clksel		= core_l3_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* usb_l4_ick */
static const struct clksel_rate usb_l4_ick_core_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel usb_l4_ick_clksel[] = {
	{ .parent = &core_l3_ck, .rates = usb_l4_ick_core_l3_rates },
	{ .parent = NULL },
};

/* It is unclear from TRM whether usb_l4_ick is really in L3 or L4 clkdm */
static struct clk usb_l4_ick = {	/* FS-USB interface clock */
	.name		= "usb_l4_ick",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				DELAYED_APP | CONFIG_PARTICIPANT,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_USB_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_USB_MASK,
	.clksel		= usb_l4_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/*
 * L4 clock management domain
 *
 * This domain contains lots of interface clocks from the L4 interface, some
 * functional clocks.	Fixed APLL functional source clocks are managed in
 * this domain.
 */
static const struct clksel_rate l4_core_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel l4_clksel[] = {
	{ .parent = &core_l3_ck, .rates = l4_core_l3_rates },
	{ .parent = NULL }
};

static struct clk l4_ck = {		/* used both as an ick and fck */
	.name		= "l4_ck",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ALWAYS_ENABLED | DELAYED_APP | RATE_PROPAGATES,
	.clkdm_name	= "core_l4_clkdm",
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_L4_MASK,
	.clksel		= l4_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/*
 * SSI is in L3 management domain, its direct parent is core not l3,
 * many core power domain entities are grouped into the L3 clock
 * domain.
 * SSI_SSR_FCLK, SSI_SST_FCLK, SSI_L4_ICLK
 *
 * ssr = core/1/2/3/4/5, sst = 1/2 ssr.
 */
static const struct clksel_rate ssi_ssr_sst_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_243X },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel ssi_ssr_sst_fck_clksel[] = {
	{ .parent = &core_ck, .rates = ssi_ssr_sst_fck_core_rates },
	{ .parent = NULL }
};

static struct clk ssi_ssr_sst_fck = {
	.name		= "ssi_fck",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				DELAYED_APP,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_SSI_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_SSI_MASK,
	.clksel		= ssi_ssr_sst_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};


/*
 * GFX clock domain
 *	Clocks:
 * GFX_FCLK, GFX_ICLK
 * GFX_CG1(2d), GFX_CG2(3d)
 *
 * GFX_FCLK runs from L3, and is divided by (1,2,3,4)
 * The 2d and 3d clocks run at a hardware determined
 * divided value of fclk.
 *
 */
/* XXX REVISIT: GFX clock is part of CONFIG_PARTICIPANT, no? doublecheck. */

/* This clksel struct is shared between gfx_3d_fck and gfx_2d_fck */
static const struct clksel gfx_fck_clksel[] = {
	{ .parent = &core_l3_ck, .rates = gfx_l3_rates },
	{ .parent = NULL },
};

static struct clk gfx_3d_fck = {
	.name		= "gfx_3d_fck",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_3D_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP_CLKSEL_GFX_MASK,
	.clksel		= gfx_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk gfx_2d_fck = {
	.name		= "gfx_2d_fck",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_2D_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP_CLKSEL_GFX_MASK,
	.clksel		= gfx_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk gfx_ick = {
	.name		= "gfx_ick",		/* From l3 */
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP_EN_GFX_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * Modem clock domain (2430)
 *	CLOCKS:
 *		MDM_OSC_CLK
 *		MDM_ICLK
 * These clocks are usable in chassis mode only.
 */
static const struct clksel_rate mdm_ick_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_243X },
	{ .div = 4, .val = 4, .flags = RATE_IN_243X | DEFAULT_RATE },
	{ .div = 6, .val = 6, .flags = RATE_IN_243X },
	{ .div = 9, .val = 9, .flags = RATE_IN_243X },
	{ .div = 0 }
};

static const struct clksel mdm_ick_clksel[] = {
	{ .parent = &core_ck, .rates = mdm_ick_core_rates },
	{ .parent = NULL }
};

static struct clk mdm_ick = {		/* used both as a ick and fck */
	.name		= "mdm_ick",
	.parent		= &core_ck,
	.flags		= CLOCK_IN_OMAP243X | DELAYED_APP | CONFIG_PARTICIPANT,
	.clkdm_name	= "mdm_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_ICLKEN),
	.enable_bit	= OMAP2430_CM_ICLKEN_MDM_EN_MDM_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP2430_CLKSEL_MDM_MASK,
	.clksel		= mdm_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk mdm_osc_ck = {
	.name		= "mdm_osc_ck",
	.parent		= &osc_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "mdm_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP2430_EN_OSC_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * DSS clock domain
 * CLOCKs:
 * DSS_L4_ICLK, DSS_L3_ICLK,
 * DSS_CLK1, DSS_CLK2, DSS_54MHz_CLK
 *
 * DSS is both initiator and target.
 */
/* XXX Add RATE_NOT_VALIDATED */

static const struct clksel_rate dss1_fck_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate dss1_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_24XX },
	{ .div = 8, .val = 8, .flags = RATE_IN_24XX },
	{ .div = 9, .val = 9, .flags = RATE_IN_24XX },
	{ .div = 12, .val = 12, .flags = RATE_IN_24XX },
	{ .div = 16, .val = 16, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel dss1_fck_clksel[] = {
	{ .parent = &sys_ck,  .rates = dss1_fck_sys_rates },
	{ .parent = &core_ck, .rates = dss1_fck_core_rates },
	{ .parent = NULL },
};

static struct clk dss_ick = {		/* Enables both L3,L4 ICLK's */
	.name		= "dss_ick",
	.parent		= &l4_ck,	/* really both l3 and l4 */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk dss1_fck = {
	.name		= "dss1_fck",
	.parent		= &core_ck,		/* Core or sys */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				DELAYED_APP,
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS1_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_DSS1_MASK,
	.clksel		= dss1_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static const struct clksel_rate dss2_fck_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate dss2_fck_48m_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel dss2_fck_clksel[] = {
	{ .parent = &sys_ck,	  .rates = dss2_fck_sys_rates },
	{ .parent = &func_48m_ck, .rates = dss2_fck_48m_rates },
	{ .parent = NULL }
};

static struct clk dss2_fck = {		/* Alt clk used in power management */
	.name		= "dss2_fck",
	.parent		= &sys_ck,		/* fixed at sys_ck or 48MHz */
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				DELAYED_APP,
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS2_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_DSS2_MASK,
	.clksel		= dss2_fck_clksel,
	.recalc		= &followparent_recalc,
};

static struct clk dss_54m_fck = {	/* Alt clk used in power management */
	.name		= "dss_54m_fck",	/* 54m tv clk */
	.parent		= &func_54m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_TV_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * CORE power domain ICLK & FCLK defines.
 * Many of the these can have more than one possible parent. Entries
 * here will likely have an L4 interface parent, and may have multiple
 * functional clock parents.
 */
static const struct clksel_rate gpt_alt_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_24XX | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel omap24xx_gpt_clksel[] = {
	{ .parent = &func_32k_ck, .rates = gpt_32k_rates },
	{ .parent = &sys_ck,	  .rates = gpt_sys_rates },
	{ .parent = &alt_ck,	  .rates = gpt_alt_rates },
	{ .parent = NULL },
};

static struct clk gpt1_ick = {
	.name		= "gpt1_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPT1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt1_fck = {
	.name		= "gpt1_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_GPT1_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT1_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk gpt2_ick = {
	.name		= "gpt2_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt2_fck = {
	.name		= "gpt2_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT2_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT2_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt3_ick = {
	.name		= "gpt3_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt3_fck = {
	.name		= "gpt3_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT3_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT3_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt4_ick = {
	.name		= "gpt4_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt4_fck = {
	.name		= "gpt4_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT4_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT4_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt5_ick = {
	.name		= "gpt5_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt5_fck = {
	.name		= "gpt5_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT5_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT5_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt6_ick = {
	.name		= "gpt6_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT6_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt6_fck = {
	.name		= "gpt6_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT6_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT6_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt7_ick = {
	.name		= "gpt7_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT7_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt7_fck = {
	.name		= "gpt7_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT7_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT7_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt8_ick = {
	.name		= "gpt8_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT8_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt8_fck = {
	.name		= "gpt8_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT8_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT8_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt9_ick = {
	.name		= "gpt9_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT9_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt9_fck = {
	.name		= "gpt9_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT9_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT9_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt10_ick = {
	.name		= "gpt10_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT10_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt10_fck = {
	.name		= "gpt10_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT10_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT10_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt11_ick = {
	.name		= "gpt11_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT11_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt11_fck = {
	.name		= "gpt11_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT11_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT11_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt12_ick = {
	.name		= "gpt12_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT12_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt12_fck = {
	.name		= "gpt12_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT12_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT12_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp1_ick = {
	.name		= "mcbsp_ick",
	.id		= 1,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp1_fck = {
	.name		= "mcbsp_fck",
	.id		= 1,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp2_ick = {
	.name		= "mcbsp_ick",
	.id		= 2,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp2_fck = {
	.name		= "mcbsp_fck",
	.id		= 2,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp3_ick = {
	.name		= "mcbsp_ick",
	.id		= 3,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp3_fck = {
	.name		= "mcbsp_fck",
	.id		= 3,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp4_ick = {
	.name		= "mcbsp_ick",
	.id		= 4,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp4_fck = {
	.name		= "mcbsp_fck",
	.id		= 4,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp5_ick = {
	.name		= "mcbsp_ick",
	.id		= 5,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp5_fck = {
	.name		= "mcbsp_fck",
	.id		= 5,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi1_ick = {
	.name		= "mcspi_ick",
	.id		= 1,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi1_fck = {
	.name		= "mcspi_fck",
	.id		= 1,
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi2_ick = {
	.name		= "mcspi_ick",
	.id		= 2,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi2_fck = {
	.name		= "mcspi_fck",
	.id		= 2,
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi3_ick = {
	.name		= "mcspi_ick",
	.id		= 3,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCSPI3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi3_fck = {
	.name		= "mcspi_fck",
	.id		= 3,
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCSPI3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart1_ick = {
	.name		= "uart1_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart1_fck = {
	.name		= "uart1_fck",
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart2_ick = {
	.name		= "uart2_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart2_fck = {
	.name		= "uart2_fck",
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart3_ick = {
	.name		= "uart3_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart3_fck = {
	.name		= "uart3_fck",
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpios_ick = {
	.name		= "gpios_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpios_fck = {
	.name		= "gpios_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mpu_wdt_ick = {
	.name		= "mpu_wdt_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mpu_wdt_fck = {
	.name		= "mpu_wdt_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sync_32k_ick = {
	.name		= "sync_32k_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ENABLE_ON_INIT,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_32KSYNC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt1_ick = {
	.name		= "wdt1_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_WDT1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk omapctrl_ick = {
	.name		= "omapctrl_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ENABLE_ON_INIT,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_OMAPCTRL_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk icr_ick = {
	.name		= "icr_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP2430_EN_ICR_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk cam_ick = {
	.name		= "cam_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * cam_fck controls both CAM_MCLK and CAM_FCLK.  It should probably be
 * split into two separate clocks, since the parent clocks are different
 * and the clockdomains are also different.
 */
static struct clk cam_fck = {
	.name		= "cam_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mailboxes_ick = {
	.name		= "mailboxes_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MAILBOXES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt4_ick = {
	.name		= "wdt4_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt4_fck = {
	.name		= "wdt4_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt3_ick = {
	.name		= "wdt3_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_WDT3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt3_fck = {
	.name		= "wdt3_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_WDT3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mspro_ick = {
	.name		= "mspro_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mspro_fck = {
	.name		= "mspro_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmc_ick = {
	.name		= "mmc_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_MMC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmc_fck = {
	.name		= "mmc_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_MMC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk fac_ick = {
	.name		= "fac_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk fac_fck = {
	.name		= "fac_fck",
	.parent		= &func_12m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk eac_ick = {
	.name		= "eac_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_EAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk eac_fck = {
	.name		= "eac_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_EAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hdq_ick = {
	.name		= "hdq_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hdq_fck = {
	.name		= "hdq_fck",
	.parent		= &func_12m_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2c2_ick = {
	.name		= "i2c_ick",
	.id		= 2,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2c2_fck = {
	.name		= "i2c_fck",
	.id		= 2,
	.parent		= &func_12m_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_I2C2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2chs2_fck = {
	.name		= "i2c_fck",
	.id		= 2,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_I2CHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2c1_ick = {
	.name		= "i2c_ick",
	.id		= 1,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2c1_fck = {
	.name		= "i2c_fck",
	.id		= 1,
	.parent		= &func_12m_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_I2C1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2chs1_fck = {
	.name		= "i2c_fck",
	.id		= 1,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_I2CHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpmc_fck = {
	.name		= "gpmc_fck",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk sdma_fck = {
	.name		= "sdma_fck",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk sdma_ick = {
	.name		= "sdma_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk vlynq_ick = {
	.name		= "vlynq_ick",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_VLYNQ_SHIFT,
	.recalc		= &followparent_recalc,
};

static const struct clksel_rate vlynq_fck_96m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_242X | DEFAULT_RATE },
	{ .div = 0 }
};

static const struct clksel_rate vlynq_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_242X },
	{ .div = 2, .val = 2, .flags = RATE_IN_242X },
	{ .div = 3, .val = 3, .flags = RATE_IN_242X },
	{ .div = 4, .val = 4, .flags = RATE_IN_242X },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 9, .val = 9, .flags = RATE_IN_242X },
	{ .div = 12, .val = 12, .flags = RATE_IN_242X },
	{ .div = 16, .val = 16, .flags = RATE_IN_242X | DEFAULT_RATE },
	{ .div = 18, .val = 18, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel vlynq_fck_clksel[] = {
	{ .parent = &func_96m_ck, .rates = vlynq_fck_96m_rates },
	{ .parent = &core_ck,	  .rates = vlynq_fck_core_rates },
	{ .parent = NULL }
};

static struct clk vlynq_fck = {
	.name		= "vlynq_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP242X | DELAYED_APP,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_VLYNQ_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP2420_CLKSEL_VLYNQ_MASK,
	.clksel		= vlynq_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk sdrc_ick = {
	.name		= "sdrc_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | ENABLE_ON_INIT,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP2430_EN_SDRC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk des_ick = {
	.name		= "des_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_DES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sha_ick = {
	.name		= "sha_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_SHA_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk rng_ick = {
	.name		= "rng_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_RNG_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk aes_ick = {
	.name		= "aes_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_AES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk pka_ick = {
	.name		= "pka_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_PKA_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk usb_fck = {
	.name		= "usb_fck",
	.parent		= &func_48m_ck,
	.flags		= CLOCK_IN_OMAP243X | CLOCK_IN_OMAP242X,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_USB_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk usbhs_ick = {
	.name		= "usbhs_ick",
	.parent		= &core_l3_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_USBHS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_ick = {
	.name		= "mmchs_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_fck = {
	.name		= "mmchs_fck",
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_ick = {
	.name		= "mmchs_ick",
	.id		= 1,
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_fck = {
	.name		= "mmchs_fck",
	.id		= 1,
	.parent		= &func_96m_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_ick = {
	.name		= "gpio5_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_GPIO5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_fck = {
	.name		= "gpio5_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_GPIO5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mdm_intc_ick = {
	.name		= "mdm_intc_ick",
	.parent		= &l4_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MDM_INTC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchsdb1_fck = {
	.name		= "mmchsdb_fck",
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHSDB1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchsdb2_fck = {
	.name		= "mmchsdb_fck",
	.id		= 1,
	.parent		= &func_32k_ck,
	.flags		= CLOCK_IN_OMAP243X,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHSDB2_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * This clock is a composite clock which does entire set changes then
 * forces a rebalance. It keys on the MPU speed, but it really could
 * be any key speed part of a set in the rate table.
 *
 * to really change a set, you need memory table sets which get changed
 * in sram, pre-notifiers & post notifiers, changing the top set, without
 * having low level display recalc's won't work... this is why dpm notifiers
 * work, isr's off, walk a list of clocks already _off_ and not messing with
 * the bus.
 *
 * This clock should have no parent. It embodies the entire upper level
 * active set. A parent will mess up some of the init also.
 */
static struct clk virt_prcm_set = {
	.name		= "virt_prcm_set",
	.flags		= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X |
				VIRTUAL_CLOCK | ALWAYS_ENABLED | DELAYED_APP,
	.parent		= &mpu_ck,	/* Indexed by mpu speed, no parent */
	.recalc		= &omap2_table_mpu_recalc,	/* sets are keyed on mpu rate */
	.set_rate	= &omap2_select_table_rate,
	.round_rate	= &omap2_round_to_table_rate,
};

static struct clk *onchip_24xx_clks[] __initdata = {
	/* external root sources */
	&func_32k_ck,
	&osc_ck,
	&sys_ck,
	&alt_ck,
	/* internal analog sources */
	&dpll_ck,
	&apll96_ck,
	&apll54_ck,
	/* internal prcm root sources */
	&func_54m_ck,
	&core_ck,
	&func_96m_ck,
	&func_48m_ck,
	&func_12m_ck,
	&wdt1_osc_ck,
	&sys_clkout_src,
	&sys_clkout,
	&sys_clkout2_src,
	&sys_clkout2,
	&emul_ck,
	/* mpu domain clocks */
	&mpu_ck,
	/* dsp domain clocks */
	&dsp_fck,
	&dsp_irate_ick,
	&dsp_ick,		/* 242x */
	&iva2_1_ick,		/* 243x */
	&iva1_ifck,		/* 242x */
	&iva1_mpu_int_ifck,	/* 242x */
	/* GFX domain clocks */
	&gfx_3d_fck,
	&gfx_2d_fck,
	&gfx_ick,
	/* Modem domain clocks */
	&mdm_ick,
	&mdm_osc_ck,
	/* DSS domain clocks */
	&dss_ick,
	&dss1_fck,
	&dss2_fck,
	&dss_54m_fck,
	/* L3 domain clocks */
	&core_l3_ck,
	&ssi_ssr_sst_fck,
	&usb_l4_ick,
	/* L4 domain clocks */
	&l4_ck,			/* used as both core_l4 and wu_l4 */
	/* virtual meta-group clock */
	&virt_prcm_set,
	/* general l4 interface ck, multi-parent functional clk */
	&gpt1_ick,
	&gpt1_fck,
	&gpt2_ick,
	&gpt2_fck,
	&gpt3_ick,
	&gpt3_fck,
	&gpt4_ick,
	&gpt4_fck,
	&gpt5_ick,
	&gpt5_fck,
	&gpt6_ick,
	&gpt6_fck,
	&gpt7_ick,
	&gpt7_fck,
	&gpt8_ick,
	&gpt8_fck,
	&gpt9_ick,
	&gpt9_fck,
	&gpt10_ick,
	&gpt10_fck,
	&gpt11_ick,
	&gpt11_fck,
	&gpt12_ick,
	&gpt12_fck,
	&mcbsp1_ick,
	&mcbsp1_fck,
	&mcbsp2_ick,
	&mcbsp2_fck,
	&mcbsp3_ick,
	&mcbsp3_fck,
	&mcbsp4_ick,
	&mcbsp4_fck,
	&mcbsp5_ick,
	&mcbsp5_fck,
	&mcspi1_ick,
	&mcspi1_fck,
	&mcspi2_ick,
	&mcspi2_fck,
	&mcspi3_ick,
	&mcspi3_fck,
	&uart1_ick,
	&uart1_fck,
	&uart2_ick,
	&uart2_fck,
	&uart3_ick,
	&uart3_fck,
	&gpios_ick,
	&gpios_fck,
	&mpu_wdt_ick,
	&mpu_wdt_fck,
	&sync_32k_ick,
	&wdt1_ick,
	&omapctrl_ick,
	&icr_ick,
	&cam_fck,
	&cam_ick,
	&mailboxes_ick,
	&wdt4_ick,
	&wdt4_fck,
	&wdt3_ick,
	&wdt3_fck,
	&mspro_ick,
	&mspro_fck,
	&mmc_ick,
	&mmc_fck,
	&fac_ick,
	&fac_fck,
	&eac_ick,
	&eac_fck,
	&hdq_ick,
	&hdq_fck,
	&i2c1_ick,
	&i2c1_fck,
	&i2chs1_fck,
	&i2c2_ick,
	&i2c2_fck,
	&i2chs2_fck,
	&gpmc_fck,
	&sdma_fck,
	&sdma_ick,
	&vlynq_ick,
	&vlynq_fck,
	&sdrc_ick,
	&des_ick,
	&sha_ick,
	&rng_ick,
	&aes_ick,
	&pka_ick,
	&usb_fck,
	&usbhs_ick,
	&mmchs1_ick,
	&mmchs1_fck,
	&mmchs2_ick,
	&mmchs2_fck,
	&gpio5_ick,
	&gpio5_fck,
	&mdm_intc_ick,
	&mmchsdb1_fck,
	&mmchsdb2_fck,
};

#endif

