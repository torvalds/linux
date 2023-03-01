/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1996, 1997, 1998, 1999, 2000, 03, 04 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2007  Maciej W. Rozycki
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/extable.h>
#include <asm/pgtable.h>
#include <asm/extable.h>
#include <asm/asm-extable.h>
#include <asm-generic/access_ok.h>

extern u64 __ua_limit;

#define __UA_ADDR	".dword"
#define __UA_LIMIT	__ua_limit

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
#define get_user(x, ptr) \
({									\
	const __typeof__(*(ptr)) __user *__p = (ptr);			\
									\
	might_fault();							\
	access_ok(__p, sizeof(*__p)) ? __get_user((x), __p) :		\
				       ((x) = 0, -EFAULT);		\
})

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
#define put_user(x, ptr) \
({									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
									\
	might_fault();							\
	access_ok(__p, sizeof(*__p)) ? __put_user((x), __p) : -EFAULT;	\
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
#define __get_user(x, ptr) \
({									\
	int __gu_err = 0;						\
									\
	__chk_user_ptr(ptr);						\
	__get_user_common((x), sizeof(*(ptr)), ptr);			\
	__gu_err;							\
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
#define __put_user(x, ptr) \
({									\
	int __pu_err = 0;						\
	__typeof__(*(ptr)) __pu_val;					\
									\
	__pu_val = (x);							\
	__chk_user_ptr(ptr);						\
	__put_user_common(ptr, sizeof(*(ptr)));				\
	__pu_err;							\
})

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#define __get_user_common(val, size, ptr)				\
do {									\
	switch (size) {							\
	case 1: __get_data_asm(val, "ld.b", ptr); break;		\
	case 2: __get_data_asm(val, "ld.h", ptr); break;		\
	case 4: __get_data_asm(val, "ld.w", ptr); break;		\
	case 8: __get_data_asm(val, "ld.d", ptr); break;		\
	default: BUILD_BUG(); break;					\
	}								\
} while (0)

#define __get_kernel_common(val, size, ptr) __get_user_common(val, size, ptr)

#define __get_data_asm(val, insn, ptr)					\
{									\
	long __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	" insn "	%1, %2				\n"	\
	"2:							\n"	\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(1b, 2b, %0, %1)			\
	: "+r" (__gu_err), "=r" (__gu_tmp)				\
	: "m" (__m(ptr)));						\
									\
	(val) = (__typeof__(*(ptr))) __gu_tmp;				\
}

#define __put_user_common(ptr, size)					\
do {									\
	switch (size) {							\
	case 1: __put_data_asm("st.b", ptr); break;			\
	case 2: __put_data_asm("st.h", ptr); break;			\
	case 4: __put_data_asm("st.w", ptr); break;			\
	case 8: __put_data_asm("st.d", ptr); break;			\
	default: BUILD_BUG(); break;					\
	}								\
} while (0)

#define __put_kernel_common(ptr, size) __put_user_common(ptr, size)

#define __put_data_asm(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	" insn "	%z2, %1		# __put_user_asm\n"	\
	"2:							\n"	\
	_ASM_EXTABLE_UACCESS_ERR(1b, 2b, %0)				\
	: "+r" (__pu_err), "=m" (__m(ptr))				\
	: "Jr" (__pu_val));						\
}

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	int __gu_err = 0;						\
									\
	__get_kernel_common(*((type *)(dst)), sizeof(type),		\
			    (__force type *)(src));			\
	if (unlikely(__gu_err))						\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	type __pu_val;							\
	int __pu_err = 0;						\
									\
	__pu_val = *(__force type *)(src);				\
	__put_kernel_common(((type *)(dst)), sizeof(type));		\
	if (unlikely(__pu_err))						\
		goto err_label;						\
} while (0)

extern unsigned long __copy_user(void *to, const void *from, __kernel_size_t n);

static inline unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_user(to, (__force const void *)from, n);
}

static inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user((__force void *)to, from, n);
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @addr: Destination address, in user space.
 * @size: Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
extern unsigned long __clear_user(void __user *addr, __kernel_size_t size);

#define clear_user(addr, n)						\
({									\
	void __user *__cl_addr = (addr);				\
	unsigned long __cl_size = (n);					\
	if (__cl_size && access_ok(__cl_addr, __cl_size))		\
		__cl_size = __clear_user(__cl_addr, __cl_size);		\
	__cl_size;							\
})

extern long strncpy_from_user(char *to, const char __user *from, long n);
extern long strnlen_user(const char __user *str, long n);

#endif /* _ASM_UACCESS_H */
