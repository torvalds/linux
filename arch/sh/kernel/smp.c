/*
 * arch/sh/kernel/smp.c
 *
 * SMP support for the SuperH processors.
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/cache.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/atomic.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>

/*
 * This was written with the Sega Saturn (SMP SH-2 7604) in mind,
 * but is designed to be usable regardless if there's an MMU
 * present or not.
 */
struct sh_cpuinfo cpu_data[NR_CPUS];

extern void per_cpu_trap_init(void);

cpumask_t cpu_possible_map;
EXPORT_SYMBOL(cpu_possible_map);

cpumask_t cpu_online_map;
EXPORT_SYMBOL(cpu_online_map);
static atomic_t cpus_booted = ATOMIC_INIT(0);

/* These are defined by the board-specific code. */

/*
 * Cause the function described by call_data to be executed on the passed
 * cpu.  When the function has finished, increment the finished field of
 * call_data.
 */
void __smp_send_ipi(unsigned int cpu, unsigned int action);

/*
 * Find the number of available processors
 */
unsigned int __smp_probe_cpus(void);

/*
 * Start a particular processor
 */
void __smp_slave_init(unsigned int cpu);

/*
 * Run specified function on a particular processor.
 */
void __smp_call_function(unsigned int cpu);

static inline void __init smp_store_cpu_info(unsigned int cpu)
{
	cpu_data[cpu].loops_per_jiffy = loops_per_jiffy;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu = smp_processor_id();
	int i;

	atomic_set(&cpus_booted, 1);
	smp_store_cpu_info(cpu);
	
	for (i = 0; i < __smp_probe_cpus(); i++)
		cpu_set(i, cpu_possible_map);
}

void __devinit smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();

	cpu_set(cpu, cpu_online_map);
	cpu_set(cpu, cpu_possible_map);
}

int __cpu_up(unsigned int cpu)
{
	struct task_struct *tsk;

	tsk = fork_idle(cpu);

	if (IS_ERR(tsk))
		panic("Failed forking idle task for cpu %d\n", cpu);
	
	task_thread_info(tsk)->cpu = cpu;

	cpu_set(cpu, cpu_online_map);

	return 0;
}

int start_secondary(void *unused)
{
	unsigned int cpu;

	cpu = smp_processor_id();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	smp_store_cpu_info(cpu);

	__smp_slave_init(cpu);
	preempt_disable();
	per_cpu_trap_init();
	
	atomic_inc(&cpus_booted);

	cpu_idle();
	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	smp_mb();
}

void smp_send_reschedule(int cpu)
{
	__smp_send_ipi(cpu, SMP_MSG_RESCHEDULE);
}

static void stop_this_cpu(void *unused)
{
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_disable();

	for (;;)
		cpu_relax();
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, 0, 1, 0);
}


struct smp_fn_call_struct smp_fn_call = {
	.lock		= SPIN_LOCK_UNLOCKED,
	.finished	= ATOMIC_INIT(0),
};

/*
 * The caller of this wants the passed function to run on every cpu.  If wait
 * is set, wait until all cpus have finished the function before returning.
 * The lock is here to protect the call structure.
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(void (*func)(void *info), void *info, int retry, int wait)
{
	unsigned int nr_cpus = atomic_read(&cpus_booted);
	int i;

	if (nr_cpus < 2)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	spin_lock(&smp_fn_call.lock);

	atomic_set(&smp_fn_call.finished, 0);
	smp_fn_call.fn = func;
	smp_fn_call.data = info;

	for (i = 0; i < nr_cpus; i++)
		if (i != smp_processor_id())
			__smp_call_function(i);

	if (wait)
		while (atomic_read(&smp_fn_call.finished) != (nr_cpus - 1));

	spin_unlock(&smp_fn_call.lock);

	return 0;
}

/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

