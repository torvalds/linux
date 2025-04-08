/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_STRING_H
#define _ASM_LKL_STRING_H

#include <asm/types.h>
#include <asm/host_ops.h>

/* use __mem* names to avoid conflict with KASAN's mem* functions. */

#ifdef CONFIG_LKL_HOST_MEMCPY
#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *dest, const void *src, size_t count);
static inline void *__memcpy(void *dest, const void *src, size_t count)
{
	return lkl_ops->memcpy(dest, src, count);
}
#define memcpy(dst, src, len) __memcpy(dst, src, len)
#endif

#ifdef CONFIG_LKL_HOST_MEMSET
#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t count);
static inline void *__memset(void *s, int c, size_t count)
{
	return lkl_ops->memset(s, c, count);
}
#define memset(s, c, n) __memset(s, c, n)
#endif

#ifdef CONFIG_LKL_HOST_MEMSET
#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *dest, const void *src, size_t count);
static inline void *__memmove(void *dest, const void *src, size_t count)
{
	return lkl_ops->memmove(dest, src, count);
}
#define memmove(dst, src, len) __memmove(dst, src, len)
#endif

#if defined(CONFIG_KASAN)

/*
 * For files that are not instrumented (e.g. mm/slub.c) we
 * should use not instrumented version of mem* functions.
 */
#if !defined(__SANITIZE_ADDRESS__)

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif

#else /* __SANITIZE_ADDRESS__ */

#undef memcpy
#undef memset
#undef memmove

#endif /* __SANITIZE_ADDRESS__ */

#endif /* CONFIG_KASAN */

#endif /* _ASM_LKL_STRING_H */
