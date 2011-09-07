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

#ifndef __ASM_OPENRISC_SIGCONTEXT_H
#define __ASM_OPENRISC_SIGCONTEXT_H

#include <asm/ptrace.h>

/* This struct is saved by setup_frame in signal.c, to keep the current
   context while a signal handler is executed. It's restored by sys_sigreturn.

   To keep things simple, we use pt_regs here even though normally you just
   specify the list of regs to save. Then we can use copy_from_user on the
   entire regs instead of a bunch of get_user's as well...
*/

struct sigcontext {
	struct pt_regs regs;  /* needs to be first */
	unsigned long oldmask;
	unsigned long usp;    /* usp before stacking this gunk on it */
};

#endif /* __ASM_OPENRISC_SIGCONTEXT_H */
