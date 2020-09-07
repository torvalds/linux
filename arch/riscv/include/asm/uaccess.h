/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 *
 * This file was copied from include/asm-generic/uaccess.h
 */

#ifndef _ASM_RISCV_UACCESS_H
#define _ASM_RISCV_UACCESS_H

#include <asm/pgtable.h>		/* for TASK_SIZE */

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

#define __enable_user_access()							\
	__asm__ __volatile__ ("csrs sstatus, %0" : : "r" (SR_SUM) : "memory")
#define __disable_user_access()							\
	__asm__ __volatile__ ("csrc sstatus, %0" : : "r" (SR_SUM) : "memory")

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(~0UL)
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)

#define get_fs()	(current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}

#define uaccess_kernel() (get_fs().seg == KERNEL_DS.seg)
#define user_addr_max()	(get_fs().seg)

/**
 * access_ok: - Checks if a user space pointer is valid
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only.  This function may sleep.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Returns true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 */
#define access_ok(addr, size) ({					\
	__chk_user_ptr(addr);						\
	likely(__access_ok((unsigned long __force)(addr), (size)));	\
})

/*
 * Ensure that the range [addr, addr+size) is within the process's
 * address space
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	const mm_segment_t fs = get_fs();

	return size <= fs.seg && addr <= fs.seg - size;
}

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

#define __get_user_asm(insn, x, ptr, err)			\
do {								\
	uintptr_t __tmp;					\
	__typeof__(x) __x;					\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	" insn " %1, %3\n"			\
		"2:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.balign 4\n"				\
		"3:\n"						\
		"	li %0, %4\n"				\
		"	li %1, 0\n"				\
		"	jump 2b, %2\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 3b\n"			\
		"	.previous"				\
		: "+r" (err), "=&r" (__x), "=r" (__tmp)		\
		: "m" (*(ptr)), "i" (-EFAULT));			\
	(x) = __x;						\
} while (0)

#ifdef CONFIG_64BIT
#define __get_user_8(x, ptr, err) \
	__get_user_asm("ld", x, ptr, err)
#else /* !CONFIG_64BIT */
#define __get_user_8(x, ptr, err)				\
do {								\
	u32 __user *__ptr = (u32 __user *)(ptr);		\
	u32 __lo, __hi;						\
	uintptr_t __tmp;					\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	lw %1, %4\n"				\
		"2:\n"						\
		"	lw %2, %5\n"				\
		"3:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.balign 4\n"				\
		"4:\n"						\
		"	li %0, %6\n"				\
		"	li %1, 0\n"				\
		"	li %2, 0\n"				\
		"	jump 3b, %3\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 4b\n"			\
		"	" RISCV_PTR " 2b, 4b\n"			\
		"	.previous"				\
		: "+r" (err), "=&r" (__lo), "=r" (__hi),	\
			"=r" (__tmp)				\
		: "m" (__ptr[__LSW]), "m" (__ptr[__MSW]),	\
			"i" (-EFAULT));				\
	(x) = (__typeof__(x))((__typeof__((x)-(x)))(		\
		(((u64)__hi << 32) | __lo)));			\
} while (0)
#endif /* CONFIG_64BIT */

#define __get_user_nocheck(x, __gu_ptr, __gu_err)		\
do {								\
	switch (sizeof(*__gu_ptr)) {				\
	case 1:							\
		__get_user_asm("lb", (x), __gu_ptr, __gu_err);	\
		break;						\
	case 2:							\
		__get_user_asm("lh", (x), __gu_ptr, __gu_err);	\
		break;						\
	case 4:							\
		__get_user_asm("lw", (x), __gu_ptr, __gu_err);	\
		break;						\
	case 8:							\
		__get_user_8((x), __gu_ptr, __gu_err);	\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
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
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);	\
	long __gu_err = 0;					\
								\
	__chk_user_ptr(__gu_ptr);				\
								\
	__enable_user_access();					\
	__get_user_nocheck(x, __gu_ptr, __gu_err);		\
	__disable_user_access();				\
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
		((x) = 0, -EFAULT);				\
})

#define __put_user_asm(insn, x, ptr, err)			\
do {								\
	uintptr_t __tmp;					\
	__typeof__(*(ptr)) __x = x;				\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	" insn " %z3, %2\n"			\
		"2:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.balign 4\n"				\
		"3:\n"						\
		"	li %0, %4\n"				\
		"	jump 2b, %1\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 3b\n"			\
		"	.previous"				\
		: "+r" (err), "=r" (__tmp), "=m" (*(ptr))	\
		: "rJ" (__x), "i" (-EFAULT));			\
} while (0)

#ifdef CONFIG_64BIT
#define __put_user_8(x, ptr, err) \
	__put_user_asm("sd", x, ptr, err)
#else /* !CONFIG_64BIT */
#define __put_user_8(x, ptr, err)				\
do {								\
	u32 __user *__ptr = (u32 __user *)(ptr);		\
	u64 __x = (__typeof__((x)-(x)))(x);			\
	uintptr_t __tmp;					\
	__asm__ __volatile__ (					\
		"1:\n"						\
		"	sw %z4, %2\n"				\
		"2:\n"						\
		"	sw %z5, %3\n"				\
		"3:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.balign 4\n"				\
		"4:\n"						\
		"	li %0, %6\n"				\
		"	jump 3b, %1\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 4b\n"			\
		"	" RISCV_PTR " 2b, 4b\n"			\
		"	.previous"				\
		: "+r" (err), "=r" (__tmp),			\
			"=m" (__ptr[__LSW]),			\
			"=m" (__ptr[__MSW])			\
		: "rJ" (__x), "rJ" (__x >> 32), "i" (-EFAULT));	\
} while (0)
#endif /* CONFIG_64BIT */

#define __put_user_nocheck(x, __gu_ptr, __pu_err)					\
do {								\
	switch (sizeof(*__gu_ptr)) {				\
	case 1:							\
		__put_user_asm("sb", (x), __gu_ptr, __pu_err);	\
		break;						\
	case 2:							\
		__put_user_asm("sh", (x), __gu_ptr, __pu_err);	\
		break;						\
	case 4:							\
		__put_user_asm("sw", (x), __gu_ptr, __pu_err);	\
		break;						\
	case 8:							\
		__put_user_8((x), __gu_ptr, __pu_err);	\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
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
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define __put_user(x, ptr)					\
({								\
	__typeof__(*(ptr)) __user *__gu_ptr = (ptr);		\
	long __pu_err = 0;					\
								\
	__chk_user_ptr(__gu_ptr);				\
								\
	__enable_user_access();					\
	__put_user_nocheck(x, __gu_ptr, __pu_err);		\
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
	return __asm_copy_from_user(to, from, n);
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __asm_copy_to_user(to, from, n);
}

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern long __must_check strlen_user(const char __user *str);
extern long __must_check strnlen_user(const char __user *str, long n);

extern
unsigned long __must_check __clear_user(void __user *addr, unsigned long n);

static inline
unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return access_ok(to, n) ?
		__clear_user(to, n) : n;
}

/*
 * Atomic compare-and-exchange, but with a fixup for userspace faults.  Faults
 * will set "err" to -EFAULT, while successful accesses return the previous
 * value.
 */
#define __cmpxchg_user(ptr, old, new, err, size, lrb, scb)	\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(*(ptr)) __old = (old);			\
	__typeof__(*(ptr)) __new = (new);			\
	__typeof__(*(ptr)) __ret;				\
	__typeof__(err) __err = 0;				\
	register unsigned int __rc;				\
	__enable_user_access();					\
	switch (size) {						\
	case 4:							\
		__asm__ __volatile__ (				\
		"0:\n"						\
		"	lr.w" #scb " %[ret], %[ptr]\n"		\
		"	bne          %[ret], %z[old], 1f\n"	\
		"	sc.w" #lrb " %[rc], %z[new], %[ptr]\n"	\
		"	bnez         %[rc], 0b\n"		\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		".balign 4\n"					\
		"2:\n"						\
		"	li %[err], %[efault]\n"			\
		"	jump 1b, %[rc]\n"			\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		".balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 2b\n"			\
		".previous\n"					\
			: [ret] "=&r" (__ret),			\
			  [rc]  "=&r" (__rc),			\
			  [ptr] "+A" (*__ptr),			\
			  [err] "=&r" (__err)			\
			: [old] "rJ" (__old),			\
			  [new] "rJ" (__new),			\
			  [efault] "i" (-EFAULT));		\
		break;						\
	case 8:							\
		__asm__ __volatile__ (				\
		"0:\n"						\
		"	lr.d" #scb " %[ret], %[ptr]\n"		\
		"	bne          %[ret], %z[old], 1f\n"	\
		"	sc.d" #lrb " %[rc], %z[new], %[ptr]\n"	\
		"	bnez         %[rc], 0b\n"		\
		"1:\n"						\
		".section .fixup,\"ax\"\n"			\
		".balign 4\n"					\
		"2:\n"						\
		"	li %[err], %[efault]\n"			\
		"	jump 1b, %[rc]\n"			\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		".balign " RISCV_SZPTR "\n"			\
		"	" RISCV_PTR " 1b, 2b\n"			\
		".previous\n"					\
			: [ret] "=&r" (__ret),			\
			  [rc]  "=&r" (__rc),			\
			  [ptr] "+A" (*__ptr),			\
			  [err] "=&r" (__err)			\
			: [old] "rJ" (__old),			\
			  [new] "rJ" (__new),			\
			  [efault] "i" (-EFAULT));		\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__disable_user_access();				\
	(err) = __err;						\
	__ret;							\
})

#define HAVE_GET_KERNEL_NOFAULT

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	long __kr_err;							\
									\
	__get_user_nocheck(*((type *)(dst)), (type *)(src), __kr_err);	\
	if (unlikely(__kr_err))						\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	long __kr_err;							\
									\
	__put_user_nocheck(*((type *)(dst)), (type *)(src), __kr_err);	\
	if (unlikely(__kr_err))						\
		goto err_label;						\
} while (0)

#else /* CONFIG_MMU */
#include <asm-generic/uaccess.h>
#endif /* CONFIG_MMU */
#endif /* _ASM_RISCV_UACCESS_H */
