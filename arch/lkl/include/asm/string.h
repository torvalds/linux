/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_STRING_H
#define _ASM_LKL_STRING_H

#include <asm/types.h>
#include <asm/host_ops.h>

/* use __mem* names to avoid conflict with KASAN's mem* functions. */

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *dest, const void *src, size_t count);
static inline void *__memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	if (lkl_ops->memcpy)
		return lkl_ops->memcpy(dest, src, count);

	/* from lib/string.c */

	while (count--)
		*tmp++ = *s++;
	return dest;
}

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t count);
static inline void *__memset(void *s, int c, size_t count)
{
	char *xs = s;

	if (lkl_ops->memset)
		return lkl_ops->memset(s, c, count);

	/* from lib/string.c */

	while (count--)
		*xs++ = c;
	return s;
}

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *dest, const void *src, size_t count);
static inline void *__memmove(void *dest, const void *src, size_t count)
{
	char *tmp;
	const char *s;

	if (lkl_ops->memmove)
		return lkl_ops->memmove(dest, src, count);

	/* from lib/string.c */

	if (dest <= src) {
		tmp = dest;
		s = src;
		while (count--)
			*tmp++ = *s++;
	} else {
		tmp = dest;
		tmp += count;
		s = src;
		s += count;
		while (count--)
			*--tmp = *--s;
	}
	return dest;
}

#define memcpy(dst, src, len) __memcpy(dst, src, len)
#define memset(s, c, n) __memset(s, c, n)
#define memmove(dst, src, len) __memmove(dst, src, len)

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
