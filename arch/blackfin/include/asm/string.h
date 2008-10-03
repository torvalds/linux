#ifndef _BLACKFIN_STRING_H_
#define _BLACKFIN_STRING_H_

#include <linux/types.h>

#ifdef __KERNEL__		/* only set these up for kernel code */

#define __HAVE_ARCH_STRCPY
extern inline char *strcpy(char *dest, const char *src)
{
	char *xdest = dest;
	char temp = 0;

	__asm__ __volatile__ (
		"1:"
		"%2 = B [%1++] (Z);"
		"B [%0++] = %2;"
		"CC = %2;"
		"if cc jump 1b (bp);"
		: "+&a" (dest), "+&a" (src), "=&d" (temp)
		:
		: "memory", "CC");

	return xdest;
}

#define __HAVE_ARCH_STRNCPY
extern inline char *strncpy(char *dest, const char *src, size_t n)
{
	char *xdest = dest;
	char temp = 0;

	if (n == 0)
		return xdest;

	__asm__ __volatile__ (
		"1:"
		"%3 = B [%1++] (Z);"
		"B [%0++] = %3;"
		"CC = %3;"
		"if ! cc jump 2f;"
		"%2 += -1;"
		"CC = %2 == 0;"
		"if ! cc jump 1b (bp);"
		"jump 4f;"
		"2:"
		/* if src is shorter than n, we need to null pad bytes now */
		"%3 = 0;"
		"3:"
		"%2 += -1;"
		"CC = %2 == 0;"
		"if cc jump 4f;"
		"B [%0++] = %3;"
		"jump 3b;"
		"4:"
		: "+&a" (dest), "+&a" (src), "+&da" (n), "=&d" (temp)
		:
		: "memory", "CC");

	return xdest;
}

#define __HAVE_ARCH_STRCMP
extern inline int strcmp(const char *cs, const char *ct)
{
	/* need to use int's here so the char's in the assembly don't get
	 * sign extended incorrectly when we don't want them to be
	 */
	int __res1, __res2;

	__asm__ __volatile__ (
		"1:"
		"%2 = B[%0++] (Z);"      /* get *cs */
		"%3 = B[%1++] (Z);"      /* get *ct */
		"CC = %2 == %3;"         /* compare a byte */
		"if ! cc jump 2f;"       /* not equal, break out */
		"CC = %2;"               /* at end of cs? */
		"if cc jump 1b (bp);"    /* no, keep going */
		"jump.s 3f;"             /* strings are equal */
		"2:"
		"%2 = %2 - %3;"          /* *cs - *ct */
		"3:"
		: "+&a" (cs), "+&a" (ct), "=&d" (__res1), "=&d" (__res2)
		:
		: "memory", "CC");

	return __res1;
}

#define __HAVE_ARCH_STRNCMP
extern inline int strncmp(const char *cs, const char *ct, size_t count)
{
	/* need to use int's here so the char's in the assembly don't get
	 * sign extended incorrectly when we don't want them to be
	 */
	int __res1, __res2;

	if (!count)
		return 0;

	__asm__ __volatile__ (
		"1:"
		"%3 = B[%0++] (Z);"      /* get *cs */
		"%4 = B[%1++] (Z);"      /* get *ct */
		"CC = %3 == %4;"         /* compare a byte */
		"if ! cc jump 3f;"       /* not equal, break out */
		"CC = %3;"               /* at end of cs? */
		"if ! cc jump 4f;"       /* yes, all done */
		"%2 += -1;"              /* no, adjust count */
		"CC = %2 == 0;"
		"if ! cc jump 1b;"       /* more to do, keep going */
		"2:"
		"%3 = 0;"                /* strings are equal */
		"jump.s 4f;"
		"3:"
		"%3 = %3 - %4;"          /* *cs - *ct */
		"4:"
		: "+&a" (cs), "+&a" (ct), "+&da" (count), "=&d" (__res1), "=&d" (__res2)
		:
		: "memory", "CC");

	return __res1;
}

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t count);
#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *d, const void *s, size_t count);
#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *, const void *, __kernel_size_t);
#define	__HAVE_ARCH_MEMCHR
extern void *memchr(const void *s, int c, size_t n);
#define	__HAVE_ARCH_MEMMOVE
extern void *memmove(void *dest, const void *src, size_t count);

#endif /*__KERNEL__*/
#endif				/* _BLACKFIN_STRING_H_ */
