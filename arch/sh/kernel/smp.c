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

#include <linux/err.h>
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

static void flush_tlb_all_ipi(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	on_each_cpu(flush_tlb_all_ipi, 0, 1, 1);
}

static void flush_tlb_mm_ipi(void *mm)
{
	local_flush_tlb_mm((struct mm_struct *)mm);
}

/*
 * The following tlb flush calls are invoked when old translations are
 * being torn down, or pte attributes are changing. For single threaded
 * address spaces, a new context is obtained on the current cpu, and tlb
 * context on other cpus are invalidated to force a new context allocation
 * at switch_mm time, should the mm ever be used on other cpus. For
 * multithreaded address spaces, intercpu interrupts have to be sent.
 * Another case where intercpu interrupts are required is when the target
 * mm might be active on another cpu (eg debuggers doing the flushes on
 * behalf of debugees, kswapd stealing pages from another process etc).
 * Kanoj 07/00.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	preempt_disable();

	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		smp_call_function(flush_tlb_mm_ipi, (void *)mm, 1, 1);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, mm) = 0;
	}
	local_flush_tlb_mm(mm);

	preempt_enable();
}

struct flush_tlb_data {
	struct vm_area_struct *vma;
	unsigned long addr1;
	unsigned long addr2;
};

static void flush_tlb_range_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_range(fd->vma, fd->addr1, fd->addr2);
}

void flush_tlb_range(struct vm_area_struct *vma,
		     unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();
	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		struct flush_tlb_data fd;

		fd.vma = vma;
		fd.addr1 = start;
		fd.addr2 = end;
		smp_call_function(flush_tlb_range_ipi, (void *)&fd, 1, 1);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, mm) = 0;
	}
	local_flush_tlb_range(vma, start, end);
	preempt_enable();
}

static void flush_tlb_kernel_range_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_kernel_range(fd->addr1, fd->addr2);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct flush_tlb_data fd;

	fd.addr1 = start;
	fd.addr2 = end;
	on_each_cpu(flush_tlb_kernel_range_ipi, (void *)&fd, 1, 1);
}

static void flush_tlb_page_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_page(fd->vma, fd->addr1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	preempt_disable();
	if ((atomic_read(&vma->vm_mm->mm_users) != 1) ||
	    (current->mm != vma->vm_mm)) {
		struct flush_tlb_data fd;

		fd.vma = vma;
		fd.addr1 = page;
		smp_call_function(flush_tlb_page_ipi, (void *)&fd, 1, 1);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, vma->vm_mm) = 0;
	}
	local_flush_tlb_page(vma, page);
	preempt_enable();
}

static void flush_tlb_one_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;
	local_flush_tlb_one(fd->addr1, fd->addr2);
}

void flush_tlb_one(unsigned long asid, unsigned long vaddr)
{
	struct flush_tlb_data fd;

	fd.addr1 = asid;
	fd.addr2 = vaddr;

	smp_call_function(flush_tlb_one_ipi, (void *)&fd, 1, 1);
	local_flush_tlb_one(asid, vaddr);
}
