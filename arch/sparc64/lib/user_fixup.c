/* user_fixup.c: Fix up user copy faults.
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

/* Calculating the exact fault address when using
 * block loads and stores can be very complicated.
 * Instead of trying to be clever and handling all
 * of the cases, just fix things up simply here.
 */

unsigned long copy_from_user_fixup(void *to, const void __user *from, unsigned long size)
{
	char *dst = to;
	const char __user *src = from;

	while (size) {
		if (__get_user(*dst, src))
			break;
		dst++;
		src++;
		size--;
	}

	if (size)
		memset(dst, 0, size);

	return size;
}

unsigned long copy_to_user_fixup(void __user *to, const void *from, unsigned long size)
{
	char __user *dst = to;
	const char *src = from;

	while (size) {
		if (__put_user(*src, dst))
			break;
		dst++;
		src++;
		size--;
	}

	return size;
}

unsigned long copy_in_user_fixup(void __user *to, void __user *from, unsigned long size)
{
	char __user *dst = to;
	char __user *src = from;

	while (size) {
		char tmp;

		if (__get_user(tmp, src))
			break;
		if (__put_user(tmp, dst))
			break;
		dst++;
		src++;
		size--;
	}

	return size;
}
