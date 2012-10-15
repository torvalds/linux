/*
 *  linux/arch/arm/mm/context.c
 *
 *  Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/percpu.h>

#include <asm/mmu_context.h>
#include <asm/thread_notify.h>
#include <asm/tlbflush.h>

static DEFINE_RAW_SPINLOCK(cpu_asid_lock);
unsigned int cpu_last_asid = ASID_FIRST_VERSION;

#ifdef CONFIG_ARM_LPAE
void cpu_set_reserved_ttbr0(void)
{
	unsigned long ttbl = __pa(swapper_pg_dir);
	unsigned long ttbh = 0;

	/*
	 * Set TTBR0 to swapper_pg_dir which contains only global entries. The
	 * ASID is set to 0.
	 */
	asm volatile(
	"	mcrr	p15, 0, %0, %1, c2		@ set TTBR0\n"
	:
	: "r" (ttbl), "r" (ttbh));
	isb();
}
#else
void cpu_set_reserved_ttbr0(void)
{
	u32 ttb;
	/* Copy TTBR1 into TTBR0 */
	asm volatile(
	"	mrc	p15, 0, %0, c2, c0, 1		@ read TTBR1\n"
	"	mcr	p15, 0, %0, c2, c0, 0		@ set TTBR0\n"
	: "=r" (ttb));
	isb();
}
#endif

#ifdef CONFIG_PID_IN_CONTEXTIDR
static int contextidr_notifier(struct notifier_block *unused, unsigned long cmd,
			       void *t)
{
	u32 contextidr;
	pid_t pid;
	struct thread_info *thread = t;

	if (cmd != THREAD_NOTIFY_SWITCH)
		return NOTIFY_DONE;

	pid = task_pid_nr(thread->task) << ASID_BITS;
	asm volatile(
	"	mrc	p15, 0, %0, c13, c0, 1\n"
	"	and	%0, %0, %2\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c13, c0, 1\n"
	: "=r" (contextidr), "+r" (pid)
	: "I" (~ASID_MASK));
	isb();

	return NOTIFY_OK;
}

static struct notifier_block contextidr_notifier_block = {
	.notifier_call = contextidr_notifier,
};

static int __init contextidr_notifier_init(void)
{
	return thread_register_notifier(&contextidr_notifier_block);
}
arch_initcall(contextidr_notifier_init);
#endif

/*
 * We fork()ed a process, and we need a new context for the child
 * to run in.
 */
void __init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.id = 0;
	raw_spin_lock_init(&mm->context.id_lock);
}

static void flush_context(void)
{
	cpu_set_reserved_ttbr0();
	local_flush_tlb_all();
	if (icache_is_vivt_asid_tagged()) {
		__flush_icache_all();
		dsb();
	}
}

#ifdef CONFIG_SMP

static void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	unsigned long flags;

	/*
	 * Locking needed for multi-threaded applications where the
	 * same mm->context.id could be set from different CPUs during
	 * the broadcast. This function is also called via IPI so the
	 * mm->context.id_lock has to be IRQ-safe.
	 */
	raw_spin_lock_irqsave(&mm->context.id_lock, flags);
	if (likely((mm->context.id ^ cpu_last_asid) >> ASID_BITS)) {
		/*
		 * Old version of ASID found. Set the new one and
		 * reset mm_cpumask(mm).
		 */
		mm->context.id = asid;
		cpumask_clear(mm_cpumask(mm));
	}
	raw_spin_unlock_irqrestore(&mm->context.id_lock, flags);

	/*
	 * Set the mm_cpumask(mm) bit for the current CPU.
	 */
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
}

/*
 * Reset the ASID on the current CPU. This function call is broadcast
 * from the CPU handling the ASID rollover and holding cpu_asid_lock.
 */
static void reset_context(void *info)
{
	unsigned int asid;
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm = current->active_mm;

	smp_rmb();
	asid = cpu_last_asid + cpu + 1;

	flush_context();
	set_mm_context(mm, asid);

	/* set the new ASID */
	cpu_switch_mm(mm->pgd, mm);
}

#else

static inline void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	mm->context.id = asid;
	cpumask_copy(mm_cpumask(mm), cpumask_of(smp_processor_id()));
}

#endif

void __new_context(struct mm_struct *mm)
{
	unsigned int asid;

	raw_spin_lock(&cpu_asid_lock);
#ifdef CONFIG_SMP
	/*
	 * Check the ASID again, in case the change was broadcast from
	 * another CPU before we acquired the lock.
	 */
	if (unlikely(((mm->context.id ^ cpu_last_asid) >> ASID_BITS) == 0)) {
		cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
		raw_spin_unlock(&cpu_asid_lock);
		return;
	}
#endif
	/*
	 * At this point, it is guaranteed that the current mm (with
	 * an old ASID) isn't active on any other CPU since the ASIDs
	 * are changed simultaneously via IPI.
	 */
	asid = ++cpu_last_asid;
	if (asid == 0)
		asid = cpu_last_asid = ASID_FIRST_VERSION;

	/*
	 * If we've used up all our ASIDs, we need
	 * to start a new version and flush the TLB.
	 */
	if (unlikely((asid & ~ASID_MASK) == 0)) {
		asid = cpu_last_asid + smp_processor_id() + 1;
		flush_context();
#ifdef CONFIG_SMP
		smp_wmb();
		smp_call_function(reset_context, NULL, 1);
#endif
		cpu_last_asid += NR_CPUS;
	}

	set_mm_context(mm, asid);
	raw_spin_unlock(&cpu_asid_lock);
}
