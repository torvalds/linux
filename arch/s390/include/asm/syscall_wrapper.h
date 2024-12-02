/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - s390 specific wrappers to syscall definitions
 *
 */

#ifndef _ASM_S390_SYSCALL_WRAPPER_H
#define _ASM_S390_SYSCALL_WRAPPER_H

#define __SC_TYPE(t, a) t

#define SYSCALL_PT_ARG6(regs, m, t1, t2, t3, t4, t5, t6)\
	SYSCALL_PT_ARG5(regs, m, t1, t2, t3, t4, t5),	\
		m(t6, (regs->gprs[7]))

#define SYSCALL_PT_ARG5(regs, m, t1, t2, t3, t4, t5)	\
	SYSCALL_PT_ARG4(regs, m, t1, t2, t3, t4),	\
		m(t5, (regs->gprs[6]))

#define SYSCALL_PT_ARG4(regs, m, t1, t2, t3, t4)	\
	SYSCALL_PT_ARG3(regs, m, t1, t2, t3),		\
		m(t4, (regs->gprs[5]))

#define SYSCALL_PT_ARG3(regs, m, t1, t2, t3)		\
	SYSCALL_PT_ARG2(regs, m, t1, t2),		\
		m(t3, (regs->gprs[4]))

#define SYSCALL_PT_ARG2(regs, m, t1, t2)		\
	SYSCALL_PT_ARG1(regs, m, t1),			\
		m(t2, (regs->gprs[3]))

#define SYSCALL_PT_ARG1(regs, m, t1)			\
		m(t1, (regs->orig_gpr2))

#define SYSCALL_PT_ARGS(x, ...) SYSCALL_PT_ARG##x(__VA_ARGS__)

#ifdef CONFIG_COMPAT
#define __SC_COMPAT_TYPE(t, a) \
	__typeof(__builtin_choose_expr(sizeof(t) > 4, 0L, (t)0)) a

#define __SC_COMPAT_CAST(t, a)						\
({									\
	long __ReS = a;							\
									\
	BUILD_BUG_ON((sizeof(t) > 4) && !__TYPE_IS_L(t) &&		\
		     !__TYPE_IS_UL(t) && !__TYPE_IS_PTR(t) &&		\
		     !__TYPE_IS_LL(t));					\
	if (__TYPE_IS_L(t))						\
		__ReS = (s32)a;						\
	if (__TYPE_IS_UL(t))						\
		__ReS = (u32)a;						\
	if (__TYPE_IS_PTR(t))						\
		__ReS = a & 0x7fffffff;					\
	if (__TYPE_IS_LL(t))						\
		return -ENOSYS;						\
	(t)__ReS;							\
})

#define __S390_SYS_STUBx(x, name, ...)						\
	long __s390_sys##name(struct pt_regs *regs);				\
	ALLOW_ERROR_INJECTION(__s390_sys##name, ERRNO);				\
	long __s390_sys##name(struct pt_regs *regs)				\
	{									\
		long ret = __do_sys##name(SYSCALL_PT_ARGS(x, regs,		\
			__SC_COMPAT_CAST, __MAP(x, __SC_TYPE, __VA_ARGS__)));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		return ret;							\
	}

/*
 * To keep the naming coherent, re-define SYSCALL_DEFINE0 to create an alias
 * named __s390x_sys_*()
 */
#define COMPAT_SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);					\
	long __s390_compat_sys_##sname(void);				\
	ALLOW_ERROR_INJECTION(__s390_compat_sys_##sname, ERRNO);	\
	long __s390_compat_sys_##sname(void)

#define SYSCALL_DEFINE0(sname)						\
	SYSCALL_METADATA(_##sname, 0);					\
	long __s390x_sys_##sname(void);					\
	ALLOW_ERROR_INJECTION(__s390x_sys_##sname, ERRNO);		\
	long __s390_sys_##sname(void)					\
		__attribute__((alias(__stringify(__s390x_sys_##sname)))); \
	long __s390x_sys_##sname(void)

#define COND_SYSCALL(name)						\
	cond_syscall(__s390x_sys_##name);				\
	cond_syscall(__s390_sys_##name)

#define SYS_NI(name)							\
	SYSCALL_ALIAS(__s390x_sys_##name, sys_ni_posix_timers);		\
	SYSCALL_ALIAS(__s390_sys_##name, sys_ni_posix_timers)

#define COMPAT_SYSCALL_DEFINEx(x, name, ...)						\
	__diag_push();									\
	__diag_ignore(GCC, 8, "-Wattribute-alias",					\
		      "Type aliasing is used to sanitize syscall arguments");		\
	long __s390_compat_sys##name(struct pt_regs *regs);				\
	long __s390_compat_sys##name(struct pt_regs *regs)				\
		__attribute__((alias(__stringify(__se_compat_sys##name))));		\
	ALLOW_ERROR_INJECTION(__s390_compat_sys##name, ERRNO);				\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	long __se_compat_sys##name(struct pt_regs *regs);				\
	long __se_compat_sys##name(struct pt_regs *regs)				\
	{										\
		long ret = __do_compat_sys##name(SYSCALL_PT_ARGS(x, regs, __SC_DELOUSE,	\
						 __MAP(x, __SC_TYPE, __VA_ARGS__)));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);						\
		return ret;								\
	}										\
	__diag_pop();									\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

/*
 * As some compat syscalls may not be implemented, we need to expand
 * COND_SYSCALL_COMPAT in kernel/sys_ni.c and COMPAT_SYS_NI in
 * kernel/time/posix-stubs.c to cover this case as well.
 */
#define COND_SYSCALL_COMPAT(name)					\
	cond_syscall(__s390_compat_sys_##name)

#define COMPAT_SYS_NI(name)						\
	SYSCALL_ALIAS(__s390_compat_sys_##name, sys_ni_posix_timers)

#else /* CONFIG_COMPAT */

#define __S390_SYS_STUBx(x, fullname, name, ...)

#define SYSCALL_DEFINE0(sname)						\
	SYSCALL_METADATA(_##sname, 0);					\
	long __s390x_sys_##sname(void);					\
	ALLOW_ERROR_INJECTION(__s390x_sys_##sname, ERRNO);		\
	long __s390x_sys_##sname(void)

#define COND_SYSCALL(name)						\
	cond_syscall(__s390x_sys_##name)

#define SYS_NI(name)							\
	SYSCALL_ALIAS(__s390x_sys_##name, sys_ni_posix_timers);

#endif /* CONFIG_COMPAT */

#define __SYSCALL_DEFINEx(x, name, ...)							\
	__diag_push();									\
	__diag_ignore(GCC, 8, "-Wattribute-alias",					\
		      "Type aliasing is used to sanitize syscall arguments");		\
	long __s390x_sys##name(struct pt_regs *regs)					\
		__attribute__((alias(__stringify(__se_sys##name))));			\
	ALLOW_ERROR_INJECTION(__s390x_sys##name, ERRNO);				\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));		\
	long __se_sys##name(struct pt_regs *regs);					\
	__S390_SYS_STUBx(x, name, __VA_ARGS__)						\
	long __se_sys##name(struct pt_regs *regs)					\
	{										\
		long ret = __do_sys##name(SYSCALL_PT_ARGS(x, regs,			\
				    __SC_CAST, __MAP(x, __SC_TYPE, __VA_ARGS__)));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);						\
		return ret;								\
	}										\
	__diag_pop();									\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#endif /* _ASM_S390_SYSCALL_WRAPPER_H */
