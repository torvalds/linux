/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UNWIND_USER_H
#define _ASM_X86_UNWIND_USER_H

#include <asm/ptrace.h>

#define ARCH_INIT_USER_FP_FRAME(ws)			\
	.cfa_off	=  2*(ws),			\
	.ra_off		= -1*(ws),			\
	.fp_off		= -2*(ws),			\
	.use_fp		= true,

static inline int unwind_user_word_size(struct pt_regs *regs)
{
	/* We can't unwind VM86 stacks */
	if (regs->flags & X86_VM_MASK)
		return 0;
#ifdef CONFIG_X86_64
	if (!user_64bit_mode(regs))
		return sizeof(int);
#endif
	return sizeof(long);
}

#endif /* _ASM_X86_UNWIND_USER_H */
