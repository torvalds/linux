// SPDX-License-Identifier: GPL-2.0
/*
 * This code tests that the current task stack is properly erased (filled
 * with STACKLEAK_POISON).
 *
 * Authors:
 *   Alexander Popov <alex.popov@linux.com>
 *   Tycho Andersen <tycho@tycho.ws>
 */

#include "lkdtm.h"
#include <linux/stackleak.h>

void lkdtm_STACKLEAK_ERASING(void)
{
	const unsigned long task_stack_base = (unsigned long)task_stack_page(current);
	const unsigned long task_stack_low = stackleak_task_low_bound(current);
	const unsigned long task_stack_high = stackleak_task_high_bound(current);
	const unsigned long current_sp = current_stack_pointer;
	const unsigned long lowest_sp = current->lowest_stack;
	unsigned long untracked_high;
	unsigned long poison_high, poison_low;
	bool test_failed = false;

	/*
	 * Depending on what has run prior to this test, the lowest recorded
	 * stack pointer could be above or below the current stack pointer.
	 * Start from the lowest of the two.
	 *
	 * Poison values are naturally-aligned unsigned longs. As the current
	 * stack pointer might not be sufficiently aligned, we must align
	 * downwards to find the lowest known stack pointer value. This is the
	 * high boundary for a portion of the stack which may have been used
	 * without being tracked, and has to be scanned for poison.
	 */
	untracked_high = min(current_sp, lowest_sp);
	untracked_high = ALIGN_DOWN(untracked_high, sizeof(unsigned long));

	/*
	 * Find the top of the poison in the same way as the erasing code.
	 */
	poison_high = stackleak_find_top_of_poison(task_stack_low, untracked_high);

	/*
	 * Check whether the poisoned portion of the stack (if any) consists
	 * entirely of poison. This verifies the entries that
	 * stackleak_find_top_of_poison() should have checked.
	 */
	poison_low = poison_high;
	while (poison_low > task_stack_low) {
		poison_low -= sizeof(unsigned long);

		if (*(unsigned long *)poison_low == STACKLEAK_POISON)
			continue;

		pr_err("FAIL: non-poison value %lu bytes below poison boundary: 0x%lx\n",
		       poison_high - poison_low, *(unsigned long *)poison_low);
		test_failed = true;
	}

	pr_info("stackleak stack usage:\n"
		"  high offset: %lu bytes\n"
		"  current:     %lu bytes\n"
		"  lowest:      %lu bytes\n"
		"  tracked:     %lu bytes\n"
		"  untracked:   %lu bytes\n"
		"  poisoned:    %lu bytes\n"
		"  low offset:  %lu bytes\n",
		task_stack_base + THREAD_SIZE - task_stack_high,
		task_stack_high - current_sp,
		task_stack_high - lowest_sp,
		task_stack_high - untracked_high,
		untracked_high - poison_high,
		poison_high - task_stack_low,
		task_stack_low - task_stack_base);

	if (test_failed) {
		pr_err("FAIL: the thread stack is NOT properly erased!\n");
		pr_expected_config(CONFIG_GCC_PLUGIN_STACKLEAK);
	} else {
		pr_info("OK: the rest of the thread stack is properly erased\n");
	}
}
