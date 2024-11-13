/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sys32.S: I-cache tricks for 32-bit compatibility layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/errno.h>

/* NOTE: call as jump breaks return stack, we have to avoid that */

	.text

	.globl		sys32_mmap2
sys32_mmap2:
	sethi		%hi(sys_mmap), %g1
	jmpl		%g1 + %lo(sys_mmap), %g0
	 sllx		%o5, 12, %o5
