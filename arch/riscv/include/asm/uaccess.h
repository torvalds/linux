/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 *
 * This file was copied from include/asm-generic/uaccess.h
 */

#ifndef _ASM_RISCV_UACCESS_H
#define _ASM_RISCV_UACCESS_H

#include <asm/asm-extable.h>
#include <asm/cpufeature.h>
#include <asm/pgtable.h>		/* for TASK_SIZE */

#ifdef CONFIG_RISCV_ISA_SUPM
static inline unsigned long __untagged_addr_remote(struct mm_struct *mm, unsigned long addr)
{
	if (riscv_has_extension_unlikely(RISCV_ISA_EXT_SUPM)) {
		u8 pmlen = mm->context.pmlen;

		/* Virtual addresses are sign-extended; physical addresses are zero-extended. */
		if (IS_ENABLED(CONFIG_MMU))
			return (long)(addr << pmlen) >> pmlen;
		else
			return (addr << pmlen) >> pmlen;
	}

	return addr;
}

#define untagged_addr(addr) ({							\
	unsigned long __addr = (__force unsigned long)(addr);			\
	(__force __typeof__(addr))__untagged_addr_remote(current->mm, __addr);	\
})

#define untagged_addr_remote(mm, addr) ({					\
	unsigned long __addr = (__force unsigned long)(addr);			\
	mmap_assert_locked(mm);							\
	(__force __typeof__(addr))__untagged_addr_remote(mm, __addr);		\
})

#define access_ok(addr, size) likely(__access_ok(untagged_addr(addr), size))
#else
#define untagged_addr(addr) (addr)
#endif

/*
 * User space memory access functions
 */
#ifdef CONFIG_MMU
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <asm/byteorder.h>
#include <asm/extable.h>
#include <asm/asm.h>
#include <asm-generic/access_ok.h>

#define __enable_user_access()							\
	__asm__ __volatile__ ("csrs sstatus, %0" : : "r" (SR_SUM) : "memory")
#define __disable_user_access()							\
	__asm__ __volatile__ ("csrc sstatus, %0" : : "r" (SR_SUM) : "memory")

/*
 * This is the smallest unsigned integer type that can fit a value
 * (up to 'long long')
 */
#define __inttype(x) __typeof__(		\
	__typefits(x, char,			\
	  __typefits(x, short,			\
	    __typefits(x, int,			\
	      __typefits(x, long, 0ULL)))))

#define __typefits(x, type, not) \
	__builtin_choose_expr(sizeof(x) <= sizeof(type), (unsigned type)0, not)

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

#define __LSW	0
#define __MSW	1

/*
 * The "__xxx" versions of the user access functions do not verify the address
 * space - it must have been done previously with a separate "access_ok()"
 * call.
 */

#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT
#define __get_user_asm(insn, x, ptr, label)			\
	asm_goto_output(					\
		"1:\n"						\
		"	" insn " %0, %1\n"			\
		_ASM_EXTABLE_UACCESS_ERR(1b, %l2, %0)		\
		: "=&r" (x)					\
		: "m" (*(ptr)) : : label)
#else /* !CONFIG_CC_HAS_ASM_GOTO_OUTPUT */
#define __get_user_asm(insn, x, ptr, label)			\
do {								\
	long __gua_err = 0;					\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	" insn " %1, %2\n"			\
		"2:\n"						\
		_ASM_EXTABLE_UACCESS_ERR_ZERO(1b, 2b, %0, %1)	\
		: "+r" (__gua_err), "=&r" (x)			\
		: "m" (*(ptr)));				\
	if (__gua_err)						\
		goto label;					\
} while (0)
#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

#ifdef CONFIG_64BIT
#define __get_user_8(x, ptr, label) \
	__get_user_asm("ld", x, ptr, label)
#else /* !CONFIG_64BIT */

#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT
#define __get_user_8(x, ptr, label)				\
do {								\
	u32 __user *__ptr = (u32 __user *)(ptr);		\
	u32 __lo, __hi;						\
	asm_goto_output(					\
		"1:\n"						\
		"	lw %0, %2\n"				\
		"2:\n"						\
		"	lw %1, %3\n"				\
		_ASM_EXTABLE_UACCESS_ERR(1b, %l4, %0)		\
		_ASM_EXTABLE_UACCESS_ERR(2b, %l4, %0)		\
		: "=&r" (__lo), "=r" (__hi)			\
		: "m" (__ptr[__LSW]), "m" (__ptr[__MSW])	\
		: : label);                                     \
	(x) = (__typeof__(x))((__typeof__((x) - (x)))(		\
		(((u64)__hi << 32) | __lo)));			\
} while (0)
#else /* !CONFIG_CC_HAS_ASM_GOTO_OUTPUT */
#define __get_user_8(x, ptr, label)				\
do {								\
	u32 __user *__ptr = (u32 __user *)(ptr);		\
	u32 __lo, __hi;						\
	long __gu8_err = 0;					\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	lw %1, %3\n"				\
		"2:\n"						\
		"	lw %2, %4\n"				\
		"3:\n"						\
		_ASM_EXTABLE_UACCESS_ERR_ZERO(1b, 3b, %0, %1)	\
		_ASM_EXTABLE_UACCESS_ERR_ZERO(2b, 3b, %0, %1)	\
		: "+r" (__gu8_err), "=&r" (__lo), "=r" (__hi)	\
		: "m" (__ptr[__LSW]), "m" (__ptr[__MSW]));	\
	if (__gu8_err) {					\
		__hi = 0;					\
		goto label;					\
	}							\
	(x) = (__typeof__(x))((__typeof__((x) - (x)))(		\
		(((u64)__hi << 32) | __lo)));			\
} while (0)
#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

#endif /* CONFIG_64BIT */

unsigned long __must_check __asm_copy_to_user_sum_enabled(void __user *to,
	const void *from, unsigned long n);
unsigned long __must_check __asm_copy_from_user_sum_enabled(void *to,
	const void __user *from, unsigned long n);

#define __get_user_nocheck(x, __gu_ptr, label)			\
do {								\
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&	\
	    !IS_ALIGNED((uintptr_t)__gu_ptr, sizeof(*__gu_ptr))) {	\
		if (__asm_copy_from_user_sum_enabled(&(x), __gu_ptr, sizeof(*__gu_ptr))) \
			goto label;				\
		break;						\
	}							\
	switch (sizeof(*__gu_ptr)) {				\
	case 1:							\
		__get_user_asm("lb", (x), __gu_ptr, label);	\
		break;						\
	case 2:							\
		__get_user_asm("lh", (x), __gu_ptr, label);	\
		break;						\
	case 4:							\
		__get_user_asm("lw", (x), __gu_ptr, label);	\
		break;						\
	case 8:							\
		__get_user_8((x), __gu_ptr, label);		\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
} while (0)

#define __get_user_error(x, ptr, err)					\
do {									\
	__label__ __gu_failed;						\
									\
	__get_user_nocheck(x, ptr, __gu_failed);			\
		err = 0;						\
		break;							\
__gu_failed:								\
		x = (__typeof__(x))0;					\
		err = -EFAULT;						\
} while (0)

/**
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define __get_user(x, ptr)					\
({								\
	const __typeof__(*(ptr)) __user *__gu_ptr = untagged_addr(ptr); \
	long __gu_err = 0;					\
	__typeof__(x) __gu_val;					\
								\
	__chk_user_ptr(__gu_ptr);				\
								\
	__enable_user_access();					\
	__get_user_error(__gu_val, __gu_ptr, __gu_err);		\
	__disable_user_access();				\
								\
	(x) = __gu_val;						\
								\
	__gu_err;						\
})

/**
 * get_user: - Get a simple variable from user space.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define get_user(x, ptr)					\
({								\
	const __typeof__(*(ptr)) __user *__p = (ptr);		\
	might_fault();						\
	access_ok(__p, sizeof(*__p)) ?		\
		__get_user((x), __p) :				\
		((x) = (__force __typeof__(x))0, -EFAULT);	\
})

#define __put_user_asm(insn, x, ptr, label)			\
do {								\
	__typeof__(*(ptr)) __x = x;				\
	asm goto(						\
		"1:\n"						\
		"	" insn " %z0, %1\n"			\
		_ASM_EXTABLE(1b, %l2)				\
		: : "rJ" (__x), "m"(*(ptr)) : : label);		\
} while (0)

#ifdef CONFIG_64BIT
#define __put_user_8(x, ptr, label) \
	__put_user_asm("sd", x, ptr, label)
#else /* !CONFIG_64BIT */
#define __put_user_8(x, ptr, label)				\
do {								\
	u32 __user *__ptr = (u32 __user *)(ptr);		\
	u64 __x = (__typeof__((x)-(x)))(x);			\
	asm goto(						\
		"1:\n"						\
		"	sw %z0, %2\n"				\
		"2:\n"						\
		"	sw %z1, %3\n"				\
		_ASM_EXTABLE(1b, %l4)				\
		_ASM_EXTABLE(2b, %l4)				\
		: : "rJ" (__x), "rJ" (__x >> 32),		\
			"m" (__ptr[__LSW]),			\
			"m" (__ptr[__MSW]) : : label);		\
} while (0)
#endif /* CONFIG_64BIT */

#define __put_user_nocheck(x, __gu_ptr, label)			\
do {								\
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&	\
	    !IS_ALIGNED((uintptr_t)__gu_ptr, sizeof(*__gu_ptr))) {	\
		__typeof__(*(__gu_ptr)) ___val = (x);		\
		if (__asm_copy_to_user_sum_enabled(__gu_ptr, &(___val), sizeof(*__gu_ptr))) \
			goto label;				\
		break;						\
	}							\
	switch (sizeof(*__gu_ptr)) {				\
	case 1:							\
		__put_user_asm("sb", (x), __gu_ptr, label);	\
		break;						\
	case 2:							\
		__put_user_asm("sh", (x), __gu_ptr, label);	\
		break;						\
	case 4:							\
		__put_user_asm("sw", (x), __gu_ptr, label);	\
		break;						\
	case 8:							\
		__put_user_8((x), __gu_ptr, label);		\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
} while (0)

#define __put_user_error(x, ptr, err)				\
do {								\
	__label__ err_label;					\
	__put_user_nocheck(x, ptr, err_label);			\
	break;							\
err_label:							\
	(err) = -EFAULT;					\
} while (0)

/**
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr. The value of @x is copied to avoid
 * re-ordering where @x is evaluated inside the block that enables user-space
 * access (thus bypassing user space protection if @x is a function).
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define __put_user(x, ptr)					\
({								\
	__typeof__(*(ptr)) __user *__gu_ptr = untagged_addr(ptr); \
	__typeof__(*__gu_ptr) __val = (x);			\
	long __pu_err = 0;					\
								\
	__chk_user_ptr(__gu_ptr);				\
								\
	__enable_user_access();					\
	__put_user_error(__val, __gu_ptr, __pu_err);		\
	__disable_user_access();				\
								\
	__pu_err;						\
})

/**
 * put_user: - Write a simple value into user space.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define put_user(x, ptr)					\
({								\
	__typeof__(*(ptr)) __user *__p = (ptr);			\
	might_fault();						\
	access_ok(__p, sizeof(*__p)) ?		\
		__put_user((x), __p) :				\
		-EFAULT;					\
})


unsigned long __must_check __asm_copy_to_user(void __user *to,
	const void *from, unsigned long n);
unsigned long __must_check __asm_copy_from_user(void *to,
	const void __user *from, unsigned long n);

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __asm_copy_from_user(to, untagged_addr(from), n);
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __asm_copy_to_user(untagged_addr(to), from, n);
}

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern long __must_check strnlen_user(const char __user *str, long n);

extern
unsigned long __must_check __clear_user(void __user *addr, unsigned long n);

static inline
unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return access_ok(to, n) ?
		__clear_user(untagged_addr(to), n) : n;
}

#define __get_kernel_nofault(dst, src, type, err_label)			\
	__get_user_nocheck(*((type *)(dst)), (__force __user type *)(src), err_label)

#define __put_kernel_nofault(dst, src, type, err_label)			\
	__put_user_nocheck(*((type *)(src)), (__force __user type *)(dst), err_label)

static __must_check __always_inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr, len)))
		return 0;
	__enable_user_access();
	return 1;
}
#define user_access_begin user_access_begin
#define user_access_end __disable_user_access

static inline unsigned long user_access_save(void) { return 0UL; }
static inline void user_access_restore(unsigned long enabled) { }

/*
 * We want the unsafe accessors to always be inlined and use
 * the error labels - thus the macro games.
 */
#define unsafe_put_user(x, ptr, label)					\
	__put_user_nocheck(x, (ptr), label)

#define unsafe_get_user(x, ptr, label)	do {				\
	__inttype(*(ptr)) __gu_val;					\
	__get_user_nocheck(__gu_val, (ptr), label);			\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
} while (0)

#define unsafe_copy_to_user(_dst, _src, _len, label)			\
	if (__asm_copy_to_user_sum_enabled(_dst, _src, _len))		\
		goto label;

#define unsafe_copy_from_user(_dst, _src, _len, label)			\
	if (__asm_copy_from_user_sum_enabled(_dst, _src, _len))		\
		goto label;

#else /* CONFIG_MMU */
#include <asm-generic/uaccess.h>
#endif /* CONFIG_MMU */
#endif /* _ASM_RISCV_UACCESS_H */
