/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common heap and stack allocations
 */

#include <linux/linkage.h>

	.data
SYM_DATA(HEAP,		.long rm_heap)
SYM_DATA(heap_end,	.long rm_stack)

	.bss
	.balign	16
SYM_DATA(rm_heap,	.space 2048)

SYM_DATA_START(rm_stack)
	.space	2048
SYM_DATA_END_LABEL(rm_stack, SYM_L_GLOBAL, rm_stack_end)
