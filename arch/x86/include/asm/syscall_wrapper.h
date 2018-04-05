/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - x86 specific wrappers to syscall definitions
 */

#ifndef _ASM_X86_SYSCALL_WRAPPER_H
#define _ASM_X86_SYSCALL_WRAPPER_H

/*
 * Instead of the generic __SYSCALL_DEFINEx() definition, this macro takes
 * struct pt_regs *regs as the only argument of the syscall stub named
 * sys_*(). It decodes just the registers it needs and passes them on to
 * the SyS_*() wrapper and then to the SYSC_*() function doing the actual job.
 * These wrappers and functions are inlined, meaning that the assembly looks
 * as follows (slightly re-ordered):
 *
 * <sys_recv>:			<-- syscall with 4 parameters
 *	callq	<__fentry__>
 *
 *	mov	0x70(%rdi),%rdi	<-- decode regs->di
 *	mov	0x68(%rdi),%rsi	<-- decode regs->si
 *	mov	0x60(%rdi),%rdx	<-- decode regs->dx
 *	mov	0x38(%rdi),%rcx	<-- decode regs->r10
 *
 *	xor	%r9d,%r9d	<-- clear %r9
 *	xor	%r8d,%r8d	<-- clear %r8
 *
 *	callq	__sys_recvfrom	<-- do the actual work in __sys_recvfrom()
 *				    which takes 6 arguments
 *
 *	cltq			<-- extend return value to 64-bit
 *	retq			<-- return
 *
 * This approach avoids leaking random user-provided register content down
 * the call chain.
 *
 * As the generic SYSCALL_DEFINE0() macro does not decode any parameters for
 * obvious reasons, and passing struct pt_regs *regs to it in %rdi does not
 * hurt, there is no need to override it.
 */
#define __SYSCALL_DEFINEx(x, name, ...)					\
	asmlinkage long sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static long SyS##name(__MAP(x,__SC_LONG,__VA_ARGS__));		\
	static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	asmlinkage long sys##name(const struct pt_regs *regs)		\
	{								\
		return SyS##name(__MAP(x,__SC_ARGS			\
			,,regs->di,,regs->si,,regs->dx			\
			,,regs->r10,,regs->r8,,regs->r9));		\
	}								\
	static long SyS##name(__MAP(x,__SC_LONG,__VA_ARGS__))		\
	{								\
		long ret = SYSC##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);				\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));	\
		return ret;						\
	}								\
	static inline long SYSC##name(__MAP(x,__SC_DECL,__VA_ARGS__))

/*
 * For VSYSCALLS, we need to declare these three syscalls with the new
 * pt_regs-based calling convention for in-kernel use.
 */
struct pt_regs;
asmlinkage long sys_getcpu(const struct pt_regs *regs);		/* di,si,dx */
asmlinkage long sys_gettimeofday(const struct pt_regs *regs);	/* di,si */
asmlinkage long sys_time(const struct pt_regs *regs);		/* di */

#endif /* _ASM_X86_SYSCALL_WRAPPER_H */
