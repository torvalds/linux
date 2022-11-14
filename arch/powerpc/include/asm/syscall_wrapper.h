/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - powerpc specific wrappers to syscall definitions
 *
 * Based on arch/{x86,arm64}/include/asm/syscall_wrapper.h
 */

#ifndef __ASM_POWERPC_SYSCALL_WRAPPER_H
#define __ASM_POWERPC_SYSCALL_WRAPPER_H

struct pt_regs;

#define SC_POWERPC_REGS_TO_ARGS(x, ...)				\
	__MAP(x,__SC_ARGS					\
	      ,,regs->gpr[3],,regs->gpr[4],,regs->gpr[5]	\
	      ,,regs->gpr[6],,regs->gpr[7],,regs->gpr[8])

#define __SYSCALL_DEFINEx(x, name, ...)						\
	long sys##name(const struct pt_regs *regs);			\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));		\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	long sys##name(const struct pt_regs *regs)			\
	{									\
		return __se_sys##name(SC_POWERPC_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}									\
	static long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))		\
	{									\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));		\
		return ret;							\
	}									\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#define SYSCALL_DEFINE0(sname)							\
	SYSCALL_METADATA(_##sname, 0);						\
	long sys_##sname(const struct pt_regs *__unused);		\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);			\
	long sys_##sname(const struct pt_regs *__unused)

#define COND_SYSCALL(name)							\
	long sys_##name(const struct pt_regs *regs);			\
	long __weak sys_##name(const struct pt_regs *regs)		\
	{									\
		return sys_ni_syscall();					\
	}

#endif // __ASM_POWERPC_SYSCALL_WRAPPER_H
