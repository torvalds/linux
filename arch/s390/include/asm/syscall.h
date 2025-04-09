/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Access to user system call parameters and results
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H	1

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/ptrace.h>

extern const sys_call_ptr_t sys_call_table[];
extern const sys_call_ptr_t sys_call_table_emu[];

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return test_pt_regs_flag(regs, PIF_SYSCALL) ?
		(regs->int_code & 0xffff) : -1;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->gprs[2] = regs->orig_gpr2;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->gprs[2];
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT)) {
		/*
		 * Sign-extend the value so (int)-EFOO becomes (long)-EFOO
		 * and will match correctly in comparisons.
		 */
		error = (long)(int)error;
	}
#endif
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->gprs[2];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	set_pt_regs_flag(regs, PIF_SYSCALL_RET_SET);
	regs->gprs[2] = error ? error : val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	unsigned long mask = -1UL;

#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		mask = 0xffffffff;
#endif
	for (int i = 1; i < 6; i++)
		args[i] = regs->gprs[2 + i] & mask;

	args[0] = regs->orig_gpr2 & mask;
}

static inline int syscall_get_arch(struct task_struct *task)
{
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		return AUDIT_ARCH_S390;
#endif
	return AUDIT_ARCH_S390X;
}

static inline bool arch_syscall_is_vdso_sigreturn(struct pt_regs *regs)
{
	return false;
}

#define SYSCALL_FMT_0
#define SYSCALL_FMT_1 , "0" (r2)
#define SYSCALL_FMT_2 , "d" (r3) SYSCALL_FMT_1
#define SYSCALL_FMT_3 , "d" (r4) SYSCALL_FMT_2
#define SYSCALL_FMT_4 , "d" (r5) SYSCALL_FMT_3
#define SYSCALL_FMT_5 , "d" (r6) SYSCALL_FMT_4
#define SYSCALL_FMT_6 , "d" (r7) SYSCALL_FMT_5

#define SYSCALL_PARM_0
#define SYSCALL_PARM_1 , long arg1
#define SYSCALL_PARM_2 SYSCALL_PARM_1, long arg2
#define SYSCALL_PARM_3 SYSCALL_PARM_2, long arg3
#define SYSCALL_PARM_4 SYSCALL_PARM_3, long arg4
#define SYSCALL_PARM_5 SYSCALL_PARM_4, long arg5
#define SYSCALL_PARM_6 SYSCALL_PARM_5, long arg6

#define SYSCALL_REGS_0
#define SYSCALL_REGS_1							\
	register long r2 asm("2") = arg1
#define SYSCALL_REGS_2							\
	SYSCALL_REGS_1;							\
	register long r3 asm("3") = arg2
#define SYSCALL_REGS_3							\
	SYSCALL_REGS_2;							\
	register long r4 asm("4") = arg3
#define SYSCALL_REGS_4							\
	SYSCALL_REGS_3;							\
	register long r5 asm("5") = arg4
#define SYSCALL_REGS_5							\
	SYSCALL_REGS_4;							\
	register long r6 asm("6") = arg5
#define SYSCALL_REGS_6							\
	SYSCALL_REGS_5;							\
	register long r7 asm("7") = arg6

#define GENERATE_SYSCALL_FUNC(nr)					\
static __always_inline							\
long syscall##nr(unsigned long syscall SYSCALL_PARM_##nr)		\
{									\
	register unsigned long r1 asm ("1") = syscall;			\
	register long rc asm ("2");					\
	SYSCALL_REGS_##nr;						\
									\
	asm volatile (							\
		"	svc	0\n"					\
		: "=d" (rc)						\
		: "d" (r1) SYSCALL_FMT_##nr				\
		: "memory");						\
	return rc;							\
}

GENERATE_SYSCALL_FUNC(0)
GENERATE_SYSCALL_FUNC(1)
GENERATE_SYSCALL_FUNC(2)
GENERATE_SYSCALL_FUNC(3)
GENERATE_SYSCALL_FUNC(4)
GENERATE_SYSCALL_FUNC(5)
GENERATE_SYSCALL_FUNC(6)

#endif	/* _ASM_SYSCALL_H */
