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
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm-generic/uaccess-unaligned.h>
#include <asm/processor.h>
#include <asm/page.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

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
#define access_ok(type, addr, size) ({ \
	__chk_user_ptr(addr); \
	likely(__range_ok((unsigned long)(addr), (size)) == 0);	\
})

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

struct exception_table_entry {
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

/*
 * We return the __get_user_N function results in a structure,
 * thus in r0 and r1.  If "err" is zero, "val" is the result
 * of the read; otherwise, "err" is -EFAULT.
 *
 * We rarely need 8-byte values on a 32-bit architecture, but
 * we size the structure to accommodate.  In practice, for the
 * the smaller reads, we can zero the high word for free, and
 * the caller will ignore it by virtue of casting anyway.
 */
struct __get_user {
	unsigned long long val;
	int err;
};

/*
 * FIXME: we should express these as inline extended assembler, since
 * they're fundamentally just a variable dereference and some
 * supporting exception_table gunk.  Note that (a la i386) we can
 * extend the copy_to_user and copy_from_user routines to call into
 * such extended assembler routines, though we will have to use a
 * different return code in that case (1, 2, or 4, rather than -EFAULT).
 */
extern struct __get_user __get_user_1(const void __user *);
extern struct __get_user __get_user_2(const void __user *);
extern struct __get_user __get_user_4(const void __user *);
extern struct __get_user __get_user_8(const void __user *);
extern int __put_user_1(long, void __user *);
extern int __put_user_2(long, void __user *);
extern int __put_user_4(long, void __user *);
extern int __put_user_8(long long, void __user *);

/* Unimplemented routines to cause linker failures */
extern struct __get_user __get_user_bad(void);
extern int __put_user_bad(void);

/*
 * Careful: we have to cast the result to the type of the pointer
 * for sign reasons.
 */
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
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 */
#define __get_user(x, ptr)						\
({	struct __get_user __ret;					\
	__typeof__(*(ptr)) const __user *__gu_addr = (ptr);		\
	__chk_user_ptr(__gu_addr);					\
	switch (sizeof(*(__gu_addr))) {					\
	case 1:								\
		__ret = __get_user_1(__gu_addr);			\
		break;							\
	case 2:								\
		__ret = __get_user_2(__gu_addr);			\
		break;							\
	case 4:								\
		__ret = __get_user_4(__gu_addr);			\
		break;							\
	case 8:								\
		__ret = __get_user_8(__gu_addr);			\
		break;							\
	default:							\
		__ret = __get_user_bad();				\
		break;							\
	}								\
	(x) = (__typeof__(*__gu_addr)) (__typeof__(*__gu_addr - *__gu_addr)) \
	  __ret.val;			                                \
	__ret.err;							\
})

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
 *
 * Implementation note: The "case 8" logic of casting to the type of
 * the result of subtracting the value from itself is basically a way
 * of keeping all integer types the same, but casting any pointers to
 * ptrdiff_t, i.e. also an integer type.  This way there are no
 * questionable casts seen by the compiler on an ILP32 platform.
 */
#define __put_user(x, ptr)						\
({									\
	int __pu_err = 0;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	typeof(*__pu_addr) __pu_val = (x);				\
	__chk_user_ptr(__pu_addr);					\
	switch (sizeof(__pu_val)) {					\
	case 1:								\
		__pu_err = __put_user_1((long)__pu_val, __pu_addr);	\
		break;							\
	case 2:								\
		__pu_err = __put_user_2((long)__pu_val, __pu_addr);	\
		break;							\
	case 4:								\
		__pu_err = __put_user_4((long)__pu_val, __pu_addr);	\
		break;							\
	case 8:								\
		__pu_err =						\
		  __put_user_8((__typeof__(__pu_val - __pu_val))__pu_val,\
			__pu_addr);					\
		break;							\
	default:							\
		__pu_err = __put_user_bad();				\
		break;							\
	}								\
	__pu_err;							\
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

/**
 * __copy_to_user() - copy data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * An alternate version - __copy_to_user_inatomic() - is designed
 * to be called from atomic context, typically bracketed by calls
 * to pagefault_disable() and pagefault_enable().
 */
extern unsigned long __must_check __copy_to_user_inatomic(
	void __user *to, const void *from, unsigned long n);

static inline unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	return __copy_to_user_inatomic(to, from, n);
}

static inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		n = __copy_to_user(to, from, n);
	return n;
}

/**
 * __copy_from_user() - copy data from user space, with less checking.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from user space to kernel space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 *
 * An alternate version - __copy_from_user_inatomic() - is designed
 * to be called from atomic context, typically bracketed by calls
 * to pagefault_disable() and pagefault_enable().  This version
 * does *NOT* pad with zeros.
 */
extern unsigned long __must_check __copy_from_user_inatomic(
	void *to, const void __user *from, unsigned long n);
extern unsigned long __must_check __copy_from_user_zeroing(
	void *to, const void __user *from, unsigned long n);

static inline unsigned long __must_check
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
       might_fault();
       return __copy_from_user_zeroing(to, from, n);
}

static inline unsigned long __must_check
_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		n = __copy_from_user(to, from, n);
	else
		memset(to, 0, n);
	return n;
}

#ifdef CONFIG_DEBUG_COPY_FROM_USER
extern void copy_from_user_overflow(void)
	__compiletime_warning("copy_from_user() size is not provably correct");

static inline unsigned long __must_check copy_from_user(void *to,
					  const void __user *from,
					  unsigned long n)
{
	int sz = __compiletime_object_size(to);

	if (likely(sz == -1 || sz >= n))
		n = _copy_from_user(to, from, n);
	else
		copy_from_user_overflow();

	return n;
}
#else
#define copy_from_user _copy_from_user
#endif

#ifdef __tilegx__
/**
 * __copy_in_user() - copy data within user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from user space to user space.  Caller must check
 * the specified blocks with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
extern unsigned long __copy_in_user_inatomic(
	void __user *to, const void __user *from, unsigned long n);

static inline unsigned long __must_check
__copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	might_sleep();
	return __copy_in_user_inatomic(to, from, n);
}

static inline unsigned long __must_check
copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n) && access_ok(VERIFY_READ, from, n))
		n = __copy_in_user(to, from, n);
	return n;
}
#endif


/**
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 *
 * Context: User context only.  This function may sleep.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 *
 * If there is a limit on the length of a valid string, you may wish to
 * consider using strnlen_user() instead.
 */
extern long strnlen_user_asm(const char __user *str, long n);
static inline long __must_check strnlen_user(const char __user *str, long n)
{
	might_fault();
	return strnlen_user_asm(str, n);
}
#define strlen_user(str) strnlen_user(str, LONG_MAX)

/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace, with less checking.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 * Caller must check the specified block with access_ok() before calling
 * this function.
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
extern long strncpy_from_user_asm(char *dst, const char __user *src, long);
static inline long __must_check __strncpy_from_user(
	char *dst, const char __user *src, long count)
{
	might_fault();
	return strncpy_from_user_asm(dst, src, count);
}
static inline long __must_check strncpy_from_user(
	char *dst, const char __user *src, long count)
{
	if (access_ok(VERIFY_READ, src, 1))
		return __strncpy_from_user(dst, src, count);
	return -EFAULT;
}

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
 * inv_user: - Invalidate a block of memory in user space from cache.
 * @mem:   Destination address, in user space.
 * @len:   Number of bytes to invalidate.
 *
 * Returns number of bytes that could not be invalidated.
 * On success, this will be zero.
 *
 * Note that on Tile64, the "inv" operation is in fact a
 * "flush and invalidate", so cache write-backs will occur prior
 * to the cache being marked invalid.
 */
extern unsigned long inv_user_asm(void __user *mem, unsigned long len);
static inline unsigned long __must_check __inv_user(
	void __user *mem, unsigned long len)
{
	int retval;

	might_fault();
	retval = inv_user_asm(mem, len);
	mb_incoherent();
	return retval;
}
static inline unsigned long __must_check inv_user(
	void __user *mem, unsigned long len)
{
	if (access_ok(VERIFY_WRITE, mem, len))
		return __inv_user(mem, len);
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
