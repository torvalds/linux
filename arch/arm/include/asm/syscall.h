/*
 * Access to user system call parameters and results
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_ARM_SYSCALL_H
#define _ASM_ARM_SYSCALL_H

#include <linux/audit.h> /* for AUDIT_ARCH_* */
#include <linux/elf.h> /* for ELF_EM */
#include <linux/err.h>
#include <linux/sched.h>

#include <asm/unistd.h>

#define NR_syscalls (__NR_syscalls)

extern const unsigned long sys_call_table[];

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return task_thread_info(task)->syscall;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->ARM_r0 = regs->ARM_ORIG_r0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ARM_r0;
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->ARM_r0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->ARM_r0 = (long) error ? error : val;
}

#define SYSCALL_MAX_ARGS 7

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	if (i + n > SYSCALL_MAX_ARGS) {
		unsigned long *args_bad = args + SYSCALL_MAX_ARGS - i;
		unsigned int n_bad = n + i - SYSCALL_MAX_ARGS;
		pr_warning("%s called with max args %d, handling only %d\n",
			   __func__, i + n, SYSCALL_MAX_ARGS);
		memset(args_bad, 0, n_bad * sizeof(args[0]));
		n = SYSCALL_MAX_ARGS - i;
	}

	if (i == 0) {
		args[0] = regs->ARM_ORIG_r0;
		args++;
		i++;
		n--;
	}

	memcpy(args, &regs->ARM_r0 + i, n * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	if (i + n > SYSCALL_MAX_ARGS) {
		pr_warning("%s called with max args %d, handling only %d\n",
			   __func__, i + n, SYSCALL_MAX_ARGS);
		n = SYSCALL_MAX_ARGS - i;
	}

	if (i == 0) {
		regs->ARM_ORIG_r0 = args[0];
		args++;
		i++;
		n--;
	}

	memcpy(&regs->ARM_r0 + i, args, n * sizeof(args[0]));
}

static inline int syscall_get_arch(void)
{
	/* ARM tasks don't change audit architectures on the fly. */
	return AUDIT_ARCH_ARM;
}

#endif /* _ASM_ARM_SYSCALL_H */
