/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/uaccess.h"
 */
#ifndef __S390_UACCESS_H
#define __S390_UACCESS_H

/*
 * User space memory access functions
 */
#include <asm/asm-extable.h>
#include <asm/processor.h>
#include <asm/ctl_reg.h>
#include <asm/extable.h>
#include <asm/facility.h>
#include <asm-generic/access_ok.h>

void debug_user_asce(int exit);

unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n);

unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n);

#ifndef CONFIG_KASAN
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER
#endif

unsigned long __must_check
_copy_from_user_key(void *to, const void __user *from, unsigned long n, unsigned long key);

static __always_inline unsigned long __must_check
copy_from_user_key(void *to, const void __user *from, unsigned long n, unsigned long key)
{
	if (likely(check_copy_size(to, n, false)))
		n = _copy_from_user_key(to, from, n, key);
	return n;
}

unsigned long __must_check
_copy_to_user_key(void __user *to, const void *from, unsigned long n, unsigned long key);

static __always_inline unsigned long __must_check
copy_to_user_key(void __user *to, const void *from, unsigned long n, unsigned long key)
{
	if (likely(check_copy_size(from, n, true)))
		n = _copy_to_user_key(to, from, n, key);
	return n;
}

int __put_user_bad(void) __attribute__((noreturn));
int __get_user_bad(void) __attribute__((noreturn));

union oac {
	unsigned int val;
	struct {
		struct {
			unsigned short key : 4;
			unsigned short	   : 4;
			unsigned short as  : 2;
			unsigned short	   : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac1;
		struct {
			unsigned short key : 4;
			unsigned short	   : 4;
			unsigned short as  : 2;
			unsigned short	   : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac2;
	};
};

#define __put_get_user_asm(to, from, size, oac_spec)			\
({									\
	int __rc;							\
									\
	asm volatile(							\
		"	lr	0,%[spec]\n"				\
		"0:	mvcos	%[_to],%[_from],%[_size]\n"		\
		"1:	xr	%[rc],%[rc]\n"				\
		"2:\n"							\
		EX_TABLE_UA(0b,2b,%[rc]) EX_TABLE_UA(1b,2b,%[rc])	\
		: [rc] "=&d" (__rc), [_to] "+Q" (*(to))			\
		: [_size] "d" (size), [_from] "Q" (*(from)),		\
		  [spec] "d" (oac_spec.val)				\
		: "cc", "0");						\
	__rc;								\
})

#define __put_user_asm(to, from, size)				\
	__put_get_user_asm(to, from, size, ((union oac) {	\
		.oac1.as = PSW_BITS_AS_SECONDARY,		\
		.oac1.a = 1					\
	}))

#define __get_user_asm(to, from, size)				\
	__put_get_user_asm(to, from, size, ((union oac) {	\
		.oac2.as = PSW_BITS_AS_SECONDARY,		\
		.oac2.a = 1					\
	}))							\

static __always_inline int __put_user_fn(void *x, void __user *ptr, unsigned long size)
{
	int rc;

	switch (size) {
	case 1:
		rc = __put_user_asm((unsigned char __user *)ptr,
				    (unsigned char *)x,
				    size);
		break;
	case 2:
		rc = __put_user_asm((unsigned short __user *)ptr,
				    (unsigned short *)x,
				    size);
		break;
	case 4:
		rc = __put_user_asm((unsigned int __user *)ptr,
				    (unsigned int *)x,
				    size);
		break;
	case 8:
		rc = __put_user_asm((unsigned long __user *)ptr,
				    (unsigned long *)x,
				    size);
		break;
	default:
		__put_user_bad();
		break;
	}
	return rc;
}

static __always_inline int __get_user_fn(void *x, const void __user *ptr, unsigned long size)
{
	int rc;

	switch (size) {
	case 1:
		rc = __get_user_asm((unsigned char *)x,
				    (unsigned char __user *)ptr,
				    size);
		break;
	case 2:
		rc = __get_user_asm((unsigned short *)x,
				    (unsigned short __user *)ptr,
				    size);
		break;
	case 4:
		rc = __get_user_asm((unsigned int *)x,
				    (unsigned int __user *)ptr,
				    size);
		break;
	case 8:
		rc = __get_user_asm((unsigned long *)x,
				    (unsigned long __user *)ptr,
				    size);
		break;
	default:
		__get_user_bad();
		break;
	}
	return rc;
}

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */
#define __put_user(x, ptr) \
({								\
	__typeof__(*(ptr)) __x = (x);				\
	int __pu_err = -EFAULT;					\
        __chk_user_ptr(ptr);                                    \
	switch (sizeof (*(ptr))) {				\
	case 1:							\
	case 2:							\
	case 4:							\
	case 8:							\
		__pu_err = __put_user_fn(&__x, ptr,		\
					 sizeof(*(ptr)));	\
		break;						\
	default:						\
		__put_user_bad();				\
		break;						\
	}							\
	__builtin_expect(__pu_err, 0);				\
})

#define put_user(x, ptr)					\
({								\
	might_fault();						\
	__put_user(x, ptr);					\
})


#define __get_user(x, ptr)					\
({								\
	int __gu_err = -EFAULT;					\
	__chk_user_ptr(ptr);					\
	switch (sizeof(*(ptr))) {				\
	case 1: {						\
		unsigned char __x = 0;				\
		__gu_err = __get_user_fn(&__x, ptr,		\
					 sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 2: {						\
		unsigned short __x = 0;				\
		__gu_err = __get_user_fn(&__x, ptr,		\
					 sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 4: {						\
		unsigned int __x = 0;				\
		__gu_err = __get_user_fn(&__x, ptr,		\
					 sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 8: {						\
		unsigned long long __x = 0;			\
		__gu_err = __get_user_fn(&__x, ptr,		\
					 sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	default:						\
		__get_user_bad();				\
		break;						\
	}							\
	__builtin_expect(__gu_err, 0);				\
})

#define get_user(x, ptr)					\
({								\
	might_fault();						\
	__get_user(x, ptr);					\
})

/*
 * Copy a null terminated string from userspace.
 */
long __must_check strncpy_from_user(char *dst, const char __user *src, long count);

long __must_check strnlen_user(const char __user *src, long count);

/*
 * Zero Userspace
 */
unsigned long __must_check __clear_user(void __user *to, unsigned long size);

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return __clear_user(to, n);
}

int copy_to_user_real(void __user *dest, unsigned long src, unsigned long count);
void *s390_kernel_write(void *dst, const void *src, size_t size);

int __noreturn __put_kernel_bad(void);

#define __put_kernel_asm(val, to, insn)					\
({									\
	int __rc;							\
									\
	asm volatile(							\
		"0:   " insn "  %2,%1\n"				\
		"1:	xr	%0,%0\n"				\
		"2:\n"							\
		EX_TABLE_UA(0b,2b,%0) EX_TABLE_UA(1b,2b,%0)		\
		: "=d" (__rc), "+Q" (*(to))				\
		: "d" (val)						\
		: "cc");						\
	__rc;								\
})

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	u64 __x = (u64)(*((type *)(src)));				\
	int __pk_err;							\
									\
	switch (sizeof(type)) {						\
	case 1:								\
		__pk_err = __put_kernel_asm(__x, (type *)(dst), "stc"); \
		break;							\
	case 2:								\
		__pk_err = __put_kernel_asm(__x, (type *)(dst), "sth"); \
		break;							\
	case 4:								\
		__pk_err = __put_kernel_asm(__x, (type *)(dst), "st");	\
		break;							\
	case 8:								\
		__pk_err = __put_kernel_asm(__x, (type *)(dst), "stg"); \
		break;							\
	default:							\
		__pk_err = __put_kernel_bad();				\
		break;							\
	}								\
	if (unlikely(__pk_err))						\
		goto err_label;						\
} while (0)

int __noreturn __get_kernel_bad(void);

#define __get_kernel_asm(val, from, insn)				\
({									\
	int __rc;							\
									\
	asm volatile(							\
		"0:   " insn "  %1,%2\n"				\
		"1:	xr	%0,%0\n"				\
		"2:\n"							\
		EX_TABLE_UA(0b,2b,%0) EX_TABLE_UA(1b,2b,%0)		\
		: "=d" (__rc), "+d" (val)				\
		: "Q" (*(from))						\
		: "cc");						\
	__rc;								\
})

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	int __gk_err;							\
									\
	switch (sizeof(type)) {						\
	case 1: {							\
		u8 __x = 0;						\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "ic");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 2: {							\
		u16 __x = 0;						\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "lh");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 4: {							\
		u32 __x = 0;						\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "l");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 8: {							\
		u64 __x = 0;						\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "lg");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	default:							\
		__gk_err = __get_kernel_bad();				\
		break;							\
	}								\
	if (unlikely(__gk_err))						\
		goto err_label;						\
} while (0)

#endif /* __S390_UACCESS_H */
