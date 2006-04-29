/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Originally written by Cort Dougan (cort@cs.nmt.edu).
 * Subsequent 32-bit hacking by Tom Rini, Armin Kuster,
 * Paul Mackerras and others.
 *
 * iSeries supported added by Mike Corrigan <mikejc@us.ibm.com>
 *
 * Additional shared processor, SMT, and firmware support
 *    Copyright (c) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 * 32-bit and 64-bit versions merged by Paul Mackerras <paulus@samba.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/sysctl.h>

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/smp.h>

#ifdef CONFIG_HOTPLUG_CPU
#define cpu_should_die()	(cpu_is_offline(smp_processor_id()) && \
				 system_state == SYSTEM_RUNNING)
#else
#define cpu_should_die()	0
#endif

/*
 * The body of the idle task.
 */
void cpu_idle(void)
{
	if (ppc_md.idle_loop)
		ppc_md.idle_loop();	/* doesn't return */

	set_thread_flag(TIF_POLLING_NRFLAG);
	while (1) {
		while (!need_resched() && !cpu_should_die()) {
			ppc64_runlatch_off();

			if (ppc_md.power_save) {
				clear_thread_flag(TIF_POLLING_NRFLAG);
				/*
				 * smp_mb is so clearing of TIF_POLLING_NRFLAG
				 * is ordered w.r.t. need_resched() test.
				 */
				smp_mb();
				local_irq_disable();

				/* check again after disabling irqs */
				if (!need_resched() && !cpu_should_die())
					ppc_md.power_save();

				local_irq_enable();
				set_thread_flag(TIF_POLLING_NRFLAG);

			} else {
				/*
				 * Go into low thread priority and possibly
				 * low power mode.
				 */
				HMT_low();
				HMT_very_low();
			}
		}

		HMT_medium();
		ppc64_runlatch_on();
		if (cpu_should_die())
			cpu_die();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

int powersave_nap;

#ifdef CONFIG_SYSCTL
/*
 * Register the sysctl to set/clear powersave_nap.
 */
static ctl_table powersave_nap_ctl_table[]={
	{
		.ctl_name	= KERN_PPC_POWERSAVE_NAP,
		.procname	= "powersave-nap",
		.data		= &powersave_nap,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ 0, },
};
static ctl_table powersave_nap_sysctl_root[] = {
	{ 1, "kernel", NULL, 0, 0755, powersave_nap_ctl_table, },
 	{ 0,},
};

static int __init
register_powersave_nap_sysctl(void)
{
	register_sysctl_table(powersave_nap_sysctl_root, 0);

	return 0;
}
__initcall(register_powersave_nap_sysctl);
#endif
