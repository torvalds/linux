#ifndef __ARCH_ARM_MACH_OMAP2_PRM_REGBITS_24XX_H
#define __ARCH_ARM_MACH_OMAP2_PRM_REGBITS_24XX_H

/*
 * OMAP24XX Power/Reset Management register bits
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "prm.h"

/* Bits shared between registers */

/* PRCM_IRQSTATUS_MPU, PM_IRQSTATUS_DSP, PRCM_IRQSTATUS_IVA shared bits */
#define OMAP24XX_VOLTTRANS_ST_MASK			(1 << 2)
#define OMAP24XX_WKUP2_ST_MASK				(1 << 1)
#define OMAP24XX_WKUP1_ST_MASK				(1 << 0)

/* PRCM_IRQENABLE_MPU, PM_IRQENABLE_DSP, PRCM_IRQENABLE_IVA shared bits */
#define OMAP24XX_VOLTTRANS_EN_MASK			(1 << 2)
#define OMAP24XX_WKUP2_EN_MASK				(1 << 1)
#define OMAP24XX_WKUP1_EN_MASK				(1 << 0)

/* PM_WKDEP_GFX, PM_WKDEP_MPU, PM_WKDEP_DSP, PM_WKDEP_MDM shared bits */
#define OMAP24XX_EN_MPU_SHIFT				1
#define OMAP24XX_EN_MPU_MASK				(1 << 1)
#define OMAP24XX_EN_CORE_SHIFT 				0
#define OMAP24XX_EN_CORE_MASK				(1 << 0)

/*
 * PM_PWSTCTRL_MPU, PM_PWSTCTRL_GFX, PM_PWSTCTRL_DSP, PM_PWSTCTRL_MDM
 * shared bits
 */
#define OMAP24XX_MEMONSTATE_SHIFT			10
#define OMAP24XX_MEMONSTATE_MASK			(0x3 << 10)
#define OMAP24XX_MEMRETSTATE_MASK			(1 << 3)

/* PM_PWSTCTRL_GFX, PM_PWSTCTRL_DSP, PM_PWSTCTRL_MDM shared bits */
#define OMAP24XX_FORCESTATE_MASK			(1 << 18)

/*
 * PM_PWSTST_CORE, PM_PWSTST_GFX, PM_PWSTST_MPU, PM_PWSTST_DSP,
 * PM_PWSTST_MDM shared bits
 */
#define OMAP24XX_CLKACTIVITY_MASK			(1 << 19)

/* PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_DSP shared bits */
#define OMAP24XX_LASTSTATEENTERED_SHIFT			4
#define OMAP24XX_LASTSTATEENTERED_MASK			(0x3 << 4)

/* PM_PWSTST_MPU and PM_PWSTST_DSP shared bits */
#define OMAP2430_MEMSTATEST_SHIFT			10
#define OMAP2430_MEMSTATEST_MASK			(0x3 << 10)

/* PM_PWSTST_GFX, PM_PWSTST_DSP, PM_PWSTST_MDM shared bits */
#define OMAP24XX_POWERSTATEST_SHIFT			0
#define OMAP24XX_POWERSTATEST_MASK			(0x3 << 0)


/* Bits specific to each register */

/* PRCM_REVISION */
#define OMAP24XX_REV_SHIFT				0
#define OMAP24XX_REV_MASK				(0xff << 0)

/* PRCM_SYSCONFIG */
#define OMAP24XX_AUTOIDLE_MASK				(1 << 0)

/* PRCM_IRQSTATUS_MPU specific bits */
#define OMAP2430_DPLL_RECAL_ST_MASK			(1 << 6)
#define OMAP24XX_TRANSITION_ST_MASK			(1 << 5)
#define OMAP24XX_EVGENOFF_ST_MASK			(1 << 4)
#define OMAP24XX_EVGENON_ST_MASK			(1 << 3)

/* PRCM_IRQENABLE_MPU specific bits */
#define OMAP2430_DPLL_RECAL_EN_MASK			(1 << 6)
#define OMAP24XX_TRANSITION_EN_MASK			(1 << 5)
#define OMAP24XX_EVGENOFF_EN_MASK			(1 << 4)
#define OMAP24XX_EVGENON_EN_MASK			(1 << 3)

/* PRCM_VOLTCTRL */
#define OMAP24XX_AUTO_EXTVOLT_MASK			(1 << 15)
#define OMAP24XX_FORCE_EXTVOLT_MASK			(1 << 14)
#define OMAP24XX_SETOFF_LEVEL_SHIFT			12
#define OMAP24XX_SETOFF_LEVEL_MASK			(0x3 << 12)
#define OMAP24XX_MEMRETCTRL_MASK			(1 << 8)
#define OMAP24XX_SETRET_LEVEL_SHIFT			6
#define OMAP24XX_SETRET_LEVEL_MASK			(0x3 << 6)
#define OMAP24XX_VOLT_LEVEL_SHIFT			0
#define OMAP24XX_VOLT_LEVEL_MASK			(0x3 << 0)

/* PRCM_VOLTST */
#define OMAP24XX_ST_VOLTLEVEL_SHIFT			0
#define OMAP24XX_ST_VOLTLEVEL_MASK			(0x3 << 0)

/* PRCM_CLKSRC_CTRL specific bits */

/* PRCM_CLKOUT_CTRL */
#define OMAP2420_CLKOUT2_EN_SHIFT			15
#define OMAP2420_CLKOUT2_EN_MASK			(1 << 15)
#define OMAP2420_CLKOUT2_DIV_SHIFT			11
#define OMAP2420_CLKOUT2_DIV_MASK			(0x7 << 11)
#define OMAP2420_CLKOUT2_SOURCE_SHIFT			8
#define OMAP2420_CLKOUT2_SOURCE_MASK			(0x3 << 8)
#define OMAP24XX_CLKOUT_EN_SHIFT			7
#define OMAP24XX_CLKOUT_EN_MASK				(1 << 7)
#define OMAP24XX_CLKOUT_DIV_SHIFT			3
#define OMAP24XX_CLKOUT_DIV_MASK			(0x7 << 3)
#define OMAP24XX_CLKOUT_SOURCE_SHIFT			0
#define OMAP24XX_CLKOUT_SOURCE_MASK			(0x3 << 0)

/* PRCM_CLKEMUL_CTRL */
#define OMAP24XX_EMULATION_EN_SHIFT			0
#define OMAP24XX_EMULATION_EN_MASK			(1 << 0)

/* PRCM_CLKCFG_CTRL */
#define OMAP24XX_VALID_CONFIG_MASK			(1 << 0)

/* PRCM_CLKCFG_STATUS */
#define OMAP24XX_CONFIG_STATUS_MASK			(1 << 0)

/* PRCM_VOLTSETUP specific bits */

/* PRCM_CLKSSETUP specific bits */

/* PRCM_POLCTRL */
#define OMAP2420_CLKOUT2_POL_MASK			(1 << 10)
#define OMAP24XX_CLKOUT_POL_MASK			(1 << 9)
#define OMAP24XX_CLKREQ_POL_MASK			(1 << 8)
#define OMAP2430_USE_POWEROK_MASK			(1 << 2)
#define OMAP2430_POWEROK_POL_MASK			(1 << 1)
#define OMAP24XX_EXTVOL_POL_MASK			(1 << 0)

/* RM_RSTST_MPU specific bits */
/* 2430 calls GLOBALWMPU_RST "GLOBALWARM_RST" instead */

/* PM_WKDEP_MPU specific bits */
#define OMAP2430_PM_WKDEP_MPU_EN_MDM_SHIFT		5
#define OMAP2430_PM_WKDEP_MPU_EN_MDM_MASK		(1 << 5)
#define OMAP24XX_PM_WKDEP_MPU_EN_DSP_SHIFT		2
#define OMAP24XX_PM_WKDEP_MPU_EN_DSP_MASK		(1 << 2)

/* PM_EVGENCTRL_MPU specific bits */

/* PM_EVEGENONTIM_MPU specific bits */

/* PM_EVEGENOFFTIM_MPU specific bits */

/* PM_PWSTCTRL_MPU specific bits */
#define OMAP2430_FORCESTATE_MASK			(1 << 18)

/* PM_PWSTST_MPU specific bits */
/* INTRANSITION, CLKACTIVITY, POWERSTATE, MEMSTATEST are 2430 only */

/* PM_WKEN1_CORE specific bits */

/* PM_WKEN2_CORE specific bits */

/* PM_WKST1_CORE specific bits*/

/* PM_WKST2_CORE specific bits */

/* PM_WKDEP_CORE specific bits*/
#define OMAP2430_PM_WKDEP_CORE_EN_MDM_MASK		(1 << 5)
#define OMAP24XX_PM_WKDEP_CORE_EN_GFX_MASK		(1 << 3)
#define OMAP24XX_PM_WKDEP_CORE_EN_DSP_MASK		(1 << 2)

/* PM_PWSTCTRL_CORE specific bits */
#define OMAP24XX_MEMORYCHANGE_MASK			(1 << 20)
#define OMAP24XX_MEM3ONSTATE_SHIFT			14
#define OMAP24XX_MEM3ONSTATE_MASK			(0x3 << 14)
#define OMAP24XX_MEM2ONSTATE_SHIFT			12
#define OMAP24XX_MEM2ONSTATE_MASK			(0x3 << 12)
#define OMAP24XX_MEM1ONSTATE_SHIFT			10
#define OMAP24XX_MEM1ONSTATE_MASK			(0x3 << 10)
#define OMAP24XX_MEM3RETSTATE_MASK			(1 << 5)
#define OMAP24XX_MEM2RETSTATE_MASK			(1 << 4)
#define OMAP24XX_MEM1RETSTATE_MASK			(1 << 3)

/* PM_PWSTST_CORE specific bits */
#define OMAP24XX_MEM3STATEST_SHIFT			14
#define OMAP24XX_MEM3STATEST_MASK			(0x3 << 14)
#define OMAP24XX_MEM2STATEST_SHIFT			12
#define OMAP24XX_MEM2STATEST_MASK			(0x3 << 12)
#define OMAP24XX_MEM1STATEST_SHIFT			10
#define OMAP24XX_MEM1STATEST_MASK			(0x3 << 10)

/* RM_RSTCTRL_GFX */
#define OMAP24XX_GFX_RST_MASK				(1 << 0)

/* RM_RSTST_GFX specific bits */
#define OMAP24XX_GFX_SW_RST_MASK			(1 << 4)

/* PM_PWSTCTRL_GFX specific bits */

/* PM_WKDEP_GFX specific bits */
/* 2430 often calls EN_WAKEUP "EN_WKUP" */

/* RM_RSTCTRL_WKUP specific bits */

/* RM_RSTTIME_WKUP specific bits */

/* RM_RSTST_WKUP specific bits */
/* 2430 calls EXTWMPU_RST "EXTWARM_RST" and GLOBALWMPU_RST "GLOBALWARM_RST" */
#define OMAP24XX_EXTWMPU_RST_MASK			(1 << 6)
#define OMAP24XX_SECU_WD_RST_MASK			(1 << 5)
#define OMAP24XX_MPU_WD_RST_MASK			(1 << 4)
#define OMAP24XX_SECU_VIOL_RST_MASK			(1 << 3)

/* PM_WKEN_WKUP specific bits */

/* PM_WKST_WKUP specific bits */

/* RM_RSTCTRL_DSP */
#define OMAP2420_RST_IVA_MASK				(1 << 8)
#define OMAP24XX_RST2_DSP_MASK				(1 << 1)
#define OMAP24XX_RST1_DSP_MASK				(1 << 0)

/* RM_RSTST_DSP specific bits */
/* 2430 calls GLOBALWMPU_RST "GLOBALWARM_RST" */
#define OMAP2420_IVA_SW_RST_MASK			(1 << 8)
#define OMAP24XX_DSP_SW_RST2_MASK			(1 << 5)
#define OMAP24XX_DSP_SW_RST1_MASK			(1 << 4)

/* PM_WKDEP_DSP specific bits */

/* PM_PWSTCTRL_DSP specific bits */
/* 2430 only: MEMONSTATE, MEMRETSTATE */
#define OMAP2420_MEMIONSTATE_SHIFT			12
#define OMAP2420_MEMIONSTATE_MASK			(0x3 << 12)
#define OMAP2420_MEMIRETSTATE_MASK			(1 << 4)

/* PM_PWSTST_DSP specific bits */
/* MEMSTATEST is 2430 only */
#define OMAP2420_MEMISTATEST_SHIFT			12
#define OMAP2420_MEMISTATEST_MASK			(0x3 << 12)

/* PRCM_IRQSTATUS_DSP specific bits */

/* PRCM_IRQENABLE_DSP specific bits */

/* RM_RSTCTRL_MDM */
/* 2430 only */
#define OMAP2430_PWRON1_MDM_MASK			(1 << 1)
#define OMAP2430_RST1_MDM_MASK				(1 << 0)

/* RM_RSTST_MDM specific bits */
/* 2430 only */
#define OMAP2430_MDM_SECU_VIOL_MASK			(1 << 6)
#define OMAP2430_MDM_SW_PWRON1_MASK			(1 << 5)
#define OMAP2430_MDM_SW_RST1_MASK			(1 << 4)

/* PM_WKEN_MDM */
/* 2430 only */
#define OMAP2430_PM_WKEN_MDM_EN_MDM_MASK		(1 << 0)

/* PM_WKST_MDM specific bits */
/* 2430 only */

/* PM_WKDEP_MDM specific bits */
/* 2430 only */

/* PM_PWSTCTRL_MDM specific bits */
/* 2430 only */
#define OMAP2430_KILLDOMAINWKUP_MASK			(1 << 19)

/* PM_PWSTST_MDM specific bits */
/* 2430 only */

/* PRCM_IRQSTATUS_IVA */
/* 2420 only */

/* PRCM_IRQENABLE_IVA */
/* 2420 only */

#endif
