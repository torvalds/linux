#ifndef __ASM_SH_SYSCALL_64_H
#define __ASM_SH_SYSCALL_64_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

/* The system call number is given by the user in R9 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return (regs->syscall_nr >= 0) ? regs->regs[9] : -1L;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/*
	 * XXX: This needs some thought. On SH we don't
	 * save away the original R9 value anywhere.
	 */
}

static inline bool syscall_has_error(struct pt_regs *regs)
{
	return (regs->sr & 0x1) ? true : false;
}
static inline void syscall_set_error(struct pt_regs *regs)
{
	regs->sr |= 0x1;
}
static inline void syscall_clear_error(struct pt_regs *regs)
{
	regs->sr &= ~0x1;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return syscall_has_error(regs) ? regs->regs[9] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->regs[9];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error) {
		syscall_set_error(regs);
		regs->regs[9] = -error;
	} else {
		syscall_clear_error(regs);
		regs->regs[9] = val;
	}
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(args, &regs->regs[2 + i], n * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(&regs->regs[2 + i], args, n * sizeof(args[0]));
}

#endif /* __ASM_SH_SYSCALL_64_H */
