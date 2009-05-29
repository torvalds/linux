/*
 * OMAP3 Power Management Routines
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Jouni Hogander
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

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/gpio.h>

#include <mach/sram.h>
#include <mach/clockdomain.h>
#include <mach/powerdomain.h>
#include <mach/control.h>
#include <mach/serial.h>

#include "cm.h"
#include "cm-regbits-34xx.h"
#include "prm-regbits-34xx.h"

#include "prm.h"
#include "pm.h"

struct power_state {
	struct powerdomain *pwrdm;
	u32 next_state;
	u32 saved_state;
	struct list_head node;
};

static LIST_HEAD(pwrst_list);

static void (*_omap_sram_idle)(u32 *addr, int save_state);

static struct powerdomain *mpu_pwrdm;

/* PRCM Interrupt Handler for wakeups */
static irqreturn_t prcm_interrupt_handler (int irq, void *dev_id)
{
	u32 wkst, irqstatus_mpu;
	u32 fclk, iclk;

	/* WKUP */
	wkst = prm_read_mod_reg(WKUP_MOD, PM_WKST);
	if (wkst) {
		iclk = cm_read_mod_reg(WKUP_MOD, CM_ICLKEN);
		fclk = cm_read_mod_reg(WKUP_MOD, CM_FCLKEN);
		cm_set_mod_reg_bits(wkst, WKUP_MOD, CM_ICLKEN);
		cm_set_mod_reg_bits(wkst, WKUP_MOD, CM_FCLKEN);
		prm_write_mod_reg(wkst, WKUP_MOD, PM_WKST);
		while (prm_read_mod_reg(WKUP_MOD, PM_WKST))
			cpu_relax();
		cm_write_mod_reg(iclk, WKUP_MOD, CM_ICLKEN);
		cm_write_mod_reg(fclk, WKUP_MOD, CM_FCLKEN);
	}

	/* CORE */
	wkst = prm_read_mod_reg(CORE_MOD, PM_WKST1);
	if (wkst) {
		iclk = cm_read_mod_reg(CORE_MOD, CM_ICLKEN1);
		fclk = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_ICLKEN1);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_FCLKEN1);
		prm_write_mod_reg(wkst, CORE_MOD, PM_WKST1);
		while (prm_read_mod_reg(CORE_MOD, PM_WKST1))
			cpu_relax();
		cm_write_mod_reg(iclk, CORE_MOD, CM_ICLKEN1);
		cm_write_mod_reg(fclk, CORE_MOD, CM_FCLKEN1);
	}
	wkst = prm_read_mod_reg(CORE_MOD, OMAP3430ES2_PM_WKST3);
	if (wkst) {
		iclk = cm_read_mod_reg(CORE_MOD, CM_ICLKEN3);
		fclk = cm_read_mod_reg(CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_ICLKEN3);
		cm_set_mod_reg_bits(wkst, CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
		prm_write_mod_reg(wkst, CORE_MOD, OMAP3430ES2_PM_WKST3);
		while (prm_read_mod_reg(CORE_MOD, OMAP3430ES2_PM_WKST3))
			cpu_relax();
		cm_write_mod_reg(iclk, CORE_MOD, CM_ICLKEN3);
		cm_write_mod_reg(fclk, CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
	}

	/* PER */
	wkst = prm_read_mod_reg(OMAP3430_PER_MOD, PM_WKST);
	if (wkst) {
		iclk = cm_read_mod_reg(OMAP3430_PER_MOD, CM_ICLKEN);
		fclk = cm_read_mod_reg(OMAP3430_PER_MOD, CM_FCLKEN);
		cm_set_mod_reg_bits(wkst, OMAP3430_PER_MOD, CM_ICLKEN);
		cm_set_mod_reg_bits(wkst, OMAP3430_PER_MOD, CM_FCLKEN);
		prm_write_mod_reg(wkst, OMAP3430_PER_MOD, PM_WKST);
		while (prm_read_mod_reg(OMAP3430_PER_MOD, PM_WKST))
			cpu_relax();
		cm_write_mod_reg(iclk, OMAP3430_PER_MOD, CM_ICLKEN);
		cm_write_mod_reg(fclk, OMAP3430_PER_MOD, CM_FCLKEN);
	}

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		/* USBHOST */
		wkst = prm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, PM_WKST);
		if (wkst) {
			iclk = cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
					       CM_ICLKEN);
			fclk = cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
					       CM_FCLKEN);
			cm_set_mod_reg_bits(wkst, OMAP3430ES2_USBHOST_MOD,
					    CM_ICLKEN);
			cm_set_mod_reg_bits(wkst, OMAP3430ES2_USBHOST_MOD,
					    CM_FCLKEN);
			prm_write_mod_reg(wkst, OMAP3430ES2_USBHOST_MOD,
					  PM_WKST);
			while (prm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
						PM_WKST))
				cpu_relax();
			cm_write_mod_reg(iclk, OMAP3430ES2_USBHOST_MOD,
					 CM_ICLKEN);
			cm_write_mod_reg(fclk, OMAP3430ES2_USBHOST_MOD,
					 CM_FCLKEN);
		}
	}

	irqstatus_mpu = prm_read_mod_reg(OCP_MOD,
					 OMAP3_PRM_IRQSTATUS_MPU_OFFSET);
	prm_write_mod_reg(irqstatus_mpu, OCP_MOD,
			  OMAP3_PRM_IRQSTATUS_MPU_OFFSET);

	while (prm_read_mod_reg(OCP_MOD, OMAP3_PRM_IRQSTATUS_MPU_OFFSET))
		cpu_relax();

	return IRQ_HANDLED;
}

static void omap_sram_idle(void)
{
	/* Variable to tell what needs to be saved and restored
	 * in omap_sram_idle*/
	/* save_state = 0 => Nothing to save and restored */
	/* save_state = 1 => Only L1 and logic lost */
	/* save_state = 2 => Only L2 lost */
	/* save_state = 3 => L1, L2 and logic lost */
	int save_state = 0, mpu_next_state;

	if (!_omap_sram_idle)
		return;

	mpu_next_state = pwrdm_read_next_pwrst(mpu_pwrdm);
	switch (mpu_next_state) {
	case PWRDM_POWER_RET:
		/* No need to save context */
		save_state = 0;
		break;
	default:
		/* Invalid state */
		printk(KERN_ERR "Invalid mpu state in sram_idle\n");
		return;
	}
	omap2_gpio_prepare_for_retention();
	omap_uart_prepare_idle(0);
	omap_uart_prepare_idle(1);
	omap_uart_prepare_idle(2);

	_omap_sram_idle(NULL, save_state);
	cpu_init();

	omap_uart_resume_idle(2);
	omap_uart_resume_idle(1);
	omap_uart_resume_idle(0);
	omap2_gpio_resume_after_retention();
}

/*
 * Check if functional clocks are enabled before entering
 * sleep. This function could be behind CONFIG_PM_DEBUG
 * when all drivers are configuring their sysconfig registers
 * properly and using their clocks properly.
 */
static int omap3_fclks_active(void)
{
	u32 fck_core1 = 0, fck_core3 = 0, fck_sgx = 0, fck_dss = 0,
		fck_cam = 0, fck_per = 0, fck_usbhost = 0;

	fck_core1 = cm_read_mod_reg(CORE_MOD,
				    CM_FCLKEN1);
	if (omap_rev() > OMAP3430_REV_ES1_0) {
		fck_core3 = cm_read_mod_reg(CORE_MOD,
					    OMAP3430ES2_CM_FCLKEN3);
		fck_sgx = cm_read_mod_reg(OMAP3430ES2_SGX_MOD,
					  CM_FCLKEN);
		fck_usbhost = cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
					      CM_FCLKEN);
	} else
		fck_sgx = cm_read_mod_reg(GFX_MOD,
					  OMAP3430ES2_CM_FCLKEN3);
	fck_dss = cm_read_mod_reg(OMAP3430_DSS_MOD,
				  CM_FCLKEN);
	fck_cam = cm_read_mod_reg(OMAP3430_CAM_MOD,
				  CM_FCLKEN);
	fck_per = cm_read_mod_reg(OMAP3430_PER_MOD,
				  CM_FCLKEN);

	/* Ignore UART clocks.  These are handled by UART core (serial.c) */
	fck_core1 &= ~(OMAP3430_EN_UART1 | OMAP3430_EN_UART2);
	fck_per &= ~OMAP3430_EN_UART3;

	if (fck_core1 | fck_core3 | fck_sgx | fck_dss |
	    fck_cam | fck_per | fck_usbhost)
		return 1;
	return 0;
}

static int omap3_can_sleep(void)
{
	if (!omap_uart_can_sleep())
		return 0;
	if (omap3_fclks_active())
		return 0;
	return 1;
}

/* This sets pwrdm state (other than mpu & core. Currently only ON &
 * RET are supported. Function is assuming that clkdm doesn't have
 * hw_sup mode enabled. */
static int set_pwrdm_state(struct powerdomain *pwrdm, u32 state)
{
	u32 cur_state;
	int sleep_switch = 0;
	int ret = 0;

	if (pwrdm == NULL || IS_ERR(pwrdm))
		return -EINVAL;

	while (!(pwrdm->pwrsts & (1 << state))) {
		if (state == PWRDM_POWER_OFF)
			return ret;
		state--;
	}

	cur_state = pwrdm_read_next_pwrst(pwrdm);
	if (cur_state == state)
		return ret;

	if (pwrdm_read_pwrst(pwrdm) < PWRDM_POWER_ON) {
		omap2_clkdm_wakeup(pwrdm->pwrdm_clkdms[0]);
		sleep_switch = 1;
		pwrdm_wait_transition(pwrdm);
	}

	ret = pwrdm_set_next_pwrst(pwrdm, state);
	if (ret) {
		printk(KERN_ERR "Unable to set state of powerdomain: %s\n",
		       pwrdm->name);
		goto err;
	}

	if (sleep_switch) {
		omap2_clkdm_allow_idle(pwrdm->pwrdm_clkdms[0]);
		pwrdm_wait_transition(pwrdm);
	}

err:
	return ret;
}

static void omap3_pm_idle(void)
{
	local_irq_disable();
	local_fiq_disable();

	if (!omap3_can_sleep())
		goto out;

	if (omap_irq_pending())
		goto out;

	omap_sram_idle();

out:
	local_fiq_enable();
	local_irq_enable();
}

static int omap3_pm_prepare(void)
{
	disable_hlt();
	return 0;
}

static int omap3_pm_suspend(void)
{
	struct power_state *pwrst;
	int state, ret = 0;

	/* Read current next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node)
		pwrst->saved_state = pwrdm_read_next_pwrst(pwrst->pwrdm);
	/* Set ones wanted by suspend */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (set_pwrdm_state(pwrst->pwrdm, pwrst->next_state))
			goto restore;
		if (pwrdm_clear_all_prev_pwrst(pwrst->pwrdm))
			goto restore;
	}

	omap_uart_prepare_suspend();
	omap_sram_idle();

restore:
	/* Restore next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		set_pwrdm_state(pwrst->pwrdm, pwrst->saved_state);
		state = pwrdm_read_prev_pwrst(pwrst->pwrdm);
		if (state > pwrst->next_state) {
			printk(KERN_INFO "Powerdomain (%s) didn't enter "
			       "target state %d\n",
			       pwrst->pwrdm->name, pwrst->next_state);
			ret = -1;
		}
	}
	if (ret)
		printk(KERN_ERR "Could not enter target state in pm_suspend\n");
	else
		printk(KERN_INFO "Successfully put all powerdomains "
		       "to target state\n");

	return ret;
}

static int omap3_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap3_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void omap3_pm_finish(void)
{
	enable_hlt();
}

static struct platform_suspend_ops omap_pm_ops = {
	.prepare	= omap3_pm_prepare,
	.enter		= omap3_pm_enter,
	.finish		= omap3_pm_finish,
	.valid		= suspend_valid_only_mem,
};


/**
 * omap3_iva_idle(): ensure IVA is in idle so it can be put into
 *                   retention
 *
 * In cases where IVA2 is activated by bootcode, it may prevent
 * full-chip retention or off-mode because it is not idle.  This
 * function forces the IVA2 into idle state so it can go
 * into retention/off and thus allow full-chip retention/off.
 *
 **/
static void __init omap3_iva_idle(void)
{
	/* ensure IVA2 clock is disabled */
	cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* if no clock activity, nothing else to do */
	if (!(cm_read_mod_reg(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSTST) &
	      OMAP3430_CLKACTIVITY_IVA2_MASK))
		return;

	/* Reset IVA2 */
	prm_write_mod_reg(OMAP3430_RST1_IVA2 |
			  OMAP3430_RST2_IVA2 |
			  OMAP3430_RST3_IVA2,
			  OMAP3430_IVA2_MOD, RM_RSTCTRL);

	/* Enable IVA2 clock */
	cm_write_mod_reg(OMAP3430_CM_FCLKEN_IVA2_EN_IVA2,
			 OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Set IVA2 boot mode to 'idle' */
	omap_ctrl_writel(OMAP3_IVA2_BOOTMOD_IDLE,
			 OMAP343X_CONTROL_IVA2_BOOTMOD);

	/* Un-reset IVA2 */
	prm_write_mod_reg(0, OMAP3430_IVA2_MOD, RM_RSTCTRL);

	/* Disable IVA2 clock */
	cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Reset IVA2 */
	prm_write_mod_reg(OMAP3430_RST1_IVA2 |
			  OMAP3430_RST2_IVA2 |
			  OMAP3430_RST3_IVA2,
			  OMAP3430_IVA2_MOD, RM_RSTCTRL);
}

static void __init omap3_d2d_idle(void)
{
	u16 mask, padconf;

	/* In a stand alone OMAP3430 where there is not a stacked
	 * modem for the D2D Idle Ack and D2D MStandby must be pulled
	 * high. S CONTROL_PADCONF_SAD2D_IDLEACK and
	 * CONTROL_PADCONF_SAD2D_MSTDBY to have a pull up. */
	mask = (1 << 4) | (1 << 3); /* pull-up, enabled */
	padconf = omap_ctrl_readw(OMAP3_PADCONF_SAD2D_MSTANDBY);
	padconf |= mask;
	omap_ctrl_writew(padconf, OMAP3_PADCONF_SAD2D_MSTANDBY);

	padconf = omap_ctrl_readw(OMAP3_PADCONF_SAD2D_IDLEACK);
	padconf |= mask;
	omap_ctrl_writew(padconf, OMAP3_PADCONF_SAD2D_IDLEACK);

	/* reset modem */
	prm_write_mod_reg(OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RSTPWRON |
			  OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RST,
			  CORE_MOD, RM_RSTCTRL);
	prm_write_mod_reg(0, CORE_MOD, RM_RSTCTRL);
}

static void __init prcm_setup_regs(void)
{
	/* XXX Reset all wkdeps. This should be done when initializing
	 * powerdomains */
	prm_write_mod_reg(0, OMAP3430_IVA2_MOD, PM_WKDEP);
	prm_write_mod_reg(0, MPU_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_DSS_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_NEON_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_CAM_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_PER_MOD, PM_WKDEP);
	if (omap_rev() > OMAP3430_REV_ES1_0) {
		prm_write_mod_reg(0, OMAP3430ES2_SGX_MOD, PM_WKDEP);
		prm_write_mod_reg(0, OMAP3430ES2_USBHOST_MOD, PM_WKDEP);
	} else
		prm_write_mod_reg(0, GFX_MOD, PM_WKDEP);

	/*
	 * Enable interface clock autoidle for all modules.
	 * Note that in the long run this should be done by clockfw
	 */
	cm_write_mod_reg(
		OMAP3430_AUTO_MODEM |
		OMAP3430ES2_AUTO_MMC3 |
		OMAP3430ES2_AUTO_ICR |
		OMAP3430_AUTO_AES2 |
		OMAP3430_AUTO_SHA12 |
		OMAP3430_AUTO_DES2 |
		OMAP3430_AUTO_MMC2 |
		OMAP3430_AUTO_MMC1 |
		OMAP3430_AUTO_MSPRO |
		OMAP3430_AUTO_HDQ |
		OMAP3430_AUTO_MCSPI4 |
		OMAP3430_AUTO_MCSPI3 |
		OMAP3430_AUTO_MCSPI2 |
		OMAP3430_AUTO_MCSPI1 |
		OMAP3430_AUTO_I2C3 |
		OMAP3430_AUTO_I2C2 |
		OMAP3430_AUTO_I2C1 |
		OMAP3430_AUTO_UART2 |
		OMAP3430_AUTO_UART1 |
		OMAP3430_AUTO_GPT11 |
		OMAP3430_AUTO_GPT10 |
		OMAP3430_AUTO_MCBSP5 |
		OMAP3430_AUTO_MCBSP1 |
		OMAP3430ES1_AUTO_FAC | /* This is es1 only */
		OMAP3430_AUTO_MAILBOXES |
		OMAP3430_AUTO_OMAPCTRL |
		OMAP3430ES1_AUTO_FSHOSTUSB |
		OMAP3430_AUTO_HSOTGUSB |
		OMAP3430_AUTO_SAD2D |
		OMAP3430_AUTO_SSI,
		CORE_MOD, CM_AUTOIDLE1);

	cm_write_mod_reg(
		OMAP3430_AUTO_PKA |
		OMAP3430_AUTO_AES1 |
		OMAP3430_AUTO_RNG |
		OMAP3430_AUTO_SHA11 |
		OMAP3430_AUTO_DES1,
		CORE_MOD, CM_AUTOIDLE2);

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		cm_write_mod_reg(
			OMAP3430_AUTO_MAD2D |
			OMAP3430ES2_AUTO_USBTLL,
			CORE_MOD, CM_AUTOIDLE3);
	}

	cm_write_mod_reg(
		OMAP3430_AUTO_WDT2 |
		OMAP3430_AUTO_WDT1 |
		OMAP3430_AUTO_GPIO1 |
		OMAP3430_AUTO_32KSYNC |
		OMAP3430_AUTO_GPT12 |
		OMAP3430_AUTO_GPT1 ,
		WKUP_MOD, CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_DSS,
		OMAP3430_DSS_MOD,
		CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_CAM,
		OMAP3430_CAM_MOD,
		CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_GPIO6 |
		OMAP3430_AUTO_GPIO5 |
		OMAP3430_AUTO_GPIO4 |
		OMAP3430_AUTO_GPIO3 |
		OMAP3430_AUTO_GPIO2 |
		OMAP3430_AUTO_WDT3 |
		OMAP3430_AUTO_UART3 |
		OMAP3430_AUTO_GPT9 |
		OMAP3430_AUTO_GPT8 |
		OMAP3430_AUTO_GPT7 |
		OMAP3430_AUTO_GPT6 |
		OMAP3430_AUTO_GPT5 |
		OMAP3430_AUTO_GPT4 |
		OMAP3430_AUTO_GPT3 |
		OMAP3430_AUTO_GPT2 |
		OMAP3430_AUTO_MCBSP4 |
		OMAP3430_AUTO_MCBSP3 |
		OMAP3430_AUTO_MCBSP2,
		OMAP3430_PER_MOD,
		CM_AUTOIDLE);

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		cm_write_mod_reg(
			OMAP3430ES2_AUTO_USBHOST,
			OMAP3430ES2_USBHOST_MOD,
			CM_AUTOIDLE);
	}

	/*
	 * Set all plls to autoidle. This is needed until autoidle is
	 * enabled by clockfw
	 */
	cm_write_mod_reg(1 << OMAP3430_AUTO_IVA2_DPLL_SHIFT,
			 OMAP3430_IVA2_MOD, CM_AUTOIDLE2);
	cm_write_mod_reg(1 << OMAP3430_AUTO_MPU_DPLL_SHIFT,
			 MPU_MOD,
			 CM_AUTOIDLE2);
	cm_write_mod_reg((1 << OMAP3430_AUTO_PERIPH_DPLL_SHIFT) |
			 (1 << OMAP3430_AUTO_CORE_DPLL_SHIFT),
			 PLL_MOD,
			 CM_AUTOIDLE);
	cm_write_mod_reg(1 << OMAP3430ES2_AUTO_PERIPH2_DPLL_SHIFT,
			 PLL_MOD,
			 CM_AUTOIDLE2);

	/*
	 * Enable control of expternal oscillator through
	 * sys_clkreq. In the long run clock framework should
	 * take care of this.
	 */
	prm_rmw_mod_reg_bits(OMAP_AUTOEXTCLKMODE_MASK,
			     1 << OMAP_AUTOEXTCLKMODE_SHIFT,
			     OMAP3430_GR_MOD,
			     OMAP3_PRM_CLKSRC_CTRL_OFFSET);

	/* setup wakup source */
	prm_write_mod_reg(OMAP3430_EN_IO | OMAP3430_EN_GPIO1 |
			  OMAP3430_EN_GPT1 | OMAP3430_EN_GPT12,
			  WKUP_MOD, PM_WKEN);
	/* No need to write EN_IO, that is always enabled */
	prm_write_mod_reg(OMAP3430_EN_GPIO1 | OMAP3430_EN_GPT1 |
			  OMAP3430_EN_GPT12,
			  WKUP_MOD, OMAP3430_PM_MPUGRPSEL);
	/* For some reason IO doesn't generate wakeup event even if
	 * it is selected to mpu wakeup goup */
	prm_write_mod_reg(OMAP3430_IO_EN | OMAP3430_WKUP_EN,
			  OCP_MOD, OMAP3_PRM_IRQENABLE_MPU_OFFSET);

	/* Don't attach IVA interrupts */
	prm_write_mod_reg(0, WKUP_MOD, OMAP3430_PM_IVAGRPSEL);
	prm_write_mod_reg(0, CORE_MOD, OMAP3430_PM_IVAGRPSEL1);
	prm_write_mod_reg(0, CORE_MOD, OMAP3430ES2_PM_IVAGRPSEL3);
	prm_write_mod_reg(0, OMAP3430_PER_MOD, OMAP3430_PM_IVAGRPSEL);

	/* Clear any pending 'reset' flags */
	prm_write_mod_reg(0xffffffff, MPU_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, CORE_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, OMAP3430_PER_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, OMAP3430_EMU_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, OMAP3430_NEON_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, OMAP3430_DSS_MOD, RM_RSTST);
	prm_write_mod_reg(0xffffffff, OMAP3430ES2_USBHOST_MOD, RM_RSTST);

	/* Clear any pending PRCM interrupts */
	prm_write_mod_reg(0, OCP_MOD, OMAP3_PRM_IRQSTATUS_MPU_OFFSET);

	omap3_iva_idle();
	omap3_d2d_idle();
}

static int __init pwrdms_setup(struct powerdomain *pwrdm)
{
	struct power_state *pwrst;

	if (!pwrdm->pwrsts)
		return 0;

	pwrst = kmalloc(sizeof(struct power_state), GFP_KERNEL);
	if (!pwrst)
		return -ENOMEM;
	pwrst->pwrdm = pwrdm;
	pwrst->next_state = PWRDM_POWER_RET;
	list_add(&pwrst->node, &pwrst_list);

	if (pwrdm_has_hdwr_sar(pwrdm))
		pwrdm_enable_hdwr_sar(pwrdm);

	return set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
}

/*
 * Enable hw supervised mode for all clockdomains if it's
 * supported. Initiate sleep transition for other clockdomains, if
 * they are not used
 */
static int __init clkdms_setup(struct clockdomain *clkdm)
{
	if (clkdm->flags & CLKDM_CAN_ENABLE_AUTO)
		omap2_clkdm_allow_idle(clkdm);
	else if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
		 atomic_read(&clkdm->usecount) == 0)
		omap2_clkdm_sleep(clkdm);
	return 0;
}

int __init omap3_pm_init(void)
{
	struct power_state *pwrst, *tmp;
	int ret;

	if (!cpu_is_omap34xx())
		return -ENODEV;

	printk(KERN_ERR "Power Management for TI OMAP3.\n");

	/* XXX prcm_setup_regs needs to be before enabling hw
	 * supervised mode for powerdomains */
	prcm_setup_regs();

	ret = request_irq(INT_34XX_PRCM_MPU_IRQ,
			  (irq_handler_t)prcm_interrupt_handler,
			  IRQF_DISABLED, "prcm", NULL);
	if (ret) {
		printk(KERN_ERR "request_irq failed to register for 0x%x\n",
		       INT_34XX_PRCM_MPU_IRQ);
		goto err1;
	}

	ret = pwrdm_for_each(pwrdms_setup);
	if (ret) {
		printk(KERN_ERR "Failed to setup powerdomains\n");
		goto err2;
	}

	(void) clkdm_for_each(clkdms_setup);

	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");
	if (mpu_pwrdm == NULL) {
		printk(KERN_ERR "Failed to get mpu_pwrdm\n");
		goto err2;
	}

	_omap_sram_idle = omap_sram_push(omap34xx_cpu_suspend,
					 omap34xx_cpu_suspend_sz);

	suspend_set_ops(&omap_pm_ops);

	pm_idle = omap3_pm_idle;

err1:
	return ret;
err2:
	free_irq(INT_34XX_PRCM_MPU_IRQ, NULL);
	list_for_each_entry_safe(pwrst, tmp, &pwrst_list, node) {
		list_del(&pwrst->node);
		kfree(pwrst);
	}
	return ret;
}

late_initcall(omap3_pm_init);
