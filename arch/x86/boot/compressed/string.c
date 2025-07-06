// SPDX-License-Identifier: GPL-2.0
/*
 * This provides an optimized implementation of memcpy, and a simplified
 * implementation of memset and memmove. These are used here because the
 * standard kernel runtime versions are not yet available and we don't
 * trust the gcc built-in implementations as they may do unexpected things
 * (e.g. FPU ops) in the minimal decompression stub execution environment.
 */
#include "error.h"

#include "../string.c"

#ifdef CONFIG_X86_32
static void *____memcpy(void *dest, const void *src, size_t n)
{
	int d0, d1, d2;
	asm volatile(
		"rep movsl\n\t"
		"movl %4,%%ecx\n\t"
		"rep movsb"
		: "=&c" (d0), "=&D" (d1), "=&S" (d2)
		: "0" (n >> 2), "g" (n & 3), "1" (dest), "2" (src)
		: "memory");

	return dest;
}
#else
static void *____memcpy(void *dest, const void *src, size_t n)
{
	long d0, d1, d2;
	asm volatile(
		"rep movsq\n\t"
		"movq %4,%%rcx\n\t"
		"rep movsb"
		: "=&c" (d0), "=&D" (d1), "=&S" (d2)
		: "0" (n >> 3), "g" (n & 7), "1" (dest), "2" (src)
		: "memory");

	return dest;
}
#endif

void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (d <= s || d - s >= n)
		return ____memcpy(dest, src, n);

	while (n-- > 0)
		d[n] = s[n];

	return dest;
}

/* Detect and warn about potential overlaps, but handle them with memmove. */
void *memcpy(void *dest, const void *src, size_t n)
{
	if (dest > src && dest - src < n) {
		warn("Avoiding potentially unsafe overlapping memcpy()!");
		return memmove(dest, src, n);
	}
	return ____memcpy(dest, src, n);
}

#ifdef CONFIG_KASAN
extern void *__memset(void *s, int c, size_t n) __alias(memset);
extern void *__memmove(void *dest, const void *src, size_t n) __alias(memmove);
extern void *__memcpy(void *dest, const void *src, size_t n) __alias(memcpy);
#endif
