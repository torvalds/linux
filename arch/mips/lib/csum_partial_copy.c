/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics GmbH
 * Copyright (C) 1998, 1999 Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/string.h>
#include <asm/uaccess.h>
#include <net/checksum.h>

/*
 * copy while checksumming, otherwise like csum_partial
 */
__wsum csum_partial_copy_nocheck(const void *src,
	void *dst, int len, __wsum sum)
{
	/*
	 * It's 2:30 am and I don't feel like doing it real ...
	 * This is lots slower than the real thing (tm)
	 */
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);

	return sum;
}

/*
 * Copy from userspace and compute checksum.  If we catch an exception
 * then zero the rest of the buffer.
 */
__wsum csum_partial_copy_from_user (const void __user *src,
	void *dst, int len, __wsum sum, int *err_ptr)
{
	int missing;

	might_sleep();
	missing = copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}

	return csum_partial(dst, len, sum);
}
