/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2008 Imagination Technologies Ltd.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_METAG_SYSCALL_H
#define _ASM_METAG_SYSCALL_H

#include <linux/sched.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#include <asm/switch.h>

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	unsigned long insn;

	/*
	 * FIXME there's no way to find out how we got here other than to
	 * examine the memory at the PC to see if it is a syscall
	 * SWITCH instruction.
	 */
	if (get_user(insn, (unsigned long *)(regs->ctx.CurrPC - 4)))
		return -1;

	if (insn == __METAG_SW_ENCODING(SYS))
		return regs->ctx.DX[0].U1;
	else
		return -1L;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* do nothing */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ctx.DX[0].U0;
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->ctx.DX[0].U0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->ctx.DX[0].U0 = (long) error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	unsigned int reg, j;
	BUG_ON(i + n > 6);

	for (j = i, reg = 6 - i; j < (i + n); j++, reg--) {
		if (reg % 2)
			args[j] = regs->ctx.DX[(reg + 1) / 2].U0;
		else
			args[j] = regs->ctx.DX[reg / 2].U1;
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	unsigned int reg;
	BUG_ON(i + n > 6);

	for (reg = 6 - i; i < (i + n); i++, reg--) {
		if (reg % 2)
			regs->ctx.DX[(reg + 1) / 2].U0 = args[i];
		else
			regs->ctx.DX[reg / 2].U1 = args[i];
	}
}

#define NR_syscalls __NR_syscalls

/* generic syscall table */
extern const void *sys_call_table[];

#endif	/* _ASM_METAG_SYSCALL_H */
