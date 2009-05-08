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

#include <linux/suspend.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>

#include <mach/at91_pmc.h>
#include <mach/gpio.h>
#include <mach/cpu.h>

#include "generic.h"

#ifdef CONFIG_ARCH_AT91RM9200
#include <mach/at91rm9200_mc.h>

/*
 * The AT91RM9200 goes into self-refresh mode with this command, and will
 * terminate self-refresh automatically on the next SDRAM access.
 */
#define sdram_selfrefresh_enable()	at91_sys_write(AT91_SDRAMC_SRR, 1)
#define sdram_selfrefresh_disable()	do {} while (0)

#elif defined(CONFIG_ARCH_AT91CAP9)
#include <mach/at91cap9_ddrsdr.h>

static u32 saved_lpr;

static inline void sdram_selfrefresh_enable(void)
{
	u32 lpr;

	saved_lpr = at91_sys_read(AT91_DDRSDRC_LPR);

	lpr = saved_lpr & ~AT91_DDRSDRC_LPCB;
	at91_sys_write(AT91_DDRSDRC_LPR, lpr | AT91_DDRSDRC_LPCB_SELF_REFRESH);
}

#define sdram_selfrefresh_disable()	at91_sys_write(AT91_DDRSDRC_LPR, saved_lpr)

#else
#include <mach/at91sam9_sdramc.h>

#ifdef CONFIG_ARCH_AT91SAM9263
/*
 * FIXME either or both the SDRAM controllers (EB0, EB1) might be in use;
 * handle those cases both here and in the Suspend-To-RAM support.
 */
#define	AT91_SDRAMC	AT91_SDRAMC0
#warning Assuming EB1 SDRAM controller is *NOT* used
#endif

static u32 saved_lpr;

static inline void sdram_selfrefresh_enable(void)
{
	u32 lpr;

	saved_lpr = at91_sys_read(AT91_SDRAMC_LPR);

	lpr = saved_lpr & ~AT91_SDRAMC_LPCB;
	at91_sys_write(AT91_SDRAMC_LPR, lpr | AT91_SDRAMC_LPCB_SELF_REFRESH);
}

#define sdram_selfrefresh_disable()	at91_sys_write(AT91_SDRAMC_LPR, saved_lpr)

#endif


/*
 * Show the reason for the previous system reset.
 */
#if defined(AT91_SHDWC)

#include <mach/at91_rstc.h>
#include <mach/at91_shdwc.h>

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

	reset_type = at91_sys_read(AT91_RSTC_SR) & AT91_RSTC_RSTTYP;
	wake_type = at91_sys_read(AT91_SHDW_SR);

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
#else
static void __init show_reset_status(void) {}
#endif


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

	scsr = at91_sys_read(AT91_PMC_SCSR);

	/* USB must not be using PLLB */
	if (cpu_is_at91rm9200()) {
		if ((scsr & (AT91RM9200_PMC_UHP | AT91RM9200_PMC_UDP)) != 0) {
			pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
			return 0;
		}
	} else if (cpu_is_at91sam9260() || cpu_is_at91sam9261() || cpu_is_at91sam9263() || cpu_is_at91sam9g20()) {
		if ((scsr & (AT91SAM926x_PMC_UHP | AT91SAM926x_PMC_UDP)) != 0) {
			pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
			return 0;
		}
	} else if (cpu_is_at91cap9()) {
		if ((scsr & AT91CAP9_PMC_UHP) != 0) {
			pr_err("AT91: PM - Suspend-to-RAM with USB still active\n");
			return 0;
		}
	}

#ifdef CONFIG_AT91_PROGRAMMABLE_CLOCKS
	/* PCK0..PCK3 must be disabled, or configured to use clk32k */
	for (i = 0; i < 4; i++) {
		u32 css;

		if ((scsr & (AT91_PMC_PCK0 << i)) == 0)
			continue;

		css = at91_sys_read(AT91_PMC_PCKR(i)) & AT91_PMC_CSS;
		if (css != AT91_PMC_CSS_SLOW) {
			pr_err("AT91: PM - Suspend-to-RAM with PCK%d src %d\n", i, css);
			return 0;
		}
	}
#endif

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


static void (*slow_clock)(void);

#ifdef CONFIG_AT91_SLOW_CLOCK
extern void at91_slow_clock(void);
extern u32 at91_slow_clock_sz;
#endif


static int at91_pm_enter(suspend_state_t state)
{
	at91_gpio_suspend();
	at91_irq_suspend();

	pr_debug("AT91: PM - wake mask %08x, pm state %d\n",
			/* remember all the always-wake irqs */
			(at91_sys_read(AT91_PMC_PCSR)
					| (1 << AT91_ID_FIQ)
					| (1 << AT91_ID_SYS)
					| (at91_extern_irq))
				& at91_sys_read(AT91_AIC_IMR),
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
#ifdef CONFIG_AT91_SLOW_CLOCK
				/* copy slow_clock handler to SRAM, and call it */
				memcpy(slow_clock, at91_slow_clock, at91_slow_clock_sz);
#endif
				slow_clock();
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
			 */
			asm("b 1f; .align 5; 1:");
			asm("mcr p15, 0, r0, c7, c10, 4");	/* drain write buffer */
			sdram_selfrefresh_enable();
			asm("mcr p15, 0, r0, c7, c0, 4");	/* wait for interrupt */
			sdram_selfrefresh_disable();
			break;

		case PM_SUSPEND_ON:
			asm("mcr p15, 0, r0, c7, c0, 4");	/* wait for interrupt */
			break;

		default:
			pr_debug("AT91: PM - bogus suspend state %d\n", state);
			goto error;
	}

	pr_debug("AT91: PM - wakeup %08x\n",
			at91_sys_read(AT91_AIC_IPR) & at91_sys_read(AT91_AIC_IMR));

error:
	target_state = PM_SUSPEND_ON;
	at91_irq_resume();
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


static struct platform_suspend_ops at91_pm_ops ={
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

#ifdef CONFIG_ARCH_AT91RM9200
	/* AT91RM9200 SDRAM low-power mode cannot be used with self-refresh. */
	at91_sys_write(AT91_SDRAMC_LPR, 0);
#endif

	suspend_set_ops(&at91_pm_ops);

	show_reset_status();
	return 0;
}
arch_initcall(at91_pm_init);
