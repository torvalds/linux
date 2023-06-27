// SPDX-License-Identifier: GPL-2.0
/*
 * Idle functions for s390.
 *
 * Copyright IBM Corp. 2014
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <trace/events/power.h>
#include <asm/cpu_mf.h>
#include <asm/cputime.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include "entry.h"

static DEFINE_PER_CPU(struct s390_idle_data, s390_idle);

void account_idle_time_irq(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long idle_time;
	u64 cycles_new[8];
	int i;

	if (smp_cpu_mtid) {
		stcctm(MT_DIAG, smp_cpu_mtid, cycles_new);
		for (i = 0; i < smp_cpu_mtid; i++)
			this_cpu_add(mt_cycles[i], cycles_new[i] - idle->mt_cycles_enter[i]);
	}

	idle_time = S390_lowcore.int_clock - idle->clock_idle_enter;

	S390_lowcore.steal_timer += idle->clock_idle_enter - S390_lowcore.last_update_clock;
	S390_lowcore.last_update_clock = S390_lowcore.int_clock;

	S390_lowcore.system_timer += S390_lowcore.last_update_timer - idle->timer_idle_enter;
	S390_lowcore.last_update_timer = S390_lowcore.sys_enter_timer;

	/* Account time spent with enabled wait psw loaded as idle time. */
	WRITE_ONCE(idle->idle_time, READ_ONCE(idle->idle_time) + idle_time);
	WRITE_ONCE(idle->idle_count, READ_ONCE(idle->idle_count) + 1);
	account_idle_time(cputime_to_nsecs(idle_time));
}

void noinstr arch_cpu_idle(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long psw_mask;

	/* Wait for external, I/O or machine check interrupt. */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_WAIT |
		   PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	clear_cpu_flag(CIF_NOHZ_DELAY);

	/* psw_idle() returns with interrupts disabled. */
	psw_idle(idle, psw_mask);
}

static ssize_t show_idle_count(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);

	return sysfs_emit(buf, "%lu\n", READ_ONCE(idle->idle_count));
}
DEVICE_ATTR(idle_count, 0444, show_idle_count, NULL);

static ssize_t show_idle_time(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);

	return sysfs_emit(buf, "%lu\n", READ_ONCE(idle->idle_time) >> 12);
}
DEVICE_ATTR(idle_time_us, 0444, show_idle_time, NULL);

void arch_cpu_idle_enter(void)
{
}

void arch_cpu_idle_exit(void)
{
}

void __noreturn arch_cpu_idle_dead(void)
{
	cpu_die();
}
