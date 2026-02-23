// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002, 2003 Andi Kleen, SuSE Labs.
 *
 * Wrappers of assembly checksum functions for x86-64.
 */
#include <asm/checksum.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <asm/smap.h>

/**
 * csum_and_copy_from_user - Copy and checksum from user space.
 * @src: source address (user space)
 * @dst: destination address
 * @len: number of bytes to be copied.
 *
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits.
 */
__wsum
csum_and_copy_from_user(const void __user *src, void *dst, int len)
{
	__wsum sum;

	might_sleep();
	if (!user_access_begin(src, len))
		return 0;
	sum = csum_partial_copy_generic((__force const void *)src, dst, len);
	user_access_end();
	return sum;
}

/**
 * csum_and_copy_to_user - Copy and checksum to user space.
 * @src: source address
 * @dst: destination address (user space)
 * @len: number of bytes to be copied.
 *
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits.
 */
__wsum
csum_and_copy_to_user(const void *src, void __user *dst, int len)
{
	__wsum sum;

	might_sleep();
	if (!user_access_begin(dst, len))
		return 0;
	sum = csum_partial_copy_generic(src, (void __force *)dst, len);
	user_access_end();
	return sum;
}

/**
 * csum_partial_copy_nocheck - Copy and checksum.
 * @src: source address
 * @dst: destination address
 * @len: number of bytes to be copied.
 *
 * Returns an 32bit unfolded checksum of the buffer.
 */
__wsum
csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	return csum_partial_copy_generic(src, dst, len);
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);

