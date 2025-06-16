/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/uaccess.h"
 */
#ifndef __S390_UACCESS_H
#define __S390_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/pgtable.h>
#include <asm/asm-extable.h>
#include <asm/processor.h>
#include <asm/extable.h>
#include <asm/facility.h>
#include <asm-generic/access_ok.h>
#include <asm/asce.h>
#include <linux/instrumented.h>

void debug_user_asce(int exit);

#ifdef CONFIG_KMSAN
#define uaccess_kmsan_or_inline noinline __maybe_unused __no_sanitize_memory
#else
#define uaccess_kmsan_or_inline __always_inline
#endif

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

static uaccess_kmsan_or_inline __must_check unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long size)
{
	unsigned long osize;
	int cc;

	while (1) {
		osize = size;
		asm_inline volatile(
			"	lhi	%%r0,%[spec]\n"
			"0:	mvcos	%[to],%[from],%[size]\n"
			"1:	nopr	%%r7\n"
			CC_IPM(cc)
			EX_TABLE_UA_MVCOS_FROM(0b, 0b)
			EX_TABLE_UA_MVCOS_FROM(1b, 0b)
			: CC_OUT(cc, cc), [size] "+d" (size), [to] "=Q" (*(char *)to)
			: [spec] "I" (0x81), [from] "Q" (*(const char __user *)from)
			: CC_CLOBBER_LIST("memory", "0"));
		if (__builtin_constant_p(osize) && osize <= 4096)
			return osize - size;
		if (likely(CC_TRANSFORM(cc) == 0))
			return osize - size;
		size -= 4096;
		to += 4096;
		from += 4096;
	}
}

static uaccess_kmsan_or_inline __must_check unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long size)
{
	unsigned long osize;
	int cc;

	while (1) {
		osize = size;
		asm_inline volatile(
			"	llilh	%%r0,%[spec]\n"
			"0:	mvcos	%[to],%[from],%[size]\n"
			"1:	nopr	%%r7\n"
			CC_IPM(cc)
			EX_TABLE_UA_MVCOS_TO(0b, 0b)
			EX_TABLE_UA_MVCOS_TO(1b, 0b)
			: CC_OUT(cc, cc), [size] "+d" (size), [to] "=Q" (*(char __user *)to)
			: [spec] "I" (0x81), [from] "Q" (*(const char *)from)
			: CC_CLOBBER_LIST("memory", "0"));
		if (__builtin_constant_p(osize) && osize <= 4096)
			return osize - size;
		if (likely(CC_TRANSFORM(cc) == 0))
			return osize - size;
		size -= 4096;
		to += 4096;
		from += 4096;
	}
}

unsigned long __must_check
_copy_from_user_key(void *to, const void __user *from, unsigned long n, unsigned long key);

static __always_inline unsigned long __must_check
copy_from_user_key(void *to, const void __user *from, unsigned long n, unsigned long key)
{
	if (check_copy_size(to, n, false))
		n = _copy_from_user_key(to, from, n, key);
	return n;
}

unsigned long __must_check
_copy_to_user_key(void __user *to, const void *from, unsigned long n, unsigned long key);

static __always_inline unsigned long __must_check
copy_to_user_key(void __user *to, const void *from, unsigned long n, unsigned long key)
{
	if (check_copy_size(from, n, true))
		n = _copy_to_user_key(to, from, n, key);
	return n;
}

int __noreturn __put_user_bad(void);

#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT

#define DEFINE_PUT_USER_NOINSTR(type)					\
static uaccess_kmsan_or_inline int					\
__put_user_##type##_noinstr(unsigned type __user *to,			\
			    unsigned type *from,			\
			    unsigned long size)				\
{									\
	asm goto(							\
		"	llilh	%%r0,%[spec]\n"				\
		"0:	mvcos	%[to],%[from],%[size]\n"		\
		"1:	nopr	%%r7\n"					\
		EX_TABLE(0b, %l[Efault])				\
		EX_TABLE(1b, %l[Efault])				\
		: [to] "+Q" (*to)					\
		: [size] "d" (size), [from] "Q" (*from),		\
		  [spec] "I" (0x81)					\
		: "cc", "0"						\
		: Efault						\
		);							\
	return 0;							\
Efault:									\
	return -EFAULT;							\
}

#else /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

#define DEFINE_PUT_USER_NOINSTR(type)					\
static uaccess_kmsan_or_inline int					\
__put_user_##type##_noinstr(unsigned type __user *to,			\
			    unsigned type *from,			\
			    unsigned long size)				\
{									\
	int rc;								\
									\
	asm_inline volatile(						\
		"	llilh	%%r0,%[spec]\n"				\
		"0:	mvcos	%[to],%[from],%[size]\n"		\
		"1:	lhi	%[rc],0\n"				\
		"2:\n"							\
		EX_TABLE_UA_FAULT(0b, 2b, %[rc])			\
		EX_TABLE_UA_FAULT(1b, 2b, %[rc])			\
		: [rc] "=d" (rc), [to] "+Q" (*to)			\
		: [size] "d" (size), [from] "Q" (*from),		\
		  [spec] "I" (0x81)					\
		: "cc", "0");						\
	return rc;							\
}

#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

DEFINE_PUT_USER_NOINSTR(char);
DEFINE_PUT_USER_NOINSTR(short);
DEFINE_PUT_USER_NOINSTR(int);
DEFINE_PUT_USER_NOINSTR(long);

#define DEFINE_PUT_USER(type)						\
static __always_inline int						\
__put_user_##type(unsigned type __user *to, unsigned type *from,	\
		  unsigned long size)					\
{									\
	int rc;								\
									\
	rc = __put_user_##type##_noinstr(to, from, size);		\
	instrument_put_user(*from, to, size);				\
	return rc;							\
}

DEFINE_PUT_USER(char);
DEFINE_PUT_USER(short);
DEFINE_PUT_USER(int);
DEFINE_PUT_USER(long);

#define __put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __x = (x);					\
	int __prc;							\
									\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__prc = __put_user_char((unsigned char __user *)(ptr),	\
					(unsigned char *)&__x,		\
					sizeof(*(ptr)));		\
		break;							\
	case 2:								\
		__prc = __put_user_short((unsigned short __user *)(ptr),\
					 (unsigned short *)&__x,	\
					 sizeof(*(ptr)));		\
		break;							\
	case 4:								\
		__prc = __put_user_int((unsigned int __user *)(ptr),	\
				       (unsigned int *)&__x,		\
				       sizeof(*(ptr)));			\
		break;							\
	case 8:								\
		__prc = __put_user_long((unsigned long __user *)(ptr),	\
					(unsigned long *)&__x,		\
					sizeof(*(ptr)));		\
		break;							\
	default:							\
		__prc = __put_user_bad();				\
		break;							\
	}								\
	__builtin_expect(__prc, 0);					\
})

#define put_user(x, ptr)						\
({									\
	might_fault();							\
	__put_user(x, ptr);						\
})

int __noreturn __get_user_bad(void);

#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT

#define DEFINE_GET_USER_NOINSTR(type)					\
static uaccess_kmsan_or_inline int					\
__get_user_##type##_noinstr(unsigned type *to,				\
			    const unsigned type __user *from,		\
			    unsigned long size)				\
{									\
	asm goto(							\
		"	lhi	%%r0,%[spec]\n"				\
		"0:	mvcos	%[to],%[from],%[size]\n"		\
		"1:	nopr	%%r7\n"					\
		EX_TABLE(0b, %l[Efault])				\
		EX_TABLE(1b, %l[Efault])				\
		: [to] "=Q" (*to)					\
		: [size] "d" (size), [from] "Q" (*from),		\
		  [spec] "I" (0x81)					\
		: "cc", "0"						\
		: Efault						\
		);							\
	return 0;							\
Efault:									\
	*to = 0;							\
	return -EFAULT;							\
}

#else /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

#define DEFINE_GET_USER_NOINSTR(type)					\
static uaccess_kmsan_or_inline int					\
__get_user_##type##_noinstr(unsigned type *to,				\
			    const unsigned type __user *from,		\
			    unsigned long size)				\
{									\
	int rc;								\
									\
	asm_inline volatile(						\
		"	lhi	%%r0,%[spec]\n"				\
		"0:	mvcos	%[to],%[from],%[size]\n"		\
		"1:	lhi	%[rc],0\n"				\
		"2:\n"							\
		EX_TABLE_UA_FAULT(0b, 2b, %[rc])			\
		EX_TABLE_UA_FAULT(1b, 2b, %[rc])			\
		: [rc] "=d" (rc), [to] "=Q" (*to)			\
		: [size] "d" (size), [from] "Q" (*from),		\
		  [spec] "I" (0x81)					\
		: "cc", "0");						\
	if (likely(!rc))						\
		return 0;						\
	*to = 0;							\
	return rc;							\
}

#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

DEFINE_GET_USER_NOINSTR(char);
DEFINE_GET_USER_NOINSTR(short);
DEFINE_GET_USER_NOINSTR(int);
DEFINE_GET_USER_NOINSTR(long);

#define DEFINE_GET_USER(type)						\
static __always_inline int						\
__get_user_##type(unsigned type *to, const unsigned type __user *from,	\
		  unsigned long size)					\
{									\
	int rc;								\
									\
	rc = __get_user_##type##_noinstr(to, from, size);		\
	instrument_get_user(*to);					\
	return rc;							\
}

DEFINE_GET_USER(char);
DEFINE_GET_USER(short);
DEFINE_GET_USER(int);
DEFINE_GET_USER(long);

#define __get_user(x, ptr)						\
({									\
	const __user void *____guptr = (ptr);				\
	int __grc;							\
									\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1: {							\
		const unsigned char __user *__guptr = ____guptr;	\
		unsigned char __x;					\
									\
		__grc = __get_user_char(&__x, __guptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 2: {							\
		const unsigned short __user *__guptr = ____guptr;	\
		unsigned short __x;					\
									\
		__grc = __get_user_short(&__x, __guptr, sizeof(*(ptr)));\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 4: {							\
		const unsigned int __user *__guptr = ____guptr;		\
		unsigned int __x;					\
									\
		__grc = __get_user_int(&__x, __guptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 8: {							\
		const unsigned long __user *__guptr = ____guptr;	\
		unsigned long __x;					\
									\
		__grc = __get_user_long(&__x, __guptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	default:							\
		__grc = __get_user_bad();				\
		break;							\
	}								\
	__builtin_expect(__grc, 0);					\
})

#define get_user(x, ptr)						\
({									\
	might_fault();							\
	__get_user(x, ptr);						\
})

/*
 * Copy a null terminated string from userspace.
 */
long __must_check strncpy_from_user(char *dst, const char __user *src, long count);

long __must_check strnlen_user(const char __user *src, long count);

static uaccess_kmsan_or_inline __must_check unsigned long
__clear_user(void __user *to, unsigned long size)
{
	unsigned long osize;
	int cc;

	while (1) {
		osize = size;
		asm_inline volatile(
			"	llilh	%%r0,%[spec]\n"
			"0:	mvcos	%[to],%[from],%[size]\n"
			"1:	nopr	%%r7\n"
			CC_IPM(cc)
			EX_TABLE_UA_MVCOS_TO(0b, 0b)
			EX_TABLE_UA_MVCOS_TO(1b, 0b)
			: CC_OUT(cc, cc), [size] "+d" (size), [to] "=Q" (*(char __user *)to)
			: [spec] "I" (0x81), [from] "Q" (*(const char *)empty_zero_page)
			: CC_CLOBBER_LIST("memory", "0"));
		if (__builtin_constant_p(osize) && osize <= 4096)
			return osize - size;
		if (CC_TRANSFORM(cc) == 0)
			return osize - size;
		size -= 4096;
		to += 4096;
	}
}

static __always_inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return __clear_user(to, n);
}

void *__s390_kernel_write(void *dst, const void *src, size_t size);

static inline void *s390_kernel_write(void *dst, const void *src, size_t size)
{
	if (__is_defined(__DECOMPRESSOR))
		return memcpy(dst, src, size);
	return __s390_kernel_write(dst, src, size);
}

void __noreturn __mvc_kernel_nofault_bad(void);

#if defined(CONFIG_CC_HAS_ASM_GOTO_OUTPUT) && defined(CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS)

#define __mvc_kernel_nofault(dst, src, type, err_label)			\
do {									\
	switch (sizeof(type)) {						\
	case 1:								\
	case 2:								\
	case 4:								\
	case 8:								\
		asm goto(						\
			"0:	mvc	%O[_dst](%[_len],%R[_dst]),%[_src]\n" \
			"1:	nopr	%%r7\n"				\
			EX_TABLE(0b, %l[err_label])			\
			EX_TABLE(1b, %l[err_label])			\
			: [_dst] "=Q" (*(type *)dst)			\
			: [_src] "Q" (*(type *)(src)),			\
			  [_len] "I" (sizeof(type))			\
			:						\
			: err_label);					\
		break;							\
	default:							\
		__mvc_kernel_nofault_bad();				\
		break;							\
	}								\
} while (0)

#else /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT) && CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#define __mvc_kernel_nofault(dst, src, type, err_label)			\
do {									\
	type *(__dst) = (type *)(dst);					\
	int __rc;							\
									\
	switch (sizeof(type)) {						\
	case 1:								\
	case 2:								\
	case 4:								\
	case 8:								\
		asm_inline volatile(					\
			"0:	mvc	0(%[_len],%[_dst]),%[_src]\n"	\
			"1:	lhi	%[_rc],0\n"			\
			"2:\n"						\
			EX_TABLE_UA_FAULT(0b, 2b, %[_rc])		\
			EX_TABLE_UA_FAULT(1b, 2b, %[_rc])		\
			: [_rc] "=d" (__rc),				\
			  "=m" (*__dst)					\
			: [_src] "Q" (*(type *)(src)),			\
			[_dst] "a" (__dst),				\
			[_len] "I" (sizeof(type)));			\
		if (__rc)						\
			goto err_label;					\
		break;							\
	default:							\
		__mvc_kernel_nofault_bad();				\
		break;							\
	}								\
} while (0)

#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT && CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#define __get_kernel_nofault __mvc_kernel_nofault
#define __put_kernel_nofault __mvc_kernel_nofault

void __cmpxchg_user_key_called_with_bad_pointer(void);

int __cmpxchg_user_key1(unsigned long address, unsigned char *uval,
			unsigned char old, unsigned char new, unsigned long key);
int __cmpxchg_user_key2(unsigned long address, unsigned short *uval,
			unsigned short old, unsigned short new, unsigned long key);
int __cmpxchg_user_key4(unsigned long address, unsigned int *uval,
			unsigned int old, unsigned int new, unsigned long key);
int __cmpxchg_user_key8(unsigned long address, unsigned long *uval,
			unsigned long old, unsigned long new, unsigned long key);
int __cmpxchg_user_key16(unsigned long address, __uint128_t *uval,
			 __uint128_t old, __uint128_t new, unsigned long key);

static __always_inline int _cmpxchg_user_key(unsigned long address, void *uval,
					     __uint128_t old, __uint128_t new,
					     unsigned long key, int size)
{
	switch (size) {
	case 1:  return __cmpxchg_user_key1(address, uval, old, new, key);
	case 2:  return __cmpxchg_user_key2(address, uval, old, new, key);
	case 4:  return __cmpxchg_user_key4(address, uval, old, new, key);
	case 8:  return __cmpxchg_user_key8(address, uval, old, new, key);
	case 16: return __cmpxchg_user_key16(address, uval, old, new, key);
	default: __cmpxchg_user_key_called_with_bad_pointer();
	}
	return 0;
}

/**
 * cmpxchg_user_key() - cmpxchg with user space target, honoring storage keys
 * @ptr: User space address of value to compare to @old and exchange with
 *	 @new. Must be aligned to sizeof(*@ptr).
 * @uval: Address where the old value of *@ptr is written to.
 * @old: Old value. Compared to the content pointed to by @ptr in order to
 *	 determine if the exchange occurs. The old value read from *@ptr is
 *	 written to *@uval.
 * @new: New value to place at *@ptr.
 * @key: Access key to use for checking storage key protection.
 *
 * Perform a cmpxchg on a user space target, honoring storage key protection.
 * @key alone determines how key checking is performed, neither
 * storage-protection-override nor fetch-protection-override apply.
 * The caller must compare *@uval and @old to determine if values have been
 * exchanged. In case of an exception *@uval is set to zero.
 *
 * Return:     0: cmpxchg executed
 *	       -EFAULT: an exception happened when trying to access *@ptr
 *	       -EAGAIN: maxed out number of retries (byte and short only)
 */
#define cmpxchg_user_key(ptr, uval, old, new, key)			\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(uval) __uval = (uval);				\
									\
	BUILD_BUG_ON(sizeof(*(__ptr)) != sizeof(*(__uval)));		\
	might_fault();							\
	__chk_user_ptr(__ptr);						\
	_cmpxchg_user_key((unsigned long)(__ptr), (void *)(__uval),	\
			  (old), (new), (key), sizeof(*(__ptr)));	\
})

#endif /* __S390_UACCESS_H */
