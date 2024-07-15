// SPDX-License-Identifier: GPL-2.0
/*
 * arch/mips/boot/compressed/string.c
 *
 * Very small subset of simple string routines
 */

#include <linux/compiler_attributes.h>
#include <linux/types.h>
#include <asm/string.h>

void *memcpy(void *dest, const void *src, size_t n)
{
	int i;
	const char *s = src;
	char *d = dest;

	for (i = 0; i < n; i++)
		d[i] = s[i];
	return dest;
}

void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

void * __weak memmove(void *dest, const void *src, size_t n)
{
	unsigned int i;
	const char *s = src;
	char *d = dest;

	if ((uintptr_t)dest < (uintptr_t)src) {
		for (i = 0; i < n; i++)
			d[i] = s[i];
	} else {
		for (i = n; i > 0; i--)
			d[i - 1] = s[i - 1];
	}
	return dest;
}
