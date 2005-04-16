/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu).  Subsequently hacked
 * on by Tom Rini, Armin Kuster, Paul Mackerras and others.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/machdep.h>

void default_idle(void)
{
	void (*powersave)(void);

	powersave = ppc_md.power_save;

	if (!need_resched()) {
		if (powersave != NULL)
			powersave();
#ifdef CONFIG_SMP
		else {
			set_thread_flag(TIF_POLLING_NRFLAG);
			while (!need_resched())
				barrier();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		}
#endif
	}
	if (need_resched())
		schedule();
}

/*
 * The body of the idle task.
 */
void cpu_idle(void)
{
	for (;;)
		if (ppc_md.idle != NULL)
			ppc_md.idle();
		else
			default_idle();
}

#if defined(CONFIG_SYSCTL) && defined(CONFIG_6xx)
/*
 * Register the sysctl to set/clear powersave_nap.
 */
extern unsigned long powersave_nap;

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
