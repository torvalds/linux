/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MICROBLAZE_SYSCALL_H
#define __ASM_MICROBLAZE_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

/* The system call number is given by the user in R12 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return regs->r12;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* TODO.  */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->r3) ? regs->r3 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->r3;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error)
		regs->r3 = -error;
	else
		regs->r3 = val;
}

static inline microblaze_reg_t microblaze_get_syscall_arg(struct pt_regs *regs,
							  unsigned int n)
{
	switch (n) {
	case 5: return regs->r10;
	case 4: return regs->r9;
	case 3: return regs->r8;
	case 2: return regs->r7;
	case 1: return regs->r6;
	case 0: return regs->r5;
	default:
		BUG();
	}
	return ~0;
}

static inline void microblaze_set_syscall_arg(struct pt_regs *regs,
					      unsigned int n,
					      unsigned long val)
{
	switch (n) {
	case 5:
		regs->r10 = val;
	case 4:
		regs->r9 = val;
	case 3:
		regs->r8 = val;
	case 2:
		regs->r7 = val;
	case 1:
		regs->r6 = val;
	case 0:
		regs->r5 = val;
	default:
		BUG();
	}
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	while (n--)
		*args++ = microblaze_get_syscall_arg(regs, i++);
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	while (n--)
		microblaze_set_syscall_arg(regs, i++, *args++);
}

asmlinkage unsigned long do_syscall_trace_enter(struct pt_regs *regs);
asmlinkage void do_syscall_trace_leave(struct pt_regs *regs);

static inline int syscall_get_arch(void)
{
	return AUDIT_ARCH_MICROBLAZE;
}
#endif /* __ASM_MICROBLAZE_SYSCALL_H */
