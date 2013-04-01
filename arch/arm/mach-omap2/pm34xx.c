/*
 * OMAP3 Power Management Routines
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Jouni Hogander
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/omap-dma.h>
#include <linux/platform_data/gpio-omap.h>

#include <trace/events/power.h>

#include <asm/fncpy.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>

#include "clockdomain.h"
#include "powerdomain.h"
#include "soc.h"
#include "common.h"
#include "cm3xxx.h"
#include "cm-regbits-34xx.h"
#include "gpmc.h"
#include "prm-regbits-34xx.h"
#include "prm3xxx.h"
#include "pm.h"
#include "sdrc.h"
#include "sram.h"
#include "control.h"

/* pm34xx errata defined in pm.h */
u16 pm34xx_errata;

struct power_state {
	struct powerdomain *pwrdm;
	u32 next_state;
#ifdef CONFIG_SUSPEND
	u32 saved_state;
#endif
	struct list_head node;
};

static LIST_HEAD(pwrst_list);

static int (*_omap_save_secure_sram)(u32 *addr);
void (*omap3_do_wfi_sram)(void);

static struct powerdomain *mpu_pwrdm, *neon_pwrdm;
static struct powerdomain *core_pwrdm, *per_pwrdm;

static void omap3_core_save_context(void)
{
	omap3_ctrl_save_padconf();

	/*
	 * Force write last pad into memory, as this can fail in some
	 * cases according to errata 1.157, 1.185
	 */
	omap_ctrl_writel(omap_ctrl_readl(OMAP343X_PADCONF_ETK_D14),
		OMAP343X_CONTROL_MEM_WKUP + 0x2a0);

	/* Save the Interrupt controller context */
	omap_intc_save_context();
	/* Save the GPMC context */
	omap3_gpmc_save_context();
	/* Save the system control module context, padconf already save above*/
	omap3_control_save_context();
	omap_dma_global_context_save();
}

static void omap3_core_restore_context(void)
{
	/* Restore the control module context, padconf restored by h/w */
	omap3_control_restore_context();
	/* Restore the GPMC context */
	omap3_gpmc_restore_context();
	/* Restore the interrupt controller context */
	omap_intc_restore_context();
	omap_dma_global_context_restore();
}

/*
 * FIXME: This function should be called before entering off-mode after
 * OMAP3 secure services have been accessed. Currently it is only called
 * once during boot sequence, but this works as we are not using secure
 * services.
 */
static void omap3_save_secure_ram_context(void)
{
	u32 ret;
	int mpu_next_state = pwrdm_read_next_pwrst(mpu_pwrdm);

	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		/*
		 * MPU next state must be set to POWER_ON temporarily,
		 * otherwise the WFI executed inside the ROM code
		 * will hang the system.
		 */
		pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_ON);
		ret = _omap_save_secure_sram((u32 *)
				__pa(omap3_secure_ram_storage));
		pwrdm_set_next_pwrst(mpu_pwrdm, mpu_next_state);
		/* Following is for error tracking, it should not happen */
		if (ret) {
			pr_err("save_secure_sram() returns %08x\n", ret);
			while (1)
				;
		}
	}
}

/*
 * PRCM Interrupt Handler Helper Function
 *
 * The purpose of this function is to clear any wake-up events latched
 * in the PRCM PM_WKST_x registers. It is possible that a wake-up event
 * may occur whilst attempting to clear a PM_WKST_x register and thus
 * set another bit in this register. A while loop is used to ensure
 * that any peripheral wake-up events occurring while attempting to
 * clear the PM_WKST_x are detected and cleared.
 */
static int prcm_clear_mod_irqs(s16 module, u8 regs, u32 ignore_bits)
{
	u32 wkst, fclk, iclk, clken;
	u16 wkst_off = (regs == 3) ? OMAP3430ES2_PM_WKST3 : PM_WKST1;
	u16 fclk_off = (regs == 3) ? OMAP3430ES2_CM_FCLKEN3 : CM_FCLKEN1;
	u16 iclk_off = (regs == 3) ? CM_ICLKEN3 : CM_ICLKEN1;
	u16 grpsel_off = (regs == 3) ?
		OMAP3430ES2_PM_MPUGRPSEL3 : OMAP3430_PM_MPUGRPSEL;
	int c = 0;

	wkst = omap2_prm_read_mod_reg(module, wkst_off);
	wkst &= omap2_prm_read_mod_reg(module, grpsel_off);
	wkst &= ~ignore_bits;
	if (wkst) {
		iclk = omap2_cm_read_mod_reg(module, iclk_off);
		fclk = omap2_cm_read_mod_reg(module, fclk_off);
		while (wkst) {
			clken = wkst;
			omap2_cm_set_mod_reg_bits(clken, module, iclk_off);
			/*
			 * For USBHOST, we don't know whether HOST1 or
			 * HOST2 woke us up, so enable both f-clocks
			 */
			if (module == OMAP3430ES2_USBHOST_MOD)
				clken |= 1 << OMAP3430ES2_EN_USBHOST2_SHIFT;
			omap2_cm_set_mod_reg_bits(clken, module, fclk_off);
			omap2_prm_write_mod_reg(wkst, module, wkst_off);
			wkst = omap2_prm_read_mod_reg(module, wkst_off);
			wkst &= ~ignore_bits;
			c++;
		}
		omap2_cm_write_mod_reg(iclk, module, iclk_off);
		omap2_cm_write_mod_reg(fclk, module, fclk_off);
	}

	return c;
}

static irqreturn_t _prcm_int_handle_io(int irq, void *unused)
{
	int c;

	c = prcm_clear_mod_irqs(WKUP_MOD, 1,
		~(OMAP3430_ST_IO_MASK | OMAP3430_ST_IO_CHAIN_MASK));

	return c ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t _prcm_int_handle_wakeup(int irq, void *unused)
{
	int c;

	/*
	 * Clear all except ST_IO and ST_IO_CHAIN for wkup module,
	 * these are handled in a separate handler to avoid acking
	 * IO events before parsing in mux code
	 */
	c = prcm_clear_mod_irqs(WKUP_MOD, 1,
		OMAP3430_ST_IO_MASK | OMAP3430_ST_IO_CHAIN_MASK);
	c += prcm_clear_mod_irqs(CORE_MOD, 1, 0);
	c += prcm_clear_mod_irqs(OMAP3430_PER_MOD, 1, 0);
	if (omap_rev() > OMAP3430_REV_ES1_0) {
		c += prcm_clear_mod_irqs(CORE_MOD, 3, 0);
		c += prcm_clear_mod_irqs(OMAP3430ES2_USBHOST_MOD, 1, 0);
	}

	return c ? IRQ_HANDLED : IRQ_NONE;
}

static void omap34xx_save_context(u32 *save)
{
	u32 val;

	/* Read Auxiliary Control Register */
	asm("mrc p15, 0, %0, c1, c0, 1" : "=r" (val));
	*save++ = 1;
	*save++ = val;

	/* Read L2 AUX ctrl register */
	asm("mrc p15, 1, %0, c9, c0, 2" : "=r" (val));
	*save++ = 1;
	*save++ = val;
}

static int omap34xx_do_sram_idle(unsigned long save_state)
{
	omap34xx_cpu_suspend(save_state);
	return 0;
}

void omap_sram_idle(void)
{
	/* Variable to tell what needs to be saved and restored
	 * in omap_sram_idle*/
	/* save_state = 0 => Nothing to save and restored */
	/* save_state = 1 => Only L1 and logic lost */
	/* save_state = 2 => Only L2 lost */
	/* save_state = 3 => L1, L2 and logic lost */
	int save_state = 0;
	int mpu_next_state = PWRDM_POWER_ON;
	int per_next_state = PWRDM_POWER_ON;
	int core_next_state = PWRDM_POWER_ON;
	int per_going_off;
	int core_prev_state;
	u32 sdrc_pwr = 0;

	mpu_next_state = pwrdm_read_next_pwrst(mpu_pwrdm);
	switch (mpu_next_state) {
	case PWRDM_POWER_ON:
	case PWRDM_POWER_RET:
		/* No need to save context */
		save_state = 0;
		break;
	case PWRDM_POWER_OFF:
		save_state = 3;
		break;
	default:
		/* Invalid state */
		pr_err("Invalid mpu state in sram_idle\n");
		return;
	}

	/* NEON control */
	if (pwrdm_read_pwrst(neon_pwrdm) == PWRDM_POWER_ON)
		pwrdm_set_next_pwrst(neon_pwrdm, mpu_next_state);

	/* Enable IO-PAD and IO-CHAIN wakeups */
	per_next_state = pwrdm_read_next_pwrst(per_pwrdm);
	core_next_state = pwrdm_read_next_pwrst(core_pwrdm);

	pwrdm_pre_transition(NULL);

	/* PER */
	if (per_next_state < PWRDM_POWER_ON) {
		per_going_off = (per_next_state == PWRDM_POWER_OFF) ? 1 : 0;
		omap2_gpio_prepare_for_idle(per_going_off);
	}

	/* CORE */
	if (core_next_state < PWRDM_POWER_ON) {
		if (core_next_state == PWRDM_POWER_OFF) {
			omap3_core_save_context();
			omap3_cm_save_context();
		}
	}

	omap3_intc_prepare_idle();

	/*
	 * On EMU/HS devices ROM code restores a SRDC value
	 * from scratchpad which has automatic self refresh on timeout
	 * of AUTO_CNT = 1 enabled. This takes care of erratum ID i443.
	 * Hence store/restore the SDRC_POWER register here.
	 */
	if (cpu_is_omap3430() && omap_rev() >= OMAP3430_REV_ES3_0 &&
	    (omap_type() == OMAP2_DEVICE_TYPE_EMU ||
	     omap_type() == OMAP2_DEVICE_TYPE_SEC) &&
	    core_next_state == PWRDM_POWER_OFF)
		sdrc_pwr = sdrc_read_reg(SDRC_POWER);

	/*
	 * omap3_arm_context is the location where some ARM context
	 * get saved. The rest is placed on the stack, and restored
	 * from there before resuming.
	 */
	if (save_state)
		omap34xx_save_context(omap3_arm_context);
	if (save_state == 1 || save_state == 3)
		cpu_suspend(save_state, omap34xx_do_sram_idle);
	else
		omap34xx_do_sram_idle(save_state);

	/* Restore normal SDRC POWER settings */
	if (cpu_is_omap3430() && omap_rev() >= OMAP3430_REV_ES3_0 &&
	    (omap_type() == OMAP2_DEVICE_TYPE_EMU ||
	     omap_type() == OMAP2_DEVICE_TYPE_SEC) &&
	    core_next_state == PWRDM_POWER_OFF)
		sdrc_write_reg(sdrc_pwr, SDRC_POWER);

	/* CORE */
	if (core_next_state < PWRDM_POWER_ON) {
		core_prev_state = pwrdm_read_prev_pwrst(core_pwrdm);
		if (core_prev_state == PWRDM_POWER_OFF) {
			omap3_core_restore_context();
			omap3_cm_restore_context();
			omap3_sram_restore_context();
			omap2_sms_restore_context();
		}
		if (core_next_state == PWRDM_POWER_OFF)
			omap2_prm_clear_mod_reg_bits(OMAP3430_AUTO_OFF_MASK,
					       OMAP3430_GR_MOD,
					       OMAP3_PRM_VOLTCTRL_OFFSET);
	}
	omap3_intc_resume_idle();

	pwrdm_post_transition(NULL);

	/* PER */
	if (per_next_state < PWRDM_POWER_ON)
		omap2_gpio_resume_after_idle();
}

static void omap3_pm_idle(void)
{
	local_fiq_disable();

	if (omap_irq_pending())
		goto out;

	trace_cpu_idle(1, smp_processor_id());

	omap_sram_idle();

	trace_cpu_idle(PWR_EVENT_EXIT, smp_processor_id());

out:
	local_fiq_enable();
}

#ifdef CONFIG_SUSPEND
static int omap3_pm_suspend(void)
{
	struct power_state *pwrst;
	int state, ret = 0;

	/* Read current next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node)
		pwrst->saved_state = pwrdm_read_next_pwrst(pwrst->pwrdm);
	/* Set ones wanted by suspend */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (omap_set_pwrdm_state(pwrst->pwrdm, pwrst->next_state))
			goto restore;
		if (pwrdm_clear_all_prev_pwrst(pwrst->pwrdm))
			goto restore;
	}

	omap3_intc_suspend();

	omap_sram_idle();

restore:
	/* Restore next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		state = pwrdm_read_prev_pwrst(pwrst->pwrdm);
		if (state > pwrst->next_state) {
			pr_info("Powerdomain (%s) didn't enter target state %d\n",
				pwrst->pwrdm->name, pwrst->next_state);
			ret = -1;
		}
		omap_set_pwrdm_state(pwrst->pwrdm, pwrst->saved_state);
	}
	if (ret)
		pr_err("Could not enter target state in pm_suspend\n");
	else
		pr_info("Successfully put all powerdomains to target state\n");

	return ret;
}

#endif /* CONFIG_SUSPEND */


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
	omap2_cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* if no clock activity, nothing else to do */
	if (!(omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSTST) &
	      OMAP3430_CLKACTIVITY_IVA2_MASK))
		return;

	/* Reset IVA2 */
	omap2_prm_write_mod_reg(OMAP3430_RST1_IVA2_MASK |
			  OMAP3430_RST2_IVA2_MASK |
			  OMAP3430_RST3_IVA2_MASK,
			  OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

	/* Enable IVA2 clock */
	omap2_cm_write_mod_reg(OMAP3430_CM_FCLKEN_IVA2_EN_IVA2_MASK,
			 OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Set IVA2 boot mode to 'idle' */
	omap_ctrl_writel(OMAP3_IVA2_BOOTMOD_IDLE,
			 OMAP343X_CONTROL_IVA2_BOOTMOD);

	/* Un-reset IVA2 */
	omap2_prm_write_mod_reg(0, OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

	/* Disable IVA2 clock */
	omap2_cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Reset IVA2 */
	omap2_prm_write_mod_reg(OMAP3430_RST1_IVA2_MASK |
			  OMAP3430_RST2_IVA2_MASK |
			  OMAP3430_RST3_IVA2_MASK,
			  OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
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
	omap2_prm_write_mod_reg(OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RSTPWRON_MASK |
			  OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RST_MASK,
			  CORE_MOD, OMAP2_RM_RSTCTRL);
	omap2_prm_write_mod_reg(0, CORE_MOD, OMAP2_RM_RSTCTRL);
}

static void __init prcm_setup_regs(void)
{
	u32 omap3630_en_uart4_mask = cpu_is_omap3630() ?
					OMAP3630_EN_UART4_MASK : 0;
	u32 omap3630_grpsel_uart4_mask = cpu_is_omap3630() ?
					OMAP3630_GRPSEL_UART4_MASK : 0;

	/* XXX This should be handled by hwmod code or SCM init code */
	omap_ctrl_writel(OMAP3430_AUTOIDLE_MASK, OMAP2_CONTROL_SYSCONFIG);

	/*
	 * Enable control of expternal oscillator through
	 * sys_clkreq. In the long run clock framework should
	 * take care of this.
	 */
	omap2_prm_rmw_mod_reg_bits(OMAP_AUTOEXTCLKMODE_MASK,
			     1 << OMAP_AUTOEXTCLKMODE_SHIFT,
			     OMAP3430_GR_MOD,
			     OMAP3_PRM_CLKSRC_CTRL_OFFSET);

	/* setup wakup source */
	omap2_prm_write_mod_reg(OMAP3430_EN_IO_MASK | OMAP3430_EN_GPIO1_MASK |
			  OMAP3430_EN_GPT1_MASK | OMAP3430_EN_GPT12_MASK,
			  WKUP_MOD, PM_WKEN);
	/* No need to write EN_IO, that is always enabled */
	omap2_prm_write_mod_reg(OMAP3430_GRPSEL_GPIO1_MASK |
			  OMAP3430_GRPSEL_GPT1_MASK |
			  OMAP3430_GRPSEL_GPT12_MASK,
			  WKUP_MOD, OMAP3430_PM_MPUGRPSEL);

	/* Enable PM_WKEN to support DSS LPR */
	omap2_prm_write_mod_reg(OMAP3430_PM_WKEN_DSS_EN_DSS_MASK,
				OMAP3430_DSS_MOD, PM_WKEN);

	/* Enable wakeups in PER */
	omap2_prm_write_mod_reg(omap3630_en_uart4_mask |
			  OMAP3430_EN_GPIO2_MASK | OMAP3430_EN_GPIO3_MASK |
			  OMAP3430_EN_GPIO4_MASK | OMAP3430_EN_GPIO5_MASK |
			  OMAP3430_EN_GPIO6_MASK | OMAP3430_EN_UART3_MASK |
			  OMAP3430_EN_MCBSP2_MASK | OMAP3430_EN_MCBSP3_MASK |
			  OMAP3430_EN_MCBSP4_MASK,
			  OMAP3430_PER_MOD, PM_WKEN);
	/* and allow them to wake up MPU */
	omap2_prm_write_mod_reg(omap3630_grpsel_uart4_mask |
			  OMAP3430_GRPSEL_GPIO2_MASK |
			  OMAP3430_GRPSEL_GPIO3_MASK |
			  OMAP3430_GRPSEL_GPIO4_MASK |
			  OMAP3430_GRPSEL_GPIO5_MASK |
			  OMAP3430_GRPSEL_GPIO6_MASK |
			  OMAP3430_GRPSEL_UART3_MASK |
			  OMAP3430_GRPSEL_MCBSP2_MASK |
			  OMAP3430_GRPSEL_MCBSP3_MASK |
			  OMAP3430_GRPSEL_MCBSP4_MASK,
			  OMAP3430_PER_MOD, OMAP3430_PM_MPUGRPSEL);

	/* Don't attach IVA interrupts */
	if (omap3_has_iva()) {
		omap2_prm_write_mod_reg(0, WKUP_MOD, OMAP3430_PM_IVAGRPSEL);
		omap2_prm_write_mod_reg(0, CORE_MOD, OMAP3430_PM_IVAGRPSEL1);
		omap2_prm_write_mod_reg(0, CORE_MOD, OMAP3430ES2_PM_IVAGRPSEL3);
		omap2_prm_write_mod_reg(0, OMAP3430_PER_MOD,
					OMAP3430_PM_IVAGRPSEL);
	}

	/* Clear any pending 'reset' flags */
	omap2_prm_write_mod_reg(0xffffffff, MPU_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, CORE_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, OMAP3430_PER_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, OMAP3430_EMU_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, OMAP3430_NEON_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, OMAP3430_DSS_MOD, OMAP2_RM_RSTST);
	omap2_prm_write_mod_reg(0xffffffff, OMAP3430ES2_USBHOST_MOD, OMAP2_RM_RSTST);

	/* Clear any pending PRCM interrupts */
	omap2_prm_write_mod_reg(0, OCP_MOD, OMAP3_PRM_IRQSTATUS_MPU_OFFSET);

	if (omap3_has_iva())
		omap3_iva_idle();

	omap3_d2d_idle();
}

void omap3_pm_off_mode_enable(int enable)
{
	struct power_state *pwrst;
	u32 state;

	if (enable)
		state = PWRDM_POWER_OFF;
	else
		state = PWRDM_POWER_RET;

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (IS_PM34XX_ERRATUM(PM_SDRC_WAKEUP_ERRATUM_i583) &&
				pwrst->pwrdm == core_pwrdm &&
				state == PWRDM_POWER_OFF) {
			pwrst->next_state = PWRDM_POWER_RET;
			pr_warn("%s: Core OFF disabled due to errata i583\n",
				__func__);
		} else {
			pwrst->next_state = state;
		}
		omap_set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
	}
}

int omap3_pm_get_suspend_state(struct powerdomain *pwrdm)
{
	struct power_state *pwrst;

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (pwrst->pwrdm == pwrdm)
			return pwrst->next_state;
	}
	return -EINVAL;
}

int omap3_pm_set_suspend_state(struct powerdomain *pwrdm, int state)
{
	struct power_state *pwrst;

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (pwrst->pwrdm == pwrdm) {
			pwrst->next_state = state;
			return 0;
		}
	}
	return -EINVAL;
}

static int __init pwrdms_setup(struct powerdomain *pwrdm, void *unused)
{
	struct power_state *pwrst;

	if (!pwrdm->pwrsts)
		return 0;

	pwrst = kmalloc(sizeof(struct power_state), GFP_ATOMIC);
	if (!pwrst)
		return -ENOMEM;
	pwrst->pwrdm = pwrdm;
	pwrst->next_state = PWRDM_POWER_RET;
	list_add(&pwrst->node, &pwrst_list);

	if (pwrdm_has_hdwr_sar(pwrdm))
		pwrdm_enable_hdwr_sar(pwrdm);

	return omap_set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
}

/*
 * Push functions to SRAM
 *
 * The minimum set of functions is pushed to SRAM for execution:
 * - omap3_do_wfi for erratum i581 WA,
 * - save_secure_ram_context for security extensions.
 */
void omap_push_sram_idle(void)
{
	omap3_do_wfi_sram = omap_sram_push(omap3_do_wfi, omap3_do_wfi_sz);

	if (omap_type() != OMAP2_DEVICE_TYPE_GP)
		_omap_save_secure_sram = omap_sram_push(save_secure_ram_context,
				save_secure_ram_context_sz);
}

static void __init pm_errata_configure(void)
{
	if (cpu_is_omap3630()) {
		pm34xx_errata |= PM_RTA_ERRATUM_i608;
		/* Enable the l2 cache toggling in sleep logic */
		enable_omap3630_toggle_l2_on_restore();
		if (omap_rev() < OMAP3630_REV_ES1_2)
			pm34xx_errata |= (PM_SDRC_WAKEUP_ERRATUM_i583 |
					  PM_PER_MEMORIES_ERRATUM_i582);
	} else if (cpu_is_omap34xx()) {
		pm34xx_errata |= PM_PER_MEMORIES_ERRATUM_i582;
	}
}

int __init omap3_pm_init(void)
{
	struct power_state *pwrst, *tmp;
	struct clockdomain *neon_clkdm, *mpu_clkdm, *per_clkdm, *wkup_clkdm;
	int ret;

	if (!omap3_has_io_chain_ctrl())
		pr_warning("PM: no software I/O chain control; some wakeups may be lost\n");

	pm_errata_configure();

	/* XXX prcm_setup_regs needs to be before enabling hw
	 * supervised mode for powerdomains */
	prcm_setup_regs();

	ret = request_irq(omap_prcm_event_to_irq("wkup"),
		_prcm_int_handle_wakeup, IRQF_NO_SUSPEND, "pm_wkup", NULL);

	if (ret) {
		pr_err("pm: Failed to request pm_wkup irq\n");
		goto err1;
	}

	/* IO interrupt is shared with mux code */
	ret = request_irq(omap_prcm_event_to_irq("io"),
		_prcm_int_handle_io, IRQF_SHARED | IRQF_NO_SUSPEND, "pm_io",
		omap3_pm_init);
	enable_irq(omap_prcm_event_to_irq("io"));

	if (ret) {
		pr_err("pm: Failed to request pm_io irq\n");
		goto err2;
	}

	ret = pwrdm_for_each(pwrdms_setup, NULL);
	if (ret) {
		pr_err("Failed to setup powerdomains\n");
		goto err3;
	}

	(void) clkdm_for_each(omap_pm_clkdms_setup, NULL);

	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");
	if (mpu_pwrdm == NULL) {
		pr_err("Failed to get mpu_pwrdm\n");
		ret = -EINVAL;
		goto err3;
	}

	neon_pwrdm = pwrdm_lookup("neon_pwrdm");
	per_pwrdm = pwrdm_lookup("per_pwrdm");
	core_pwrdm = pwrdm_lookup("core_pwrdm");

	neon_clkdm = clkdm_lookup("neon_clkdm");
	mpu_clkdm = clkdm_lookup("mpu_clkdm");
	per_clkdm = clkdm_lookup("per_clkdm");
	wkup_clkdm = clkdm_lookup("wkup_clkdm");

#ifdef CONFIG_SUSPEND
	omap_pm_suspend = omap3_pm_suspend;
#endif

	arm_pm_idle = omap3_pm_idle;
	omap3_idle_init();

	/*
	 * RTA is disabled during initialization as per erratum i608
	 * it is safer to disable RTA by the bootloader, but we would like
	 * to be doubly sure here and prevent any mishaps.
	 */
	if (IS_PM34XX_ERRATUM(PM_RTA_ERRATUM_i608))
		omap3630_ctrl_disable_rta();

	/*
	 * The UART3/4 FIFO and the sidetone memory in McBSP2/3 are
	 * not correctly reset when the PER powerdomain comes back
	 * from OFF or OSWR when the CORE powerdomain is kept active.
	 * See OMAP36xx Erratum i582 "PER Domain reset issue after
	 * Domain-OFF/OSWR Wakeup".  This wakeup dependency is not a
	 * complete workaround.  The kernel must also prevent the PER
	 * powerdomain from going to OSWR/OFF while the CORE
	 * powerdomain is not going to OSWR/OFF.  And if PER last
	 * power state was off while CORE last power state was ON, the
	 * UART3/4 and McBSP2/3 SIDETONE devices need to run a
	 * self-test using their loopback tests; if that fails, those
	 * devices are unusable until the PER/CORE can complete a transition
	 * from ON to OSWR/OFF and then back to ON.
	 *
	 * XXX Technically this workaround is only needed if off-mode
	 * or OSWR is enabled.
	 */
	if (IS_PM34XX_ERRATUM(PM_PER_MEMORIES_ERRATUM_i582))
		clkdm_add_wkdep(per_clkdm, wkup_clkdm);

	clkdm_add_wkdep(neon_clkdm, mpu_clkdm);
	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		omap3_secure_ram_storage =
			kmalloc(0x803F, GFP_KERNEL);
		if (!omap3_secure_ram_storage)
			pr_err("Memory allocation failed when allocating for secure sram context\n");

		local_irq_disable();
		local_fiq_disable();

		omap_dma_global_context_save();
		omap3_save_secure_ram_context();
		omap_dma_global_context_restore();

		local_irq_enable();
		local_fiq_enable();
	}

	omap3_save_scratchpad_contents();
	return ret;

err3:
	list_for_each_entry_safe(pwrst, tmp, &pwrst_list, node) {
		list_del(&pwrst->node);
		kfree(pwrst);
	}
	free_irq(omap_prcm_event_to_irq("io"), omap3_pm_init);
err2:
	free_irq(omap_prcm_event_to_irq("wkup"), NULL);
err1:
	return ret;
}
