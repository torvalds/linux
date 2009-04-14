#ifndef _M68KNOMMU_STRING_H_
#define _M68KNOMMU_STRING_H_

#ifdef __KERNEL__ /* only set these up for kernel code */

#include <asm/setup.h>
#include <asm/page.h>

#define __HAVE_ARCH_STRCPY
static inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
        "jne 1b"
	: "=a" (dest), "=a" (src)
        : "0" (dest), "1" (src) : "memory");
  return xdest;
}

#define __HAVE_ARCH_STRNCPY
static inline char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"jeq 2f\n\t"
        "subql #1,%2\n\t"
        "jne 1b\n\t"
        "2:"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "memory");
  return xdest;
}


#ifndef CONFIG_COLDFIRE

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char * cs,const char * ct)
{
  char __res;

  __asm__
       ("1:\tmoveb %0@+,%2\n\t" /* get *cs */
        "cmpb %1@+,%2\n\t"      /* compare a byte */
        "jne  2f\n\t"           /* not equal, break out */
        "tstb %2\n\t"           /* at end of cs? */
        "jne  1b\n\t"           /* no, keep going */
        "jra  3f\n\t"		/* strings are equal */
        "2:\tsubb %1@-,%2\n\t"  /* *cs - *ct */
        "3:"
        : "=a" (cs), "=a" (ct), "=d" (__res)
        : "0" (cs), "1" (ct));

  return __res;
}

#define __HAVE_ARCH_STRNCMP
static inline int strncmp(const char * cs,const char * ct,size_t count)
{
  char __res;

  if (!count)
    return 0;
  __asm__
       ("1:\tmovb %0@+,%3\n\t"          /* get *cs */
        "cmpb   %1@+,%3\n\t"            /* compare a byte */
        "jne    3f\n\t"                 /* not equal, break out */
        "tstb   %3\n\t"                 /* at end of cs? */
        "jeq    4f\n\t"                 /* yes, all done */
        "subql  #1,%2\n\t"              /* no, adjust count */
        "jne    1b\n\t"                 /* more to do, keep going */
        "2:\tmoveq #0,%3\n\t"           /* strings are equal */
        "jra    4f\n\t"
        "3:\tsubb %1@-,%3\n\t"          /* *cs - *ct */
        "4:"
        : "=a" (cs), "=a" (ct), "=d" (count), "=d" (__res)
        : "0" (cs), "1" (ct), "2" (count));
  return __res;
}

#endif /* CONFIG_COLDFIRE */

#define __HAVE_ARCH_MEMSET
extern void * memset(void * s, int c, size_t count);

#define __HAVE_ARCH_MEMCPY
extern void * memcpy(void *d, const void *s, size_t count);

#else /* KERNEL */

/*
 *	let user libraries deal with these,
 *	IMHO the kernel has no place defining these functions for user apps
 */

#define __HAVE_ARCH_STRCPY 1
#define __HAVE_ARCH_STRNCPY 1
#define __HAVE_ARCH_STRCAT 1
#define __HAVE_ARCH_STRNCAT 1
#define __HAVE_ARCH_STRCMP 1
#define __HAVE_ARCH_STRNCMP 1
#define __HAVE_ARCH_STRNICMP 1
#define __HAVE_ARCH_STRCHR 1
#define __HAVE_ARCH_STRRCHR 1
#define __HAVE_ARCH_STRSTR 1
#define __HAVE_ARCH_STRLEN 1
#define __HAVE_ARCH_STRNLEN 1
#define __HAVE_ARCH_MEMSET 1
#define __HAVE_ARCH_MEMCPY 1
#define __HAVE_ARCH_MEMMOVE 1
#define __HAVE_ARCH_MEMSCAN 1
#define __HAVE_ARCH_MEMCMP 1
#define __HAVE_ARCH_MEMCHR 1
#define __HAVE_ARCH_STRTOK 1

#endif /* KERNEL */

#endif /* _M68K_STRING_H_ */
