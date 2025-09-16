/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SYSCALL_H
#define __ASM_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/compat.h>
#include <linux/err.h>

typedef long (*syscall_fn_t)(const struct pt_regs *regs);

extern const syscall_fn_t sys_call_table[];

#ifdef CONFIG_COMPAT
extern const syscall_fn_t compat_sys_call_table[];
#endif

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->syscallno;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->regs[0] = regs->orig_x0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	unsigned long val = regs->regs[0];

	if (is_compat_thread(task_thread_info(task)))
		val = sign_extend64(val, 31);

	return val;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = syscall_get_return_value(task, regs);

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error)
		val = error;

	if (is_compat_thread(task_thread_info(task)))
		val = lower_32_bits(val);

	regs->regs[0] = val;
}

static inline void syscall_set_nr(struct task_struct *task,
				  struct pt_regs *regs,
				  int nr)
{
	regs->syscallno = nr;
	if (nr == -1) {
		/*
		 * When the syscall number is set to -1, the syscall will be
		 * skipped.  In this case the syscall return value has to be
		 * set explicitly, otherwise the first syscall argument is
		 * returned as the syscall return value.
		 */
		syscall_set_return_value(task, regs, -ENOSYS, 0);
	}
}

#define SYSCALL_MAX_ARGS 6

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_x0;
	args++;

	memcpy(args, &regs->regs[1], 5 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	memcpy(&regs->regs[0], args, 6 * sizeof(args[0]));
	/*
	 * Also copy the first argument into orig_x0
	 * so that syscall_get_arguments() would return it
	 * instead of the previous value.
	 */
	regs->orig_x0 = regs->regs[0];
}

/*
 * We don't care about endianness (__AUDIT_ARCH_LE bit) here because
 * AArch64 has the same system calls both on little- and big- endian.
 */
static inline int syscall_get_arch(struct task_struct *task)
{
	if (is_compat_thread(task_thread_info(task)))
		return AUDIT_ARCH_ARM;

	return AUDIT_ARCH_AARCH64;
}

int syscall_trace_enter(struct pt_regs *regs);
void syscall_trace_exit(struct pt_regs *regs);

#endif	/* __ASM_SYSCALL_H */
