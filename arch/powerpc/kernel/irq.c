// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * The MPC8xx has an interrupt mask in the SIU.  If a bit is set, the
 * interrupt is _enabled_.  As expected, IRQ0 is bit 0 in the 32-bit
 * mask register (of which only 16 are defined), hence the weird shifting
 * and complement of the cached_irq_mask.  I want to be able to stuff
 * this right into the SIU SMASK register.
 * Many of the prep/chrp functions are conditional compiled on CONFIG_PPC_8xx
 * to reduce code space and undefined function references.
 */

#undef DEBUG

#include <linux/export.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/profile.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/vmalloc.h>
#include <linux/pgtable.h>
#include <linux/static_call.h>

#include <linux/uaccess.h>
#include <asm/interrupt.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/hw_irq.h>
#include <asm/softirq_stack.h>
#include <asm/ppc_asm.h>

#define CREATE_TRACE_POINTS
#include <asm/trace.h>
#include <asm/cpu_has_feature.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);

#ifdef CONFIG_PPC32
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
u32 tau_interrupts(unsigned long cpu);
#endif
#endif /* CONFIG_PPC32 */

int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

#if defined(CONFIG_PPC32) && defined(CONFIG_TAU_INT)
	if (tau_initialized) {
		seq_printf(p, "%*s: ", prec, "TAU");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", tau_interrupts(j));
		seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
	}
#endif /* CONFIG_PPC32 && CONFIG_TAU_INT */

	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).timer_irqs_event);
        seq_printf(p, "  Local timer interrupts for timer event device\n");

	seq_printf(p, "%*s: ", prec, "BCT");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).broadcast_irqs_event);
	seq_printf(p, "  Broadcast timer interrupts for timer event device\n");

	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).timer_irqs_others);
        seq_printf(p, "  Local timer interrupts for others\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).spurious_irqs);
	seq_printf(p, "  Spurious interrupts\n");

	seq_printf(p, "%*s: ", prec, "PMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).pmu_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");

	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).mce_exceptions);
	seq_printf(p, "  Machine check exceptions\n");

#ifdef CONFIG_PPC_BOOK3S_64
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		seq_printf(p, "%*s: ", prec, "HMI");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", paca_ptrs[j]->hmi_irqs);
		seq_printf(p, "  Hypervisor Maintenance Interrupts\n");
	}
#endif

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).sreset_irqs);
	seq_printf(p, "  System Reset interrupts\n");

#ifdef CONFIG_PPC_WATCHDOG
	seq_printf(p, "%*s: ", prec, "WDG");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).soft_nmi_irqs);
	seq_printf(p, "  Watchdog soft-NMI interrupts\n");
#endif

#ifdef CONFIG_PPC_DOORBELL
	if (cpu_has_feature(CPU_FTR_DBELL)) {
		seq_printf(p, "%*s: ", prec, "DBL");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", per_cpu(irq_stat, j).doorbell_irqs);
		seq_printf(p, "  Doorbell interrupts\n");
	}
#endif

	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = per_cpu(irq_stat, cpu).timer_irqs_event;

	sum += per_cpu(irq_stat, cpu).broadcast_irqs_event;
	sum += per_cpu(irq_stat, cpu).pmu_irqs;
	sum += per_cpu(irq_stat, cpu).mce_exceptions;
	sum += per_cpu(irq_stat, cpu).spurious_irqs;
	sum += per_cpu(irq_stat, cpu).timer_irqs_others;
#ifdef CONFIG_PPC_BOOK3S_64
	sum += paca_ptrs[cpu]->hmi_irqs;
#endif
	sum += per_cpu(irq_stat, cpu).sreset_irqs;
#ifdef CONFIG_PPC_WATCHDOG
	sum += per_cpu(irq_stat, cpu).soft_nmi_irqs;
#endif
#ifdef CONFIG_PPC_DOORBELL
	sum += per_cpu(irq_stat, cpu).doorbell_irqs;
#endif

	return sum;
}

static inline void check_stack_overflow(unsigned long sp)
{
	if (!IS_ENABLED(CONFIG_DEBUG_STACKOVERFLOW))
		return;

	sp &= THREAD_SIZE - 1;

	/* check for stack overflow: is there less than 1/4th free? */
	if (unlikely(sp < THREAD_SIZE / 4)) {
		pr_err("do_IRQ: stack overflow: %ld\n", sp);
		dump_stack();
	}
}

#ifdef CONFIG_SOFTIRQ_ON_OWN_STACK
static __always_inline void call_do_softirq(const void *sp)
{
	/* Temporarily switch r1 to sp, call __do_softirq() then restore r1. */
	asm volatile (
		 PPC_STLU "	%%r1, %[offset](%[sp])	;"
		"mr		%%r1, %[sp]		;"
#ifdef CONFIG_PPC_KERNEL_PCREL
		"bl		%[callee]@notoc		;"
#else
		"bl		%[callee]		;"
#endif
		 PPC_LL "	%%r1, 0(%%r1)		;"
		 : // Outputs
		 : // Inputs
		   [sp] "b" (sp), [offset] "i" (THREAD_SIZE - STACK_FRAME_MIN_SIZE),
		   [callee] "i" (__do_softirq)
		 : // Clobbers
		   "lr", "xer", "ctr", "memory", "cr0", "cr1", "cr5", "cr6",
		   "cr7", "r0", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
		   "r11", "r12"
	);
}
#endif

DEFINE_STATIC_CALL_RET0(ppc_get_irq, *ppc_md.get_irq);

static void __do_irq(struct pt_regs *regs, unsigned long oldsp)
{
	unsigned int irq;

	trace_irq_entry(regs);

	check_stack_overflow(oldsp);

	/*
	 * Query the platform PIC for the interrupt & ack it.
	 *
	 * This will typically lower the interrupt line to the CPU
	 */
	irq = static_call(ppc_get_irq)();

	/* We can hard enable interrupts now to allow perf interrupts */
	if (should_hard_irq_enable(regs))
		do_hard_irq_enable();

	/* And finally process it */
	if (unlikely(!irq))
		__this_cpu_inc(irq_stat.spurious_irqs);
	else
		generic_handle_irq(irq);

	trace_irq_exit(regs);
}

static __always_inline void call_do_irq(struct pt_regs *regs, void *sp)
{
	register unsigned long r3 asm("r3") = (unsigned long)regs;

	/* Temporarily switch r1 to sp, call __do_irq() then restore r1. */
	asm volatile (
		 PPC_STLU "	%%r1, %[offset](%[sp])	;"
		"mr		%%r4, %%r1		;"
		"mr		%%r1, %[sp]		;"
#ifdef CONFIG_PPC_KERNEL_PCREL
		"bl		%[callee]@notoc		;"
#else
		"bl		%[callee]		;"
#endif
		 PPC_LL "	%%r1, 0(%%r1)		;"
		 : // Outputs
		   "+r" (r3)
		 : // Inputs
		   [sp] "b" (sp), [offset] "i" (THREAD_SIZE - STACK_FRAME_MIN_SIZE),
		   [callee] "i" (__do_irq)
		 : // Clobbers
		   "lr", "xer", "ctr", "memory", "cr0", "cr1", "cr5", "cr6",
		   "cr7", "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
		   "r11", "r12"
	);
}

void __do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	void *cursp, *irqsp, *sirqsp;

	/* Switch to the irq stack to handle this */
	cursp = (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
	irqsp = hardirq_ctx[raw_smp_processor_id()];
	sirqsp = softirq_ctx[raw_smp_processor_id()];

	/* Already there ? If not switch stack and call */
	if (unlikely(cursp == irqsp || cursp == sirqsp))
		__do_irq(regs, current_stack_pointer);
	else
		call_do_irq(regs, irqsp);

	set_irq_regs(old_regs);
}

DEFINE_INTERRUPT_HANDLER_ASYNC(do_IRQ)
{
	__do_IRQ(regs);
}

static void *__init alloc_vm_stack(void)
{
	return __vmalloc_node(THREAD_SIZE, THREAD_ALIGN, THREADINFO_GFP,
			      NUMA_NO_NODE, (void *)_RET_IP_);
}

static void __init vmap_irqstack_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		softirq_ctx[i] = alloc_vm_stack();
		hardirq_ctx[i] = alloc_vm_stack();
	}
}


void __init init_IRQ(void)
{
	if (IS_ENABLED(CONFIG_VMAP_STACK))
		vmap_irqstack_init();

	if (ppc_md.init_IRQ)
		ppc_md.init_IRQ();

	if (!WARN_ON(!ppc_md.get_irq))
		static_call_update(ppc_get_irq, ppc_md.get_irq);
}

#ifdef CONFIG_BOOKE_OR_40x
void   *critirq_ctx[NR_CPUS] __read_mostly;
void    *dbgirq_ctx[NR_CPUS] __read_mostly;
void *mcheckirq_ctx[NR_CPUS] __read_mostly;
#endif

void *softirq_ctx[NR_CPUS] __read_mostly;
void *hardirq_ctx[NR_CPUS] __read_mostly;

#ifdef CONFIG_SOFTIRQ_ON_OWN_STACK
void do_softirq_own_stack(void)
{
	call_do_softirq(softirq_ctx[smp_processor_id()]);
}
#endif

irq_hw_number_t virq_to_hw(unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	return WARN_ON(!irq_data) ? 0 : irq_data->hwirq;
}
EXPORT_SYMBOL_GPL(virq_to_hw);

#ifdef CONFIG_SMP
int irq_choose_cpu(const struct cpumask *mask)
{
	int cpuid;

	if (cpumask_equal(mask, cpu_online_mask)) {
		static int irq_rover;
		static DEFINE_RAW_SPINLOCK(irq_rover_lock);
		unsigned long flags;

		/* Round-robin distribution... */
do_round_robin:
		raw_spin_lock_irqsave(&irq_rover_lock, flags);

		irq_rover = cpumask_next(irq_rover, cpu_online_mask);
		if (irq_rover >= nr_cpu_ids)
			irq_rover = cpumask_first(cpu_online_mask);

		cpuid = irq_rover;

		raw_spin_unlock_irqrestore(&irq_rover_lock, flags);
	} else {
		cpuid = cpumask_first_and(mask, cpu_online_mask);
		if (cpuid >= nr_cpu_ids)
			goto do_round_robin;
	}

	return get_hard_smp_processor_id(cpuid);
}
#else
int irq_choose_cpu(const struct cpumask *mask)
{
	return hard_smp_processor_id();
}
#endif
