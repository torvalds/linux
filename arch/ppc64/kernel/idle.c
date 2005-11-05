/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Originally Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * iSeries supported added by Mike Corrigan <mikejc@us.ibm.com>
 *
 * Additional shared processor, SMT, and firmware support
 *    Copyright (c) 2003 Dave Engebretsen <engebret@us.ibm.com>
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
#include <asm/systemcfg.h>
#include <asm/machdep.h>

extern void power4_idle(void);

void default_idle(void)
{
	long oldval;
	unsigned int cpu = smp_processor_id();

	while (1) {
		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);

			while (!need_resched() && !cpu_is_offline(cpu)) {
				ppc64_runlatch_off();

				/*
				 * Go into low thread priority and possibly
				 * low power mode.
				 */
				HMT_low();
				HMT_very_low();
			}

			HMT_medium();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		ppc64_runlatch_on();
		schedule();
		if (cpu_is_offline(cpu) && system_state == SYSTEM_RUNNING)
			cpu_die();
	}
}

void native_idle(void)
{
	while (1) {
		ppc64_runlatch_off();

		if (!need_resched())
			power4_idle();

		if (need_resched()) {
			ppc64_runlatch_on();
			schedule();
		}

		if (cpu_is_offline(smp_processor_id()) &&
		    system_state == SYSTEM_RUNNING)
			cpu_die();
	}
}

void cpu_idle(void)
{
	BUG_ON(NULL == ppc_md.idle_loop);
	ppc_md.idle_loop();
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
