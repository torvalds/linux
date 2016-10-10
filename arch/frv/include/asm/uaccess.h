/* uaccess.h: userspace accessor functions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/segment.h>
#include <asm/sections.h>

#define HAVE_ARCH_UNMAPPED_AREA	/* we decide where to put mmaps */

#define __ptr(x) ((unsigned long __force *)(x))

#define VERIFY_READ	0
#define VERIFY_WRITE	1

/*
 * check that a range of addresses falls within the current address limit
 */
static inline int ___range_ok(unsigned long addr, unsigned long size)
{
#ifdef CONFIG_MMU
	int flag = -EFAULT, tmp;

	asm volatile (
		"	addcc	%3,%2,%1,icc0	\n"	/* set C-flag if addr+size>4GB */
		"	subcc.p	%1,%4,gr0,icc1	\n"	/* jump if addr+size>limit */
		"	bc	icc0,#0,0f	\n"
		"	bhi	icc1,#0,0f	\n"
		"	setlos	#0,%0		\n"	/* mark okay */
		"0:				\n"
		: "=r"(flag), "=&r"(tmp)
		: "r"(addr), "r"(size), "r"(get_addr_limit()), "0"(flag)
		);

	return flag;

#else

	if (addr < memory_start ||
	    addr > memory_end ||
	    size > memory_end - memory_start ||
	    addr + size > memory_end)
		return -EFAULT;

	return 0;
#endif
}

#define __range_ok(addr,size) ___range_ok((unsigned long) (addr), (unsigned long) (size))

#define access_ok(type,addr,size) (__range_ok((void __user *)(addr), (size)) == 0)
#define __access_ok(addr,size) (__range_ok((addr), (size)) == 0)

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
extern unsigned long search_exception_table(unsigned long);


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */
#define __put_user(x, ptr)						\
({									\
	int __pu_err = 0;						\
									\
	typeof(*(ptr)) __pu_val = (x);					\
	__chk_user_ptr(ptr);						\
									\
	switch (sizeof (*(ptr))) {					\
	case 1:								\
		__put_user_asm(__pu_err, __pu_val, ptr, "b", "r");	\
		break;							\
	case 2:								\
		__put_user_asm(__pu_err, __pu_val, ptr, "h", "r");	\
		break;							\
	case 4:								\
		__put_user_asm(__pu_err, __pu_val, ptr, "",  "r");	\
		break;							\
	case 8:								\
		__put_user_asm(__pu_err, __pu_val, ptr, "d", "e");	\
		break;							\
	default:							\
		__pu_err = __put_user_bad();				\
		break;							\
	}								\
	__pu_err;							\
})

#define put_user(x, ptr)			\
({						\
	typeof(*(ptr)) __user *_p = (ptr);	\
	int _e;					\
						\
	_e = __range_ok(_p, sizeof(*_p));	\
	if (_e == 0)				\
		_e = __put_user((x), _p);	\
	_e;					\
})

extern int __put_user_bad(void);

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */

#ifdef CONFIG_MMU

#define __put_user_asm(err,x,ptr,dsize,constraint)					\
do {											\
	asm volatile("1:	st"dsize"%I1	%2,%M1	\n"				\
		     "2:				\n"				\
		     ".subsection 2			\n"				\
		     "3:	setlos		%3,%0	\n"				\
		     "		bra		2b	\n"				\
		     ".previous				\n"				\
		     ".section __ex_table,\"a\"		\n"				\
		     "		.balign		8	\n"				\
		     "		.long		1b,3b	\n"				\
		     ".previous"							\
		     : "=r" (err)							\
		     : "m" (*__ptr(ptr)), constraint (x), "i"(-EFAULT), "0"(err)	\
		     : "memory");							\
} while (0)

#else

#define __put_user_asm(err,x,ptr,bwl,con)	\
do {						\
	asm("	st"bwl"%I0	%1,%M0	\n"	\
	    "	membar			\n"	\
	    :					\
	    : "m" (*__ptr(ptr)), con (x)	\
	    : "memory");			\
} while (0)

#endif

/*****************************************************************************/
/*
 *
 */
#define __get_user(x, ptr)						\
({									\
	int __gu_err = 0;						\
	__chk_user_ptr(ptr);						\
									\
	switch (sizeof(*(ptr))) {					\
	case 1: {							\
		unsigned char __gu_val;					\
		__get_user_asm(__gu_err, __gu_val, ptr, "ub", "=r");	\
		(x) = *(__force __typeof__(*(ptr)) *) &__gu_val;	\
		break;							\
	}								\
	case 2: {							\
		unsigned short __gu_val;				\
		__get_user_asm(__gu_err, __gu_val, ptr, "uh", "=r");	\
		(x) = *(__force __typeof__(*(ptr)) *) &__gu_val;	\
		break;							\
	}								\
	case 4: {							\
		unsigned int __gu_val;					\
		__get_user_asm(__gu_err, __gu_val, ptr, "", "=r");	\
		(x) = *(__force __typeof__(*(ptr)) *) &__gu_val;	\
		break;							\
	}								\
	case 8: {							\
		unsigned long long __gu_val;				\
		__get_user_asm(__gu_err, __gu_val, ptr, "d", "=e");	\
		(x) = *(__force __typeof__(*(ptr)) *) &__gu_val;	\
		break;							\
	}								\
	default:							\
		__gu_err = __get_user_bad();				\
		break;							\
	}								\
	__gu_err;							\
})

#define get_user(x, ptr)			\
({						\
	const typeof(*(ptr)) __user *_p = (ptr);\
	int _e;					\
						\
	_e = __range_ok(_p, sizeof(*_p));	\
	if (likely(_e == 0))			\
		_e = __get_user((x), _p);	\
	else					\
		(x) = (typeof(x)) 0;		\
	_e;					\
})

extern int __get_user_bad(void);

#ifdef CONFIG_MMU

#define __get_user_asm(err,x,ptr,dtype,constraint)	\
do {							\
	asm("1:		ld"dtype"%I2	%M2,%1	\n"	\
	    "2:					\n"	\
	    ".subsection 2			\n"	\
	    "3:		setlos		%3,%0	\n"	\
	    "		setlos		#0,%1	\n"	\
	    "		bra		2b	\n"	\
	    ".previous				\n"	\
	    ".section __ex_table,\"a\"		\n"	\
	    "		.balign		8	\n"	\
	    "		.long		1b,3b	\n"	\
	    ".previous"					\
	    : "=r" (err), constraint (x)		\
	    : "m" (*__ptr(ptr)), "i"(-EFAULT), "0"(err)	\
	    );						\
} while(0)

#else

#define __get_user_asm(err,x,ptr,bwl,con)	\
	asm("	ld"bwl"%I1	%M1,%0	\n"	\
	    "	membar			\n"	\
	    : con(x)				\
	    : "m" (*__ptr(ptr)))

#endif

/*****************************************************************************/
/*
 *
 */
#define ____force(x) (__force void *)(void __user *)(x)
#ifdef CONFIG_MMU
extern long __memset_user(void *dst, unsigned long count);
extern long __memcpy_user(void *dst, const void *src, unsigned long count);

#define __clear_user(dst,count)			__memset_user(____force(dst), (count))
#define __copy_from_user_inatomic(to, from, n)	__memcpy_user((to), ____force(from), (n))
#define __copy_to_user_inatomic(to, from, n)	__memcpy_user(____force(to), (from), (n))

#else

#define __clear_user(dst,count)			(memset(____force(dst), 0, (count)), 0)
#define __copy_from_user_inatomic(to, from, n)	(memcpy((to), ____force(from), (n)), 0)
#define __copy_to_user_inatomic(to, from, n)	(memcpy(____force(to), (from), (n)), 0)

#endif

static inline unsigned long __must_check
clear_user(void __user *to, unsigned long n)
{
	if (likely(__access_ok(to, n)))
		n = __clear_user(to, n);
	return n;
}

static inline unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
       might_fault();
       return __copy_to_user_inatomic(to, from, n);
}

static inline unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
       might_fault();
       return __copy_from_user_inatomic(to, from, n);
}

static inline long copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long ret = n;

	if (likely(__access_ok(from, n)))
		ret = __copy_from_user(to, from, n);

	if (unlikely(ret != 0))
		memset(to + (n - ret), 0, ret);

	return ret;
}

static inline long copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return likely(__access_ok(to, n)) ? __copy_to_user(to, from, n) : n;
}

extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern long strnlen_user(const char __user *src, long count);

#define strlen_user(str) strnlen_user(str, 32767)

extern unsigned long search_exception_table(unsigned long addr);

#endif /* _ASM_UACCESS_H */
