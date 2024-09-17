/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * purgatory:  stack
 *
 * Copyright (C) 2014 Red Hat Inc.
 */

#include <linux/linkage.h>

	/* A stack for the loaded kernel.
	 * Separate and in the data section so it can be prepopulated.
	 */
	.data
	.balign 4096

SYM_DATA_START(stack)
	.skip 4096
SYM_DATA_END_LABEL(stack, SYM_L_GLOBAL, stack_end)
