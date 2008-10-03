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
extern __kernel_size_t __memcpy(void *,const void *,__kernel_size_t);
extern __kernel_size_t __memset(void *,int,__kernel_size_t);

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

static inline void *__constant_memcpy(void *to, const void *from, __kernel_size_t n)
{
	extern void __copy_1page(void *, const void *);

	if(n <= 32) {
		__builtin_memcpy(to, from, n);
	} else if (((unsigned int) to & 7) != 0) {
		/* Destination is not aligned on the double-word boundary */
		__memcpy(to, from, n);
	} else {
		switch(n) {
		case PAGE_SIZE:
			__copy_1page(to, from);
			break;
		default:
			__memcpy(to, from, n);
			break;
		}
	}
	return to;
}

static inline void *__nonconstant_memcpy(void *to, const void *from, __kernel_size_t n)
{
	__memcpy(to, from, n);
	return to;
}

#undef memcpy
#define memcpy(t, f, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy((t),(f),(n)) : \
 __nonconstant_memcpy((t),(f),(n)))

#define __HAVE_ARCH_MEMSET

static inline void *__constant_c_and_count_memset(void *s, char c, __kernel_size_t count)
{
	extern void bzero_1page(void *);
	extern __kernel_size_t __bzero(void *, __kernel_size_t);

	if(!c) {
		if(count == PAGE_SIZE)
			bzero_1page(s);
		else
			__bzero(s, count);
	} else {
		__memset(s, c, count);
	}
	return s;
}

static inline void *__constant_c_memset(void *s, char c, __kernel_size_t count)
{
	extern __kernel_size_t __bzero(void *, __kernel_size_t);

	if(!c)
		__bzero(s, count);
	else
		__memset(s, c, count);
	return s;
}

static inline void *__nonconstant_memset(void *s, char c, __kernel_size_t count)
{
	__memset(s, c, count);
	return s;
}

#undef memset
#define memset(s, c, count) \
(__builtin_constant_p(c) ? (__builtin_constant_p(count) ? \
                            __constant_c_and_count_memset((s), (c), (count)) : \
                            __constant_c_memset((s), (c), (count))) \
                          : __nonconstant_memset((s), (c), (count)))

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
