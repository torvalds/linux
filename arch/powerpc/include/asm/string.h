/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_STRING_H
#define _ASM_POWERPC_STRING_H

#ifdef __KERNEL__

#ifndef CONFIG_KASAN
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRNCMP
#define __HAVE_ARCH_MEMCHR
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_MEMSET16
#endif

#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMCPY_FLUSHCACHE

extern char * strcpy(char *,const char *);
extern char * strncpy(char *,const char *, __kernel_size_t);
extern __kernel_size_t strlen(const char *);
extern int strcmp(const char *,const char *);
extern int strncmp(const char *, const char *, __kernel_size_t);
extern char * strcat(char *, const char *);
extern void * memset(void *,int,__kernel_size_t);
extern void * memcpy(void *,const void *,__kernel_size_t);
extern void * memmove(void *,const void *,__kernel_size_t);
extern int memcmp(const void *,const void *,__kernel_size_t);
extern void * memchr(const void *,int,__kernel_size_t);
void memcpy_flushcache(void *dest, const void *src, size_t size);

void *__memset(void *s, int c, __kernel_size_t count);
void *__memcpy(void *to, const void *from, __kernel_size_t n);
void *__memmove(void *to, const void *from, __kernel_size_t n);

#if defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__)
/*
 * For files that are not instrumented (e.g. mm/slub.c) we
 * should use not instrumented version of mem* functions.
 */
#define memcpy(dst, src, len) __memcpy(dst, src, len)
#define memmove(dst, src, len) __memmove(dst, src, len)
#define memset(s, c, n) __memset(s, c, n)

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif

#endif

#ifdef CONFIG_PPC64
#ifndef CONFIG_KASAN
#define __HAVE_ARCH_MEMSET32
#define __HAVE_ARCH_MEMSET64

extern void *__memset16(uint16_t *, uint16_t v, __kernel_size_t);
extern void *__memset32(uint32_t *, uint32_t v, __kernel_size_t);
extern void *__memset64(uint64_t *, uint64_t v, __kernel_size_t);

static inline void *memset16(uint16_t *p, uint16_t v, __kernel_size_t n)
{
	return __memset16(p, v, n * 2);
}

static inline void *memset32(uint32_t *p, uint32_t v, __kernel_size_t n)
{
	return __memset32(p, v, n * 4);
}

static inline void *memset64(uint64_t *p, uint64_t v, __kernel_size_t n)
{
	return __memset64(p, v, n * 8);
}
#endif
#else
#ifndef CONFIG_KASAN
#define __HAVE_ARCH_STRLEN
#endif

extern void *memset16(uint16_t *, uint16_t, __kernel_size_t);
#endif
#endif /* __KERNEL__ */

#endif	/* _ASM_POWERPC_STRING_H */
