/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - s390 specific wrappers to syscall definitions
 *
 */

#ifndef _ASM_S390_SYSCALL_WRAPPER_H
#define _ASM_S390_SYSCALL_WRAPPER_H

/* Mapping of registers to parameters for syscalls */
#define SC_S390_REGS_TO_ARGS(x, ...)					\
	__MAP(x, __SC_ARGS						\
	      ,, regs->orig_gpr2,, regs->gprs[3],, regs->gprs[4]	\
	      ,, regs->gprs[5],, regs->gprs[6],, regs->gprs[7])

#define SYSCALL_DEFINE0(sname)						\
	SYSCALL_METADATA(_##sname, 0);					\
	long __s390x_sys_##sname(struct pt_regs *__unused);		\
	ALLOW_ERROR_INJECTION(__s390x_sys_##sname, ERRNO);		\
	static inline long __do_sys_##sname(void);			\
	long __s390x_sys_##sname(struct pt_regs *__unused)		\
	{								\
		return __do_sys_##sname();				\
	}								\
	static inline long __do_sys_##sname(void)

#define COND_SYSCALL(name)						\
	cond_syscall(__s390x_sys_##name)

#define __S390_SYS_STUBx(x, fullname, name, ...)

#define __SYSCALL_DEFINEx(x, name, ...)						\
	long __s390x_sys##name(struct pt_regs *regs);				\
	ALLOW_ERROR_INJECTION(__s390x_sys##name, ERRNO);			\
	static inline long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__));	\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__));	\
	__S390_SYS_STUBx(x, name, __VA_ARGS__);					\
	long __s390x_sys##name(struct pt_regs *regs)				\
	{									\
		return __se_sys##name(SC_S390_REGS_TO_ARGS(x, __VA_ARGS__));	\
	}									\
	static inline long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__))	\
	{									\
		__MAP(x, __SC_TEST, __VA_ARGS__);				\
		return __do_sys##name(__MAP(x, __SC_CAST, __VA_ARGS__));	\
	}									\
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))

#endif /* _ASM_S390_SYSCALL_WRAPPER_H */
