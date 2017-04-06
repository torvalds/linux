/*
 * Functions which are too large to be inlined.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/uaccess.h>

unsigned long copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (likely(access_ok(VERIFY_READ, from, n)))
		n = __copy_from_user(to, from, n);
	else
		memset(to, 0, n);
	return n;
}

unsigned long copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(access_ok(VERIFY_WRITE, to, n)))
		n = __copy_to_user(to, from, n);
	return n;
}

unsigned long copy_in_user(void __user *to, const void __user *from,
			   unsigned long n)
{
	might_sleep();
	if (likely(access_ok(VERIFY_READ, from, n) &&
	    access_ok(VERIFY_WRITE, to, n)))
		n =__copy_tofrom_user(to, from, n);
	return n;
}

EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(copy_in_user);

