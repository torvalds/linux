/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_UACCESS_H
#define __ASM_AVR32_UACCESS_H

#include <linux/errno.h>
#include <linux/sched.h>

typedef struct {
	unsigned int is_user_space;
} mm_segment_t;

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons (Data Segment Register?), these macros are misnamed.
 */
#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })
#define segment_eq(a, b)	((a).is_user_space == (b).is_user_space)

#define USER_ADDR_LIMIT 0x80000000

#define KERNEL_DS	MAKE_MM_SEG(0)
#define USER_DS		MAKE_MM_SEG(1)

#define get_ds()	(KERNEL_DS)

static inline mm_segment_t get_fs(void)
{
	return MAKE_MM_SEG(test_thread_flag(TIF_USERSPACE));
}

static inline void set_fs(mm_segment_t s)
{
	if (s.is_user_space)
		set_thread_flag(TIF_USERSPACE);
	else
		clear_thread_flag(TIF_USERSPACE);
}

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 *
 * We do the following checks:
 *   1. Is the access from kernel space?
 *   2. Does (addr + size) set the carry bit?
 *   3. Is (addr + size) a negative number (i.e. >= 0x80000000)?
 *
 * If yes on the first check, access is granted.
 * If no on any of the others, access is denied.
 */
#define __range_ok(addr, size)						\
	(test_thread_flag(TIF_USERSPACE)				\
	 && (((unsigned long)(addr) >= 0x80000000)			\
	     || ((unsigned long)(size) > 0x80000000)			\
	     || (((unsigned long)(addr) + (unsigned long)(size)) > 0x80000000)))

#define access_ok(type, addr, size) (likely(__range_ok(addr, size) == 0))

/* Generic arbitrary sized copy. Return the number of bytes NOT copied */
extern __kernel_size_t __copy_user(void *to, const void *from,
				   __kernel_size_t n);

extern __kernel_size_t copy_to_user(void __user *to, const void *from,
				    __kernel_size_t n);
extern __kernel_size_t ___copy_from_user(void *to, const void __user *from,
				      __kernel_size_t n);

static inline __kernel_size_t __copy_to_user(void __user *to, const void *from,
					     __kernel_size_t n)
{
	return __copy_user((void __force *)to, from, n);
}
static inline __kernel_size_t __copy_from_user(void *to,
					       const void __user *from,
					       __kernel_size_t n)
{
	return __copy_user(to, (const void __force *)from, n);
}
static inline __kernel_size_t copy_from_user(void *to,
					       const void __user *from,
					       __kernel_size_t n)
{
	size_t res = ___copy_from_user(to, from, n);
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/*
 * put_user: - Write a simple value into user space.
 * @x:   Value to copy to user space.
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
#define put_user(x, ptr)	\
	__put_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * get_user: - Get a simple variable from user space.
 * @x:   Variable to store result.
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
#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
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
#define __put_user(x, ptr) \
	__put_user_nocheck((x), (ptr), sizeof(*(ptr)))

/*
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:   Variable to store result.
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
#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

extern int __get_user_bad(void);
extern int __put_user_bad(void);

#define __get_user_nocheck(x, ptr, size)				\
({									\
	unsigned long __gu_val = 0;					\
	int __gu_err = 0;						\
									\
	switch (size) {							\
	case 1: __get_user_asm("ub", __gu_val, ptr, __gu_err); break;	\
	case 2: __get_user_asm("uh", __gu_val, ptr, __gu_err); break;	\
	case 4: __get_user_asm("w", __gu_val, ptr, __gu_err); break;	\
	default: __gu_err = __get_user_bad(); break;			\
	}								\
									\
	x = (__force typeof(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_check(x, ptr, size)					\
({									\
	unsigned long __gu_val = 0;					\
	const typeof(*(ptr)) __user * __gu_addr = (ptr);		\
	int __gu_err = 0;						\
									\
	if (access_ok(VERIFY_READ, __gu_addr, size)) {			\
		switch (size) {						\
		case 1:							\
			__get_user_asm("ub", __gu_val, __gu_addr,	\
				       __gu_err);			\
			break;						\
		case 2:							\
			__get_user_asm("uh", __gu_val, __gu_addr,	\
				       __gu_err);			\
			break;						\
		case 4:							\
			__get_user_asm("w", __gu_val, __gu_addr,	\
				       __gu_err);			\
			break;						\
		default:						\
			__gu_err = __get_user_bad();			\
			break;						\
		}							\
	} else {							\
		__gu_err = -EFAULT;					\
	}								\
	x = (__force typeof(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_asm(suffix, __gu_val, ptr, __gu_err)			\
	asm volatile(							\
		"1:	ld." suffix "	%1, %3			\n"	\
		"2:						\n"	\
		"	.subsection 1				\n"	\
		"3:	mov	%0, %4				\n"	\
		"	rjmp	2b				\n"	\
		"	.subsection 0				\n"	\
		"	.section __ex_table, \"a\"		\n"	\
		"	.long	1b, 3b				\n"	\
		"	.previous				\n"	\
		: "=r"(__gu_err), "=r"(__gu_val)			\
		: "0"(__gu_err), "m"(*(ptr)), "i"(-EFAULT))

#define __put_user_nocheck(x, ptr, size)				\
({									\
	typeof(*(ptr)) __pu_val;					\
	int __pu_err = 0;						\
									\
	__pu_val = (x);							\
	switch (size) {							\
	case 1: __put_user_asm("b", ptr, __pu_val, __pu_err); break;	\
	case 2: __put_user_asm("h", ptr, __pu_val, __pu_err); break;	\
	case 4: __put_user_asm("w", ptr, __pu_val, __pu_err); break;	\
	case 8: __put_user_asm("d", ptr, __pu_val, __pu_err); break;	\
	default: __pu_err = __put_user_bad(); break;			\
	}								\
	__pu_err;							\
})

#define __put_user_check(x, ptr, size)					\
({									\
	typeof(*(ptr)) __pu_val;					\
	typeof(*(ptr)) __user *__pu_addr = (ptr);			\
	int __pu_err = 0;						\
									\
	__pu_val = (x);							\
	if (access_ok(VERIFY_WRITE, __pu_addr, size)) {			\
		switch (size) {						\
		case 1:							\
			__put_user_asm("b", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 2:							\
			__put_user_asm("h", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 4:							\
			__put_user_asm("w", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 8:							\
			__put_user_asm("d", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		default:						\
			__pu_err = __put_user_bad();			\
			break;						\
		}							\
	} else {							\
		__pu_err = -EFAULT;					\
	}								\
	__pu_err;							\
})

#define __put_user_asm(suffix, ptr, __pu_val, __gu_err)			\
	asm volatile(							\
		"1:	st." suffix "	%1, %3			\n"	\
		"2:						\n"	\
		"	.subsection 1				\n"	\
		"3:	mov	%0, %4				\n"	\
		"	rjmp	2b				\n"	\
		"	.subsection 0				\n"	\
		"	.section __ex_table, \"a\"		\n"	\
		"	.long	1b, 3b				\n"	\
		"	.previous				\n"	\
		: "=r"(__gu_err), "=m"(*(ptr))				\
		: "0"(__gu_err), "r"(__pu_val), "i"(-EFAULT))

extern __kernel_size_t clear_user(void __user *addr, __kernel_size_t size);
extern __kernel_size_t __clear_user(void __user *addr, __kernel_size_t size);

extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern long __strncpy_from_user(char *dst, const char __user *src, long count);

extern long strnlen_user(const char __user *__s, long __n);
extern long __strnlen_user(const char __user *__s, long __n);

#define strlen_user(s) strnlen_user(s, ~0UL >> 1)

struct exception_table_entry
{
	unsigned long insn, fixup;
};

#endif /* __ASM_AVR32_UACCESS_H */
