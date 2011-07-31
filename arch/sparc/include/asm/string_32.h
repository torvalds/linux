/*
 * string.h: External definitions for optimized assembly string
 *           routines for the Linux Kernel.
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef __SPARC_STRING_H__
#define __SPARC_STRING_H__

#include <asm/page.h>

/* Really, userland/ksyms should not see any of this stuff. */

#ifdef __KERNEL__

extern void __memmove(void *,const void *,__kernel_size_t);

#ifndef EXPORT_SYMTAB_STROPS

/* First the mem*() things. */
#define __HAVE_ARCH_MEMMOVE
#undef memmove
#define memmove(_to, _from, _n) \
({ \
	void *_t = (_to); \
	__memmove(_t, (_from), (_n)); \
	_t; \
})

#define __HAVE_ARCH_MEMCPY
#define memcpy(t, f, n) __builtin_memcpy(t, f, n)

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) __builtin_memset(s, c, count)

#define __HAVE_ARCH_MEMSCAN

#undef memscan
#define memscan(__arg0, __char, __arg2)						\
({										\
	extern void *__memscan_zero(void *, size_t);				\
	extern void *__memscan_generic(void *, int, size_t);			\
	void *__retval, *__addr = (__arg0);					\
	size_t __size = (__arg2);						\
										\
	if(__builtin_constant_p(__char) && !(__char))				\
		__retval = __memscan_zero(__addr, __size);			\
	else									\
		__retval = __memscan_generic(__addr, (__char), __size);		\
										\
	__retval;								\
})

#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t);

/* Now the str*() stuff... */
#define __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *);

#define __HAVE_ARCH_STRNCMP

extern int __strncmp(const char *, const char *, __kernel_size_t);

static inline int __constant_strncmp(const char *src, const char *dest, __kernel_size_t count)
{
	register int retval;
	switch(count) {
	case 0: return 0;
	case 1: return (src[0] - dest[0]);
	case 2: retval = (src[0] - dest[0]);
		if(!retval && src[0])
		  retval = (src[1] - dest[1]);
		return retval;
	case 3: retval = (src[0] - dest[0]);
		if(!retval && src[0]) {
		  retval = (src[1] - dest[1]);
		  if(!retval && src[1])
		    retval = (src[2] - dest[2]);
		}
		return retval;
	case 4: retval = (src[0] - dest[0]);
		if(!retval && src[0]) {
		  retval = (src[1] - dest[1]);
		  if(!retval && src[1]) {
		    retval = (src[2] - dest[2]);
		    if (!retval && src[2])
		      retval = (src[3] - dest[3]);
		  }
		}
		return retval;
	case 5: retval = (src[0] - dest[0]);
		if(!retval && src[0]) {
		  retval = (src[1] - dest[1]);
		  if(!retval && src[1]) {
		    retval = (src[2] - dest[2]);
		    if (!retval && src[2]) {
		      retval = (src[3] - dest[3]);
		      if (!retval && src[3])
		        retval = (src[4] - dest[4]);
		    }
		  }
		}
		return retval;
	default:
		retval = (src[0] - dest[0]);
		if(!retval && src[0]) {
		  retval = (src[1] - dest[1]);
		  if(!retval && src[1]) {
		    retval = (src[2] - dest[2]);
		    if(!retval && src[2])
		      retval = __strncmp(src+3,dest+3,count-3);
		  }
		}
		return retval;
	}
}

#undef strncmp
#define strncmp(__arg0, __arg1, __arg2)	\
(__builtin_constant_p(__arg2) ?	\
 __constant_strncmp(__arg0, __arg1, __arg2) : \
 __strncmp(__arg0, __arg1, __arg2))

#endif /* !EXPORT_SYMTAB_STROPS */

#endif /* __KERNEL__ */

#endif /* !(__SPARC_STRING_H__) */
