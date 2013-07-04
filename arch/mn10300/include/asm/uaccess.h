/* MN10300 userspace access functions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/errno.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */
#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_XDS	MAKE_MM_SEG(0xBFFFFFFF)
#define KERNEL_DS	MAKE_MM_SEG(0x9FFFFFFF)
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))
#define __kernel_ds_p() (current_thread_info()->addr_limit.seg == 0x9FFFFFFF)

#define segment_eq(a, b) ((a).seg == (b).seg)

#define __addr_ok(addr) \
	((unsigned long)(addr) < (current_thread_info()->addr_limit.seg))

/*
 * check that a range of addresses falls within the current address limit
 */
static inline int ___range_ok(unsigned long addr, unsigned int size)
{
	int flag = 1, tmp;

	asm("	add	%3,%1	\n"	/* set C-flag if addr + size > 4Gb */
	    "	bcs	0f	\n"
	    "	cmp	%4,%1	\n"	/* jump if addr+size>limit (error) */
	    "	bhi	0f	\n"
	    "	clr	%0	\n"	/* mark okay */
	    "0:			\n"
	    : "=r"(flag), "=&r"(tmp)
	    : "1"(addr), "ir"(size),
	      "r"(current_thread_info()->addr_limit.seg), "0"(flag)
	    : "cc"
	    );

	return flag;
}

#define __range_ok(addr, size) ___range_ok((unsigned long)(addr), (u32)(size))

#define access_ok(type, addr, size) (__range_ok((addr), (size)) == 0)
#define __access_ok(addr, size)     (__range_ok((addr), (size)) == 0)

static inline int verify_area(int type, const void *addr, unsigned long size)
{
	return access_ok(type, addr, size) ? 0 : -EFAULT;
}


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

/* Returns 0 if exception not found and fixup otherwise.  */
extern int fixup_exception(struct pt_regs *regs);

#define put_user(x, ptr) __put_user_check((x), (ptr), sizeof(*(ptr)))
#define get_user(x, ptr) __get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x, ptr) __put_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr) __get_user_nocheck((x), (ptr), sizeof(*(ptr)))

/*
 * The "xxx_ret" versions return constant specified in third argument, if
 * something bad happens. These macros can be optimized for the
 * case of just returning from the function xxx_ret is used.
 */

#define put_user_ret(x, ptr, ret) \
	({ if (put_user((x), (ptr)))	return (ret); })
#define get_user_ret(x, ptr, ret) \
	({ if (get_user((x), (ptr)))	return (ret); })
#define __put_user_ret(x, ptr, ret) \
	({ if (__put_user((x), (ptr)))	return (ret); })
#define __get_user_ret(x, ptr, ret) \
	({ if (__get_user((x), (ptr)))	return (ret); })

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __get_user_nocheck(x, ptr, size)				\
({									\
	unsigned long __gu_addr;					\
	int __gu_err;							\
	__gu_addr = (unsigned long) (ptr);				\
	switch (size) {							\
	case 1: {							\
		unsigned char __gu_val;					\
		__get_user_asm("bu");					\
		(x) = *(__force __typeof__(*(ptr))*) &__gu_val;		\
		break;							\
	}								\
	case 2: {							\
		unsigned short __gu_val;				\
		__get_user_asm("hu");					\
		(x) = *(__force __typeof__(*(ptr))*) &__gu_val;		\
		break;							\
	}								\
	case 4: {							\
		unsigned int __gu_val;					\
		__get_user_asm("");					\
		(x) = *(__force __typeof__(*(ptr))*) &__gu_val;		\
		break;							\
	}								\
	default:							\
		__get_user_unknown();					\
		break;							\
	}								\
	__gu_err;							\
})

#define __get_user_check(x, ptr, size)					\
({									\
	const __typeof__(*(ptr))* __guc_ptr = (ptr);			\
	int _e;								\
	if (likely(__access_ok((unsigned long) __guc_ptr, (size))))	\
		_e = __get_user_nocheck((x), __guc_ptr, (size));	\
	else {								\
		_e = -EFAULT;						\
		(x) = (__typeof__(x))0;					\
	}								\
	_e;								\
})

#define __get_user_asm(INSN)					\
({								\
	asm volatile(					\
		"1:\n"						\
		"	mov"INSN"	%2,%1\n"		\
		"	mov		0,%0\n"			\
		"2:\n"						\
		"	.section	.fixup,\"ax\"\n"	\
		"3:\n\t"					\
		"	mov		%3,%0\n"		\
		"	jmp		2b\n"			\
		"	.previous\n"				\
		"	.section	__ex_table,\"a\"\n"	\
		"	.balign		4\n"			\
		"	.long		1b, 3b\n"		\
		"	.previous"				\
		: "=&r" (__gu_err), "=&r" (__gu_val)		\
		: "m" (__m(__gu_addr)), "i" (-EFAULT));		\
})

extern int __get_user_unknown(void);

#define __put_user_nocheck(x, ptr, size)			\
({								\
	union {							\
		__typeof__(*(ptr)) val;				\
		u32 bits[2];					\
	} __pu_val;						\
	unsigned long __pu_addr;				\
	int __pu_err;						\
	__pu_val.val = (x);					\
	__pu_addr = (unsigned long) (ptr);			\
	switch (size) {						\
	case 1:  __put_user_asm("bu"); break;			\
	case 2:  __put_user_asm("hu"); break;			\
	case 4:  __put_user_asm(""  ); break;			\
	case 8:  __put_user_asm8();    break;			\
	default: __pu_err = __put_user_unknown(); break;	\
	}							\
	__pu_err;						\
})

#define __put_user_check(x, ptr, size)					\
({									\
	union {								\
		__typeof__(*(ptr)) val;					\
		u32 bits[2];						\
	} __pu_val;							\
	unsigned long __pu_addr;					\
	int __pu_err;							\
	__pu_val.val = (x);						\
	__pu_addr = (unsigned long) (ptr);				\
	if (likely(__access_ok(__pu_addr, size))) {			\
		switch (size) {						\
		case 1:  __put_user_asm("bu"); break;			\
		case 2:  __put_user_asm("hu"); break;			\
		case 4:  __put_user_asm(""  ); break;			\
		case 8:  __put_user_asm8();    break;			\
		default: __pu_err = __put_user_unknown(); break;	\
		}							\
	}								\
	else {								\
		__pu_err = -EFAULT;					\
	}								\
	__pu_err;							\
})

#define __put_user_asm(INSN)					\
({								\
	asm volatile(						\
		"1:\n"						\
		"	mov"INSN"	%1,%2\n"		\
		"	mov		0,%0\n"			\
		"2:\n"						\
		"	.section	.fixup,\"ax\"\n"	\
		"3:\n"						\
		"	mov		%3,%0\n"		\
		"	jmp		2b\n"			\
		"	.previous\n"				\
		"	.section	__ex_table,\"a\"\n"	\
		"	.balign		4\n"			\
		"	.long		1b, 3b\n"		\
		"	.previous"				\
		: "=&r" (__pu_err)				\
		: "r" (__pu_val.val), "m" (__m(__pu_addr)),	\
		  "i" (-EFAULT)					\
		);						\
})

#define __put_user_asm8()						\
({									\
	asm volatile(							\
		"1:	mov		%1,%3		\n"		\
		"2:	mov		%2,%4		\n"		\
		"	mov		0,%0		\n"		\
		"3:					\n"		\
		"	.section	.fixup,\"ax\"	\n"		\
		"4:					\n"		\
		"	mov		%5,%0		\n"		\
		"	jmp		3b		\n"		\
		"	.previous			\n"		\
		"	.section	__ex_table,\"a\"\n"		\
		"	.balign		4		\n"		\
		"	.long		1b, 4b		\n"		\
		"	.long		2b, 4b		\n"		\
		"	.previous			\n"		\
		: "=&r" (__pu_err)					\
		: "r" (__pu_val.bits[0]), "r" (__pu_val.bits[1]),	\
		  "m" (__m(__pu_addr)), "m" (__m(__pu_addr+4)),		\
		  "i" (-EFAULT)						\
		);							\
})

extern int __put_user_unknown(void);


/*
 * Copy To/From Userspace
 */
/* Generic arbitrary sized copy.  */
#define __copy_user(to, from, size)					\
do {									\
	if (size) {							\
		void *__to = to;					\
		const void *__from = from;				\
		int w;							\
		asm volatile(						\
			"0:     movbu	(%0),%3;\n"			\
			"1:     movbu	%3,(%1);\n"			\
			"	inc	%0;\n"				\
			"	inc	%1;\n"				\
			"       add	-1,%2;\n"			\
			"       bne	0b;\n"				\
			"2:\n"						\
			"	.section .fixup,\"ax\"\n"		\
			"3:	jmp	2b\n"				\
			"	.previous\n"				\
			"	.section __ex_table,\"a\"\n"		\
			"       .balign	4\n"				\
			"       .long	0b,3b\n"			\
			"       .long	1b,3b\n"			\
			"	.previous\n"				\
			: "=a"(__from), "=a"(__to), "=r"(size), "=&r"(w)\
			: "0"(__from), "1"(__to), "2"(size)		\
			: "cc", "memory");				\
	}								\
} while (0)

#define __copy_user_zeroing(to, from, size)				\
do {									\
	if (size) {							\
		void *__to = to;					\
		const void *__from = from;				\
		int w;							\
		asm volatile(						\
			"0:     movbu	(%0),%3;\n"			\
			"1:     movbu	%3,(%1);\n"			\
			"	inc	%0;\n"				\
			"	inc	%1;\n"				\
			"       add	-1,%2;\n"			\
			"       bne	0b;\n"				\
			"2:\n"						\
			"	.section .fixup,\"ax\"\n"		\
			"3:\n"						\
			"	mov	%2,%0\n"			\
			"	clr	%3\n"				\
			"4:     movbu	%3,(%1);\n"			\
			"	inc	%1;\n"				\
			"       add	-1,%2;\n"			\
			"       bne	4b;\n"				\
			"	mov	%0,%2\n"			\
			"	jmp	2b\n"				\
			"	.previous\n"				\
			"	.section __ex_table,\"a\"\n"		\
			"       .balign	4\n"				\
			"       .long	0b,3b\n"			\
			"       .long	1b,3b\n"			\
			"	.previous\n"				\
			: "=a"(__from), "=a"(__to), "=r"(size), "=&r"(w)\
			: "0"(__from), "1"(__to), "2"(size)		\
			: "cc", "memory");				\
	}								\
} while (0)

/* We let the __ versions of copy_from/to_user inline, because they're often
 * used in fast paths and have only a small space overhead.
 */
static inline
unsigned long __generic_copy_from_user_nocheck(void *to, const void *from,
					       unsigned long n)
{
	__copy_user_zeroing(to, from, n);
	return n;
}

static inline
unsigned long __generic_copy_to_user_nocheck(void *to, const void *from,
					     unsigned long n)
{
	__copy_user(to, from, n);
	return n;
}


#if 0
#error "don't use - these macros don't increment to & from pointers"
/* Optimize just a little bit when we know the size of the move. */
#define __constant_copy_user(to, from, size)	\
do {						\
	asm volatile(				\
		"       mov %0,a0;\n"		\
		"0:     movbu (%1),d3;\n"	\
		"1:     movbu d3,(%2);\n"	\
		"       add -1,a0;\n"		\
		"       bne 0b;\n"		\
		"2:;"				\
		".section .fixup,\"ax\"\n"	\
		"3:	jmp 2b\n"		\
		".previous\n"			\
		".section __ex_table,\"a\"\n"	\
		"       .balign 4\n"		\
		"       .long 0b,3b\n"		\
		"       .long 1b,3b\n"		\
		".previous"			\
		:				\
		: "d"(size), "d"(to), "d"(from)	\
		: "d3", "a0");			\
} while (0)

/* Optimize just a little bit when we know the size of the move. */
#define __constant_copy_user_zeroing(to, from, size)	\
do {							\
	asm volatile(					\
		"       mov %0,a0;\n"			\
		"0:     movbu (%1),d3;\n"		\
		"1:     movbu d3,(%2);\n"		\
		"       add -1,a0;\n"			\
		"       bne 0b;\n"			\
		"2:;"					\
		".section .fixup,\"ax\"\n"		\
		"3:	jmp 2b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"       .balign 4\n"			\
		"       .long 0b,3b\n"			\
		"       .long 1b,3b\n"			\
		".previous"				\
		:					\
		: "d"(size), "d"(to), "d"(from)		\
		: "d3", "a0");				\
} while (0)

static inline
unsigned long __constant_copy_to_user(void *to, const void *from,
				      unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__constant_copy_user(to, from, n);
	return n;
}

static inline
unsigned long __constant_copy_from_user(void *to, const void *from,
					unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__constant_copy_user_zeroing(to, from, n);
	return n;
}

static inline
unsigned long __constant_copy_to_user_nocheck(void *to, const void *from,
					      unsigned long n)
{
	__constant_copy_user(to, from, n);
	return n;
}

static inline
unsigned long __constant_copy_from_user_nocheck(void *to, const void *from,
						unsigned long n)
{
	__constant_copy_user_zeroing(to, from, n);
	return n;
}
#endif

extern unsigned long __generic_copy_to_user(void __user *, const void *,
					    unsigned long);
extern unsigned long __generic_copy_from_user(void *, const void __user *,
					      unsigned long);

#define __copy_to_user_inatomic(to, from, n) \
	__generic_copy_to_user_nocheck((to), (from), (n))
#define __copy_from_user_inatomic(to, from, n) \
	__generic_copy_from_user_nocheck((to), (from), (n))

#define __copy_to_user(to, from, n)			\
({							\
	might_sleep();					\
	__copy_to_user_inatomic((to), (from), (n));	\
})

#define __copy_from_user(to, from, n)			\
({							\
	might_sleep();					\
	__copy_from_user_inatomic((to), (from), (n));	\
})


#define copy_to_user(to, from, n)   __generic_copy_to_user((to), (from), (n))
#define copy_from_user(to, from, n) __generic_copy_from_user((to), (from), (n))

extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern long __strncpy_from_user(char *dst, const char __user *src, long count);
extern long strnlen_user(const char __user *str, long n);
#define strlen_user(str) strnlen_user(str, ~0UL >> 1)
extern unsigned long clear_user(void __user *mem, unsigned long len);
extern unsigned long __clear_user(void __user *mem, unsigned long len);

#endif /* _ASM_UACCESS_H */
