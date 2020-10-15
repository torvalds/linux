/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_UACCESS_H
#define __ASM_OPENRISC_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/prefetch.h>
#include <linux/string.h>
#include <asm/page.h>
#include <asm/extable.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

/* addr_limit is the maximum accessible address for the task. we misuse
 * the KERNEL_DS and USER_DS values to both assign and compare the
 * addr_limit values through the equally misnamed get/set_fs macros.
 * (see above)
 */

#define KERNEL_DS	(~0UL)

#define USER_DS		(TASK_SIZE)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define uaccess_kernel()	(get_fs() == KERNEL_DS)

/* Ensure that the range from addr to addr+size is all within the process'
 * address space
 */
static inline int __range_ok(unsigned long addr, unsigned long size)
{
	const mm_segment_t fs = get_fs();

	return size <= fs && addr <= (fs - size);
}

#define access_ok(addr, size)						\
({ 									\
	__chk_user_ptr(addr);						\
	__range_ok((unsigned long)(addr), (size));			\
})

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 *
 * As we use the same address space for kernel and user data on the
 * PowerPC, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 */
#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))
#define put_user(x, ptr) \
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

extern long __put_user_bad(void);

#define __put_user_nocheck(x, ptr, size)		\
({							\
	long __pu_err;					\
	__put_user_size((x), (ptr), (size), __pu_err);	\
	__pu_err;					\
})

#define __put_user_check(x, ptr, size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	if (access_ok(__pu_addr, size))			\
		__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;							\
})

#define __put_user_size(x, ptr, size, retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	case 1: __put_user_asm(x, ptr, retval, "l.sb"); break;		\
	case 2: __put_user_asm(x, ptr, retval, "l.sh"); break;		\
	case 4: __put_user_asm(x, ptr, retval, "l.sw"); break;		\
	case 8: __put_user_asm2(x, ptr, retval); break;			\
	default: __put_user_bad();					\
	}								\
} while (0)

struct __large_struct {
	unsigned long buf[100];
};
#define __m(x) (*(struct __large_struct *)(x))

/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 */
#define __put_user_asm(x, addr, err, op)			\
	__asm__ __volatile__(					\
		"1:	"op" 0(%2),%1\n"			\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	l.addi %0,r0,%3\n"			\
		"	l.j 2b\n"				\
		"	l.nop\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 2\n"				\
		"	.long 1b,3b\n"				\
		".previous"					\
		: "=r"(err)					\
		: "r"(x), "r"(addr), "i"(-EFAULT), "0"(err))

#define __put_user_asm2(x, addr, err)				\
	__asm__ __volatile__(					\
		"1:	l.sw 0(%2),%1\n"			\
		"2:	l.sw 4(%2),%H1\n"			\
		"3:\n"						\
		".section .fixup,\"ax\"\n"			\
		"4:	l.addi %0,r0,%3\n"			\
		"	l.j 3b\n"				\
		"	l.nop\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 2\n"				\
		"	.long 1b,4b\n"				\
		"	.long 2b,4b\n"				\
		".previous"					\
		: "=r"(err)					\
		: "r"(x), "r"(addr), "i"(-EFAULT), "0"(err))

#define __get_user_nocheck(x, ptr, size)			\
({								\
	long __gu_err, __gu_val;				\
	__get_user_size(__gu_val, (ptr), (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;		\
	__gu_err;						\
})

#define __get_user_check(x, ptr, size)					\
({									\
	long __gu_err = -EFAULT, __gu_val = 0;				\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	if (access_ok(__gu_addr, size))			\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x, ptr, size, retval)				\
do {									\
	retval = 0;							\
	switch (size) {							\
	case 1: __get_user_asm(x, ptr, retval, "l.lbz"); break;		\
	case 2: __get_user_asm(x, ptr, retval, "l.lhz"); break;		\
	case 4: __get_user_asm(x, ptr, retval, "l.lwz"); break;		\
	case 8: __get_user_asm2(x, ptr, retval); break;			\
	default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %1,0(%2)\n"		\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	l.addi %0,r0,%3\n"		\
		"	l.addi %1,r0,0\n"		\
		"	l.j 2b\n"			\
		"	l.nop\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align 2\n"			\
		"	.long 1b,3b\n"			\
		".previous"				\
		: "=r"(err), "=r"(x)			\
		: "r"(addr), "i"(-EFAULT), "0"(err))

#define __get_user_asm2(x, addr, err)			\
	__asm__ __volatile__(				\
		"1:	l.lwz %1,0(%2)\n"		\
		"2:	l.lwz %H1,4(%2)\n"		\
		"3:\n"					\
		".section .fixup,\"ax\"\n"		\
		"4:	l.addi %0,r0,%3\n"		\
		"	l.addi %1,r0,0\n"		\
		"	l.addi %H1,r0,0\n"		\
		"	l.j 3b\n"			\
		"	l.nop\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align 2\n"			\
		"	.long 1b,4b\n"			\
		"	.long 2b,4b\n"			\
		".previous"				\
		: "=r"(err), "=&r"(x)			\
		: "r"(addr), "i"(-EFAULT), "0"(err))

/* more complex routines */

extern unsigned long __must_check
__copy_tofrom_user(void *to, const void *from, unsigned long size);
static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long size)
{
	return __copy_tofrom_user(to, (__force const void *)from, size);
}
static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long size)
{
	return __copy_tofrom_user((__force void *)to, from, size);
}
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

extern unsigned long __clear_user(void __user *addr, unsigned long size);

static inline __must_check unsigned long
clear_user(void __user *addr, unsigned long size)
{
	if (likely(access_ok(addr, size)))
		size = __clear_user(addr, size);
	return size;
}

#define user_addr_max() \
	(uaccess_kernel() ? ~0UL : TASK_SIZE)

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

#endif /* __ASM_OPENRISC_UACCESS_H */
