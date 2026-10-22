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
DEFINE_PER_CPU(unsigned int, __softirq_pending);

#ifdef CONFIG_PPC32
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
u32 tau_interrupts(unsigned long cpu);
#endif
#endif /* CONFIG_PPC32 */

struct irq_stat_info {
	const char	*symbol;
	const char	*text;
	int		optional;
};

/* ISE - IRQ STAT ENABLED, ISC - IRQ STAT CONDITIONAL */
#define ISE(idx, sym, txt)[IRQ_COUNT_##idx] = { .symbol = sym, .text = txt, .optional = 0}
#define ISC(idx, sym, txt)[IRQ_COUNT_##idx] = { .symbol = sym, .text = txt, .optional = 1}


static struct irq_stat_info irq_stat_info[IRQ_COUNT_MAX] __ro_after_init = {
	ISE(LOC_TIMER,		"LOC", "  Local timer interrupts for timer event device\n"),
	ISE(BCT_TIMER,		"BCT", "  Broadcast timer interrupts for timer event device\n"),
	ISE(OTHER_TIMER,	"LOC", "  Local timer interrupts for others\n"),
	ISE(SPURIOUS,		"SPU", "  Spurious interrupts\n"),
	ISE(PMI,		"PMI", "  Performance monitoring interrupts\n"),
	ISC(MCE,		"MCE", "  Machine check exceptions\n"),
	ISC(NMI_SRESET,		"NMI", "  System Reset interrupts\n"),
#ifdef CONFIG_PPC_WATCHDOG
	ISE(WATCHDOG,		"WDG", "  Watchdog soft-NMI interrupts\n"),
#endif
#ifdef CONFIG_PPC_DOORBELL
	ISE(DOORBELL,		"DBL", "  Doorbell interrupts\n"),
#endif
};

/*
 * Used for default disabled counters to increment the stats and to enable the
 * entry for /proc/interrupts output.
 */
static DECLARE_BITMAP(irq_stat_count_show, IRQ_COUNT_MAX) __read_mostly;
void inc_irq_stat_and_enable(enum irq_stat_counts which)
{
	__this_cpu_inc(irq_stat.counts[which]);
	set_bit(which, irq_stat_count_show);
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	const struct irq_stat_info *info = irq_stat_info;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++, info++) {
		if (info->optional && !test_bit(i, irq_stat_count_show))
			continue;

		seq_printf(p, "%*s:", prec, info->symbol);
		irq_proc_emit_counts(p, &irq_stat.counts[i]);
		seq_puts(p, info->text);
	}

#if defined(CONFIG_PPC32) && defined(CONFIG_TAU_INT)
	if (tau_initialized) {
		int j;
		seq_printf(p, "%*s:", prec, "TAU");
		for_each_online_cpu(j)
			seq_put_decimal_ull_width(p, " ", tau_interrupts(j), 10);
		seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
	}
#endif /* CONFIG_PPC32 && CONFIG_TAU_INT */
#ifdef CONFIG_PPC_BOOK3S_64
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		int j;
		seq_printf(p, "%*s:", prec, "HMI");
		for_each_online_cpu(j)
			seq_put_decimal_ull_width(p, " ", paca_ptrs[j]->hmi_irqs, 10);
		seq_printf(p, "  Hypervisor Maintenance Interrupts\n");
	}
#endif
	return 0;
}

static int __init irq_init_stats(void)
{
	struct irq_stat_info *info = irq_stat_info;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++, info++) {
		if (info->optional == 0)
			set_bit(i, irq_stat_count_show);
	}

	return 0;
}
late_initcall(irq_init_stats);

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	irq_cpustat_t *p = per_cpu_ptr(&irq_stat, cpu);
	u64 sum = 0;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++)
		sum += p->counts[i];

#ifdef CONFIG_PPC_BOOK3S_64
	sum += paca_ptrs[cpu]->hmi_irqs;
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
		   "lr", "xer", "ctr", "memory", "cr0", "cr1", "cr5", "cr6", "cr7", "r0",
		  /* r2 may be clobbered by the callee when using PCREL mode in the ELFv2 ABI. */
#ifdef CONFIG_PPC_KERNEL_PCREL
		   "r2",
#endif
		   "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
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
		inc_irq_stat(SPURIOUS);
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
		   "lr", "xer", "ctr", "memory", "cr0", "cr1", "cr5", "cr6", "cr7", "r0",
		  /* r2 may be clobbered by the callee when using PCREL mode in the ELFv2 ABI. */
#ifdef CONFIG_PPC_KERNEL_PCREL
		   "r2",
#endif
		   "r4", "r5", "r6", "r7", "r8", "r9", "r10",
		   "r11", "r12"
	);
}

void __do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	void *cursp, *irqsp;

	/* Switch to the irq stack to handle this */
	cursp = (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
	irqsp = hardirq_ctx[raw_smp_processor_id()];

	/* Already there ? If not switch stack and call */
	if (unlikely(cursp == irqsp))
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

#ifdef CONFIG_BOOKE
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

		irq_rover = cpumask_next_wrap(irq_rover, cpu_online_mask);
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
