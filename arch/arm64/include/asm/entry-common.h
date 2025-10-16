/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_ARM64_ENTRY_COMMON_H
#define _ASM_ARM64_ENTRY_COMMON_H

#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/fpsimd.h>
#include <asm/mte.h>
#include <asm/stacktrace.h>

#define ARCH_EXIT_TO_USER_MODE_WORK (_TIF_MTE_ASYNC_FAULT | _TIF_FOREIGN_FPSTATE)

static __always_inline void arch_exit_to_user_mode_work(struct pt_regs *regs,
							unsigned long ti_work)
{
	if (ti_work & _TIF_MTE_ASYNC_FAULT) {
		clear_thread_flag(TIF_MTE_ASYNC_FAULT);
		send_sig_fault(SIGSEGV, SEGV_MTEAERR, (void __user *)NULL, current);
	}

	if (ti_work & _TIF_FOREIGN_FPSTATE)
		fpsimd_restore_current_state();
}

#define arch_exit_to_user_mode_work arch_exit_to_user_mode_work

static inline bool arch_irqentry_exit_need_resched(void)
{
	/*
	 * DAIF.DA are cleared at the start of IRQ/FIQ handling, and when GIC
	 * priority masking is used the GIC irqchip driver will clear DAIF.IF
	 * using gic_arch_enable_irqs() for normal IRQs. If anything is set in
	 * DAIF we must have handled an NMI, so skip preemption.
	 */
	if (system_uses_irq_prio_masking() && read_sysreg(daif))
		return false;

	/*
	 * Preempting a task from an IRQ means we leave copies of PSTATE
	 * on the stack. cpufeature's enable calls may modify PSTATE, but
	 * resuming one of these preempted tasks would undo those changes.
	 *
	 * Only allow a task to be preempted once cpufeatures have been
	 * enabled.
	 */
	if (!system_capabilities_finalized())
		return false;

	return true;
}

#define arch_irqentry_exit_need_resched arch_irqentry_exit_need_resched

#endif /* _ASM_ARM64_ENTRY_COMMON_H */
