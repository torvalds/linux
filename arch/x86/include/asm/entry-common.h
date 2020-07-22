/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_ENTRY_COMMON_H
#define _ASM_X86_ENTRY_COMMON_H

/* Check that the stack and regs on entry from user mode are sane. */
static __always_inline void arch_check_user_regs(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_DEBUG_ENTRY)) {
		/*
		 * Make sure that the entry code gave us a sensible EFLAGS
		 * register.  Native because we want to check the actual CPU
		 * state, not the interrupt state as imagined by Xen.
		 */
		unsigned long flags = native_save_fl();
		WARN_ON_ONCE(flags & (X86_EFLAGS_AC | X86_EFLAGS_DF |
				      X86_EFLAGS_NT));

		/* We think we came from user mode. Make sure pt_regs agrees. */
		WARN_ON_ONCE(!user_mode(regs));

		/*
		 * All entries from user mode (except #DF) should be on the
		 * normal thread stack and should have user pt_regs in the
		 * correct location.
		 */
		WARN_ON_ONCE(!on_thread_stack());
		WARN_ON_ONCE(regs != task_pt_regs(current));
	}
}
#define arch_check_user_regs arch_check_user_regs

#endif
