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

#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/dbell.h>
#include <asm/trace.h>
#include <asm/cpu_has_feature.h>

int distribute_irqs = 1;

static inline void next_interrupt(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
		WARN_ON(!(local_paca->irq_happened & PACA_IRQ_HARD_DIS));
		WARN_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);
	}

	/*
	 * We are responding to the next interrupt, so interrupt-off
	 * latencies should be reset here.
	 */
	lockdep_hardirq_exit();
	trace_hardirqs_on();
	trace_hardirqs_off();
	lockdep_hardirq_enter();
}

static inline bool irq_happened_test_and_clear(u8 irq)
{
	if (local_paca->irq_happened & irq) {
		local_paca->irq_happened &= ~irq;
		return true;
	}
	return false;
}

static void __replay_soft_interrupts(void)
{
	struct pt_regs regs;

	/*
	 * We use local_paca rather than get_paca() to avoid all the
	 * debug_smp_processor_id() business in this low level function.
	 */

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
		WARN_ON_ONCE(mfmsr() & MSR_EE);
		WARN_ON(!(local_paca->irq_happened & PACA_IRQ_HARD_DIS));
		WARN_ON(local_paca->irq_happened & PACA_IRQ_REPLAYING);
	}

	/*
	 * PACA_IRQ_REPLAYING prevents interrupt handlers from enabling
	 * MSR[EE] to get PMIs, which can result in more IRQs becoming
	 * pending.
	 */
	local_paca->irq_happened |= PACA_IRQ_REPLAYING;

	ppc_save_regs(&regs);
	regs.softe = IRQS_ENABLED;
	regs.msr |= MSR_EE;

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
	if (IS_ENABLED(CONFIG_PPC_BOOK3S) &&
	    irq_happened_test_and_clear(PACA_IRQ_HMI)) {
		regs.trap = INTERRUPT_HMI;
		handle_hmi_exception(&regs);
		next_interrupt(&regs);
	}

	if (irq_happened_test_and_clear(PACA_IRQ_DEC)) {
		regs.trap = INTERRUPT_DECREMENTER;
		timer_interrupt(&regs);
		next_interrupt(&regs);
	}

	if (irq_happened_test_and_clear(PACA_IRQ_EE)) {
		regs.trap = INTERRUPT_EXTERNAL;
		do_IRQ(&regs);
		next_interrupt(&regs);
	}

	if (IS_ENABLED(CONFIG_PPC_DOORBELL) &&
	    irq_happened_test_and_clear(PACA_IRQ_DBELL)) {
		regs.trap = INTERRUPT_DOORBELL;
		doorbell_exception(&regs);
		next_interrupt(&regs);
	}

	/* Book3E does not support soft-masking PMI interrupts */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S) &&
	    irq_happened_test_and_clear(PACA_IRQ_PMI)) {
		regs.trap = INTERRUPT_PERFMON;
		performance_monitor_exception(&regs);
		next_interrupt(&regs);
	}

	local_paca->irq_happened &= ~PACA_IRQ_REPLAYING;
}

void replay_soft_interrupts(void)
{
	irq_enter(); /* See comment in arch_local_irq_restore */
	__replay_soft_interrupts();
	irq_exit();
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
	kuap_assert_locked();

	if (kuap_state != AMR_KUAP_BLOCKED)
		set_kuap(AMR_KUAP_BLOCKED);

	__replay_soft_interrupts();

	if (kuap_state != AMR_KUAP_BLOCKED)
		set_kuap(kuap_state);
}
#else
#define replay_soft_interrupts_irqrestore() __replay_soft_interrupts()
#endif

notrace void arch_local_irq_restore(unsigned long mask)
{
	unsigned char irq_happened;

	/* Write the new soft-enabled value if it is a disable */
	if (mask) {
		irq_soft_mask_set(mask);
		return;
	}

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
		WARN_ON_ONCE(in_nmi());
		WARN_ON_ONCE(in_hardirq());
		WARN_ON_ONCE(local_paca->irq_happened & PACA_IRQ_REPLAYING);
	}

again:
	/*
	 * After the stb, interrupts are unmasked and there are no interrupts
	 * pending replay. The restart sequence makes this atomic with
	 * respect to soft-masked interrupts. If this was just a simple code
	 * sequence, a soft-masked interrupt could become pending right after
	 * the comparison and before the stb.
	 *
	 * This allows interrupts to be unmasked without hard disabling, and
	 * also without new hard interrupts coming in ahead of pending ones.
	 */
	asm_volatile_goto(
"1:					\n"
"		lbz	9,%0(13)	\n"
"		cmpwi	9,0		\n"
"		bne	%l[happened]	\n"
"		stb	9,%1(13)	\n"
"2:					\n"
		RESTART_TABLE(1b, 2b, 1b)
	: : "i" (offsetof(struct paca_struct, irq_happened)),
	    "i" (offsetof(struct paca_struct, irq_soft_mask))
	: "cr0", "r9"
	: happened);

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON_ONCE(!(mfmsr() & MSR_EE));

	/*
	 * If we came here from the replay below, we might have a preempt
	 * pending (due to preempt_enable_no_resched()). Have to check now.
	 */
	preempt_check_resched();

	return;

happened:
	irq_happened = READ_ONCE(local_paca->irq_happened);
	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		WARN_ON_ONCE(!irq_happened);

	if (irq_happened == PACA_IRQ_HARD_DIS) {
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
			WARN_ON_ONCE(mfmsr() & MSR_EE);
		irq_soft_mask_set(IRQS_ENABLED);
		local_paca->irq_happened = 0;
		__hard_irq_enable();
		preempt_check_resched();
		return;
	}

	/* Have interrupts to replay, need to hard disable first */
	if (!(irq_happened & PACA_IRQ_HARD_DIS)) {
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
			if (!(mfmsr() & MSR_EE)) {
				/*
				 * An interrupt could have come in and cleared
				 * MSR[EE] and set IRQ_HARD_DIS, so check
				 * IRQ_HARD_DIS again and warn if it is still
				 * clear.
				 */
				irq_happened = READ_ONCE(local_paca->irq_happened);
				WARN_ON_ONCE(!(irq_happened & PACA_IRQ_HARD_DIS));
			}
		}
		__hard_irq_disable();
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
	} else {
		if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG)) {
			if (WARN_ON_ONCE(mfmsr() & MSR_EE))
				__hard_irq_disable();
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

	/*
	 * Now enter interrupt context. The interrupt handlers themselves
	 * also call irq_enter/exit (which is okay, they can nest). But call
	 * it here now to hold off softirqs until the below irq_exit(). If
	 * we allowed replayed handlers to run softirqs, that enables irqs,
	 * which must replay interrupts, which recurses in here and makes
	 * things more complicated. The recursion is limited to 2, and it can
	 * be made to work, but it's complicated.
	 *
	 * local_bh_disable can not be used here because interrupts taken in
	 * idle are not in the right context (RCU, tick, etc) to run softirqs
	 * so irq_enter must be called.
	 */
	irq_enter();

	replay_soft_interrupts_irqrestore();

	irq_exit();

	if (unlikely(local_paca->irq_happened != PACA_IRQ_HARD_DIS)) {
		/*
		 * The softirq processing in irq_exit() may enable interrupts
		 * temporarily, which can result in MSR[EE] being enabled and
		 * more irqs becoming pending. Go around again if that happens.
		 */
		trace_hardirqs_on();
		preempt_enable_no_resched();
		goto again;
	}

	trace_hardirqs_on();
	irq_soft_mask_set(IRQS_ENABLED);
	local_paca->irq_happened = 0;
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

static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
