/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		M32R specific IP/TCP/UDP checksumming routines
 *		(Some code taken from MIPS architecture)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995  Waldorf Electronics GmbH
 * Copyright (C) 1998, 1999  Ralf Baechle
 * Copyright (C) 2001-2005  Hiroyuki Kondo, Hirokazu Takata
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>

#include <net/checksum.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

/*
 * Copy while checksumming, otherwise like csum_partial
 */
unsigned int
csum_partial_copy_nocheck (const unsigned char *src, unsigned char *dst,
                           int len, unsigned int sum)
{
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);

	return sum;
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);

/*
 * Copy from userspace and compute checksum.  If we catch an exception
 * then zero the rest of the buffer.
 */
unsigned int
csum_partial_copy_from_user (const unsigned char __user *src,
			     unsigned char *dst,
			     int len, unsigned int sum, int *err_ptr)
{
	int missing;

	missing = copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}

	return csum_partial(dst, len-missing, sum);
}
EXPORT_SYMBOL(csum_partial_copy_from_user);
EXPORT_SYMBOL(csum_partial);
