/* MN10300 Watchdog timer
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/i386/kernel/nmi.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/nmi.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/intctl-regs.h>
#include <asm/rtc-regs.h>
#include <asm/div64.h>
#include <asm/smp.h>
#include <asm/gdb-stub.h>
#include <asm/proc/clock.h>

static DEFINE_SPINLOCK(watchdog_print_lock);
static unsigned int watchdog;
static unsigned int watchdog_hz = 1;
unsigned int watchdog_alert_counter;

EXPORT_SYMBOL(touch_nmi_watchdog);

/*
 * the best way to detect whether a CPU has a 'hard lockup' problem
 * is to check its timer makes IRQ counts. If they are not
 * changing then that CPU has some problem.
 *
 * as these watchdog NMI IRQs are generated on every CPU, we only
 * have to check the current processor.
 *
 * since NMIs dont listen to _any_ locks, we have to be extremely
 * careful not to rely on unsafe variables. The printk might lock
 * up though, so we have to break up any console locks first ...
 * [when there will be more tty-related locks, break them up
 *  here too!]
 */
static unsigned int last_irq_sums[NR_CPUS];

int __init check_watchdog(void)
{
	irq_cpustat_t tmp[1];

	printk(KERN_INFO "Testing Watchdog... ");

	memcpy(tmp, irq_stat, sizeof(tmp));
	local_irq_enable();
	mdelay((10 * 1000) / watchdog_hz); /* wait 10 ticks */
	local_irq_disable();

	if (nmi_count(0) - tmp[0].__nmi_count <= 5) {
		printk(KERN_WARNING "CPU#%d: Watchdog appears to be stuck!\n",
		       0);
		return -1;
	}

	printk(KERN_INFO "OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	 * something more reasonable; makes a difference in some configs
	 */
	watchdog_hz = 1;

	return 0;
}

static int __init setup_watchdog(char *str)
{
	unsigned tmp;
	int opt;
	u8 ctr;

	get_option(&str, &opt);
	if (opt != 1)
		return 0;

	watchdog = opt;
	if (watchdog) {
		set_intr_stub(EXCEP_WDT, watchdog_handler);
		ctr = WDCTR_WDCK_65536th;
		WDCTR = WDCTR_WDRST | ctr;
		WDCTR = ctr;
		tmp = WDCTR;

		tmp = __muldiv64u(1 << (16 + ctr * 2), 1000000, MN10300_WDCLK);
		tmp = 1000000000 / tmp;
		watchdog_hz = (tmp + 500) / 1000;
	}

	return 1;
}

__setup("watchdog=", setup_watchdog);

void __init watchdog_go(void)
{
	u8 wdt;

	if (watchdog) {
		printk(KERN_INFO "Watchdog: running at %uHz\n", watchdog_hz);
		wdt = WDCTR & ~WDCTR_WDCNE;
		WDCTR = wdt | WDCTR_WDRST;
		wdt = WDCTR;
		WDCTR = wdt | WDCTR_WDCNE;
		wdt = WDCTR;

		check_watchdog();
	}
}

asmlinkage
void watchdog_interrupt(struct pt_regs *regs, enum exception_code excep)
{

	/*
	 * Since current-> is always on the stack, and we always switch
	 * the stack NMI-atomically, it's safe to use smp_processor_id().
	 */
	int sum, cpu = smp_processor_id();
	int irq = NMIIRQ;
	u8 wdt, tmp;

	wdt = WDCTR & ~WDCTR_WDCNE;
	WDCTR = wdt;
	tmp = WDCTR;
	NMICR = NMICR_WDIF;

	nmi_count(cpu)++;
	kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));
	sum = irq_stat[cpu].__irq_count;

	if (last_irq_sums[cpu] == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		watchdog_alert_counter++;
		if (watchdog_alert_counter == 5 * watchdog_hz) {
			spin_lock(&watchdog_print_lock);
			/*
			 * We are in trouble anyway, lets at least try
			 * to get a message out.
			 */
			bust_spinlocks(1);
			printk(KERN_ERR
			       "NMI Watchdog detected LOCKUP on CPU%d,"
			       " pc %08lx, registers:\n",
			       cpu, regs->pc);
			show_registers(regs);
			printk("console shuts up ...\n");
			console_silent();
			spin_unlock(&watchdog_print_lock);
			bust_spinlocks(0);
#ifdef CONFIG_GDBSTUB
			if (gdbstub_busy)
				gdbstub_exception(regs, excep);
			else
				gdbstub_intercept(regs, excep);
#endif
			do_exit(SIGSEGV);
		}
	} else {
		last_irq_sums[cpu] = sum;
		watchdog_alert_counter = 0;
	}

	WDCTR = wdt | WDCTR_WDRST;
	tmp = WDCTR;
	WDCTR = wdt | WDCTR_WDCNE;
	tmp = WDCTR;
}
