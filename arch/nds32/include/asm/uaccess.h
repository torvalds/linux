/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMANDES_UACCESS_H
#define _ASMANDES_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>
#include <asm/errno.h>
#include <asm/memory.h>
#include <asm/types.h>

#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

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
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

#define KERNEL_DS 	((mm_segment_t) { ~0UL })
#define USER_DS		((mm_segment_t) {TASK_SIZE - 1})

#define get_fs()	(current_thread_info()->addr_limit)
#define user_addr_max	get_fs

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}

#define uaccess_kernel()	(get_fs() == KERNEL_DS)

#define __range_ok(addr, size) (size <= get_fs() && addr <= (get_fs() -size))

#define access_ok(addr, size)	\
	__range_ok((unsigned long)addr, (unsigned long)size)
/*
 * Single-value transfer routines.  They automatically use the right
 * size if we just have the right pointer type.  Note that the functions
 * which read from user space (*get_*) need to take care not to leak
 * kernel data even if the calling code is buggy and fails to check
 * the return value.  This means zeroing out the destination variable
 * or buffer on error.  Normally this is done out of line by the
 * fixup code, but there are a few places where it intrudes on the
 * main code path.  When we only write to user space, there is no
 * problem.
 *
 * The "__xxx" versions of the user access functions do not verify the
 * address space - it must have been done previously with a separate
 * "access_ok()" call.
 *
 * The "xxx_error" versions set the third argument to EFAULT if an
 * error occurs, and leave it unchanged on success.  Note that these
 * versions are void (ie, don't return a value as such).
 */

#define get_user	__get_user					\

#define __get_user(x, ptr)						\
({									\
	long __gu_err = 0;						\
	__get_user_check((x), (ptr), __gu_err);				\
	__gu_err;							\
})

#define __get_user_error(x, ptr, err)					\
({									\
	__get_user_check((x), (ptr), (err));				\
	(void)0;							\
})

#define __get_user_check(x, ptr, err)					\
({									\
	const __typeof__(*(ptr)) __user *__p = (ptr);			\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {		\
		__get_user_err((x), __p, (err));			\
	} else {							\
		(x) = 0; (err) = -EFAULT;				\
	}								\
})

#define __get_user_err(x, ptr, err)					\
do {									\
	unsigned long __gu_val;						\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm("lbi", __gu_val, (ptr), (err));		\
		break;							\
	case 2:								\
		__get_user_asm("lhi", __gu_val, (ptr), (err));		\
		break;							\
	case 4:								\
		__get_user_asm("lwi", __gu_val, (ptr), (err));		\
		break;							\
	case 8:								\
		__get_user_asm_dword(__gu_val, (ptr), (err));		\
		break;							\
	default:							\
		BUILD_BUG(); 						\
		break;							\
	}								\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
} while (0)

#define __get_user_asm(inst, x, addr, err)				\
	__asm__ __volatile__ (						\
		"1:	"inst"	%1,[%2]\n"				\
		"2:\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.align	2\n"					\
		"3:	move %0, %3\n"					\
		"	move %1, #0\n"					\
		"	b	2b\n"					\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	3\n"					\
		"	.long	1b, 3b\n"				\
		"	.previous"					\
		: "+r" (err), "=&r" (x)					\
		: "r" (addr), "i" (-EFAULT)				\
		: "cc")

#ifdef __NDS32_EB__
#define __gu_reg_oper0 "%H1"
#define __gu_reg_oper1 "%L1"
#else
#define __gu_reg_oper0 "%L1"
#define __gu_reg_oper1 "%H1"
#endif

#define __get_user_asm_dword(x, addr, err) 				\
	__asm__ __volatile__ (						\
		"\n1:\tlwi " __gu_reg_oper0 ",[%2]\n"			\
		"\n2:\tlwi " __gu_reg_oper1 ",[%2+4]\n"			\
		"3:\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.align	2\n"					\
		"4:	move	%0, %3\n"				\
		"	b	3b\n"					\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	3\n"					\
		"	.long	1b, 4b\n"				\
		"	.long	2b, 4b\n"				\
		"	.previous"					\
		: "+r"(err), "=&r"(x)					\
		: "r"(addr), "i"(-EFAULT)				\
		: "cc")

#define put_user	__put_user					\

#define __put_user(x, ptr)						\
({									\
	long __pu_err = 0;						\
	__put_user_err((x), (ptr), __pu_err);				\
	__pu_err;							\
})

#define __put_user_error(x, ptr, err)					\
({									\
	__put_user_err((x), (ptr), (err));				\
	(void)0;							\
})

#define __put_user_check(x, ptr, err)					\
({									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {		\
		__put_user_err((x), __p, (err));			\
	} else	{							\
		(err) = -EFAULT;					\
	}								\
})

#define __put_user_err(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __pu_val = (x);				\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__put_user_asm("sbi", __pu_val, (ptr), (err));		\
		break;							\
	case 2: 							\
		__put_user_asm("shi", __pu_val, (ptr), (err));		\
		break;							\
	case 4: 							\
		__put_user_asm("swi", __pu_val, (ptr), (err));		\
		break;							\
	case 8:								\
		__put_user_asm_dword(__pu_val, (ptr), (err));		\
		break;							\
	default:							\
		BUILD_BUG(); 						\
		break;							\
	}								\
} while (0)

#define __put_user_asm(inst, x, addr, err)				\
	__asm__ __volatile__ (						\
		"1:	"inst"	%1,[%2]\n"				\
		"2:\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.align	2\n"					\
		"3:	move	%0, %3\n"				\
		"	b	2b\n"					\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	3\n"					\
		"	.long	1b, 3b\n"				\
		"	.previous"					\
		: "+r" (err)						\
		: "r" (x), "r" (addr), "i" (-EFAULT)			\
		: "cc")

#ifdef __NDS32_EB__
#define __pu_reg_oper0 "%H2"
#define __pu_reg_oper1 "%L2"
#else
#define __pu_reg_oper0 "%L2"
#define __pu_reg_oper1 "%H2"
#endif

#define __put_user_asm_dword(x, addr, err) 				\
	__asm__ __volatile__ (						\
		"\n1:\tswi " __pu_reg_oper0 ",[%1]\n"			\
		"\n2:\tswi " __pu_reg_oper1 ",[%1+4]\n"			\
		"3:\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.align	2\n"					\
		"4:	move	%0, %3\n"				\
		"	b	3b\n"					\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	3\n"					\
		"	.long	1b, 4b\n"				\
		"	.long	2b, 4b\n"				\
		"	.previous"					\
		: "+r"(err)						\
		: "r"(addr), "r"(x), "i"(-EFAULT)			\
		: "cc")

extern unsigned long __arch_clear_user(void __user * addr, unsigned long n);
extern long strncpy_from_user(char *dest, const char __user * src, long count);
extern __must_check long strlen_user(const char __user * str);
extern __must_check long strnlen_user(const char __user * str, long n);
extern unsigned long __arch_copy_from_user(void *to, const void __user * from,
                                           unsigned long n);
extern unsigned long __arch_copy_to_user(void __user * to, const void *from,
                                         unsigned long n);

#define raw_copy_from_user __arch_copy_from_user
#define raw_copy_to_user __arch_copy_to_user

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER
static inline unsigned long clear_user(void __user * to, unsigned long n)
{
	if (access_ok(to, n))
		n = __arch_clear_user(to, n);
	return n;
}

static inline unsigned long __clear_user(void __user * to, unsigned long n)
{
	return __arch_clear_user(to, n);
}

#endif /* _ASMNDS32_UACCESS_H */
