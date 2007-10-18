/* 32 and 64-bit millicode, original author Hewlett-Packard
   adapted for gcc by Paul Bame <bame@debian.org>
   and Alan Modra <alan@linuxcare.com.au>.

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of GCC and is released under the terms of
   of the GNU General Public License as published by the Free Software
   Foundation; either version 2, or (at your option) any later version.
   See the file COPYING in the top-level GCC source directory for a copy
   of the license.  */

#include "milli.h"

#ifdef L_mulI
/* VERSION "@(#)$$mulI $ Revision: 12.4 $ $ Date: 94/03/17 17:18:51 $" */
/******************************************************************************
This routine is used on PA2.0 processors when gcc -mno-fpregs is used

ROUTINE:	$$mulI


DESCRIPTION:	

	$$mulI multiplies two single word integers, giving a single 
	word result.  


INPUT REGISTERS:

	arg0 = Operand 1
	arg1 = Operand 2
	r31  == return pc
	sr0  == return space when called externally 


OUTPUT REGISTERS:

	arg0 = undefined
	arg1 = undefined
	ret1 = result 

OTHER REGISTERS AFFECTED:

	r1   = undefined

SIDE EFFECTS:

	Causes a trap under the following conditions:  NONE
	Changes memory at the following places:  NONE

PERMISSIBLE CONTEXT:

	Unwindable
	Does not create a stack frame
	Is usable for internal or external microcode

DISCUSSION:

	Calls other millicode routines via mrp:  NONE
	Calls other millicode routines:  NONE

***************************************************************************/


#define	a0	%arg0
#define	a1	%arg1
#define	t0	%r1
#define	r	%ret1

#define	a0__128a0	zdep	a0,24,25,a0
#define	a0__256a0	zdep	a0,23,24,a0
#define	a1_ne_0_b_l0	comb,<>	a1,0,LREF(l0)
#define	a1_ne_0_b_l1	comb,<>	a1,0,LREF(l1)
#define	a1_ne_0_b_l2	comb,<>	a1,0,LREF(l2)
#define	b_n_ret_t0	b,n	LREF(ret_t0)
#define	b_e_shift	b	LREF(e_shift)
#define	b_e_t0ma0	b	LREF(e_t0ma0)
#define	b_e_t0		b	LREF(e_t0)
#define	b_e_t0a0	b	LREF(e_t0a0)
#define	b_e_t02a0	b	LREF(e_t02a0)
#define	b_e_t04a0	b	LREF(e_t04a0)
#define	b_e_2t0		b	LREF(e_2t0)
#define	b_e_2t0a0	b	LREF(e_2t0a0)
#define	b_e_2t04a0	b	LREF(e2t04a0)
#define	b_e_3t0		b	LREF(e_3t0)
#define	b_e_4t0		b	LREF(e_4t0)
#define	b_e_4t0a0	b	LREF(e_4t0a0)
#define	b_e_4t08a0	b	LREF(e4t08a0)
#define	b_e_5t0		b	LREF(e_5t0)
#define	b_e_8t0		b	LREF(e_8t0)
#define	b_e_8t0a0	b	LREF(e_8t0a0)
#define	r__r_a0		add	r,a0,r
#define	r__r_2a0	sh1add	a0,r,r
#define	r__r_4a0	sh2add	a0,r,r
#define	r__r_8a0	sh3add	a0,r,r
#define	r__r_t0		add	r,t0,r
#define	r__r_2t0	sh1add	t0,r,r
#define	r__r_4t0	sh2add	t0,r,r
#define	r__r_8t0	sh3add	t0,r,r
#define	t0__3a0		sh1add	a0,a0,t0
#define	t0__4a0		sh2add	a0,0,t0
#define	t0__5a0		sh2add	a0,a0,t0
#define	t0__8a0		sh3add	a0,0,t0
#define	t0__9a0		sh3add	a0,a0,t0
#define	t0__16a0	zdep	a0,27,28,t0
#define	t0__32a0	zdep	a0,26,27,t0
#define	t0__64a0	zdep	a0,25,26,t0
#define	t0__128a0	zdep	a0,24,25,t0
#define	t0__t0ma0	sub	t0,a0,t0
#define	t0__t0_a0	add	t0,a0,t0
#define	t0__t0_2a0	sh1add	a0,t0,t0
#define	t0__t0_4a0	sh2add	a0,t0,t0
#define	t0__t0_8a0	sh3add	a0,t0,t0
#define	t0__2t0_a0	sh1add	t0,a0,t0
#define	t0__3t0		sh1add	t0,t0,t0
#define	t0__4t0		sh2add	t0,0,t0
#define	t0__4t0_a0	sh2add	t0,a0,t0
#define	t0__5t0		sh2add	t0,t0,t0
#define	t0__8t0		sh3add	t0,0,t0
#define	t0__8t0_a0	sh3add	t0,a0,t0
#define	t0__9t0		sh3add	t0,t0,t0
#define	t0__16t0	zdep	t0,27,28,t0
#define	t0__32t0	zdep	t0,26,27,t0
#define	t0__256a0	zdep	a0,23,24,t0


	SUBSPA_MILLI
	ATTR_MILLI
	.align 16
	.proc
	.callinfo millicode
	.export $$mulI,millicode
GSYM($$mulI)	
	combt,<<=	a1,a0,LREF(l4)	/* swap args if unsigned a1>a0 */
	copy		0,r		/* zero out the result */
	xor		a0,a1,a0	/* swap a0 & a1 using the */
	xor		a0,a1,a1	/*  old xor trick */
	xor		a0,a1,a0
LSYM(l4)
	combt,<=	0,a0,LREF(l3)		/* if a0>=0 then proceed like unsigned */
	zdep		a1,30,8,t0	/* t0 = (a1&0xff)<<1 ********* */
	sub,>		0,a1,t0		/* otherwise negate both and */
	combt,<=,n	a0,t0,LREF(l2)	/*  swap back if |a0|<|a1| */
	sub		0,a0,a1
	movb,tr,n	t0,a0,LREF(l2)	/* 10th inst.  */

LSYM(l0)	r__r_t0				/* add in this partial product */
LSYM(l1)	a0__256a0			/* a0 <<= 8 ****************** */
LSYM(l2)	zdep		a1,30,8,t0	/* t0 = (a1&0xff)<<1 ********* */
LSYM(l3)	blr		t0,0		/* case on these 8 bits ****** */
		extru		a1,23,24,a1	/* a1 >>= 8 ****************** */

/*16 insts before this.  */
/*			  a0 <<= 8 ************************** */
LSYM(x0)	a1_ne_0_b_l2	! a0__256a0	! MILLIRETN	! nop
LSYM(x1)	a1_ne_0_b_l1	! r__r_a0	! MILLIRETN	! nop
LSYM(x2)	a1_ne_0_b_l1	! r__r_2a0	! MILLIRETN	! nop
LSYM(x3)	a1_ne_0_b_l0	! t0__3a0	! MILLIRET	! r__r_t0
LSYM(x4)	a1_ne_0_b_l1	! r__r_4a0	! MILLIRETN	! nop
LSYM(x5)	a1_ne_0_b_l0	! t0__5a0	! MILLIRET	! r__r_t0
LSYM(x6)	t0__3a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x7)	t0__3a0		! a1_ne_0_b_l0	! r__r_4a0	! b_n_ret_t0
LSYM(x8)	a1_ne_0_b_l1	! r__r_8a0	! MILLIRETN	! nop
LSYM(x9)	a1_ne_0_b_l0	! t0__9a0	! MILLIRET	! r__r_t0
LSYM(x10)	t0__5a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x11)	t0__3a0		! a1_ne_0_b_l0	! r__r_8a0	! b_n_ret_t0
LSYM(x12)	t0__3a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x13)	t0__5a0		! a1_ne_0_b_l0	! r__r_8a0	! b_n_ret_t0
LSYM(x14)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x15)	t0__5a0		! a1_ne_0_b_l0	! t0__3t0	! b_n_ret_t0
LSYM(x16)	t0__16a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x17)	t0__9a0		! a1_ne_0_b_l0	! t0__t0_8a0	! b_n_ret_t0
LSYM(x18)	t0__9a0		! a1_ne_0_b_l1	! r__r_2t0	! MILLIRETN
LSYM(x19)	t0__9a0		! a1_ne_0_b_l0	! t0__2t0_a0	! b_n_ret_t0
LSYM(x20)	t0__5a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x21)	t0__5a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x22)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x23)	t0__5a0		! t0__2t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x24)	t0__3a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x25)	t0__5a0		! a1_ne_0_b_l0	! t0__5t0	! b_n_ret_t0
LSYM(x26)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x27)	t0__3a0		! a1_ne_0_b_l0	! t0__9t0	! b_n_ret_t0
LSYM(x28)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x29)	t0__3a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x30)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_2t0
LSYM(x31)	t0__32a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x32)	t0__32a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x33)	t0__8a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x34)	t0__16a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x35)	t0__9a0		! t0__3t0	! b_e_t0	! t0__t0_8a0
LSYM(x36)	t0__9a0		! a1_ne_0_b_l1	! r__r_4t0	! MILLIRETN
LSYM(x37)	t0__9a0		! a1_ne_0_b_l0	! t0__4t0_a0	! b_n_ret_t0
LSYM(x38)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_2t0
LSYM(x39)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x40)	t0__5a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x41)	t0__5a0		! a1_ne_0_b_l0	! t0__8t0_a0	! b_n_ret_t0
LSYM(x42)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x43)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x44)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x45)	t0__9a0		! a1_ne_0_b_l0	! t0__5t0	! b_n_ret_t0
LSYM(x46)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_a0
LSYM(x47)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_2a0
LSYM(x48)	t0__3a0		! a1_ne_0_b_l0	! t0__16t0	! b_n_ret_t0
LSYM(x49)	t0__9a0		! t0__5t0	! b_e_t0	! t0__t0_4a0
LSYM(x50)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_2t0
LSYM(x51)	t0__9a0		! t0__t0_8a0	! b_e_t0	! t0__3t0
LSYM(x52)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x53)	t0__3a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x54)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_2t0
LSYM(x55)	t0__9a0		! t0__3t0	! b_e_t0	! t0__2t0_a0
LSYM(x56)	t0__3a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x57)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__3t0
LSYM(x58)	t0__3a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x59)	t0__9a0		! t0__2t0_a0	! b_e_t02a0	! t0__3t0
LSYM(x60)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x61)	t0__5a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x62)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x63)	t0__64a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x64)	t0__64a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x65)	t0__8a0		! a1_ne_0_b_l0	! t0__8t0_a0	! b_n_ret_t0
LSYM(x66)	t0__32a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x67)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x68)	t0__8a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x69)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x70)	t0__64a0	! t0__t0_4a0	! b_e_t0	! t0__t0_2a0
LSYM(x71)	t0__9a0		! t0__8t0	! b_e_t0	! t0__t0ma0
LSYM(x72)	t0__9a0		! a1_ne_0_b_l1	! r__r_8t0	! MILLIRETN
LSYM(x73)	t0__9a0		! t0__8t0_a0	! b_e_shift	! r__r_t0
LSYM(x74)	t0__9a0		! t0__4t0_a0	! b_e_shift	! r__r_2t0
LSYM(x75)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x76)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_4t0
LSYM(x77)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x78)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x79)	t0__16a0	! t0__5t0	! b_e_t0	! t0__t0ma0
LSYM(x80)	t0__16a0	! t0__5t0	! b_e_shift	! r__r_t0
LSYM(x81)	t0__9a0		! t0__9t0	! b_e_shift	! r__r_t0
LSYM(x82)	t0__5a0		! t0__8t0_a0	! b_e_shift	! r__r_2t0
LSYM(x83)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x84)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x85)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__5t0
LSYM(x86)	t0__5a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x87)	t0__9a0		! t0__9t0	! b_e_t02a0	! t0__t0_4a0
LSYM(x88)	t0__5a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x89)	t0__5a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x90)	t0__9a0		! t0__5t0	! b_e_shift	! r__r_2t0
LSYM(x91)	t0__9a0		! t0__5t0	! b_e_t0	! t0__2t0_a0
LSYM(x92)	t0__5a0		! t0__2t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x93)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__3t0
LSYM(x94)	t0__9a0		! t0__5t0	! b_e_2t0	! t0__t0_2a0
LSYM(x95)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__5t0
LSYM(x96)	t0__8a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x97)	t0__8a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x98)	t0__32a0	! t0__3t0	! b_e_t0	! t0__t0_2a0
LSYM(x99)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__3t0
LSYM(x100)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_4t0
LSYM(x101)	t0__5a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x102)	t0__32a0	! t0__t0_2a0	! b_e_t0	! t0__3t0
LSYM(x103)	t0__5a0		! t0__5t0	! b_e_t02a0	! t0__4t0_a0
LSYM(x104)	t0__3a0		! t0__4t0_a0	! b_e_shift	! r__r_8t0
LSYM(x105)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x106)	t0__3a0		! t0__4t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x107)	t0__9a0		! t0__t0_4a0	! b_e_t02a0	! t0__8t0_a0
LSYM(x108)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_4t0
LSYM(x109)	t0__9a0		! t0__3t0	! b_e_t0	! t0__4t0_a0
LSYM(x110)	t0__9a0		! t0__3t0	! b_e_2t0	! t0__2t0_a0
LSYM(x111)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__3t0
LSYM(x112)	t0__3a0		! t0__2t0_a0	! b_e_t0	! t0__16t0
LSYM(x113)	t0__9a0		! t0__4t0_a0	! b_e_t02a0	! t0__3t0
LSYM(x114)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__3t0
LSYM(x115)	t0__9a0		! t0__2t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x116)	t0__3a0		! t0__2t0_a0	! b_e_4t0	! t0__4t0_a0
LSYM(x117)	t0__3a0		! t0__4t0_a0	! b_e_t0	! t0__9t0
LSYM(x118)	t0__3a0		! t0__4t0_a0	! b_e_t0a0	! t0__9t0
LSYM(x119)	t0__3a0		! t0__4t0_a0	! b_e_t02a0	! t0__9t0
LSYM(x120)	t0__5a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x121)	t0__5a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x122)	t0__5a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x123)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x124)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_4t0
LSYM(x125)	t0__5a0		! t0__5t0	! b_e_t0	! t0__5t0
LSYM(x126)	t0__64a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x127)	t0__128a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
LSYM(x128)	t0__128a0	! a1_ne_0_b_l1	! r__r_t0	! MILLIRETN
LSYM(x129)	t0__128a0	! a1_ne_0_b_l0	! t0__t0_a0	! b_n_ret_t0
LSYM(x130)	t0__64a0	! t0__t0_a0	! b_e_shift	! r__r_2t0
LSYM(x131)	t0__8a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x132)	t0__8a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x133)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x134)	t0__8a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x135)	t0__9a0		! t0__5t0	! b_e_t0	! t0__3t0
LSYM(x136)	t0__8a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x137)	t0__8a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x138)	t0__8a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x139)	t0__8a0		! t0__2t0_a0	! b_e_2t0a0	! t0__4t0_a0
LSYM(x140)	t0__3a0		! t0__2t0_a0	! b_e_4t0	! t0__5t0
LSYM(x141)	t0__8a0		! t0__2t0_a0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x142)	t0__9a0		! t0__8t0	! b_e_2t0	! t0__t0ma0
LSYM(x143)	t0__16a0	! t0__9t0	! b_e_t0	! t0__t0ma0
LSYM(x144)	t0__9a0		! t0__8t0	! b_e_shift	! r__r_2t0
LSYM(x145)	t0__9a0		! t0__8t0	! b_e_t0	! t0__2t0_a0
LSYM(x146)	t0__9a0		! t0__8t0_a0	! b_e_shift	! r__r_2t0
LSYM(x147)	t0__9a0		! t0__8t0_a0	! b_e_t0	! t0__2t0_a0
LSYM(x148)	t0__9a0		! t0__4t0_a0	! b_e_shift	! r__r_4t0
LSYM(x149)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__4t0_a0
LSYM(x150)	t0__9a0		! t0__4t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x151)	t0__9a0		! t0__4t0_a0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x152)	t0__9a0		! t0__2t0_a0	! b_e_shift	! r__r_8t0
LSYM(x153)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x154)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__4t0_a0
LSYM(x155)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__5t0
LSYM(x156)	t0__9a0		! t0__2t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x157)	t0__32a0	! t0__t0ma0	! b_e_t02a0	! t0__5t0
LSYM(x158)	t0__16a0	! t0__5t0	! b_e_2t0	! t0__t0ma0
LSYM(x159)	t0__32a0	! t0__5t0	! b_e_t0	! t0__t0ma0
LSYM(x160)	t0__5a0		! t0__4t0	! b_e_shift	! r__r_8t0
LSYM(x161)	t0__8a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x162)	t0__9a0		! t0__9t0	! b_e_shift	! r__r_2t0
LSYM(x163)	t0__9a0		! t0__9t0	! b_e_t0	! t0__2t0_a0
LSYM(x164)	t0__5a0		! t0__8t0_a0	! b_e_shift	! r__r_4t0
LSYM(x165)	t0__8a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x166)	t0__5a0		! t0__8t0_a0	! b_e_2t0	! t0__2t0_a0
LSYM(x167)	t0__5a0		! t0__8t0_a0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x168)	t0__5a0		! t0__4t0_a0	! b_e_shift	! r__r_8t0
LSYM(x169)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__8t0_a0
LSYM(x170)	t0__32a0	! t0__t0_2a0	! b_e_t0	! t0__5t0
LSYM(x171)	t0__9a0		! t0__2t0_a0	! b_e_t0	! t0__9t0
LSYM(x172)	t0__5a0		! t0__4t0_a0	! b_e_4t0	! t0__2t0_a0
LSYM(x173)	t0__9a0		! t0__2t0_a0	! b_e_t02a0	! t0__9t0
LSYM(x174)	t0__32a0	! t0__t0_2a0	! b_e_t04a0	! t0__5t0
LSYM(x175)	t0__8a0		! t0__2t0_a0	! b_e_5t0	! t0__2t0_a0
LSYM(x176)	t0__5a0		! t0__4t0_a0	! b_e_8t0	! t0__t0_a0
LSYM(x177)	t0__5a0		! t0__4t0_a0	! b_e_8t0a0	! t0__t0_a0
LSYM(x178)	t0__5a0		! t0__2t0_a0	! b_e_2t0	! t0__8t0_a0
LSYM(x179)	t0__5a0		! t0__2t0_a0	! b_e_2t0a0	! t0__8t0_a0
LSYM(x180)	t0__9a0		! t0__5t0	! b_e_shift	! r__r_4t0
LSYM(x181)	t0__9a0		! t0__5t0	! b_e_t0	! t0__4t0_a0
LSYM(x182)	t0__9a0		! t0__5t0	! b_e_2t0	! t0__2t0_a0
LSYM(x183)	t0__9a0		! t0__5t0	! b_e_2t0a0	! t0__2t0_a0
LSYM(x184)	t0__5a0		! t0__9t0	! b_e_4t0	! t0__t0_a0
LSYM(x185)	t0__9a0		! t0__4t0_a0	! b_e_t0	! t0__5t0
LSYM(x186)	t0__32a0	! t0__t0ma0	! b_e_2t0	! t0__3t0
LSYM(x187)	t0__9a0		! t0__4t0_a0	! b_e_t02a0	! t0__5t0
LSYM(x188)	t0__9a0		! t0__5t0	! b_e_4t0	! t0__t0_2a0
LSYM(x189)	t0__5a0		! t0__4t0_a0	! b_e_t0	! t0__9t0
LSYM(x190)	t0__9a0		! t0__2t0_a0	! b_e_2t0	! t0__5t0
LSYM(x191)	t0__64a0	! t0__3t0	! b_e_t0	! t0__t0ma0
LSYM(x192)	t0__8a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x193)	t0__8a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x194)	t0__8a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x195)	t0__8a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x196)	t0__8a0		! t0__3t0	! b_e_4t0	! t0__2t0_a0
LSYM(x197)	t0__8a0		! t0__3t0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x198)	t0__64a0	! t0__t0_2a0	! b_e_t0	! t0__3t0
LSYM(x199)	t0__8a0		! t0__4t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x200)	t0__5a0		! t0__5t0	! b_e_shift	! r__r_8t0
LSYM(x201)	t0__5a0		! t0__5t0	! b_e_t0	! t0__8t0_a0
LSYM(x202)	t0__5a0		! t0__5t0	! b_e_2t0	! t0__4t0_a0
LSYM(x203)	t0__5a0		! t0__5t0	! b_e_2t0a0	! t0__4t0_a0
LSYM(x204)	t0__8a0		! t0__2t0_a0	! b_e_4t0	! t0__3t0
LSYM(x205)	t0__5a0		! t0__8t0_a0	! b_e_t0	! t0__5t0
LSYM(x206)	t0__64a0	! t0__t0_4a0	! b_e_t02a0	! t0__3t0
LSYM(x207)	t0__8a0		! t0__2t0_a0	! b_e_3t0	! t0__4t0_a0
LSYM(x208)	t0__5a0		! t0__5t0	! b_e_8t0	! t0__t0_a0
LSYM(x209)	t0__5a0		! t0__5t0	! b_e_8t0a0	! t0__t0_a0
LSYM(x210)	t0__5a0		! t0__4t0_a0	! b_e_2t0	! t0__5t0
LSYM(x211)	t0__5a0		! t0__4t0_a0	! b_e_2t0a0	! t0__5t0
LSYM(x212)	t0__3a0		! t0__4t0_a0	! b_e_4t0	! t0__4t0_a0
LSYM(x213)	t0__3a0		! t0__4t0_a0	! b_e_4t0a0	! t0__4t0_a0
LSYM(x214)	t0__9a0		! t0__t0_4a0	! b_e_2t04a0	! t0__8t0_a0
LSYM(x215)	t0__5a0		! t0__4t0_a0	! b_e_5t0	! t0__2t0_a0
LSYM(x216)	t0__9a0		! t0__3t0	! b_e_shift	! r__r_8t0
LSYM(x217)	t0__9a0		! t0__3t0	! b_e_t0	! t0__8t0_a0
LSYM(x218)	t0__9a0		! t0__3t0	! b_e_2t0	! t0__4t0_a0
LSYM(x219)	t0__9a0		! t0__8t0_a0	! b_e_t0	! t0__3t0
LSYM(x220)	t0__3a0		! t0__9t0	! b_e_4t0	! t0__2t0_a0
LSYM(x221)	t0__3a0		! t0__9t0	! b_e_4t0a0	! t0__2t0_a0
LSYM(x222)	t0__9a0		! t0__4t0_a0	! b_e_2t0	! t0__3t0
LSYM(x223)	t0__9a0		! t0__4t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x224)	t0__9a0		! t0__3t0	! b_e_8t0	! t0__t0_a0
LSYM(x225)	t0__9a0		! t0__5t0	! b_e_t0	! t0__5t0
LSYM(x226)	t0__3a0		! t0__2t0_a0	! b_e_t02a0	! t0__32t0
LSYM(x227)	t0__9a0		! t0__5t0	! b_e_t02a0	! t0__5t0
LSYM(x228)	t0__9a0		! t0__2t0_a0	! b_e_4t0	! t0__3t0
LSYM(x229)	t0__9a0		! t0__2t0_a0	! b_e_4t0a0	! t0__3t0
LSYM(x230)	t0__9a0		! t0__5t0	! b_e_5t0	! t0__t0_a0
LSYM(x231)	t0__9a0		! t0__2t0_a0	! b_e_3t0	! t0__4t0_a0
LSYM(x232)	t0__3a0		! t0__2t0_a0	! b_e_8t0	! t0__4t0_a0
LSYM(x233)	t0__3a0		! t0__2t0_a0	! b_e_8t0a0	! t0__4t0_a0
LSYM(x234)	t0__3a0		! t0__4t0_a0	! b_e_2t0	! t0__9t0
LSYM(x235)	t0__3a0		! t0__4t0_a0	! b_e_2t0a0	! t0__9t0
LSYM(x236)	t0__9a0		! t0__2t0_a0	! b_e_4t08a0	! t0__3t0
LSYM(x237)	t0__16a0	! t0__5t0	! b_e_3t0	! t0__t0ma0
LSYM(x238)	t0__3a0		! t0__4t0_a0	! b_e_2t04a0	! t0__9t0
LSYM(x239)	t0__16a0	! t0__5t0	! b_e_t0ma0	! t0__3t0
LSYM(x240)	t0__9a0		! t0__t0_a0	! b_e_8t0	! t0__3t0
LSYM(x241)	t0__9a0		! t0__t0_a0	! b_e_8t0a0	! t0__3t0
LSYM(x242)	t0__5a0		! t0__3t0	! b_e_2t0	! t0__8t0_a0
LSYM(x243)	t0__9a0		! t0__9t0	! b_e_t0	! t0__3t0
LSYM(x244)	t0__5a0		! t0__3t0	! b_e_4t0	! t0__4t0_a0
LSYM(x245)	t0__8a0		! t0__3t0	! b_e_5t0	! t0__2t0_a0
LSYM(x246)	t0__5a0		! t0__8t0_a0	! b_e_2t0	! t0__3t0
LSYM(x247)	t0__5a0		! t0__8t0_a0	! b_e_2t0a0	! t0__3t0
LSYM(x248)	t0__32a0	! t0__t0ma0	! b_e_shift	! r__r_8t0
LSYM(x249)	t0__32a0	! t0__t0ma0	! b_e_t0	! t0__8t0_a0
LSYM(x250)	t0__5a0		! t0__5t0	! b_e_2t0	! t0__5t0
LSYM(x251)	t0__5a0		! t0__5t0	! b_e_2t0a0	! t0__5t0
LSYM(x252)	t0__64a0	! t0__t0ma0	! b_e_shift	! r__r_4t0
LSYM(x253)	t0__64a0	! t0__t0ma0	! b_e_t0	! t0__4t0_a0
LSYM(x254)	t0__128a0	! t0__t0ma0	! b_e_shift	! r__r_2t0
LSYM(x255)	t0__256a0	! a1_ne_0_b_l0	! t0__t0ma0	! b_n_ret_t0
/*1040 insts before this.  */
LSYM(ret_t0)	MILLIRET
LSYM(e_t0)	r__r_t0
LSYM(e_shift)	a1_ne_0_b_l2
	a0__256a0	/* a0 <<= 8 *********** */
	MILLIRETN
LSYM(e_t0ma0)	a1_ne_0_b_l0
	t0__t0ma0
	MILLIRET
	r__r_t0
LSYM(e_t0a0)	a1_ne_0_b_l0
	t0__t0_a0
	MILLIRET
	r__r_t0
LSYM(e_t02a0)	a1_ne_0_b_l0
	t0__t0_2a0
	MILLIRET
	r__r_t0
LSYM(e_t04a0)	a1_ne_0_b_l0
	t0__t0_4a0
	MILLIRET
	r__r_t0
LSYM(e_2t0)	a1_ne_0_b_l1
	r__r_2t0
	MILLIRETN
LSYM(e_2t0a0)	a1_ne_0_b_l0
	t0__2t0_a0
	MILLIRET
	r__r_t0
LSYM(e2t04a0)	t0__t0_2a0
	a1_ne_0_b_l1
	r__r_2t0
	MILLIRETN
LSYM(e_3t0)	a1_ne_0_b_l0
	t0__3t0
	MILLIRET
	r__r_t0
LSYM(e_4t0)	a1_ne_0_b_l1
	r__r_4t0
	MILLIRETN
LSYM(e_4t0a0)	a1_ne_0_b_l0
	t0__4t0_a0
	MILLIRET
	r__r_t0
LSYM(e4t08a0)	t0__t0_2a0
	a1_ne_0_b_l1
	r__r_4t0
	MILLIRETN
LSYM(e_5t0)	a1_ne_0_b_l0
	t0__5t0
	MILLIRET
	r__r_t0
LSYM(e_8t0)	a1_ne_0_b_l1
	r__r_8t0
	MILLIRETN
LSYM(e_8t0a0)	a1_ne_0_b_l0
	t0__8t0_a0
	MILLIRET
	r__r_t0

	.procend
	.end
#endif
