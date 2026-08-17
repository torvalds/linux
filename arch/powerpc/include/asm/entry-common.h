/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_PPC_ENTRY_COMMON_H
#define _ASM_PPC_ENTRY_COMMON_H

#include <asm/cputime.h>
#include <asm/interrupt.h>
#include <asm/runlatch.h>
#include <asm/stacktrace.h>
#include <asm/switch_to.h>
#include <asm/tm.h>

#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
/*
 * WARN/BUG is handled with a program interrupt so minimise checks here to
 * avoid recursion and maximise the chance of getting the first oops handled.
 */
#define INT_SOFT_MASK_BUG_ON(regs, cond)				\
do {									\
	if ((user_mode(regs) || (TRAP(regs) != INTERRUPT_PROGRAM)))	\
		BUG_ON(cond);						\
} while (0)
#else
#define INT_SOFT_MASK_BUG_ON(regs, cond)
#endif

#ifdef CONFIG_PPC_BOOK3S_64
extern char __end_soft_masked[];
bool search_kernel_soft_mask_table(unsigned long addr);
unsigned long search_kernel_restart_table(unsigned long addr);

DECLARE_STATIC_KEY_FALSE(interrupt_exit_not_reentrant);

static inline bool is_implicit_soft_masked(struct pt_regs *regs)
{
	if (user_mode(regs))
		return false;

	if (regs->nip >= (unsigned long)__end_soft_masked)
		return false;

	return search_kernel_soft_mask_table(regs->nip);
}

static inline void srr_regs_clobbered(void)
{
	local_paca->srr_valid = 0;
	local_paca->hsrr_valid = 0;
}
#else
static inline unsigned long search_kernel_restart_table(unsigned long addr)
{
	return 0;
}

static inline bool is_implicit_soft_masked(struct pt_regs *regs)
{
	return false;
}

static inline void srr_regs_clobbered(void)
{
}
#endif

static inline void nap_adjust_return(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_970_NAP
	if (unlikely(test_thread_local_flags(_TLF_NAPPING))) {
		/* Can avoid a test-and-clear because NMIs do not call this */
		clear_thread_local_flags(_TLF_NAPPING);
		regs_set_return_ip(regs, (unsigned long)power4_idle_nap_return);
	}
#endif
}

static __always_inline void booke_load_dbcr0(void)
{
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	unsigned long dbcr0 = current->thread.debug.dbcr0;

	if (likely(!(dbcr0 & DBCR0_IDM)))
		return;

	/*
	 * Check to see if the dbcr0 register is set up to debug.
	 * Use the internal debug mode bit to do this.
	 */
	mtmsr(mfmsr() & ~MSR_DE);
	if (IS_ENABLED(CONFIG_PPC32)) {
		isync();
		global_dbcr0[smp_processor_id()] = mfspr(SPRN_DBCR0);
	}
	mtspr(SPRN_DBCR0, dbcr0);
	mtspr(SPRN_DBSR, -1);
#endif
}

static inline void booke_restore_dbcr0(void)
{
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	unsigned long dbcr0 = current->thread.debug.dbcr0;

	if (IS_ENABLED(CONFIG_PPC32) && unlikely(dbcr0 & DBCR0_IDM)) {
		mtspr(SPRN_DBSR, -1);
		mtspr(SPRN_DBCR0, global_dbcr0[smp_processor_id()]);
	}
#endif
}

static inline void check_return_regs_valid(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_BOOK3S_64
	unsigned long trap, srr0, srr1;
	static bool warned;
	u8 *validp;
	char *h;

	if (trap_is_scv(regs))
		return;

	trap = TRAP(regs);
	// EE in HV mode sets HSRRs like 0xea0
	if (cpu_has_feature(CPU_FTR_HVMODE) && trap == INTERRUPT_EXTERNAL)
		trap = 0xea0;

	switch (trap) {
	case 0x980:
	case INTERRUPT_H_DATA_STORAGE:
	case 0xe20:
	case 0xe40:
	case INTERRUPT_HMI:
	case 0xe80:
	case 0xea0:
	case INTERRUPT_H_FAC_UNAVAIL:
	case 0x1200:
	case 0x1500:
	case 0x1600:
	case 0x1800:
		validp = &local_paca->hsrr_valid;
		if (!READ_ONCE(*validp))
			return;

		srr0 = mfspr(SPRN_HSRR0);
		srr1 = mfspr(SPRN_HSRR1);
		h = "H";

		break;
	default:
		validp = &local_paca->srr_valid;
		if (!READ_ONCE(*validp))
			return;

		srr0 = mfspr(SPRN_SRR0);
		srr1 = mfspr(SPRN_SRR1);
		h = "";
		break;
	}

	if (srr0 == regs->nip && srr1 == regs->msr)
		return;

	/*
	 * A NMI / soft-NMI interrupt may have come in after we found
	 * srr_valid and before the SRRs are loaded. The interrupt then
	 * comes in and clobbers SRRs and clears srr_valid. Then we load
	 * the SRRs here and test them above and find they don't match.
	 *
	 * Test validity again after that, to catch such false positives.
	 *
	 * This test in general will have some window for false negatives
	 * and may not catch and fix all such cases if an NMI comes in
	 * later and clobbers SRRs without clearing srr_valid, but hopefully
	 * such things will get caught most of the time, statistically
	 * enough to be able to get a warning out.
	 */
	if (!READ_ONCE(*validp))
		return;

	if (!data_race(warned)) {
		data_race(warned = true);
		pr_warn("%sSRR0 was: %lx should be: %lx\n", h, srr0, regs->nip);
		pr_warn("%sSRR1 was: %lx should be: %lx\n", h, srr1, regs->msr);
		show_regs(regs);
	}

	WRITE_ONCE(*validp, 0); /* fixup */
#endif
}

static inline void arch_interrupt_enter_prepare(struct pt_regs *regs)
{
#ifdef CONFIG_PPC64
	irq_soft_mask_set(IRQS_ALL_DISABLED);

	/*
	 * If the interrupt was taken with HARD_DIS clear, then enable MSR[EE].
	 * Asynchronous interrupts get here with HARD_DIS set (see below), so
	 * this enables MSR[EE] for synchronous interrupts. IRQs remain
	 * soft-masked. The interrupt handler may later call
	 * interrupt_cond_local_irq_enable() to achieve a regular process
	 * context.
	 */
	if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS)) {
		INT_SOFT_MASK_BUG_ON(regs, !(regs->msr & MSR_EE));
		__hard_irq_enable();
	} else {
		__hard_RI_enable();
	}
	/* Enable MSR[RI] early, to support kernel SLB and hash faults */
#endif

	if (!regs_irqs_disabled(regs))
		trace_hardirqs_off();

	if (user_mode(regs)) {
		kuap_lock();
		account_cpu_user_entry();
		account_stolen_time();
	} else {
		kuap_save_and_lock(regs);
		/*
		 * CT_WARN_ON comes here via program_check_exception,
		 * so avoid recursion.
		 */
		if (TRAP(regs) != INTERRUPT_PROGRAM)
			CT_WARN_ON(ct_state() != CT_STATE_KERNEL &&
				   ct_state() != CT_STATE_IDLE);
		INT_SOFT_MASK_BUG_ON(regs, is_implicit_soft_masked(regs));
		INT_SOFT_MASK_BUG_ON(regs, regs_irqs_disabled(regs) &&
				     search_kernel_restart_table(regs->nip));
	}
	INT_SOFT_MASK_BUG_ON(regs, !regs_irqs_disabled(regs) &&
			     !(regs->msr & MSR_EE));

	booke_restore_dbcr0();
}

/*
 * Care should be taken to note that arch_interrupt_exit_prepare and
 * arch_interrupt_async_exit_prepare do not necessarily return immediately to
 * regs context (e.g., if regs is usermode, we don't necessarily return to
 * user mode). Other interrupts might be taken between here and return,
 * context switch / preemption may occur in the exit path after this, or a
 * signal may be delivered, etc.
 *
 * The real interrupt exit code is platform specific, e.g.,
 * interrupt_exit_user_prepare / interrupt_exit_kernel_prepare for 64s.
 *
 * However arch_interrupt_nmi_exit_prepare does return directly to regs, because
 * NMIs do not do "exit work" or replay soft-masked interrupts.
 */
static inline void arch_interrupt_exit_prepare(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		BUG_ON(regs_is_unrecoverable(regs));
		BUG_ON(regs_irqs_disabled(regs));
		/*
		 * We don't need to restore AMR on the way back to userspace for KUAP.
		 * AMR can only have been unlocked if we interrupted the kernel.
		 */
		kuap_assert_locked();
	}

	/* irqentry_exit expects to be called with interrupts disabled */
	local_irq_disable();
}

static inline void arch_interrupt_async_enter_prepare(struct pt_regs *regs)
{
#ifdef CONFIG_PPC64
	/* Ensure arch_interrupt_enter_prepare does not enable MSR[EE] */
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
#endif
	arch_interrupt_enter_prepare(regs);
#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * RI=1 is set by arch_interrupt_enter_prepare, so this thread flags access
	 * has to come afterward (it can cause SLB faults).
	 */
	if (cpu_has_feature(CPU_FTR_CTRL) &&
	    !test_thread_local_flags(_TLF_RUNLATCH))
		__ppc64_runlatch_on();
#endif
}

static inline void arch_interrupt_async_exit_prepare(struct pt_regs *regs)
{
	/*
	 * Adjust at exit so the main handler sees the true NIA. This must
	 * come before irq_exit() because irq_exit can enable interrupts, and
	 * if another interrupt is taken before nap_adjust_return has run
	 * here, then that interrupt would return directly to idle nap return.
	 */
	nap_adjust_return(regs);

	arch_interrupt_exit_prepare(regs);
}

struct interrupt_nmi_state {
#ifdef CONFIG_PPC64
	u8 irq_soft_mask;
	u8 irq_happened;
	u8 ftrace_enabled;
	u64 softe;
#endif
};

static inline bool nmi_disables_ftrace(struct pt_regs *regs)
{
	/* Allow DEC and PMI to be traced when they are soft-NMI */
	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64)) {
		if (TRAP(regs) == INTERRUPT_DECREMENTER)
			return false;
		if (TRAP(regs) == INTERRUPT_PERFMON)
			return false;
	}
	if (IS_ENABLED(CONFIG_PPC_BOOK3E_64)) {
		if (TRAP(regs) == INTERRUPT_PERFMON)
			return false;
	}

	return true;
}

static inline void arch_interrupt_nmi_enter_prepare(struct pt_regs *regs,
						    struct interrupt_nmi_state *state)
{
#ifdef CONFIG_PPC64
	state->irq_soft_mask = local_paca->irq_soft_mask;
	state->irq_happened = local_paca->irq_happened;
	state->softe = regs->softe;

	/*
	 * Set IRQS_ALL_DISABLED unconditionally so irqs_disabled() does
	 * the right thing, and set IRQ_HARD_DIS. We do not want to reconcile
	 * because that goes through irq tracing which we don't want in NMI.
	 */
	local_paca->irq_soft_mask = IRQS_ALL_DISABLED;
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

	if (!(regs->msr & MSR_EE) || is_implicit_soft_masked(regs)) {
		/*
		 * Adjust regs->softe to be soft-masked if it had not been
		 * reconcied (e.g., interrupt entry with MSR[EE]=0 but softe
		 * not yet set disabled), or if it was in an implicit soft
		 * masked state. This makes regs_irqs_disabled(regs)
		 * behave as expected.
		 */
		regs->softe = IRQS_ALL_DISABLED;
	}

	__hard_RI_enable();

	/* Don't do any per-CPU operations until interrupt state is fixed */

	if (nmi_disables_ftrace(regs)) {
		state->ftrace_enabled = this_cpu_get_ftrace_enabled();
		this_cpu_set_ftrace_enabled(0);
	}
#endif
}

static inline void arch_interrupt_nmi_exit_prepare(struct pt_regs *regs,
						   struct interrupt_nmi_state *state)
{
	/*
	 * nmi does not call nap_adjust_return because nmi should not create
	 * new work to do (must use irq_work for that).
	 */

#ifdef CONFIG_PPC64
#ifdef CONFIG_PPC_BOOK3S
	if (regs_irqs_disabled(regs)) {
		unsigned long rst = search_kernel_restart_table(regs->nip);

		if (rst)
			regs_set_return_ip(regs, rst);
	}
#endif

	if (nmi_disables_ftrace(regs))
		this_cpu_set_ftrace_enabled(state->ftrace_enabled);

	/* Check we didn't change the pending interrupt mask. */
	WARN_ON_ONCE((state->irq_happened | PACA_IRQ_HARD_DIS) != local_paca->irq_happened);
	regs->softe = state->softe;
	local_paca->irq_happened = state->irq_happened;
	local_paca->irq_soft_mask = state->irq_soft_mask;
#endif
}

static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs)
{
	kuap_lock();

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		BUG_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);

	BUG_ON(regs_is_unrecoverable(regs));
	BUG_ON(!user_mode(regs));
	BUG_ON(regs_irqs_disabled(regs));

#ifdef CONFIG_PPC_PKEY
	if (mmu_has_feature(MMU_FTR_PKEY) && trap_is_syscall(regs)) {
		unsigned long amr, iamr;
		bool flush_needed = false;
		/*
		 * When entering from userspace we mostly have the AMR/IAMR
		 * different from kernel default values. Hence don't compare.
		 */
		amr = mfspr(SPRN_AMR);
		iamr = mfspr(SPRN_IAMR);
		regs->amr  = amr;
		regs->iamr = iamr;
		if (mmu_has_feature(MMU_FTR_KUAP)) {
			mtspr(SPRN_AMR, AMR_KUAP_BLOCKED);
			flush_needed = true;
		}
		if (mmu_has_feature(MMU_FTR_BOOK3S_KUEP)) {
			mtspr(SPRN_IAMR, AMR_KUEP_BLOCKED);
			flush_needed = true;
		}
		if (flush_needed)
			isync();
	}
#endif
	kuap_assert_locked();
	booke_restore_dbcr0();
	account_cpu_user_entry();
	account_stolen_time();

	/*
	 * This is not required for the syscall exit path, but makes the
	 * stack frame look nicer. If this was initialised in the first stack
	 * frame, or if the unwinder was taught the first stack frame always
	 * returns to user with IRQS_ENABLED, this store could be avoided!
	 */
	irq_soft_mask_regs_set_state(regs, IRQS_ENABLED);

	/*
	 * If system call is called with TM active, set _TIF_RESTOREALL to
	 * prevent RFSCV being used to return to userspace, because POWER9
	 * TM implementation has problems with this instruction returning to
	 * transactional state. Final register values are not relevant because
	 * the transaction will be aborted upon return anyway. Or in the case
	 * of unsupported_scv SIGILL fault, the return state does not much
	 * matter because it's an edge case.
	 */
	if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) &&
	    unlikely(MSR_TM_TRANSACTIONAL(regs->msr)))
		set_bits(_TIF_RESTOREALL, &current_thread_info()->flags);

	/*
	 * If the system call was made with a transaction active, doom it and
	 * return without performing the system call. Unless it was an
	 * unsupported scv vector, in which case it's treated like an illegal
	 * instruction.
	 */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (unlikely(MSR_TM_TRANSACTIONAL(regs->msr)) &&
	    !trap_is_unsupported_scv(regs)) {
		/* Enable TM in the kernel, and disable EE (for scv) */
		hard_irq_disable();
		mtmsr(mfmsr() | MSR_TM);

		/* tabort, this dooms the transaction, nothing else */
		asm volatile(".long 0x7c00071d | ((%0) << 16)"
			     :: "r"(TM_CAUSE_SYSCALL | TM_CAUSE_PERSISTENT));

		/*
		 * Userspace will never see the return value. Execution will
		 * resume after the tbegin. of the aborted transaction with the
		 * checkpointed register state. A context switch could occur
		 * or signal delivered to the process before resuming the
		 * doomed transaction context, but that should all be handled
		 * as expected.
		 */
		return;
	}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
}

#define arch_enter_from_user_mode arch_enter_from_user_mode

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	unsigned long mathflags;

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && IS_ENABLED(CONFIG_PPC_FPU)) {
		if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) &&
		    unlikely((ti_work & _TIF_RESTORE_TM))) {
			restore_tm_state(regs);
		} else {
			mathflags = MSR_FP;

			if (cpu_has_feature(CPU_FTR_VSX))
				mathflags |= MSR_VEC | MSR_VSX;
			else if (cpu_has_feature(CPU_FTR_ALTIVEC))
				mathflags |= MSR_VEC;

			/*
			 * If userspace MSR has all available FP bits set,
			 * then they are live and no need to restore. If not,
			 * it means the regs were given up and restore_math
			 * may decide to restore them (to avoid taking an FP
			 * fault).
			 */
			if ((regs->msr & mathflags) != mathflags)
				restore_math(regs);
		}
	}

	check_return_regs_valid(regs);
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif
	/* Restore user access locks last */
	kuap_user_restore(regs);
}

#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

static __always_inline void arch_exit_to_user_mode(void)
{
	booke_load_dbcr0();

	account_cpu_user_exit();
}

#define arch_exit_to_user_mode arch_exit_to_user_mode

#endif /* _ASM_PPC_ENTRY_COMMON_H */
