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
#include <asm/percpu.h>
#include <asm/runtime-const.h>

/*
 * Virtual variable: there's no actual backing store for this,
 * it can purely be used as 'runtime_const_ptr(USER_PTR_MAX)'
 */
extern unsigned long USER_PTR_MAX;

#ifdef CONFIG_ADDRESS_MASKING
/*
 * Mask out tag bits from the address.
 */
static inline unsigned long __untagged_addr(unsigned long addr)
{
	asm (ALTERNATIVE("",
			 "and " __percpu_arg([mask]) ", %[addr]", X86_FEATURE_LAM)
	     : [addr] "+r" (addr)
	     : [mask] "m" (__my_cpu_var(tlbstate_untag_mask)));

	return addr;
}

#define untagged_addr(addr)	({					\
	unsigned long __addr = (__force unsigned long)(addr);		\
	(__force __typeof__(addr))__untagged_addr(__addr);		\
})

static inline unsigned long __untagged_addr_remote(struct mm_struct *mm,
						   unsigned long addr)
{
	mmap_assert_locked(mm);
	return addr & (mm)->context.untag_mask;
}

#define untagged_addr_remote(mm, addr)	({				\
	unsigned long __addr = (__force unsigned long)(addr);		\
	(__force __typeof__(addr))__untagged_addr_remote(mm, __addr);	\
})

#endif

#define valid_user_address(x) \
	((__force unsigned long)(x) <= runtime_const_ptr(USER_PTR_MAX))

/*
 * Masking the user address is an alternative to a conditional
 * user_access_begin that can avoid the fencing. This only works
 * for dense accesses starting at the address.
 */
static inline void __user *mask_user_address(const void __user *ptr)
{
	unsigned long mask;
	asm("cmp %1,%0\n\t"
	    "sbb %0,%0"
		:"=r" (mask)
		:"r" (ptr),
		 "0" (runtime_const_ptr(USER_PTR_MAX)));
	return (__force void __user *)(mask | (__force unsigned long)ptr);
}
#define masked_user_access_begin(x) ({				\
	__auto_type __masked_ptr = (x);				\
	__masked_ptr = mask_user_address(__masked_ptr);		\
	__uaccess_begin(); __masked_ptr; })

/*
 * User pointers can have tag bits on x86-64.  This scheme tolerates
 * arbitrary values in those bits rather then masking them off.
 *
 * Enforce two rules:
 * 1. 'ptr' must be in the user part of the address space
 * 2. 'ptr+size' must not overflow into kernel addresses
 *
 * Note that we always have at least one guard page between the
 * max user address and the non-canonical gap, allowing us to
 * ignore small sizes entirely.
 *
 * In fact, we could probably remove the size check entirely, since
 * any kernel accesses will be in increasing address order starting
 * at 'ptr'.
 *
 * That's a separate optimization, for now just handle the small
 * constant case.
 */
static inline bool __access_ok(const void __user *ptr, unsigned long size)
{
	if (__builtin_constant_p(size <= PAGE_SIZE) && size <= PAGE_SIZE) {
		return valid_user_address(ptr);
	} else {
		unsigned long sum = size + (__force unsigned long)ptr;

		return valid_user_address(sum) && sum >= (__force unsigned long)ptr;
	}
}
#define __access_ok __access_ok

/*
 * Copy To/From Userspace
 */

/* Handles exceptions in both to and from, but doesn't do access_ok */
__must_check unsigned long
rep_movs_alternative(void *to, const void *from, unsigned len);

static __always_inline __must_check unsigned long
copy_user_generic(void *to, const void *from, unsigned long len)
{
	stac();
	/*
	 * If CPU has FSRM feature, use 'rep movs'.
	 * Otherwise, use rep_movs_alternative.
	 */
	asm volatile(
		"1:\n\t"
		ALTERNATIVE("rep movsb",
			    "call rep_movs_alternative", ALT_NOT(X86_FEATURE_FSRM))
		"2:\n"
		_ASM_EXTABLE_UA(1b, 2b)
		:"+c" (len), "+D" (to), "+S" (from), ASM_CALL_CONSTRAINT
		: : "memory", "rax");
	clac();
	return len;
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

extern long __copy_user_nocache(void *dst, const void __user *src, unsigned size);
extern long __copy_user_flushcache(void *dst, const void __user *src, unsigned size);

static inline int
__copy_from_user_inatomic_nocache(void *dst, const void __user *src,
				  unsigned size)
{
	long ret;
	kasan_check_write(dst, size);
	stac();
	ret = __copy_user_nocache(dst, src, size);
	clac();
	return ret;
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
rep_stos_alternative(void __user *addr, unsigned long len);

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
		ALTERNATIVE("rep stosb",
			    "call rep_stos_alternative", ALT_NOT(X86_FEATURE_FSRS))
		"2:\n"
	       _ASM_EXTABLE_UA(1b, 2b)
	       : "+c" (size), "+D" (addr), ASM_CALL_CONSTRAINT
	       : "a" (0));

	clac();

	return size;
}

static __always_inline unsigned long clear_user(void __user *to, unsigned long n)
{
	if (__access_ok(to, n))
		return __clear_user(to, n);
	return n;
}
#endif /* _ASM_X86_UACCESS_64_H */
