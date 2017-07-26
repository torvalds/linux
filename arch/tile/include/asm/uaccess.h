/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_UACCESS_H
#define _ASM_TILE_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */
#define MAKE_MM_SEG(a)  ((mm_segment_t) { (a) })

#define KERNEL_DS	MAKE_MM_SEG(-1UL)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b) ((a).seg == (b).seg)

#ifndef __tilegx__
/*
 * We could allow mapping all 16 MB at 0xfc000000, but we set up a
 * special hack in arch_setup_additional_pages() to auto-create a mapping
 * for the first 16 KB, and it would seem strange to have different
 * user-accessible semantics for memory at 0xfc000000 and above 0xfc004000.
 */
static inline int is_arch_mappable_range(unsigned long addr,
					 unsigned long size)
{
	return (addr >= MEM_USER_INTRPT &&
		addr < (MEM_USER_INTRPT + INTRPT_SIZE) &&
		size <= (MEM_USER_INTRPT + INTRPT_SIZE) - addr);
}
#define is_arch_mappable_range is_arch_mappable_range
#else
#define is_arch_mappable_range(addr, size) 0
#endif

/*
 * Note that using this definition ignores is_arch_mappable_range(),
 * so on tilepro code that uses user_addr_max() is constrained not
 * to reference the tilepro user-interrupt region.
 */
#define user_addr_max() (current_thread_info()->addr_limit.seg)

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 */
int __range_ok(unsigned long addr, unsigned long size);

/**
 * access_ok: - Checks if a user space pointer is valid
 * @type: Type of access: %VERIFY_READ or %VERIFY_WRITE.  Note that
 *        %VERIFY_WRITE is a superset of %VERIFY_READ - if it is safe
 *        to write to a block, it is always safe to read from it.
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
#define access_ok(type, addr, size) ({ \
	__chk_user_ptr(addr); \
	likely(__range_ok((unsigned long)(addr), (size)) == 0);	\
})

#include <asm/extable.h>

/*
 * This is a type: either unsigned long, if the argument fits into
 * that type, or otherwise unsigned long long.
 */
#define __inttype(x) \
	__typeof__(__builtin_choose_expr(sizeof(x) > sizeof(0UL), 0ULL, 0UL))

/*
 * Support macros for __get_user().
 * Note that __get_user() and __put_user() assume proper alignment.
 */

#ifdef __LP64__
#define _ASM_PTR	".quad"
#define _ASM_ALIGN	".align 8"
#else
#define _ASM_PTR	".long"
#define _ASM_ALIGN	".align 4"
#endif

#define __get_user_asm(OP, x, ptr, ret)					\
	asm volatile("1: {" #OP " %1, %2; movei %0, 0 }\n"		\
		     ".pushsection .fixup,\"ax\"\n"			\
		     "0: { movei %1, 0; movei %0, %3 }\n"		\
		     "j 9f\n"						\
		     ".section __ex_table,\"a\"\n"			\
		     _ASM_ALIGN "\n"					\
		     _ASM_PTR " 1b, 0b\n"				\
		     ".popsection\n"					\
		     "9:"						\
		     : "=r" (ret), "=r" (x)				\
		     : "r" (ptr), "i" (-EFAULT))

#ifdef __tilegx__
#define __get_user_1(x, ptr, ret) __get_user_asm(ld1u, x, ptr, ret)
#define __get_user_2(x, ptr, ret) __get_user_asm(ld2u, x, ptr, ret)
#define __get_user_4(x, ptr, ret) __get_user_asm(ld4s, x, ptr, ret)
#define __get_user_8(x, ptr, ret) __get_user_asm(ld, x, ptr, ret)
#else
#define __get_user_1(x, ptr, ret) __get_user_asm(lb_u, x, ptr, ret)
#define __get_user_2(x, ptr, ret) __get_user_asm(lh_u, x, ptr, ret)
#define __get_user_4(x, ptr, ret) __get_user_asm(lw, x, ptr, ret)
#ifdef __LITTLE_ENDIAN
#define __lo32(a, b) a
#define __hi32(a, b) b
#else
#define __lo32(a, b) b
#define __hi32(a, b) a
#endif
#define __get_user_8(x, ptr, ret)					\
	({								\
		unsigned int __a, __b;					\
		asm volatile("1: { lw %1, %3; addi %2, %3, 4 }\n"	\
			     "2: { lw %2, %2; movei %0, 0 }\n"		\
			     ".pushsection .fixup,\"ax\"\n"		\
			     "0: { movei %1, 0; movei %2, 0 }\n"	\
			     "{ movei %0, %4; j 9f }\n"			\
			     ".section __ex_table,\"a\"\n"		\
			     ".align 4\n"				\
			     ".word 1b, 0b\n"				\
			     ".word 2b, 0b\n"				\
			     ".popsection\n"				\
			     "9:"					\
			     : "=r" (ret), "=r" (__a), "=&r" (__b)	\
			     : "r" (ptr), "i" (-EFAULT));		\
		(x) = (__force __typeof(x))(__inttype(x))		\
			(((u64)__hi32(__a, __b) << 32) |		\
			 __lo32(__a, __b));				\
	})
#endif

extern int __get_user_bad(void)
  __attribute__((warning("sizeof __get_user argument not 1, 2, 4 or 8")));

/**
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
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 */
#define __get_user(x, ptr)						\
	({								\
		int __ret;						\
		typeof(x) _x;						\
		__chk_user_ptr(ptr);					\
		switch (sizeof(*(ptr))) {				\
		case 1: __get_user_1(_x, ptr, __ret); break;		\
		case 2: __get_user_2(_x, ptr, __ret); break;		\
		case 4: __get_user_4(_x, ptr, __ret); break;		\
		case 8: __get_user_8(_x, ptr, __ret); break;		\
		default: __ret = __get_user_bad(); break;		\
		}							\
		(x) = (typeof(*(ptr))) _x;				\
		__ret;							\
	})

/* Support macros for __put_user(). */

#define __put_user_asm(OP, x, ptr, ret)			\
	asm volatile("1: {" #OP " %1, %2; movei %0, 0 }\n"		\
		     ".pushsection .fixup,\"ax\"\n"			\
		     "0: { movei %0, %3; j 9f }\n"			\
		     ".section __ex_table,\"a\"\n"			\
		     _ASM_ALIGN "\n"					\
		     _ASM_PTR " 1b, 0b\n"				\
		     ".popsection\n"					\
		     "9:"						\
		     : "=r" (ret)					\
		     : "r" (ptr), "r" (x), "i" (-EFAULT))

#ifdef __tilegx__
#define __put_user_1(x, ptr, ret) __put_user_asm(st1, x, ptr, ret)
#define __put_user_2(x, ptr, ret) __put_user_asm(st2, x, ptr, ret)
#define __put_user_4(x, ptr, ret) __put_user_asm(st4, x, ptr, ret)
#define __put_user_8(x, ptr, ret) __put_user_asm(st, x, ptr, ret)
#else
#define __put_user_1(x, ptr, ret) __put_user_asm(sb, x, ptr, ret)
#define __put_user_2(x, ptr, ret) __put_user_asm(sh, x, ptr, ret)
#define __put_user_4(x, ptr, ret) __put_user_asm(sw, x, ptr, ret)
#define __put_user_8(x, ptr, ret)					\
	({								\
		u64 __x = (__force __inttype(x))(x);			\
		int __lo = (int) __x, __hi = (int) (__x >> 32);		\
		asm volatile("1: { sw %1, %2; addi %0, %1, 4 }\n"	\
			     "2: { sw %0, %3; movei %0, 0 }\n"		\
			     ".pushsection .fixup,\"ax\"\n"		\
			     "0: { movei %0, %4; j 9f }\n"		\
			     ".section __ex_table,\"a\"\n"		\
			     ".align 4\n"				\
			     ".word 1b, 0b\n"				\
			     ".word 2b, 0b\n"				\
			     ".popsection\n"				\
			     "9:"					\
			     : "=&r" (ret)				\
			     : "r" (ptr), "r" (__lo32(__lo, __hi)),	\
			     "r" (__hi32(__lo, __hi)), "i" (-EFAULT));	\
	})
#endif

extern int __put_user_bad(void)
  __attribute__((warning("sizeof __put_user argument not 1, 2, 4 or 8")));

/**
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
#define __put_user(x, ptr)						\
({									\
	int __ret;							\
	typeof(*(ptr)) _x = (x);					\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1: __put_user_1(_x, ptr, __ret); break;			\
	case 2: __put_user_2(_x, ptr, __ret); break;			\
	case 4: __put_user_4(_x, ptr, __ret); break;			\
	case 8: __put_user_8(_x, ptr, __ret); break;			\
	default: __ret = __put_user_bad(); break;			\
	}								\
	__ret;								\
})

/*
 * The versions of get_user and put_user without initial underscores
 * check the address of their arguments to make sure they are not
 * in kernel space.
 */
#define put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __user *__Pu_addr = (ptr);			\
	access_ok(VERIFY_WRITE, (__Pu_addr), sizeof(*(__Pu_addr))) ?	\
		__put_user((x), (__Pu_addr)) :				\
		-EFAULT;						\
})

#define get_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) const __user *__Gu_addr = (ptr);		\
	access_ok(VERIFY_READ, (__Gu_addr), sizeof(*(__Gu_addr))) ?	\
		__get_user((x), (__Gu_addr)) :				\
		((x) = 0, -EFAULT);					\
})

extern unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n);
extern unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n);
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

#ifdef __tilegx__
extern unsigned long raw_copy_in_user(
	void __user *to, const void __user *from, unsigned long n);
#endif


extern long strnlen_user(const char __user *str, long n);
extern long strncpy_from_user(char *dst, const char __user *src, long);

/**
 * clear_user: - Zero a block of memory in user space.
 * @mem:   Destination address, in user space.
 * @len:   Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
extern unsigned long clear_user_asm(void __user *mem, unsigned long len);
static inline unsigned long __must_check __clear_user(
	void __user *mem, unsigned long len)
{
	might_fault();
	return clear_user_asm(mem, len);
}
static inline unsigned long __must_check clear_user(
	void __user *mem, unsigned long len)
{
	if (access_ok(VERIFY_WRITE, mem, len))
		return __clear_user(mem, len);
	return len;
}

/**
 * flush_user: - Flush a block of memory in user space from cache.
 * @mem:   Destination address, in user space.
 * @len:   Number of bytes to flush.
 *
 * Returns number of bytes that could not be flushed.
 * On success, this will be zero.
 */
extern unsigned long flush_user_asm(void __user *mem, unsigned long len);
static inline unsigned long __must_check __flush_user(
	void __user *mem, unsigned long len)
{
	int retval;

	might_fault();
	retval = flush_user_asm(mem, len);
	mb_incoherent();
	return retval;
}

static inline unsigned long __must_check flush_user(
	void __user *mem, unsigned long len)
{
	if (access_ok(VERIFY_WRITE, mem, len))
		return __flush_user(mem, len);
	return len;
}

/**
 * finv_user: - Flush-inval a block of memory in user space from cache.
 * @mem:   Destination address, in user space.
 * @len:   Number of bytes to invalidate.
 *
 * Returns number of bytes that could not be flush-invalidated.
 * On success, this will be zero.
 */
extern unsigned long finv_user_asm(void __user *mem, unsigned long len);
static inline unsigned long __must_check __finv_user(
	void __user *mem, unsigned long len)
{
	int retval;

	might_fault();
	retval = finv_user_asm(mem, len);
	mb_incoherent();
	return retval;
}
static inline unsigned long __must_check finv_user(
	void __user *mem, unsigned long len)
{
	if (access_ok(VERIFY_WRITE, mem, len))
		return __finv_user(mem, len);
	return len;
}

#endif /* _ASM_TILE_UACCESS_H */
