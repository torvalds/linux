// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *    Optimized memory copy routines.
 *
 *    Copyright (C) 2004 Randolph Chung <tausq@debian.org>
 *    Copyright (C) 2013-2017 Helge Deller <deller@gmx.de>
 *
 *    Portions derived from the GNU C Library
 *    Copyright (C) 1991, 1997, 2003 Free Software Foundation, Inc.
 */

#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#define get_user_space()	mfsp(SR_USER)
#define get_kernel_space()	SR_KERNEL

/* Returns 0 for success, otherwise, returns number of bytes not transferred. */
extern unsigned long pa_memcpy(void *dst, const void *src,
				unsigned long len);

unsigned long raw_copy_to_user(void __user *dst, const void *src,
			       unsigned long len)
{
	mtsp(get_kernel_space(), SR_TEMP1);
	mtsp(get_user_space(), SR_TEMP2);
	return pa_memcpy((void __force *)dst, src, len);
}
EXPORT_SYMBOL(raw_copy_to_user);

unsigned long raw_copy_from_user(void *dst, const void __user *src,
			       unsigned long len)
{
	unsigned long start = (unsigned long) src;
	unsigned long end = start + len;
	unsigned long newlen = len;

	mtsp(get_user_space(), SR_TEMP1);
	mtsp(get_kernel_space(), SR_TEMP2);

	/* Check region is user accessible */
	if (start)
	while (start < end) {
		if (!prober_user(SR_TEMP1, start)) {
			newlen = (start - (unsigned long) src);
			break;
		}
		start += PAGE_SIZE;
		/* align to page boundry which may have different permission */
		start = PAGE_ALIGN_DOWN(start);
	}
	return len - newlen + pa_memcpy(dst, (void __force *)src, newlen);
}
EXPORT_SYMBOL(raw_copy_from_user);

void * memcpy(void * dst,const void *src, size_t count)
{
	mtsp(get_kernel_space(), SR_TEMP1);
	mtsp(get_kernel_space(), SR_TEMP2);
	pa_memcpy(dst, src, count);
	return dst;
}

EXPORT_SYMBOL(memcpy);

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	if ((unsigned long)unsafe_src < PAGE_SIZE)
		return false;
	/* check for I/O space F_EXTEND(0xfff00000) access as well? */
	return true;
}
