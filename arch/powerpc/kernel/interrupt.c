// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/context_tracking.h>
#include <linux/err.h>
#include <linux/compat.h>

#include <asm/asm-prototypes.h>
#include <asm/kup.h>
#include <asm/cputime.h>
#include <asm/interrupt.h>
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

	if (!IS_ENABLED(CONFIG_BOOKE) && !IS_ENABLED(CONFIG_40x))
		BUG_ON(!(regs->msr & MSR_RI));
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

/*
 * local irqs must be disabled. Returns false if the caller must re-enable
 * them, check for new work, and try again.
 *
 * This should be called with local irqs disabled, but if they were previously
 * enabled when the interrupt handler returns (indicating a process-context /
 * synchronous interrupt) then irqs_enabled should be true.
 */
static notrace __always_inline bool __prep_irq_for_enabled_exit(bool clear_ri)
{
	/* This must be done with RI=1 because tracing may touch vmaps */
	trace_hardirqs_on();

	/* This pattern matches prep_irq_for_idle */
	if (clear_ri)
		__hard_EE_RI_disable();
	else
		__hard_irq_disable();
#ifdef CONFIG_PPC64
	if (unlikely(lazy_irq_pending_nocheck())) {
		/* Took an interrupt, may have more exit work to do. */
		if (clear_ri)
			__hard_RI_enable();
		trace_hardirqs_off();
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

		return false;
	}
	local_paca->irq_happened = 0;
	irq_soft_mask_set(IRQS_ENABLED);
#endif
	return true;
}

static notrace inline bool prep_irq_for_enabled_exit(bool clear_ri, bool irqs_enabled)
{
	if (__prep_irq_for_enabled_exit(clear_ri))
		return true;

	/*
	 * Must replay pending soft-masked interrupts now. Don't just
	 * local_irq_enabe(); local_irq_disable(); because if we are
	 * returning from an asynchronous interrupt here, another one
	 * might hit after irqs are enabled, and it would exit via this
	 * same path allowing another to fire, and so on unbounded.
	 *
	 * If interrupts were enabled when this interrupt exited,
	 * indicating a process context (synchronous) interrupt,
	 * local_irq_enable/disable can be used, which will enable
	 * interrupts rather than keeping them masked (unclear how
	 * much benefit this is over just replaying for all cases,
	 * because we immediately disable again, so all we're really
	 * doing is allowing hard interrupts to execute directly for
	 * a very small time, rather than being masked and replayed).
	 */
	if (irqs_enabled) {
		local_irq_enable();
		local_irq_disable();
	} else {
		replay_soft_interrupts();
	}

	return false;
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

	if (IS_ENABLED(CONFIG_PPC_BOOK3S) && IS_ENABLED(CONFIG_PPC_FPU)) {
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

	user_enter_irqoff();

	/* scv need not set RI=0 because SRRs are not used */
	if (unlikely(!__prep_irq_for_enabled_exit(is_not_scv))) {
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

notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs, unsigned long msr)
{
	unsigned long ti_flags;
	unsigned long flags;
	unsigned long ret = 0;

	if (!IS_ENABLED(CONFIG_BOOKE) && !IS_ENABLED(CONFIG_40x))
		BUG_ON(!(regs->msr & MSR_RI));
	BUG_ON(!(regs->msr & MSR_PR));
	BUG_ON(arch_irq_disabled_regs(regs));
	CT_WARN_ON(ct_state() == CONTEXT_USER);

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * AMR can only have been unlocked if we interrupted the kernel.
	 */
	kuap_assert_locked();

	local_irq_save(flags);

again:
	ti_flags = READ_ONCE(current_thread_info()->flags);
	while (unlikely(ti_flags & (_TIF_USER_WORK_MASK & ~_TIF_RESTORE_TM))) {
		local_irq_enable(); /* returning to user: may enable */
		if (ti_flags & _TIF_NEED_RESCHED) {
			schedule();
		} else {
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

			/* See above restore_math comment */
			if ((regs->msr & mathflags) != mathflags)
				restore_math(regs);
		}
	}

	user_enter_irqoff();

	if (unlikely(!__prep_irq_for_enabled_exit(true))) {
		user_exit_irqoff();
		local_irq_enable();
		local_irq_disable();
		goto again;
	}

	booke_load_dbcr0();

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	account_cpu_user_exit();

	/* Restore user access locks last */
	kuap_user_restore(regs);
	kuep_unlock();

	return ret;
}

void preempt_schedule_irq(void);

notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs, unsigned long msr)
{
	unsigned long flags;
	unsigned long ret = 0;
	unsigned long kuap;

	if (!IS_ENABLED(CONFIG_BOOKE) && !IS_ENABLED(CONFIG_40x) &&
	    unlikely(!(regs->msr & MSR_RI)))
		unrecoverable_exception(regs);
	BUG_ON(regs->msr & MSR_PR);
	/*
	 * CT_WARN_ON comes here via program_check_exception,
	 * so avoid recursion.
	 */
	if (TRAP(regs) != INTERRUPT_PROGRAM)
		CT_WARN_ON(ct_state() == CONTEXT_USER);

	kuap = kuap_get_and_assert_locked();

	if (unlikely(current_thread_info()->flags & _TIF_EMULATE_STACK_STORE)) {
		clear_bits(_TIF_EMULATE_STACK_STORE, &current_thread_info()->flags);
		ret = 1;
	}

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

		if (unlikely(!prep_irq_for_enabled_exit(true, !irqs_disabled_flags(flags))))
			goto again;
	} else {
		/* Returning to a kernel context with local irqs disabled. */
		__hard_EE_RI_disable();
#ifdef CONFIG_PPC64
		if (regs->msr & MSR_EE)
			local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;
#endif
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
