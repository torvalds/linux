#ifndef _ASM_X86_STRING_64_H
#define _ASM_X86_STRING_64_H

#ifdef __KERNEL__
#include <linux/jump_label.h>

/* Written 2002 by Andi Kleen */

/* Only used for special circumstances. Stolen from i386/string.h */
static __always_inline void *__inline_memcpy(void *to, const void *from, size_t n)
{
	unsigned long d0, d1, d2;
	asm volatile("rep ; movsl\n\t"
		     "testb $2,%b4\n\t"
		     "je 1f\n\t"
		     "movsw\n"
		     "1:\ttestb $1,%b4\n\t"
		     "je 2f\n\t"
		     "movsb\n"
		     "2:"
		     : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		     : "0" (n / 4), "q" (n), "1" ((long)to), "2" ((long)from)
		     : "memory");
	return to;
}

/* Even with __builtin_ the compiler may decide to use the out of line
   function. */

#define __HAVE_ARCH_MEMCPY 1
extern void *memcpy(void *to, const void *from, size_t len);
extern void *__memcpy(void *to, const void *from, size_t len);

#ifndef CONFIG_KMEMCHECK
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
#else
/*
 * kmemcheck becomes very happy if we use the REP instructions unconditionally,
 * because it means that we know both memory operands in advance.
 */
#define memcpy(dst, src, len) __inline_memcpy((dst), (src), (len))
#endif

#define __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n);
void *__memset(void *s, int c, size_t n);

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
#endif

#define __HAVE_ARCH_MEMCPY_MCSAFE 1
__must_check int memcpy_mcsafe_unrolled(void *dst, const void *src, size_t cnt);
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
 * Return 0 for success, -EFAULT for fail
 */
static __always_inline __must_check int
memcpy_mcsafe(void *dst, const void *src, size_t cnt)
{
#ifdef CONFIG_X86_MCE
	if (static_branch_unlikely(&mcsafe_key))
		return memcpy_mcsafe_unrolled(dst, src, cnt);
	else
#endif
		memcpy(dst, src, cnt);
	return 0;
}

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
#define __HAVE_ARCH_MEMCPY_FLUSHCACHE 1
void memcpy_flushcache(void *dst, const void *src, size_t cnt);
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_STRING_64_H */
