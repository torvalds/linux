/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_STRING_H
#define __ASM_SH_STRING_H

/*
 * Copyright (C) 1999 Niibe Yutaka
 * But consider these trivial functions to be public domain.
 */

#define __HAVE_ARCH_STRCPY
static inline char *strcpy(char *__dest, const char *__src)
{
	register char *__xdest = __dest;
	unsigned long __dummy;

	__asm__ __volatile__("1:\n\t"
			     "mov.b	@%1+, %2\n\t"
			     "mov.b	%2, @%0\n\t"
			     "cmp/eq	#0, %2\n\t"
			     "bf/s	1b\n\t"
			     " add	#1, %0\n\t"
			     : "=r" (__dest), "=r" (__src), "=&z" (__dummy)
			     : "0" (__dest), "1" (__src)
			     : "memory", "t");

	return __xdest;
}

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char *__cs, const char *__ct)
{
	register int __res;
	unsigned long __dummy;

	__asm__ __volatile__(
		"mov.b	@%1+, %3\n"
		"1:\n\t"
		"mov.b	@%0+, %2\n\t"
		"cmp/eq #0, %3\n\t"
		"bt	2f\n\t"
		"cmp/eq %2, %3\n\t"
		"bt/s	1b\n\t"
		" mov.b	@%1+, %3\n\t"
		"add	#-2, %1\n\t"
		"mov.b	@%1, %3\n\t"
		"sub	%3, %2\n"
		"2:"
		: "=r" (__cs), "=r" (__ct), "=&r" (__res), "=&z" (__dummy)
		: "0" (__cs), "1" (__ct)
		: "t");

	return __res;
}

#define __HAVE_ARCH_STRNCMP
static inline int strncmp(const char *__cs, const char *__ct, size_t __n)
{
	register int __res;
	unsigned long __dummy;

	if (__n == 0)
		return 0;

	__asm__ __volatile__(
		"mov.b	@%1+, %3\n"
		"1:\n\t"
		"mov.b	@%0+, %2\n\t"
		"cmp/eq %6, %0\n\t"
		"bt/s	2f\n\t"
		" cmp/eq #0, %3\n\t"
		"bt/s	3f\n\t"
		" cmp/eq %3, %2\n\t"
		"bt/s	1b\n\t"
		" mov.b	@%1+, %3\n\t"
		"add	#-2, %1\n\t"
		"mov.b	@%1, %3\n"
		"2:\n\t"
		"sub	%3, %2\n"
		"3:"
		:"=r" (__cs), "=r" (__ct), "=&r" (__res), "=&z" (__dummy)
		: "0" (__cs), "1" (__ct), "r" (__cs+__n)
		: "t");

	return __res;
}

#define __HAVE_ARCH_MEMSET
extern void *memset(void *__s, int __c, size_t __count);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *__to, __const__ void *__from, size_t __n);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *__dest, __const__ void *__src, size_t __n);

#define __HAVE_ARCH_MEMCHR
extern void *memchr(const void *__s, int __c, size_t __n);

#define __HAVE_ARCH_STRLEN
extern size_t strlen(const char *);

#endif /* __ASM_SH_STRING_H */
