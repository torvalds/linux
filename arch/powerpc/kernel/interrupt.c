// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/context_tracking.h>
#include <linux/err.h>
#include <linux/compat.h>
#include <linux/sched/debug.h> /* for show_regs */

#include <asm/asm-prototypes.h>
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
#include <asm/unistd.h>

#if defined(CONFIG_PPC_ADV_DEBUG_REGS) && defined(CONFIG_PPC32)
unsigned long global_dbcr0[NR_CPUS];
#endif

typedef long (*syscall_fn)(long, long, long, long, long, long);

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
	/* This must be done with RI=1 because tracing may touch vmaps */
	trace_hardirqs_on();

	if (exit_must_hard_disable() || !restartable)
		__hard_EE_RI_disable();

#ifdef CONFIG_PPC64
	/* This pattern matches prep_irq_for_idle */
	if (unlikely(lazy_irq_pending_nocheck())) {
		if (exit_must_hard_disable() || !restartable) {
			local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
			__hard_RI_enable();
		}
		trace_hardirqs_off();

		return false;
	}
#endif
	return true;
}

/* Has to run notrace because it is entered not completely "reconciled" */
notrace long system_call_exception(long r3, long r4, long r5,
				   long r6, long r7, long r8,
				   unsigned long r0, struct pt_regs *regs)
{
	syscall_fn f;

	kuep_lock();

	regs->orig_gpr3 = r3;

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		BUG_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);

	trace_hardirqs_off(); /* finish reconciling */

	CT_WARN_ON(ct_state() == CONTEXT_KERNEL);
	user_exit_irqoff();

	BUG_ON(regs_is_unrecoverable(regs));
	BUG_ON(!(regs->msr & MSR_PR));
	BUG_ON(arch_irq_disabled_regs(regs));

#ifdef CONFIG_PPC_PKEY
	if (mmu_has_feature(MMU_FTR_PKEY)) {
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
		if (mmu_has_feature(MMU_FTR_BOOK3S_KUAP)) {
			mtspr(SPRN_AMR, AMR_KUAP_BLOCKED);
			flush_needed = true;
		}
		if (mmu_has_feature(MMU_FTR_BOOK3S_KUEP)) {
			mtspr(SPRN_IAMR, AMR_KUEP_BLOCKED);
			flush_needed = true;
		}
		if (flush_needed)
			isync();
	} else
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

	local_irq_enable();

	if (unlikely(current_thread_info()->flags & _TIF_SYSCALL_DOTRACE)) {
		if (unlikely(trap_is_unsupported_scv(regs))) {
			/* Unsupported scv vector */
			_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
			return regs->gpr[3];
		}
		/*
		 * We use the return value of do_syscall_trace_enter() as the
		 * syscall number. If the syscall was rejected for any reason
		 * do_syscall_trace_enter() returns an invalid syscall number
		 * and the test against NR_syscalls will fail and the return
		 * value to be used is in regs->gpr[3].
		 */
		r0 = do_syscall_trace_enter(regs);
		if (unlikely(r0 >= NR_syscalls))
			return regs->gpr[3];
		r3 = regs->gpr[3];
		r4 = regs->gpr[4];
		r5 = regs->gpr[5];
		r6 = regs->gpr[6];
		r7 = regs->gpr[7];
		r8 = regs->gpr[8];

	} else if (unlikely(r0 >= NR_syscalls)) {
		if (unlikely(trap_is_unsupported_scv(regs))) {
			/* Unsupported scv vector */
			_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
			return regs->gpr[3];
		}
		return -ENOSYS;
	}

	/* May be faster to do array_index_nospec? */
	barrier_nospec();

	if (unlikely(is_compat_task())) {
		f = (void *)compat_sys_call_table[r0];

		r3 &= 0x00000000ffffffffULL;
		r4 &= 0x00000000ffffffffULL;
		r5 &= 0x00000000ffffffffULL;
		r6 &= 0x00000000ffffffffULL;
		r7 &= 0x00000000ffffffffULL;
		r8 &= 0x00000000ffffffffULL;

	} else {
		f = (void *)sys_call_table[r0];
	}

	return f(r3, r4, r5, r6, r7, r8);
}

static notrace void booke_load_dbcr0(void)
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

static void check_return_regs_valid(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_BOOK3S_64
	unsigned long trap, srr0, srr1;
	static bool warned;
	u8 *validp;
	char *h;

	if (trap_is_scv(regs))
		return;

	trap = regs->trap;
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
		if (!*validp)
			return;

		srr0 = mfspr(SPRN_HSRR0);
		srr1 = mfspr(SPRN_HSRR1);
		h = "H";

		break;
	default:
		validp = &local_paca->srr_valid;
		if (!*validp)
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
	barrier();

	if (!*validp)
		return;

	if (!warned) {
		warned = true;
		printk("%sSRR0 was: %lx should be: %lx\n", h, srr0, regs->nip);
		printk("%sSRR1 was: %lx should be: %lx\n", h, srr1, regs->msr);
		show_regs(regs);
	}

	*validp = 0; /* fixup */
#endif
}

static notrace unsigned long
interrupt_exit_user_prepare_main(unsigned long ret, struct pt_regs *regs)
{
	unsigned long ti_flags;

again:
	ti_flags = READ_ONCE(current_thread_info()->flags);
	while (unlikely(ti_flags & (_TIF_USER_WORK_MASK & ~_TIF_RESTORE_TM))) {
		local_irq_enable();
		if (ti_flags & _TIF_NEED_RESCHED) {
			schedule();
		} else {
			/*
			 * SIGPENDING must restore signal handler function
			 * argument GPRs, and some non-volatiles (e.g., r1).
			 * Restore all for now. This could be made lighter.
			 */
			if (ti_flags & _TIF_SIGPENDING)
				ret |= _TIF_RESTOREALL;
			do_notify_resume(regs, ti_flags);
		}
		local_irq_disable();
		ti_flags = READ_ONCE(current_thread_info()->flags);
	}

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && IS_ENABLED(CONFIG_PPC_FPU)) {
		if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) &&
				unlikely((ti_flags & _TIF_RESTORE_TM))) {
			restore_tm_state(regs);
		} else {
			unsigned long mathflags = MSR_FP;

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

	user_enter_irqoff();
	if (!prep_irq_for_enabled_exit(true)) {
		user_exit_irqoff();
		local_irq_enable();
		local_irq_disable();
		goto again;
	}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	booke_load_dbcr0();

	account_cpu_user_exit();

	/* Restore user access locks last */
	kuap_user_restore(regs);
	kuep_unlock();

	return ret;
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
	unsigned long ret = 0;
	bool is_not_scv = !IS_ENABLED(CONFIG_PPC_BOOK3S_64) || !scv;

	CT_WARN_ON(ct_state() == CONTEXT_USER);

	kuap_assert_locked();

	regs->result = r3;

	/* Check whether the syscall is issued inside a restartable sequence */
	rseq_syscall(regs);

	ti_flags = current_thread_info()->flags;

	if (unlikely(r3 >= (unsigned long)-MAX_ERRNO) && is_not_scv) {
		if (likely(!(ti_flags & (_TIF_NOERROR | _TIF_RESTOREALL)))) {
			r3 = -r3;
			regs->ccr |= 0x10000000; /* Set SO bit in CR */
		}
	}

	if (unlikely(ti_flags & _TIF_PERSYSCALL_MASK)) {
		if (ti_flags & _TIF_RESTOREALL)
			ret = _TIF_RESTOREALL;
		else
			regs->gpr[3] = r3;
		clear_bits(_TIF_PERSYSCALL_MASK, &current_thread_info()->flags);
	} else {
		regs->gpr[3] = r3;
	}

	if (unlikely(ti_flags & _TIF_SYSCALL_DOTRACE)) {
		do_syscall_trace_leave(regs);
		ret |= _TIF_RESTOREALL;
	}

	local_irq_disable();
	ret = interrupt_exit_user_prepare_main(ret, regs);

#ifdef CONFIG_PPC64
	regs->exit_result = ret;
#endif

	return ret;
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

	trace_hardirqs_off();
	user_exit_irqoff();
	account_cpu_user_entry();

	BUG_ON(!user_mode(regs));

	regs->exit_result = interrupt_exit_user_prepare_main(regs->exit_result, regs);

	return regs->exit_result;
}
#endif

notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs)
{
	unsigned long ret;

	BUG_ON(regs_is_unrecoverable(regs));
	BUG_ON(arch_irq_disabled_regs(regs));
	CT_WARN_ON(ct_state() == CONTEXT_USER);

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * AMR can only have been unlocked if we interrupted the kernel.
	 */
	kuap_assert_locked();

	local_irq_disable();

	ret = interrupt_exit_user_prepare_main(0, regs);

#ifdef CONFIG_PPC64
	regs->exit_result = ret;
#endif

	return ret;
}

void preempt_schedule_irq(void);

notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs)
{
	unsigned long flags;
	unsigned long ret = 0;
	unsigned long kuap;
	bool stack_store = current_thread_info()->flags &
						_TIF_EMULATE_STACK_STORE;

	if (regs_is_unrecoverable(regs))
		unrecoverable_exception(regs);
	/*
	 * CT_WARN_ON comes here via program_check_exception,
	 * so avoid recursion.
	 */
	if (TRAP(regs) != INTERRUPT_PROGRAM)
		CT_WARN_ON(ct_state() == CONTEXT_USER);

	kuap = kuap_get_and_assert_locked();

	local_irq_save(flags);

	if (!arch_irq_disabled_regs(regs)) {
		/* Returning to a kernel context with local irqs enabled. */
		WARN_ON_ONCE(!(regs->msr & MSR_EE));
again:
		if (IS_ENABLED(CONFIG_PREEMPT)) {
			/* Return to preemptible kernel context */
			if (unlikely(current_thread_info()->flags & _TIF_NEED_RESCHED)) {
				if (preempt_count() == 0)
					preempt_schedule_irq();
			}
		}

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
		/*
		 * Returning to a kernel context with local irqs disabled.
		 * Here, if EE was enabled in the interrupted context, enable
		 * it on return as well. A problem exists here where a soft
		 * masked interrupt may have cleared MSR[EE] and set HARD_DIS
		 * here, and it will still exist on return to the caller. This
		 * will be resolved by the masked interrupt firing again.
		 */
		if (regs->msr & MSR_EE)
			local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;
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
	user_exit_irqoff();
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
