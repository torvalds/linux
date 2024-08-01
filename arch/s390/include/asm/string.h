/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#ifndef _S390_STRING_H_
#define _S390_STRING_H_

#ifndef _LINUX_TYPES_H
#include <linux/types.h>
#endif

#define __HAVE_ARCH_MEMCPY	/* gcc builtin & arch function */
#define __HAVE_ARCH_MEMMOVE	/* gcc builtin & arch function */
#define __HAVE_ARCH_MEMSET	/* gcc builtin & arch function */

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);

#if !defined(CONFIG_KASAN) && !defined(CONFIG_KMSAN)
#define __HAVE_ARCH_MEMCHR	/* inline & arch function */
#define __HAVE_ARCH_MEMCMP	/* arch function */
#define __HAVE_ARCH_MEMSCAN	/* inline & arch function */
#define __HAVE_ARCH_STRCAT	/* inline & arch function */
#define __HAVE_ARCH_STRCMP	/* arch function */
#define __HAVE_ARCH_STRCPY	/* inline & arch function */
#define __HAVE_ARCH_STRLCAT	/* arch function */
#define __HAVE_ARCH_STRLEN	/* inline & arch function */
#define __HAVE_ARCH_STRNCAT	/* arch function */
#define __HAVE_ARCH_STRNCPY	/* arch function */
#define __HAVE_ARCH_STRNLEN	/* inline & arch function */
#define __HAVE_ARCH_STRSTR	/* arch function */
#define __HAVE_ARCH_MEMSET16	/* arch function */
#define __HAVE_ARCH_MEMSET32	/* arch function */
#define __HAVE_ARCH_MEMSET64	/* arch function */

/* Prototypes for non-inlined arch strings functions. */
int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);
size_t strlcat(char *dest, const char *src, size_t n);
char *strncat(char *dest, const char *src, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
char *strstr(const char *s1, const char *s2);
#endif /* !defined(CONFIG_KASAN) && !defined(CONFIG_KMSAN) */

#undef __HAVE_ARCH_STRCHR
#undef __HAVE_ARCH_STRNCHR
#undef __HAVE_ARCH_STRNCMP
#undef __HAVE_ARCH_STRPBRK
#undef __HAVE_ARCH_STRSEP
#undef __HAVE_ARCH_STRSPN

#if defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__)

#define strlen(s) __strlen(s)

#define __no_sanitize_prefix_strfunc(x) __##x

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif

#else
#define __no_sanitize_prefix_strfunc(x) x
#endif /* defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__) */

void *__memcpy(void *dest, const void *src, size_t n);
void *__memset(void *s, int c, size_t n);
void *__memmove(void *dest, const void *src, size_t n);
void *__memset16(uint16_t *s, uint16_t v, size_t count);
void *__memset32(uint32_t *s, uint32_t v, size_t count);
void *__memset64(uint64_t *s, uint64_t v, size_t count);

#ifdef __HAVE_ARCH_MEMSET16
static inline void *memset16(uint16_t *s, uint16_t v, size_t count)
{
	return __memset16(s, v, count * sizeof(v));
}
#endif

#ifdef __HAVE_ARCH_MEMSET32
static inline void *memset32(uint32_t *s, uint32_t v, size_t count)
{
	return __memset32(s, v, count * sizeof(v));
}
#endif

#ifdef __HAVE_ARCH_MEMSET64
#ifdef IN_BOOT_STRING_C
void *memset64(uint64_t *s, uint64_t v, size_t count);
#else
static inline void *memset64(uint64_t *s, uint64_t v, size_t count)
{
	return __memset64(s, v, count * sizeof(v));
}
#endif
#endif

#if !defined(IN_ARCH_STRING_C) && (!defined(CONFIG_FORTIFY_SOURCE) || defined(__NO_FORTIFY))

#ifdef __HAVE_ARCH_MEMCHR
static inline void *memchr(const void * s, int c, size_t n)
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
#endif

#ifdef __HAVE_ARCH_MEMSCAN
static inline void *memscan(void *s, int c, size_t n)
{
	const void *ret = s + n;

	asm volatile(
		"	lgr	0,%[c]\n"
		"0:	srst	%[ret],%[s]\n"
		"	jo	0b\n"
		: [ret] "+&a" (ret), [s] "+&a" (s)
		: [c] "d" (c)
		: "cc", "memory", "0");
	return (void *) ret;
}
#endif

#ifdef __HAVE_ARCH_STRCAT
static inline char *strcat(char *dst, const char *src)
{
	unsigned long dummy = 0;
	char *ret = dst;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[dummy],%[dst]\n"
		"	jo	0b\n"
		"1:	mvst	%[dummy],%[src]\n"
		"	jo	1b"
		: [dummy] "+&a" (dummy), [dst] "+&a" (dst), [src] "+&a" (src)
		:
		: "cc", "memory", "0");
	return ret;
}
#endif

#ifdef __HAVE_ARCH_STRCPY
static inline char *strcpy(char *dst, const char *src)
{
	char *ret = dst;

	asm volatile(
		"	lghi	0,0\n"
		"0:	mvst	%[dst],%[src]\n"
		"	jo	0b"
		: [dst] "+&a" (dst), [src] "+&a" (src)
		:
		: "cc", "memory", "0");
	return ret;
}
#endif

#if defined(__HAVE_ARCH_STRLEN) || (defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__))
static inline size_t __no_sanitize_prefix_strfunc(strlen)(const char *s)
{
	unsigned long end = 0;
	const char *tmp = s;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[end],%[tmp]\n"
		"	jo	0b"
		: [end] "+&a" (end), [tmp] "+&a" (tmp)
		:
		: "cc", "memory", "0");
	return end - (unsigned long)s;
}
#endif

#ifdef __HAVE_ARCH_STRNLEN
static inline size_t strnlen(const char * s, size_t n)
{
	const char *tmp = s;
	const char *end = s + n;

	asm volatile(
		"	lghi	0,0\n"
		"0:	srst	%[end],%[tmp]\n"
		"	jo	0b"
		: [end] "+&a" (end), [tmp] "+&a" (tmp)
		:
		: "cc", "memory", "0");
	return end - s;
}
#endif
#else /* IN_ARCH_STRING_C */
void *memchr(const void * s, int c, size_t n);
void *memscan(void *s, int c, size_t n);
char *strcat(char *dst, const char *src);
char *strcpy(char *dst, const char *src);
size_t strlen(const char *s);
size_t strnlen(const char * s, size_t n);
#endif /* !IN_ARCH_STRING_C */

#endif /* __S390_STRING_H_ */
