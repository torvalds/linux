/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_UACCESS_H
#define _ASM_MICROBLAZE_UACCESS_H

#include <linux/kernel.h>

#include <asm/mmu.h>
#include <asm/page.h>
#include <linux/pgtable.h>
#include <asm/extable.h>
#include <linux/string.h>

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

# define get_fs()	(current_thread_info()->addr_limit)
# define set_fs(val)	(current_thread_info()->addr_limit = (val))

# define uaccess_kernel()	(get_fs().seg == KERNEL_DS.seg)

#ifndef CONFIG_MMU

/* Check against bounds of physical memory */
static inline int ___range_ok(unsigned long addr, unsigned long size)
{
	return ((addr < memory_start) ||
		((addr + size - 1) > (memory_start + memory_size - 1)));
}

#define __range_ok(addr, size) \
		___range_ok((unsigned long)(addr), (unsigned long)(size))

#define access_ok(addr, size) (__range_ok((addr), (size)) == 0)

#else

static inline int access_ok(const void __user *addr, unsigned long size)
{
	if (!size)
		goto ok;

	if ((get_fs().seg < ((unsigned long)addr)) ||
			(get_fs().seg < ((unsigned long)addr + size - 1))) {
		pr_devel("ACCESS fail at 0x%08x (size 0x%x), seg 0x%08x\n",
			(__force u32)addr, (u32)size,
			(u32)get_fs().seg);
		return 0;
	}
ok:
	pr_devel("ACCESS OK at 0x%08x (size 0x%x), seg 0x%08x\n",
			(__force u32)addr, (u32)size,
			(u32)get_fs().seg);
	return 1;
}
#endif

#ifdef CONFIG_MMU
# define __FIXUP_SECTION	".section .fixup,\"ax\"\n"
# define __EX_TABLE_SECTION	".section __ex_table,\"a\"\n"
#else
# define __FIXUP_SECTION	".section .discard,\"ax\"\n"
# define __EX_TABLE_SECTION	".section .discard,\"ax\"\n"
#endif

extern unsigned long __copy_tofrom_user(void __user *to,
		const void __user *from, unsigned long size);

/* Return: number of not copied bytes, i.e. 0 if OK or non-zero if fail. */
static inline unsigned long __must_check __clear_user(void __user *to,
							unsigned long n)
{
	/* normal memset with two words to __ex_table */
	__asm__ __volatile__ (				\
			"1:	sb	r0, %1, r0;"	\
			"	addik	%0, %0, -1;"	\
			"	bneid	%0, 1b;"	\
			"	addik	%1, %1, 1;"	\
			"2:			"	\
			__EX_TABLE_SECTION		\
			".word	1b,2b;"			\
			".previous;"			\
		: "=r"(n), "=r"(to)			\
		: "0"(n), "1"(to)
	);
	return n;
}

static inline unsigned long __must_check clear_user(void __user *to,
							unsigned long n)
{
	might_fault();
	if (unlikely(!access_ok(to, n)))
		return n;

	return __clear_user(to, n);
}

/* put_user and get_user macros */
extern long __user_bad(void);

#define __get_user_asm(insn, __gu_ptr, __gu_val, __gu_err)	\
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
		: "=&r"(__gu_err), "=r"(__gu_val)		\
		: "r"(__gu_ptr), "i"(-EFAULT)			\
	);							\
})

/**
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
#define get_user(x, ptr) ({				\
	const typeof(*(ptr)) __user *__gu_ptr = (ptr);	\
	access_ok(__gu_ptr, sizeof(*__gu_ptr)) ?	\
		__get_user(x, __gu_ptr) : -EFAULT;	\
})

#define __get_user(x, ptr)						\
({									\
	long __gu_err;							\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm("lbu", (ptr), x, __gu_err);		\
		break;							\
	case 2:								\
		__get_user_asm("lhu", (ptr), x, __gu_err);		\
		break;							\
	case 4:								\
		__get_user_asm("lw", (ptr), x, __gu_err);		\
		break;							\
	case 8: {							\
		__u64 __x = 0;						\
		__gu_err = raw_copy_from_user(&__x, ptr, 8) ?		\
							-EFAULT : 0;	\
		(x) = (typeof(x))(typeof((x) - (x)))__x;		\
		break;							\
	}								\
	default:							\
		/* __gu_val = 0; __gu_err = -EINVAL;*/ __gu_err = __user_bad();\
	}								\
	__gu_err;							\
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
			"3:			"		\
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

/**
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
#define put_user(x, ptr)						\
	__put_user_check((x), (ptr), sizeof(*(ptr)))

#define __put_user_check(x, ptr, size)					\
({									\
	typeof(*(ptr)) volatile __pu_val = x;				\
	typeof(*(ptr)) __user *__pu_addr = (ptr);			\
	int __pu_err = 0;						\
									\
	if (access_ok(__pu_addr, size)) {			\
		switch (size) {						\
		case 1:							\
			__put_user_asm("sb", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 2:							\
			__put_user_asm("sh", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 4:							\
			__put_user_asm("sw", __pu_addr, __pu_val,	\
				       __pu_err);			\
			break;						\
		case 8:							\
			__put_user_asm_8(__pu_addr, __pu_val, __pu_err);\
			break;						\
		default:						\
			__pu_err = __user_bad();			\
			break;						\
		}							\
	} else {							\
		__pu_err = -EFAULT;					\
	}								\
	__pu_err;							\
})

#define __put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) volatile __gu_val = (x);			\
	long __gu_err = 0;						\
	switch (sizeof(__gu_val)) {					\
	case 1:								\
		__put_user_asm("sb", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 2:								\
		__put_user_asm("sh", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 4:								\
		__put_user_asm("sw", (ptr), __gu_val, __gu_err);	\
		break;							\
	case 8:								\
		__put_user_asm_8((ptr), __gu_val, __gu_err);		\
		break;							\
	default:							\
		/*__gu_err = -EINVAL;*/	__gu_err = __user_bad();	\
	}								\
	__gu_err;							\
})

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_tofrom_user((__force void __user *)to, from, n);
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_tofrom_user(to, (__force const void __user *)from, n);
}
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

/*
 * Copy a null terminated string from userspace.
 */
extern int __strncpy_user(char *to, const char __user *from, int len);

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	if (!access_ok(src, 1))
		return -EFAULT;
	return __strncpy_user(dst, src, count);
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
extern int __strnlen_user(const char __user *sstr, int len);

static inline long strnlen_user(const char __user *src, long n)
{
	if (!access_ok(src, 1))
		return 0;
	return __strnlen_user(src, n);
}

#endif /* _ASM_MICROBLAZE_UACCESS_H */
