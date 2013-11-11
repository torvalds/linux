/*
 * OMAP2 Power Management Routines
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Written by:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Tony Lindgren
 * Juha Yrjola
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Igor Stoppa <igor.stoppa@nokia.com>
 *
 * Based on pm.c for omap1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/gpio.h>
#include <linux/platform_data/gpio-omap.h>

#include <asm/fncpy.h>

#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>
#include <asm/system_misc.h>

#include <linux/omap-dma.h>

#include "soc.h"
#include "common.h"
#include "clock.h"
#include "prm2xxx.h"
#include "prm-regbits-24xx.h"
#include "cm2xxx.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"
#include "sram.h"
#include "pm.h"
#include "control.h"
#include "powerdomain.h"
#include "clockdomain.h"

static void (*omap2_sram_suspend)(u32 dllctrl, void __iomem *sdrc_dlla_ctrl,
				  void __iomem *sdrc_power);

static struct powerdomain *mpu_pwrdm, *core_pwrdm;
static struct clockdomain *dsp_clkdm, *mpu_clkdm, *wkup_clkdm, *gfx_clkdm;

static struct clk *osc_ck, *emul_ck;

static int omap2_fclks_active(void)
{
	u32 f1, f2;

	f1 = omap2_cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	f2 = omap2_cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);

	return (f1 | f2) ? 1 : 0;
}

static int omap2_enter_full_retention(void)
{
	u32 l;

	/* There is 1 reference hold for all children of the oscillator
	 * clock, the following will remove it. If no one else uses the
	 * oscillator itself it will be disabled if/when we enter retention
	 * mode.
	 */
	clk_disable(osc_ck);

	/* Clear old wake-up events */
	/* REVISIT: These write to reserved bits? */
	omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
	omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);
	omap2_prm_write_mod_reg(0xffffffff, WKUP_MOD, PM_WKST);

	pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_RET);
	pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_RET);

	/* Workaround to kill USB */
	l = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0) | OMAP24XX_USBSTANDBYCTRL;
	omap_ctrl_writel(l, OMAP2_CONTROL_DEVCONF0);

	omap2_gpio_prepare_for_idle(0);

	/* One last check for pending IRQs to avoid extra latency due
	 * to sleeping unnecessarily. */
	if (omap_irq_pending())
		goto no_sleep;

	/* Jump to SRAM suspend code */
	omap2_sram_suspend(sdrc_read_reg(SDRC_DLLA_CTRL),
			   OMAP_SDRC_REGADDR(SDRC_DLLA_CTRL),
			   OMAP_SDRC_REGADDR(SDRC_POWER));

no_sleep:
	omap2_gpio_resume_after_idle();

	clk_enable(osc_ck);

	/* clear CORE wake-up events */
	omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
	omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);

	/* wakeup domain events - bit 1: GPT1, bit5 GPIO */
	omap2_prm_clear_mod_reg_bits(0x4 | 0x1, WKUP_MOD, PM_WKST);

	/* MPU domain wake events */
	l = omap2_prm_read_mod_reg(OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
	if (l & 0x01)
		omap2_prm_write_mod_reg(0x01, OCP_MOD,
				  OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
	if (l & 0x20)
		omap2_prm_write_mod_reg(0x20, OCP_MOD,
				  OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);

	/* Mask future PRCM-to-MPU interrupts */
	omap2_prm_write_mod_reg(0x0, OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);

	pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_ON);
	pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_ON);

	return 0;
}

static int sti_console_enabled;

static int omap2_allow_mpu_retention(void)
{
	u32 l;

	/* Check for MMC, UART2, UART1, McSPI2, McSPI1 and DSS1. */
	l = omap2_cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	if (l & (OMAP2420_EN_MMC_MASK | OMAP24XX_EN_UART2_MASK |
		 OMAP24XX_EN_UART1_MASK | OMAP24XX_EN_MCSPI2_MASK |
		 OMAP24XX_EN_MCSPI1_MASK | OMAP24XX_EN_DSS1_MASK))
		return 0;
	/* Check for UART3. */
	l = omap2_cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);
	if (l & OMAP24XX_EN_UART3_MASK)
		return 0;
	if (sti_console_enabled)
		return 0;

	return 1;
}

static void omap2_enter_mpu_retention(void)
{
	const int zero = 0;

	/* The peripherals seem not to be able to wake up the MPU when
	 * it is in retention mode. */
	if (omap2_allow_mpu_retention()) {
		/* REVISIT: These write to reserved bits? */
		omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
		omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);
		omap2_prm_write_mod_reg(0xffffffff, WKUP_MOD, PM_WKST);

		/* Try to enter MPU retention */
		pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_RET);

	} else {
		/* Block MPU retention */
		pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_ON);
	}

	/* WFI */
	asm("mcr p15, 0, %0, c7, c0, 4" : : "r" (zero) : "memory", "cc");

	pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_ON);
}

static int omap2_can_sleep(void)
{
	if (omap2_fclks_active())
		return 0;
	if (__clk_is_enabled(osc_ck))
		return 0;
	if (omap_dma_running())
		return 0;

	return 1;
}

static void omap2_pm_idle(void)
{
	if (!omap2_can_sleep()) {
		if (omap_irq_pending())
			return;
		omap2_enter_mpu_retention();
		return;
	}

	if (omap_irq_pending())
		return;

	omap2_enter_full_retention();
}

static void __init prcm_setup_regs(void)
{
	int i, num_mem_banks;
	struct powerdomain *pwrdm;

	/*
	 * Enable autoidle
	 * XXX This should be handled by hwmod code or PRCM init code
	 */
	omap2_prm_write_mod_reg(OMAP24XX_AUTOIDLE_MASK, OCP_MOD,
			  OMAP2_PRCM_SYSCONFIG_OFFSET);

	/*
	 * Set CORE powerdomain memory banks to retain their contents
	 * during RETENTION
	 */
	num_mem_banks = pwrdm_get_mem_bank_count(core_pwrdm);
	for (i = 0; i < num_mem_banks; i++)
		pwrdm_set_mem_retst(core_pwrdm, i, PWRDM_POWER_RET);

	pwrdm_set_logic_retst(core_pwrdm, PWRDM_POWER_RET);

	pwrdm_set_logic_retst(mpu_pwrdm, PWRDM_POWER_RET);

	/* Force-power down DSP, GFX powerdomains */

	pwrdm = clkdm_get_pwrdm(dsp_clkdm);
	pwrdm_set_next_pwrst(pwrdm, PWRDM_POWER_OFF);

	pwrdm = clkdm_get_pwrdm(gfx_clkdm);
	pwrdm_set_next_pwrst(pwrdm, PWRDM_POWER_OFF);

	/* Enable hardware-supervised idle for all clkdms */
	clkdm_for_each(omap_pm_clkdms_setup, NULL);
	clkdm_add_wkdep(mpu_clkdm, wkup_clkdm);

#ifdef CONFIG_SUSPEND
	omap_pm_suspend = omap2_enter_full_retention;
#endif

	/* REVISIT: Configure number of 32 kHz clock cycles for sys_clk
	 * stabilisation */
	omap2_prm_write_mod_reg(15 << OMAP_SETUP_TIME_SHIFT, OMAP24XX_GR_MOD,
				OMAP2_PRCM_CLKSSETUP_OFFSET);

	/* Configure automatic voltage transition */
	omap2_prm_write_mod_reg(2 << OMAP_SETUP_TIME_SHIFT, OMAP24XX_GR_MOD,
				OMAP2_PRCM_VOLTSETUP_OFFSET);
	omap2_prm_write_mod_reg(OMAP24XX_AUTO_EXTVOLT_MASK |
				(0x1 << OMAP24XX_SETOFF_LEVEL_SHIFT) |
				OMAP24XX_MEMRETCTRL_MASK |
				(0x1 << OMAP24XX_SETRET_LEVEL_SHIFT) |
				(0x0 << OMAP24XX_VOLT_LEVEL_SHIFT),
				OMAP24XX_GR_MOD, OMAP2_PRCM_VOLTCTRL_OFFSET);

	/* Enable wake-up events */
	omap2_prm_write_mod_reg(OMAP24XX_EN_GPIOS_MASK | OMAP24XX_EN_GPT1_MASK,
				WKUP_MOD, PM_WKEN);
}

int __init omap2_pm_init(void)
{
	u32 l;

	printk(KERN_INFO "Power Management for OMAP2 initializing\n");
	l = omap2_prm_read_mod_reg(OCP_MOD, OMAP2_PRCM_REVISION_OFFSET);
	printk(KERN_INFO "PRCM revision %d.%d\n", (l >> 4) & 0x0f, l & 0x0f);

	/* Look up important powerdomains */

	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");
	if (!mpu_pwrdm)
		pr_err("PM: mpu_pwrdm not found\n");

	core_pwrdm = pwrdm_lookup("core_pwrdm");
	if (!core_pwrdm)
		pr_err("PM: core_pwrdm not found\n");

	/* Look up important clockdomains */

	mpu_clkdm = clkdm_lookup("mpu_clkdm");
	if (!mpu_clkdm)
		pr_err("PM: mpu_clkdm not found\n");

	wkup_clkdm = clkdm_lookup("wkup_clkdm");
	if (!wkup_clkdm)
		pr_err("PM: wkup_clkdm not found\n");

	dsp_clkdm = clkdm_lookup("dsp_clkdm");
	if (!dsp_clkdm)
		pr_err("PM: dsp_clkdm not found\n");

	gfx_clkdm = clkdm_lookup("gfx_clkdm");
	if (!gfx_clkdm)
		pr_err("PM: gfx_clkdm not found\n");


	osc_ck = clk_get(NULL, "osc_ck");
	if (IS_ERR(osc_ck)) {
		printk(KERN_ERR "could not get osc_ck\n");
		return -ENODEV;
	}

	if (cpu_is_omap242x()) {
		emul_ck = clk_get(NULL, "emul_ck");
		if (IS_ERR(emul_ck)) {
			printk(KERN_ERR "could not get emul_ck\n");
			clk_put(osc_ck);
			return -ENODEV;
		}
	}

	prcm_setup_regs();

	/*
	 * We copy the assembler sleep/wakeup routines to SRAM.
	 * These routines need to be in SRAM as that's the only
	 * memory the MPU can see when it wakes up after the entire
	 * chip enters idle.
	 */
	omap2_sram_suspend = omap_sram_push(omap24xx_cpu_suspend,
					    omap24xx_cpu_suspend_sz);

	arm_pm_idle = omap2_pm_idle;

	return 0;
}
