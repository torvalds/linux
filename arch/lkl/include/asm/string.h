/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_STRING_H
#define _ASM_LKL_STRING_H

#include <asm/types.h>
#include <asm/host_ops.h>

#define __HAVE_ARCH_MEMCPY
static inline void *memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	/* from lib/string.c */
	if (lkl_ops->memcpy)
		return lkl_ops->memcpy(dest, src, count);

	while (count--)
		*tmp++ = *s++;
	return dest;
}

#endif /* _ASM_LKL_STRING_H */
