// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/err.h>
#include <linux/compat.h>
#include <linux/rseq.h>
#include <linux/sched/debug.h> /* for show_regs */

#include <asm/kup.h>
#include <asm/cputime.h>
#include <asm/hw_irq.h>
#include <asm/interrupt.h>
#include <asm/kprobes.h>
#include <asm/paca.h>
#include <asm/ptrace.h>
#include <asm/reg.h>
#include <asm/signal.h>
#include <asm/switch_to.h>
#include <asm/syscall.h>
#include <asm/time.h>
#include <asm/tm.h>
#include <asm/unistd.h>

#if defined(CONFIG_PPC_ADV_DEBUG_REGS) && defined(CONFIG_PPC32)
unsigned long global_dbcr0[NR_CPUS];
#endif

#ifdef CONFIG_PPC_BOOK3S_64
DEFINE_STATIC_KEY_FALSE(interrupt_exit_not_reentrant);
static inline bool exit_must_hard_disable(void)
{
	return static_branch_unlikely(&interrupt_exit_not_reentrant);
}
#else
static inline bool exit_must_hard_disable(void)
{
	return true;
}
#endif

/*
 * local irqs must be disabled. Returns false if the caller must re-enable
 * them, check for new work, and try again.
 *
 * This should be called with local irqs disabled, but if they were previously
 * enabled when the interrupt handler returns (indicating a process-context /
 * synchronous interrupt) then irqs_enabled should be true.
 *
 * restartable is true then EE/RI can be left on because interrupts are handled
 * with a restart sequence.
 */
static notrace __always_inline bool prep_irq_for_enabled_exit(bool restartable)
{
	bool must_hard_disable = (exit_must_hard_disable() || !restartable);

	/* This must be done with RI=1 because tracing may touch vmaps */
	trace_hardirqs_on();

	if (must_hard_disable)
		__hard_EE_RI_disable();

#ifdef CONFIG_PPC64
	/* This pattern matches prep_irq_for_idle */
	if (unlikely(lazy_irq_pending_nocheck())) {
		if (must_hard_disable) {
			local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
			__hard_RI_enable();
		}
		trace_hardirqs_off();

		return false;
	}
#endif
	return true;
}

/*
 * This should be called after a syscall returns, with r3 the return value
 * from the syscall. If this function returns non-zero, the system call
 * exit assembly should additionally load all GPR registers and CTR and XER
 * from the interrupt frame.
 *
 * The function graph tracer can not trace the return side of this function,
 * because RI=0 and soft mask state is "unreconciled", so it is marked notrace.
 */
notrace unsigned long syscall_exit_prepare(unsigned long r3,
					   struct pt_regs *regs,
					   long scv)
{
	unsigned long ti_flags;
	bool is_not_scv = !IS_ENABLED(CONFIG_PPC_BOOK3S_64) || !scv;

	kuap_assert_locked();

	regs->result = r3;
	regs->exit_flags = 0;

	ti_flags = read_thread_flags();

	if (unlikely(r3 >= (unsigned long)-MAX_ERRNO) && is_not_scv) {
		if (likely(!(ti_flags & (_TIF_NOERROR | _TIF_RESTOREALL)))) {
			r3 = -r3;
			regs->ccr |= 0x10000000; /* Set SO bit in CR */
		}
	}

	if (unlikely(ti_flags & _TIF_PERSYSCALL_MASK)) {
		if (ti_flags & _TIF_RESTOREALL)
			regs->exit_flags = _TIF_RESTOREALL;
		else
			regs->gpr[3] = r3;
		clear_bits(_TIF_PERSYSCALL_MASK, &current_thread_info()->flags);
	} else {
		regs->gpr[3] = r3;
	}

	if (unlikely(ti_flags & _TIF_SYSCALL_DOTRACE)) {
		regs->exit_flags |= _TIF_RESTOREALL;
	}

	syscall_exit_to_user_mode(regs);

again:
	user_enter_irqoff();
	if (!prep_irq_for_enabled_exit(true)) {
		user_exit_irqoff();
		local_irq_enable();
		local_irq_disable();
		goto again;
	}

	/* Restore user access locks last */
	kuap_user_restore(regs);

#ifdef CONFIG_PPC64
	regs->exit_result = regs->exit_flags;
#endif

	return regs->exit_flags;
}

#ifdef CONFIG_PPC64
notrace unsigned long syscall_exit_restart(unsigned long r3, struct pt_regs *regs)
{
	/*
	 * This is called when detecting a soft-pending interrupt as well as
	 * an alternate-return interrupt. So we can't just have the alternate
	 * return path clear SRR1[MSR] and set PACA_IRQ_HARD_DIS (unless
	 * the soft-pending case were to fix things up as well). RI might be
	 * disabled, in which case it gets re-enabled by __hard_irq_disable().
	 */
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

#ifdef CONFIG_PPC_BOOK3S_64
	set_kuap(AMR_KUAP_BLOCKED);
#endif

again:
	user_enter_irqoff();
	if (!prep_irq_for_enabled_exit(true)) {
		user_exit_irqoff();
		local_irq_enable();
		local_irq_disable();
		goto again;
	}

	kuap_user_restore(regs);
	regs->exit_result |= regs->exit_flags;

	return regs->exit_result;
}
#endif

notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs)
{
	unsigned long ret;

	BUG_ON(regs_is_unrecoverable(regs));
	BUG_ON(regs_irqs_disabled(regs));

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * AMR can only have been unlocked if we interrupted the kernel.
	 */
	kuap_assert_locked();

	local_irq_disable();
	regs->exit_flags = 0;
again:
	check_return_regs_valid(regs);
	user_enter_irqoff();
	if (!prep_irq_for_enabled_exit(true)) {
		user_exit_irqoff();
		local_irq_enable();
		local_irq_disable();
		goto again;
	}

	/* Restore user access locks last */
	kuap_user_restore(regs);

	ret = regs->exit_flags;

#ifdef CONFIG_PPC64
	regs->exit_result = ret;
#endif

	return ret;
}

void preempt_schedule_irq(void);

notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs)
{
	unsigned long ret = 0;
	unsigned long kuap;
	bool stack_store = read_thread_flags() & _TIF_EMULATE_STACK_STORE;

	if (regs_is_unrecoverable(regs))
		unrecoverable_exception(regs);
	/*
	 * CT_WARN_ON comes here via program_check_exception, so avoid
	 * recursion.
	 *
	 * Skip the assertion on PMIs on 64e to work around a problem caused
	 * by NMI PMIs incorrectly taking this interrupt return path, it's
	 * possible for this to hit after interrupt exit to user switches
	 * context to user. See also the comment in the performance monitor
	 * handler in exceptions-64e.S
	 */
	if (!IS_ENABLED(CONFIG_PPC_BOOK3E_64) &&
	    TRAP(regs) != INTERRUPT_PROGRAM &&
	    TRAP(regs) != INTERRUPT_PERFMON)
		CT_WARN_ON(ct_state() == CT_STATE_USER);

	kuap = kuap_get_and_assert_locked();

	local_irq_disable();

	if (!regs_irqs_disabled(regs)) {
		/* Returning to a kernel context with local irqs enabled. */
		WARN_ON_ONCE(!(regs->msr & MSR_EE));
again:

		check_return_regs_valid(regs);

		/*
		 * Stack store exit can't be restarted because the interrupt
		 * stack frame might have been clobbered.
		 */
		if (!prep_irq_for_enabled_exit(unlikely(stack_store))) {
			/*
			 * Replay pending soft-masked interrupts now. Don't
			 * just local_irq_enabe(); local_irq_disable(); because
			 * if we are returning from an asynchronous interrupt
			 * here, another one might hit after irqs are enabled,
			 * and it would exit via this same path allowing
			 * another to fire, and so on unbounded.
			 */
			hard_irq_disable();
			replay_soft_interrupts();
			/* Took an interrupt, may have more exit work to do. */
			goto again;
		}
#ifdef CONFIG_PPC64
		/*
		 * An interrupt may clear MSR[EE] and set this concurrently,
		 * but it will be marked pending and the exit will be retried.
		 * This leaves a racy window where MSR[EE]=0 and HARD_DIS is
		 * clear, until interrupt_exit_kernel_restart() calls
		 * hard_irq_disable(), which will set HARD_DIS again.
		 */
		local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;

	} else {
		check_return_regs_valid(regs);

		if (unlikely(stack_store))
			__hard_EE_RI_disable();
#else
	} else {
		__hard_EE_RI_disable();
#endif /* CONFIG_PPC64 */
	}

	if (unlikely(stack_store)) {
		clear_bits(_TIF_EMULATE_STACK_STORE, &current_thread_info()->flags);
		ret = 1;
	}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	/*
	 * 64s does not want to mfspr(SPRN_AMR) here, because this comes after
	 * mtmsr, which would cause Read-After-Write stalls. Hence, take the
	 * AMR value from the check above.
	 */
	kuap_kernel_restore(regs, kuap);

	return ret;
}

#ifdef CONFIG_PPC64
notrace unsigned long interrupt_exit_user_restart(struct pt_regs *regs)
{
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

#ifdef CONFIG_PPC_BOOK3S_64
	set_kuap(AMR_KUAP_BLOCKED);
#endif

	trace_hardirqs_off();
	account_cpu_user_entry();

	BUG_ON(!user_mode(regs));

	regs->exit_result |= interrupt_exit_user_prepare(regs);

	return regs->exit_result;
}

/*
 * No real need to return a value here because the stack store case does not
 * get restarted.
 */
notrace unsigned long interrupt_exit_kernel_restart(struct pt_regs *regs)
{
	__hard_irq_disable();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

#ifdef CONFIG_PPC_BOOK3S_64
	set_kuap(AMR_KUAP_BLOCKED);
#endif

	if (regs->softe == IRQS_ENABLED)
		trace_hardirqs_off();

	BUG_ON(user_mode(regs));

	return interrupt_exit_kernel_prepare(regs);
}
#endif
