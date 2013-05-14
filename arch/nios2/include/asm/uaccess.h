/*
 * User space memory access functions for Nios II
 *
 * Copyright (C) 2010-2011, Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009, Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * Based on asm/uaccess.h from m68knommu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_UACCESS_H
#define _ASM_NIOS2_UACCESS_H

#include <linux/errno.h>
#include <linux/thread_info.h>
#include <linux/string.h>

#include <asm/page.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

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
	unsigned long insn;
	unsigned long fixup;
};

#ifdef CONFIG_MMU
extern int fixup_exception(struct pt_regs *regs);
#endif

/*
 * Segment stuff
 */
#define MAKE_MM_SEG(s)		((mm_segment_t) { (s) })
#define USER_DS			MAKE_MM_SEG(0x80000000UL)
#define KERNEL_DS		MAKE_MM_SEG(0)

#define get_ds()		(KERNEL_DS)

#define get_fs()		(current_thread_info()->addr_limit)
#define set_fs(seg)		(current_thread_info()->addr_limit = (seg))

#define segment_eq(a, b)	((a).seg == (b).seg)

#ifdef CONFIG_MMU
#define __access_ok(addr, len)			\
	(((signed long)(((long)get_fs().seg) &	\
		((long)(addr) | (((long)(addr)) + (len)) | (len)))) == 0)
#else
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	addr &= ~CONFIG_IO_REGION_BASE;	/* ignore 'uncached' bit */
	return ((addr >= CONFIG_MEM_BASE) && ((addr + size) <= memory_end));
}
#endif /* CONFIG_MMU */

#define access_ok(type, addr, len)		\
	likely(__access_ok((unsigned long)(addr), (unsigned long)(len)))

#ifdef CONFIG_MMU
# define __EX_TABLE_SECTION	".section __ex_table,\"a\"\n"
#else
# define __EX_TABLE_SECTION	".section .discard,\"a\"\n"
#endif

/*
 * Zero Userspace
 */

static inline unsigned long __must_check __clear_user(void __user *to,
						      unsigned long n)
{
	__asm__ __volatile__ (
		"1:     stb     zero, 0(%1)\n"
		"       addi    %0, %0, -1\n"
		"       addi    %1, %1, 1\n"
		"       bne     %0, zero, 1b\n"
		"2:\n"
		__EX_TABLE_SECTION
		".word  1b, 2b\n"
		".previous\n"
		: "=r" (n), "=r" (to)
		: "0" (n), "1" (to)
	);

	return n;
}

static inline unsigned long __must_check clear_user(void __user *to,
						    unsigned long n)
{
	if (!access_ok(VERIFY_WRITE, to, n))
		return n;
	return __clear_user(to, n);
}

#ifdef CONFIG_MMU
extern long __copy_from_user(void *to, const void __user *from,
				unsigned long n);
extern long __copy_to_user(void __user *to, const void *from, unsigned long n);

static inline long copy_from_user(void *to, const void __user *from,
				unsigned long n)
{
	if (!access_ok(VERIFY_READ, from, n))
		return n;
	return __copy_from_user(to, from, n);
}

static inline long copy_to_user(void __user *to, const void *from,
				unsigned long n)
{
	if (!access_ok(VERIFY_WRITE, to, n))
		return n;
	return __copy_to_user(to, from, n);
}

extern long strncpy_from_user(char *__to, const char __user *__from,
				long __len);
extern long strnlen_user(const char __user *s, long n);

#else /* CONFIG_MMU */
# define copy_from_user(to, from, n)	(memcpy(to, from, n), 0)
# define copy_to_user(to, from, n)	(memcpy(to, from, n), 0)

# define __copy_from_user(to, from, n)	copy_from_user(to, from, n)
# define __copy_to_user(to, from, n)	copy_to_user(to, from, n)

static inline long strncpy_from_user(char *dst, const char *src, long count)
{
	char *tmp;
	strncpy(dst, src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--)
		;
	return tmp - dst; /* DAVIDM should we count a NUL ?  check getname */
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
static inline long strnlen_user(const char *src, long n)
{
	return strlen(src) + 1; /* DAVIDM make safer */
}

#endif /* CONFIG_MMU */

#define __copy_from_user_inatomic	__copy_from_user
#define __copy_to_user_inatomic		__copy_to_user

/*
 * TODO: get_user/put_user stuff below can probably be the same for MMU and
 * NOMMU.
 */

#ifdef CONFIG_MMU

/* Optimized macros */
#define __get_user_asm(val, insn, addr, err)				\
{									\
	__asm__ __volatile__(						\
	"       movi    %0, %3\n"					\
	"1:   " insn " %1, 0(%2)\n"					\
	"       movi     %0, 0\n"					\
	"2:\n"								\
	"       .section __ex_table,\"a\"\n"				\
	"       .word 1b, 2b\n"						\
	"       .previous"						\
	: "=&r" (err), "=r" (val)					\
	: "r" (addr), "i" (-EFAULT));					\
}

#define __get_user_unknown(val, size, ptr, err) do {			\
	err = 0;							\
	if (copy_from_user(&(val), ptr, size)) {			\
		err = -EFAULT;						\
	}								\
	} while (0)

#define __get_user_common(val, size, ptr, err)				\
do {									\
	switch (size) {							\
	case 1:								\
		__get_user_asm(val, "ldbu", ptr, err);			\
		break;							\
	case 2:								\
		__get_user_asm(val, "ldhu", ptr, err);			\
		break;							\
	case 4:								\
		__get_user_asm(val, "ldw", ptr, err);			\
		break;							\
	default:							\
		__get_user_unknown(val, size, ptr, err);		\
		break;							\
	}								\
} while (0)

#define __get_user(x, ptr)						\
	({								\
	long __gu_err = -EFAULT;					\
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);		\
	unsigned long __gu_val; /* FIXME: should be __typeof__ */	\
	__get_user_common(__gu_val, sizeof(*(ptr)), __gu_ptr, __gu_err);\
	(x) = (__typeof__(x))__gu_val;					\
	__gu_err;							\
	})

#define get_user(x, ptr)						\
({									\
	long __gu_err = -EFAULT;					\
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);		\
	unsigned long __gu_val = 0;					\
	if (access_ok(VERIFY_READ,  __gu_ptr, sizeof(*__gu_ptr)))	\
		__get_user_common(__gu_val, sizeof(*__gu_ptr),		\
			__gu_ptr, __gu_err);				\
	(x) = (__typeof__(x))__gu_val;					\
	__gu_err;							\
})

#define __put_user_asm(val, insn, ptr, err)				\
{									\
	__asm__ __volatile__(						\
	"       movi    %0, %3\n"					\
	"1:   " insn " %1, 0(%2)\n"					\
	"       movi     %0, 0\n"					\
	"2:\n"								\
	"       .section __ex_table,\"a\"\n"				\
	"       .word 1b, 2b\n"						\
	"       .previous\n"						\
	: "=&r" (err)							\
	: "r" (val), "r" (ptr), "i" (-EFAULT));				\
}

#define put_user(x, ptr)						\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __user *__pu_ptr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (__typeof(*ptr))(x);		\
	if (access_ok(VERIFY_WRITE, __pu_ptr, sizeof(*__pu_ptr))) {	\
		switch (sizeof(*__pu_ptr)) {				\
		case 1:							\
			__put_user_asm(__pu_val, "stb", __pu_ptr, __pu_err); \
			break;						\
		case 2:							\
			__put_user_asm(__pu_val, "sth", __pu_ptr, __pu_err); \
			break;						\
		case 4:							\
			__put_user_asm(__pu_val, "stw", __pu_ptr, __pu_err); \
			break;						\
		default:						\
			/* XXX: This looks wrong... */			\
			__pu_err = 0;					\
			if (copy_to_user(__pu_ptr, &(__pu_val),		\
				sizeof(*__pu_ptr)))			\
				__pu_err = -EFAULT;			\
			break;						\
		}							\
	}								\
	__pu_err;							\
})

#define __put_user(x, ptr) put_user(x, ptr)

#else /* CONFIG_MMU */

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, ptr)				\
({							\
	int __pu_err = 0;				\
	__typeof__(*(ptr)) __pu_val = (x);		\
	switch (sizeof(*(ptr))) {			\
	case 1:						\
	case 2:						\
	case 4:						\
	case 8:						\
		memcpy(ptr, &__pu_val, sizeof(*(ptr)));	\
		break;					\
	default:					\
		__pu_err = __put_user_bad();		\
		break;					\
	}						\
	__pu_err;					\
})
#define __put_user(x, ptr) put_user(x, ptr)

extern int __put_user_bad(void);

#define get_user(x, ptr)				\
({							\
	int __gu_err = 0;				\
	typeof(*(ptr)) __gu_val = 0;			\
	switch (sizeof(*(ptr))) {			\
	case 1:						\
	case 2:						\
	case 4:						\
	case 8:						\
		memcpy(&__gu_val, ptr, sizeof(*(ptr)));	\
		break;					\
	default:					\
		__gu_val = 0;				\
		__gu_err = __get_user_bad();		\
		break;					\
	}						\
	(x) = __gu_val;					\
	__gu_err;					\
})
#define __get_user(x, ptr) get_user(x, ptr)

extern int __get_user_bad(void);

#endif /* CONFIG_MMU */

#endif /* _ASM_NIOS2_UACCESS_H */
