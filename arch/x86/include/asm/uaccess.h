/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UACCESS_H
#define _ASM_X86_UACCESS_H
/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>
#include <asm/smap.h>
#include <asm/extable.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(-1UL)
#define USER_DS 	MAKE_MM_SEG(TASK_SIZE_MAX)

#define get_fs()	(current->thread.addr_limit)
static inline void set_fs(mm_segment_t fs)
{
	current->thread.addr_limit = fs;
	/* On user-mode return, check fs is correct */
	set_thread_flag(TIF_FSCHECK);
}

#define segment_eq(a, b)	((a).seg == (b).seg)
#define user_addr_max() (current->thread.addr_limit.seg)

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 */
static inline bool __chk_range_not_ok(unsigned long addr, unsigned long size, unsigned long limit)
{
	/*
	 * If we have used "sizeof()" for the size,
	 * we know it won't overflow the limit (but
	 * it might overflow the 'addr', so it's
	 * important to subtract the size from the
	 * limit, not add it to the address).
	 */
	if (__builtin_constant_p(size))
		return unlikely(addr > limit - size);

	/* Arbitrary sizes? Be careful about overflow */
	addr += size;
	if (unlikely(addr < size))
		return true;
	return unlikely(addr > limit);
}

#define __range_not_ok(addr, size, limit)				\
({									\
	__chk_user_ptr(addr);						\
	__chk_range_not_ok((unsigned long __force)(addr), size, limit); \
})

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
static inline bool pagefault_disabled(void);
# define WARN_ON_IN_IRQ()	\
	WARN_ON_ONCE(!in_task() && !pagefault_disabled())
#else
# define WARN_ON_IN_IRQ()
#endif

/**
 * access_ok - Checks if a user space pointer is valid
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 *
 * Return: true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 */
#define access_ok(addr, size)					\
({									\
	WARN_ON_IN_IRQ();						\
	likely(!__range_not_ok(addr, size, user_addr_max()));		\
})

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */

extern int __get_user_1(void);
extern int __get_user_2(void);
extern int __get_user_4(void);
extern int __get_user_8(void);
extern int __get_user_bad(void);

#define __uaccess_begin() stac()
#define __uaccess_end()   clac()
#define __uaccess_begin_nospec()	\
({					\
	stac();				\
	barrier_nospec();		\
})

/*
 * This is a type: either unsigned long, if the argument fits into
 * that type, or otherwise unsigned long long.
 */
#define __inttype(x) \
__typeof__(__builtin_choose_expr(sizeof(x) > sizeof(0UL), 0ULL, 0UL))

/**
 * get_user - Get a simple variable from user space.
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
 * Return: zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
/*
 * Careful: we have to cast the result to the type of the pointer
 * for sign reasons.
 *
 * The use of _ASM_DX as the register specifier is a bit of a
 * simplification, as gcc only cares about it as the starting point
 * and not size: for a 64-bit value it will use %ecx:%edx on 32 bits
 * (%ecx being the next register in gcc's x86 register sequence), and
 * %rdx on 64 bits.
 *
 * Clang/LLVM cares about the size of the register, but still wants
 * the base register for something that ends up being a pair.
 */
#define get_user(x, ptr)						\
({									\
	int __ret_gu;							\
	register __inttype(*(ptr)) __val_gu asm("%"_ASM_DX);		\
	__chk_user_ptr(ptr);						\
	might_fault();							\
	asm volatile("call __get_user_%P4"				\
		     : "=a" (__ret_gu), "=r" (__val_gu),		\
			ASM_CALL_CONSTRAINT				\
		     : "0" (ptr), "i" (sizeof(*(ptr))));		\
	(x) = (__force __typeof__(*(ptr))) __val_gu;			\
	__builtin_expect(__ret_gu, 0);					\
})

#define __put_user_x(size, x, ptr, __ret_pu)			\
	asm volatile("call __put_user_" #size : "=a" (__ret_pu)	\
		     : "0" ((typeof(*(ptr)))(x)), "c" (ptr) : "ebx")



#ifdef CONFIG_X86_32
#define __put_user_goto_u64(x, addr, label)			\
	asm_volatile_goto("\n"					\
		     "1:	movl %%eax,0(%1)\n"		\
		     "2:	movl %%edx,4(%1)\n"		\
		     _ASM_EXTABLE_UA(1b, %l2)			\
		     _ASM_EXTABLE_UA(2b, %l2)			\
		     : : "A" (x), "r" (addr)			\
		     : : label)

#define __put_user_asm_ex_u64(x, addr)					\
	asm volatile("\n"						\
		     "1:	movl %%eax,0(%1)\n"			\
		     "2:	movl %%edx,4(%1)\n"			\
		     "3:"						\
		     _ASM_EXTABLE_EX(1b, 2b)				\
		     _ASM_EXTABLE_EX(2b, 3b)				\
		     : : "A" (x), "r" (addr))

#define __put_user_x8(x, ptr, __ret_pu)				\
	asm volatile("call __put_user_8" : "=a" (__ret_pu)	\
		     : "A" ((typeof(*(ptr)))(x)), "c" (ptr) : "ebx")
#else
#define __put_user_goto_u64(x, ptr, label) \
	__put_user_goto(x, ptr, "q", "", "er", label)
#define __put_user_asm_ex_u64(x, addr)	\
	__put_user_asm_ex(x, addr, "q", "", "er")
#define __put_user_x8(x, ptr, __ret_pu) __put_user_x(8, x, ptr, __ret_pu)
#endif

extern void __put_user_bad(void);

/*
 * Strange magic calling convention: pointer in %ecx,
 * value in %eax(:%edx), return value in %eax. clobbers %rbx
 */
extern void __put_user_1(void);
extern void __put_user_2(void);
extern void __put_user_4(void);
extern void __put_user_8(void);

/**
 * put_user - Write a simple value into user space.
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
 * Return: zero on success, or -EFAULT on error.
 */
#define put_user(x, ptr)					\
({								\
	int __ret_pu;						\
	__typeof__(*(ptr)) __pu_val;				\
	__chk_user_ptr(ptr);					\
	might_fault();						\
	__pu_val = x;						\
	switch (sizeof(*(ptr))) {				\
	case 1:							\
		__put_user_x(1, __pu_val, ptr, __ret_pu);	\
		break;						\
	case 2:							\
		__put_user_x(2, __pu_val, ptr, __ret_pu);	\
		break;						\
	case 4:							\
		__put_user_x(4, __pu_val, ptr, __ret_pu);	\
		break;						\
	case 8:							\
		__put_user_x8(__pu_val, ptr, __ret_pu);		\
		break;						\
	default:						\
		__put_user_x(X, __pu_val, ptr, __ret_pu);	\
		break;						\
	}							\
	__builtin_expect(__ret_pu, 0);				\
})

#define __put_user_size(x, ptr, size, label)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__put_user_goto(x, ptr, "b", "b", "iq", label);	\
		break;							\
	case 2:								\
		__put_user_goto(x, ptr, "w", "w", "ir", label);		\
		break;							\
	case 4:								\
		__put_user_goto(x, ptr, "l", "k", "ir", label);		\
		break;							\
	case 8:								\
		__put_user_goto_u64(x, ptr, label);			\
		break;							\
	default:							\
		__put_user_bad();					\
	}								\
} while (0)

/*
 * This doesn't do __uaccess_begin/end - the exception handling
 * around it must do that.
 */
#define __put_user_size_ex(x, ptr, size)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__put_user_asm_ex(x, ptr, "b", "b", "iq");		\
		break;							\
	case 2:								\
		__put_user_asm_ex(x, ptr, "w", "w", "ir");		\
		break;							\
	case 4:								\
		__put_user_asm_ex(x, ptr, "l", "k", "ir");		\
		break;							\
	case 8:								\
		__put_user_asm_ex_u64((__typeof__(*ptr))(x), ptr);	\
		break;							\
	default:							\
		__put_user_bad();					\
	}								\
} while (0)

#ifdef CONFIG_X86_32
#define __get_user_asm_u64(x, ptr, retval, errret)			\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	asm volatile("\n"					\
		     "1:	movl %2,%%eax\n"			\
		     "2:	movl %3,%%edx\n"			\
		     "3:\n"				\
		     ".section .fixup,\"ax\"\n"				\
		     "4:	mov %4,%0\n"				\
		     "	xorl %%eax,%%eax\n"				\
		     "	xorl %%edx,%%edx\n"				\
		     "	jmp 3b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE_UA(1b, 4b)				\
		     _ASM_EXTABLE_UA(2b, 4b)				\
		     : "=r" (retval), "=&A"(x)				\
		     : "m" (__m(__ptr)), "m" __m(((u32 __user *)(__ptr)) + 1),	\
		       "i" (errret), "0" (retval));			\
})

#define __get_user_asm_ex_u64(x, ptr)			(x) = __get_user_bad()
#else
#define __get_user_asm_u64(x, ptr, retval, errret) \
	 __get_user_asm(x, ptr, retval, "q", "", "=r", errret)
#define __get_user_asm_ex_u64(x, ptr) \
	 __get_user_asm_ex(x, ptr, "q", "", "=r")
#endif

#define __get_user_size(x, ptr, size, retval, errret)			\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__get_user_asm(x, ptr, retval, "b", "b", "=q", errret);	\
		break;							\
	case 2:								\
		__get_user_asm(x, ptr, retval, "w", "w", "=r", errret);	\
		break;							\
	case 4:								\
		__get_user_asm(x, ptr, retval, "l", "k", "=r", errret);	\
		break;							\
	case 8:								\
		__get_user_asm_u64(x, ptr, retval, errret);		\
		break;							\
	default:							\
		(x) = __get_user_bad();					\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype, rtype, ltype, errret)	\
	asm volatile("\n"						\
		     "1:	mov"itype" %2,%"rtype"1\n"		\
		     "2:\n"						\
		     ".section .fixup,\"ax\"\n"				\
		     "3:	mov %3,%0\n"				\
		     "	xor"itype" %"rtype"1,%"rtype"1\n"		\
		     "	jmp 2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE_UA(1b, 3b)				\
		     : "=r" (err), ltype(x)				\
		     : "m" (__m(addr)), "i" (errret), "0" (err))

#define __get_user_asm_nozero(x, addr, err, itype, rtype, ltype, errret)	\
	asm volatile("\n"						\
		     "1:	mov"itype" %2,%"rtype"1\n"		\
		     "2:\n"						\
		     ".section .fixup,\"ax\"\n"				\
		     "3:	mov %3,%0\n"				\
		     "	jmp 2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE_UA(1b, 3b)				\
		     : "=r" (err), ltype(x)				\
		     : "m" (__m(addr)), "i" (errret), "0" (err))

/*
 * This doesn't do __uaccess_begin/end - the exception handling
 * around it must do that.
 */
#define __get_user_size_ex(x, ptr, size)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__get_user_asm_ex(x, ptr, "b", "b", "=q");		\
		break;							\
	case 2:								\
		__get_user_asm_ex(x, ptr, "w", "w", "=r");		\
		break;							\
	case 4:								\
		__get_user_asm_ex(x, ptr, "l", "k", "=r");		\
		break;							\
	case 8:								\
		__get_user_asm_ex_u64(x, ptr);				\
		break;							\
	default:							\
		(x) = __get_user_bad();					\
	}								\
} while (0)

#define __get_user_asm_ex(x, addr, itype, rtype, ltype)			\
	asm volatile("1:	mov"itype" %1,%"rtype"0\n"		\
		     "2:\n"						\
		     ".section .fixup,\"ax\"\n"				\
                     "3:xor"itype" %"rtype"0,%"rtype"0\n"		\
		     "  jmp 2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE_EX(1b, 3b)				\
		     : ltype(x) : "m" (__m(addr)))

#define __put_user_nocheck(x, ptr, size)			\
({								\
	__label__ __pu_label;					\
	int __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __pu_val = (x);			\
	__typeof__(ptr) __pu_ptr = (ptr);			\
	__typeof__(size) __pu_size = (size);			\
	__uaccess_begin();					\
	__put_user_size(__pu_val, __pu_ptr, __pu_size, __pu_label);	\
	__pu_err = 0;						\
__pu_label:							\
	__uaccess_end();					\
	__builtin_expect(__pu_err, 0);				\
})

#define __get_user_nocheck(x, ptr, size)				\
({									\
	int __gu_err;							\
	__inttype(*(ptr)) __gu_val;					\
	__uaccess_begin_nospec();					\
	__get_user_size(__gu_val, (ptr), (size), __gu_err, -EFAULT);	\
	__uaccess_end();						\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
	__builtin_expect(__gu_err, 0);					\
})

/* FIXME: this hack is definitely wrong -AK */
struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_goto(x, addr, itype, rtype, ltype, label)	\
	asm_volatile_goto("\n"						\
		"1:	mov"itype" %"rtype"0,%1\n"			\
		_ASM_EXTABLE_UA(1b, %l2)					\
		: : ltype(x), "m" (__m(addr))				\
		: : label)

#define __put_user_failed(x, addr, itype, rtype, ltype, errret)		\
	({	__label__ __puflab;					\
		int __pufret = errret;					\
		__put_user_goto(x,addr,itype,rtype,ltype,__puflab);	\
		__pufret = 0;						\
	__puflab: __pufret; })

#define __put_user_asm(x, addr, retval, itype, rtype, ltype, errret)	do {	\
	retval = __put_user_failed(x, addr, itype, rtype, ltype, errret);	\
} while (0)

#define __put_user_asm_ex(x, addr, itype, rtype, ltype)			\
	asm volatile("1:	mov"itype" %"rtype"0,%1\n"		\
		     "2:\n"						\
		     _ASM_EXTABLE_EX(1b, 2b)				\
		     : : ltype(x), "m" (__m(addr)))

/*
 * uaccess_try and catch
 */
#define uaccess_try	do {						\
	current->thread.uaccess_err = 0;				\
	__uaccess_begin();						\
	barrier();

#define uaccess_try_nospec do {						\
	current->thread.uaccess_err = 0;				\
	__uaccess_begin_nospec();					\

#define uaccess_catch(err)						\
	__uaccess_end();						\
	(err) |= (current->thread.uaccess_err ? -EFAULT : 0);		\
} while (0)

/**
 * __get_user - Get a simple variable from user space, with less checking.
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
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Return: zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */

#define __get_user(x, ptr)						\
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

/**
 * __put_user - Write a simple value into user space, with less checking.
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
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Return: zero on success, or -EFAULT on error.
 */

#define __put_user(x, ptr)						\
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

/*
 * {get|put}_user_try and catch
 *
 * get_user_try {
 *	get_user_ex(...);
 * } get_user_catch(err)
 */
#define get_user_try		uaccess_try_nospec
#define get_user_catch(err)	uaccess_catch(err)

#define get_user_ex(x, ptr)	do {					\
	unsigned long __gue_val;					\
	__get_user_size_ex((__gue_val), (ptr), (sizeof(*(ptr))));	\
	(x) = (__force __typeof__(*(ptr)))__gue_val;			\
} while (0)

#define put_user_try		uaccess_try
#define put_user_catch(err)	uaccess_catch(err)

#define put_user_ex(x, ptr)						\
	__put_user_size_ex((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

extern unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n);
extern __must_check long
strncpy_from_user(char *dst, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

unsigned long __must_check clear_user(void __user *mem, unsigned long len);
unsigned long __must_check __clear_user(void __user *mem, unsigned long len);

extern void __cmpxchg_wrong_size(void)
	__compiletime_error("Bad argument size for cmpxchg");

#define __user_atomic_cmpxchg_inatomic(uval, ptr, old, new, size)	\
({									\
	int __ret = 0;							\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	__uaccess_begin_nospec();					\
	switch (size) {							\
	case 1:								\
	{								\
		asm volatile("\n"					\
			"1:\t" LOCK_PREFIX "cmpxchgb %4, %2\n"		\
			"2:\n"						\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE_UA(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "q" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 2:								\
	{								\
		asm volatile("\n"					\
			"1:\t" LOCK_PREFIX "cmpxchgw %4, %2\n"		\
			"2:\n"						\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE_UA(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 4:								\
	{								\
		asm volatile("\n"					\
			"1:\t" LOCK_PREFIX "cmpxchgl %4, %2\n"		\
			"2:\n"						\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE_UA(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 8:								\
	{								\
		if (!IS_ENABLED(CONFIG_X86_64))				\
			__cmpxchg_wrong_size();				\
									\
		asm volatile("\n"					\
			"1:\t" LOCK_PREFIX "cmpxchgq %4, %2\n"		\
			"2:\n"						\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE_UA(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	default:							\
		__cmpxchg_wrong_size();					\
	}								\
	__uaccess_end();						\
	*(uval) = __old;						\
	__ret;								\
})

#define user_atomic_cmpxchg_inatomic(uval, ptr, old, new)		\
({									\
	access_ok((ptr), sizeof(*(ptr))) ?		\
		__user_atomic_cmpxchg_inatomic((uval), (ptr),		\
				(old), (new), sizeof(*(ptr))) :		\
		-EFAULT;						\
})

/*
 * movsl can be slow when source and dest are not both 8-byte aligned
 */
#ifdef CONFIG_X86_INTEL_USERCOPY
extern struct movsl_mask {
	int mask;
} ____cacheline_aligned_in_smp movsl_mask;
#endif

#define ARCH_HAS_NOCACHE_UACCESS 1

#ifdef CONFIG_X86_32
# include <asm/uaccess_32.h>
#else
# include <asm/uaccess_64.h>
#endif

/*
 * We rely on the nested NMI work to allow atomic faults from the NMI path; the
 * nested NMI paths are careful to preserve CR2.
 *
 * Caller must use pagefault_enable/disable, or run in interrupt context,
 * and also do a uaccess_ok() check
 */
#define __copy_from_user_nmi __copy_from_user_inatomic

/*
 * The "unsafe" user accesses aren't really "unsafe", but the naming
 * is a big fat warning: you have to not only do the access_ok()
 * checking before using them, but you have to surround them with the
 * user_access_begin/end() pair.
 */
static __must_check __always_inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr,len)))
		return 0;
	__uaccess_begin_nospec();
	return 1;
}
#define user_access_begin(a,b)	user_access_begin(a,b)
#define user_access_end()	__uaccess_end()

#define user_access_save()	smap_save()
#define user_access_restore(x)	smap_restore(x)

#define unsafe_put_user(x, ptr, label)	\
	__put_user_size((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)), label)

#define unsafe_get_user(x, ptr, err_label)					\
do {										\
	int __gu_err;								\
	__inttype(*(ptr)) __gu_val;						\
	__get_user_size(__gu_val, (ptr), sizeof(*(ptr)), __gu_err, -EFAULT);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
	if (unlikely(__gu_err)) goto err_label;					\
} while (0)

#endif /* _ASM_X86_UACCESS_H */

