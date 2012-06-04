/*
 * Common heap and stack allocations
 */

#include <linux/linkage.h>

	.data
GLOBAL(HEAP)
	.long	rm_heap
GLOBAL(heap_end)
	.long	rm_stack

	.bss
	.balign	16
GLOBAL(rm_heap)
	.space	2048
GLOBAL(rm_stack)
	.space	2048
GLOBAL(rm_stack_end)
