/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
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
#include <linux/string.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

/*
 * On Microblaze the fs value is actually the top of the corresponding
 * address space.
 *
 * The fs value determines whether argument validity checking should be
 * performed or not. If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 *
 * For non-MMU arch like Microblaze, KERNEL_DS and USER_DS is equal.
 */
# define MAKE_MM_SEG(s)       ((mm_segment_t) { (s) })

#  ifndef CONFIG_MMU
#  define KERNEL_DS	MAKE_MM_SEG(0)
#  define USER_DS	KERNEL_DS
#  else
#  define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#  define USER_DS	MAKE_MM_SEG(TASK_SIZE - 1)
#  endif

# define get_ds()	(KERNEL_DS)
# define get_fs()	(current_thread_info()->addr_limit)
# define set_fs(val)	(current_thread_info()->addr_limit = (val))

# define segment_eq(a, b)	((a).seg == (b).seg)

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

#ifndef CONFIG_MMU

/* Check against bounds of physical memory */
static inline int ___range_ok(unsigned long addr, unsigned long size)
{
	return ((addr < memory_start) ||
		((addr + size) > memory_end));
}

#define __range_ok(addr, size) \
		___range_ok((unsigned long)(addr), (unsigned long)(size))

#define access_ok(type, addr, size) (__range_ok((addr), (size)) == 0)

#else

/*
 * Address is valid if:
 *  - "addr", "addr + size" and "size" are all below the limit
 */
#define access_ok(type, addr, size) \
	(get_fs().seg > (((unsigned long)(addr)) | \
		(size) | ((unsigned long)(addr) + (size))))

/* || printk("access_ok failed for %s at 0x%08lx (size %d), seg 0x%08x\n",
 type?"WRITE":"READ",addr,size,get_fs().seg)) */

#endif

#ifdef CONFIG_MMU
# define __FIXUP_SECTION	".section .fixup,\"ax\"\n"
# define __EX_TABLE_SECTION	".section __ex_table,\"a\"\n"
#else
# define __FIXUP_SECTION	".section .discard,\"ax\"\n"
# define __EX_TABLE_SECTION	".section .discard,\"a\"\n"
#endif

#ifndef CONFIG_MMU

/* Undefined function to trigger linker error */
extern int bad_user_access_length(void);

/* FIXME this is function for optimalization -> memcpy */
#define __get_user(var, ptr)				\
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

/* FIXME is not there defined __pu_val */
#define __put_user(var, ptr)					\
({								\
	int __pu_err = 0;					\
	switch (sizeof(*(ptr))) {				\
	case 1:							\
	case 2:							\
	case 4:							\
		*(ptr) = (var);					\
		break;						\
	case 8: {						\
		typeof(*(ptr)) __pu_val = (var);		\
		memcpy(ptr, &__pu_val, sizeof(__pu_val));	\
		}						\
		break;						\
	default:						\
		__pu_err = __put_user_bad();			\
		break;						\
	}							\
	__pu_err;						\
})

#define __put_user_bad()	(bad_user_access_length(), (-EFAULT))

#define put_user(x, ptr)	__put_user((x), (ptr))
#define get_user(x, ptr)	__get_user((x), (ptr))

#define copy_to_user(to, from, n)	(memcpy((to), (from), (n)), 0)
#define copy_from_user(to, from, n)	(memcpy((to), (from), (n)), 0)

#define __copy_to_user(to, from, n)	(copy_to_user((to), (from), (n)))
#define __copy_from_user(to, from, n)	(copy_from_user((to), (from), (n)))
#define __copy_to_user_inatomic(to, from, n) \
			(__copy_to_user((to), (from), (n)))
#define __copy_from_user_inatomic(to, from, n) \
			(__copy_from_user((to), (from), (n)))

#define __clear_user(addr, n)	(memset((void *)(addr), 0, (n)), 0)

/* stejne s MMU */
static inline unsigned long clear_user(void *addr, unsigned long size)
{
	if (access_ok(VERIFY_WRITE, addr, size))
		size = __clear_user(addr, size);
	return size;
}

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);

extern long strncpy_from_user(char *dst, const char *src, long count);
extern long strnlen_user(const char *src, long count);

#else /* CONFIG_MMU */

/*
 * All the __XXX versions macros/functions below do not perform
 * access checking. It is assumed that the necessary checks have been
 * already performed before the finction (macro) is called.
 */

#define __get_user_asm(insn, __gu_ptr, __gu_val, __gu_err)	\
({								\
	__asm__ __volatile__ (					\
			"1:"	insn	" %1, %2, r0;"		\
			"	addk	%0, r0, r0;"		\
			"2:			"		\
			__FIXUP_SECTION				\
			"3:	brid	2b;	"		\
			"	addik	%0, r0, %3;"		\
			".previous;"				\
			__EX_TABLE_SECTION			\
			".word	1b,3b;"				\
			".previous;"				\
		: "=&r"(__gu_err), "=r"(__gu_val)		\
		: "r"(__gu_ptr), "i"(-EFAULT)			\
	);							\
})

#define __get_user(x, ptr)						\
({									\
	unsigned long __gu_val;						\
	/*unsigned long __gu_ptr = (unsigned long)(ptr);*/		\
	long __gu_err;							\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm("lbu", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 2:								\
		__get_user_asm("lhu", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 4:								\
		__get_user_asm("lw", (ptr), __gu_val, __gu_err);	\
		break;							\
	default:							\
		__gu_val = 0; __gu_err = -EINVAL;			\
	}								\
	x = (__typeof__(*(ptr))) __gu_val;				\
	__gu_err;							\
})

#define get_user(x, ptr)						\
({									\
	access_ok(VERIFY_READ, (ptr), sizeof(*(ptr)))			\
		? __get_user((x), (ptr)) : -EFAULT;			\
})

#define __put_user_asm(insn, __gu_ptr, __gu_val, __gu_err)	\
({								\
	__asm__ __volatile__ (					\
			"1:"	insn	" %1, %2, r0;"		\
			"	addk	%0, r0, r0;"		\
			"2:			"		\
			__FIXUP_SECTION				\
			"3:	brid	2b;"			\
			"	addik	%0, r0, %3;"		\
			".previous;"				\
			__EX_TABLE_SECTION			\
			".word	1b,3b;"				\
			".previous;"				\
		: "=&r"(__gu_err)				\
		: "r"(__gu_val), "r"(__gu_ptr), "i"(-EFAULT)	\
	);							\
})

#define __put_user_asm_8(__gu_ptr, __gu_val, __gu_err)		\
({								\
	__asm__ __volatile__ ("	lwi	%0, %1, 0;"		\
			"1:	swi	%0, %2, 0;"		\
			"	lwi	%0, %1, 4;"		\
			"2:	swi	%0, %2, 4;"		\
			"	addk	%0, r0, r0;"		\
			"3:				"	\
			__FIXUP_SECTION				\
			"4:	brid	3b;"			\
			"	addik	%0, r0, %3;"		\
			".previous;"				\
			__EX_TABLE_SECTION			\
			".word	1b,4b,2b,4b;"			\
			".previous;"				\
		: "=&r"(__gu_err)				\
		: "r"(&__gu_val), "r"(__gu_ptr), "i"(-EFAULT)	\
		);						\
})

#define __put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) volatile __gu_val = (x);			\
	long __gu_err = 0;						\
	switch (sizeof(__gu_val)) {					\
	case 1:								\
		__put_user_asm("sb", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 2: 							\
		__put_user_asm("sh", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 4:								\
		__put_user_asm("sw", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 8:								\
		__put_user_asm_8((ptr), __gu_val, __gu_err);		\
		break;							\
	default:							\
		__gu_err = -EINVAL;					\
	}								\
	__gu_err;							\
})

#define put_user(x, ptr)						\
({									\
	access_ok(VERIFY_WRITE, (ptr), sizeof(*(ptr)))			\
		? __put_user((x), (ptr)) : -EFAULT;			\
})

/* Return: number of not copied bytes, i.e. 0 if OK or non-zero if fail. */
static inline unsigned long __must_check __clear_user(void __user *to,
							unsigned long n)
{
	/* normal memset with two words to __ex_table */
	__asm__ __volatile__ (				\
			"1:	sb	r0, %2, r0;"	\
			"	addik	%0, %0, -1;"	\
			"	bneid	%0, 1b;"	\
			"	addik	%2, %2, 1;"	\
			"2:			"	\
			__EX_TABLE_SECTION		\
			".word	1b,2b;"			\
			".previous;"			\
		: "=r"(n)				\
		: "0"(n), "r"(to)
	);
	return n;
}

static inline unsigned long __must_check clear_user(void __user *to,
							unsigned long n)
{
	might_sleep();
	if (unlikely(!access_ok(VERIFY_WRITE, to, n)))
		return n;

	return __clear_user(to, n);
}

#define __copy_from_user(to, from, n)	copy_from_user((to), (from), (n))
#define __copy_from_user_inatomic(to, from, n) \
		copy_from_user((to), (from), (n))

#define copy_to_user(to, from, n)					\
	(access_ok(VERIFY_WRITE, (to), (n)) ?				\
		__copy_tofrom_user((void __user *)(to),			\
			(__force const void __user *)(from), (n))	\
		: -EFAULT)

#define __copy_to_user(to, from, n)	copy_to_user((to), (from), (n))
#define __copy_to_user_inatomic(to, from, n)	copy_to_user((to), (from), (n))

#define copy_from_user(to, from, n)					\
	(access_ok(VERIFY_READ, (from), (n)) ?				\
		__copy_tofrom_user((__force void __user *)(to),		\
			(void __user *)(from), (n))			\
		: -EFAULT)

extern int __strncpy_user(char *to, const char __user *from, int len);
extern int __strnlen_user(const char __user *sstr, int len);

#define strncpy_from_user(to, from, len)	\
		(access_ok(VERIFY_READ, from, 1) ?	\
			__strncpy_user(to, from, len) : -EFAULT)
#define strnlen_user(str, len)	\
		(access_ok(VERIFY_READ, str, 1) ? __strnlen_user(str, len) : 0)

#endif /* CONFIG_MMU */

extern unsigned long __copy_tofrom_user(void __user *to,
		const void __user *from, unsigned long size);

#endif  /* __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* _ASM_MICROBLAZE_UACCESS_H */
