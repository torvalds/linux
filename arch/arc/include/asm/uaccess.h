/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: June 2010
 *    -__clear_user( ) called multiple times during elf load was byte loop
 *    converted to do as much word clear as possible.
 *
 * vineetg: Dec 2009
 *    -Hand crafted constant propagation for "constant" copy sizes
 *    -stock kernel shrunk by 33K at -O3
 *
 * vineetg: Sept 2009
 *    -Added option to (UN)inline copy_(to|from)_user to reduce code sz
 *    -kernel shrunk by 200K even at -O3 (gcc 4.2.1)
 *    -Enabled when doing -Os
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_UACCESS_H
#define _ASM_ARC_UACCESS_H

#include <linux/string.h>	/* for generic string functions */


#define __kernel_ok		(segment_eq(get_fs(), KERNEL_DS))

/*
 * Algorithmically, for __user_ok() we want do:
 * 	(start < TASK_SIZE) && (start+len < TASK_SIZE)
 * where TASK_SIZE could either be retrieved from thread_info->addr_limit or
 * emitted directly in code.
 *
 * This can however be rewritten as follows:
 *	(len <= TASK_SIZE) && (start+len < TASK_SIZE)
 *
 * Because it essentially checks if buffer end is within limit and @len is
 * non-ngeative, which implies that buffer start will be within limit too.
 *
 * The reason for rewriting being, for majority of cases, @len is generally
 * compile time constant, causing first sub-expression to be compile time
 * subsumed.
 *
 * The second part would generate weird large LIMMs e.g. (0x6000_0000 - 0x10),
 * so we check for TASK_SIZE using get_fs() since the addr_limit load from mem
 * would already have been done at this call site for __kernel_ok()
 *
 */
#define __user_ok(addr, sz)	(((sz) <= TASK_SIZE) && \
				 ((addr) <= (get_fs() - (sz))))
#define __access_ok(addr, sz)	(unlikely(__kernel_ok) || \
				 likely(__user_ok((addr), (sz))))

/*********** Single byte/hword/word copies ******************/

#define __get_user_fn(sz, u, k)					\
({								\
	long __ret = 0;	/* success by default */	\
	switch (sz) {						\
	case 1: __arc_get_user_one(*(k), u, "ldb", __ret); break;	\
	case 2: __arc_get_user_one(*(k), u, "ldw", __ret); break;	\
	case 4: __arc_get_user_one(*(k), u, "ld", __ret);  break;	\
	case 8: __arc_get_user_one_64(*(k), u, __ret);     break;	\
	}							\
	__ret;							\
})

/*
 * Returns 0 on success, -EFAULT if not.
 * @ret already contains 0 - given that errors will be less likely
 * (hence +r asm constraint below).
 * In case of error, fixup code will make it -EFAULT
 */
#define __arc_get_user_one(dst, src, op, ret)	\
	__asm__ __volatile__(                   \
	"1:	"op"    %1,[%2]\n"		\
	"2:	;nop\n"				\
	"	.section .fixup, \"ax\"\n"	\
	"	.align 4\n"			\
	"3:	# return -EFAULT\n"		\
	"	mov %0, %3\n"			\
	"	# zero out dst ptr\n"		\
	"	mov %1,  0\n"			\
	"	j   2b\n"			\
	"	.previous\n"			\
	"	.section __ex_table, \"a\"\n"	\
	"	.align 4\n"			\
	"	.word 1b,3b\n"			\
	"	.previous\n"			\
						\
	: "+r" (ret), "=r" (dst)		\
	: "r" (src), "ir" (-EFAULT))

#define __arc_get_user_one_64(dst, src, ret)	\
	__asm__ __volatile__(                   \
	"1:	ld   %1,[%2]\n"			\
	"4:	ld  %R1,[%2, 4]\n"		\
	"2:	;nop\n"				\
	"	.section .fixup, \"ax\"\n"	\
	"	.align 4\n"			\
	"3:	# return -EFAULT\n"		\
	"	mov %0, %3\n"			\
	"	# zero out dst ptr\n"		\
	"	mov %1,  0\n"			\
	"	mov %R1, 0\n"			\
	"	j   2b\n"			\
	"	.previous\n"			\
	"	.section __ex_table, \"a\"\n"	\
	"	.align 4\n"			\
	"	.word 1b,3b\n"			\
	"	.word 4b,3b\n"			\
	"	.previous\n"			\
						\
	: "+r" (ret), "=r" (dst)		\
	: "r" (src), "ir" (-EFAULT))

#define __put_user_fn(sz, u, k)					\
({								\
	long __ret = 0;	/* success by default */	\
	switch (sz) {						\
	case 1: __arc_put_user_one(*(k), u, "stb", __ret); break;	\
	case 2: __arc_put_user_one(*(k), u, "stw", __ret); break;	\
	case 4: __arc_put_user_one(*(k), u, "st", __ret);  break;	\
	case 8: __arc_put_user_one_64(*(k), u, __ret);     break;	\
	}							\
	__ret;							\
})

#define __arc_put_user_one(src, dst, op, ret)	\
	__asm__ __volatile__(                   \
	"1:	"op"    %1,[%2]\n"		\
	"2:	;nop\n"				\
	"	.section .fixup, \"ax\"\n"	\
	"	.align 4\n"			\
	"3:	mov %0, %3\n"			\
	"	j   2b\n"			\
	"	.previous\n"			\
	"	.section __ex_table, \"a\"\n"	\
	"	.align 4\n"			\
	"	.word 1b,3b\n"			\
	"	.previous\n"			\
						\
	: "+r" (ret)				\
	: "r" (src), "r" (dst), "ir" (-EFAULT))

#define __arc_put_user_one_64(src, dst, ret)	\
	__asm__ __volatile__(                   \
	"1:	st   %1,[%2]\n"			\
	"4:	st  %R1,[%2, 4]\n"		\
	"2:	;nop\n"				\
	"	.section .fixup, \"ax\"\n"	\
	"	.align 4\n"			\
	"3:	mov %0, %3\n"			\
	"	j   2b\n"			\
	"	.previous\n"			\
	"	.section __ex_table, \"a\"\n"	\
	"	.align 4\n"			\
	"	.word 1b,3b\n"			\
	"	.word 4b,3b\n"			\
	"	.previous\n"			\
						\
	: "+r" (ret)				\
	: "r" (src), "r" (dst), "ir" (-EFAULT))


static inline unsigned long
__arc_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	long res = 0;
	char val;
	unsigned long tmp1, tmp2, tmp3, tmp4;
	unsigned long orig_n = n;

	if (n == 0)
		return 0;

	/* unaligned */
	if (((unsigned long)to & 0x3) || ((unsigned long)from & 0x3)) {

		unsigned char tmp;

		__asm__ __volatile__ (
		"	mov.f   lp_count, %0		\n"
		"	lpnz 2f				\n"
		"1:	ldb.ab  %1, [%3, 1]		\n"
		"	stb.ab  %1, [%2, 1]		\n"
		"	sub     %0,%0,1			\n"
		"2:	;nop				\n"
		"	.section .fixup, \"ax\"		\n"
		"	.align 4			\n"
		"3:	j   2b				\n"
		"	.previous			\n"
		"	.section __ex_table, \"a\"	\n"
		"	.align 4			\n"
		"	.word   1b, 3b			\n"
		"	.previous			\n"

		: "+r" (n),
		/*
		 * Note as an '&' earlyclobber operand to make sure the
		 * temporary register inside the loop is not the same as
		 *  FROM or TO.
		*/
		  "=&r" (tmp), "+r" (to), "+r" (from)
		:
		: "lp_count", "lp_start", "lp_end", "memory");

		return n;
	}

	/*
	 * Hand-crafted constant propagation to reduce code sz of the
	 * laddered copy 16x,8,4,2,1
	 */
	if (__builtin_constant_p(orig_n)) {
		res = orig_n;

		if (orig_n / 16) {
			orig_n = orig_n % 16;

			__asm__ __volatile__(
			"	lsr   lp_count, %7,4		\n"
			"	lp    3f			\n"
			"1:	ld.ab   %3, [%2, 4]		\n"
			"11:	ld.ab   %4, [%2, 4]		\n"
			"12:	ld.ab   %5, [%2, 4]		\n"
			"13:	ld.ab   %6, [%2, 4]		\n"
			"	st.ab   %3, [%1, 4]		\n"
			"	st.ab   %4, [%1, 4]		\n"
			"	st.ab   %5, [%1, 4]		\n"
			"	st.ab   %6, [%1, 4]		\n"
			"	sub     %0,%0,16		\n"
			"3:	;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   3b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   1b, 4b			\n"
			"	.word   11b,4b			\n"
			"	.word   12b,4b			\n"
			"	.word   13b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from),
			  "=r"(tmp1), "=r"(tmp2), "=r"(tmp3), "=r"(tmp4)
			: "ir"(n)
			: "lp_count", "memory");
		}
		if (orig_n / 8) {
			orig_n = orig_n % 8;

			__asm__ __volatile__(
			"14:	ld.ab   %3, [%2,4]		\n"
			"15:	ld.ab   %4, [%2,4]		\n"
			"	st.ab   %3, [%1,4]		\n"
			"	st.ab   %4, [%1,4]		\n"
			"	sub     %0,%0,8			\n"
			"31:	;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   31b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   14b,4b			\n"
			"	.word   15b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from),
			  "=r"(tmp1), "=r"(tmp2)
			:
			: "memory");
		}
		if (orig_n / 4) {
			orig_n = orig_n % 4;

			__asm__ __volatile__(
			"16:	ld.ab   %3, [%2,4]		\n"
			"	st.ab   %3, [%1,4]		\n"
			"	sub     %0,%0,4			\n"
			"32:	;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   32b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   16b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
		if (orig_n / 2) {
			orig_n = orig_n % 2;

			__asm__ __volatile__(
			"17:	ldw.ab   %3, [%2,2]		\n"
			"	stw.ab   %3, [%1,2]		\n"
			"	sub      %0,%0,2		\n"
			"33:	;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   33b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   17b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
		if (orig_n & 1) {
			__asm__ __volatile__(
			"18:	ldb.ab   %3, [%2,2]		\n"
			"	stb.ab   %3, [%1,2]		\n"
			"	sub      %0,%0,1		\n"
			"34:	; nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   34b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   18b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
	} else {  /* n is NOT constant, so laddered copy of 16x,8,4,2,1  */

		__asm__ __volatile__(
		"	mov %0,%3			\n"
		"	lsr.f   lp_count, %3,4		\n"  /* 16x bytes */
		"	lpnz    3f			\n"
		"1:	ld.ab   %5, [%2, 4]		\n"
		"11:	ld.ab   %6, [%2, 4]		\n"
		"12:	ld.ab   %7, [%2, 4]		\n"
		"13:	ld.ab   %8, [%2, 4]		\n"
		"	st.ab   %5, [%1, 4]		\n"
		"	st.ab   %6, [%1, 4]		\n"
		"	st.ab   %7, [%1, 4]		\n"
		"	st.ab   %8, [%1, 4]		\n"
		"	sub     %0,%0,16		\n"
		"3:	and.f   %3,%3,0xf		\n"  /* stragglers */
		"	bz      34f			\n"
		"	bbit0   %3,3,31f		\n"  /* 8 bytes left */
		"14:	ld.ab   %5, [%2,4]		\n"
		"15:	ld.ab   %6, [%2,4]		\n"
		"	st.ab   %5, [%1,4]		\n"
		"	st.ab   %6, [%1,4]		\n"
		"	sub.f   %0,%0,8			\n"
		"31:	bbit0   %3,2,32f		\n"  /* 4 bytes left */
		"16:	ld.ab   %5, [%2,4]		\n"
		"	st.ab   %5, [%1,4]		\n"
		"	sub.f   %0,%0,4			\n"
		"32:	bbit0   %3,1,33f		\n"  /* 2 bytes left */
		"17:	ldw.ab  %5, [%2,2]		\n"
		"	stw.ab  %5, [%1,2]		\n"
		"	sub.f   %0,%0,2			\n"
		"33:	bbit0   %3,0,34f		\n"
		"18:	ldb.ab  %5, [%2,1]		\n"  /* 1 byte left */
		"	stb.ab  %5, [%1,1]		\n"
		"	sub.f   %0,%0,1			\n"
		"34:	;nop				\n"
		"	.section .fixup, \"ax\"		\n"
		"	.align 4			\n"
		"4:	j   34b				\n"
		"	.previous			\n"
		"	.section __ex_table, \"a\"	\n"
		"	.align 4			\n"
		"	.word   1b, 4b			\n"
		"	.word   11b,4b			\n"
		"	.word   12b,4b			\n"
		"	.word   13b,4b			\n"
		"	.word   14b,4b			\n"
		"	.word   15b,4b			\n"
		"	.word   16b,4b			\n"
		"	.word   17b,4b			\n"
		"	.word   18b,4b			\n"
		"	.previous			\n"
		: "=r" (res), "+r"(to), "+r"(from), "+r"(n), "=r"(val),
		  "=r"(tmp1), "=r"(tmp2), "=r"(tmp3), "=r"(tmp4)
		:
		: "lp_count", "memory");
	}

	return res;
}

extern unsigned long slowpath_copy_to_user(void __user *to, const void *from,
					   unsigned long n);

static inline unsigned long
__arc_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	long res = 0;
	char val;
	unsigned long tmp1, tmp2, tmp3, tmp4;
	unsigned long orig_n = n;

	if (n == 0)
		return 0;

	/* unaligned */
	if (((unsigned long)to & 0x3) || ((unsigned long)from & 0x3)) {

		unsigned char tmp;

		__asm__ __volatile__(
		"	mov.f   lp_count, %0		\n"
		"	lpnz 3f				\n"
		"	ldb.ab  %1, [%3, 1]		\n"
		"1:	stb.ab  %1, [%2, 1]		\n"
		"	sub     %0, %0, 1		\n"
		"3:	;nop				\n"
		"	.section .fixup, \"ax\"		\n"
		"	.align 4			\n"
		"4:	j   3b				\n"
		"	.previous			\n"
		"	.section __ex_table, \"a\"	\n"
		"	.align 4			\n"
		"	.word   1b, 4b			\n"
		"	.previous			\n"

		: "+r" (n),
		/* Note as an '&' earlyclobber operand to make sure the
		 * temporary register inside the loop is not the same as
		 * FROM or TO.
		 */
		  "=&r" (tmp), "+r" (to), "+r" (from)
		:
		: "lp_count", "lp_start", "lp_end", "memory");

		return n;
	}

	if (__builtin_constant_p(orig_n)) {
		res = orig_n;

		if (orig_n / 16) {
			orig_n = orig_n % 16;

			__asm__ __volatile__(
			"	lsr lp_count, %7,4		\n"
			"	lp  3f				\n"
			"	ld.ab %3, [%2, 4]		\n"
			"	ld.ab %4, [%2, 4]		\n"
			"	ld.ab %5, [%2, 4]		\n"
			"	ld.ab %6, [%2, 4]		\n"
			"1:	st.ab %3, [%1, 4]		\n"
			"11:	st.ab %4, [%1, 4]		\n"
			"12:	st.ab %5, [%1, 4]		\n"
			"13:	st.ab %6, [%1, 4]		\n"
			"	sub   %0, %0, 16		\n"
			"3:;nop					\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   3b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   1b, 4b			\n"
			"	.word   11b,4b			\n"
			"	.word   12b,4b			\n"
			"	.word   13b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from),
			  "=r"(tmp1), "=r"(tmp2), "=r"(tmp3), "=r"(tmp4)
			: "ir"(n)
			: "lp_count", "memory");
		}
		if (orig_n / 8) {
			orig_n = orig_n % 8;

			__asm__ __volatile__(
			"	ld.ab   %3, [%2,4]		\n"
			"	ld.ab   %4, [%2,4]		\n"
			"14:	st.ab   %3, [%1,4]		\n"
			"15:	st.ab   %4, [%1,4]		\n"
			"	sub     %0, %0, 8		\n"
			"31:;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   31b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   14b,4b			\n"
			"	.word   15b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from),
			  "=r"(tmp1), "=r"(tmp2)
			:
			: "memory");
		}
		if (orig_n / 4) {
			orig_n = orig_n % 4;

			__asm__ __volatile__(
			"	ld.ab   %3, [%2,4]		\n"
			"16:	st.ab   %3, [%1,4]		\n"
			"	sub     %0, %0, 4		\n"
			"32:;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   32b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   16b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
		if (orig_n / 2) {
			orig_n = orig_n % 2;

			__asm__ __volatile__(
			"	ldw.ab    %3, [%2,2]		\n"
			"17:	stw.ab    %3, [%1,2]		\n"
			"	sub       %0, %0, 2		\n"
			"33:;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   33b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   17b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
		if (orig_n & 1) {
			__asm__ __volatile__(
			"	ldb.ab  %3, [%2,1]		\n"
			"18:	stb.ab  %3, [%1,1]		\n"
			"	sub     %0, %0, 1		\n"
			"34:	;nop				\n"
			"	.section .fixup, \"ax\"		\n"
			"	.align 4			\n"
			"4:	j   34b				\n"
			"	.previous			\n"
			"	.section __ex_table, \"a\"	\n"
			"	.align 4			\n"
			"	.word   18b,4b			\n"
			"	.previous			\n"
			: "+r" (res), "+r"(to), "+r"(from), "=r"(tmp1)
			:
			: "memory");
		}
	} else {  /* n is NOT constant, so laddered copy of 16x,8,4,2,1  */

		__asm__ __volatile__(
		"	mov   %0,%3			\n"
		"	lsr.f lp_count, %3,4		\n"  /* 16x bytes */
		"	lpnz  3f			\n"
		"	ld.ab %5, [%2, 4]		\n"
		"	ld.ab %6, [%2, 4]		\n"
		"	ld.ab %7, [%2, 4]		\n"
		"	ld.ab %8, [%2, 4]		\n"
		"1:	st.ab %5, [%1, 4]		\n"
		"11:	st.ab %6, [%1, 4]		\n"
		"12:	st.ab %7, [%1, 4]		\n"
		"13:	st.ab %8, [%1, 4]		\n"
		"	sub   %0, %0, 16		\n"
		"3:	and.f %3,%3,0xf			\n" /* stragglers */
		"	bz 34f				\n"
		"	bbit0   %3,3,31f		\n" /* 8 bytes left */
		"	ld.ab   %5, [%2,4]		\n"
		"	ld.ab   %6, [%2,4]		\n"
		"14:	st.ab   %5, [%1,4]		\n"
		"15:	st.ab   %6, [%1,4]		\n"
		"	sub.f   %0, %0, 8		\n"
		"31:	bbit0   %3,2,32f		\n"  /* 4 bytes left */
		"	ld.ab   %5, [%2,4]		\n"
		"16:	st.ab   %5, [%1,4]		\n"
		"	sub.f   %0, %0, 4		\n"
		"32:	bbit0 %3,1,33f			\n"  /* 2 bytes left */
		"	ldw.ab    %5, [%2,2]		\n"
		"17:	stw.ab    %5, [%1,2]		\n"
		"	sub.f %0, %0, 2			\n"
		"33:	bbit0 %3,0,34f			\n"
		"	ldb.ab    %5, [%2,1]		\n"  /* 1 byte left */
		"18:	stb.ab  %5, [%1,1]		\n"
		"	sub.f %0, %0, 1			\n"
		"34:	;nop				\n"
		"	.section .fixup, \"ax\"		\n"
		"	.align 4			\n"
		"4:	j   34b				\n"
		"	.previous			\n"
		"	.section __ex_table, \"a\"	\n"
		"	.align 4			\n"
		"	.word   1b, 4b			\n"
		"	.word   11b,4b			\n"
		"	.word   12b,4b			\n"
		"	.word   13b,4b			\n"
		"	.word   14b,4b			\n"
		"	.word   15b,4b			\n"
		"	.word   16b,4b			\n"
		"	.word   17b,4b			\n"
		"	.word   18b,4b			\n"
		"	.previous			\n"
		: "=r" (res), "+r"(to), "+r"(from), "+r"(n), "=r"(val),
		  "=r"(tmp1), "=r"(tmp2), "=r"(tmp3), "=r"(tmp4)
		:
		: "lp_count", "memory");
	}

	return res;
}

static inline unsigned long __arc_clear_user(void __user *to, unsigned long n)
{
	long res = n;
	unsigned char *d_char = to;

	__asm__ __volatile__(
	"	bbit0   %0, 0, 1f		\n"
	"75:	stb.ab  %2, [%0,1]		\n"
	"	sub %1, %1, 1			\n"
	"1:	bbit0   %0, 1, 2f		\n"
	"76:	stw.ab  %2, [%0,2]		\n"
	"	sub %1, %1, 2			\n"
	"2:	asr.f   lp_count, %1, 2		\n"
	"	lpnz    3f			\n"
	"77:	st.ab   %2, [%0,4]		\n"
	"	sub %1, %1, 4			\n"
	"3:	bbit0   %1, 1, 4f		\n"
	"78:	stw.ab  %2, [%0,2]		\n"
	"	sub %1, %1, 2			\n"
	"4:	bbit0   %1, 0, 5f		\n"
	"79:	stb.ab  %2, [%0,1]		\n"
	"	sub %1, %1, 1			\n"
	"5:					\n"
	"	.section .fixup, \"ax\"		\n"
	"	.align 4			\n"
	"3:	j   5b				\n"
	"	.previous			\n"
	"	.section __ex_table, \"a\"	\n"
	"	.align 4			\n"
	"	.word   75b, 3b			\n"
	"	.word   76b, 3b			\n"
	"	.word   77b, 3b			\n"
	"	.word   78b, 3b			\n"
	"	.word   79b, 3b			\n"
	"	.previous			\n"
	: "+r"(d_char), "+r"(res)
	: "i"(0)
	: "lp_count", "lp_start", "lp_end", "memory");

	return res;
}

static inline long
__arc_strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res = 0;
	char val;

	if (count == 0)
		return 0;

	__asm__ __volatile__(
	"	lp	3f			\n"
	"1:	ldb.ab  %3, [%2, 1]		\n"
	"	breq.d	%3, 0, 3f               \n"
	"	stb.ab  %3, [%1, 1]		\n"
	"	add	%0, %0, 1	# Num of NON NULL bytes copied	\n"
	"3:								\n"
	"	.section .fixup, \"ax\"		\n"
	"	.align 4			\n"
	"4:	mov %0, %4		# sets @res as -EFAULT	\n"
	"	j   3b				\n"
	"	.previous			\n"
	"	.section __ex_table, \"a\"	\n"
	"	.align 4			\n"
	"	.word   1b, 4b			\n"
	"	.previous			\n"
	: "+r"(res), "+r"(dst), "+r"(src), "=r"(val)
	: "g"(-EFAULT), "l"(count)
	: "memory");

	return res;
}

static inline long __arc_strnlen_user(const char __user *s, long n)
{
	long res, tmp1, cnt;
	char val;

	__asm__ __volatile__(
	"	mov %2, %1			\n"
	"1:	ldb.ab  %3, [%0, 1]		\n"
	"	breq.d  %3, 0, 2f		\n"
	"	sub.f   %2, %2, 1		\n"
	"	bnz 1b				\n"
	"	sub %2, %2, 1			\n"
	"2:	sub %0, %1, %2			\n"
	"3:	;nop				\n"
	"	.section .fixup, \"ax\"		\n"
	"	.align 4			\n"
	"4:	mov %0, 0			\n"
	"	j   3b				\n"
	"	.previous			\n"
	"	.section __ex_table, \"a\"	\n"
	"	.align 4			\n"
	"	.word 1b, 4b			\n"
	"	.previous			\n"
	: "=r"(res), "=r"(tmp1), "=r"(cnt), "=r"(val)
	: "0"(s), "1"(n)
	: "memory");

	return res;
}

#ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
#define __copy_from_user(t, f, n)	__arc_copy_from_user(t, f, n)
#define __copy_to_user(t, f, n)		__arc_copy_to_user(t, f, n)
#define __clear_user(d, n)		__arc_clear_user(d, n)
#define __strncpy_from_user(d, s, n)	__arc_strncpy_from_user(d, s, n)
#define __strnlen_user(s, n)		__arc_strnlen_user(s, n)
#else
extern long arc_copy_from_user_noinline(void *to, const void __user * from,
		unsigned long n);
extern long arc_copy_to_user_noinline(void __user *to, const void *from,
		unsigned long n);
extern unsigned long arc_clear_user_noinline(void __user *to,
		unsigned long n);
extern long arc_strncpy_from_user_noinline (char *dst, const char __user *src,
		long count);
extern long arc_strnlen_user_noinline(const char __user *src, long n);

#define __copy_from_user(t, f, n)	arc_copy_from_user_noinline(t, f, n)
#define __copy_to_user(t, f, n)		arc_copy_to_user_noinline(t, f, n)
#define __clear_user(d, n)		arc_clear_user_noinline(d, n)
#define __strncpy_from_user(d, s, n)	arc_strncpy_from_user_noinline(d, s, n)
#define __strnlen_user(s, n)		arc_strnlen_user_noinline(s, n)

#endif

#include <asm-generic/uaccess.h>

extern int fixup_exception(struct pt_regs *regs);

#endif
