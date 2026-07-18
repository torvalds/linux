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
#include <linux/sched/stat.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/export.h>
#include <trace/events/power.h>
#include <asm/cpu_mf.h>
#include <asm/cputime.h>
#include <asm/idle.h>
#include <asm/nmi.h>
#include <asm/smp.h>

DEFINE_PER_CPU(struct s390_idle_data, s390_idle);

static __always_inline void __account_idle_time_irq(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long idle_time;

	idle_time = idle->clock_idle_exit.tod - idle->clock_idle_enter.tod;
	account_idle_time(cputime_to_nsecs(idle_time));
}

static __always_inline void __account_idle_time_setup(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);

	store_tod_clock_ext(&idle->clock_idle_enter);
	idle->timer_idle_enter = get_cpu_timer();
	idle->clock_idle_exit = idle->clock_idle_enter;
}

#ifdef CONFIG_NO_HZ_COMMON

static u64 arch_cpu_in_idle_time(int cpu)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, cpu);
	union tod_clock now;
	u64 idle_time;

	if (!idle->in_idle)
		return 0;
	store_tod_clock_ext(&now);
	if (tod_after(idle->clock_idle_exit.tod, idle->clock_idle_enter.tod))
		idle_time = idle->clock_idle_exit.tod - idle->clock_idle_enter.tod;
	else
		idle_time = now.tod - idle->clock_idle_enter.tod;
	return cputime_to_nsecs(idle_time);
}

static u64 arch_cpu_idle_time(int cpu, enum cpu_usage_stat idx, bool compute_delta)
{
	struct kernel_cpustat *kc = &kcpustat_cpu(cpu);
	u64 *cpustat = kc->cpustat;
	unsigned int seq;
	u64 idle_time;

	/*
	 * The open coded seqcount writer in entry.S relies on the
	 * raw counting mechanism without any writer protection.
	 */
	typecheck(typeof(kc->idle_sleeptime_seq), seqcount_t);
	do {
		seq = read_seqcount_begin(&kc->idle_sleeptime_seq);
		idle_time = cpustat[idx];
		if (compute_delta)
			idle_time += arch_cpu_in_idle_time(cpu);
	} while (read_seqcount_retry(&kc->idle_sleeptime_seq, seq));
	return idle_time;
}

u64 arch_kcpustat_field_idle(int cpu)
{
	return arch_cpu_idle_time(cpu, CPUTIME_IDLE, !nr_iowait_cpu(cpu));
}
EXPORT_SYMBOL_GPL(arch_kcpustat_field_idle);

u64 arch_kcpustat_field_iowait(int cpu)
{
	return arch_cpu_idle_time(cpu, CPUTIME_IOWAIT, nr_iowait_cpu(cpu));
}
EXPORT_SYMBOL_GPL(arch_kcpustat_field_iowait);

void account_idle_time_irq(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	struct kernel_cpustat *kc = kcpustat_this_cpu;

	write_seqcount_begin(&kc->idle_sleeptime_seq);
	idle->in_idle = false;
	__account_idle_time_irq();
	write_seqcount_end(&kc->idle_sleeptime_seq);
}

static __always_inline void account_idle_time_setup(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	struct kernel_cpustat *kc = kcpustat_this_cpu;

	raw_write_seqcount_begin(&kc->idle_sleeptime_seq);
	idle->in_idle = true;
	__account_idle_time_setup();
	raw_write_seqcount_end(&kc->idle_sleeptime_seq);
}

#else  /* CONFIG_NO_HZ_COMMON */

void account_idle_time_irq(void)
{
	__account_idle_time_irq();
}

static __always_inline void account_idle_time_setup(void)
{
	__account_idle_time_setup();
}

#endif /* CONFIG_NO_HZ_COMMON */

void noinstr arch_cpu_idle(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long psw_mask;

	/* Wait for external, I/O or machine check interrupt. */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_WAIT |
		   PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	clear_cpu_flag(CIF_NOHZ_DELAY);
	set_cpu_flag(CIF_ENABLED_WAIT);
	if (smp_cpu_mtid)
		stcctm(MT_DIAG, smp_cpu_mtid, (u64 *)&idle->mt_cycles_enter);
	account_idle_time_setup();
	bpon();
	__load_psw_mask(psw_mask);
}

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
