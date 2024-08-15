/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

/*
 * User space memory access functions
 */

#include <linux/compiler.h>
#include <linux/string.h>
#include <asm/asi.h>
#include <asm/spitfire.h>

#include <asm/processor.h>
#include <asm-generic/access_ok.h>

/*
 * Sparc64 is segmented, though more like the M68K than the I386.
 * We use the secondary ASI to address user memory, which references a
 * completely different VM map, thus there is zero chance of the user
 * doing something queer and tricking us into poking kernel memory.
 */

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 */
static inline bool __chk_range_not_ok(unsigned long addr, unsigned long size, unsigned long limit)
{
	if (__builtin_constant_p(size))
		return addr > limit - size;

	addr += size;
	if (addr < size)
		return true;

	return addr > limit;
}

#define __range_not_ok(addr, size, limit)                               \
({                                                                      \
	__chk_user_ptr(addr);                                           \
	__chk_range_not_ok((unsigned long __force)(addr), size, limit); \
})

void __retl_efault(void);

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
	__put_user_nocheck((__typeof__(*(ptr)))(x), __pu_addr, sizeof(*(ptr)));\
})

#define get_user(x, ptr) ({ \
	unsigned long __gu_addr = (unsigned long)(ptr); \
	__chk_user_ptr(ptr); \
	__get_user_nocheck((x), __gu_addr, sizeof(*(ptr)), __typeof__(*(ptr)));\
})

#define __put_user(x, ptr) put_user(x, ptr)
#define __get_user(x, ptr) get_user(x, ptr)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct *)(x))

#define __put_kernel_nofault(dst, src, type, label)			\
do {									\
	type *addr = (type __force *)(dst);				\
	type data = *(type *)src;					\
	register int __pu_ret;						\
	switch (sizeof(type)) {						\
	case 1: __put_kernel_asm(data, b, addr, __pu_ret); break;	\
	case 2: __put_kernel_asm(data, h, addr, __pu_ret); break;	\
	case 4: __put_kernel_asm(data, w, addr, __pu_ret); break;	\
	case 8: __put_kernel_asm(data, x, addr, __pu_ret); break;	\
	default: __pu_ret = __put_user_bad(); break;			\
	}								\
	if (__pu_ret)							\
		goto label;						\
} while (0)

#define __put_kernel_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Put kernel asm, inline. */\n"			\
	"1:\t"	"st"#size " %1, [%2]\n\t"				\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"sethi	%%hi(2b), %0\n\t"				\
		"jmpl	%0 + %%lo(2b), %%g0\n\t"			\
		" mov	%3, %0\n\n\t"					\
		".previous\n\t"						\
		".section __ex_table,\"a\"\n\t"				\
		".align	4\n\t"						\
		".word	1b, 3b\n\t"					\
		".previous\n\n\t"					\
	       : "=r" (ret) : "r" (x), "r" (__m(addr)),			\
		 "i" (-EFAULT))

#define __put_user_nocheck(data, addr, size) ({			\
	register int __pu_ret;					\
	switch (size) {						\
	case 1: __put_user_asm(data, b, addr, __pu_ret); break;	\
	case 2: __put_user_asm(data, h, addr, __pu_ret); break;	\
	case 4: __put_user_asm(data, w, addr, __pu_ret); break;	\
	case 8: __put_user_asm(data, x, addr, __pu_ret); break;	\
	default: __pu_ret = __put_user_bad(); break;		\
	}							\
	__pu_ret;						\
})

#define __put_user_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Put user asm, inline. */\n"				\
	"1:\t"	"st"#size "a %1, [%2] %%asi\n\t"			\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"sethi	%%hi(2b), %0\n\t"				\
		"jmpl	%0 + %%lo(2b), %%g0\n\t"			\
		" mov	%3, %0\n\n\t"					\
		".previous\n\t"						\
		".section __ex_table,\"a\"\n\t"				\
		".align	4\n\t"						\
		".word	1b, 3b\n\t"					\
		".previous\n\n\t"					\
	       : "=r" (ret) : "r" (x), "r" (__m(addr)),			\
		 "i" (-EFAULT))

int __put_user_bad(void);

#define __get_kernel_nofault(dst, src, type, label)			     \
do {									     \
	type *addr = (type __force *)(src);		     		     \
	register int __gu_ret;						     \
	register unsigned long __gu_val;				     \
	switch (sizeof(type)) {						     \
		case 1: __get_kernel_asm(__gu_val, ub, addr, __gu_ret); break; \
		case 2: __get_kernel_asm(__gu_val, uh, addr, __gu_ret); break; \
		case 4: __get_kernel_asm(__gu_val, uw, addr, __gu_ret); break; \
		case 8: __get_kernel_asm(__gu_val, x, addr, __gu_ret); break;  \
		default:						     \
			__gu_val = 0;					     \
			__gu_ret = __get_user_bad();			     \
			break;						     \
	} 								     \
	if (__gu_ret)							     \
		goto label;						     \
	*(type *)dst = (__force type) __gu_val;				     \
} while (0)
#define __get_kernel_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Get kernel asm, inline. */\n"			\
	"1:\t"	"ld"#size " [%2], %1\n\t"				\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"sethi	%%hi(2b), %0\n\t"				\
		"clr	%1\n\t"						\
		"jmpl	%0 + %%lo(2b), %%g0\n\t"			\
		" mov	%3, %0\n\n\t"					\
		".previous\n\t"						\
		".section __ex_table,\"a\"\n\t"				\
		".align	4\n\t"						\
		".word	1b, 3b\n\n\t"					\
		".previous\n\t"						\
	       : "=r" (ret), "=r" (x) : "r" (__m(addr)),		\
		 "i" (-EFAULT))

#define __get_user_nocheck(data, addr, size, type) ({			     \
	register int __gu_ret;						     \
	register unsigned long __gu_val;				     \
	switch (size) {							     \
		case 1: __get_user_asm(__gu_val, ub, addr, __gu_ret); break; \
		case 2: __get_user_asm(__gu_val, uh, addr, __gu_ret); break; \
		case 4: __get_user_asm(__gu_val, uw, addr, __gu_ret); break; \
		case 8: __get_user_asm(__gu_val, x, addr, __gu_ret); break;  \
		default:						     \
			__gu_val = 0;					     \
			__gu_ret = __get_user_bad();			     \
			break;						     \
	} 								     \
	data = (__force type) __gu_val;					     \
	 __gu_ret;							     \
})

#define __get_user_asm(x, size, addr, ret)				\
__asm__ __volatile__(							\
		"/* Get user asm, inline. */\n"				\
	"1:\t"	"ld"#size "a [%2] %%asi, %1\n\t"			\
		"clr	%0\n"						\
	"2:\n\n\t"							\
		".section .fixup,#alloc,#execinstr\n\t"			\
		".align	4\n"						\
	"3:\n\t"							\
		"sethi	%%hi(2b), %0\n\t"				\
		"clr	%1\n\t"						\
		"jmpl	%0 + %%lo(2b), %%g0\n\t"			\
		" mov	%3, %0\n\n\t"					\
		".previous\n\t"						\
		".section __ex_table,\"a\"\n\t"				\
		".align	4\n\t"						\
		".word	1b, 3b\n\n\t"					\
		".previous\n\t"						\
	       : "=r" (ret), "=r" (x) : "r" (__m(addr)),		\
		 "i" (-EFAULT))

int __get_user_bad(void);

unsigned long __must_check raw_copy_from_user(void *to,
					     const void __user *from,
					     unsigned long size);

unsigned long __must_check raw_copy_to_user(void __user *to,
					   const void *from,
					   unsigned long size);
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

unsigned long __must_check raw_copy_in_user(void __user *to,
					   const void __user *from,
					   unsigned long size);

unsigned long __must_check __clear_user(void __user *, unsigned long);

#define clear_user __clear_user

__must_check long strnlen_user(const char __user *str, long n);

struct pt_regs;
unsigned long compute_effective_address(struct pt_regs *,
					unsigned int insn,
					unsigned int rd);

#endif /* _ASM_UACCESS_H */
