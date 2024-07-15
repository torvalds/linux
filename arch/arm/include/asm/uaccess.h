/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/uaccess.h
 */
#ifndef _ASMARM_UACCESS_H
#define _ASMARM_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/page.h>
#include <asm/domain.h>
#include <asm/unaligned.h>
#include <asm/unified.h>
#include <asm/compiler.h>

#include <asm/extable.h>

/*
 * These two functions allow hooking accesses to userspace to increase
 * system integrity by ensuring that the kernel can not inadvertantly
 * perform such accesses (eg, via list poison values) which could then
 * be exploited for priviledge escalation.
 */
static __always_inline unsigned int uaccess_save_and_enable(void)
{
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	unsigned int old_domain = get_domain();

	/* Set the current domain access to permit user accesses */
	set_domain((old_domain & ~domain_mask(DOMAIN_USER)) |
		   domain_val(DOMAIN_USER, DOMAIN_CLIENT));

	return old_domain;
#else
	return 0;
#endif
}

static __always_inline void uaccess_restore(unsigned int flags)
{
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	/* Restore the user access mask */
	set_domain(flags);
#endif
}

/*
 * These two are intentionally not defined anywhere - if the kernel
 * code generates any references to them, that's a bug.
 */
extern int __get_user_bad(void);
extern int __put_user_bad(void);

#ifdef CONFIG_MMU

/*
 * This is a type: either unsigned long, if the argument fits into
 * that type, or otherwise unsigned long long.
 */
#define __inttype(x) \
	__typeof__(__builtin_choose_expr(sizeof(x) > sizeof(0UL), 0ULL, 0UL))

/*
 * Sanitise a uaccess pointer such that it becomes NULL if addr+size
 * is above the current addr_limit.
 */
#define uaccess_mask_range_ptr(ptr, size)			\
	((__typeof__(ptr))__uaccess_mask_range_ptr(ptr, size))
static inline void __user *__uaccess_mask_range_ptr(const void __user *ptr,
						    size_t size)
{
	void __user *safe_ptr = (void __user *)ptr;
	unsigned long tmp;

	asm volatile(
	"	.syntax unified\n"
	"	sub	%1, %3, #1\n"
	"	subs	%1, %1, %0\n"
	"	addhs	%1, %1, #1\n"
	"	subshs	%1, %1, %2\n"
	"	movlo	%0, #0\n"
	: "+r" (safe_ptr), "=&r" (tmp)
	: "r" (size), "r" (TASK_SIZE)
	: "cc");

	csdb();
	return safe_ptr;
}

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
 */
extern int __get_user_1(void *);
extern int __get_user_2(void *);
extern int __get_user_4(void *);
extern int __get_user_32t_8(void *);
extern int __get_user_8(void *);
extern int __get_user_64t_1(void *);
extern int __get_user_64t_2(void *);
extern int __get_user_64t_4(void *);

#define __get_user_x(__r2, __p, __e, __l, __s)				\
	   __asm__ __volatile__ (					\
		__asmeq("%0", "r0") __asmeq("%1", "r2")			\
		__asmeq("%3", "r1")					\
		"bl	__get_user_" #__s				\
		: "=&r" (__e), "=r" (__r2)				\
		: "0" (__p), "r" (__l)					\
		: "ip", "lr", "cc")

/* narrowing a double-word get into a single 32bit word register: */
#ifdef __ARMEB__
#define __get_user_x_32t(__r2, __p, __e, __l, __s)			\
	__get_user_x(__r2, __p, __e, __l, 32t_8)
#else
#define __get_user_x_32t __get_user_x
#endif

/*
 * storing result into proper least significant word of 64bit target var,
 * different only for big endian case where 64 bit __r2 lsw is r3:
 */
#ifdef __ARMEB__
#define __get_user_x_64t(__r2, __p, __e, __l, __s)		        \
	   __asm__ __volatile__ (					\
		__asmeq("%0", "r0") __asmeq("%1", "r2")			\
		__asmeq("%3", "r1")					\
		"bl	__get_user_64t_" #__s				\
		: "=&r" (__e), "=r" (__r2)				\
		: "0" (__p), "r" (__l)					\
		: "ip", "lr", "cc")
#else
#define __get_user_x_64t __get_user_x
#endif


#define __get_user_check(x, p)						\
	({								\
		unsigned long __limit = TASK_SIZE - 1; \
		register typeof(*(p)) __user *__p asm("r0") = (p);	\
		register __inttype(x) __r2 asm("r2");			\
		register unsigned long __l asm("r1") = __limit;		\
		register int __e asm("r0");				\
		unsigned int __ua_flags = uaccess_save_and_enable();	\
		int __tmp_e;						\
		switch (sizeof(*(__p))) {				\
		case 1:							\
			if (sizeof((x)) >= 8)				\
				__get_user_x_64t(__r2, __p, __e, __l, 1); \
			else						\
				__get_user_x(__r2, __p, __e, __l, 1);	\
			break;						\
		case 2:							\
			if (sizeof((x)) >= 8)				\
				__get_user_x_64t(__r2, __p, __e, __l, 2); \
			else						\
				__get_user_x(__r2, __p, __e, __l, 2);	\
			break;						\
		case 4:							\
			if (sizeof((x)) >= 8)				\
				__get_user_x_64t(__r2, __p, __e, __l, 4); \
			else						\
				__get_user_x(__r2, __p, __e, __l, 4);	\
			break;						\
		case 8:							\
			if (sizeof((x)) < 8)				\
				__get_user_x_32t(__r2, __p, __e, __l, 4); \
			else						\
				__get_user_x(__r2, __p, __e, __l, 8);	\
			break;						\
		default: __e = __get_user_bad(); break;			\
		}							\
		__tmp_e = __e;						\
		uaccess_restore(__ua_flags);				\
		x = (typeof(*(p))) __r2;				\
		__tmp_e;						\
	})

#define get_user(x, p)							\
	({								\
		might_fault();						\
		__get_user_check(x, p);					\
	 })

extern int __put_user_1(void *, unsigned int);
extern int __put_user_2(void *, unsigned int);
extern int __put_user_4(void *, unsigned int);
extern int __put_user_8(void *, unsigned long long);

#define __put_user_check(__pu_val, __ptr, __err, __s)			\
	({								\
		unsigned long __limit = TASK_SIZE - 1; \
		register typeof(__pu_val) __r2 asm("r2") = __pu_val;	\
		register const void __user *__p asm("r0") = __ptr;	\
		register unsigned long __l asm("r1") = __limit;		\
		register int __e asm("r0");				\
		__asm__ __volatile__ (					\
			__asmeq("%0", "r0") __asmeq("%2", "r2")		\
			__asmeq("%3", "r1")				\
			"bl	__put_user_" #__s			\
			: "=&r" (__e)					\
			: "0" (__p), "r" (__r2), "r" (__l)		\
			: "ip", "lr", "cc");				\
		__err = __e;						\
	})

#else /* CONFIG_MMU */

#define get_user(x, p)	__get_user(x, p)
#define __put_user_check __put_user_nocheck

#endif /* CONFIG_MMU */

#include <asm-generic/access_ok.h>

#ifdef CONFIG_CPU_SPECTRE
/*
 * When mitigating Spectre variant 1, it is not worth fixing the non-
 * verifying accessors, because we need to add verification of the
 * address space there.  Force these to use the standard get_user()
 * version instead.
 */
#define __get_user(x, ptr) get_user(x, ptr)
#else

/*
 * The "__xxx" versions of the user access functions do not verify the
 * address space - it must have been done previously with a separate
 * "access_ok()" call.
 *
 * The "xxx_error" versions set the third argument to EFAULT if an
 * error occurs, and leave it unchanged on success.  Note that these
 * versions are void (ie, don't return a value as such).
 */
#define __get_user(x, ptr)						\
({									\
	long __gu_err = 0;						\
	__get_user_err((x), (ptr), __gu_err, TUSER());			\
	__gu_err;							\
})

#define __get_user_err(x, ptr, err, __t)				\
do {									\
	unsigned long __gu_addr = (unsigned long)(ptr);			\
	unsigned long __gu_val;						\
	unsigned int __ua_flags;					\
	__chk_user_ptr(ptr);						\
	might_fault();							\
	__ua_flags = uaccess_save_and_enable();				\
	switch (sizeof(*(ptr))) {					\
	case 1:	__get_user_asm_byte(__gu_val, __gu_addr, err, __t); break;	\
	case 2:	__get_user_asm_half(__gu_val, __gu_addr, err, __t); break;	\
	case 4:	__get_user_asm_word(__gu_val, __gu_addr, err, __t); break;	\
	default: (__gu_val) = __get_user_bad();				\
	}								\
	uaccess_restore(__ua_flags);					\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
} while (0)
#endif

#define __get_user_asm(x, addr, err, instr)			\
	__asm__ __volatile__(					\
	"1:	" instr " %1, [%2], #0\n"			\
	"2:\n"							\
	"	.pushsection .text.fixup,\"ax\"\n"		\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	mov	%1, #0\n"				\
	"	b	2b\n"					\
	"	.popsection\n"					\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.popsection"					\
	: "+r" (err), "=&r" (x)					\
	: "r" (addr), "i" (-EFAULT)				\
	: "cc")

#define __get_user_asm_byte(x, addr, err, __t)			\
	__get_user_asm(x, addr, err, "ldrb" __t)

#if __LINUX_ARM_ARCH__ >= 6

#define __get_user_asm_half(x, addr, err, __t)			\
	__get_user_asm(x, addr, err, "ldrh" __t)

#else

#ifndef __ARMEB__
#define __get_user_asm_half(x, __gu_addr, err, __t)		\
({								\
	unsigned long __b1, __b2;				\
	__get_user_asm_byte(__b1, __gu_addr, err, __t);		\
	__get_user_asm_byte(__b2, __gu_addr + 1, err, __t);	\
	(x) = __b1 | (__b2 << 8);				\
})
#else
#define __get_user_asm_half(x, __gu_addr, err, __t)		\
({								\
	unsigned long __b1, __b2;				\
	__get_user_asm_byte(__b1, __gu_addr, err, __t);		\
	__get_user_asm_byte(__b2, __gu_addr + 1, err, __t);	\
	(x) = (__b1 << 8) | __b2;				\
})
#endif

#endif /* __LINUX_ARM_ARCH__ >= 6 */

#define __get_user_asm_word(x, addr, err, __t)			\
	__get_user_asm(x, addr, err, "ldr" __t)

#define __put_user_switch(x, ptr, __err, __fn)				\
	do {								\
		const __typeof__(*(ptr)) __user *__pu_ptr = (ptr);	\
		__typeof__(*(ptr)) __pu_val = (x);			\
		unsigned int __ua_flags;				\
		might_fault();						\
		__ua_flags = uaccess_save_and_enable();			\
		switch (sizeof(*(ptr))) {				\
		case 1: __fn(__pu_val, __pu_ptr, __err, 1); break;	\
		case 2:	__fn(__pu_val, __pu_ptr, __err, 2); break;	\
		case 4:	__fn(__pu_val, __pu_ptr, __err, 4); break;	\
		case 8:	__fn(__pu_val, __pu_ptr, __err, 8); break;	\
		default: __err = __put_user_bad(); break;		\
		}							\
		uaccess_restore(__ua_flags);				\
	} while (0)

#define put_user(x, ptr)						\
({									\
	int __pu_err = 0;						\
	__put_user_switch((x), (ptr), __pu_err, __put_user_check);	\
	__pu_err;							\
})

#ifdef CONFIG_CPU_SPECTRE
/*
 * When mitigating Spectre variant 1.1, all accessors need to include
 * verification of the address space.
 */
#define __put_user(x, ptr) put_user(x, ptr)

#else
#define __put_user(x, ptr)						\
({									\
	long __pu_err = 0;						\
	__put_user_switch((x), (ptr), __pu_err, __put_user_nocheck);	\
	__pu_err;							\
})

#define __put_user_nocheck(x, __pu_ptr, __err, __size)			\
	do {								\
		unsigned long __pu_addr = (unsigned long)__pu_ptr;	\
		__put_user_nocheck_##__size(x, __pu_addr, __err, TUSER());\
	} while (0)

#define __put_user_nocheck_1 __put_user_asm_byte
#define __put_user_nocheck_2 __put_user_asm_half
#define __put_user_nocheck_4 __put_user_asm_word
#define __put_user_nocheck_8 __put_user_asm_dword

#endif /* !CONFIG_CPU_SPECTRE */

#define __put_user_asm(x, __pu_addr, err, instr)		\
	__asm__ __volatile__(					\
	"1:	" instr " %1, [%2], #0\n"		\
	"2:\n"							\
	"	.pushsection .text.fixup,\"ax\"\n"		\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	b	2b\n"					\
	"	.popsection\n"					\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.popsection"					\
	: "+r" (err)						\
	: "r" (x), "r" (__pu_addr), "i" (-EFAULT)		\
	: "cc")

#define __put_user_asm_byte(x, __pu_addr, err, __t)		\
	__put_user_asm(x, __pu_addr, err, "strb" __t)

#if __LINUX_ARM_ARCH__ >= 6

#define __put_user_asm_half(x, __pu_addr, err, __t)		\
	__put_user_asm(x, __pu_addr, err, "strh" __t)

#else

#ifndef __ARMEB__
#define __put_user_asm_half(x, __pu_addr, err, __t)		\
({								\
	unsigned long __temp = (__force unsigned long)(x);	\
	__put_user_asm_byte(__temp, __pu_addr, err, __t);	\
	__put_user_asm_byte(__temp >> 8, __pu_addr + 1, err, __t);\
})
#else
#define __put_user_asm_half(x, __pu_addr, err, __t)		\
({								\
	unsigned long __temp = (__force unsigned long)(x);	\
	__put_user_asm_byte(__temp >> 8, __pu_addr, err, __t);	\
	__put_user_asm_byte(__temp, __pu_addr + 1, err, __t);	\
})
#endif

#endif /* __LINUX_ARM_ARCH__ >= 6 */

#define __put_user_asm_word(x, __pu_addr, err, __t)		\
	__put_user_asm(x, __pu_addr, err, "str" __t)

#ifndef __ARMEB__
#define	__reg_oper0	"%R2"
#define	__reg_oper1	"%Q2"
#else
#define	__reg_oper0	"%Q2"
#define	__reg_oper1	"%R2"
#endif

#define __put_user_asm_dword(x, __pu_addr, err, __t)		\
	__asm__ __volatile__(					\
 ARM(	"1:	str" __t "	" __reg_oper1 ", [%1], #4\n"  ) \
 ARM(	"2:	str" __t "	" __reg_oper0 ", [%1]\n"      ) \
 THUMB(	"1:	str" __t "	" __reg_oper1 ", [%1]\n"      ) \
 THUMB(	"2:	str" __t "	" __reg_oper0 ", [%1, #4]\n"  ) \
	"3:\n"							\
	"	.pushsection .text.fixup,\"ax\"\n"		\
	"	.align	2\n"					\
	"4:	mov	%0, %3\n"				\
	"	b	3b\n"					\
	"	.popsection\n"					\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 4b\n"				\
	"	.long	2b, 4b\n"				\
	"	.popsection"					\
	: "+r" (err), "+r" (__pu_addr)				\
	: "r" (x), "i" (-EFAULT)				\
	: "cc")

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	const type *__pk_ptr = (src);					\
	unsigned long __src = (unsigned long)(__pk_ptr);		\
	type __val;							\
	int __err = 0;							\
	switch (sizeof(type)) {						\
	case 1:	__get_user_asm_byte(__val, __src, __err, ""); break;	\
	case 2: __get_user_asm_half(__val, __src, __err, ""); break;	\
	case 4: __get_user_asm_word(__val, __src, __err, ""); break;	\
	case 8: {							\
		u32 *__v32 = (u32*)&__val;				\
		__get_user_asm_word(__v32[0], __src, __err, "");	\
		if (__err)						\
			break;						\
		__get_user_asm_word(__v32[1], __src+4, __err, "");	\
		break;							\
	}								\
	default: __err = __get_user_bad(); break;			\
	}								\
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))		\
		put_unaligned(__val, (type *)(dst));			\
	else								\
		*(type *)(dst) = __val; /* aligned by caller */		\
	if (__err)							\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	const type *__pk_ptr = (dst);					\
	unsigned long __dst = (unsigned long)__pk_ptr;			\
	int __err = 0;							\
	type __val = IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)	\
		     ? get_unaligned((type *)(src))			\
		     : *(type *)(src);	/* aligned by caller */		\
	switch (sizeof(type)) {						\
	case 1: __put_user_asm_byte(__val, __dst, __err, ""); break;	\
	case 2:	__put_user_asm_half(__val, __dst, __err, ""); break;	\
	case 4:	__put_user_asm_word(__val, __dst, __err, ""); break;	\
	case 8:	__put_user_asm_dword(__val, __dst, __err, ""); break;	\
	default: __err = __put_user_bad(); break;			\
	}								\
	if (__err)							\
		goto err_label;						\
} while (0)

#ifdef CONFIG_MMU
extern unsigned long __must_check
arm_copy_from_user(void *to, const void __user *from, unsigned long n);

static inline unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned int __ua_flags;

	__ua_flags = uaccess_save_and_enable();
	n = arm_copy_from_user(to, from, n);
	uaccess_restore(__ua_flags);
	return n;
}

extern unsigned long __must_check
arm_copy_to_user(void __user *to, const void *from, unsigned long n);
extern unsigned long __must_check
__copy_to_user_std(void __user *to, const void *from, unsigned long n);

static inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
#ifndef CONFIG_UACCESS_WITH_MEMCPY
	unsigned int __ua_flags;
	__ua_flags = uaccess_save_and_enable();
	n = arm_copy_to_user(to, from, n);
	uaccess_restore(__ua_flags);
	return n;
#else
	return arm_copy_to_user(to, from, n);
#endif
}

extern unsigned long __must_check
arm_clear_user(void __user *addr, unsigned long n);
extern unsigned long __must_check
__clear_user_std(void __user *addr, unsigned long n);

static inline unsigned long __must_check
__clear_user(void __user *addr, unsigned long n)
{
	unsigned int __ua_flags = uaccess_save_and_enable();
	n = arm_clear_user(addr, n);
	uaccess_restore(__ua_flags);
	return n;
}

#else
static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, (const void __force *)from, n);
	return 0;
}
static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy((void __force *)to, from, n);
	return 0;
}
#define __clear_user(addr, n)		(memset((void __force *)addr, 0, n), 0)
#endif
#define INLINE_COPY_TO_USER
#define INLINE_COPY_FROM_USER

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	if (access_ok(to, n))
		n = __clear_user(to, n);
	return n;
}

/* These are from lib/ code, and use __get_user() and friends */
extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

#endif /* _ASMARM_UACCESS_H */
