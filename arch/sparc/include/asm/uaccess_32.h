/* SPDX-License-Identifier: GPL-2.0 */
/*
 * uaccess.h: User space memore access functions.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/compiler.h>
#include <linux/string.h>

#include <asm/processor.h>

/* Sparc is not segmented, however we need to be able to fool access_ok()
 * when doing system calls from kernel mode legitimately.
 *
 * "For historical reasons, these macros are grossly misnamed." -Linus
 */

#define KERNEL_DS   ((mm_segment_t) { 0 })
#define USER_DS     ((mm_segment_t) { -1 })

#define get_fs()	(current->thread.current_ds)
#define set_fs(val)	((current->thread.current_ds) = (val))

#define uaccess_kernel() (get_fs().seg == KERNEL_DS.seg)

/* We have there a nice not-mapped page at PAGE_OFFSET - PAGE_SIZE, so that this test
 * can be fairly lightweight.
 * No one can read/write anything from userland in the kernel space by setting
 * large size and address near to PAGE_OFFSET - a fault will break his intentions.
 */
#define __user_ok(addr, size) ({ (void)(size); (addr) < STACK_TOP; })
#define __kernel_ok (uaccess_kernel())
#define __access_ok(addr, size) (__user_ok((addr) & get_fs().seg, (size)))
#define access_ok(addr, size) __access_ok((unsigned long)(addr), size)

/* Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 */
#define put_user(x, ptr) ({ \
	unsigned long __pu_addr = (unsigned long)(ptr); \
	__chk_user_ptr(ptr); \
	__put_user_check((__typeof__(*(ptr)))(x), __pu_addr, sizeof(*(ptr))); \
})

#define get_user(x, ptr) ({ \
	unsigned long __gu_addr = (unsigned long)(ptr); \
	__chk_user_ptr(ptr); \
	__get_user_check((x), __gu_addr, sizeof(*(ptr)), __typeof__(*(ptr))); \
})

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr) \
    __get_user_nocheck((x), (ptr), sizeof(*(ptr)), __typeof__(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct __user *)(x))

#define __put_user_check(x, addr, size) ({ \
	register int __pu_ret; \
	if (__access_ok(addr, size)) { \
		switch (size) { \
		case 1: \
			__put_user_asm(x, b, addr, __pu_ret); \
			break; \
		case 2: \
			__put_user_asm(x, h, addr, __pu_ret); \
			break; \
		case 4: \
			__put_user_asm(x, , addr, __pu_ret); \
			break; \
		case 8: \
			__put_user_asm(x, d, addr, __pu_ret); \
			break; \
		default: \
			__pu_ret = __put_user_bad(); \
			break; \
		} \
	} else { \
		__pu_ret = -EFAULT; \
	} \
	__pu_ret; \
})

#define __put_user_nocheck(x, addr, size) ({			\
	register int __pu_ret;					\
	switch (size) {						\
	case 1: __put_user_asm(x, b, addr, __pu_ret); break;	\
	case 2: __put_user_asm(x, h, addr, __pu_ret); break;	\
	case 4: __put_user_asm(x, , addr, __pu_ret); break;	\
	case 8: __put_user_asm(x, d, addr, __pu_ret); break;	\
	default: __pu_ret = __put_user_bad(); break;		\
	} \
	__pu_ret; \
})

#define __put_user_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Put user asm, inline. */\n"				\
	"1:\t"	"st"#size " %1, %2\n\t"					\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"b	2b\n\t"						\
		" mov	%3, %0\n\t"					\
		".previous\n\n\t"					\
		".section __ex_table,#alloc\n\t"			\
		".align	4\n\t"						\
		".word	1b, 3b\n\t"					\
		".previous\n\n\t"					\
	       : "=&r" (ret) : "r" (x), "m" (*__m(addr)),		\
		 "i" (-EFAULT))

int __put_user_bad(void);

#define __get_user_check(x, addr, size, type) ({ \
	register int __gu_ret; \
	register unsigned long __gu_val; \
	if (__access_ok(addr, size)) { \
		switch (size) { \
		case 1: \
			 __get_user_asm(__gu_val, ub, addr, __gu_ret); \
			break; \
		case 2: \
			__get_user_asm(__gu_val, uh, addr, __gu_ret); \
			break; \
		case 4: \
			__get_user_asm(__gu_val, , addr, __gu_ret); \
			break; \
		case 8: \
			__get_user_asm(__gu_val, d, addr, __gu_ret); \
			break; \
		default: \
			__gu_val = 0; \
			__gu_ret = __get_user_bad(); \
			break; \
		} \
	 } else { \
		 __gu_val = 0; \
		 __gu_ret = -EFAULT; \
	} \
	x = (__force type) __gu_val; \
	__gu_ret; \
})

#define __get_user_nocheck(x, addr, size, type) ({			\
	register int __gu_ret;						\
	register unsigned long __gu_val;				\
	switch (size) {							\
	case 1: __get_user_asm(__gu_val, ub, addr, __gu_ret); break;	\
	case 2: __get_user_asm(__gu_val, uh, addr, __gu_ret); break;	\
	case 4: __get_user_asm(__gu_val, , addr, __gu_ret); break;	\
	case 8: __get_user_asm(__gu_val, d, addr, __gu_ret); break;	\
	default:							\
		__gu_val = 0;						\
		__gu_ret = __get_user_bad();				\
		break;							\
	}								\
	x = (__force type) __gu_val;					\
	__gu_ret;							\
})

#define __get_user_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Get user asm, inline. */\n"				\
	"1:\t"	"ld"#size " %2, %1\n\t"					\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"clr	%1\n\t"						\
		"b	2b\n\t"						\
		" mov	%3, %0\n\n\t"					\
		".previous\n\t"						\
		".section __ex_table,#alloc\n\t"			\
		".align	4\n\t"						\
		".word	1b, 3b\n\n\t"					\
		".previous\n\t"						\
	       : "=&r" (ret), "=&r" (x) : "m" (*__m(addr)),		\
		 "i" (-EFAULT))

int __get_user_bad(void);

unsigned long __copy_user(void __user *to, const void __user *from, unsigned long size);

static inline unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user(to, (__force void __user *) from, n);
}

static inline unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_user((__force void __user *) to, from, n);
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

static inline unsigned long __clear_user(void __user *addr, unsigned long size)
{
	unsigned long ret;

	__asm__ __volatile__ (
		"mov %2, %%o1\n"
		"call __bzero\n\t"
		" mov %1, %%o0\n\t"
		"mov %%o0, %0\n"
		: "=r" (ret) : "r" (addr), "r" (size) :
		"o0", "o1", "o2", "o3", "o4", "o5", "o7",
		"g1", "g2", "g3", "g4", "g5", "g7", "cc");

	return ret;
}

static inline unsigned long clear_user(void __user *addr, unsigned long n)
{
	if (n && __access_ok((unsigned long) addr, n))
		return __clear_user(addr, n);
	else
		return n;
}

__must_check long strnlen_user(const char __user *str, long n);

#endif /* _ASM_UACCESS_H */
