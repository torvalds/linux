/* uaccess.c: userspace access functions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

/*****************************************************************************/
/*
 * copy a null terminated string from userspace
 */
long strncpy_from_user(char *dst, const char __user *src, long count)
{
	unsigned long max;
	char *p, ch;
	long err = -EFAULT;

	BUG_ON(count < 0);

	p = dst;

#ifndef CONFIG_MMU
	if ((unsigned long) src < memory_start)
		goto error;
#endif

	if ((unsigned long) src >= get_addr_limit())
		goto error;

	max = get_addr_limit() - (unsigned long) src;
	if ((unsigned long) count > max) {
		memset(dst + max, 0, count - max);
		count = max;
	}

	err = 0;
	for (; count > 0; count--, p++, src++) {
		__get_user_asm(err, ch, src, "ub", "=r");
		if (err < 0)
			goto error;
		if (!ch)
			break;
		*p = ch;
	}

	err = p - dst; /* return length excluding NUL */

 error:
	if (count > 0)
		memset(p, 0, count); /* clear remainder of buffer [security] */

	return err;

} /* end strncpy_from_user() */

EXPORT_SYMBOL(strncpy_from_user);

/*****************************************************************************/
/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
long strnlen_user(const char __user *src, long count)
{
	const char __user *p;
	long err = 0;
	char ch;

	BUG_ON(count < 0);

#ifndef CONFIG_MMU
	if ((unsigned long) src < memory_start)
		return 0;
#endif

	if ((unsigned long) src >= get_addr_limit())
		return 0;

	for (p = src; count > 0; count--, p++) {
		__get_user_asm(err, ch, p, "ub", "=r");
		if (err < 0)
			return 0;
		if (!ch)
			break;
	}

	return p - src + 1; /* return length including NUL */

} /* end strnlen_user() */

EXPORT_SYMBOL(strnlen_user);
