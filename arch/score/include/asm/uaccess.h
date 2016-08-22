#ifndef __SCORE_UACCESS_H
#define __SCORE_UACCESS_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/thread_info.h>

#define VERIFY_READ		0
#define VERIFY_WRITE		1

#define get_ds()		(KERNEL_DS)
#define get_fs()		(current_thread_info()->addr_limit)
#define segment_eq(a, b)	((a).seg == (b).seg)

/*
 * Is a address valid? This does a straighforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - AND "addr+size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 *
 * __ua_size() is a trick to avoid runtime checking of positive constant
 * sizes; for those we already know at compile time that the size is ok.
 */
#define __ua_size(size)							\
	((__builtin_constant_p(size) && (signed long) (size) > 0) ? 0 : (size))

/*
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

#define __access_ok(addr, size)					\
	(((long)((get_fs().seg) &				\
		 ((addr) | ((addr) + (size)) |			\
		  __ua_size(size)))) == 0)

#define access_ok(type, addr, size)				\
	likely(__access_ok((unsigned long)(addr), (size)))

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
#define put_user(x, ptr) __put_user_check((x), (ptr), sizeof(*(ptr)))

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
#define get_user(x, ptr) __get_user_check((x), (ptr), sizeof(*(ptr)))

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
#define __put_user(x, ptr) __put_user_nocheck((x), (ptr), sizeof(*(ptr)))

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
#define __get_user(x, ptr) __get_user_nocheck((x), (ptr), sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
extern void __get_user_unknown(void);

#define __get_user_common(val, size, ptr)				\
do {									\
	switch (size) {							\
	case 1:								\
		__get_user_asm(val, "lb", ptr);				\
		break;							\
	case 2:								\
		__get_user_asm(val, "lh", ptr);				\
		 break;							\
	case 4:								\
		__get_user_asm(val, "lw", ptr);				\
		 break;							\
	case 8: 							\
		if (__copy_from_user((void *)&val, ptr, 8) == 0)	\
			__gu_err = 0;					\
		else							\
			__gu_err = -EFAULT;				\
		break;							\
	default:							\
		__get_user_unknown();					\
		break;							\
	}								\
} while (0)

#define __get_user_nocheck(x, ptr, size)				\
({									\
	long __gu_err = 0;						\
	__get_user_common((x), size, ptr);				\
	__gu_err;							\
})

#define __get_user_check(x, ptr, size)					\
({									\
	long __gu_err = -EFAULT;					\
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);		\
									\
	if (likely(access_ok(VERIFY_READ, __gu_ptr, size)))		\
		__get_user_common((x), size, __gu_ptr);			\
	else								\
		(x) = 0;						\
									\
	__gu_err;							\
})

#define __get_user_asm(val, insn, addr)					\
{									\
	long __gu_tmp;							\
									\
	__asm__ __volatile__(						\
		"1:" insn " %1, %3\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:li	%0, %4\n"					\
		"li	%1, 0\n"					\
		"j	2b\n"						\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		".word	1b, 3b\n"					\
		".previous\n"						\
		: "=r" (__gu_err), "=r" (__gu_tmp)			\
		: "0" (0), "o" (__m(addr)), "i" (-EFAULT));		\
									\
		(val) = (__typeof__(*(addr))) __gu_tmp;			\
}

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#define __put_user_nocheck(val, ptr, size)				\
({									\
	__typeof__(*(ptr)) __pu_val;					\
	long __pu_err = 0;						\
									\
	__pu_val = (val);						\
	switch (size) {							\
	case 1:								\
		__put_user_asm("sb", ptr);				\
		break;							\
	case 2:								\
		__put_user_asm("sh", ptr);				\
		break;							\
	case 4:								\
		__put_user_asm("sw", ptr);				\
		break;							\
	case 8: 							\
		if ((__copy_to_user((void *)ptr, &__pu_val, 8)) == 0)	\
			__pu_err = 0;					\
		else							\
			__pu_err = -EFAULT;				\
		break;							\
	default:							\
		 __put_user_unknown();					\
		 break;							\
	}								\
	__pu_err;							\
})


#define __put_user_check(val, ptr, size)				\
({									\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (val);				\
	long __pu_err = -EFAULT;					\
									\
	if (likely(access_ok(VERIFY_WRITE, __pu_addr, size))) {		\
		switch (size) {						\
		case 1:							\
			__put_user_asm("sb", __pu_addr);		\
			break;						\
		case 2:							\
			__put_user_asm("sh", __pu_addr);		\
			break;						\
		case 4:							\
			__put_user_asm("sw", __pu_addr);		\
			break;						\
		case 8: 						\
			if ((__copy_to_user((void *)__pu_addr, &__pu_val, 8)) == 0)\
				__pu_err = 0;				\
			else						\
				__pu_err = -EFAULT;			\
			break;						\
		default:						\
			__put_user_unknown();				\
			break;						\
		}							\
	}								\
	__pu_err;							\
})

#define __put_user_asm(insn, ptr)					\
	__asm__ __volatile__(						\
		"1:" insn " %2, %3\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:li %0, %4\n"						\
		"j 2b\n"						\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		".word 1b, 3b\n"					\
		".previous\n"						\
		: "=r" (__pu_err)					\
		: "0" (0), "r" (__pu_val), "o" (__m(ptr)),		\
		  "i" (-EFAULT));

extern void __put_user_unknown(void);
extern int __copy_tofrom_user(void *to, const void *from, unsigned long len);

static inline unsigned long
copy_from_user(void *to, const void *from, unsigned long len)
{
	unsigned long res = len;

	if (likely(access_ok(VERIFY_READ, from, len)))
		res = __copy_tofrom_user(to, from, len);

	if (unlikely(res))
		memset(to + (len - res), 0, res);

	return res;
}

static inline unsigned long
copy_to_user(void *to, const void *from, unsigned long len)
{
	if (likely(access_ok(VERIFY_WRITE, to, len)))
		len = __copy_tofrom_user(to, from, len);

	return len;
}

static inline unsigned long
__copy_from_user(void *to, const void *from, unsigned long len)
{
	unsigned long left = __copy_tofrom_user(to, from, len);
	if (unlikely(left))
		memset(to + (len - left), 0, left);
	return left;
}

#define __copy_to_user(to, from, len)		\
		__copy_tofrom_user((to), (from), (len))

static inline unsigned long
__copy_to_user_inatomic(void *to, const void *from, unsigned long len)
{
	return __copy_to_user(to, from, len);
}

static inline unsigned long
__copy_from_user_inatomic(void *to, const void *from, unsigned long len)
{
	return __copy_tofrom_user(to, from, len);
}

#define __copy_in_user(to, from, len)	__copy_tofrom_user(to, from, len)

static inline unsigned long
copy_in_user(void *to, const void *from, unsigned long len)
{
	if (access_ok(VERIFY_READ, from, len) &&
		      access_ok(VERFITY_WRITE, to, len))
		return __copy_tofrom_user(to, from, len);
}

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
extern unsigned long __clear_user(void __user *src, unsigned long size);

static inline unsigned long clear_user(char *src, unsigned long size)
{
	if (access_ok(VERIFY_WRITE, src, size))
		return __clear_user(src, size);

	return -EFAULT;
}
/*
 * __strncpy_from_user: - Copy a NUL terminated string from userspace, with less checking.
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
extern int __strncpy_from_user(char *dst, const char *src, long len);

static inline int strncpy_from_user(char *dst, const char *src, long len)
{
	if (access_ok(VERIFY_READ, src, 1))
		return __strncpy_from_user(dst, src, len);

	return -EFAULT;
}

extern int __strlen_user(const char *src);
static inline long strlen_user(const char __user *src)
{
	return __strlen_user(src);
}

extern int __strnlen_user(const char *str, long len);
static inline long strnlen_user(const char __user *str, long len)
{
	if (!access_ok(VERIFY_READ, str, 0))
		return 0;
	else		
		return __strnlen_user(str, len);
}

struct exception_table_entry {
	unsigned long insn;
	unsigned long fixup;
};

extern int fixup_exception(struct pt_regs *regs);

#endif /* __SCORE_UACCESS_H */

