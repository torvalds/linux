/*
 * purgatory:  stack
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

	/* A stack for the loaded kernel.
	 * Seperate and in the data section so it can be prepopulated.
	 */
	.data
	.balign 4096
	.globl stack, stack_end

stack:
	.skip 4096
stack_end:
