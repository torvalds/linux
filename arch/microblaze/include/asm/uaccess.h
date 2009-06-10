/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_UACCESS_H
#define _ASM_MICROBLAZE_UACCESS_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h> /* RLIMIT_FSIZE */
#include <linux/mm.h>

#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <linux/string.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

extern int ___range_ok(unsigned long addr, unsigned long size);

#define __range_ok(addr, size) \
		___range_ok((unsigned long)(addr), (unsigned long)(size))

#define access_ok(type, addr, size) (__range_ok((addr), (size)) == 0)
#define __access_ok(add, size) (__range_ok((addr), (size)) == 0)

extern inline int bad_user_access_length(void)
{
	return 0;
}
/* FIXME this is function for optimalization -> memcpy */
#define __get_user(var, ptr)					\
	({							\
		int __gu_err = 0;				\
		switch (sizeof(*(ptr))) {			\
		case 1:						\
		case 2:						\
		case 4:						\
			(var) = *(ptr);				\
			break;					\
		case 8:						\
			memcpy((void *) &(var), (ptr), 8);	\
			break;					\
		default:					\
			(var) = 0;				\
			__gu_err = __get_user_bad();		\
			break;					\
		}						\
		__gu_err;					\
	})

#define __get_user_bad()	(bad_user_access_length(), (-EFAULT))

#define __put_user(var, ptr)					\
	({							\
		int __pu_err = 0;				\
		switch (sizeof(*(ptr))) {			\
		case 1:						\
		case 2:						\
		case 4:						\
			*(ptr) = (var);				\
			break;					\
		case 8: {					\
			typeof(*(ptr)) __pu_val = var;		\
			memcpy(ptr, &__pu_val, sizeof(__pu_val));\
			}					\
			break;					\
		default:					\
			__pu_err = __put_user_bad();		\
			break;					\
		}							\
		__pu_err;						\
	})

#define __put_user_bad()	(bad_user_access_length(), (-EFAULT))

#define put_user(x, ptr)	__put_user(x, ptr)
#define get_user(x, ptr)	__get_user(x, ptr)

#define copy_to_user(to, from, n)		(memcpy(to, from, n), 0)
#define copy_from_user(to, from, n)		(memcpy(to, from, n), 0)

#define __copy_to_user(to, from, n)		(copy_to_user(to, from, n))
#define __copy_from_user(to, from, n)		(copy_from_user(to, from, n))
#define __copy_to_user_inatomic(to, from, n)	(__copy_to_user(to, from, n))
#define __copy_from_user_inatomic(to, from, n)	(__copy_from_user(to, from, n))

#define __clear_user(addr, n)	(memset((void *)addr, 0, n), 0)

static inline unsigned long clear_user(void *addr, unsigned long size)
{
	if (access_ok(VERIFY_WRITE, addr, size))
		size = __clear_user(addr, size);
	return size;
}

/* Returns 0 if exception not found and fixup otherwise. */
extern unsigned long search_exception_table(unsigned long);


extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern long strnlen_user(const char __user *src, long count);
extern long __strncpy_from_user(char *dst, const char __user *src, long count);

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue. No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path. This means when everything is well,
 * we don't even have to jump over them. Further, they do not intrude
 * on our cache or tlb entries.
 */
struct exception_table_entry {
	unsigned long insn, fixup;
};

#endif  /* __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* _ASM_MICROBLAZE_UACCESS_H */
