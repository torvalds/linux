/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_STRING_H
#define ___ASM_SPARC_STRING_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/string_64.h>
#else
#include <asm/string_32.h>
#endif

/* First the mem*() things. */
#define __HAVE_ARCH_MEMMOVE
void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCPY
#define memcpy(t, f, n) __builtin_memcpy(t, f, n)

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) __builtin_memset(s, c, count)

#define __HAVE_ARCH_MEMSCAN

#define memscan(__arg0, __char, __arg2)						\
({										\
	void *__memscan_zero(void *, size_t);					\
	void *__memscan_generic(void *, int, size_t);				\
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
int memcmp(const void *,const void *,__kernel_size_t);

/* Now the str*() stuff... */
#define __HAVE_ARCH_STRLEN
__kernel_size_t strlen(const char *);

#define __HAVE_ARCH_STRNCMP
int strncmp(const char *, const char *, __kernel_size_t);

#endif
