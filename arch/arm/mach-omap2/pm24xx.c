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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/gpio.h>

#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>

#include <mach/irqs.h>
#include <plat/clock.h>
#include <plat/sram.h>
#include <plat/dma.h>
#include <plat/board.h>

#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"
#include "pm.h"
#include "control.h"

#include <plat/powerdomain.h>
#include <plat/clockdomain.h>

static void (*omap2_sram_idle)(void);
static void (*omap2_sram_suspend)(u32 dllctrl, void __iomem *sdrc_dlla_ctrl,
				  void __iomem *sdrc_power);

static struct powerdomain *mpu_pwrdm, *core_pwrdm;
static struct clockdomain *dsp_clkdm, *mpu_clkdm, *wkup_clkdm, *gfx_clkdm;

static struct clk *osc_ck, *emul_ck;

static int omap2_fclks_active(void)
{
	u32 f1, f2;

	f1 = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	f2 = cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);

	/* Ignore UART clocks.  These are handled by UART core (serial.c) */
	f1 &= ~(OMAP24XX_EN_UART1_MASK | OMAP24XX_EN_UART2_MASK);
	f2 &= ~OMAP24XX_EN_UART3_MASK;

	if (f1 | f2)
		return 1;
	return 0;
}

static void omap2_enter_full_retention(void)
{
	u32 l;
	struct timespec ts_preidle, ts_postidle, ts_idle;

	/* There is 1 reference hold for all children of the oscillator
	 * clock, the following will remove it. If no one else uses the
	 * oscillator itself it will be disabled if/when we enter retention
	 * mode.
	 */
	clk_disable(osc_ck);

	/* Clear old wake-up events */
	/* REVISIT: These write to reserved bits? */
	prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
	prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);
	prm_write_mod_reg(0xffffffff, WKUP_MOD, PM_WKST);

	/*
	 * Set MPU powerdomain's next power state to RETENTION;
	 * preserve logic state during retention
	 */
	pwrdm_set_logic_retst(mpu_pwrdm, PWRDM_POWER_RET);
	pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_RET);

	/* Workaround to kill USB */
	l = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0) | OMAP24XX_USBSTANDBYCTRL;
	omap_ctrl_writel(l, OMAP2_CONTROL_DEVCONF0);

	omap2_gpio_prepare_for_idle(PWRDM_POWER_RET);

	if (omap2_pm_debug) {
		omap2_pm_dump(0, 0, 0);
		getnstimeofday(&ts_preidle);
	}

	/* One last check for pending IRQs to avoid extra latency due
	 * to sleeping unnecessarily. */
	if (omap_irq_pending())
		goto no_sleep;

	omap_uart_prepare_idle(0);
	omap_uart_prepare_idle(1);
	omap_uart_prepare_idle(2);

	/* Jump to SRAM suspend code */
	omap2_sram_suspend(sdrc_read_reg(SDRC_DLLA_CTRL),
			   OMAP_SDRC_REGADDR(SDRC_DLLA_CTRL),
			   OMAP_SDRC_REGADDR(SDRC_POWER));

	omap_uart_resume_idle(2);
	omap_uart_resume_idle(1);
	omap_uart_resume_idle(0);

no_sleep:
	if (omap2_pm_debug) {
		unsigned long long tmp;

		getnstimeofday(&ts_postidle);
		ts_idle = timespec_sub(ts_postidle, ts_preidle);
		tmp = timespec_to_ns(&ts_idle) * NSEC_PER_USEC;
		omap2_pm_dump(0, 1, tmp);
	}
	omap2_gpio_resume_after_idle();

	clk_enable(osc_ck);

	/* clear CORE wake-up events */
	prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
	prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);

	/* wakeup domain events - bit 1: GPT1, bit5 GPIO */
	prm_clear_mod_reg_bits(0x4 | 0x1, WKUP_MOD, PM_WKST);

	/* MPU domain wake events */
	l = prm_read_mod_reg(OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
	if (l & 0x01)
		prm_write_mod_reg(0x01, OCP_MOD,
				  OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
	if (l & 0x20)
		prm_write_mod_reg(0x20, OCP_MOD,
				  OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);

	/* Mask future PRCM-to-MPU interrupts */
	prm_write_mod_reg(0x0, OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
}

static int omap2_i2c_active(void)
{
	u32 l;

	l = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	return l & (OMAP2420_EN_I2C2_MASK | OMAP2420_EN_I2C1_MASK);
}

static int sti_console_enabled;

static int omap2_allow_mpu_retention(void)
{
	u32 l;

	/* Check for MMC, UART2, UART1, McSPI2, McSPI1 and DSS1. */
	l = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	if (l & (OMAP2420_EN_MMC_MASK | OMAP24XX_EN_UART2_MASK |
		 OMAP24XX_EN_UART1_MASK | OMAP24XX_EN_MCSPI2_MASK |
		 OMAP24XX_EN_MCSPI1_MASK | OMAP24XX_EN_DSS1_MASK))
		return 0;
	/* Check for UART3. */
	l = cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);
	if (l & OMAP24XX_EN_UART3_MASK)
		return 0;
	if (sti_console_enabled)
		return 0;

	return 1;
}

static void omap2_enter_mpu_retention(void)
{
	int only_idle = 0;
	struct timespec ts_preidle, ts_postidle, ts_idle;

	/* Putting MPU into the WFI state while a transfer is active
	 * seems to cause the I2C block to timeout. Why? Good question. */
	if (omap2_i2c_active())
		return;

	/* The peripherals seem not to be able to wake up the MPU when
	 * it is in retention mode. */
	if (omap2_allow_mpu_retention()) {
		/* REVISIT: These write to reserved bits? */
		prm_write_mod_reg(0xffffffff, CORE_MOD, PM_WKST1);
		prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP24XX_PM_WKST2);
		prm_write_mod_reg(0xffffffff, WKUP_MOD, PM_WKST);

		/* Try to enter MPU retention */
		prm_write_mod_reg((0x01 << OMAP_POWERSTATE_SHIFT) |
				  OMAP_LOGICRETSTATE_MASK,
				  MPU_MOD, OMAP2_PM_PWSTCTRL);
	} else {
		/* Block MPU retention */

		prm_write_mod_reg(OMAP_LOGICRETSTATE_MASK, MPU_MOD,
						 OMAP2_PM_PWSTCTRL);
		only_idle = 1;
	}

	if (omap2_pm_debug) {
		omap2_pm_dump(only_idle ? 2 : 1, 0, 0);
		getnstimeofday(&ts_preidle);
	}

	omap2_sram_idle();

	if (omap2_pm_debug) {
		unsigned long long tmp;

		getnstimeofday(&ts_postidle);
		ts_idle = timespec_sub(ts_postidle, ts_preidle);
		tmp = timespec_to_ns(&ts_idle) * NSEC_PER_USEC;
		omap2_pm_dump(only_idle ? 2 : 1, 1, tmp);
	}
}

static int omap2_can_sleep(void)
{
	if (omap2_fclks_active())
		return 0;
	if (!omap_uart_can_sleep())
		return 0;
	if (osc_ck->usecount > 1)
		return 0;
	if (omap_dma_running())
		return 0;

	return 1;
}

static void omap2_pm_idle(void)
{
	local_irq_disable();
	local_fiq_disable();

	if (!omap2_can_sleep()) {
		if (omap_irq_pending())
			goto out;
		omap2_enter_mpu_retention();
		goto out;
	}

	if (omap_irq_pending())
		goto out;

	omap2_enter_full_retention();

out:
	local_fiq_enable();
	local_irq_enable();
}

static int omap2_pm_prepare(void)
{
	/* We cannot sleep in idle until we have resumed */
	disable_hlt();
	return 0;
}

static int omap2_pm_suspend(void)
{
	u32 wken_wkup, mir1;

	wken_wkup = prm_read_mod_reg(WKUP_MOD, PM_WKEN);
	wken_wkup &= ~OMAP24XX_EN_GPT1_MASK;
	prm_write_mod_reg(wken_wkup, WKUP_MOD, PM_WKEN);

	/* Mask GPT1 */
	mir1 = omap_readl(0x480fe0a4);
	omap_writel(1 << 5, 0x480fe0ac);

	omap_uart_prepare_suspend();
	omap2_enter_full_retention();

	omap_writel(mir1, 0x480fe0a4);
	prm_write_mod_reg(wken_wkup, WKUP_MOD, PM_WKEN);

	return 0;
}

static int omap2_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap2_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void omap2_pm_finish(void)
{
	enable_hlt();
}

static struct platform_suspend_ops omap_pm_ops = {
	.prepare	= omap2_pm_prepare,
	.enter		= omap2_pm_enter,
	.finish		= omap2_pm_finish,
	.valid		= suspend_valid_only_mem,
};

/* XXX This function should be shareable between OMAP2xxx and OMAP3 */
static int __init clkdms_setup(struct clockdomain *clkdm, void *unused)
{
	clkdm_clear_all_wkdeps(clkdm);
	clkdm_clear_all_sleepdeps(clkdm);

	if (clkdm->flags & CLKDM_CAN_ENABLE_AUTO)
		omap2_clkdm_allow_idle(clkdm);
	else if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
		 atomic_read(&clkdm->usecount) == 0)
		omap2_clkdm_sleep(clkdm);
	return 0;
}

static void __init prcm_setup_regs(void)
{
	int i, num_mem_banks;
	struct powerdomain *pwrdm;

	/* Enable autoidle */
	prm_write_mod_reg(OMAP24XX_AUTOIDLE_MASK, OCP_MOD,
			  OMAP2_PRCM_SYSCONFIG_OFFSET);

	/*
	 * Set CORE powerdomain memory banks to retain their contents
	 * during RETENTION
	 */
	num_mem_banks = pwrdm_get_mem_bank_count(core_pwrdm);
	for (i = 0; i < num_mem_banks; i++)
		pwrdm_set_mem_retst(core_pwrdm, i, PWRDM_POWER_RET);

	/* Set CORE powerdomain's next power state to RETENTION */
	pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_RET);

	/*
	 * Set MPU powerdomain's next power state to RETENTION;
	 * preserve logic state during retention
	 */
	pwrdm_set_logic_retst(mpu_pwrdm, PWRDM_POWER_RET);
	pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_RET);

	/* Force-power down DSP, GFX powerdomains */

	pwrdm = clkdm_get_pwrdm(dsp_clkdm);
	pwrdm_set_next_pwrst(pwrdm, PWRDM_POWER_OFF);
	omap2_clkdm_sleep(dsp_clkdm);

	pwrdm = clkdm_get_pwrdm(gfx_clkdm);
	pwrdm_set_next_pwrst(pwrdm, PWRDM_POWER_OFF);
	omap2_clkdm_sleep(gfx_clkdm);

	/*
	 * Clear clockdomain wakeup dependencies and enable
	 * hardware-supervised idle for all clkdms
	 */
	clkdm_for_each(clkdms_setup, NULL);
	clkdm_add_wkdep(mpu_clkdm, wkup_clkdm);

	/* Enable clock autoidle for all domains */
	cm_write_mod_reg(OMAP24XX_AUTO_CAM_MASK |
			 OMAP24XX_AUTO_MAILBOXES_MASK |
			 OMAP24XX_AUTO_WDT4_MASK |
			 OMAP2420_AUTO_WDT3_MASK |
			 OMAP24XX_AUTO_MSPRO_MASK |
			 OMAP2420_AUTO_MMC_MASK |
			 OMAP24XX_AUTO_FAC_MASK |
			 OMAP2420_AUTO_EAC_MASK |
			 OMAP24XX_AUTO_HDQ_MASK |
			 OMAP24XX_AUTO_UART2_MASK |
			 OMAP24XX_AUTO_UART1_MASK |
			 OMAP24XX_AUTO_I2C2_MASK |
			 OMAP24XX_AUTO_I2C1_MASK |
			 OMAP24XX_AUTO_MCSPI2_MASK |
			 OMAP24XX_AUTO_MCSPI1_MASK |
			 OMAP24XX_AUTO_MCBSP2_MASK |
			 OMAP24XX_AUTO_MCBSP1_MASK |
			 OMAP24XX_AUTO_GPT12_MASK |
			 OMAP24XX_AUTO_GPT11_MASK |
			 OMAP24XX_AUTO_GPT10_MASK |
			 OMAP24XX_AUTO_GPT9_MASK |
			 OMAP24XX_AUTO_GPT8_MASK |
			 OMAP24XX_AUTO_GPT7_MASK |
			 OMAP24XX_AUTO_GPT6_MASK |
			 OMAP24XX_AUTO_GPT5_MASK |
			 OMAP24XX_AUTO_GPT4_MASK |
			 OMAP24XX_AUTO_GPT3_MASK |
			 OMAP24XX_AUTO_GPT2_MASK |
			 OMAP2420_AUTO_VLYNQ_MASK |
			 OMAP24XX_AUTO_DSS_MASK,
			 CORE_MOD, CM_AUTOIDLE1);
	cm_write_mod_reg(OMAP24XX_AUTO_UART3_MASK |
			 OMAP24XX_AUTO_SSI_MASK |
			 OMAP24XX_AUTO_USB_MASK,
			 CORE_MOD, CM_AUTOIDLE2);
	cm_write_mod_reg(OMAP24XX_AUTO_SDRC_MASK |
			 OMAP24XX_AUTO_GPMC_MASK |
			 OMAP24XX_AUTO_SDMA_MASK,
			 CORE_MOD, CM_AUTOIDLE3);
	cm_write_mod_reg(OMAP24XX_AUTO_PKA_MASK |
			 OMAP24XX_AUTO_AES_MASK |
			 OMAP24XX_AUTO_RNG_MASK |
			 OMAP24XX_AUTO_SHA_MASK |
			 OMAP24XX_AUTO_DES_MASK,
			 CORE_MOD, OMAP24XX_CM_AUTOIDLE4);

	cm_write_mod_reg(OMAP2420_AUTO_DSP_IPI_MASK, OMAP24XX_DSP_MOD,
			 CM_AUTOIDLE);

	/* Put DPLL and both APLLs into autoidle mode */
	cm_write_mod_reg((0x03 << OMAP24XX_AUTO_DPLL_SHIFT) |
			 (0x03 << OMAP24XX_AUTO_96M_SHIFT) |
			 (0x03 << OMAP24XX_AUTO_54M_SHIFT),
			 PLL_MOD, CM_AUTOIDLE);

	cm_write_mod_reg(OMAP24XX_AUTO_OMAPCTRL_MASK |
			 OMAP24XX_AUTO_WDT1_MASK |
			 OMAP24XX_AUTO_MPU_WDT_MASK |
			 OMAP24XX_AUTO_GPIOS_MASK |
			 OMAP24XX_AUTO_32KSYNC_MASK |
			 OMAP24XX_AUTO_GPT1_MASK,
			 WKUP_MOD, CM_AUTOIDLE);

	/* REVISIT: Configure number of 32 kHz clock cycles for sys_clk
	 * stabilisation */
	prm_write_mod_reg(15 << OMAP_SETUP_TIME_SHIFT, OMAP24XX_GR_MOD,
			  OMAP2_PRCM_CLKSSETUP_OFFSET);

	/* Configure automatic voltage transition */
	prm_write_mod_reg(2 << OMAP_SETUP_TIME_SHIFT, OMAP24XX_GR_MOD,
			  OMAP2_PRCM_VOLTSETUP_OFFSET);
	prm_write_mod_reg(OMAP24XX_AUTO_EXTVOLT_MASK |
			  (0x1 << OMAP24XX_SETOFF_LEVEL_SHIFT) |
			  OMAP24XX_MEMRETCTRL_MASK |
			  (0x1 << OMAP24XX_SETRET_LEVEL_SHIFT) |
			  (0x0 << OMAP24XX_VOLT_LEVEL_SHIFT),
			  OMAP24XX_GR_MOD, OMAP2_PRCM_VOLTCTRL_OFFSET);

	/* Enable wake-up events */
	prm_write_mod_reg(OMAP24XX_EN_GPIOS_MASK | OMAP24XX_EN_GPT1_MASK,
			  WKUP_MOD, PM_WKEN);
}

static int __init omap2_pm_init(void)
{
	u32 l;

	if (!cpu_is_omap24xx())
		return -ENODEV;

	printk(KERN_INFO "Power Management for OMAP2 initializing\n");
	l = prm_read_mod_reg(OCP_MOD, OMAP2_PRCM_REVISION_OFFSET);
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

	/* Hack to prevent MPU retention when STI console is enabled. */
	{
		const struct omap_sti_console_config *sti;

		sti = omap_get_config(OMAP_TAG_STI_CONSOLE,
				      struct omap_sti_console_config);
		if (sti != NULL && sti->enable)
			sti_console_enabled = 1;
	}

	/*
	 * We copy the assembler sleep/wakeup routines to SRAM.
	 * These routines need to be in SRAM as that's the only
	 * memory the MPU can see when it wakes up.
	 */
	if (cpu_is_omap24xx()) {
		omap2_sram_idle = omap_sram_push(omap24xx_idle_loop_suspend,
						 omap24xx_idle_loop_suspend_sz);

		omap2_sram_suspend = omap_sram_push(omap24xx_cpu_suspend,
						    omap24xx_cpu_suspend_sz);
	}

	suspend_set_ops(&omap_pm_ops);
	pm_idle = omap2_pm_idle;

	return 0;
}

late_initcall(omap2_pm_init);
