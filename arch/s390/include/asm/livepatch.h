/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * livepatch.h - s390-specific Kernel Live Patching Core
 *
 *  Copyright (c) 2013-2015 SUSE
 *   Authors: Jiri Kosina
 *	      Vojtech Pavlik
 *	      Jiri Slaby
 */

#ifndef ASM_LIVEPATCH_H
#define ASM_LIVEPATCH_H

#include <linux/ftrace.h>
#include <asm/ptrace.h>

static inline void klp_arch_set_pc(struct ftrace_regs *fregs, unsigned long ip)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	regs->psw.addr = ip;
}

#endif
