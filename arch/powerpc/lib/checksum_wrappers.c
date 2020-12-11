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
			       int len)
{
	__wsum csum;

	might_sleep();

	if (unlikely(!access_ok(src, len)))
		return 0;

	allow_read_from_user(src, len);

	csum = csum_partial_copy_generic((void __force *)src, dst, len);

	prevent_read_from_user(src, len);
	return csum;
}
EXPORT_SYMBOL(csum_and_copy_from_user);

__wsum csum_and_copy_to_user(const void *src, void __user *dst, int len)
{
	__wsum csum;

	might_sleep();
	if (unlikely(!access_ok(dst, len)))
		return 0;

	allow_write_to_user(dst, len);

	csum = csum_partial_copy_generic(src, (void __force *)dst, len);

	prevent_write_to_user(dst, len);
	return csum;
}
EXPORT_SYMBOL(csum_and_copy_to_user);
