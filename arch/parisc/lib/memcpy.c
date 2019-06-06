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

#define get_user_space() (uaccess_kernel() ? 0 : mfsp(3))
#define get_kernel_space() (0)

/* Returns 0 for success, otherwise, returns number of bytes not transferred. */
extern unsigned long pa_memcpy(void *dst, const void *src,
				unsigned long len);

unsigned long raw_copy_to_user(void __user *dst, const void *src,
			       unsigned long len)
{
	mtsp(get_kernel_space(), 1);
	mtsp(get_user_space(), 2);
	return pa_memcpy((void __force *)dst, src, len);
}
EXPORT_SYMBOL(raw_copy_to_user);

unsigned long raw_copy_from_user(void *dst, const void __user *src,
			       unsigned long len)
{
	mtsp(get_user_space(), 1);
	mtsp(get_kernel_space(), 2);
	return pa_memcpy(dst, (void __force *)src, len);
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long raw_copy_in_user(void __user *dst, const void __user *src, unsigned long len)
{
	mtsp(get_user_space(), 1);
	mtsp(get_user_space(), 2);
	return pa_memcpy((void __force *)dst, (void __force *)src, len);
}


void * memcpy(void * dst,const void *src, size_t count)
{
	mtsp(get_kernel_space(), 1);
	mtsp(get_kernel_space(), 2);
	pa_memcpy(dst, src, count);
	return dst;
}

EXPORT_SYMBOL(raw_copy_in_user);
EXPORT_SYMBOL(memcpy);

long probe_kernel_read(void *dst, const void *src, size_t size)
{
	unsigned long addr = (unsigned long)src;

	if (addr < PAGE_SIZE)
		return -EFAULT;

	/* check for I/O space F_EXTEND(0xfff00000) access as well? */

	return __probe_kernel_read(dst, src, size);
}
