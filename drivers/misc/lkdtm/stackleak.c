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
	bool test_failed = false;

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
		test_failed = true;
		goto end;
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
		pr_err("FAIL: the erased part is not found (checked %lu bytes)\n",
						i * sizeof(unsigned long));
		test_failed = true;
		goto end;
	}

	pr_info("the erased part begins after %lu not poisoned bytes\n",
				(i - found) * sizeof(unsigned long));

	/* The rest of thread stack should be erased */
	for (; i < left; i++) {
		if (*(sp - i) != STACKLEAK_POISON) {
			pr_err("FAIL: bad value number %lu in the erased part: 0x%lx\n",
								i, *(sp - i));
			test_failed = true;
		}
	}

end:
	if (test_failed) {
		pr_err("FAIL: the thread stack is NOT properly erased!\n");
		pr_expected_config(CONFIG_GCC_PLUGIN_STACKLEAK);
	} else {
		pr_info("OK: the rest of the thread stack is properly erased\n");
	}
}
