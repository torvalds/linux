// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/err.h>
#include <asm/asm-prototypes.h>
#include <asm/book3s/64/kup-radix.h>
#include <asm/cputime.h>
#include <asm/hw_irq.h>
#include <asm/kprobes.h>
#include <asm/paca.h>
#include <asm/ptrace.h>
#include <asm/reg.h>
#include <asm/signal.h>
#include <asm/switch_to.h>
#include <asm/syscall.h>
#include <asm/time.h>
#include <asm/unistd.h>

typedef long (*syscall_fn)(long, long, long, long, long, long);

/* Has to run notrace because it is entered not completely "reconciled" */
notrace long system_call_exception(long r3, long r4, long r5,
				   long r6, long r7, long r8,
				   unsigned long r0, struct pt_regs *regs)
{
	syscall_fn f;

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		BUG_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);

	trace_hardirqs_off(); /* finish reconciling */

	if (IS_ENABLED(CONFIG_PPC_BOOK3S))
		BUG_ON(!(regs->msr & MSR_RI));
	BUG_ON(!(regs->msr & MSR_PR));
	BUG_ON(!FULL_REGS(regs));
	BUG_ON(regs->softe != IRQS_ENABLED);

	kuap_check_amr();

	account_cpu_user_entry();

#ifdef CONFIG_PPC_SPLPAR
	if (IS_ENABLED(CONFIG_VIRT_CPU_ACCOUNTING_NATIVE) &&
	    firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct lppaca *lp = local_paca->lppaca_ptr;

		if (unlikely(local_paca->dtl_ridx != be64_to_cpu(lp->dtl_idx)))
			accumulate_stolen_time();
	}
#endif

	/*
	 * This is not required for the syscall exit path, but makes the
	 * stack frame look nicer. If this was initialised in the first stack
	 * frame, or if the unwinder was taught the first stack frame always
	 * returns to user with IRQS_ENABLED, this store could be avoided!
	 */
	regs->softe = IRQS_ENABLED;

	local_irq_enable();

	if (unlikely(current_thread_info()->flags & _TIF_SYSCALL_DOTRACE)) {
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
		return -ENOSYS;
	}

	/* May be faster to do array_index_nospec? */
	barrier_nospec();

	if (unlikely(is_32bit_task())) {
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
 * This should be called after a syscall returns, with r3 the return value
 * from the syscall. If this function returns non-zero, the system call
 * exit assembly should additionally load all GPR registers and CTR and XER
 * from the interrupt frame.
 *
 * The function graph tracer can not trace the return side of this function,
 * because RI=0 and soft mask state is "unreconciled", so it is marked notrace.
 */
notrace unsigned long syscall_exit_prepare(unsigned long r3,
					   struct pt_regs *regs)
{
	unsigned long *ti_flagsp = &current_thread_info()->flags;
	unsigned long ti_flags;
	unsigned long ret = 0;

	kuap_check_amr();

	regs->result = r3;

	/* Check whether the syscall is issued inside a restartable sequence */
	rseq_syscall(regs);

	ti_flags = *ti_flagsp;

	if (unlikely(r3 >= (unsigned long)-MAX_ERRNO)) {
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
		clear_bits(_TIF_PERSYSCALL_MASK, ti_flagsp);
	} else {
		regs->gpr[3] = r3;
	}

	if (unlikely(ti_flags & _TIF_SYSCALL_DOTRACE)) {
		do_syscall_trace_leave(regs);
		ret |= _TIF_RESTOREALL;
	}

again:
	local_irq_disable();
	ti_flags = READ_ONCE(*ti_flagsp);
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
		ti_flags = READ_ONCE(*ti_flagsp);
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

			if ((regs->msr & mathflags) != mathflags)
				restore_math(regs);
		}
	}

	/* This must be done with RI=1 because tracing may touch vmaps */
	trace_hardirqs_on();

	/* This pattern matches prep_irq_for_idle */
	__hard_EE_RI_disable();
	if (unlikely(lazy_irq_pending_nocheck())) {
		__hard_RI_enable();
		trace_hardirqs_off();
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
		local_irq_enable();
		/* Took an interrupt, may have more exit work to do. */
		goto again;
	}
	local_paca->irq_happened = 0;
	irq_soft_mask_set(IRQS_ENABLED);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	account_cpu_user_exit();

	return ret;
}

#ifdef CONFIG_PPC_BOOK3S /* BOOK3E not yet using this */
notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs, unsigned long msr)
{
#ifdef CONFIG_PPC_BOOK3E
	struct thread_struct *ts = &current->thread;
#endif
	unsigned long *ti_flagsp = &current_thread_info()->flags;
	unsigned long ti_flags;
	unsigned long flags;
	unsigned long ret = 0;

	if (IS_ENABLED(CONFIG_PPC_BOOK3S))
		BUG_ON(!(regs->msr & MSR_RI));
	BUG_ON(!(regs->msr & MSR_PR));
	BUG_ON(!FULL_REGS(regs));
	BUG_ON(regs->softe != IRQS_ENABLED);

	kuap_check_amr();

	local_irq_save(flags);

again:
	ti_flags = READ_ONCE(*ti_flagsp);
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
		ti_flags = READ_ONCE(*ti_flagsp);
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

			if ((regs->msr & mathflags) != mathflags)
				restore_math(regs);
		}
	}

	trace_hardirqs_on();
	__hard_EE_RI_disable();
	if (unlikely(lazy_irq_pending_nocheck())) {
		__hard_RI_enable();
		trace_hardirqs_off();
		local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
		local_irq_enable();
		local_irq_disable();
		/* Took an interrupt, may have more exit work to do. */
		goto again;
	}
	local_paca->irq_happened = 0;
	irq_soft_mask_set(IRQS_ENABLED);

#ifdef CONFIG_PPC_BOOK3E
	if (unlikely(ts->debug.dbcr0 & DBCR0_IDM)) {
		/*
		 * Check to see if the dbcr0 register is set up to debug.
		 * Use the internal debug mode bit to do this.
		 */
		mtmsr(mfmsr() & ~MSR_DE);
		mtspr(SPRN_DBCR0, ts->debug.dbcr0);
		mtspr(SPRN_DBSR, -1);
	}
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	account_cpu_user_exit();

	return ret;
}

void unrecoverable_exception(struct pt_regs *regs);
void preempt_schedule_irq(void);

notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs, unsigned long msr)
{
	unsigned long *ti_flagsp = &current_thread_info()->flags;
	unsigned long flags;
	unsigned long ret = 0;

	if (IS_ENABLED(CONFIG_PPC_BOOK3S) && unlikely(!(regs->msr & MSR_RI)))
		unrecoverable_exception(regs);
	BUG_ON(regs->msr & MSR_PR);
	BUG_ON(!FULL_REGS(regs));

	kuap_check_amr();

	if (unlikely(*ti_flagsp & _TIF_EMULATE_STACK_STORE)) {
		clear_bits(_TIF_EMULATE_STACK_STORE, ti_flagsp);
		ret = 1;
	}

	local_irq_save(flags);

	if (regs->softe == IRQS_ENABLED) {
		/* Returning to a kernel context with local irqs enabled. */
		WARN_ON_ONCE(!(regs->msr & MSR_EE));
again:
		if (IS_ENABLED(CONFIG_PREEMPT)) {
			/* Return to preemptible kernel context */
			if (unlikely(*ti_flagsp & _TIF_NEED_RESCHED)) {
				if (preempt_count() == 0)
					preempt_schedule_irq();
			}
		}

		trace_hardirqs_on();
		__hard_EE_RI_disable();
		if (unlikely(lazy_irq_pending_nocheck())) {
			__hard_RI_enable();
			irq_soft_mask_set(IRQS_ALL_DISABLED);
			trace_hardirqs_off();
			local_paca->irq_happened |= PACA_IRQ_HARD_DIS;
			/*
			 * Can't local_irq_restore to replay if we were in
			 * interrupt context. Must replay directly.
			 */
			if (irqs_disabled_flags(flags)) {
				replay_soft_interrupts();
			} else {
				local_irq_restore(flags);
				local_irq_save(flags);
			}
			/* Took an interrupt, may have more exit work to do. */
			goto again;
		}
		local_paca->irq_happened = 0;
		irq_soft_mask_set(IRQS_ENABLED);
	} else {
		/* Returning to a kernel context with local irqs disabled. */
		__hard_EE_RI_disable();
		if (regs->msr & MSR_EE)
			local_paca->irq_happened &= ~PACA_IRQ_HARD_DIS;
	}


#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	local_paca->tm_scratch = regs->msr;
#endif

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * The value of AMR only matters while we're in the kernel.
	 */
	kuap_restore_amr(regs);

	return ret;
}
#endif
