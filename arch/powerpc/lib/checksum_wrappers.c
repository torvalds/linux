// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) IBM Corporation, 2010
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <linux/export.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/checksum.h>
#include <linux/uaccess.h>

__wsum csum_and_copy_from_user(const void __user *src, void *dst,
			       int len, __wsum sum, int *err_ptr)
{
	unsigned int csum;

	might_sleep();
	allow_read_from_user(src, len);

	*err_ptr = 0;

	if (!len) {
		csum = 0;
		goto out;
	}

	if (unlikely((len < 0) || !access_ok(src, len))) {
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
	prevent_read_from_user(src, len);
	return (__force __wsum)csum;
}
EXPORT_SYMBOL(csum_and_copy_from_user);

__wsum csum_and_copy_to_user(const void *src, void __user *dst, int len,
			     __wsum sum, int *err_ptr)
{
	unsigned int csum;

	might_sleep();
	allow_write_to_user(dst, len);

	*err_ptr = 0;

	if (!len) {
		csum = 0;
		goto out;
	}

	if (unlikely((len < 0) || !access_ok(dst, len))) {
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
	prevent_write_to_user(dst, len);
	return (__force __wsum)csum;
}
EXPORT_SYMBOL(csum_and_copy_to_user);
