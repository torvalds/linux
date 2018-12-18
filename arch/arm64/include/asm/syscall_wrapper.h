/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - arm64 specific wrappers to syscall definitions
 *
 * Based on arch/x86/include/asm_syscall_wrapper.h
 */

#ifndef __ASM_SYSCALL_WRAPPER_H
#define __ASM_SYSCALL_WRAPPER_H

#define SC_ARM64_REGS_TO_ARGS(x, ...)				\
	__MAP(x,__SC_ARGS					\
	      ,,regs->regs[0],,regs->regs[1],,regs->regs[2]	\
	      ,,regs->regs[3],,regs->regs[4],,regs->regs[5])

#ifdef CONFIG_COMPAT

#define COMPAT_SYSCALL_DEFINEx(x, name, ...)						\
	asmlinkage long __arm64_compat_sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(__arm64_compat_sys##name, ERRNO);				\
	static long __se_compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));		\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	asmlinkage long __arm64_compat_sys##name(const struct pt_regs *regs)		\
	{										\
		return __se_compat_sys##name(SC_ARM64_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}										\
	static long __se_compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))		\
	{										\
		return __do_compat_sys##name(__MAP(x,__SC_DELOUSE,__VA_ARGS__));	\
	}										\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#define COMPAT_SYSCALL_DEFINE0(sname)					\
	asmlinkage long __arm64_compat_sys_##sname(void);		\
	ALLOW_ERROR_INJECTION(__arm64_compat_sys_##sname, ERRNO);	\
	asmlinkage long __arm64_compat_sys_##sname(void)

#define COND_SYSCALL_COMPAT(name) \
	cond_syscall(__arm64_compat_sys_##name);

#define COMPAT_SYS_NI(name) \
	SYSCALL_ALIAS(__arm64_compat_sys_##name, sys_ni_posix_timers);

#endif /* CONFIG_COMPAT */

#define __SYSCALL_DEFINEx(x, name, ...)						\
	asmlinkage long __arm64_sys##name(const struct pt_regs *regs);		\
	ALLOW_ERROR_INJECTION(__arm64_sys##name, ERRNO);			\
	static long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));		\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	asmlinkage long __arm64_sys##name(const struct pt_regs *regs)		\
	{									\
		return __se_sys##name(SC_ARM64_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}									\
	static long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))		\
	{									\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));		\
		return ret;							\
	}									\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#ifndef SYSCALL_DEFINE0
#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);				\
	asmlinkage long __arm64_sys_##sname(void);		\
	ALLOW_ERROR_INJECTION(__arm64_sys_##sname, ERRNO);	\
	asmlinkage long __arm64_sys_##sname(void)
#endif

#ifndef COND_SYSCALL
#define COND_SYSCALL(name) cond_syscall(__arm64_sys_##name)
#endif

#ifndef SYS_NI
#define SYS_NI(name) SYSCALL_ALIAS(__arm64_sys_##name, sys_ni_posix_timers);
#endif

#endif /* __ASM_SYSCALL_WRAPPER_H */
