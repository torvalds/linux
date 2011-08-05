/*
 * OpenRISC sys_or32.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on some platforms.
 * Since we don't have to do any backwards compatibility, our
 * versions are done in the most "normal" way possible.
 */

#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/mm.h>

#include <asm/syscalls.h>

/* These are secondary entry points as the primary entry points are defined in
 * entry.S where we add the 'regs' parameter value
 */

asmlinkage long _sys_clone(unsigned long clone_flags, unsigned long newsp,
			   int __user *parent_tid, int __user *child_tid,
			   struct pt_regs *regs)
{
	long ret;

	/* FIXME: Is alignment necessary? */
	/* newsp = ALIGN(newsp, 4); */

	if (!newsp)
		newsp = regs->sp;

	ret = do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);

	return ret;
}

asmlinkage int _sys_fork(struct pt_regs *regs)
{
#ifdef CONFIG_MMU
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
#else
	return -EINVAL;
#endif
}
