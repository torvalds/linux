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
	int insn, fixup;
};

static inline unsigned long extable_insn(const struct exception_table_entry *x)
{
	return (unsigned long)&x->insn + x->insn;
}

static inline unsigned long extable_fixup(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}

#define ARCH_HAS_SORT_EXTABLE
#define ARCH_HAS_SEARCH_EXTABLE

/**
 * __copy_from_user: - Copy a block of data from user space, with less checking.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:	  Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
unsigned long __must_check __copy_from_user(void *to, const void __user *from,
					    unsigned long n);

/**
 * __copy_to_user: - Copy a block of data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:	  Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
unsigned long __must_check __copy_to_user(void __user *to, const void *from,
					  unsigned long n);

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

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

#define __put_user_fn(x, ptr, size) __put_get_user_asm(ptr, x, size, 0x810000UL)
#define __get_user_fn(x, ptr, size) __put_get_user_asm(x, ptr, size, 0x81UL)

#else /* CONFIG_HAVE_MARCH_Z10_FEATURES */

static inline int __put_user_fn(void *x, void __user *ptr, unsigned long size)
{
	size = __copy_to_user(ptr, x, size);
	return size ? -EFAULT : 0;
}

static inline int __get_user_fn(void *x, const void __user *ptr, unsigned long size)
{
	size = __copy_from_user(x, ptr, size);
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
	__pu_err;						\
})

#define put_user(x, ptr)					\
({								\
	might_fault();						\
	__put_user(x, ptr);					\
})


int __put_user_bad(void) __attribute__((noreturn));

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
	__gu_err;						\
})

#define get_user(x, ptr)					\
({								\
	might_fault();						\
	__get_user(x, ptr);					\
})

int __get_user_bad(void) __attribute__((noreturn));

#define __put_user_unaligned __put_user
#define __get_user_unaligned __get_user

/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
	return __copy_to_user(to, from, n);
}

void copy_from_user_overflow(void)
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
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
	return __copy_from_user(to, from, n);
}

unsigned long __must_check
__copy_in_user(void __user *to, const void __user *from, unsigned long n);

static inline unsigned long __must_check
copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	might_fault();
	return __copy_in_user(to, from, n);
}

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

/**
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
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
unsigned long __must_check __clear_user(void __user *to, unsigned long size);

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	might_fault();
	return __clear_user(to, n);
}

int copy_to_user_real(void __user *dest, void *src, unsigned long count);
void s390_kernel_write(void *dst, const void *src, size_t size);

#endif /* __S390_UACCESS_H */
