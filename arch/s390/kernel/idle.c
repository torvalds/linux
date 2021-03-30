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
#include <linux/sched/cputime.h>
#include <trace/events/power.h>
#include <asm/cpu_mf.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include "entry.h"

static DEFINE_PER_CPU(struct s390_idle_data, s390_idle);

void account_idle_time_irq(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	u64 cycles_new[8];
	int i;

	clear_cpu_flag(CIF_ENABLED_WAIT);
	if (smp_cpu_mtid) {
		stcctm(MT_DIAG, smp_cpu_mtid, cycles_new);
		for (i = 0; i < smp_cpu_mtid; i++)
			this_cpu_add(mt_cycles[i], cycles_new[i] - idle->mt_cycles_enter[i]);
	}

	idle->clock_idle_exit = S390_lowcore.int_clock;
	idle->timer_idle_exit = S390_lowcore.sys_enter_timer;

	S390_lowcore.steal_timer += idle->clock_idle_enter - S390_lowcore.last_update_clock;
	S390_lowcore.last_update_clock = idle->clock_idle_exit;

	S390_lowcore.system_timer += S390_lowcore.last_update_timer - idle->timer_idle_enter;
	S390_lowcore.last_update_timer = idle->timer_idle_exit;
}

void arch_cpu_idle(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long idle_time;
	unsigned long psw_mask;

	/* Wait for external, I/O or machine check interrupt. */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_WAIT | PSW_MASK_DAT |
		PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	clear_cpu_flag(CIF_NOHZ_DELAY);

	/* psw_idle() returns with interrupts disabled. */
	psw_idle(idle, psw_mask);

	/* Account time spent with enabled wait psw loaded as idle time. */
	raw_write_seqcount_begin(&idle->seqcount);
	idle_time = idle->clock_idle_exit - idle->clock_idle_enter;
	idle->clock_idle_enter = idle->clock_idle_exit = 0ULL;
	idle->idle_time += idle_time;
	idle->idle_count++;
	account_idle_time(cputime_to_nsecs(idle_time));
	raw_write_seqcount_end(&idle->seqcount);
	raw_local_irq_enable();
}

static ssize_t show_idle_count(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned long idle_count;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&idle->seqcount);
		idle_count = READ_ONCE(idle->idle_count);
		if (READ_ONCE(idle->clock_idle_enter))
			idle_count++;
	} while (read_seqcount_retry(&idle->seqcount, seq));
	return sprintf(buf, "%lu\n", idle_count);
}
DEVICE_ATTR(idle_count, 0444, show_idle_count, NULL);

static ssize_t show_idle_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long now, idle_time, idle_enter, idle_exit, in_idle;
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&idle->seqcount);
		idle_time = READ_ONCE(idle->idle_time);
		idle_enter = READ_ONCE(idle->clock_idle_enter);
		idle_exit = READ_ONCE(idle->clock_idle_exit);
	} while (read_seqcount_retry(&idle->seqcount, seq));
	in_idle = 0;
	now = get_tod_clock();
	if (idle_enter) {
		if (idle_exit) {
			in_idle = idle_exit - idle_enter;
		} else if (now > idle_enter) {
			in_idle = now - idle_enter;
		}
	}
	idle_time += in_idle;
	return sprintf(buf, "%lu\n", idle_time >> 12);
}
DEVICE_ATTR(idle_time_us, 0444, show_idle_time, NULL);

u64 arch_cpu_idle_time(int cpu)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, cpu);
	unsigned long now, idle_enter, idle_exit, in_idle;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&idle->seqcount);
		idle_enter = READ_ONCE(idle->clock_idle_enter);
		idle_exit = READ_ONCE(idle->clock_idle_exit);
	} while (read_seqcount_retry(&idle->seqcount, seq));
	in_idle = 0;
	now = get_tod_clock();
	if (idle_enter) {
		if (idle_exit) {
			in_idle = idle_exit - idle_enter;
		} else if (now > idle_enter) {
			in_idle = now - idle_enter;
		}
	}
	return cputime_to_nsecs(in_idle);
}

void arch_cpu_idle_enter(void)
{
}

void arch_cpu_idle_exit(void)
{
}

void arch_cpu_idle_dead(void)
{
	cpu_die();
}
