/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_LEGACY_H
#define __X86_KERNEL_FPU_LEGACY_H

#include <asm/fpu/types.h>

/*
 * Returns 0 on success or the trap number when the operation raises an
 * exception.
 */
#define user_insn(insn, output, input...)				\
({									\
	int err;							\
									\
	might_fault();							\
									\
	asm volatile(ASM_STAC "\n"					\
		     "1: " #insn "\n"					\
		     "2: " ASM_CLAC "\n"				\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_MCE_SAFE)	\
		     : [err] "=a" (err), output				\
		     : "0"(0), input);					\
	err;								\
})

#define kernel_insn_err(insn, output, input...)				\
({									\
	int err;							\
	asm volatile("1:" #insn "\n\t"					\
		     "2:\n"						\
		     ".section .fixup,\"ax\"\n"				\
		     "3:  movl $-1,%[err]\n"				\
		     "    jmp  2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE(1b, 3b)				\
		     : [err] "=r" (err), output				\
		     : "0"(0), input);					\
	err;								\
})

#define kernel_insn(insn, output, input...)				\
	asm volatile("1:" #insn "\n\t"					\
		     "2:\n"						\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FPU_RESTORE)	\
		     : output : input)

static inline int fnsave_to_user_sigframe(struct fregs_state __user *fx)
{
	return user_insn(fnsave %[fx]; fwait,  [fx] "=m" (*fx), "m" (*fx));
}

static inline int fxsave_to_user_sigframe(struct fxregs_state __user *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return user_insn(fxsave %[fx], [fx] "=m" (*fx), "m" (*fx));
	else
		return user_insn(fxsaveq %[fx], [fx] "=m" (*fx), "m" (*fx));

}

static inline void fxrstor(struct fxregs_state *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		kernel_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else
		kernel_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline int fxrstor_safe(struct fxregs_state *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return kernel_insn_err(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else
		return kernel_insn_err(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline int fxrstor_from_user_sigframe(struct fxregs_state __user *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return user_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else
		return user_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void frstor(struct fregs_state *fx)
{
	kernel_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline int frstor_safe(struct fregs_state *fx)
{
	return kernel_insn_err(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline int frstor_from_user_sigframe(struct fregs_state __user *fx)
{
	return user_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void fxsave(struct fxregs_state *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		asm volatile( "fxsave %[fx]" : [fx] "=m" (*fx));
	else
		asm volatile("fxsaveq %[fx]" : [fx] "=m" (*fx));
}

#endif
