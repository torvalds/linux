/*
 * OpenRISC idle.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * Idle daemon for or32.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/tick.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/pgalloc.h>

void (*powersave) (void) = NULL;

static inline void pm_idle(void)
{
	barrier();
}

void cpu_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_idle_enter();
		rcu_idle_enter();

		while (!need_resched()) {
			check_pgt_cache();
			rmb();

			clear_thread_flag(TIF_POLLING_NRFLAG);

			local_irq_disable();
			/* Don't trace irqs off for idle */
			stop_critical_timings();
			if (!need_resched() && powersave != NULL)
				powersave();
			start_critical_timings();
			local_irq_enable();
			set_thread_flag(TIF_POLLING_NRFLAG);
		}

		rcu_idle_exit();
		tick_nohz_idle_exit();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}
