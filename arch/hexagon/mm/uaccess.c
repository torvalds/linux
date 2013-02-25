/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * Support for user memory access from kernel.  This will
 * probably be inlined for performance at some point, but
 * for ease of debug, and to a lesser degree for code size,
 * we implement here as subroutines.
 */
#include <linux/types.h>
#include <asm/uaccess.h>
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
		uncleared = __copy_to_user_hexagon(dest, &empty_zero_page,
						PAGE_SIZE);
		if (uncleared)
			return count - (PAGE_SIZE - uncleared);
		count -= PAGE_SIZE;
		dest += PAGE_SIZE;
	}
	if (count)
		count = __copy_to_user_hexagon(dest, &empty_zero_page, count);

	return count;
}

unsigned long clear_user_hexagon(void __user *dest, unsigned long count)
{
	if (!access_ok(VERIFY_WRITE, dest, count))
		return count;
	else
		return __clear_user_hexagon(dest, count);
}
