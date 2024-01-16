/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UACCESS_64_H
#define _ASM_X86_UACCESS_64_H

/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/lockdep.h>
#include <linux/kasan-checks.h>
#include <asm/alternative.h>
#include <asm/cpufeatures.h>
#include <asm/page.h>

/*
 * Copy To/From Userspace
 */

/* Handles exceptions in both to and from, but doesn't do access_ok */
__must_check unsigned long
copy_user_enhanced_fast_string(void *to, const void *from, unsigned len);
__must_check unsigned long
copy_user_generic_string(void *to, const void *from, unsigned len);
__must_check unsigned long
copy_user_generic_unrolled(void *to, const void *from, unsigned len);

static __always_inline __must_check unsigned long
copy_user_generic(void *to, const void *from, unsigned len)
{
	unsigned ret;

	/*
	 * If CPU has ERMS feature, use copy_user_enhanced_fast_string.
	 * Otherwise, if CPU has rep_good feature, use copy_user_generic_string.
	 * Otherwise, use copy_user_generic_unrolled.
	 */
	alternative_call_2(copy_user_generic_unrolled,
			 copy_user_generic_string,
			 X86_FEATURE_REP_GOOD,
			 copy_user_enhanced_fast_string,
			 X86_FEATURE_ERMS,
			 ASM_OUTPUT2("=a" (ret), "=D" (to), "=S" (from),
				     "=d" (len)),
			 "1" (to), "2" (from), "3" (len)
			 : "memory", "rcx", "r8", "r9", "r10", "r11");
	return ret;
}

static __always_inline __must_check unsigned long
raw_copy_from_user(void *dst, const void __user *src, unsigned long size)
{
	return copy_user_generic(dst, (__force void *)src, size);
}

static __always_inline __must_check unsigned long
raw_copy_to_user(void __user *dst, const void *src, unsigned long size)
{
	return copy_user_generic((__force void *)dst, src, size);
}

extern long __copy_user_nocache(void *dst, const void __user *src,
				unsigned size, int zerorest);

extern long __copy_user_flushcache(void *dst, const void __user *src, unsigned size);
extern void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
			   size_t len);

static inline int
__copy_from_user_inatomic_nocache(void *dst, const void __user *src,
				  unsigned size)
{
	kasan_check_write(dst, size);
	return __copy_user_nocache(dst, src, size, 0);
}

static inline int
__copy_from_user_flushcache(void *dst, const void __user *src, unsigned size)
{
	kasan_check_write(dst, size);
	return __copy_user_flushcache(dst, src, size);
}

/*
 * Zero Userspace.
 */

__must_check unsigned long
clear_user_original(void __user *addr, unsigned long len);
__must_check unsigned long
clear_user_rep_good(void __user *addr, unsigned long len);
__must_check unsigned long
clear_user_erms(void __user *addr, unsigned long len);

static __always_inline __must_check unsigned long __clear_user(void __user *addr, unsigned long size)
{
	might_fault();
	stac();

	/*
	 * No memory constraint because it doesn't change any memory gcc
	 * knows about.
	 */
	asm volatile(
		"1:\n\t"
		ALTERNATIVE_3("rep stosb",
			      "call clear_user_erms",	  ALT_NOT(X86_FEATURE_FSRM),
			      "call clear_user_rep_good", ALT_NOT(X86_FEATURE_ERMS),
			      "call clear_user_original", ALT_NOT(X86_FEATURE_REP_GOOD))
		"2:\n"
	       _ASM_EXTABLE_UA(1b, 2b)
	       : "+c" (size), "+D" (addr), ASM_CALL_CONSTRAINT
	       : "a" (0)
		/* rep_good clobbers %rdx */
	       : "rdx");

	clac();

	return size;
}

static __always_inline unsigned long clear_user(void __user *to, unsigned long n)
{
	if (access_ok(to, n))
		return __clear_user(to, n);
	return n;
}
#endif /* _ASM_X86_UACCESS_64_H */
