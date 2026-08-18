// SPDX-License-Identifier: GPL-2.0
/*
 *    Optimized string functions
 *
 *  S390 version
 *    Copyright IBM Corp. 2004
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define IN_ARCH_STRING_C 1
#ifndef __NO_FORTIFY
# define __NO_FORTIFY
#endif

#include <linux/types.h>
#include <linux/string.h>
#include <linux/export.h>
#include <asm/facility.h>
#include <asm/asm.h>

#define SYMBOL_FUNCTION_ALIAS(alias, name)		\
asm(".globl " __stringify(alias) "\n\t"			\
    ".set   " __stringify(alias) "," __stringify(name))

#ifdef __HAVE_ARCH_MEMMOVE
noinstr void *__memmove(void *dest, const void *src, size_t n)
{
	const char *s = src;
	char *d = dest;

	if (!n)
		return dest;
	if ((d <= s || d >= s + n)) {
		/* Forward copy */
		while (n >= 256) {
			asm volatile(
				"	mvc	0(256,%[d]),0(%[s])\n"
				:
				: [d] "a" (d), [s] "a" (s)
				: "memory");
			d += 256;
			s += 256;
			n -= 256;
		}
		if (n) {
			asm volatile(
				"	exrl	%[n],0f\n"
				"	j	1f\n"
				"0:	mvc	0(1,%[d]),0(%[s])\n"
				"1:"
				:
				: [d] "a" (d), [s] "a" (s), [n] "a" (n - 1)
				: "memory");
		}
		return dest;
	}
	/* Backward copy */
	if (test_facility(61)) {
		/* Use mvcrl instruction if available */
		while (n >= 256) {
			asm volatile(
				"	lghi	%%r0,255\n"
				"	.insn	sse,0xe50a00000000,%[d],%[s]\n"
				: [d] "=Q" (*(d + n - 256))
				: [s] "Q" (*(s + n - 256))
				: "0", "memory");
			n -= 256;
		}
		if (n) {
			asm volatile(
				"	lgr	%%r0,%[n]\n"
				"	.insn	sse,0xe50a00000000,%[d],%[s]\n"
				: [d] "=Q" (*d)
				: [s] "Q" (*s), [n] "d" (n - 1)
				: "0", "memory");
		}
	} else {
		while (n--)
			d[n] = s[n];
	}
	return dest;
}
SYMBOL_FUNCTION_ALIAS(memmove, __memmove);
EXPORT_SYMBOL(__memmove);
EXPORT_SYMBOL(memmove);
#endif

#ifdef __HAVE_ARCH_MEMSET
noinstr void *__memset(void *s, int c, size_t n)
{
	char *xs = s;

	if (!n)
		return s;
	if (!c) {
		/* Clear memory */
		while (n >= 256) {
			asm volatile(
				"	xc	 0(256,%[xs]),0(%[xs])"
				:
				: [xs] "a" (xs)
				: "cc", "memory");
			xs += 256;
			n -= 256;
		}
		if (!n)
			return s;
		asm volatile(
			"	exrl	%[n],0f\n"
			"	j	1f\n"
			"0:	xc	0(1,%[xs]),0(%[xs])\n"
			"1:"
			:
			: [xs] "a" (xs), [n] "a" (n - 1)
			: "cc", "memory");
	} else {
		/* Fill memory */
		while (n >= 256) {
			*xs = c;
			asm volatile(
				"	mvc	1(255,%[xs]),0(%[xs])"
				:
				: [xs] "a" (xs)
				: "memory");
			xs += 256;
			n -= 256;
		}
		if (!n)
			return s;
		*xs = c;
		if (n == 1)
			return s;
		asm volatile(
			"	exrl	%[n],0f\n"
			"	j	1f\n"
			"0:	mvc	1(1,%[xs]),0(%[xs])\n"
			"1:"
			:
			: [xs] "a" (xs), [n] "a" (n - 2)
			: "memory");
	}
	return s;
}
SYMBOL_FUNCTION_ALIAS(memset, __memset);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(memset);
#endif

#ifdef __HAVE_ARCH_MEMCPY
noinstr void *__memcpy(void *dest, const void *src, size_t n)
{
	void *d = dest;

	if (!n)
		return d;
	while (n >= 256) {
		asm volatile(
			"	mvc	0(256,%[dest]),0(%[src])"
			:
			: [dest] "a" (dest), [src] "a" (src)
			: "memory");
		dest += 256;
		src += 256;
		n -= 256;
	}
	if (!n)
		return d;
	asm volatile(
		"	exrl	%[n],1f\n"
		"	j	2f\n"
		"1:	mvc	0(1,%[dest]),0(%[src])\n"
		"2:"
		:
		: [dest] "a" (dest), [src] "a" (src), [n] "a" (n - 1)
		: "memory");
	return d;
}
SYMBOL_FUNCTION_ALIAS(memcpy, __memcpy);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(memcpy);
#endif

#define DEFINE_MEMSET(_bits, _bytes, _type)					\
void *__memset##_bits(_type *s, _type v, size_t n)				\
{										\
	_type *xs = s;								\
										\
	if (!n)									\
		return s;							\
	while (n >= 256) {							\
		*xs = v;							\
		asm volatile(							\
			"	mvc	%[_b](256-%[_b],%[xs]),0(%[xs])\n"	\
			:							\
			: [xs] "a" (xs), [_b] "i" (_bytes)			\
			: "memory");						\
		xs = (_type *)((char *)xs + 256);				\
		n -= 256;							\
	}									\
	if (!n)									\
		return s;							\
	*xs = v;								\
	if (n == _bytes)							\
		return s;							\
	n -= _bytes + 1;							\
	asm volatile(								\
		"	exrl	 %[n],1f\n"					\
		"	j	 2f\n"						\
		"1:	mvc	 %[_b](1,%[xs]),0(%[xs])\n"			\
		"2:"								\
		:								\
		: [n] "a" (n), [xs] "a" (xs), [_b] "i" (_bytes)			\
		: "memory");							\
	return s;								\
}										\
EXPORT_SYMBOL(__memset##_bits)

#ifdef __HAVE_ARCH_MEMSET16
DEFINE_MEMSET(16, 2, uint16_t);
#endif

#ifdef __HAVE_ARCH_MEMSET32
DEFINE_MEMSET(32, 4, uint32_t);
#endif

#ifdef __HAVE_ARCH_MEMSET64
DEFINE_MEMSET(64, 8, uint64_t);
#endif

/*
 * Helper functions to find the end of a string
 */
static inline char *__strend(const char *s)
{
	unsigned long e = 0;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[e],%[s]\n"
		"	jo	0b"
		: [e] "+&a" (e), [s] "+&a" (s)
		:
		: "cc", "memory", "0");
	return (char *)e;
}

static inline char *__strnend(const char *s, size_t n)
{
	const char *p = s + n;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[p],%[s]\n"
		"	jo	0b"
		: [p] "+&d" (p), [s] "+&a" (s)
		:
		: "cc", "memory", "0");
	return (char *)p;
}

/**
 * strlen - Find the length of a string
 * @s: The string to be sized
 *
 * returns the length of @s
 */
#ifdef __HAVE_ARCH_STRLEN
size_t strlen(const char *s)
{
	return __strend(s) - s;
}
EXPORT_SYMBOL(strlen);
#endif

/**
 * strnlen - Find the length of a length-limited string
 * @s: The string to be sized
 * @n: The maximum number of bytes to search
 *
 * returns the minimum of the length of @s and @n
 */
#ifdef __HAVE_ARCH_STRNLEN
size_t strnlen(const char *s, size_t n)
{
	return __strnend(s, n) - s;
}
EXPORT_SYMBOL(strnlen);
#endif

/**
 * strcat - Append one %NUL-terminated string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 *
 * returns a pointer to @dest
 */
#ifdef __HAVE_ARCH_STRCAT
char *strcat(char *dest, const char *src)
{
	unsigned long dummy = 0;
	char *ret = dest;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[dummy],%[dest]\n"
		"	jo	0b\n"
		"1:	mvst	%[dummy],%[src]\n"
		"	jo	1b"
		: [dummy] "+&a" (dummy), [dest] "+&a" (dest), [src] "+&a" (src)
		:
		: "cc", "memory", "0");
	return ret;
}
EXPORT_SYMBOL(strcat);
#endif

/**
 * strncat - Append a length-limited, %NUL-terminated string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @n: The maximum numbers of bytes to copy
 *
 * returns a pointer to @dest
 */
#ifdef __HAVE_ARCH_STRNCAT
char *strncat(char *dest, const char *src, size_t n)
{
	size_t len = __strnend(src, n) - src;
	char *p = __strend(dest);

	p[len] = '\0';
	memcpy(p, src, len);
	return dest;
}
EXPORT_SYMBOL(strncat);
#endif

/**
 * strcmp - Compare two strings
 * @s1: One string
 * @s2: Another string
 *
 * returns   0 if @s1 and @s2 are equal,
 *	   < 0 if @s1 is less than @s2
 *	   > 0 if @s1 is greater than @s2
 */
#ifdef __HAVE_ARCH_STRCMP
int strcmp(const char *s1, const char *s2)
{
	int ret = 0;

	asm volatile(
		"	lghi	0,0\n"
		"0:	clst	%[s1],%[s2]\n"
		"	jo	0b\n"
		"	je	1f\n"
		"	ic	%[ret],0(%[s1])\n"
		"	ic	0,0(%[s2])\n"
		"	sr	%[ret],0\n"
		"1:"
		: [ret] "+&d" (ret), [s1] "+&a" (s1), [s2] "+&a" (s2)
		:
		: "cc", "memory", "0");
	return ret;
}
EXPORT_SYMBOL(strcmp);
#endif

static inline int clcle(const char *s1, unsigned long l1,
			const char *s2, unsigned long l2)
{
	union register_pair r1 = { .even = (unsigned long)s1, .odd = l1, };
	union register_pair r3 = { .even = (unsigned long)s2, .odd = l2, };
	int cc;

	asm volatile(
		"0:	clcle	%[r1],%[r3],0\n"
		"	jo	0b\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc), [r1] "+d" (r1.pair), [r3] "+d" (r3.pair)
		:
		: CC_CLOBBER_LIST("memory"));
	return CC_TRANSFORM(cc);
}

/**
 * strstr - Find the first substring in a %NUL terminated string
 * @s1: The string to be searched
 * @s2: The string to search for
 */
#ifdef __HAVE_ARCH_STRSTR
char *strstr(const char *s1, const char *s2)
{
	int l1, l2;

	l2 = __strend(s2) - s2;
	if (!l2)
		return (char *) s1;
	l1 = __strend(s1) - s1;
	while (l1-- >= l2) {
		int cc;

		cc = clcle(s1, l2, s2, l2);
		if (!cc)
			return (char *) s1;
		s1++;
	}
	return NULL;
}
EXPORT_SYMBOL(strstr);
#endif

/**
 * memchr - Find a character in an area of memory.
 * @s: The memory area
 * @c: The byte to search for
 * @n: The size of the area.
 *
 * returns the address of the first occurrence of @c, or %NULL
 * if @c is not found
 */
#ifdef __HAVE_ARCH_MEMCHR
void *memchr(const void *s, int c, size_t n)
{
	const void *ret = s + n;

	asm volatile(
		"	lgr	0,%[c]\n"
		"0:	srst	%[ret],%[s]\n"
		"	jo	0b\n"
		"	jl	1f\n"
		"	la	%[ret],0\n"
		"1:"
		: [ret] "+&a" (ret), [s] "+&a" (s)
		: [c] "d" (c)
		: "cc", "memory", "0");
	return (void *) ret;
}
EXPORT_SYMBOL(memchr);
#endif

/**
 * memcmp - Compare two areas of memory
 * @s1: One area of memory
 * @s2: Another area of memory
 * @n: The size of the area.
 */
#ifdef __HAVE_ARCH_MEMCMP
int memcmp(const void *s1, const void *s2, size_t n)
{
	int ret;

	ret = clcle(s1, n, s2, n);
	if (ret)
		ret = ret == 1 ? -1 : 1;
	return ret;
}
EXPORT_SYMBOL(memcmp);
#endif

/**
 * memscan - Find a character in an area of memory.
 * @s: The memory area
 * @c: The byte to search for
 * @n: The size of the area.
 *
 * returns the address of the first occurrence of @c, or 1 byte past
 * the area if @c is not found
 */
#ifdef __HAVE_ARCH_MEMSCAN
void *memscan(void *s, int c, size_t n)
{
	const void *ret = s + n;

	asm volatile(
		"	lgr	0,%[c]\n"
		"0:	srst	%[ret],%[s]\n"
		"	jo	0b"
		: [ret] "+&a" (ret), [s] "+&a" (s)
		: [c] "d" (c)
		: "cc", "memory", "0");
	return (void *)ret;
}
EXPORT_SYMBOL(memscan);
#endif
