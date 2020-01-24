/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300_BITOPS_H
#define _H8300_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 * Copyright 2002, Yoshinori Sato
 */

#include <linux/compiler.h>

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

/*
 * Function prototypes to keep gcc -Wall happy
 */

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %1\n\t"
		"adds #1,%0\n\t"
		"bcs 1b"
		: "=r"(result),"=r"(word)
		: "0"(result), "1"(word));
	return result;
}

#define H8300_GEN_BITOP(FNAME, OP)				\
static inline void FNAME(int nr, volatile unsigned long *addr)	\
{								\
	unsigned char *b_addr;					\
	unsigned char bit = nr & 7;				\
								\
	b_addr = (unsigned char *)addr + ((nr >> 3) ^ 3);	\
	if (__builtin_constant_p(nr)) {				\
		__asm__(OP " %1,%0" : "+WU"(*b_addr) : "i"(nr & 7));	\
	} else {						\
		__asm__(OP " %s1,%0" : "+WU"(*b_addr) : "r"(bit));	\
	}							\
}

H8300_GEN_BITOP(set_bit,    "bset")
H8300_GEN_BITOP(clear_bit,  "bclr")
H8300_GEN_BITOP(change_bit, "bnot")
#define __set_bit(nr, addr)    set_bit((nr), (addr))
#define __clear_bit(nr, addr)  clear_bit((nr), (addr))
#define __change_bit(nr, addr) change_bit((nr), (addr))

#undef H8300_GEN_BITOP

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	int ret = 0;
	unsigned char *b_addr;
	unsigned char bit = nr & 7;

	b_addr = (unsigned char *)addr + ((nr >> 3) ^ 3);
	if (__builtin_constant_p(nr)) {
		__asm__("bld %Z2,%1\n\t"
			"rotxl %0\n\t"
			: "=r"(ret)
			: "WU"(*b_addr), "i"(nr & 7), "0"(ret) : "cc");
	} else {
		__asm__("btst %w2,%1\n\t"
			"beq 1f\n\t"
			"inc.l #1,%0\n"
			"1:"
			: "=r"(ret)
			: "WU"(*b_addr), "r"(bit), "0"(ret) : "cc");
	}
	return ret;
}

#define __test_bit(nr, addr) test_bit(nr, addr)

#define H8300_GEN_TEST_BITOP(FNNAME, OP)				\
static inline int FNNAME(int nr, void *addr)				\
{									\
	int retval = 0;							\
	char ccrsave;							\
	unsigned char *b_addr;						\
	unsigned char bit = nr & 7;					\
									\
	b_addr = (unsigned char *)addr + ((nr >> 3) ^ 3);		\
	if (__builtin_constant_p(nr)) {					\
		__asm__("stc ccr,%s2\n\t"				\
			"orc #0x80,ccr\n\t"				\
			"bld %4,%1\n\t"					\
			OP " %4,%1\n\t"					\
			"rotxl.l %0\n\t"				\
			"ldc %s2,ccr"					\
			: "=r"(retval), "+WU" (*b_addr), "=&r"(ccrsave)	\
			: "0"(retval), "i"(nr & 7) : "cc");		\
	} else {							\
		__asm__("stc ccr,%t3\n\t"				\
			"orc #0x80,ccr\n\t"				\
			"btst %s3,%1\n\t"				\
			OP " %s3,%1\n\t"				\
			"beq 1f\n\t"					\
			"inc.l #1,%0\n\t"				\
			"1:\n"						\
			"ldc %t3,ccr"					\
			: "=r"(retval), "+WU" (*b_addr)			\
			: "0" (retval), "r"(bit) : "cc");		\
	}								\
	return retval;							\
}									\
									\
static inline int __ ## FNNAME(int nr, void *addr)			\
{									\
	int retval = 0;							\
	unsigned char *b_addr;						\
	unsigned char bit = nr & 7;					\
									\
	b_addr = (unsigned char *)addr + ((nr >> 3) ^ 3);		\
	if (__builtin_constant_p(nr)) {					\
		__asm__("bld %3,%1\n\t"					\
			OP " %3,%1\n\t"					\
			"rotxl.l %0\n\t"				\
			: "=r"(retval), "+WU"(*b_addr)			\
			: "0" (retval), "i"(nr & 7));			\
	} else {							\
		__asm__("btst %s3,%1\n\t"				\
			OP " %s3,%1\n\t"				\
			"beq 1f\n\t"					\
			"inc.l #1,%0\n\t"				\
			"1:"						\
			: "=r"(retval), "+WU"(*b_addr)			\
			: "0" (retval), "r"(bit));			\
	}								\
	return retval;							\
}

H8300_GEN_TEST_BITOP(test_and_set_bit,	  "bset")
H8300_GEN_TEST_BITOP(test_and_clear_bit,  "bclr")
H8300_GEN_TEST_BITOP(test_and_change_bit, "bnot")
#undef H8300_GEN_TEST_BITOP

#include <asm-generic/bitops/ffs.h>

static inline unsigned long __ffs(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %1\n\t"
		"adds #1,%0\n\t"
		"bcc 1b"
		: "=r" (result),"=r"(word)
		: "0"(result), "1"(word));
	return result;
}

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* __KERNEL__ */

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#endif /* _H8300_BITOPS_H */
