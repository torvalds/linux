/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UNWIND_USER_H
#define _ASM_X86_UNWIND_USER_H

#ifdef CONFIG_HAVE_UNWIND_USER_FP

#include <asm/ptrace.h>
#include <asm/uprobes.h>

#define ARCH_INIT_USER_FP_FRAME(ws)			\
	.cfa_off	=  2*(ws),			\
	.ra_off		= -1*(ws),			\
	.fp_off		= -2*(ws),			\
	.use_fp		= true,

#define ARCH_INIT_USER_FP_ENTRY_FRAME(ws)		\
	.cfa_off	=  1*(ws),			\
	.ra_off		= -1*(ws),			\
	.fp_off		= 0,				\
	.use_fp		= false,

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

static inline bool unwind_user_at_function_start(struct pt_regs *regs)
{
	return is_uprobe_at_func_entry(regs);
}

#endif /* CONFIG_HAVE_UNWIND_USER_FP */

#endif /* _ASM_X86_UNWIND_USER_H */
