// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/compat.h>
#include <linux/context_tracking.h>
#include <linux/randomize_kstack.h>

#include <asm/interrupt.h>
#include <asm/kup.h>
#include <asm/syscall.h>
#include <asm/time.h>
#include <asm/tm.h>
#include <asm/unistd.h>


/* Has to run notrace because it is entered not completely "reconciled" */
notrace long system_call_exception(struct pt_regs *regs, unsigned long r0)
{
	long ret;
	syscall_fn f;

	kuap_lock();

	add_random_kstack_offset();

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
				:: "r"(TM_CAUSE_SYSCALL|TM_CAUSE_PERSISTENT));

		/*
		 * Userspace will never see the return value. Execution will
		 * resume after the tbegin. of the aborted transaction with the
		 * checkpointed register state. A context switch could occur
		 * or signal delivered to the process before resuming the
		 * doomed transaction context, but that should all be handled
		 * as expected.
		 */
		return -ENOSYS;
	}
#endif // CONFIG_PPC_TRANSACTIONAL_MEM

	local_irq_enable();

	if (unlikely(read_thread_flags() & _TIF_SYSCALL_DOTRACE)) {
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

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
	// No COMPAT if we have SYSCALL_WRAPPER, see Kconfig
	f = (void *)sys_call_table[r0];
	ret = f(regs);
#else
	if (unlikely(is_compat_task())) {
		unsigned long r3, r4, r5, r6, r7, r8;

		f = (void *)compat_sys_call_table[r0];

		r3 = regs->gpr[3] & 0x00000000ffffffffULL;
		r4 = regs->gpr[4] & 0x00000000ffffffffULL;
		r5 = regs->gpr[5] & 0x00000000ffffffffULL;
		r6 = regs->gpr[6] & 0x00000000ffffffffULL;
		r7 = regs->gpr[7] & 0x00000000ffffffffULL;
		r8 = regs->gpr[8] & 0x00000000ffffffffULL;

		ret = f(r3, r4, r5, r6, r7, r8);
	} else {
		f = (void *)sys_call_table[r0];

		ret = f(regs->gpr[3], regs->gpr[4], regs->gpr[5],
			regs->gpr[6], regs->gpr[7], regs->gpr[8]);
	}
#endif

	/*
	 * Ultimately, this value will get limited by KSTACK_OFFSET_MAX(),
	 * so the maximum stack offset is 1k bytes (10 bits).
	 *
	 * The actual entropy will be further reduced by the compiler when
	 * applying stack alignment constraints: the powerpc architecture
	 * may have two kinds of stack alignment (16-bytes and 8-bytes).
	 *
	 * So the resulting 6 or 7 bits of entropy is seen in SP[9:4] or SP[9:3].
	 */
	choose_random_kstack_offset(mftb());

	return ret;
}
