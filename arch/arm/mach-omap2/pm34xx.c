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
#include <linux/omap-gpmc.h>
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
#include "prm-regbits-34xx.h"
#include "prm3xxx.h"
#include "pm.h"
#include "sdrc.h"
#include "sram.h"
#include "control.h"
#include "vc.h"

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
		ret = _omap_save_secure_sram((u32 *)(unsigned long)
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

static irqreturn_t _prcm_int_handle_io(int irq, void *unused)
{
	int c;

	c = omap_prm_clear_mod_irqs(WKUP_MOD, 1, OMAP3430_ST_IO_MASK |
				    OMAP3430_ST_IO_CHAIN_MASK);

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
	c = omap_prm_clear_mod_irqs(WKUP_MOD, 1, ~(OMAP3430_ST_IO_MASK |
						   OMAP3430_ST_IO_CHAIN_MASK));
	c += omap_prm_clear_mod_irqs(CORE_MOD, 1, ~0);
	c += omap_prm_clear_mod_irqs(OMAP3430_PER_MOD, 1, ~0);
	if (omap_rev() > OMAP3430_REV_ES1_0) {
		c += omap_prm_clear_mod_irqs(CORE_MOD, 3, ~0);
		c += omap_prm_clear_mod_irqs(OMAP3430ES2_USBHOST_MOD, 1, ~0);
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

	/* Configure PMIC signaling for I2C4 or sys_off_mode */
	omap3_vc_set_pmic_signaling(core_next_state);

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
	if (core_next_state < PWRDM_POWER_ON &&
	    pwrdm_read_prev_pwrst(core_pwrdm) == PWRDM_POWER_OFF) {
		omap3_core_restore_context();
		omap3_cm_restore_context();
		omap3_sram_restore_context();
		omap2_sms_restore_context();
	} else {
		/*
		 * In off-mode resume path above, omap3_core_restore_context
		 * also handles the INTC autoidle restore done here so limit
		 * this to non-off mode resume paths so we don't do it twice.
		 */
		omap3_intc_resume_idle();
	}

	pwrdm_post_transition(NULL);

	/* PER */
	if (per_next_state < PWRDM_POWER_ON)
		omap2_gpio_resume_after_idle();
}

static void omap3_pm_idle(void)
{
	if (omap_irq_pending())
		return;

	trace_cpu_idle_rcuidle(1, smp_processor_id());

	omap_sram_idle();

	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
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
#else
#define omap3_pm_suspend NULL
#endif /* CONFIG_SUSPEND */

static void __init prcm_setup_regs(void)
{
	omap3_ctrl_init();

	omap3_prm_init_pm(cpu_is_omap3630(), omap3_has_iva());
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
		pr_warn("PM: no software I/O chain control; some wakeups may be lost\n");

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

	omap_common_suspend_init(omap3_pm_suspend);

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

		omap_dma_global_context_save();
		omap3_save_secure_ram_context();
		omap_dma_global_context_restore();

		local_irq_enable();
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
