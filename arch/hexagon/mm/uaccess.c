// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * Support for user memory access from kernel.  This will
 * probably be inlined for performance at some point, but
 * for ease of debug, and to a lesser degree for code size,
 * we implement here as subroutines.
 */
#include <linux/types.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

/*
 * For clear_user(), exploit previously defined copy_to_user function
 * and the fact that we've got a handy zero page defined in kernel/head.S
 *
 * dczero here would be even faster.
 */
__kernel_size_t __clear_user_hexagon(void __user *dest, unsigned long count)
{
	long uncleared;

	while (count > PAGE_SIZE) {
		uncleared = raw_copy_to_user(dest, &empty_zero_page, PAGE_SIZE);
		if (uncleared)
			return count - (PAGE_SIZE - uncleared);
		count -= PAGE_SIZE;
		dest += PAGE_SIZE;
	}
	if (count)
		count = raw_copy_to_user(dest, &empty_zero_page, count);

	return count;
}

unsigned long clear_user_hexagon(void __user *dest, unsigned long count)
{
	if (!access_ok(dest, count))
		return count;
	else
		return __clear_user_hexagon(dest, count);
}
