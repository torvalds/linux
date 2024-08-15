/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _UAPI__ASM_OPENRISC_PTRACE_H
#define _UAPI__ASM_OPENRISC_PTRACE_H

#ifndef __ASSEMBLY__
/*
 * This is the layout of the regset returned by the GETREGSET ptrace call
 */
struct user_regs_struct {
	/* GPR R0-R31... */
	unsigned long gpr[32];
	unsigned long pc;
	unsigned long sr;
};
#endif


#endif /* _UAPI__ASM_OPENRISC_PTRACE_H */
