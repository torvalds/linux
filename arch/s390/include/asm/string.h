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

#define __HAVE_ARCH_MEMCHR	/* inline & arch function */
#define __HAVE_ARCH_MEMCMP	/* arch function */
#define __HAVE_ARCH_MEMCPY	/* gcc builtin & arch function */
#define __HAVE_ARCH_MEMMOVE	/* gcc builtin & arch function */
#define __HAVE_ARCH_MEMSCAN	/* inline & arch function */
#define __HAVE_ARCH_MEMSET	/* gcc builtin & arch function */
#define __HAVE_ARCH_STRCAT	/* inline & arch function */
#define __HAVE_ARCH_STRCMP	/* arch function */
#define __HAVE_ARCH_STRCPY	/* inline & arch function */
#define __HAVE_ARCH_STRLCAT	/* arch function */
#define __HAVE_ARCH_STRLCPY	/* arch function */
#define __HAVE_ARCH_STRLEN	/* inline & arch function */
#define __HAVE_ARCH_STRNCAT	/* arch function */
#define __HAVE_ARCH_STRNCPY	/* arch function */
#define __HAVE_ARCH_STRNLEN	/* inline & arch function */
#define __HAVE_ARCH_STRRCHR	/* arch function */
#define __HAVE_ARCH_STRSTR	/* arch function */

/* Prototypes for non-inlined arch strings functions. */
extern int memcmp(const void *, const void *, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void *memmove(void *, const void *, size_t);
extern int strcmp(const char *,const char *);
extern size_t strlcat(char *, const char *, size_t);
extern size_t strlcpy(char *, const char *, size_t);
extern char *strncat(char *, const char *, size_t);
extern char *strncpy(char *, const char *, size_t);
extern char *strrchr(const char *, int);
extern char *strstr(const char *, const char *);

#undef __HAVE_ARCH_STRCHR
#undef __HAVE_ARCH_STRNCHR
#undef __HAVE_ARCH_STRNCMP
#undef __HAVE_ARCH_STRPBRK
#undef __HAVE_ARCH_STRSEP
#undef __HAVE_ARCH_STRSPN

#if !defined(IN_ARCH_STRING_C)

static inline void *memchr(const void * s, int c, size_t n)
{
	register int r0 asm("0") = (char) c;
	const void *ret = s + n;

	asm volatile(
		"0:	srst	%0,%1\n"
		"	jo	0b\n"
		"	jl	1f\n"
		"	la	%0,0\n"
		"1:"
		: "+a" (ret), "+&a" (s) : "d" (r0) : "cc", "memory");
	return (void *) ret;
}

static inline void *memscan(void *s, int c, size_t n)
{
	register int r0 asm("0") = (char) c;
	const void *ret = s + n;

	asm volatile(
		"0:	srst	%0,%1\n"
		"	jo	0b\n"
		: "+a" (ret), "+&a" (s) : "d" (r0) : "cc", "memory");
	return (void *) ret;
}

static inline char *strcat(char *dst, const char *src)
{
	register int r0 asm("0") = 0;
	unsigned long dummy;
	char *ret = dst;

	asm volatile(
		"0:	srst	%0,%1\n"
		"	jo	0b\n"
		"1:	mvst	%0,%2\n"
		"	jo	1b"
		: "=&a" (dummy), "+a" (dst), "+a" (src)
		: "d" (r0), "0" (0) : "cc", "memory" );
	return ret;
}

static inline char *strcpy(char *dst, const char *src)
{
	register int r0 asm("0") = 0;
	char *ret = dst;

	asm volatile(
		"0:	mvst	%0,%1\n"
		"	jo	0b"
		: "+&a" (dst), "+&a" (src) : "d" (r0)
		: "cc", "memory");
	return ret;
}

static inline size_t strlen(const char *s)
{
	register unsigned long r0 asm("0") = 0;
	const char *tmp = s;

	asm volatile(
		"0:	srst	%0,%1\n"
		"	jo	0b"
		: "+d" (r0), "+a" (tmp) :  : "cc", "memory");
	return r0 - (unsigned long) s;
}

static inline size_t strnlen(const char * s, size_t n)
{
	register int r0 asm("0") = 0;
	const char *tmp = s;
	const char *end = s + n;

	asm volatile(
		"0:	srst	%0,%1\n"
		"	jo	0b"
		: "+a" (end), "+a" (tmp) : "d" (r0)  : "cc", "memory");
	return end - s;
}
#else /* IN_ARCH_STRING_C */
void *memchr(const void * s, int c, size_t n);
void *memscan(void *s, int c, size_t n);
char *strcat(char *dst, const char *src);
char *strcpy(char *dst, const char *src);
size_t strlen(const char *s);
size_t strnlen(const char * s, size_t n);
#endif /* !IN_ARCH_STRING_C */

#endif /* __S390_STRING_H_ */
