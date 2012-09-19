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
#include <linux/sched.h>
#include <linux/errno.h>
#include <asm/ctl_reg.h>

#define VERIFY_READ     0
#define VERIFY_WRITE    1


/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(a)  ((mm_segment_t) { (a) })


#define KERNEL_DS       MAKE_MM_SEG(0)
#define USER_DS         MAKE_MM_SEG(1)

#define get_ds()        (KERNEL_DS)
#define get_fs()        (current->thread.mm_segment)

#define set_fs(x) \
({									\
	unsigned long __pto;						\
	current->thread.mm_segment = (x);				\
	__pto = current->thread.mm_segment.ar4 ?			\
		S390_lowcore.user_asce : S390_lowcore.kernel_asce;	\
	__ctl_load(__pto, 7, 7);					\
})

#define segment_eq(a,b) ((a).ar4 == (b).ar4)

static inline int __range_ok(unsigned long addr, unsigned long size)
{
	return 1;
}

#define __access_ok(addr, size)				\
({							\
	__chk_user_ptr(addr);				\
	__range_ok((unsigned long)(addr), (size));	\
})

#define access_ok(type, addr, size) __access_ok(addr, size)

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

struct exception_table_entry
{
        unsigned long insn, fixup;
};

struct uaccess_ops {
	size_t (*copy_from_user)(size_t, const void __user *, void *);
	size_t (*copy_from_user_small)(size_t, const void __user *, void *);
	size_t (*copy_to_user)(size_t, void __user *, const void *);
	size_t (*copy_to_user_small)(size_t, void __user *, const void *);
	size_t (*copy_in_user)(size_t, void __user *, const void __user *);
	size_t (*clear_user)(size_t, void __user *);
	size_t (*strnlen_user)(size_t, const char __user *);
	size_t (*strncpy_from_user)(size_t, const char __user *, char *);
	int (*futex_atomic_op)(int op, u32 __user *, int oparg, int *old);
	int (*futex_atomic_cmpxchg)(u32 *, u32 __user *, u32 old, u32 new);
};

extern struct uaccess_ops uaccess;
extern struct uaccess_ops uaccess_std;
extern struct uaccess_ops uaccess_mvcos;
extern struct uaccess_ops uaccess_mvcos_switch;
extern struct uaccess_ops uaccess_pt;

extern int __handle_fault(unsigned long, unsigned long, int);

static inline int __put_user_fn(size_t size, void __user *ptr, void *x)
{
	size = uaccess.copy_to_user_small(size, ptr, x);
	return size ? -EFAULT : size;
}

static inline int __get_user_fn(size_t size, const void __user *ptr, void *x)
{
	size = uaccess.copy_from_user_small(size, ptr, x);
	return size ? -EFAULT : size;
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
		__pu_err = __put_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		break;						\
	default:						\
		__put_user_bad();				\
		break;						\
	 }							\
	__pu_err;						\
})

#define put_user(x, ptr)					\
({								\
	might_fault();						\
	__put_user(x, ptr);					\
})


extern int __put_user_bad(void) __attribute__((noreturn));

#define __get_user(x, ptr)					\
({								\
	int __gu_err = -EFAULT;					\
	__chk_user_ptr(ptr);					\
	switch (sizeof(*(ptr))) {				\
	case 1: {						\
		unsigned char __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 2: {						\
		unsigned short __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 4: {						\
		unsigned int __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 8: {						\
		unsigned long long __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	default:						\
		__get_user_bad();				\
		break;						\
	}							\
	__gu_err;						\
})

#define get_user(x, ptr)					\
({								\
	might_fault();						\
	__get_user(x, ptr);					\
})

extern int __get_user_bad(void) __attribute__((noreturn));

#define __put_user_unaligned __put_user
#define __get_user_unaligned __get_user

/**
 * __copy_to_user: - Copy a block of data into user space, with less checking.
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
 */
static inline unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (__builtin_constant_p(n) && (n <= 256))
		return uaccess.copy_to_user_small(n, to, from);
	else
		return uaccess.copy_to_user(n, to, from);
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from kernel space to user space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
static inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (access_ok(VERIFY_WRITE, to, n))
		n = __copy_to_user(to, from, n);
	return n;
}

/**
 * __copy_from_user: - Copy a block of data from user space, with less checking.
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
 */
static inline unsigned long __must_check
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (__builtin_constant_p(n) && (n <= 256))
		return uaccess.copy_from_user_small(n, from, to);
	else
		return uaccess.copy_from_user(n, from, to);
}

extern void copy_from_user_overflow(void)
#ifdef CONFIG_DEBUG_STRICT_USER_COPY_CHECKS
__compiletime_warning("copy_from_user() buffer size is not provably correct")
#endif
;

/**
 * copy_from_user: - Copy a block of data from user space.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from user space to kernel space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 */
static inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned int sz = __compiletime_object_size(to);

	might_fault();
	if (unlikely(sz != -1 && sz < n)) {
		copy_from_user_overflow();
		return n;
	}
	if (access_ok(VERIFY_READ, from, n))
		n = __copy_from_user(to, from, n);
	else
		memset(to, 0, n);
	return n;
}

static inline unsigned long __must_check
__copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	return uaccess.copy_in_user(n, to, from);
}

static inline unsigned long __must_check
copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	might_fault();
	if (__access_ok(from,n) && __access_ok(to,n))
		n = __copy_in_user(to, from, n);
	return n;
}

/*
 * Copy a null terminated string from userspace.
 */
static inline long __must_check
strncpy_from_user(char *dst, const char __user *src, long count)
{
        long res = -EFAULT;
	might_fault();
        if (access_ok(VERIFY_READ, src, 1))
		res = uaccess.strncpy_from_user(count, src, dst);
        return res;
}

static inline unsigned long
strnlen_user(const char __user * src, unsigned long n)
{
	might_fault();
	return uaccess.strnlen_user(n, src);
}

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
#define strlen_user(str) strnlen_user(str, ~0UL)

/*
 * Zero Userspace
 */

static inline unsigned long __must_check
__clear_user(void __user *to, unsigned long n)
{
	return uaccess.clear_user(n, to);
}

static inline unsigned long __must_check
clear_user(void __user *to, unsigned long n)
{
	might_fault();
	if (access_ok(VERIFY_WRITE, to, n))
		n = uaccess.clear_user(n, to);
	return n;
}

extern int copy_to_user_real(void __user *dest, void *src, size_t count);
extern int copy_from_user_real(void *dest, void __user *src, size_t count);

#endif /* __S390_UACCESS_H */
