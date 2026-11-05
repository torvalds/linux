/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_STRING_64_H
#define _ASM_X86_STRING_64_H

#ifdef __KERNEL__

/* Written 2002 by Andi Kleen */

/* Even with __builtin_ the compiler may decide to use the out of line
   function. */

#if defined(__SANITIZE_MEMORY__) && defined(__NO_FORTIFY)
#include <linux/kmsan_string.h>
#endif

#define __HAVE_ARCH_MEMCPY 1
extern void *memcpy(void *to, const void *from, size_t len);
extern void *__memcpy(void *to, const void *from, size_t len);

#define __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n);
void *__memset(void *s, int c, size_t n);
KCFI_REFERENCE(__memset);

/*
 * KMSAN needs to instrument as much code as possible. Use C versions of
 * memsetXX() from lib/string.c under KMSAN.
 */
#if !defined(CONFIG_KMSAN)
#define __HAVE_ARCH_MEMSET16
static inline void *memset16(uint16_t *s, uint16_t v, size_t n)
{
	const auto s0 = s;
	asm volatile (
		"rep stosw"
		: "+D" (s), "+c" (n)
		: "a" (v)
		: "memory"
	);
	return s0;
}

#define __HAVE_ARCH_MEMSET32
static inline void *memset32(uint32_t *s, uint32_t v, size_t n)
{
	const auto s0 = s;
	asm volatile (
		"rep stosl"
		: "+D" (s), "+c" (n)
		: "a" (v)
		: "memory"
	);
	return s0;
}

#define __HAVE_ARCH_MEMSET64
static inline void *memset64(uint64_t *s, uint64_t v, size_t n)
{
	const auto s0 = s;
	asm volatile (
		"rep stosq"
		: "+D" (s), "+c" (n)
		: "a" (v)
		: "memory"
	);
	return s0;
}
#endif

#define __HAVE_ARCH_MEMMOVE
void *memmove(void *dest, const void *src, size_t count);
void *__memmove(void *dest, const void *src, size_t count);
KCFI_REFERENCE(__memmove);

int memcmp(const void *cs, const void *ct, size_t count);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
int strcmp(const char *cs, const char *ct);

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
#define __HAVE_ARCH_MEMCPY_FLUSHCACHE 1
void __memcpy_flushcache(void *dst, const void *src, size_t cnt);

static __always_inline void movnti_4(void *dst, const void *src)
{
	asm volatile("movntil %1, %0"
		     : "=m"(*(u32 *)dst)
		     : "r"(*(const u32 *)src)
		     : "memory");
}

static __always_inline void movnti_8(void *dst, const void *src)
{
	asm volatile("movntiq %1, %0"
		     : "=m"(*(u64 *)dst)
		     : "r"(*(const u64 *)src)
		     : "memory");
}

static __always_inline void movnti_16(void *dst, const void *src)
{
	movnti_8(dst, src);
	movnti_8(dst + 8, src + 8);
}

static __always_inline void movnti_32(void *dst, const void *src)
{
	movnti_16(dst, src);
	movnti_16(dst + 16, src + 16);
}

static __always_inline void movnti_64(void *dst, const void *src)
{
	movnti_32(dst, src);
	movnti_32(dst + 32, src + 32);
}

static __always_inline void memcpy_flushcache(void *dst, const void *src,
					      size_t cnt)
{
	if (!__builtin_constant_p(cnt))
		return __memcpy_flushcache(dst, src, cnt);

	/*
	 * The relevant fixed-size copies here are the x86_64 struct page sizes:
	 * 64, 80, and 96 bytes. Keep 32-byte and 48-byte copies inline as well
	 * instead of sending those nearby fixed-size cases back to
	 * __memcpy_flushcache().
	 */
	switch (cnt) {
	case 4:  movnti_4(dst, src); break;
	case 8:  movnti_8(dst, src); break;
	case 16: movnti_16(dst, src); break;
	case 32: movnti_32(dst, src); break;
	case 48: movnti_32(dst, src); movnti_16(dst + 32, src + 32); break;
	case 64: movnti_64(dst, src); break;
	case 80: movnti_64(dst, src); movnti_16(dst + 64, src + 64); break;
	case 96: movnti_64(dst, src); movnti_32(dst + 64, src + 64); break;
	default: __memcpy_flushcache(dst, src, cnt); break;
	}
}

#define memcpy_nontemporal memcpy_nontemporal
/*
 * Reuse the existing x86 flushcache backend as the non-temporal copy
 * primitive.
 */
static __always_inline void memcpy_nontemporal(void *dst, const void *src,
		size_t cnt)
{
	memcpy_flushcache(dst, src, cnt);
}

#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_STRING_64_H */
