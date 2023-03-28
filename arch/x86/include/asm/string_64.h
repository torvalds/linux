/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_STRING_64_H
#define _ASM_X86_STRING_64_H

#ifdef __KERNEL__
#include <linux/jump_label.h>

/* Written 2002 by Andi Kleen */

/* Even with __builtin_ the compiler may decide to use the out of line
   function. */

#if defined(__SANITIZE_MEMORY__) && defined(__NO_FORTIFY)
#include <linux/kmsan_string.h>
#endif

#define __HAVE_ARCH_MEMCPY 1
#if defined(__SANITIZE_MEMORY__) && defined(__NO_FORTIFY)
#undef memcpy
#define memcpy __msan_memcpy
#else
extern void *memcpy(void *to, const void *from, size_t len);
#endif
extern void *__memcpy(void *to, const void *from, size_t len);

#define __HAVE_ARCH_MEMSET
#if defined(__SANITIZE_MEMORY__) && defined(__NO_FORTIFY)
extern void *__msan_memset(void *s, int c, size_t n);
#undef memset
#define memset __msan_memset
#else
void *memset(void *s, int c, size_t n);
#endif
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
#if defined(__SANITIZE_MEMORY__) && defined(__NO_FORTIFY)
#undef memmove
void *__msan_memmove(void *dest, const void *src, size_t len);
#define memmove __msan_memmove
#else
void *memmove(void *dest, const void *src, size_t count);
#endif
void *__memmove(void *dest, const void *src, size_t count);

int memcmp(const void *cs, const void *ct, size_t count);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
int strcmp(const char *cs, const char *ct);

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
