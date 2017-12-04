// SPDX-License-Identifier: GPL-2.0
/*
 *	arch/alpha/lib/srm_puts.c
 */

#include <linux/string.h>
#include <asm/console.h>

long
srm_puts(const char *str, long len)
{
	long remaining, written;

	if (!callback_init_done)
		return len;

	for (remaining = len; remaining > 0; remaining -= written)
	{
		written = callback_puts(0, str, remaining);
		written &= 0xffffffff;
		str += written;
	}
	return len;
}
