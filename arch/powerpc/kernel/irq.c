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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
 * Many of the prep/chrp functions are conditional compiled on CONFIG_8xx
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
#include <linux/bootmem.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/debug.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#endif
#define CREATE_TRACE_POINTS
#include <asm/trace.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);

int __irq_offset_value;

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(__irq_offset_value);
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
extern int tau_interrupts(int);
#endif
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64

int distribute_irqs = 1;

static inline notrace unsigned long get_irq_happened(void)
{
	unsigned long happened;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (happened) : "i" (offsetof(struct paca_struct, irq_happened)));

	return happened;
}

static inline notrace void set_soft_enabled(unsigned long enable)
{
	__asm__ __volatile__("stb %0,%1(13)"
	: : "r" (enable), "i" (offsetof(struct paca_struct, soft_enabled)));
}

static inline notrace int decrementer_check_overflow(void)
{
 	u64 now = get_tb_or_rtc();
 	u64 *next_tb = &__get_cpu_var(decrementers_next_tb);
 
	return now >= *next_tb;
}

/* This is called whenever we are re-enabling interrupts
 * and returns either 0 (nothing to do) or 500/900/280/a00/e80 if
 * there's an EE, DEC or DBELL to generate.
 *
 * This is called in two contexts: From arch_local_irq_restore()
 * before soft-enabling interrupts, and from the exception exit
 * path when returning from an interrupt from a soft-disabled to
 * a soft enabled context. In both case we have interrupts hard
 * disabled.
 *
 * We take care of only clearing the bits we handled in the
 * PACA irq_happened field since we can only re-emit one at a
 * time and we don't want to "lose" one.
 */
notrace unsigned int __check_irq_replay(void)
{
	/*
	 * We use local_paca rather than get_paca() to avoid all
	 * the debug_smp_processor_id() business in this low level
	 * function
	 */
	unsigned char happened = local_paca->irq_happened;

	/* Clear bit 0 which we wouldn't clear otherwise */
	local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;

	/*
	 * Force the delivery of pending soft-disabled interrupts on PS3.
	 * Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp, tmp2;
		lv1_get_version_info(&tmp, &tmp2);
	}

	/*
	 * We may have missed a decrementer interrupt. We check the
	 * decrementer itself rather than the paca irq_happened field
	 * in case we also had a rollover while hard disabled
	 */
	local_paca->irq_happened &= ~PACA_IRQ_DEC;
	if (decrementer_check_overflow())
		return 0x900;

	/* Finally check if an external interrupt happened */
	local_paca->irq_happened &= ~PACA_IRQ_EE;
	if (happened & PACA_IRQ_EE)
		return 0x500;

#ifdef CONFIG_PPC_BOOK3E
	/* Finally check if an EPR external interrupt happened
	 * this bit is typically set if we need to handle another
	 * "edge" interrupt from within the MPIC "EPR" handler
	 */
	local_paca->irq_happened &= ~PACA_IRQ_EE_EDGE;
	if (happened & PACA_IRQ_EE_EDGE)
		return 0x500;

	local_paca->irq_happened &= ~PACA_IRQ_DBELL;
	if (happened & PACA_IRQ_DBELL)
		return 0x280;
#else
	local_paca->irq_happened &= ~PACA_IRQ_DBELL;
	if (happened & PACA_IRQ_DBELL) {
		if (cpu_has_feature(CPU_FTR_HVMODE))
			return 0xe80;
		return 0xa00;
	}
#endif /* CONFIG_PPC_BOOK3E */

	/* There should be nothing left ! */
	BUG_ON(local_paca->irq_happened != 0);

	return 0;
}

notrace void arch_local_irq_restore(unsigned long en)
{
	unsigned char irq_happened;
	unsigned int replay;

	/* Write the new soft-enabled value */
	set_soft_enabled(en);
	if (!en)
		return;
	/*
	 * From this point onward, we can take interrupts, preempt,
	 * etc... unless we got hard-disabled. We check if an event
	 * happened. If none happened, we know we can just return.
	 *
	 * We may have preempted before the check below, in which case
	 * we are checking the "new" CPU instead of the old one. This
	 * is only a problem if an event happened on the "old" CPU.
	 *
	 * External interrupt events will have caused interrupts to
	 * be hard-disabled, so there is no problem, we
	 * cannot have preempted.
	 */
	irq_happened = get_irq_happened();
	if (!irq_happened)
		return;

	/*
	 * We need to hard disable to get a trusted value from
	 * __check_irq_replay(). We also need to soft-disable
	 * again to avoid warnings in there due to the use of
	 * per-cpu variables.
	 *
	 * We know that if the value in irq_happened is exactly 0x01
	 * then we are already hard disabled (there are other less
	 * common cases that we'll ignore for now), so we skip the
	 * (expensive) mtmsrd.
	 */
	if (unlikely(irq_happened != PACA_IRQ_HARD_DIS))
		__hard_irq_disable();
#ifdef CONFIG_TRACE_IRQFLAGS
	else {
		/*
		 * We should already be hard disabled here. We had bugs
		 * where that wasn't the case so let's dbl check it and
		 * warn if we are wrong. Only do that when IRQ tracing
		 * is enabled as mfmsr() can be costly.
		 */
		if (WARN_ON(mfmsr() & MSR_EE))
			__hard_irq_disable();
	}
#endif /* CONFIG_TRACE_IRQFLAG */

	set_soft_enabled(0);

	/*
	 * Check if anything needs to be re-emitted. We haven't
	 * soft-enabled yet to avoid warnings in decrementer_check_overflow
	 * accessing per-cpu variables
	 */
	replay = __check_irq_replay();

	/* We can soft-enable now */
	set_soft_enabled(1);

	/*
	 * And replay if we have to. This will return with interrupts
	 * hard-enabled.
	 */
	if (replay) {
		__replay_interrupt(replay);
		return;
	}

	/* Finally, let's ensure we are hard enabled */
	__hard_irq_enable();
}
EXPORT_SYMBOL(arch_local_irq_restore);

/*
 * This is specifically called by assembly code to re-enable interrupts
 * if they are currently disabled. This is typically called before
 * schedule() or do_signal() when returning to userspace. We do it
 * in C to avoid the burden of dealing with lockdep etc...
 *
 * NOTE: This is called with interrupts hard disabled but not marked
 * as such in paca->irq_happened, so we need to resync this.
 */
void notrace restore_interrupts(void)
{
	if (irqs_disabled()) {
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
		local_irq_enable();
	} else
		__hard_irq_enable();
}

/*
 * This is a helper to use when about to go into idle low-power
 * when the latter has the side effect of re-enabling interrupts
 * (such as calling H_CEDE under pHyp).
 *
 * You call this function with interrupts soft-disabled (this is
 * already the case when ppc_md.power_save is called). The function
 * will return whether to enter power save or just return.
 *
 * In the former case, it will have notified lockdep of interrupts
 * being re-enabled and generally sanitized the lazy irq state,
 * and in the latter case it will leave with interrupts hard
 * disabled and marked as such, so the local_irq_enable() call
 * in cpu_idle() will properly re-enable everything.
 */
bool prep_irq_for_idle(void)
{
	/*
	 * First we need to hard disable to ensure no interrupt
	 * occurs before we effectively enter the low power state
	 */
	hard_irq_disable();

	/*
	 * If anything happened while we were soft-disabled,
	 * we return now and do not enter the low power state.
	 */
	if (lazy_irq_pending())
		return false;

	/* Tell lockdep we are about to re-enable */
	trace_hardirqs_on();

	/*
	 * Mark interrupts as soft-enabled and clear the
	 * PACA_IRQ_HARD_DIS from the pending mask since we
	 * are about to hard enable as well as a side effect
	 * of entering the low power state.
	 */
	local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;
	local_paca->soft_enabled = 1;

	/* Tell the caller to enter the low power state */
	return true;
}

#endif /* CONFIG_PPC64 */

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
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).timer_irqs);
        seq_printf(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).spurious_irqs);
	seq_printf(p, "  Spurious interrupts\n");

	seq_printf(p, "%*s: ", prec, "CNT");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).pmu_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");

	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).mce_exceptions);
	seq_printf(p, "  Machine check exceptions\n");

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
	u64 sum = per_cpu(irq_stat, cpu).timer_irqs;

	sum += per_cpu(irq_stat, cpu).pmu_irqs;
	sum += per_cpu(irq_stat, cpu).mce_exceptions;
	sum += per_cpu(irq_stat, cpu).spurious_irqs;
#ifdef CONFIG_PPC_DOORBELL
	sum += per_cpu(irq_stat, cpu).doorbell_irqs;
#endif

	return sum;
}

#ifdef CONFIG_HOTPLUG_CPU
void migrate_irqs(void)
{
	struct irq_desc *desc;
	unsigned int irq;
	static int warned;
	cpumask_var_t mask;
	const struct cpumask *map = cpu_online_mask;

	alloc_cpumask_var(&mask, GFP_KERNEL);

	for_each_irq_desc(irq, desc) {
		struct irq_data *data;
		struct irq_chip *chip;

		data = irq_desc_get_irq_data(desc);
		if (irqd_is_per_cpu(data))
			continue;

		chip = irq_data_get_irq_chip(data);

		cpumask_and(mask, data->affinity, map);
		if (cpumask_any(mask) >= nr_cpu_ids) {
			printk("Breaking affinity for irq %i\n", irq);
			cpumask_copy(mask, map);
		}
		if (chip->irq_set_affinity)
			chip->irq_set_affinity(data, mask, true);
		else if (desc->action && !(warned++))
			printk("Cannot set affinity for irq %i\n", irq);
	}

	free_cpumask_var(mask);

	local_irq_enable();
	mdelay(1);
	local_irq_disable();
}
#endif

static inline void handle_one_irq(unsigned int irq)
{
	struct thread_info *curtp, *irqtp;
	unsigned long saved_sp_limit;
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (!desc)
		return;

	/* Switch to the irq stack to handle this */
	curtp = current_thread_info();
	irqtp = hardirq_ctx[smp_processor_id()];

	if (curtp == irqtp) {
		/* We're already on the irq stack, just handle it */
		desc->handle_irq(irq, desc);
		return;
	}

	saved_sp_limit = current->thread.ksp_limit;

	irqtp->task = curtp->task;
	irqtp->flags = 0;

	/* Copy the softirq bits in preempt_count so that the
	 * softirq checks work in the hardirq context. */
	irqtp->preempt_count = (irqtp->preempt_count & ~SOFTIRQ_MASK) |
			       (curtp->preempt_count & SOFTIRQ_MASK);

	current->thread.ksp_limit = (unsigned long)irqtp +
		_ALIGN_UP(sizeof(struct thread_info), 16);

	call_handle_irq(irq, desc, irqtp, desc->handle_irq);
	current->thread.ksp_limit = saved_sp_limit;
	irqtp->task = NULL;

	/* Set any flag that may have been set on the
	 * alternate stack
	 */
	if (irqtp->flags)
		set_bits(irqtp->flags, &curtp->flags);
}

static inline void check_stack_overflow(void)
{
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	long sp;

	sp = __get_SP() & (THREAD_SIZE-1);

	/* check for stack overflow: is there less than 2KB free? */
	if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
		printk("do_IRQ: stack overflow: %ld\n",
			sp - sizeof(struct thread_info));
		dump_stack();
	}
#endif
}

void do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int irq;

	irq_enter();

	trace_irq_entry(regs);

	check_stack_overflow();

	/*
	 * Query the platform PIC for the interrupt & ack it.
	 *
	 * This will typically lower the interrupt line to the CPU
	 */
	irq = ppc_md.get_irq();

	/* We can hard enable interrupts now */
	may_hard_irq_enable();

	/* And finally process it */
	if (irq != NO_IRQ)
		handle_one_irq(irq);
	else
		__get_cpu_var(irq_stat).spurious_irqs++;

	trace_irq_exit(regs);

	irq_exit();
	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	if (ppc_md.init_IRQ)
		ppc_md.init_IRQ();

	exc_lvl_ctx_init();

	irq_ctx_init();
}

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
struct thread_info   *critirq_ctx[NR_CPUS] __read_mostly;
struct thread_info    *dbgirq_ctx[NR_CPUS] __read_mostly;
struct thread_info *mcheckirq_ctx[NR_CPUS] __read_mostly;

void exc_lvl_ctx_init(void)
{
	struct thread_info *tp;
	int i, cpu_nr;

	for_each_possible_cpu(i) {
#ifdef CONFIG_PPC64
		cpu_nr = i;
#else
		cpu_nr = get_hard_smp_processor_id(i);
#endif
		memset((void *)critirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = critirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = 0;

#ifdef CONFIG_BOOKE
		memset((void *)dbgirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = dbgirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = 0;

		memset((void *)mcheckirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = mcheckirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = HARDIRQ_OFFSET;
#endif
	}
}
#endif

struct thread_info *softirq_ctx[NR_CPUS] __read_mostly;
struct thread_info *hardirq_ctx[NR_CPUS] __read_mostly;

void irq_ctx_init(void)
{
	struct thread_info *tp;
	int i;

	for_each_possible_cpu(i) {
		memset((void *)softirq_ctx[i], 0, THREAD_SIZE);
		tp = softirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = 0;

		memset((void *)hardirq_ctx[i], 0, THREAD_SIZE);
		tp = hardirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = HARDIRQ_OFFSET;
	}
}

static inline void do_softirq_onstack(void)
{
	struct thread_info *curtp, *irqtp;
	unsigned long saved_sp_limit = current->thread.ksp_limit;

	curtp = current_thread_info();
	irqtp = softirq_ctx[smp_processor_id()];
	irqtp->task = curtp->task;
	irqtp->flags = 0;
	current->thread.ksp_limit = (unsigned long)irqtp +
				    _ALIGN_UP(sizeof(struct thread_info), 16);
	call_do_softirq(irqtp);
	current->thread.ksp_limit = saved_sp_limit;
	irqtp->task = NULL;

	/* Set any flag that may have been set on the
	 * alternate stack
	 */
	if (irqtp->flags)
		set_bits(irqtp->flags, &curtp->flags);
}

void do_softirq(void)
{
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending())
		do_softirq_onstack();

	local_irq_restore(flags);
}

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

int arch_early_irq_init(void)
{
	return 0;
}

#ifdef CONFIG_PPC64
static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
#endif /* CONFIG_PPC64 */
