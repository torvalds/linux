/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_STRING_H_
#define _M68K_STRING_H_

#include <linux/types.h>
#include <linux/compiler.h>

#define __HAVE_ARCH_STRNLEN
static inline size_t strnlen(const char *s, size_t count)
{
	const char *sc = s;

	asm volatile ("\n"
		"1:     subq.l  #1,%1\n"
		"       jcs     2f\n"
		"       tst.b   (%0)+\n"
		"       jne     1b\n"
		"       subq.l  #1,%0\n"
		"2:"
		: "+a" (sc), "+d" (count));
	return sc - s;
}

#define __HAVE_ARCH_STRNCPY
static inline char *strncpy(char *dest, const char *src, size_t n)
{
	char *xdest = dest;

	asm volatile ("\n"
		"	jra	2f\n"
		"1:	move.b	(%1),(%0)+\n"
		"	jeq	2f\n"
		"	addq.l	#1,%1\n"
		"2:	subq.l	#1,%2\n"
		"	jcc	1b\n"
		: "+a" (dest), "+a" (src), "+d" (n)
		: : "memory");
	return xdest;
}

#ifndef CONFIG_COLDFIRE
#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char *cs, const char *ct)
{
	char res;

	asm ("\n"
		"1:	move.b	(%0)+,%2\n"	/* get *cs */
		"	cmp.b	(%1)+,%2\n"	/* compare a byte */
		"	jne	2f\n"		/* not equal, break out */
		"	tst.b	%2\n"		/* at end of cs? */
		"	jne	1b\n"		/* no, keep going */
		"	jra	3f\n"		/* strings are equal */
		"2:	sub.b	-(%1),%2\n"	/* *cs - *ct */
		"3:"
		: "+a" (cs), "+a" (ct), "=d" (res));
	return res;
}
#endif /* CONFIG_COLDFIRE */

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define memcmp(d, s, n) __builtin_memcmp(d, s, n)

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int, __kernel_size_t);
#define memset(d, c, n) __builtin_memset(d, c, n)

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);
#define memcpy(d, s, n) __builtin_memcpy(d, s, n)

#endif /* _M68K_STRING_H_ */
