#ifndef _H8300_BITOPS_H
#define _H8300_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 * Copyright 2002, Yoshinori Sato
 */

#include <linux/compiler.h>
#include <asm/system.h>

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
static __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %2\n\t"
		"adds #1,%0\n\t"
		"bcs 1b"
		: "=r" (result)
		: "0"  (result),"r" (word));
	return result;
}

#define H8300_GEN_BITOP_CONST(OP,BIT)			    \
	case BIT:					    \
	__asm__(OP " #" #BIT ",@%0"::"r"(b_addr):"memory"); \
	break;

#define H8300_GEN_BITOP(FNAME,OP)				      \
static __inline__ void FNAME(int nr, volatile unsigned long* addr)    \
{								      \
	volatile unsigned char *b_addr;				      \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);    \
	if (__builtin_constant_p(nr)) {				      \
		switch(nr & 7) {				      \
			H8300_GEN_BITOP_CONST(OP,0)		      \
			H8300_GEN_BITOP_CONST(OP,1)		      \
			H8300_GEN_BITOP_CONST(OP,2)		      \
			H8300_GEN_BITOP_CONST(OP,3)		      \
			H8300_GEN_BITOP_CONST(OP,4)		      \
			H8300_GEN_BITOP_CONST(OP,5)		      \
			H8300_GEN_BITOP_CONST(OP,6)		      \
			H8300_GEN_BITOP_CONST(OP,7)		      \
		}						      \
	} else {						      \
		__asm__(OP " %w0,@%1"::"r"(nr),"r"(b_addr):"memory"); \
	}							      \
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

H8300_GEN_BITOP(set_bit	  ,"bset")
H8300_GEN_BITOP(clear_bit ,"bclr")
H8300_GEN_BITOP(change_bit,"bnot")
#define __set_bit(nr,addr)    set_bit((nr),(addr))
#define __clear_bit(nr,addr)  clear_bit((nr),(addr))
#define __change_bit(nr,addr) change_bit((nr),(addr))

#undef H8300_GEN_BITOP
#undef H8300_GEN_BITOP_CONST

static __inline__ int test_bit(int nr, const unsigned long* addr)
{
	return (*((volatile unsigned char *)addr + 
               ((nr >> 3) ^ 3)) & (1UL << (nr & 7))) != 0;
}

#define __test_bit(nr, addr) test_bit(nr, addr)

#define H8300_GEN_TEST_BITOP_CONST_INT(OP,BIT)			     \
	case BIT:						     \
	__asm__("stc ccr,%w1\n\t"				     \
		"orc #0x80,ccr\n\t"				     \
		"bld #" #BIT ",@%4\n\t"				     \
		OP " #" #BIT ",@%4\n\t"				     \
		"rotxl.l %0\n\t"				     \
		"ldc %w1,ccr"					     \
		: "=r"(retval),"=&r"(ccrsave),"=m"(*b_addr)	     \
		: "0" (retval),"r" (b_addr)			     \
		: "memory");                                         \
        break;

#define H8300_GEN_TEST_BITOP_CONST(OP,BIT)			     \
	case BIT:						     \
	__asm__("bld #" #BIT ",@%3\n\t"				     \
		OP " #" #BIT ",@%3\n\t"				     \
		"rotxl.l %0\n\t"				     \
		: "=r"(retval),"=m"(*b_addr)			     \
		: "0" (retval),"r" (b_addr)			     \
		: "memory");                                         \
        break;

#define H8300_GEN_TEST_BITOP(FNNAME,OP)				     \
static __inline__ int FNNAME(int nr, volatile void * addr)	     \
{								     \
	int retval = 0;						     \
	char ccrsave;						     \
	volatile unsigned char *b_addr;				     \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);   \
	if (__builtin_constant_p(nr)) {				     \
		switch(nr & 7) {				     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,0)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,1)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,2)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,3)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,4)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,5)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,6)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,7)	     \
		}						     \
	} else {						     \
		__asm__("stc ccr,%w1\n\t"			     \
			"orc #0x80,ccr\n\t"			     \
			"btst %w5,@%4\n\t"			     \
			OP " %w5,@%4\n\t"			     \
			"beq 1f\n\t"				     \
			"inc.l #1,%0\n"				     \
			"1:\n\t"				     \
			"ldc %w1,ccr"				     \
			: "=r"(retval),"=&r"(ccrsave),"=m"(*b_addr)  \
			: "0" (retval),"r" (b_addr),"r"(nr)	     \
			: "memory");				     \
	}							     \
	return retval;						     \
}								     \
								     \
static __inline__ int __ ## FNNAME(int nr, volatile void * addr)     \
{								     \
	int retval = 0;						     \
	volatile unsigned char *b_addr;				     \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);   \
	if (__builtin_constant_p(nr)) {				     \
		switch(nr & 7) {				     \
			H8300_GEN_TEST_BITOP_CONST(OP,0) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,1) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,2) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,3) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,4) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,5) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,6) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,7) 	     \
		}						     \
	} else {						     \
		__asm__("btst %w4,@%3\n\t"			     \
			OP " %w4,@%3\n\t"			     \
			"beq 1f\n\t"				     \
			"inc.l #1,%0\n"				     \
			"1:"					     \
			: "=r"(retval),"=m"(*b_addr)		     \
			: "0" (retval),"r" (b_addr),"r"(nr)	     \
			: "memory");				     \
	}							     \
	return retval;						     \
}

H8300_GEN_TEST_BITOP(test_and_set_bit,	 "bset")
H8300_GEN_TEST_BITOP(test_and_clear_bit, "bclr")
H8300_GEN_TEST_BITOP(test_and_change_bit,"bnot")
#undef H8300_GEN_TEST_BITOP_CONST
#undef H8300_GEN_TEST_BITOP_CONST_INT
#undef H8300_GEN_TEST_BITOP

#include <asm-generic/bitops/ffs.h>

static __inline__ unsigned long __ffs(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %2\n\t"
		"adds #1,%0\n\t"
		"bcc 1b"
		: "=r" (result)
		: "0"(result),"r"(word));
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
