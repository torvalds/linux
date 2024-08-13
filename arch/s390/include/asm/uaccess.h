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
#include <asm/asm-extable.h>
#include <asm/processor.h>
#include <asm/extable.h>
#include <asm/facility.h>
#include <asm-generic/access_ok.h>
#include <linux/instrumented.h>

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

int __noreturn __put_user_bad(void);

#ifdef CONFIG_KMSAN
#define get_put_user_noinstr_attributes \
	noinline __maybe_unused __no_sanitize_memory
#else
#define get_put_user_noinstr_attributes __always_inline
#endif

#define DEFINE_PUT_USER(type)						\
static get_put_user_noinstr_attributes int				\
__put_user_##type##_noinstr(unsigned type __user *to,			\
			    unsigned type *from,			\
			    unsigned long size)				\
{									\
	union oac __oac_spec = {					\
		.oac1.as = PSW_BITS_AS_SECONDARY,			\
		.oac1.a = 1,						\
	};								\
	int rc;								\
									\
	asm volatile(							\
		"	lr	0,%[spec]\n"				\
		"0:	mvcos	%[_to],%[_from],%[_size]\n"		\
		"1:	xr	%[rc],%[rc]\n"				\
		"2:\n"							\
		EX_TABLE_UA_STORE(0b, 2b, %[rc])			\
		EX_TABLE_UA_STORE(1b, 2b, %[rc])			\
		: [rc] "=&d" (rc), [_to] "+Q" (*(to))			\
		: [_size] "d" (size), [_from] "Q" (*(from)),		\
		  [spec] "d" (__oac_spec.val)				\
		: "cc", "0");						\
	return rc;							\
}									\
									\
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

static __always_inline int __put_user_fn(void *x, void __user *ptr, unsigned long size)
{
	int rc;

	switch (size) {
	case 1:
		rc = __put_user_char((unsigned char __user *)ptr,
				     (unsigned char *)x,
				     size);
		break;
	case 2:
		rc = __put_user_short((unsigned short __user *)ptr,
				      (unsigned short *)x,
				      size);
		break;
	case 4:
		rc = __put_user_int((unsigned int __user *)ptr,
				    (unsigned int *)x,
				    size);
		break;
	case 8:
		rc = __put_user_long((unsigned long __user *)ptr,
				     (unsigned long *)x,
				     size);
		break;
	default:
		__put_user_bad();
		break;
	}
	return rc;
}

int __noreturn __get_user_bad(void);

#define DEFINE_GET_USER(type)						\
static get_put_user_noinstr_attributes int				\
__get_user_##type##_noinstr(unsigned type *to,				\
			    unsigned type __user *from,			\
			    unsigned long size)				\
{									\
	union oac __oac_spec = {					\
		.oac2.as = PSW_BITS_AS_SECONDARY,			\
		.oac2.a = 1,						\
	};								\
	int rc;								\
									\
	asm volatile(							\
		"	lr	0,%[spec]\n"				\
		"0:	mvcos	0(%[_to]),%[_from],%[_size]\n"		\
		"1:	xr	%[rc],%[rc]\n"				\
		"2:\n"							\
		EX_TABLE_UA_LOAD_MEM(0b, 2b, %[rc], %[_to], %[_ksize])	\
		EX_TABLE_UA_LOAD_MEM(1b, 2b, %[rc], %[_to], %[_ksize])	\
		: [rc] "=&d" (rc), "=Q" (*(to))				\
		: [_size] "d" (size), [_from] "Q" (*(from)),		\
		  [spec] "d" (__oac_spec.val), [_to] "a" (to),		\
		  [_ksize] "K" (size)					\
		: "cc", "0");						\
	return rc;							\
}									\
									\
static __always_inline int						\
__get_user_##type(unsigned type *to, unsigned type __user *from,	\
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

static __always_inline int __get_user_fn(void *x, const void __user *ptr, unsigned long size)
{
	int rc;

	switch (size) {
	case 1:
		rc = __get_user_char((unsigned char *)x,
				     (unsigned char __user *)ptr,
				     size);
		break;
	case 2:
		rc = __get_user_short((unsigned short *)x,
				      (unsigned short __user *)ptr,
				      size);
		break;
	case 4:
		rc = __get_user_int((unsigned int *)x,
				    (unsigned int __user *)ptr,
				    size);
		break;
	case 8:
		rc = __get_user_long((unsigned long *)x,
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
#define __put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __x = (x);					\
	int __pu_err = -EFAULT;						\
									\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
	case 2:								\
	case 4:								\
	case 8:								\
		__pu_err = __put_user_fn(&__x, ptr, sizeof(*(ptr)));	\
		break;							\
	default:							\
		__put_user_bad();					\
		break;							\
	}								\
	__builtin_expect(__pu_err, 0);					\
})

#define put_user(x, ptr)						\
({									\
	might_fault();							\
	__put_user(x, ptr);						\
})

#define __get_user(x, ptr)						\
({									\
	int __gu_err = -EFAULT;						\
									\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1: {							\
		unsigned char __x;					\
									\
		__gu_err = __get_user_fn(&__x, ptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 2: {							\
		unsigned short __x;					\
									\
		__gu_err = __get_user_fn(&__x, ptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 4: {							\
		unsigned int __x;					\
									\
		__gu_err = __get_user_fn(&__x, ptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	case 8: {							\
		unsigned long __x;					\
									\
		__gu_err = __get_user_fn(&__x, ptr, sizeof(*(ptr)));	\
		(x) = *(__force __typeof__(*(ptr)) *)&__x;		\
		break;							\
	};								\
	default:							\
		__get_user_bad();					\
		break;							\
	}								\
	__builtin_expect(__gu_err, 0);					\
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

/*
 * Zero Userspace
 */
unsigned long __must_check __clear_user(void __user *to, unsigned long size);

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
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

int __noreturn __put_kernel_bad(void);

#define __put_kernel_asm(val, to, insn)					\
({									\
	int __rc;							\
									\
	asm volatile(							\
		"0:   " insn "  %[_val],%[_to]\n"			\
		"1:	xr	%[rc],%[rc]\n"				\
		"2:\n"							\
		EX_TABLE_UA_STORE(0b, 2b, %[rc])			\
		EX_TABLE_UA_STORE(1b, 2b, %[rc])			\
		: [rc] "=d" (__rc), [_to] "+Q" (*(to))			\
		: [_val] "d" (val)					\
		: "cc");						\
	__rc;								\
})

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	unsigned long __x = (unsigned long)(*((type *)(src)));		\
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
		"0:   " insn "  %[_val],%[_from]\n"			\
		"1:	xr	%[rc],%[rc]\n"				\
		"2:\n"							\
		EX_TABLE_UA_LOAD_REG(0b, 2b, %[rc], %[_val])		\
		EX_TABLE_UA_LOAD_REG(1b, 2b, %[rc], %[_val])		\
		: [rc] "=d" (__rc), [_val] "=d" (val)			\
		: [_from] "Q" (*(from))					\
		: "cc");						\
	__rc;								\
})

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	int __gk_err;							\
									\
	switch (sizeof(type)) {						\
	case 1: {							\
		unsigned char __x;					\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "ic");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 2: {							\
		unsigned short __x;					\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "lh");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 4: {							\
		unsigned int __x;					\
									\
		__gk_err = __get_kernel_asm(__x, (type *)(src), "l");	\
		*((type *)(dst)) = (type)__x;				\
		break;							\
	};								\
	case 8: {							\
		unsigned long __x;					\
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

void __cmpxchg_user_key_called_with_bad_pointer(void);

#define CMPXCHG_USER_KEY_MAX_LOOPS 128

static __always_inline int __cmpxchg_user_key(unsigned long address, void *uval,
					      __uint128_t old, __uint128_t new,
					      unsigned long key, int size)
{
	int rc = 0;

	switch (size) {
	case 1: {
		unsigned int prev, shift, mask, _old, _new;
		unsigned long count;

		shift = (3 ^ (address & 3)) << 3;
		address ^= address & 3;
		_old = ((unsigned int)old & 0xff) << shift;
		_new = ((unsigned int)new & 0xff) << shift;
		mask = ~(0xff << shift);
		asm volatile(
			"	spka	0(%[key])\n"
			"	sacf	256\n"
			"	llill	%[count],%[max_loops]\n"
			"0:	l	%[prev],%[address]\n"
			"1:	nr	%[prev],%[mask]\n"
			"	xilf	%[mask],0xffffffff\n"
			"	or	%[new],%[prev]\n"
			"	or	%[prev],%[tmp]\n"
			"2:	lr	%[tmp],%[prev]\n"
			"3:	cs	%[prev],%[new],%[address]\n"
			"4:	jnl	5f\n"
			"	xr	%[tmp],%[prev]\n"
			"	xr	%[new],%[tmp]\n"
			"	nr	%[tmp],%[mask]\n"
			"	jnz	5f\n"
			"	brct	%[count],2b\n"
			"5:	sacf	768\n"
			"	spka	%[default_key]\n"
			EX_TABLE_UA_LOAD_REG(0b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(1b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(3b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(4b, 5b, %[rc], %[prev])
			: [rc] "+&d" (rc),
			  [prev] "=&d" (prev),
			  [address] "+Q" (*(int *)address),
			  [tmp] "+&d" (_old),
			  [new] "+&d" (_new),
			  [mask] "+&d" (mask),
			  [count] "=a" (count)
			: [key] "%[count]" (key << 4),
			  [default_key] "J" (PAGE_DEFAULT_KEY),
			  [max_loops] "J" (CMPXCHG_USER_KEY_MAX_LOOPS)
			: "memory", "cc");
		*(unsigned char *)uval = prev >> shift;
		if (!count)
			rc = -EAGAIN;
		return rc;
	}
	case 2: {
		unsigned int prev, shift, mask, _old, _new;
		unsigned long count;

		shift = (2 ^ (address & 2)) << 3;
		address ^= address & 2;
		_old = ((unsigned int)old & 0xffff) << shift;
		_new = ((unsigned int)new & 0xffff) << shift;
		mask = ~(0xffff << shift);
		asm volatile(
			"	spka	0(%[key])\n"
			"	sacf	256\n"
			"	llill	%[count],%[max_loops]\n"
			"0:	l	%[prev],%[address]\n"
			"1:	nr	%[prev],%[mask]\n"
			"	xilf	%[mask],0xffffffff\n"
			"	or	%[new],%[prev]\n"
			"	or	%[prev],%[tmp]\n"
			"2:	lr	%[tmp],%[prev]\n"
			"3:	cs	%[prev],%[new],%[address]\n"
			"4:	jnl	5f\n"
			"	xr	%[tmp],%[prev]\n"
			"	xr	%[new],%[tmp]\n"
			"	nr	%[tmp],%[mask]\n"
			"	jnz	5f\n"
			"	brct	%[count],2b\n"
			"5:	sacf	768\n"
			"	spka	%[default_key]\n"
			EX_TABLE_UA_LOAD_REG(0b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(1b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(3b, 5b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(4b, 5b, %[rc], %[prev])
			: [rc] "+&d" (rc),
			  [prev] "=&d" (prev),
			  [address] "+Q" (*(int *)address),
			  [tmp] "+&d" (_old),
			  [new] "+&d" (_new),
			  [mask] "+&d" (mask),
			  [count] "=a" (count)
			: [key] "%[count]" (key << 4),
			  [default_key] "J" (PAGE_DEFAULT_KEY),
			  [max_loops] "J" (CMPXCHG_USER_KEY_MAX_LOOPS)
			: "memory", "cc");
		*(unsigned short *)uval = prev >> shift;
		if (!count)
			rc = -EAGAIN;
		return rc;
	}
	case 4:	{
		unsigned int prev = old;

		asm volatile(
			"	spka	0(%[key])\n"
			"	sacf	256\n"
			"0:	cs	%[prev],%[new],%[address]\n"
			"1:	sacf	768\n"
			"	spka	%[default_key]\n"
			EX_TABLE_UA_LOAD_REG(0b, 1b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(1b, 1b, %[rc], %[prev])
			: [rc] "+&d" (rc),
			  [prev] "+&d" (prev),
			  [address] "+Q" (*(int *)address)
			: [new] "d" ((unsigned int)new),
			  [key] "a" (key << 4),
			  [default_key] "J" (PAGE_DEFAULT_KEY)
			: "memory", "cc");
		*(unsigned int *)uval = prev;
		return rc;
	}
	case 8: {
		unsigned long prev = old;

		asm volatile(
			"	spka	0(%[key])\n"
			"	sacf	256\n"
			"0:	csg	%[prev],%[new],%[address]\n"
			"1:	sacf	768\n"
			"	spka	%[default_key]\n"
			EX_TABLE_UA_LOAD_REG(0b, 1b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REG(1b, 1b, %[rc], %[prev])
			: [rc] "+&d" (rc),
			  [prev] "+&d" (prev),
			  [address] "+QS" (*(long *)address)
			: [new] "d" ((unsigned long)new),
			  [key] "a" (key << 4),
			  [default_key] "J" (PAGE_DEFAULT_KEY)
			: "memory", "cc");
		*(unsigned long *)uval = prev;
		return rc;
	}
	case 16: {
		__uint128_t prev = old;

		asm volatile(
			"	spka	0(%[key])\n"
			"	sacf	256\n"
			"0:	cdsg	%[prev],%[new],%[address]\n"
			"1:	sacf	768\n"
			"	spka	%[default_key]\n"
			EX_TABLE_UA_LOAD_REGPAIR(0b, 1b, %[rc], %[prev])
			EX_TABLE_UA_LOAD_REGPAIR(1b, 1b, %[rc], %[prev])
			: [rc] "+&d" (rc),
			  [prev] "+&d" (prev),
			  [address] "+QS" (*(__int128_t *)address)
			: [new] "d" (new),
			  [key] "a" (key << 4),
			  [default_key] "J" (PAGE_DEFAULT_KEY)
			: "memory", "cc");
		*(__uint128_t *)uval = prev;
		return rc;
	}
	}
	__cmpxchg_user_key_called_with_bad_pointer();
	return rc;
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
	__cmpxchg_user_key((unsigned long)(__ptr), (void *)(__uval),	\
			   (old), (new), (key), sizeof(*(__ptr)));	\
})

#endif /* __S390_UACCESS_H */
