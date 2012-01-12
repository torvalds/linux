/*
 * opp2xxx.h - macros for old-style OMAP2xxx "OPP" definitions
 *
 * Copyright (C) 2005-2009 Texas Instruments, Inc.
 * Copyright (C) 2004-2009 Nokia Corporation
 *
 * Richard Woodruff <r-woodruff2@ti.com>
 *
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
 *
 * XXX Missing voltage data.
 *
 * THe format described in this file is deprecated.  Once a reasonable
 * OPP API exists, the data in this file should be converted to use it.
 *
 * This is technically part of the OMAP2xxx clock code.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_OPP2XXX_H
#define __ARCH_ARM_MACH_OMAP2_OPP2XXX_H

/**
 * struct prcm_config - define clock rates on a per-OPP basis (24xx)
 *
 * Key dividers which make up a PRCM set. Ratio's for a PRCM are mandated.
 * xtal_speed, dpll_speed, mpu_speed, CM_CLKSEL_MPU,CM_CLKSEL_DSP
 * CM_CLKSEL_GFX, CM_CLKSEL1_CORE, CM_CLKSEL1_PLL CM_CLKSEL2_PLL, CM_CLKSEL_MDM
 *
 * This is deprecated.  As soon as we have a decent OPP API, we should
 * move all this stuff to it.
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
	unsigned short flags;
};


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
#define R1_CM_CLKSEL1_CORE_VAL		(R1_CLKSEL_USB | RX_CLKSEL_SSI | \
					 RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					 R1_CLKSEL_L4 | R1_CLKSEL_L3)
#define R1_CLKSEL_MPU			(2 << 0)
#define R1_CM_CLKSEL_MPU_VAL		R1_CLKSEL_MPU
#define R1_CLKSEL_DSP			(2 << 0)
#define R1_CLKSEL_DSP_IF		(2 << 5)
#define R1_CM_CLKSEL_DSP_VAL		(R1_CLKSEL_DSP | R1_CLKSEL_DSP_IF)
#define R1_CLKSEL_GFX			(2 << 0)
#define R1_CM_CLKSEL_GFX_VAL		R1_CLKSEL_GFX
#define R1_CLKSEL_MDM			(4 << 0)
#define R1_CM_CLKSEL_MDM_VAL		R1_CLKSEL_MDM

/* 2430-Ratio Config 2 */
#define R2_CLKSEL_L3			(6 << 0)
#define R2_CLKSEL_L4			(2 << 5)
#define R2_CLKSEL_USB			(2 << 25)
#define R2_CM_CLKSEL1_CORE_VAL		(R2_CLKSEL_USB | RX_CLKSEL_SSI | \
					 RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					 R2_CLKSEL_L4 | R2_CLKSEL_L3)
#define R2_CLKSEL_MPU			(2 << 0)
#define R2_CM_CLKSEL_MPU_VAL		R2_CLKSEL_MPU
#define R2_CLKSEL_DSP			(2 << 0)
#define R2_CLKSEL_DSP_IF		(3 << 5)
#define R2_CM_CLKSEL_DSP_VAL		(R2_CLKSEL_DSP | R2_CLKSEL_DSP_IF)
#define R2_CLKSEL_GFX			(2 << 0)
#define R2_CM_CLKSEL_GFX_VAL		R2_CLKSEL_GFX
#define R2_CLKSEL_MDM			(6 << 0)
#define R2_CM_CLKSEL_MDM_VAL		R2_CLKSEL_MDM

/* 2430-Ratio Bootm (BYPASS) */
#define RB_CLKSEL_L3			(1 << 0)
#define RB_CLKSEL_L4			(1 << 5)
#define RB_CLKSEL_USB			(1 << 25)
#define RB_CM_CLKSEL1_CORE_VAL		(RB_CLKSEL_USB | RX_CLKSEL_SSI | \
					 RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					 RB_CLKSEL_L4 | RB_CLKSEL_L3)
#define RB_CLKSEL_MPU			(1 << 0)
#define RB_CM_CLKSEL_MPU_VAL		RB_CLKSEL_MPU
#define RB_CLKSEL_DSP			(1 << 0)
#define RB_CLKSEL_DSP_IF		(1 << 5)
#define RB_CM_CLKSEL_DSP_VAL		(RB_CLKSEL_DSP | RB_CLKSEL_DSP_IF)
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
#define RIII_CM_CLKSEL1_CORE_VAL	(RIII_CLKSEL_USB | RXX_CLKSEL_SSI | \
					 RXX_CLKSEL_VLYNQ | RX_CLKSEL_DSS2 | \
					 RX_CLKSEL_DSS1 | RIII_CLKSEL_L4 | \
					 RIII_CLKSEL_L3)
#define RIII_CLKSEL_MPU			(2 << 0)	/* 266MHz */
#define RIII_CM_CLKSEL_MPU_VAL		RIII_CLKSEL_MPU
#define RIII_CLKSEL_DSP			(3 << 0)	/* c5x - 177.3MHz */
#define RIII_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 88.67MHz */
#define RIII_SYNC_DSP			(1 << 7)	/* Enable sync */
#define RIII_CLKSEL_IVA			(6 << 8)	/* iva1 - 88.67MHz */
#define RIII_SYNC_IVA			(1 << 13)	/* Enable sync */
#define RIII_CM_CLKSEL_DSP_VAL		(RIII_SYNC_IVA | RIII_CLKSEL_IVA | \
					 RIII_SYNC_DSP | RIII_CLKSEL_DSP_IF | \
					 RIII_CLKSEL_DSP)
#define RIII_CLKSEL_GFX			(2 << 0)	/* 66.5MHz */
#define RIII_CM_CLKSEL_GFX_VAL		RIII_CLKSEL_GFX

/* 2420-PRCM II 600MHz core */
#define RII_CLKSEL_L3			(6 << 0)	/* 100MHz */
#define RII_CLKSEL_L4			(2 << 5)	/* 50MHz */
#define RII_CLKSEL_USB			(2 << 25)	/* 50MHz */
#define RII_CM_CLKSEL1_CORE_VAL		(RII_CLKSEL_USB | RXX_CLKSEL_SSI | \
					 RXX_CLKSEL_VLYNQ | RX_CLKSEL_DSS2 | \
					 RX_CLKSEL_DSS1 | RII_CLKSEL_L4 | \
					 RII_CLKSEL_L3)
#define RII_CLKSEL_MPU			(2 << 0)	/* 300MHz */
#define RII_CM_CLKSEL_MPU_VAL		RII_CLKSEL_MPU
#define RII_CLKSEL_DSP			(3 << 0)	/* c5x - 200MHz */
#define RII_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 100MHz */
#define RII_SYNC_DSP			(0 << 7)	/* Bypass sync */
#define RII_CLKSEL_IVA			(3 << 8)	/* iva1 - 200MHz */
#define RII_SYNC_IVA			(0 << 13)	/* Bypass sync */
#define RII_CM_CLKSEL_DSP_VAL		(RII_SYNC_IVA | RII_CLKSEL_IVA | \
					 RII_SYNC_DSP | RII_CLKSEL_DSP_IF | \
					 RII_CLKSEL_DSP)
#define RII_CLKSEL_GFX			(2 << 0)	/* 50MHz */
#define RII_CM_CLKSEL_GFX_VAL		RII_CLKSEL_GFX

/* 2420-PRCM I 660MHz core */
#define RI_CLKSEL_L3			(4 << 0)	/* 165MHz */
#define RI_CLKSEL_L4			(2 << 5)	/* 82.5MHz */
#define RI_CLKSEL_USB			(4 << 25)	/* 41.25MHz */
#define RI_CM_CLKSEL1_CORE_VAL		(RI_CLKSEL_USB |		\
					 RXX_CLKSEL_SSI | RXX_CLKSEL_VLYNQ | \
					 RX_CLKSEL_DSS2 | RX_CLKSEL_DSS1 | \
					 RI_CLKSEL_L4 | RI_CLKSEL_L3)
#define RI_CLKSEL_MPU			(2 << 0)	/* 330MHz */
#define RI_CM_CLKSEL_MPU_VAL		RI_CLKSEL_MPU
#define RI_CLKSEL_DSP			(3 << 0)	/* c5x - 220MHz */
#define RI_CLKSEL_DSP_IF		(2 << 5)	/* c5x - 110MHz */
#define RI_SYNC_DSP			(1 << 7)	/* Activate sync */
#define RI_CLKSEL_IVA			(4 << 8)	/* iva1 - 165MHz */
#define RI_SYNC_IVA			(0 << 13)	/* Bypass sync */
#define RI_CM_CLKSEL_DSP_VAL		(RI_SYNC_IVA | RI_CLKSEL_IVA |	\
					 RI_SYNC_DSP | RI_CLKSEL_DSP_IF | \
					 RI_CLKSEL_DSP)
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

#define RVII_CM_CLKSEL1_CORE_VAL	(RVII_CLKSEL_USB | RVII_CLKSEL_SSI | \
					 RVII_CLKSEL_VLYNQ | \
					 RVII_CLKSEL_DSS2 | RVII_CLKSEL_DSS1 | \
					 RVII_CLKSEL_L4 | RVII_CLKSEL_L3)

#define RVII_CLKSEL_MPU			(1 << 0) /* all divide by 1 */
#define RVII_CM_CLKSEL_MPU_VAL		RVII_CLKSEL_MPU

#define RVII_CLKSEL_DSP			(1 << 0)
#define RVII_CLKSEL_DSP_IF		(1 << 5)
#define RVII_SYNC_DSP			(0 << 7)
#define RVII_CLKSEL_IVA			(1 << 8)
#define RVII_SYNC_IVA			(0 << 13)
#define RVII_CM_CLKSEL_DSP_VAL		(RVII_SYNC_IVA | RVII_CLKSEL_IVA | \
					 RVII_SYNC_DSP | RVII_CLKSEL_DSP_IF | \
					 RVII_CLKSEL_DSP)

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
#define M5A_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5A_DPLL_DIV_12 | M5A_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)
#define M5A_DPLL_MULT_13		(61 << 12)
#define M5A_DPLL_DIV_13			(2 << 8)
#define M5A_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5A_DPLL_DIV_13 | M5A_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)
#define M5A_DPLL_MULT_19		(55 << 12)
#define M5A_DPLL_DIV_19			(3 << 8)
#define M5A_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5A_DPLL_DIV_19 | M5A_DPLL_MULT_19 | \
					 MX_APLLS_CLIKIN_19_2)
/* #5b	(ratio1) target DPLL = 200*2 = 400MHz */
#define M5B_DPLL_MULT_12		(50 << 12)
#define M5B_DPLL_DIV_12			(2 << 8)
#define M5B_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5B_DPLL_DIV_12 | M5B_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)
#define M5B_DPLL_MULT_13		(200 << 12)
#define M5B_DPLL_DIV_13			(12 << 8)

#define M5B_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5B_DPLL_DIV_13 | M5B_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)
#define M5B_DPLL_MULT_19		(125 << 12)
#define M5B_DPLL_DIV_19			(31 << 8)
#define M5B_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M5B_DPLL_DIV_19 | M5B_DPLL_MULT_19 | \
					 MX_APLLS_CLIKIN_19_2)
/*
 * #4	(ratio2), DPLL = 399*2 = 798MHz, L3=133MHz
 */
#define M4_DPLL_MULT_12			(133 << 12)
#define M4_DPLL_DIV_12			(3 << 8)
#define M4_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M4_DPLL_DIV_12 | M4_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)

#define M4_DPLL_MULT_13			(399 << 12)
#define M4_DPLL_DIV_13			(12 << 8)
#define M4_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M4_DPLL_DIV_13 | M4_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)

#define M4_DPLL_MULT_19			(145 << 12)
#define M4_DPLL_DIV_19			(6 << 8)
#define M4_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M4_DPLL_DIV_19 | M4_DPLL_MULT_19 | \
					 MX_APLLS_CLIKIN_19_2)

/*
 * #3	(ratio2) baseport-target, target DPLL = 330*2 = 660MHz
 */
#define M3_DPLL_MULT_12			(55 << 12)
#define M3_DPLL_DIV_12			(1 << 8)
#define M3_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M3_DPLL_DIV_12 | M3_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)
#define M3_DPLL_MULT_13			(76 << 12)
#define M3_DPLL_DIV_13			(2 << 8)
#define M3_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M3_DPLL_DIV_13 | M3_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)
#define M3_DPLL_MULT_19			(17 << 12)
#define M3_DPLL_DIV_19			(0 << 8)
#define M3_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M3_DPLL_DIV_19 | M3_DPLL_MULT_19 | \
					 MX_APLLS_CLIKIN_19_2)

/*
 * #2   (ratio1) DPLL = 330*2 = 660MHz, L3=165MHz
 */
#define M2_DPLL_MULT_12		        (55 << 12)
#define M2_DPLL_DIV_12		        (1 << 8)
#define M2_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M2_DPLL_DIV_12 | M2_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)

/* Speed changes - Used 658.7MHz instead of 660MHz for LP-Refresh M=76 N=2,
 * relock time issue */
/* Core frequency changed from 330/165 to 329/164 MHz*/
#define M2_DPLL_MULT_13		        (76 << 12)
#define M2_DPLL_DIV_13		        (2 << 8)
#define M2_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M2_DPLL_DIV_13 | M2_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)

#define M2_DPLL_MULT_19		        (17 << 12)
#define M2_DPLL_DIV_19		        (0 << 8)
#define M2_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 M2_DPLL_DIV_19 | M2_DPLL_MULT_19 | \
					 MX_APLLS_CLIKIN_19_2)

/* boot (boot) */
#define MB_DPLL_MULT			(1 << 12)
#define MB_DPLL_DIV			(0 << 8)
#define MB_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 MB_DPLL_DIV | MB_DPLL_MULT | \
					 MX_APLLS_CLIKIN_12)

#define MB_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 MB_DPLL_DIV | MB_DPLL_MULT | \
					 MX_APLLS_CLIKIN_13)

#define MB_CM_CLKSEL1_PLL_19_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 MB_DPLL_DIV | MB_DPLL_MULT | \
					 MX_APLLS_CLIKIN_19)

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
#define MI_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC | \
					 MI_DPLL_DIV_12 | MI_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)

/*
 * 2420 Equivalent - mode registers
 * PRCM II , target DPLL = 2*300MHz = 600MHz
 */
#define MII_DPLL_MULT_12		(50 << 12)
#define MII_DPLL_DIV_12			(1 << 8)
#define MII_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC |	\
					 MII_DPLL_DIV_12 | MII_DPLL_MULT_12 | \
					 MX_APLLS_CLIKIN_12)
#define MII_DPLL_MULT_13		(300 << 12)
#define MII_DPLL_DIV_13			(12 << 8)
#define MII_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC |	\
					 MII_DPLL_DIV_13 | MII_DPLL_MULT_13 | \
					 MX_APLLS_CLIKIN_13)

/* PRCM III target DPLL = 2*266 = 532MHz*/
#define MIII_DPLL_MULT_12		(133 << 12)
#define MIII_DPLL_DIV_12		(5 << 8)
#define MIII_CM_CLKSEL1_PLL_12_VAL	(MX_48M_SRC | MX_54M_SRC |	\
					 MIII_DPLL_DIV_12 | \
					 MIII_DPLL_MULT_12 | MX_APLLS_CLIKIN_12)
#define MIII_DPLL_MULT_13		(266 << 12)
#define MIII_DPLL_DIV_13		(12 << 8)
#define MIII_CM_CLKSEL1_PLL_13_VAL	(MX_48M_SRC | MX_54M_SRC |	\
					 MIII_DPLL_DIV_13 | \
					 MIII_DPLL_MULT_13 | MX_APLLS_CLIKIN_13)

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


extern const struct prcm_config omap2420_rate_table[];

#ifdef CONFIG_SOC_OMAP2430
extern const struct prcm_config omap2430_rate_table[];
#else
#define omap2430_rate_table	NULL
#endif
extern const struct prcm_config *rate_table;
extern const struct prcm_config *curr_prcm_set;

#endif
