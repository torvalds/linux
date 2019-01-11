/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_STRING_64_H
#define _ASM_X86_STRING_64_H

#ifdef __KERNEL__
#include <linux/jump_label.h>

/* Written 2002 by Andi Kleen */

/* Even with __builtin_ the compiler may decide to use the out of line
   function. */

#define __HAVE_ARCH_MEMCPY 1
extern void *memcpy(void *to, const void *from, size_t len);
extern void *__memcpy(void *to, const void *from, size_t len);

#ifndef CONFIG_FORTIFY_SOURCE
#if (__GNUC__ == 4 && __GNUC_MINOR__ < 3) || __GNUC__ < 4
#define memcpy(dst, src, len)					\
({								\
	size_t __len = (len);					\
	void *__ret;						\
	if (__builtin_constant_p(len) && __len >= 64)		\
		__ret = __memcpy((dst), (src), __len);		\
	else							\
		__ret = __builtin_memcpy((dst), (src), __len);	\
	__ret;							\
})
#endif
#endif /* !CONFIG_FORTIFY_SOURCE */

#define __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n);
void *__memset(void *s, int c, size_t n);

#define __HAVE_ARCH_MEMSET16
static inline void *memset16(uint16_t *s, uint16_t v, size_t n)
{
	long d0, d1;
	asm volatile("rep\n\t"
		     "stosw"
		     : "=&c" (d0), "=&D" (d1)
		     : "a" (v), "1" (s), "0" (n)
		     : "memory");
	return s;
}

#define __HAVE_ARCH_MEMSET32
static inline void *memset32(uint32_t *s, uint32_t v, size_t n)
{
	long d0, d1;
	asm volatile("rep\n\t"
		     "stosl"
		     : "=&c" (d0), "=&D" (d1)
		     : "a" (v), "1" (s), "0" (n)
		     : "memory");
	return s;
}

#define __HAVE_ARCH_MEMSET64
static inline void *memset64(uint64_t *s, uint64_t v, size_t n)
{
	long d0, d1;
	asm volatile("rep\n\t"
		     "stosq"
		     : "=&c" (d0), "=&D" (d1)
		     : "a" (v), "1" (s), "0" (n)
		     : "memory");
	return s;
}

#define __HAVE_ARCH_MEMMOVE
void *memmove(void *dest, const void *src, size_t count);
void *__memmove(void *dest, const void *src, size_t count);

int memcmp(const void *cs, const void *ct, size_t count);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
int strcmp(const char *cs, const char *ct);

#if defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__)

/*
 * For files that not instrumented (e.g. mm/slub.c) we
 * should use not instrumented version of mem* functions.
 */

#undef memcpy
#define memcpy(dst, src, len) __memcpy(dst, src, len)
#define memmove(dst, src, len) __memmove(dst, src, len)
#define memset(s, c, n) __memset(s, c, n)

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif

#endif

#define __HAVE_ARCH_MEMCPY_MCSAFE 1
__must_check unsigned long __memcpy_mcsafe(void *dst, const void *src,
		size_t cnt);
DECLARE_STATIC_KEY_FALSE(mcsafe_key);

/**
 * memcpy_mcsafe - copy memory with indication if a machine check happened
 *
 * @dst:	destination address
 * @src:	source address
 * @cnt:	number of bytes to copy
 *
 * Low level memory copy function that catches machine checks
 * We only call into the "safe" function on systems that can
 * actually do machine check recovery. Everyone else can just
 * use memcpy().
 *
 * Return 0 for success, or number of bytes not copied if there was an
 * exception.
 */
static __always_inline __must_check unsigned long
memcpy_mcsafe(void *dst, const void *src, size_t cnt)
{
#ifdef CONFIG_X86_MCE
	if (static_branch_unlikely(&mcsafe_key))
		return __memcpy_mcsafe(dst, src, cnt);
	else
#endif
		memcpy(dst, src, cnt);
	return 0;
}

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
#define __HAVE_ARCH_MEMCPY_FLUSHCACHE 1
void __memcpy_flushcache(void *dst, const void *src, size_t cnt);
static __always_inline void memcpy_flushcache(void *dst, const void *src, size_t cnt)
{
	if (__builtin_constant_p(cnt)) {
		switch (cnt) {
			case 4:
				asm ("movntil %1, %0" : "=m"(*(u32 *)dst) : "r"(*(u32 *)src));
				return;
			case 8:
				asm ("movntiq %1, %0" : "=m"(*(u64 *)dst) : "r"(*(u64 *)src));
				return;
			case 16:
				asm ("movntiq %1, %0" : "=m"(*(u64 *)dst) : "r"(*(u64 *)src));
				asm ("movntiq %1, %0" : "=m"(*(u64 *)(dst + 8)) : "r"(*(u64 *)(src + 8)));
				return;
		}
	}
	__memcpy_flushcache(dst, src, cnt);
}
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_STRING_64_H */
