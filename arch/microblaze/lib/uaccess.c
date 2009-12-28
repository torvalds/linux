/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/string.h>
#include <asm/uaccess.h>

#include <asm/bug.h>

long strnlen_user(const char __user *src, long count)
{
	return strlen(src) + 1;
}

#define __do_strncpy_from_user(dst, src, count, res)			\
	do {								\
		char *tmp;						\
		strncpy(dst, src, count);				\
		for (tmp = dst; *tmp && count > 0; tmp++, count--)	\
			;						\
		res = (tmp - dst);					\
	} while (0)

long __strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

long strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}

unsigned long __copy_tofrom_user(void __user *to,
		const void __user *from, unsigned long size)
{
	memcpy(to, from, size);
	return 0;
}
