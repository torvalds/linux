/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000, 03, 04 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2007  Maciej W. Rozycki
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/asm-eva.h>
#include <asm/extable.h>

#ifdef CONFIG_32BIT

#define __UA_LIMIT 0x80000000UL
#define TASK_SIZE_MAX	KSEG0

#define __UA_ADDR	".word"
#define __UA_LA		"la"
#define __UA_ADDU	"addu"
#define __UA_t0		"$8"
#define __UA_t1		"$9"

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

extern u64 __ua_limit;

#define __UA_LIMIT	__ua_limit
#define TASK_SIZE_MAX	XKSSEG

#define __UA_ADDR	".dword"
#define __UA_LA		"dla"
#define __UA_ADDU	"daddu"
#define __UA_t0		"$12"
#define __UA_t1		"$13"

#endif /* CONFIG_64BIT */

#include <asm-generic/access_ok.h>

/*
 * put_user: - Write a simple value into user space.
 * @x:	 Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
#define put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
									\
	might_fault();							\
	access_ok(__p, sizeof(*__p)) ? __put_user((x), __p) : -EFAULT;	\
})

/*
 * get_user: - Get a simple variable from user space.
 * @x:	 Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
#define get_user(x, ptr)						\
({									\
	const __typeof__(*(ptr)) __user *__p = (ptr);			\
									\
	might_fault();							\
	access_ok(__p, sizeof(*__p)) ? __get_user((x), __p) :		\
				       ((x) = 0, -EFAULT);		\
})

/*
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:	 Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
#define __put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __user *__pu_ptr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (x);				\
	int __pu_err = 0;						\
									\
	__chk_user_ptr(__pu_ptr);					\
	switch (sizeof(*__pu_ptr)) {					\
	case 1:								\
		__put_data_asm(user_sb, __pu_ptr);			\
		break;							\
	case 2:								\
		__put_data_asm(user_sh, __pu_ptr);			\
		break;							\
	case 4:								\
		__put_data_asm(user_sw, __pu_ptr);			\
		break;							\
	case 8:								\
		__PUT_DW(user_sd, __pu_ptr);				\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	__pu_err;							\
})

/*
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:	 Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
#define __get_user(x, ptr)						\
({									\
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);		\
	int __gu_err = 0;						\
									\
	__chk_user_ptr(__gu_ptr);					\
	switch (sizeof(*__gu_ptr)) {					\
	case 1:								\
		__get_data_asm((x), user_lb, __gu_ptr);			\
		break;							\
	case 2:								\
		__get_data_asm((x), user_lh, __gu_ptr);			\
		break;							\
	case 4:								\
		__get_data_asm((x), user_lw, __gu_ptr);			\
		break;							\
	case 8:								\
		__GET_DW((x), user_ld, __gu_ptr);			\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	__gu_err;							\
})

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#ifdef CONFIG_32BIT
#define __GET_DW(val, insn, ptr) __get_data_asm_ll32(val, insn, ptr)
#endif
#ifdef CONFIG_64BIT
#define __GET_DW(val, insn, ptr) __get_data_asm(val, insn, ptr)
#endif

#define __get_data_asm(val, insn, addr)					\
{									\
	long __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	"insn("%1", "%3")"				\n"	\
	"2:							\n"	\
	"	.insn						\n"	\
	"	.section .fixup,\"ax\"				\n"	\
	"3:	li	%0, %4					\n"	\
	"	move	%1, $0					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section __ex_table,\"a\"			\n"	\
	"	"__UA_ADDR "\t1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=r" (__gu_tmp)				\
	: "0" (0), "o" (__m(addr)), "i" (-EFAULT));			\
									\
	(val) = (__typeof__(*(addr))) __gu_tmp;				\
}

/*
 * Get a long long 64 using 32 bit registers.
 */
#define __get_data_asm_ll32(val, insn, addr)				\
{									\
	union {								\
		unsigned long long	l;				\
		__typeof__(*(addr))	t;				\
	} __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	" insn("%1", "(%3)")"				\n"	\
	"2:	" insn("%D1", "4(%3)")"				\n"	\
	"3:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	move	%1, $0					\n"	\
	"	move	%D1, $0					\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=&r" (__gu_tmp.l)				\
	: "0" (0), "r" (addr), "i" (-EFAULT));				\
									\
	(val) = __gu_tmp.t;						\
}

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	int __gu_err;							\
									\
	switch (sizeof(type)) {						\
	case 1:								\
		__get_data_asm(*(type *)(dst), kernel_lb,		\
			       (__force type *)(src));			\
		break;							\
	case 2:								\
		__get_data_asm(*(type *)(dst), kernel_lh,		\
			       (__force type *)(src));			\
		break;							\
	case 4:								\
		 __get_data_asm(*(type *)(dst), kernel_lw,		\
			       (__force type *)(src));			\
		break;							\
	case 8:								\
		__GET_DW(*(type *)(dst), kernel_ld,			\
			 (__force type *)(src));			\
		break;							\
	default:							\
		BUILD_BUG();						\
		break;							\
	}								\
	if (unlikely(__gu_err))						\
		goto err_label;						\
} while (0)

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#ifdef CONFIG_32BIT
#define __PUT_DW(insn, ptr) __put_data_asm_ll32(insn, ptr)
#endif
#ifdef CONFIG_64BIT
#define __PUT_DW(insn, ptr) __put_data_asm(insn, ptr)
#endif

#define __put_data_asm(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	"insn("%z2", "%3")"	# __put_data_asm	\n"	\
	"2:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"3:	li	%0, %4					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__pu_err)						\
	: "0" (0), "Jr" (__pu_val), "o" (__m(ptr)),			\
	  "i" (-EFAULT));						\
}

#define __put_data_asm_ll32(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	"insn("%2", "(%3)")"	# __put_data_asm_ll32	\n"	\
	"2:	"insn("%D2", "4(%3)")"				\n"	\
	"3:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous"						\
	: "=r" (__pu_err)						\
	: "0" (0), "r" (__pu_val), "r" (ptr),				\
	  "i" (-EFAULT));						\
}

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	type __pu_val;					\
	int __pu_err = 0;						\
									\
	__pu_val = *(__force type *)(src);				\
	switch (sizeof(type)) {						\
	case 1:								\
		__put_data_asm(kernel_sb, (type *)(dst));		\
		break;							\
	case 2:								\
		__put_data_asm(kernel_sh, (type *)(dst));		\
		break;							\
	case 4:								\
		__put_data_asm(kernel_sw, (type *)(dst))		\
		break;							\
	case 8:								\
		__PUT_DW(kernel_sd, (type *)(dst));			\
		break;							\
	default:							\
		BUILD_BUG();						\
		break;							\
	}								\
	if (unlikely(__pu_err))						\
		goto err_label;						\
} while (0)


/*
 * We're generating jump to subroutines which will be outside the range of
 * jump instructions
 */
#ifdef MODULE
#define __MODULE_JAL(destination)					\
	".set\tnoat\n\t"						\
	__UA_LA "\t$1, " #destination "\n\t"				\
	"jalr\t$1\n\t"							\
	".set\tat\n\t"
#else
#define __MODULE_JAL(destination)					\
	"jal\t" #destination "\n\t"
#endif

#if defined(CONFIG_CPU_DADDI_WORKAROUNDS) || (defined(CONFIG_EVA) &&	\
					      defined(CONFIG_CPU_HAS_PREFETCH))
#define DADDI_SCRATCH "$3"
#else
#define DADDI_SCRATCH "$0"
#endif

extern size_t __raw_copy_from_user(void *__to, const void *__from, size_t __n);
extern size_t __raw_copy_to_user(void *__to, const void *__from, size_t __n);

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	register void *__cu_to_r __asm__("$4");
	register const void __user *__cu_from_r __asm__("$5");
	register long __cu_len_r __asm__("$6");

	__cu_to_r = to;
	__cu_from_r = from;
	__cu_len_r = n;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		__MODULE_JAL(__raw_copy_from_user)
		".set\tnoat\n\t"
		__UA_ADDU "\t$1, %1, %2\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "+r" (__cu_to_r), "+r" (__cu_from_r), "+r" (__cu_len_r)
		:
		: "$8", "$9", "$10", "$11", "$12", "$14", "$15", "$24", "$31",
		  DADDI_SCRATCH, "memory");

	return __cu_len_r;
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	register void __user *__cu_to_r __asm__("$4");
	register const void *__cu_from_r __asm__("$5");
	register long __cu_len_r __asm__("$6");

	__cu_to_r = (to);
	__cu_from_r = (from);
	__cu_len_r = (n);

	__asm__ __volatile__(
		__MODULE_JAL(__raw_copy_to_user)
		: "+r" (__cu_to_r), "+r" (__cu_from_r), "+r" (__cu_len_r)
		:
		: "$8", "$9", "$10", "$11", "$12", "$14", "$15", "$24", "$31",
		  DADDI_SCRATCH, "memory");

	return __cu_len_r;
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

extern __kernel_size_t __bzero(void __user *addr, __kernel_size_t size);

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:	  Destination address, in user space.
 * @n:	  Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
static inline __kernel_size_t
__clear_user(void __user *addr, __kernel_size_t size)
{
	__kernel_size_t res;

#ifdef CONFIG_CPU_MICROMIPS
/* micromips memset / bzero also clobbers t7 & t8 */
#define bzero_clobbers "$4", "$5", "$6", __UA_t0, __UA_t1, "$15", "$24", "$31"
#else
#define bzero_clobbers "$4", "$5", "$6", __UA_t0, __UA_t1, "$31"
#endif /* CONFIG_CPU_MICROMIPS */

	might_fault();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, $0\n\t"
		"move\t$6, %2\n\t"
		__MODULE_JAL(__bzero)
		"move\t%0, $6"
		: "=r" (res)
		: "r" (addr), "r" (size)
		: bzero_clobbers);

	return res;
}

#define clear_user(addr,n)						\
({									\
	void __user * __cl_addr = (addr);				\
	unsigned long __cl_size = (n);					\
	if (__cl_size && access_ok(__cl_addr, __cl_size))		\
		__cl_size = __clear_user(__cl_addr, __cl_size);		\
	__cl_size;							\
})

extern long __strncpy_from_user_asm(char *__to, const char __user *__from, long __len);

/*
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *	   least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
static inline long
strncpy_from_user(char *__to, const char __user *__from, long __len)
{
	long res;

	if (!access_ok(__from, __len))
		return -EFAULT;

	might_fault();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		"move\t$6, %3\n\t"
		__MODULE_JAL(__strncpy_from_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (__to), "r" (__from), "r" (__len)
		: "$2", "$3", "$4", "$5", "$6", __UA_t0, "$31", "memory");

	return res;
}

extern long __strnlen_user_asm(const char __user *s, long n);

/*
 * strnlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
static inline long strnlen_user(const char __user *s, long n)
{
	long res;

	if (!access_ok(s, 1))
		return 0;

	might_fault();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		__MODULE_JAL(__strnlen_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s), "r" (n)
		: "$2", "$4", "$5", __UA_t0, "$31");

	return res;
}

#endif /* _ASM_UACCESS_H */
