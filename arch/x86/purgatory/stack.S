/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * purgatory:  stack
 *
 * Copyright (C) 2014 Red Hat Inc.
 */

	/* A stack for the loaded kernel.
	 * Separate and in the data section so it can be prepopulated.
	 */
	.data
	.balign 4096
	.globl stack, stack_end

stack:
	.skip 4096
stack_end:
