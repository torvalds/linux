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
#include <asm/processor.h>
#include <asm/ctl_reg.h>
#include <asm/extable.h>
#include <asm/facility.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define KERNEL_DS	(0)
#define KERNEL_DS_SACF	(1)
#define USER_DS		(2)
#define USER_DS_SACF	(3)

#define get_fs()        (current->thread.mm_segment)
#define uaccess_kernel() ((get_fs() & 2) == KERNEL_DS)

void set_fs(mm_segment_t fs);

static inline int __range_ok(unsigned long addr, unsigned long size)
{
	return 1;
}

#define __access_ok(addr, size)				\
({							\
	__chk_user_ptr(addr);				\
	__range_ok((unsigned long)(addr), (size));	\
})

#define access_ok(addr, size) __access_ok(addr, size)

unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n);

unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n);

#ifndef CONFIG_KASAN
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER
#endif

int __put_user_bad(void) __attribute__((noreturn));
int __get_user_bad(void) __attribute__((noreturn));

#ifdef CONFIG_HAVE_MARCH_Z10_FEATURES

#define __put_get_user_asm(to, from, size, spec)		\
({								\
	register unsigned long __reg0 asm("0") = spec;		\
	int __rc;						\
								\
	asm volatile(						\
		"0:	mvcos	%1,%3,%2\n"			\
		"1:	xr	%0,%0\n"			\
		"2:\n"						\
		".pushsection .fixup, \"ax\"\n"			\
		"3:	lhi	%0,%5\n"			\
		"	jg	2b\n"				\
		".popsection\n"					\
		EX_TABLE(0b,3b) EX_TABLE(1b,3b)			\
		: "=d" (__rc), "+Q" (*(to))			\
		: "d" (size), "Q" (*(from)),			\
		  "d" (__reg0), "K" (-EFAULT)			\
		: "cc");					\
	__rc;							\
})

static __always_inline int __put_user_fn(void *x, void __user *ptr, unsigned long size)
{
	unsigned long spec = 0x010000UL;
	int rc;

	switch (size) {
	case 1:
		rc = __put_get_user_asm((unsigned char __user *)ptr,
					(unsigned char *)x,
					size, spec);
		break;
	case 2:
		rc = __put_get_user_asm((unsigned short __user *)ptr,
					(unsigned short *)x,
					size, spec);
		break;
	case 4:
		rc = __put_get_user_asm((unsigned int __user *)ptr,
					(unsigned int *)x,
					size, spec);
		break;
	case 8:
		rc = __put_get_user_asm((unsigned long __user *)ptr,
					(unsigned long *)x,
					size, spec);
		break;
	default:
		__put_user_bad();
		break;
	}
	return rc;
}

static __always_inline int __get_user_fn(void *x, const void __user *ptr, unsigned long size)
{
	unsigned long spec = 0x01UL;
	int rc;

	switch (size) {
	case 1:
		rc = __put_get_user_asm((unsigned char *)x,
					(unsigned char __user *)ptr,
					size, spec);
		break;
	case 2:
		rc = __put_get_user_asm((unsigned short *)x,
					(unsigned short __user *)ptr,
					size, spec);
		break;
	case 4:
		rc = __put_get_user_asm((unsigned int *)x,
					(unsigned int __user *)ptr,
					size, spec);
		break;
	case 8:
		rc = __put_get_user_asm((unsigned long *)x,
					(unsigned long __user *)ptr,
					size, spec);
		break;
	default:
		__get_user_bad();
		break;
	}
	return rc;
}

#else /* CONFIG_HAVE_MARCH_Z10_FEATURES */

static inline int __put_user_fn(void *x, void __user *ptr, unsigned long size)
{
	size = raw_copy_to_user(ptr, x, size);
	return size ? -EFAULT : 0;
}

static inline int __get_user_fn(void *x, const void __user *ptr, unsigned long size)
{
	size = raw_copy_from_user(x, ptr, size);
	return size ? -EFAULT : 0;
}

#endif /* CONFIG_HAVE_MARCH_Z10_FEATURES */

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

unsigned long __must_check
raw_copy_in_user(void __user *to, const void __user *from, unsigned long n);

/*
 * Copy a null terminated string from userspace.
 */

long __strncpy_from_user(char *dst, const char __user *src, long count);

static inline long __must_check
strncpy_from_user(char *dst, const char __user *src, long count)
{
	might_fault();
	return __strncpy_from_user(dst, src, count);
}

unsigned long __must_check __strnlen_user(const char __user *src, unsigned long count);

static inline unsigned long strnlen_user(const char __user *src, unsigned long n)
{
	might_fault();
	return __strnlen_user(src, n);
}

/*
 * Zero Userspace
 */
unsigned long __must_check __clear_user(void __user *to, unsigned long size);

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return __clear_user(to, n);
}

int copy_to_user_real(void __user *dest, void *src, unsigned long count);
void *s390_kernel_write(void *dst, const void *src, size_t size);

#define HAVE_GET_KERNEL_NOFAULT

int __noreturn __put_kernel_bad(void);

#define __put_kernel_asm(val, to, insn)					\
({									\
	int __rc;							\
									\
	asm volatile(							\
		"0:   " insn "  %2,%1\n"				\
		"1:	xr	%0,%0\n"				\
		"2:\n"							\
		".pushsection .fixup, \"ax\"\n"				\
		"3:	lhi	%0,%3\n"				\
		"	jg	2b\n"					\
		".popsection\n"						\
		EX_TABLE(0b,3b) EX_TABLE(1b,3b)				\
		: "=d" (__rc), "+Q" (*(to))				\
		: "d" (val), "K" (-EFAULT)				\
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
		".pushsection .fixup, \"ax\"\n"				\
		"3:	lhi	%0,%3\n"				\
		"	jg	2b\n"					\
		".popsection\n"						\
		EX_TABLE(0b,3b) EX_TABLE(1b,3b)				\
		: "=d" (__rc), "+d" (val)				\
		: "Q" (*(from)), "K" (-EFAULT)				\
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
