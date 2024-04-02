/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - riscv specific wrappers to syscall definitions
 *
 * Based on arch/arm64/include/syscall_wrapper.h
 */

#ifndef __ASM_SYSCALL_WRAPPER_H
#define __ASM_SYSCALL_WRAPPER_H

#include <asm/ptrace.h>

asmlinkage long __riscv_sys_ni_syscall(const struct pt_regs *);

#ifdef CONFIG_64BIT

#define __SYSCALL_SE_DEFINEx(x, prefix, name, ...)					\
	static long __se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__));		\
	static long __se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__))

#define SC_RISCV_REGS_TO_ARGS(x, ...)							\
	__MAP(x,__SC_ARGS								\
	      ,,regs->orig_a0,,regs->a1,,regs->a2					\
	      ,,regs->a3,,regs->a4,,regs->a5,,regs->a6)

#else
/*
 * Use type aliasing to ensure registers a0-a6 are correctly passed to the syscall
 * implementation when >word-size arguments are used.
 */
#define __SYSCALL_SE_DEFINEx(x, prefix, name, ...)					\
	__diag_push();									\
	__diag_ignore(GCC, 8, "-Wattribute-alias",					\
			"Type aliasing is used to sanitize syscall arguments");		\
	static long __se_##prefix##name(ulong, ulong, ulong, ulong, ulong, ulong, 	\
					ulong)						\
			__attribute__((alias(__stringify(___se_##prefix##name))));	\
	__diag_pop();									\
	static long noinline ___se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	static long ___se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__))

#define SC_RISCV_REGS_TO_ARGS(x, ...) \
	regs->orig_a0,regs->a1,regs->a2,regs->a3,regs->a4,regs->a5,regs->a6

#endif /* CONFIG_64BIT */

#ifdef CONFIG_COMPAT

#define COMPAT_SYSCALL_DEFINEx(x, name, ...)						\
	asmlinkage long __riscv_compat_sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(__riscv_compat_sys##name, ERRNO);				\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	__SYSCALL_SE_DEFINEx(x, compat_sys, name, __VA_ARGS__)				\
	{										\
		return __do_compat_sys##name(__MAP(x,__SC_DELOUSE,__VA_ARGS__));	\
	}										\
	asmlinkage long __riscv_compat_sys##name(const struct pt_regs *regs)		\
	{										\
		return __se_compat_sys##name(SC_RISCV_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}										\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#define COMPAT_SYSCALL_DEFINE0(sname)							\
	asmlinkage long __riscv_compat_sys_##sname(const struct pt_regs *__unused);	\
	ALLOW_ERROR_INJECTION(__riscv_compat_sys_##sname, ERRNO);			\
	asmlinkage long __riscv_compat_sys_##sname(const struct pt_regs *__unused)

#define COND_SYSCALL_COMPAT(name) 							\
	asmlinkage long __weak __riscv_compat_sys_##name(const struct pt_regs *regs);	\
	asmlinkage long __weak __riscv_compat_sys_##name(const struct pt_regs *regs)	\
	{										\
		return sys_ni_syscall();						\
	}

#endif /* CONFIG_COMPAT */

#define __SYSCALL_DEFINEx(x, name, ...)						\
	asmlinkage long __riscv_sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(__riscv_sys##name, ERRNO);			\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	__SYSCALL_SE_DEFINEx(x, sys, name, __VA_ARGS__)				\
	{									\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));		\
		return ret;							\
	}									\
	asmlinkage long __riscv_sys##name(const struct pt_regs *regs)		\
	{									\
		return __se_sys##name(SC_RISCV_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}									\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#define SYSCALL_DEFINE0(sname)							\
	SYSCALL_METADATA(_##sname, 0);						\
	asmlinkage long __riscv_sys_##sname(const struct pt_regs *__unused);	\
	ALLOW_ERROR_INJECTION(__riscv_sys_##sname, ERRNO);			\
	asmlinkage long __riscv_sys_##sname(const struct pt_regs *__unused)

#define COND_SYSCALL(name)							\
	asmlinkage long __weak __riscv_sys_##name(const struct pt_regs *regs);	\
	asmlinkage long __weak __riscv_sys_##name(const struct pt_regs *regs)	\
	{									\
		return sys_ni_syscall();					\
	}

#endif /* __ASM_SYSCALL_WRAPPER_H */
