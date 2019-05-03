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
	unsigned long *sp, left, found, i;
	const unsigned long check_depth =
			STACKLEAK_SEARCH_DEPTH / sizeof(unsigned long);

	/*
	 * For the details about the alignment of the poison values, see
	 * the comment in stackleak_track_stack().
	 */
	sp = PTR_ALIGN(&i, sizeof(unsigned long));

	left = ((unsigned long)sp & (THREAD_SIZE - 1)) / sizeof(unsigned long);
	sp--;

	/*
	 * One 'long int' at the bottom of the thread stack is reserved
	 * and not poisoned.
	 */
	if (left > 1) {
		left--;
	} else {
		pr_err("FAIL: not enough stack space for the test\n");
		return;
	}

	pr_info("checking unused part of the thread stack (%lu bytes)...\n",
					left * sizeof(unsigned long));

	/*
	 * Search for 'check_depth' poison values in a row (just like
	 * stackleak_erase() does).
	 */
	for (i = 0, found = 0; i < left && found <= check_depth; i++) {
		if (*(sp - i) == STACKLEAK_POISON)
			found++;
		else
			found = 0;
	}

	if (found <= check_depth) {
		pr_err("FAIL: thread stack is not erased (checked %lu bytes)\n",
						i * sizeof(unsigned long));
		return;
	}

	pr_info("first %lu bytes are unpoisoned\n",
				(i - found) * sizeof(unsigned long));

	/* The rest of thread stack should be erased */
	for (; i < left; i++) {
		if (*(sp - i) != STACKLEAK_POISON) {
			pr_err("FAIL: thread stack is NOT properly erased\n");
			return;
		}
	}

	pr_info("OK: the rest of the thread stack is properly erased\n");
	return;
}
