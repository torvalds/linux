/*
 * linux/arch/arm/mach-omap2/pm.c
 *
 * OMAP2 Power Management Routines
 *
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
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

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/irqs.h>
#include <asm/arch/clock.h>
#include <asm/arch/sram.h>
#include <asm/arch/pm.h>

#include "prcm-regs.h"

static struct clk *vclk;
static void (*omap2_sram_idle)(void);
static void (*omap2_sram_suspend)(int dllctrl, int cpu_rev);
static void (*saved_idle)(void);

extern void __init pmdomain_init(void);
extern void pmdomain_set_autoidle(void);

static unsigned int omap24xx_sleep_save[OMAP24XX_SLEEP_SAVE_SIZE];

void omap2_pm_idle(void)
{
	local_irq_disable();
	local_fiq_disable();
	if (need_resched()) {
		local_fiq_enable();
		local_irq_enable();
		return;
	}

	/*
	 * Since an interrupt may set up a timer, we don't want to
	 * reprogram the hardware timer with interrupts enabled.
	 * Re-enable interrupts only after returning from idle.
	 */
	timer_dyn_reprogram();

	omap2_sram_idle();
	local_fiq_enable();
	local_irq_enable();
}

static int omap2_pm_prepare(suspend_state_t state)
{
	int error = 0;

	/* We cannot sleep in idle until we have resumed */
	saved_idle = pm_idle;
	pm_idle = NULL;

	switch (state)
	{
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		break;

	default:
		return -EINVAL;
	}

	return error;
}

#define INT0_WAKE_MASK	(OMAP_IRQ_BIT(INT_24XX_GPIO_BANK1) |	\
			OMAP_IRQ_BIT(INT_24XX_GPIO_BANK2) |	\
			OMAP_IRQ_BIT(INT_24XX_GPIO_BANK3))

#define INT1_WAKE_MASK	(OMAP_IRQ_BIT(INT_24XX_GPIO_BANK4))

#define INT2_WAKE_MASK	(OMAP_IRQ_BIT(INT_24XX_UART1_IRQ) |	\
			OMAP_IRQ_BIT(INT_24XX_UART2_IRQ) |	\
			OMAP_IRQ_BIT(INT_24XX_UART3_IRQ))

#define preg(reg)	printk("%s\t(0x%p):\t0x%08x\n", #reg, &reg, reg);

static void omap2_pm_debug(char * desc)
{
	printk("%s:\n", desc);

	preg(CM_CLKSTCTRL_MPU);
	preg(CM_CLKSTCTRL_CORE);
	preg(CM_CLKSTCTRL_GFX);
	preg(CM_CLKSTCTRL_DSP);
	preg(CM_CLKSTCTRL_MDM);

	preg(PM_PWSTCTRL_MPU);
	preg(PM_PWSTCTRL_CORE);
	preg(PM_PWSTCTRL_GFX);
	preg(PM_PWSTCTRL_DSP);
	preg(PM_PWSTCTRL_MDM);

	preg(PM_PWSTST_MPU);
	preg(PM_PWSTST_CORE);
	preg(PM_PWSTST_GFX);
	preg(PM_PWSTST_DSP);
	preg(PM_PWSTST_MDM);

	preg(CM_AUTOIDLE1_CORE);
	preg(CM_AUTOIDLE2_CORE);
	preg(CM_AUTOIDLE3_CORE);
	preg(CM_AUTOIDLE4_CORE);
	preg(CM_AUTOIDLE_WKUP);
	preg(CM_AUTOIDLE_PLL);
	preg(CM_AUTOIDLE_DSP);
	preg(CM_AUTOIDLE_MDM);

	preg(CM_ICLKEN1_CORE);
	preg(CM_ICLKEN2_CORE);
	preg(CM_ICLKEN3_CORE);
	preg(CM_ICLKEN4_CORE);
	preg(CM_ICLKEN_GFX);
	preg(CM_ICLKEN_WKUP);
	preg(CM_ICLKEN_DSP);
	preg(CM_ICLKEN_MDM);

	preg(CM_IDLEST1_CORE);
	preg(CM_IDLEST2_CORE);
	preg(CM_IDLEST3_CORE);
	preg(CM_IDLEST4_CORE);
	preg(CM_IDLEST_GFX);
	preg(CM_IDLEST_WKUP);
	preg(CM_IDLEST_CKGEN);
	preg(CM_IDLEST_DSP);
	preg(CM_IDLEST_MDM);

	preg(RM_RSTST_MPU);
	preg(RM_RSTST_GFX);
	preg(RM_RSTST_WKUP);
	preg(RM_RSTST_DSP);
	preg(RM_RSTST_MDM);

	preg(PM_WKDEP_MPU);
	preg(PM_WKDEP_CORE);
	preg(PM_WKDEP_GFX);
	preg(PM_WKDEP_DSP);
	preg(PM_WKDEP_MDM);

	preg(CM_FCLKEN_WKUP);
	preg(CM_ICLKEN_WKUP);
	preg(CM_IDLEST_WKUP);
	preg(CM_AUTOIDLE_WKUP);
	preg(CM_CLKSEL_WKUP);

	preg(PM_WKEN_WKUP);
	preg(PM_WKST_WKUP);
}

static inline void omap2_pm_save_registers(void)
{
	/* Save interrupt registers */
	OMAP24XX_SAVE(INTC_MIR0);
	OMAP24XX_SAVE(INTC_MIR1);
	OMAP24XX_SAVE(INTC_MIR2);

	/* Save power control registers */
	OMAP24XX_SAVE(CM_CLKSTCTRL_MPU);
	OMAP24XX_SAVE(CM_CLKSTCTRL_CORE);
	OMAP24XX_SAVE(CM_CLKSTCTRL_GFX);
	OMAP24XX_SAVE(CM_CLKSTCTRL_DSP);
	OMAP24XX_SAVE(CM_CLKSTCTRL_MDM);

	/* Save power state registers */
	OMAP24XX_SAVE(PM_PWSTCTRL_MPU);
	OMAP24XX_SAVE(PM_PWSTCTRL_CORE);
	OMAP24XX_SAVE(PM_PWSTCTRL_GFX);
	OMAP24XX_SAVE(PM_PWSTCTRL_DSP);
	OMAP24XX_SAVE(PM_PWSTCTRL_MDM);

	/* Save autoidle registers */
	OMAP24XX_SAVE(CM_AUTOIDLE1_CORE);
	OMAP24XX_SAVE(CM_AUTOIDLE2_CORE);
	OMAP24XX_SAVE(CM_AUTOIDLE3_CORE);
	OMAP24XX_SAVE(CM_AUTOIDLE4_CORE);
	OMAP24XX_SAVE(CM_AUTOIDLE_WKUP);
	OMAP24XX_SAVE(CM_AUTOIDLE_PLL);
	OMAP24XX_SAVE(CM_AUTOIDLE_DSP);
	OMAP24XX_SAVE(CM_AUTOIDLE_MDM);

	/* Save idle state registers */
	OMAP24XX_SAVE(CM_IDLEST1_CORE);
	OMAP24XX_SAVE(CM_IDLEST2_CORE);
	OMAP24XX_SAVE(CM_IDLEST3_CORE);
	OMAP24XX_SAVE(CM_IDLEST4_CORE);
	OMAP24XX_SAVE(CM_IDLEST_GFX);
	OMAP24XX_SAVE(CM_IDLEST_WKUP);
	OMAP24XX_SAVE(CM_IDLEST_CKGEN);
	OMAP24XX_SAVE(CM_IDLEST_DSP);
	OMAP24XX_SAVE(CM_IDLEST_MDM);

	/* Save clock registers */
	OMAP24XX_SAVE(CM_FCLKEN1_CORE);
	OMAP24XX_SAVE(CM_FCLKEN2_CORE);
	OMAP24XX_SAVE(CM_ICLKEN1_CORE);
	OMAP24XX_SAVE(CM_ICLKEN2_CORE);
	OMAP24XX_SAVE(CM_ICLKEN3_CORE);
	OMAP24XX_SAVE(CM_ICLKEN4_CORE);
}

static inline void omap2_pm_restore_registers(void)
{
	/* Restore clock state registers */
	OMAP24XX_RESTORE(CM_CLKSTCTRL_MPU);
	OMAP24XX_RESTORE(CM_CLKSTCTRL_CORE);
	OMAP24XX_RESTORE(CM_CLKSTCTRL_GFX);
	OMAP24XX_RESTORE(CM_CLKSTCTRL_DSP);
	OMAP24XX_RESTORE(CM_CLKSTCTRL_MDM);

	/* Restore power state registers */
	OMAP24XX_RESTORE(PM_PWSTCTRL_MPU);
	OMAP24XX_RESTORE(PM_PWSTCTRL_CORE);
	OMAP24XX_RESTORE(PM_PWSTCTRL_GFX);
	OMAP24XX_RESTORE(PM_PWSTCTRL_DSP);
	OMAP24XX_RESTORE(PM_PWSTCTRL_MDM);

	/* Restore idle state registers */
	OMAP24XX_RESTORE(CM_IDLEST1_CORE);
	OMAP24XX_RESTORE(CM_IDLEST2_CORE);
	OMAP24XX_RESTORE(CM_IDLEST3_CORE);
	OMAP24XX_RESTORE(CM_IDLEST4_CORE);
	OMAP24XX_RESTORE(CM_IDLEST_GFX);
	OMAP24XX_RESTORE(CM_IDLEST_WKUP);
	OMAP24XX_RESTORE(CM_IDLEST_CKGEN);
	OMAP24XX_RESTORE(CM_IDLEST_DSP);
	OMAP24XX_RESTORE(CM_IDLEST_MDM);

	/* Restore autoidle registers */
	OMAP24XX_RESTORE(CM_AUTOIDLE1_CORE);
	OMAP24XX_RESTORE(CM_AUTOIDLE2_CORE);
	OMAP24XX_RESTORE(CM_AUTOIDLE3_CORE);
	OMAP24XX_RESTORE(CM_AUTOIDLE4_CORE);
	OMAP24XX_RESTORE(CM_AUTOIDLE_WKUP);
	OMAP24XX_RESTORE(CM_AUTOIDLE_PLL);
	OMAP24XX_RESTORE(CM_AUTOIDLE_DSP);
	OMAP24XX_RESTORE(CM_AUTOIDLE_MDM);

	/* Restore clock registers */
	OMAP24XX_RESTORE(CM_FCLKEN1_CORE);
	OMAP24XX_RESTORE(CM_FCLKEN2_CORE);
	OMAP24XX_RESTORE(CM_ICLKEN1_CORE);
	OMAP24XX_RESTORE(CM_ICLKEN2_CORE);
	OMAP24XX_RESTORE(CM_ICLKEN3_CORE);
	OMAP24XX_RESTORE(CM_ICLKEN4_CORE);

	/* REVISIT: Clear interrupts here */

	/* Restore interrupt registers */
	OMAP24XX_RESTORE(INTC_MIR0);
	OMAP24XX_RESTORE(INTC_MIR1);
	OMAP24XX_RESTORE(INTC_MIR2);
}

static int omap2_pm_suspend(void)
{
	int processor_type = 0;

	/* REVISIT: 0x21 or 0x26? */
	if (cpu_is_omap2420())
		processor_type = 0x21;

	if (!processor_type)
		return -ENOTSUPP;

	local_irq_disable();
	local_fiq_disable();

	omap2_pm_save_registers();

	/* Disable interrupts except for the wake events */
	INTC_MIR_SET0 = 0xffffffff & ~INT0_WAKE_MASK;
	INTC_MIR_SET1 = 0xffffffff & ~INT1_WAKE_MASK;
	INTC_MIR_SET2 = 0xffffffff & ~INT2_WAKE_MASK;

	pmdomain_set_autoidle();

	/* Clear old wake-up events */
	PM_WKST1_CORE = 0;
	PM_WKST2_CORE = 0;
	PM_WKST_WKUP = 0;

	/* Enable wake-up events */
	PM_WKEN1_CORE = (1 << 22) | (1 << 21);	/* UART1 & 2 */
	PM_WKEN2_CORE = (1 << 2);		/* UART3 */
	PM_WKEN_WKUP = (1 << 2) | (1 << 0);	/* GPIO & GPT1 */

	/* Disable clocks except for CM_ICLKEN2_CORE. It gets disabled
	 * in the SRAM suspend code */
	CM_FCLKEN1_CORE = 0;
	CM_FCLKEN2_CORE = 0;
	CM_ICLKEN1_CORE = 0;
	CM_ICLKEN3_CORE = 0;
	CM_ICLKEN4_CORE = 0;

	omap2_pm_debug("Status before suspend");

	/* Must wait for serial buffers to clear */
	mdelay(200);

	/* Jump to SRAM suspend code
	 * REVISIT: When is this SDRC_DLLB_CTRL?
	 */
	omap2_sram_suspend(SDRC_DLLA_CTRL, processor_type);

	/* Back from sleep */
	omap2_pm_restore_registers();

	local_fiq_enable();
	local_irq_enable();

	return 0;
}

static int omap2_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state)
	{
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap2_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int omap2_pm_finish(suspend_state_t state)
{
	pm_idle = saved_idle;
	return 0;
}

static struct pm_ops omap_pm_ops = {
	.prepare	= omap2_pm_prepare,
	.enter		= omap2_pm_enter,
	.finish		= omap2_pm_finish,
	.valid		= pm_valid_only_mem,
};

int __init omap2_pm_init(void)
{
	printk("Power Management for TI OMAP.\n");

	vclk = clk_get(NULL, "virt_prcm_set");
	if (IS_ERR(vclk)) {
		printk(KERN_ERR "Could not get PM vclk\n");
		return -ENODEV;
	}

	/*
	 * We copy the assembler sleep/wakeup routines to SRAM.
	 * These routines need to be in SRAM as that's the only
	 * memory the MPU can see when it wakes up.
	 */
	omap2_sram_idle = omap_sram_push(omap24xx_idle_loop_suspend,
					 omap24xx_idle_loop_suspend_sz);

	omap2_sram_suspend = omap_sram_push(omap24xx_cpu_suspend,
					    omap24xx_cpu_suspend_sz);

	pm_set_ops(&omap_pm_ops);
	pm_idle = omap2_pm_idle;

	pmdomain_init();

	return 0;
}

__initcall(omap2_pm_init);
