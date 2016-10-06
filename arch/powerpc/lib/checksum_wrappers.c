/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2010
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <linux/export.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/checksum.h>
#include <asm/uaccess.h>

__wsum csum_and_copy_from_user(const void __user *src, void *dst,
			       int len, __wsum sum, int *err_ptr)
{
	unsigned int csum;

	might_sleep();

	*err_ptr = 0;

	if (!len) {
		csum = 0;
		goto out;
	}

	if (unlikely((len < 0) || !access_ok(VERIFY_READ, src, len))) {
		*err_ptr = -EFAULT;
		csum = (__force unsigned int)sum;
		goto out;
	}

	csum = csum_partial_copy_generic((void __force *)src, dst,
					 len, sum, err_ptr, NULL);

	if (unlikely(*err_ptr)) {
		int missing = __copy_from_user(dst, src, len);

		if (missing) {
			memset(dst + len - missing, 0, missing);
			*err_ptr = -EFAULT;
		} else {
			*err_ptr = 0;
		}

		csum = csum_partial(dst, len, sum);
	}

out:
	return (__force __wsum)csum;
}
EXPORT_SYMBOL(csum_and_copy_from_user);

__wsum csum_and_copy_to_user(const void *src, void __user *dst, int len,
			     __wsum sum, int *err_ptr)
{
	unsigned int csum;

	might_sleep();

	*err_ptr = 0;

	if (!len) {
		csum = 0;
		goto out;
	}

	if (unlikely((len < 0) || !access_ok(VERIFY_WRITE, dst, len))) {
		*err_ptr = -EFAULT;
		csum = -1; /* invalid checksum */
		goto out;
	}

	csum = csum_partial_copy_generic(src, (void __force *)dst,
					 len, sum, NULL, err_ptr);

	if (unlikely(*err_ptr)) {
		csum = csum_partial(src, len, sum);

		if (copy_to_user(dst, src, len)) {
			*err_ptr = -EFAULT;
			csum = -1; /* invalid checksum */
		}
	}

out:
	return (__force __wsum)csum;
}
EXPORT_SYMBOL(csum_and_copy_to_user);
