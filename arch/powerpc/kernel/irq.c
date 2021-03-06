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

#include <linux/uaccess.h>
#include <asm/interrupt.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/livepatch.h>
#include <asm/asm-prototypes.h>
#include <asm/hw_irq.h>
#include <asm/softirq_stack.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/dbell.h>
#endif
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

#ifdef CONFIG_PPC64

int distribute_irqs = 1;

static inline notrace unsigned long get_irq_happened(void)
{
	unsigned long happened;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (happened) : "i" (offsetof(struct paca_struct, irq_happened)));

	return happened;
}

#ifdef CONFIG_PPC_BOOK3E

/* This is called whenever we are re-enabling interrupts
 * and returns either 0 (nothing to do) or 500/900/280 if
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

	/*
	 * We are responding to the next interrupt, so interrupt-off
	 * latencies should be reset here.
	 */
	trace_hardirqs_on();
	trace_hardirqs_off();

	if (happened & PACA_IRQ_DEC) {
		local_paca->irq_happened &= ~PACA_IRQ_DEC;
		return 0x900;
	}

	if (happened & PACA_IRQ_EE) {
		local_paca->irq_happened &= ~PACA_IRQ_EE;
		return 0x500;
	}

	if (happened & PACA_IRQ_DBELL) {
		local_paca->irq_happened &= ~PACA_IRQ_DBELL;
		return 0x280;
	}

	if (happened & PACA_IRQ_HARD_DIS)
		local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;

	/* There should be nothing left ! */
	BUG_ON(local_paca->irq_happened != 0);

	return 0;
}

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

#endif /* CONFIG_PPC_BOOK3E */

void replay_soft_interrupts(void)
{
	struct pt_regs regs;

	/*
	 * Be careful here, calling these interrupt handlers can cause
	 * softirqs to be raised, which they may run when calling irq_exit,
	 * which will cause local_irq_enable() to be run, which can then
	 * recurse into this function. Don't keep any state across
	 * interrupt handler calls which may change underneath us.
	 *
	 * We use local_paca rather than get_paca() to avoid all the
	 * debug_smp_processor_id() business in this low level function.
	 */

	ppc_save_regs(&regs);
	regs.softe = IRQS_ENABLED;

again:
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON_ONCE(mfmsr() & MSR_EE);

	/*
	 * Force the delivery of pending soft-disabled interrupts on PS3.
	 * Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp, tmp2;
		lv1_get_version_info(&tmp, &tmp2);
	}

	/*
	 * Check if an hypervisor Maintenance interrupt happened.
	 * This is a higher priority interrupt than the others, so
	 * replay it first.
	 */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S) && (local_paca->irq_happened & PACA_IRQ_HMI)) {
		local_paca->irq_happened &= ~PACA_IRQ_HMI;
		regs.trap = 0xe60;
		handle_hmi_exception(&regs);
		if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS))
			hard_irq_disable();
	}

	if (local_paca->irq_happened & PACA_IRQ_DEC) {
		local_paca->irq_happened &= ~PACA_IRQ_DEC;
		regs.trap = 0x900;
		timer_interrupt(&regs);
		if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS))
			hard_irq_disable();
	}

	if (local_paca->irq_happened & PACA_IRQ_EE) {
		local_paca->irq_happened &= ~PACA_IRQ_EE;
		regs.trap = 0x500;
		do_IRQ(&regs);
		if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS))
			hard_irq_disable();
	}

	if (IS_ENABLED(CONFIG_PPC_DOORBELL) && (local_paca->irq_happened & PACA_IRQ_DBELL)) {
		local_paca->irq_happened &= ~PACA_IRQ_DBELL;
		if (IS_ENABLED(CONFIG_PPC_BOOK3E))
			regs.trap = 0x280;
		else
			regs.trap = 0xa00;
		doorbell_exception(&regs);
		if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS))
			hard_irq_disable();
	}

	/* Book3E does not support soft-masking PMI interrupts */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S) && (local_paca->irq_happened & PACA_IRQ_PMI)) {
		local_paca->irq_happened &= ~PACA_IRQ_PMI;
		regs.trap = 0xf00;
		performance_monitor_exception(&regs);
		if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS))
			hard_irq_disable();
	}

	if (local_paca->irq_happened & ~PACA_IRQ_HARD_DIS) {
		/*
		 * We are responding to the next interrupt, so interrupt-off
		 * latencies should be reset here.
		 */
		trace_hardirqs_on();
		trace_hardirqs_off();
		goto again;
	}
}

#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_PPC_KUAP)
static inline void replay_soft_interrupts_irqrestore(void)
{
	unsigned long kuap_state = get_kuap();

	/*
	 * Check if anything calls local_irq_enable/restore() when KUAP is
	 * disabled (user access enabled). We handle that case here by saving
	 * and re-locking AMR but we shouldn't get here in the first place,
	 * hence the warning.
	 */
	kuap_check_amr();

	if (kuap_state != AMR_KUAP_BLOCKED)
		set_kuap(AMR_KUAP_BLOCKED);

	replay_soft_interrupts();

	if (kuap_state != AMR_KUAP_BLOCKED)
		set_kuap(kuap_state);
}
#else
#define replay_soft_interrupts_irqrestore() replay_soft_interrupts()
#endif

notrace void arch_local_irq_restore(unsigned long mask)
{
	unsigned char irq_happened;

	/* Write the new soft-enabled value */
	irq_soft_mask_set(mask);
	if (mask)
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
	if (!irq_happened) {
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
			WARN_ON_ONCE(!(mfmsr() & MSR_EE));
		return;
	}

	/* We need to hard disable to replay. */
	if (!(irq_happened & PACA_IRQ_HARD_DIS)) {
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
			WARN_ON_ONCE(!(mfmsr() & MSR_EE));
		__hard_irq_disable();
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
	} else {
		/*
		 * We should already be hard disabled here. We had bugs
		 * where that wasn't the case so let's dbl check it and
		 * warn if we are wrong. Only do that when IRQ tracing
		 * is enabled as mfmsr() can be costly.
		 */
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
			if (WARN_ON_ONCE(mfmsr() & MSR_EE))
				__hard_irq_disable();
		}

		if (irq_happened == PACA_IRQ_HARD_DIS) {
			local_paca->irq_happened = 0;
			__hard_irq_enable();
			return;
		}
	}

	/*
	 * Disable preempt here, so that the below preempt_enable will
	 * perform resched if required (a replayed interrupt may set
	 * need_resched).
	 */
	preempt_disable();
	irq_soft_mask_set(IRQS_ALL_DISABLED);
	trace_hardirqs_off();

	replay_soft_interrupts_irqrestore();
	local_paca->irq_happened = 0;

	trace_hardirqs_on();
	irq_soft_mask_set(IRQS_ENABLED);
	__hard_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(arch_local_irq_restore);

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
 * in arch_cpu_idle() will properly re-enable everything.
 */
bool prep_irq_for_idle(void)
{
	/*
	 * First we need to hard disable to ensure no interrupt
	 * occurs before we effectively enter the low power state
	 */
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

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
	irq_soft_mask_set(IRQS_ENABLED);

	/* Tell the caller to enter the low power state */
	return true;
}

#ifdef CONFIG_PPC_BOOK3S
/*
 * This is for idle sequences that return with IRQs off, but the
 * idle state itself wakes on interrupt. Tell the irq tracer that
 * IRQs are enabled for the duration of idle so it does not get long
 * off times. Must be paired with fini_irq_for_idle_irqsoff.
 */
bool prep_irq_for_idle_irqsoff(void)
{
	WARN_ON(!irqs_disabled());

	/*
	 * First we need to hard disable to ensure no interrupt
	 * occurs before we effectively enter the low power state
	 */
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

	/*
	 * If anything happened while we were soft-disabled,
	 * we return now and do not enter the low power state.
	 */
	if (lazy_irq_pending())
		return false;

	/* Tell lockdep we are about to re-enable */
	trace_hardirqs_on();

	return true;
}

/*
 * Take the SRR1 wakeup reason, index into this table to find the
 * appropriate irq_happened bit.
 *
 * Sytem reset exceptions taken in idle state also come through here,
 * but they are NMI interrupts so do not need to wait for IRQs to be
 * restored, and should be taken as early as practical. These are marked
 * with 0xff in the table. The Power ISA specifies 0100b as the system
 * reset interrupt reason.
 */
#define IRQ_SYSTEM_RESET	0xff

static const u8 srr1_to_lazyirq[0x10] = {
	0, 0, 0,
	PACA_IRQ_DBELL,
	IRQ_SYSTEM_RESET,
	PACA_IRQ_DBELL,
	PACA_IRQ_DEC,
	0,
	PACA_IRQ_EE,
	PACA_IRQ_EE,
	PACA_IRQ_HMI,
	0, 0, 0, 0, 0 };

void replay_system_reset(void)
{
	struct pt_regs regs;

	ppc_save_regs(&regs);
	regs.trap = 0x100;
	get_paca()->in_nmi = 1;
	system_reset_exception(&regs);
	get_paca()->in_nmi = 0;
}
EXPORT_SYMBOL_GPL(replay_system_reset);

void irq_set_pending_from_srr1(unsigned long srr1)
{
	unsigned int idx = (srr1 & SRR1_WAKEMASK_P8) >> 18;
	u8 reason = srr1_to_lazyirq[idx];

	/*
	 * Take the system reset now, which is immediately after registers
	 * are restored from idle. It's an NMI, so interrupts need not be
	 * re-enabled before it is taken.
	 */
	if (unlikely(reason == IRQ_SYSTEM_RESET)) {
		replay_system_reset();
		return;
	}

	if (reason == PACA_IRQ_DBELL) {
		/*
		 * When doorbell triggers a system reset wakeup, the message
		 * is not cleared, so if the doorbell interrupt is replayed
		 * and the IPI handled, the doorbell interrupt would still
		 * fire when EE is enabled.
		 *
		 * To avoid taking the superfluous doorbell interrupt,
		 * execute a msgclr here before the interrupt is replayed.
		 */
		ppc_msgclr(PPC_DBELL_MSGTYPE);
	}

	/*
	 * The 0 index (SRR1[42:45]=b0000) must always evaluate to 0,
	 * so this can be called unconditionally with the SRR1 wake
	 * reason as returned by the idle code, which uses 0 to mean no
	 * interrupt.
	 *
	 * If a future CPU was to designate this as an interrupt reason,
	 * then a new index for no interrupt must be assigned.
	 */
	local_paca->irq_happened |= reason;
}
#endif /* CONFIG_PPC_BOOK3S */

/*
 * Force a replay of the external interrupt handler on this CPU.
 */
void force_external_irq_replay(void)
{
	/*
	 * This must only be called with interrupts soft-disabled,
	 * the replay will happen when re-enabling.
	 */
	WARN_ON(!arch_irqs_disabled());

	/*
	 * Interrupts must always be hard disabled before irq_happened is
	 * modified (to prevent lost update in case of interrupt between
	 * load and store).
	 */
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

	/* Indicate in the PACA that we have an interrupt to replay */
	local_paca->irq_happened |= PACA_IRQ_EE;
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

static inline void check_stack_overflow(void)
{
	long sp;

	if (!IS_ENABLED(CONFIG_DEBUG_STACKOVERFLOW))
		return;

	sp = current_stack_pointer & (THREAD_SIZE - 1);

	/* check for stack overflow: is there less than 2KB free? */
	if (unlikely(sp < 2048)) {
		pr_err("do_IRQ: stack overflow: %ld\n", sp);
		dump_stack();
	}
}

void __do_irq(struct pt_regs *regs)
{
	unsigned int irq;

	trace_irq_entry(regs);

	/*
	 * Query the platform PIC for the interrupt & ack it.
	 *
	 * This will typically lower the interrupt line to the CPU
	 */
	irq = ppc_md.get_irq();

	/* We can hard enable interrupts now to allow perf interrupts */
	may_hard_irq_enable();

	/* And finally process it */
	if (unlikely(!irq))
		__this_cpu_inc(irq_stat.spurious_irqs);
	else
		generic_handle_irq(irq);

	trace_irq_exit(regs);
}

DEFINE_INTERRUPT_HANDLER_ASYNC(do_IRQ)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	void *cursp, *irqsp, *sirqsp;

	/* Switch to the irq stack to handle this */
	cursp = (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
	irqsp = hardirq_ctx[raw_smp_processor_id()];
	sirqsp = softirq_ctx[raw_smp_processor_id()];

	check_stack_overflow();

	/* Already there ? */
	if (unlikely(cursp == irqsp || cursp == sirqsp)) {
		__do_irq(regs);
		set_irq_regs(old_regs);
		return;
	}
	/* Switch stack and call */
	call_do_irq(regs, irqsp);

	set_irq_regs(old_regs);
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
}

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
void   *critirq_ctx[NR_CPUS] __read_mostly;
void    *dbgirq_ctx[NR_CPUS] __read_mostly;
void *mcheckirq_ctx[NR_CPUS] __read_mostly;
#endif

void *softirq_ctx[NR_CPUS] __read_mostly;
void *hardirq_ctx[NR_CPUS] __read_mostly;

void do_softirq_own_stack(void)
{
	call_do_softirq(softirq_ctx[smp_processor_id()]);
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

#ifdef CONFIG_PPC64
static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
#endif /* CONFIG_PPC64 */
