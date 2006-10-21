/*
 * linux/arch/arm/mach-omap2/pm-domain.c
 *
 * Power domain functions for OMAP2
 *
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * Some code based on earlier OMAP2 sample PM code
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>

#include <asm/io.h>

#include "prcm-regs.h"

/* Power domain offsets */
#define PM_MPU_OFFSET			0x100
#define PM_CORE_OFFSET			0x200
#define PM_GFX_OFFSET			0x300
#define PM_WKUP_OFFSET			0x400		/* Autoidle only */
#define PM_PLL_OFFSET			0x500		/* Autoidle only */
#define PM_DSP_OFFSET			0x800
#define PM_MDM_OFFSET			0xc00

/* Power domain wake-up dependency control register */
#define PM_WKDEP_OFFSET			0xc8
#define		EN_MDM			(1 << 5)
#define		EN_WKUP			(1 << 4)
#define		EN_GFX			(1 << 3)
#define		EN_DSP			(1 << 2)
#define		EN_MPU			(1 << 1)
#define		EN_CORE			(1 << 0)

/* Core power domain state transition control register */
#define PM_PWSTCTRL_OFFSET		0xe0
#define		FORCESTATE		(1 << 18)	/* Only for DSP & GFX */
#define		MEM4RETSTATE		(1 << 6)
#define		MEM3RETSTATE		(1 << 5)
#define		MEM2RETSTATE		(1 << 4)
#define		MEM1RETSTATE		(1 << 3)
#define		LOGICRETSTATE		(1 << 2)	/* Logic is retained */
#define		POWERSTATE_OFF		0x3
#define		POWERSTATE_RETENTION	0x1
#define		POWERSTATE_ON		0x0

/* Power domain state register */
#define PM_PWSTST_OFFSET		0xe4

/* Hardware supervised state transition control register */
#define CM_CLKSTCTRL_OFFSET		0x48
#define		AUTOSTAT_MPU		(1 << 0)	/* MPU */
#define		AUTOSTAT_DSS		(1 << 2)	/* Core */
#define		AUTOSTAT_L4		(1 << 1)	/* Core */
#define		AUTOSTAT_L3		(1 << 0)	/* Core */
#define		AUTOSTAT_GFX		(1 << 0)	/* GFX */
#define		AUTOSTAT_IVA		(1 << 8)	/* 2420 IVA in DSP domain */
#define		AUTOSTAT_DSP		(1 << 0)	/* DSP */
#define		AUTOSTAT_MDM		(1 << 0)	/* MDM */

/* Automatic control of interface clock idling */
#define CM_AUTOIDLE1_OFFSET		0x30
#define CM_AUTOIDLE2_OFFSET		0x34		/* Core only */
#define CM_AUTOIDLE3_OFFSET		0x38		/* Core only */
#define CM_AUTOIDLE4_OFFSET		0x3c		/* Core only */
#define		AUTO_54M(x)		(((x) & 0x3) << 6)
#define		AUTO_96M(x)		(((x) & 0x3) << 2)
#define		AUTO_DPLL(x)		(((x) & 0x3) << 0)
#define		AUTO_STOPPED		0x3
#define		AUTO_BYPASS_FAST	0x2		/* DPLL only */
#define		AUTO_BYPASS_LOW_POWER	0x1		/* DPLL only */
#define		AUTO_DISABLED		0x0

/* Voltage control PRCM_VOLTCTRL bits */
#define		AUTO_EXTVOLT		(1 << 15)
#define		FORCE_EXTVOLT		(1 << 14)
#define		SETOFF_LEVEL(x)		(((x) & 0x3) << 12)
#define		MEMRETCTRL		(1 << 8)
#define		SETRET_LEVEL(x)		(((x) & 0x3) << 6)
#define		VOLT_LEVEL(x)		(((x) & 0x3) << 0)

#define OMAP24XX_PRCM_VBASE	IO_ADDRESS(OMAP24XX_PRCM_BASE)
#define prcm_readl(r)		__raw_readl(OMAP24XX_PRCM_VBASE + (r))
#define prcm_writel(v, r)	__raw_writel((v), OMAP24XX_PRCM_VBASE + (r))

static u32 pmdomain_get_wakeup_dependencies(int domain_offset)
{
	return prcm_readl(domain_offset + PM_WKDEP_OFFSET);
}

static void pmdomain_set_wakeup_dependencies(u32 state, int domain_offset)
{
	prcm_writel(state, domain_offset + PM_WKDEP_OFFSET);
}

static u32 pmdomain_get_powerstate(int domain_offset)
{
	return prcm_readl(domain_offset + PM_PWSTCTRL_OFFSET);
}

static void pmdomain_set_powerstate(u32 state, int domain_offset)
{
	prcm_writel(state, domain_offset + PM_PWSTCTRL_OFFSET);
}

static u32 pmdomain_get_clock_autocontrol(int domain_offset)
{
	return prcm_readl(domain_offset + CM_CLKSTCTRL_OFFSET);
}

static void pmdomain_set_clock_autocontrol(u32 state, int domain_offset)
{
	prcm_writel(state, domain_offset + CM_CLKSTCTRL_OFFSET);
}

static u32 pmdomain_get_clock_autoidle1(int domain_offset)
{
	return prcm_readl(domain_offset + CM_AUTOIDLE1_OFFSET);
}

/* Core domain only */
static u32 pmdomain_get_clock_autoidle2(int domain_offset)
{
	return prcm_readl(domain_offset + CM_AUTOIDLE2_OFFSET);
}

/* Core domain only */
static u32 pmdomain_get_clock_autoidle3(int domain_offset)
{
	return prcm_readl(domain_offset + CM_AUTOIDLE3_OFFSET);
}

/* Core domain only */
static u32 pmdomain_get_clock_autoidle4(int domain_offset)
{
	return prcm_readl(domain_offset + CM_AUTOIDLE4_OFFSET);
}

static void pmdomain_set_clock_autoidle1(u32 state, int domain_offset)
{
	prcm_writel(state, CM_AUTOIDLE1_OFFSET + domain_offset);
}

/* Core domain only */
static void pmdomain_set_clock_autoidle2(u32 state, int domain_offset)
{
	prcm_writel(state, CM_AUTOIDLE2_OFFSET + domain_offset);
}

/* Core domain only */
static void pmdomain_set_clock_autoidle3(u32 state, int domain_offset)
{
	prcm_writel(state, CM_AUTOIDLE3_OFFSET + domain_offset);
}

/* Core domain only */
static void pmdomain_set_clock_autoidle4(u32 state, int domain_offset)
{
	prcm_writel(state, CM_AUTOIDLE4_OFFSET + domain_offset);
}

/*
 * Configures power management domains to idle clocks automatically.
 */
void pmdomain_set_autoidle(void)
{
	u32 val;

	/* Set PLL auto stop for 54M, 96M & DPLL */
	pmdomain_set_clock_autoidle1(AUTO_54M(AUTO_STOPPED) |
				     AUTO_96M(AUTO_STOPPED) |
				     AUTO_DPLL(AUTO_STOPPED), PM_PLL_OFFSET);

	/* External clock input control
	 * REVISIT: Should this be in clock framework?
	 */
	PRCM_CLKSRC_CTRL |= (0x3 << 3);

	/* Configure number of 32KHz clock cycles for sys_clk */
	PRCM_CLKSSETUP = 0x00ff;

	/* Configure automatic voltage transition */
	PRCM_VOLTSETUP = 0;
	val = PRCM_VOLTCTRL;
	val &= ~(SETOFF_LEVEL(0x3) | VOLT_LEVEL(0x3));
	val |= SETOFF_LEVEL(1) | VOLT_LEVEL(1) | AUTO_EXTVOLT;
	PRCM_VOLTCTRL = val;

	/* Disable emulation tools functional clock */
	PRCM_CLKEMUL_CTRL = 0x0;

	/* Set core memory retention state */
	val = pmdomain_get_powerstate(PM_CORE_OFFSET);
	if (cpu_is_omap2420()) {
		val &= ~(0x7 << 3);
		val |= (MEM3RETSTATE | MEM2RETSTATE | MEM1RETSTATE);
	} else {
		val &= ~(0xf << 3);
		val |= (MEM4RETSTATE | MEM3RETSTATE | MEM2RETSTATE |
			MEM1RETSTATE);
	}
	pmdomain_set_powerstate(val, PM_CORE_OFFSET);

	/* OCP interface smart idle. REVISIT: Enable autoidle bit0 ? */
	val = SMS_SYSCONFIG;
	val &= ~(0x3 << 3);
	val |= (0x2 << 3) | (1 << 0);
	SMS_SYSCONFIG |= val;

	val = SDRC_SYSCONFIG;
	val &= ~(0x3 << 3);
	val |= (0x2 << 3);
	SDRC_SYSCONFIG = val;

	/* Configure L3 interface for smart idle.
	 * REVISIT: Enable autoidle bit0 ?
	 */
	val = GPMC_SYSCONFIG;
	val &= ~(0x3 << 3);
	val |= (0x2 << 3) | (1 << 0);
	GPMC_SYSCONFIG = val;

	pmdomain_set_powerstate(LOGICRETSTATE | POWERSTATE_RETENTION,
				PM_MPU_OFFSET);
	pmdomain_set_powerstate(POWERSTATE_RETENTION, PM_CORE_OFFSET);
	if (!cpu_is_omap2420())
		pmdomain_set_powerstate(POWERSTATE_RETENTION, PM_MDM_OFFSET);

	/* Assume suspend function has saved the state for DSP and GFX */
	pmdomain_set_powerstate(FORCESTATE | POWERSTATE_OFF, PM_DSP_OFFSET);
	pmdomain_set_powerstate(FORCESTATE | POWERSTATE_OFF, PM_GFX_OFFSET);

#if 0
	/* REVISIT: Internal USB needs special handling */
	force_standby_usb();
	if (cpu_is_omap2430())
		force_hsmmc();
	sdram_self_refresh_on_idle_req(1);
#endif

	/* Enable clock auto control for all domains.
	 * Note that CORE domain includes also DSS, L4 & L3.
	 */
	pmdomain_set_clock_autocontrol(AUTOSTAT_MPU, PM_MPU_OFFSET);
	pmdomain_set_clock_autocontrol(AUTOSTAT_GFX, PM_GFX_OFFSET);
	pmdomain_set_clock_autocontrol(AUTOSTAT_DSS | AUTOSTAT_L4 | AUTOSTAT_L3,
				       PM_CORE_OFFSET);
	if (cpu_is_omap2420())
		pmdomain_set_clock_autocontrol(AUTOSTAT_IVA | AUTOSTAT_DSP,
					       PM_DSP_OFFSET);
	else {
		pmdomain_set_clock_autocontrol(AUTOSTAT_DSP, PM_DSP_OFFSET);
		pmdomain_set_clock_autocontrol(AUTOSTAT_MDM, PM_MDM_OFFSET);
	}

	/* Enable clock autoidle for all domains */
	pmdomain_set_clock_autoidle1(0x2, PM_DSP_OFFSET);
	if (cpu_is_omap2420()) {
		pmdomain_set_clock_autoidle1(0xfffffff9, PM_CORE_OFFSET);
		pmdomain_set_clock_autoidle2(0x7, PM_CORE_OFFSET);
		pmdomain_set_clock_autoidle1(0x3f, PM_WKUP_OFFSET);
	} else {
		pmdomain_set_clock_autoidle1(0xeafffff1, PM_CORE_OFFSET);
		pmdomain_set_clock_autoidle2(0xfff, PM_CORE_OFFSET);
		pmdomain_set_clock_autoidle1(0x7f, PM_WKUP_OFFSET);
		pmdomain_set_clock_autoidle1(0x3, PM_MDM_OFFSET);
	}
	pmdomain_set_clock_autoidle3(0x7, PM_CORE_OFFSET);
	pmdomain_set_clock_autoidle4(0x1f, PM_CORE_OFFSET);
}

/*
 * Initializes power domains by removing wake-up dependencies and powering
 * down DSP and GFX. Gets called from PM init. Note that DSP and IVA code
 * must re-enable DSP and GFX when used.
 */
void __init pmdomain_init(void)
{
	/* Remove all domain wakeup dependencies */
	pmdomain_set_wakeup_dependencies(EN_WKUP | EN_CORE, PM_MPU_OFFSET);
	pmdomain_set_wakeup_dependencies(0, PM_DSP_OFFSET);
	pmdomain_set_wakeup_dependencies(0, PM_GFX_OFFSET);
	pmdomain_set_wakeup_dependencies(EN_WKUP | EN_MPU, PM_CORE_OFFSET);
	if (cpu_is_omap2430())
		pmdomain_set_wakeup_dependencies(0, PM_MDM_OFFSET);

	/* Power down DSP and GFX */
	pmdomain_set_powerstate(POWERSTATE_OFF | FORCESTATE, PM_DSP_OFFSET);
	pmdomain_set_powerstate(POWERSTATE_OFF | FORCESTATE, PM_GFX_OFFSET);
}
