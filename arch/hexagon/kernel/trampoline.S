/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * Trampoline sequences to be copied onto user stack.
 * This consumes a little more space than hand-assembling
 * immediate constants for use in C, but is more portable
 * to future tweaks to the Hexagon instruction set.
 */

#include <asm/unistd.h>

/*  Sig trampolines - call sys_sigreturn or sys_rt_sigreturn as appropriate */

/*  plain sigreturn is gone.  */

	.globl __rt_sigtramp_template
__rt_sigtramp_template:
	r6 = #__NR_rt_sigreturn;
	trap0(#1);
