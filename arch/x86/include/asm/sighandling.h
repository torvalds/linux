/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SIGHANDLING_H
#define _ASM_X86_SIGHANDLING_H

#include <linux/compiler.h>
#include <linux/ptrace.h>
#include <linux/signal.h>

#include <asm/processor-flags.h>

#define FIX_EFLAGS	(X86_EFLAGS_AC | X86_EFLAGS_OF | \
			 X86_EFLAGS_DF | X86_EFLAGS_TF | X86_EFLAGS_SF | \
			 X86_EFLAGS_ZF | X86_EFLAGS_AF | X86_EFLAGS_PF | \
			 X86_EFLAGS_CF | X86_EFLAGS_RF)

void signal_fault(struct pt_regs *regs, void __user *frame, char *where);

void __user *
get_sigframe(struct ksignal *ksig, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate);

int ia32_setup_frame(struct ksignal *ksig, struct pt_regs *regs);
int ia32_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs);
int x64_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs);
int x32_setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs);

/*
 * To prevent immediate repeat of single step trap on return from SIGTRAP
 * handler if the trap flag (TF) is set without an external debugger attached,
 * clear the software event flag in the augmented SS, ensuring no single-step
 * trap is pending upon ERETU completion.
 *
 * Note, this function should be called in sigreturn() before the original
 * state is restored to make sure the TF is read from the entry frame.
 */
static __always_inline void prevent_single_step_upon_eretu(struct pt_regs *regs)
{
	/*
	 * If the trap flag (TF) is set, i.e., the sigreturn() SYSCALL instruction
	 * is being single-stepped, do not clear the software event flag in the
	 * augmented SS, thus a debugger won't skip over the following instruction.
	 */
#ifdef CONFIG_X86_FRED
	if (!(regs->flags & X86_EFLAGS_TF))
		regs->fred_ss.swevent = 0;
#endif
}

#endif /* _ASM_X86_SIGHANDLING_H */
