/*
 * arch/arm/mach-at91/pm.c
 * AT91 Power Management
 *
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/gpio.h>
#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <linux/atomic.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>

#include <mach/at91_pmc.h>
#include <mach/cpu.h>

#include "at91_aic.h"
#include "generic.h"
#include "pm.h"

/*
 * Show the reason for the previous system reset.
 */

#include "at91_rstc.h"
#include "at91_shdwc.h"

static void __init show_reset_status(void)
{
	static char reset[] __initdata = "reset";

	static char general[] __initdata = "general";
	static char wakeup[] __initdata = "wakeup";
	static char watchdog[] __initdata = "watchdog";
	static char software[] __initdata = "software";
	static char user[] __initdata = "user";
	static char unknown[] __initdata = "unknown";

	static char signal[] __initdata = "signal";
	static char rtc[] __initdata = "rtc";
	static char rtt[] __initdata = "rtt";
	static char restore[] __initdata = "power-restored";

	char *reason, *r2 = reset;
	u32 reset_type, wake_type;

	if (!at91_shdwc_base || !at91_rstc_base)
		return;

	reset_type = at91_rstc_read(AT91_RSTC_SR) & AT91_RSTC_RSTTYP;
	wake_type = at91_shdwc_read(AT91_SHDW_SR);

	switch (reset_type) {
	case AT91_RSTC_RSTTYP_GENERAL:
		reason = general;
		break;
	case AT91_RSTC_RSTTYP_WAKEUP:
		/* board-specific code enabled the wakeup sources */
		reason = wakeup;

		/* "wakeup signal" */
		if (wake_type & AT91_SHDW_WAKEUP0)
			r2 = signal;
		else {
			r2 = reason;
			if (wake_type & AT91_SHDW_RTTWK)	/* rtt wakeup */
				reason = rtt;
			else if (wake_type & AT91_SHDW_RTCWK)	/* rtc wakeup */
				reason = rtc;
			else if (wake_type == 0)	/* power-restored wakeup */
				reason = restore;
			else				/* unknown wakeup */
				reason = unknown;
		}
		break;
	case AT91_RSTC_RSTTYP_WATCHDOG:
		reason = watchdog;
		break;
	case AT91_RSTC_RSTTYP_SOFTWARE:
		reason = software;
		break;
	case AT91_RSTC_RSTTYP_USER:
		reason = user;
		break;
	default:
		reason = unknown;
		break;
	}
	pr_info("AT91: Starting after %s %s\n", reason, r2);
}

static int at91_pm_valid_state(suspend_state_t state)
{
	switch (state) {
		case PM_SUSPEND_ON:
		case PM_SUSPEND_STANDBY:
		case PM_SUSPEND_MEM:
			return 1;

		default:
			return 0;
	}
}


static suspend_state_t target_state;

/*
 * Called after processes are frozen, but before we shutdown devices.
 */
static int at91_pm_begin(suspend_state_t state)
{
	target_state = state;
	return 0;
}

/*
 * Verify that all the clocks are correct before entering
 * slow-clock mode.
 */
static int at91_pm_verify_clocks(void)
{
	unsigned long scsr;
	int i;

	scsr = at91_pmc_read(AT91_PMC_SCSR);

	/* USB must not be using PLLB */
	if (cpu_is_at91rm9200()) {
		if ((scsr & (AT91RM9200_PMC_UHP | AT91RM9200_PMC_UDP)) != 0) {
			pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
			return 0;
		}
	} else if (cpu_is_at91sam9260() || cpu_is_at91sam9261() || cpu_is_at91sam9263()
			|| cpu_is_at91sam9g20() || cpu_is_at91sam9g10()) {
		if ((scsr & (AT91SAM926x_PMC_UHP | AT91SAM926x_PMC_UDP)) != 0) {
			pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
			return 0;
		}
	}

	if (!IS_ENABLED(CONFIG_AT91_PROGRAMMABLE_CLOCKS))
		return 1;

	/* PCK0..PCK3 must be disabled, or configured to use clk32k */
	for (i = 0; i < 4; i++) {
		u32 css;

		if ((scsr & (AT91_PMC_PCK0 << i)) == 0)
			continue;

		css = at91_pmc_read(AT91_PMC_PCKR(i)) & AT91_PMC_CSS;
		if (css != AT91_PMC_CSS_SLOW) {
			pr_err("AT91: PM - Suspend-to-RAM with PCK%d src %d\n", i, css);
			return 0;
		}
	}

	return 1;
}

/*
 * Call this from platform driver suspend() to see how deeply to suspend.
 * For example, some controllers (like OHCI) need one of the PLL clocks
 * in order to act as a wakeup source, and those are not available when
 * going into slow clock mode.
 *
 * REVISIT: generalize as clk_will_be_available(clk)?  Other platforms have
 * the very same problem (but not using at91 main_clk), and it'd be better
 * to add one generic API rather than lots of platform-specific ones.
 */
int at91_suspend_entering_slow_clock(void)
{
	return (target_state == PM_SUSPEND_MEM);
}
EXPORT_SYMBOL(at91_suspend_entering_slow_clock);


static void (*slow_clock)(void __iomem *pmc, void __iomem *ramc0,
			  void __iomem *ramc1, int memctrl);

#ifdef CONFIG_AT91_SLOW_CLOCK
extern void at91_slow_clock(void __iomem *pmc, void __iomem *ramc0,
			    void __iomem *ramc1, int memctrl);
extern u32 at91_slow_clock_sz;
#endif

static int at91_pm_enter(suspend_state_t state)
{
	if (of_have_populated_dt())
		at91_pinctrl_gpio_suspend();
	else
		at91_gpio_suspend();
	at91_irq_suspend();

	pr_debug("AT91: PM - wake mask %08x, pm state %d\n",
			/* remember all the always-wake irqs */
			(at91_pmc_read(AT91_PMC_PCSR)
					| (1 << AT91_ID_FIQ)
					| (1 << AT91_ID_SYS)
					| (at91_extern_irq))
				& at91_aic_read(AT91_AIC_IMR),
			state);

	switch (state) {
		/*
		 * Suspend-to-RAM is like STANDBY plus slow clock mode, so
		 * drivers must suspend more deeply:  only the master clock
		 * controller may be using the main oscillator.
		 */
		case PM_SUSPEND_MEM:
			/*
			 * Ensure that clocks are in a valid state.
			 */
			if (!at91_pm_verify_clocks())
				goto error;

			/*
			 * Enter slow clock mode by switching over to clk32k and
			 * turning off the main oscillator; reverse on wakeup.
			 */
			if (slow_clock) {
				int memctrl = AT91_MEMCTRL_SDRAMC;

				if (cpu_is_at91rm9200())
					memctrl = AT91_MEMCTRL_MC;
				else if (cpu_is_at91sam9g45())
					memctrl = AT91_MEMCTRL_DDRSDR;
#ifdef CONFIG_AT91_SLOW_CLOCK
				/* copy slow_clock handler to SRAM, and call it */
				memcpy(slow_clock, at91_slow_clock, at91_slow_clock_sz);
#endif
				slow_clock(at91_pmc_base, at91_ramc_base[0],
					   at91_ramc_base[1], memctrl);
				break;
			} else {
				pr_info("AT91: PM - no slow clock mode enabled ...\n");
				/* FALLTHROUGH leaving master clock alone */
			}

		/*
		 * STANDBY mode has *all* drivers suspended; ignores irqs not
		 * marked as 'wakeup' event sources; and reduces DRAM power.
		 * But otherwise it's identical to PM_SUSPEND_ON:  cpu idle, and
		 * nothing fancy done with main or cpu clocks.
		 */
		case PM_SUSPEND_STANDBY:
			/*
			 * NOTE: the Wait-for-Interrupt instruction needs to be
			 * in icache so no SDRAM accesses are needed until the
			 * wakeup IRQ occurs and self-refresh is terminated.
			 * For ARM 926 based chips, this requirement is weaker
			 * as at91sam9 can access a RAM in self-refresh mode.
			 */
			if (cpu_is_at91rm9200())
				at91rm9200_standby();
			else if (cpu_is_at91sam9g45())
				at91sam9g45_standby();
			else
				at91sam9_standby();
			break;

		case PM_SUSPEND_ON:
			cpu_do_idle();
			break;

		default:
			pr_debug("AT91: PM - bogus suspend state %d\n", state);
			goto error;
	}

	pr_debug("AT91: PM - wakeup %08x\n",
			at91_aic_read(AT91_AIC_IPR) & at91_aic_read(AT91_AIC_IMR));

error:
	target_state = PM_SUSPEND_ON;
	at91_irq_resume();
	if (of_have_populated_dt())
		at91_pinctrl_gpio_resume();
	else
		at91_gpio_resume();
	return 0;
}

/*
 * Called right prior to thawing processes.
 */
static void at91_pm_end(void)
{
	target_state = PM_SUSPEND_ON;
}


static const struct platform_suspend_ops at91_pm_ops = {
	.valid	= at91_pm_valid_state,
	.begin	= at91_pm_begin,
	.enter	= at91_pm_enter,
	.end	= at91_pm_end,
};

static int __init at91_pm_init(void)
{
#ifdef CONFIG_AT91_SLOW_CLOCK
	slow_clock = (void *) (AT91_IO_VIRT_BASE - at91_slow_clock_sz);
#endif

	pr_info("AT91: Power Management%s\n", (slow_clock ? " (with slow clock mode)" : ""));

	/* AT91RM9200 SDRAM low-power mode cannot be used with self-refresh. */
	if (cpu_is_at91rm9200())
		at91_ramc_write(0, AT91RM9200_SDRAMC_LPR, 0);

	suspend_set_ops(&at91_pm_ops);

	show_reset_status();
	return 0;
}
arch_initcall(at91_pm_init);
