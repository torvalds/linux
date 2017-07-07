/* MN10300 Optimised checksumming wrappers
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/byteorder.h>
#include <linux/uaccess.h>
#include <asm/checksum.h>
#include "internal.h"

static inline unsigned short from32to16(__wsum sum)
{
	asm("	add	%1,%0		\n"
	    "	addc	0xffff,%0	\n"
	    : "=r" (sum)
	    : "r" (sum << 16), "0" (sum & 0xffff0000)
	    : "cc"
	    );
	return sum >> 16;
}

__sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	return ~do_csum(iph, ihl * 4);
}
EXPORT_SYMBOL(ip_fast_csum);

__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	__wsum result;

	result = do_csum(buff, len);
	result += sum;
	if (sum > result)
		result++;
	return result;
}
EXPORT_SYMBOL(csum_partial);

__sum16 ip_compute_csum(const void *buff, int len)
{
	return ~from32to16(do_csum(buff, len));
}
EXPORT_SYMBOL(ip_compute_csum);

__wsum csum_partial_copy(const void *src, void *dst, int len, __wsum sum)
{
	copy_from_user(dst, src, len);
	return csum_partial(dst, len, sum);
}
EXPORT_SYMBOL(csum_partial_copy);

__wsum csum_partial_copy_nocheck(const void *src, void *dst,
				 int len, __wsum sum)
{
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);
	return sum;
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);

__wsum csum_partial_copy_from_user(const void *src, void *dst,
				   int len, __wsum sum,
				   int *err_ptr)
{
	int missing;

	missing = copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}

	return csum_partial(dst, len, sum);
}
EXPORT_SYMBOL(csum_partial_copy_from_user);

__wsum csum_and_copy_to_user(const void *src, void *dst,
			     int len, __wsum sum,
			     int *err_ptr)
{
	int missing;

	missing = copy_to_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}

	return csum_partial(src, len, sum);
}
EXPORT_SYMBOL(csum_and_copy_to_user);
