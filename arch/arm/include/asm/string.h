/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_STRING_H
#define __ASM_ARM_STRING_H

/*
 * We don't do inline string functions, since the
 * optimised inline asm versions are not small.
 */

#define __HAVE_ARCH_STRRCHR
extern char * strrchr(const char * s, int c);

#define __HAVE_ARCH_STRCHR
extern char * strchr(const char * s, int c);

#define __HAVE_ARCH_MEMCPY
extern void * memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMMOVE
extern void * memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCHR
extern void * memchr(const void *, int, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void * memset(void *, int, __kernel_size_t);

#define __HAVE_ARCH_MEMSET32
extern void *__memset32(uint32_t *, uint32_t v, __kernel_size_t);
static inline void *memset32(uint32_t *p, uint32_t v, __kernel_size_t n)
{
	return __memset32(p, v, n * 4);
}

#define __HAVE_ARCH_MEMSET64
extern void *__memset64(uint64_t *, uint32_t low, __kernel_size_t, uint32_t hi);
static inline void *memset64(uint64_t *p, uint64_t v, __kernel_size_t n)
{
	return __memset64(p, v, n * 8, v >> 32);
}

#endif
