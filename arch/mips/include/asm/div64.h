/*
 * Copyright (C) 2000, 2004, 2021  Maciej W. Rozycki
 * Copyright (C) 2003, 07 Ralf Baechle (ralf@linux-mips.org)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_DIV64_H
#define __ASM_DIV64_H

#include <asm/bitsperlong.h>

#if BITS_PER_LONG == 32

/*
 * No traps on overflows for any of these...
 */

#define do_div64_32(res, high, low, base) ({				\
	unsigned long __cf, __tmp, __tmp2, __i;				\
	unsigned long __quot32, __mod32;				\
									\
	__asm__(							\
	"	.set	push					\n"	\
	"	.set	noat					\n"	\
	"	.set	noreorder				\n"	\
	"	move	%2, $0					\n"	\
	"	move	%3, $0					\n"	\
	"	b	1f					\n"	\
	"	 li	%4, 0x21				\n"	\
	"0:							\n"	\
	"	sll	$1, %0, 0x1				\n"	\
	"	srl	%3, %0, 0x1f				\n"	\
	"	or	%0, $1, %5				\n"	\
	"	sll	%1, %1, 0x1				\n"	\
	"	sll	%2, %2, 0x1				\n"	\
	"1:							\n"	\
	"	bnez	%3, 2f					\n"	\
	"	 sltu	%5, %0, %z6				\n"	\
	"	bnez	%5, 3f					\n"	\
	"2:							\n"	\
	"	 addiu	%4, %4, -1				\n"	\
	"	subu	%0, %0, %z6				\n"	\
	"	addiu	%2, %2, 1				\n"	\
	"3:							\n"	\
	"	bnez	%4, 0b					\n"	\
	"	 srl	%5, %1, 0x1f				\n"	\
	"	.set	pop"						\
	: "=&r" (__mod32), "=&r" (__tmp),				\
	  "=&r" (__quot32), "=&r" (__cf),				\
	  "=&r" (__i), "=&r" (__tmp2)					\
	: "Jr" (base), "0" (high), "1" (low));				\
									\
	(res) = __quot32;						\
	__mod32;							\
})

#define __div64_32(n, base) ({						\
	unsigned long __upper, __low, __high, __radix;			\
	unsigned long long __modquot;					\
	unsigned long long __quot;					\
	unsigned long long __div;					\
	unsigned long __mod;						\
									\
	__div = (*n);							\
	__radix = (base);						\
									\
	__high = __div >> 32;						\
	__low = __div;							\
	__upper = __high;						\
									\
	if (__high) {							\
		__asm__("divu	$0, %z1, %z2"				\
		: "=x" (__modquot)					\
		: "Jr" (__high), "Jr" (__radix));			\
		__upper = __modquot >> 32;				\
		__high = __modquot;					\
	}								\
									\
	__mod = do_div64_32(__low, __upper, __low, __radix);		\
									\
	__quot = __high;						\
	__quot = __quot << 32 | __low;					\
	(*n) = __quot;							\
	__mod;								\
})

#endif /* BITS_PER_LONG == 32 */

#include <asm-generic/div64.h>

#endif /* __ASM_DIV64_H */
