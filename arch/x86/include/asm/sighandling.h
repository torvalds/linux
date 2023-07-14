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

#endif /* _ASM_X86_SIGHANDLING_H */
