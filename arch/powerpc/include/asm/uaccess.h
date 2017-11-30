/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_POWERPC_UACCESS_H
#define _ARCH_POWERPC_UACCESS_H

#include <asm/asm-compat.h>
#include <asm/ppc_asm.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/extable.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 *
 * The fs/ds values are now the highest legal address in the "segment".
 * This simplifies the checking in the routines below.
 */

#define MAKE_MM_SEG(s)  ((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(~0UL)
#ifdef __powerpc64__
/* We use TASK_SIZE_USER64 as TASK_SIZE is not constant */
#define USER_DS		MAKE_MM_SEG(TASK_SIZE_USER64 - 1)
#else
#define USER_DS		MAKE_MM_SEG(TASK_SIZE - 1)
#endif

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->thread.fs)
#define set_fs(val)	(current->thread.fs = (val))

#define segment_eq(a, b)	((a).seg == (b).seg)

#define user_addr_max()	(get_fs().seg)

#ifdef __powerpc64__
/*
 * This check is sufficient because there is a large enough
 * gap between user addresses and the kernel addresses
 */
#define __access_ok(addr, size, segment)	\
	(((addr) <= (segment).seg) && ((size) <= (segment).seg))

#else

#define __access_ok(addr, size, segment)	\
	(((addr) <= (segment).seg) &&		\
	 (((size) == 0) || (((size) - 1) <= ((segment).seg - (addr)))))

#endif

#define access_ok(type, addr, size)		\
	(__chk_user_ptr(addr),			\
	 __access_ok((__force unsigned long)(addr), (size), get_fs()))

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 *
 * As we use the same address space for kernel and user data on the
 * PowerPC, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 *
 */
#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))
#define put_user(x, ptr) \
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user_inatomic(x, ptr) \
	__get_user_nosleep((x), (ptr), sizeof(*(ptr)))
#define __put_user_inatomic(x, ptr) \
	__put_user_nosleep((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

extern long __put_user_bad(void);

/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 */
#define __put_user_asm(x, addr, err, op)			\
	__asm__ __volatile__(					\
		"1:	" op " %1,0(%2)	# put_user\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	li %0,%3\n"				\
		"	b 2b\n"					\
		".previous\n"					\
		EX_TABLE(1b, 3b)				\
		: "=r" (err)					\
		: "r" (x), "b" (addr), "i" (-EFAULT), "0" (err))

#ifdef __powerpc64__
#define __put_user_asm2(x, ptr, retval)				\
	  __put_user_asm(x, ptr, retval, "std")
#else /* __powerpc64__ */
#define __put_user_asm2(x, addr, err)				\
	__asm__ __volatile__(					\
		"1:	stw %1,0(%2)\n"				\
		"2:	stw %1+1,4(%2)\n"			\
		"3:\n"						\
		".section .fixup,\"ax\"\n"			\
		"4:	li %0,%3\n"				\
		"	b 3b\n"					\
		".previous\n"					\
		EX_TABLE(1b, 4b)				\
		EX_TABLE(2b, 4b)				\
		: "=r" (err)					\
		: "r" (x), "b" (addr), "i" (-EFAULT), "0" (err))
#endif /* __powerpc64__ */

#define __put_user_size(x, ptr, size, retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	  case 1: __put_user_asm(x, ptr, retval, "stb"); break;	\
	  case 2: __put_user_asm(x, ptr, retval, "sth"); break;	\
	  case 4: __put_user_asm(x, ptr, retval, "stw"); break;	\
	  case 8: __put_user_asm2(x, ptr, retval); break;	\
	  default: __put_user_bad();				\
	}							\
} while (0)

#define __put_user_nocheck(x, ptr, size)			\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	if (!is_kernel_addr((unsigned long)__pu_addr))		\
		might_fault();					\
	__chk_user_ptr(ptr);					\
	__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;						\
})

#define __put_user_check(x, ptr, size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	might_fault();							\
	if (access_ok(VERIFY_WRITE, __pu_addr, size))			\
		__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;							\
})

#define __put_user_nosleep(x, ptr, size)			\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__chk_user_ptr(ptr);					\
	__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;						\
})


extern long __get_user_bad(void);

/*
 * This does an atomic 128 byte aligned load from userspace.
 * Upto caller to do enable_kernel_vmx() before calling!
 */
#define __get_user_atomic_128_aligned(kaddr, uaddr, err)		\
	__asm__ __volatile__(				\
		"1:	lvx  0,0,%1	# get user\n"	\
		" 	stvx 0,0,%2	# put kernel\n"	\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,%3\n"			\
		"	b 2b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 3b)			\
		: "=r" (err)			\
		: "b" (uaddr), "b" (kaddr), "i" (-EFAULT), "0" (err))

#define __get_user_asm(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %1,0(%2)	# get_user\n"	\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,%3\n"			\
		"	li %1,0\n"			\
		"	b 2b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 3b)			\
		: "=r" (err), "=r" (x)			\
		: "b" (addr), "i" (-EFAULT), "0" (err))

#ifdef __powerpc64__
#define __get_user_asm2(x, addr, err)			\
	__get_user_asm(x, addr, err, "ld")
#else /* __powerpc64__ */
#define __get_user_asm2(x, addr, err)			\
	__asm__ __volatile__(				\
		"1:	lwz %1,0(%2)\n"			\
		"2:	lwz %1+1,4(%2)\n"		\
		"3:\n"					\
		".section .fixup,\"ax\"\n"		\
		"4:	li %0,%3\n"			\
		"	li %1,0\n"			\
		"	li %1+1,0\n"			\
		"	b 3b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 4b)			\
		EX_TABLE(2b, 4b)			\
		: "=r" (err), "=&r" (x)			\
		: "b" (addr), "i" (-EFAULT), "0" (err))
#endif /* __powerpc64__ */

#define __get_user_size(x, ptr, size, retval)			\
do {								\
	retval = 0;						\
	__chk_user_ptr(ptr);					\
	if (size > sizeof(x))					\
		(x) = __get_user_bad();				\
	switch (size) {						\
	case 1: __get_user_asm(x, ptr, retval, "lbz"); break;	\
	case 2: __get_user_asm(x, ptr, retval, "lhz"); break;	\
	case 4: __get_user_asm(x, ptr, retval, "lwz"); break;	\
	case 8: __get_user_asm2(x, ptr, retval);  break;	\
	default: (x) = __get_user_bad();			\
	}							\
} while (0)

#define __get_user_nocheck(x, ptr, size)			\
({								\
	long __gu_err;						\
	unsigned long __gu_val;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__chk_user_ptr(ptr);					\
	if (!is_kernel_addr((unsigned long)__gu_addr))		\
		might_fault();					\
	__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x, ptr, size)					\
({									\
	long __gu_err = -EFAULT;					\
	unsigned long  __gu_val = 0;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	might_fault();							\
	if (access_ok(VERIFY_READ, __gu_addr, (size)))			\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_nosleep(x, ptr, size)			\
({								\
	long __gu_err;						\
	unsigned long __gu_val;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__chk_user_ptr(ptr);					\
	__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})


/* more complex routines */

extern unsigned long __copy_tofrom_user(void __user *to,
		const void __user *from, unsigned long size);

#ifdef __powerpc64__
static inline unsigned long
raw_copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	return __copy_tofrom_user(to, from, n);
}
#endif /* __powerpc64__ */

static inline unsigned long raw_copy_from_user(void *to,
		const void __user *from, unsigned long n)
{
	if (__builtin_constant_p(n) && (n <= 8)) {
		unsigned long ret = 1;

		switch (n) {
		case 1:
			__get_user_size(*(u8 *)to, from, 1, ret);
			break;
		case 2:
			__get_user_size(*(u16 *)to, from, 2, ret);
			break;
		case 4:
			__get_user_size(*(u32 *)to, from, 4, ret);
			break;
		case 8:
			__get_user_size(*(u64 *)to, from, 8, ret);
			break;
		}
		if (ret == 0)
			return 0;
	}

	return __copy_tofrom_user((__force void __user *)to, from, n);
}

static inline unsigned long raw_copy_to_user(void __user *to,
		const void *from, unsigned long n)
{
	if (__builtin_constant_p(n) && (n <= 8)) {
		unsigned long ret = 1;

		switch (n) {
		case 1:
			__put_user_size(*(u8 *)from, (u8 __user *)to, 1, ret);
			break;
		case 2:
			__put_user_size(*(u16 *)from, (u16 __user *)to, 2, ret);
			break;
		case 4:
			__put_user_size(*(u32 *)from, (u32 __user *)to, 4, ret);
			break;
		case 8:
			__put_user_size(*(u64 *)from, (u64 __user *)to, 8, ret);
			break;
		}
		if (ret == 0)
			return 0;
	}

	return __copy_tofrom_user(to, (__force const void __user *)from, n);
}

extern unsigned long __clear_user(void __user *addr, unsigned long size);

static inline unsigned long clear_user(void __user *addr, unsigned long size)
{
	might_fault();
	if (likely(access_ok(VERIFY_WRITE, addr, size)))
		return __clear_user(addr, size);
	return size;
}

extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern __must_check long strnlen_user(const char __user *str, long n);

extern long __copy_from_user_flushcache(void *dst, const void __user *src,
		unsigned size);
extern void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
			   size_t len);

#endif	/* _ARCH_POWERPC_UACCESS_H */
