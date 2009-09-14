/* syscall parameter access functions
 *
 * Copyright (C) 2009 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H

#include <linux/err.h>
#include <asm/ptrace.h>

/*
 * Get the system call number or -1
 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return regs->syscallno;
}

/*
 * Restore the clobbered GR8 register
 * (1st syscall arg was overwritten with syscall return or error)
 */
static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->gr8 = regs->orig_gr8;
}

/*
 * See if the syscall return value is an error, returning it if it is and 0 if
 * not
 */
static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->gr8) ? regs->gr8 : 0;
}

/*
 * Get the syscall return value
 */
static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->gr8;
}

/*
 * Set the syscall return value
 */
static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error)
		regs->gr8 = -error;
	else
		regs->gr8 = val;
}

/*
 * Retrieve the system call arguments
 */
static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	/*
	 * Do this simply for now. If we need to start supporting
	 * fetching arguments from arbitrary indices, this will need some
	 * extra logic. Presently there are no in-tree users that depend
	 * on this behaviour.
	 */
	BUG_ON(i);

	/* Argument pattern is: GR8, GR9, GR10, GR11, GR12, GR13 */
	switch (n) {
	case 6: args[5] = regs->gr13;
	case 5: args[4] = regs->gr12;
	case 4: args[3] = regs->gr11;
	case 3: args[2] = regs->gr10;
	case 2: args[1] = regs->gr9;
	case 1:	args[0] = regs->gr8;
		break;
	default:
		BUG();
	}
}

/*
 * Alter the system call arguments
 */
static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	/* Same note as above applies */
	BUG_ON(i);

	switch (n) {
	case 6: regs->gr13 = args[5];
	case 5: regs->gr12 = args[4];
	case 4: regs->gr11 = args[3];
	case 3: regs->gr10 = args[2];
	case 2: regs->gr9  = args[1];
	case 1: regs->gr8  = args[0];
		break;
	default:
		BUG();
	}
}

#endif /* _ASM_SYSCALL_H */
