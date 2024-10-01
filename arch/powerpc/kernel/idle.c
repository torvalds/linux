// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/sysctl.h>
#include <linux/tick.h>

#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/runlatch.h>
#include <asm/smp.h>


unsigned long cpuidle_disable = IDLE_NO_OVERRIDE;
EXPORT_SYMBOL(cpuidle_disable);

static int __init powersave_off(char *arg)
{
	ppc_md.power_save = NULL;
	cpuidle_disable = IDLE_POWERSAVE_OFF;
	return 1;
}
__setup("powersave=off", powersave_off);

void arch_cpu_idle(void)
{
	ppc64_runlatch_off();

	if (ppc_md.power_save) {
		ppc_md.power_save();
		/*
		 * Some power_save functions return with
		 * interrupts enabled, some don't.
		 */
		if (!irqs_disabled())
			raw_local_irq_disable();
	} else {
		/*
		 * Go into low thread priority and possibly
		 * low power mode.
		 */
		HMT_low();
		HMT_very_low();
	}

	HMT_medium();
	ppc64_runlatch_on();
}

int powersave_nap;

#ifdef CONFIG_PPC_970_NAP
void power4_idle(void)
{
	if (!cpu_has_feature(CPU_FTR_CAN_NAP))
		return;

	if (!powersave_nap)
		return;

	if (!prep_irq_for_idle())
		return;

	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile(PPC_DSSALL " ; sync" ::: "memory");

	power4_idle_nap();

	/*
	 * power4_idle_nap returns with interrupts enabled (soft and hard).
	 * to our caller with interrupts enabled (soft and hard). Our caller
	 * can cope with either interrupts disabled or enabled upon return.
	 */
}
#endif

#ifdef CONFIG_SYSCTL
/*
 * Register the sysctl to set/clear powersave_nap.
 */
static struct ctl_table powersave_nap_ctl_table[] = {
	{
		.procname	= "powersave-nap",
		.data		= &powersave_nap,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
};

static int __init
register_powersave_nap_sysctl(void)
{
	register_sysctl("kernel", powersave_nap_ctl_table);

	return 0;
}
__initcall(register_powersave_nap_sysctl);
#endif
